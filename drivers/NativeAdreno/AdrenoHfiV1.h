// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * AdrenoHfiV1.h - enchilada (SDM845 / Adreno A630) LEGACY GMU HFI v1
 *                 transport model.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * Upstream UniXNU kits/AdrenoFirmware/AdrenoHfi.h:23 says
 *   "Modern (nonlegacy) GMU HFI only."
 * mainline Linux v6.12 a6xx_gmu.c:1736-1738 marks the A630 family
 *   HFI v1, has sptprac: gmu->legacy = true;
 * so the modern transport (3 queues, 4-dword index alignment, 0xfafafafa
 * write padding, response-queue id 1, CORE_FW_START + START) CANNOT be used
 * for enchilada. This module models the LEGACY HFI v1 wire protocol as a
 * freestanding, pure-C, byte-exact ring encoder/decoder. It is a HOST-VERIFIED
 * protocol/transport component - NOT a GPU driver: it performs no MMIO, loads
 * no firmware, starts no GMU, and consumes no boot-arg. Its notify/wait
 * callbacks stand in for the GMU doorbell and interrupt poll that a future
 * kernel caller must supply (together with the required DSB/DMB barriers).
 *
 * SOURCE PROVENANCE (all sha-pinned / jsdelivr Linux v6.12)
 * ---------------------------------------------------------
 *  a6xx_hfi.c  sha256 f62c802d25475b86ac05cc58df012e088230ed64a0e9319456eb20ebb40c9faa
 *    :28-64   queue_read  - legacy SKIPS  :59-60 ALIGN(index,4)
 *    :66-100  queue_write - legacy SKIPS  :90-93 0xfafafafa padding
 *    :171-192 send_msg    - header = (seqnum<<20)|(dwords<<8)|id
 *    :206-215 get_fw_version - supported_version = (1<<28)|(1<<19)|(1<<17)
 *    :708-741 start_v1    - INIT/FW_VERSION/PERF_TABLE/BW_TABLE/TEST only
 *    :743-772 start()     - :747 if (gmu->legacy) return start_v1(...)
 *    :795-820 queue_init  - size=SZ_4K>>2, type=10<<8|id, status=1
 *    :822-854 hfi_init    - qhdr0_offset=6, qhdr_size=12, 2 queues,
 *                           :853 response queue id = legacy ? 4 : 1
 *  a6xx_hfi.h (https://cdn.jsdelivr.net/gh/torvalds/linux@v6.12/drivers/gpu/drm/msm/adreno/a6xx_hfi.h)
 *    queue_table_header 6 u32 / queue_header 12 u32; HFI_HEADER_ID/SIZE/SEQNUM;
 *    HFI_MSG_CMD 0, HFI_MSG_ACK 1, HFI_MSG_ACK_V1 2, HFI_F2H_MSG_ACK 126,
 *    HFI_F2H_MSG_ERROR 100; HFI_H2F_MSG_INIT 0/FW_VERSION 1/BW_TABLE 3/
 *    PERF_TABLE 4/TEST 5/START 10/CORE_FW_START 14.
 *  a6xx_gmu.h  - struct a6xx_gmu { ... queues[2]; ... bool legacy; a618 or a630 }.
 *
 * THE SEVEN LEGACY-vs-MODERN DELTAS ENCODED BELOW
 *  1. 2 queues (not 3): num_queues = active_queues = 2.
 *  2. table size 120 B (24 + 2*48), qhdr0_offset 6, qhdr_size 12.
 *  3. response queue type 0xa04 (id 4), modern would be 0xa01 (id 1).
 *  4. NO 4-dword align of read/write index, NO 0xfafafafa padding.
 *  5. startup v1 = INIT -> FW_VERSION -> PERF_TABLE -> BW_TABLE -> TEST,
 *     NO CORE_FW_START, NO START.
 *  6. supported_version = (1<<28)|(1<<19)|(1<<17) = 0x100A0000 (v1.10).
 *  7. wire header (seqnum<<20)|(count<<8)|id; seqnum % 0xfff; ACK matched via
 *     HFI_HEADER_SEQNUM(ret_header); F2H error id 100 skipped; resp.error -> error.
 */

#ifndef ADRENO_HFI_V1_H
#define ADRENO_HFI_V1_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- queue / table geometry (legacy: 2 queues) -------------------------- */
#define ADRENO_HFI_V1_QUEUE_COUNT          2u
#define ADRENO_HFI_V1_QUEUE_WORDS         1024u          /* SZ_4K >> 2      */
#define ADRENO_HFI_V1_TABLE_HEADER_WORDS  6u             /* 24 B / 4        */
#define ADRENO_HFI_V1_QUEUE_HEADER_WORDS  12u            /* 48 B / 4        */
#define ADRENO_HFI_V1_TABLE_SIZE_BYTES    120u           /* 24 + 2*48       */
#define ADRENO_HFI_V1_QHDR0_OFFSET_WORDS  6u             /* sizeof(table)>>2 */
#define ADRENO_HFI_V1_QHDR_SIZE_WORDS     12u            /* sizeof(qhdr)>>2  */
#define ADRENO_HFI_V1_PAGE_BYTES          4096u
#define ADRENO_HFI_V1_SHARED_BYTES        (3u * ADRENO_HFI_V1_PAGE_BYTES) /* 12288 */
#define ADRENO_HFI_V1_MAX_PACKET_WORDS    64u

/* queue ids: index 0 = command, index 1 = response. The TYPE field of the
 * response queue is id 4 for legacy (a6xx_hfi.c:853), NOT id 1 (modern). */
#define ADRENO_HFI_V1_CMD_QUEUE_ID        0u
#define ADRENO_HFI_V1_RESP_QUEUE_ID      4u             /* legacy; modern = 1 */
#define ADRENO_HFI_V1_QUEUE_TYPE(id)      (0xa00u | (id))
#define ADRENO_HFI_V1_CMD_QUEUE_TYPE      ADRENO_HFI_V1_QUEUE_TYPE(ADRENO_HFI_V1_CMD_QUEUE_ID)
#define ADRENO_HFI_V1_RESP_QUEUE_TYPE     ADRENO_HFI_V1_QUEUE_TYPE(ADRENO_HFI_V1_RESP_QUEUE_ID)

/* ---- wire header (a6xx_hfi.c:181, a6xx_hfi.h macros) -------------------- */
#define ADRENO_HFI_V1_SEQNUM_MOD          0xfffu
#define ADRENO_HFI_V1_HEADER_ID(h)        ((uint32_t)(h) & 0xffu)
#define ADRENO_HFI_V1_HEADER_SIZE(h)      (((uint32_t)(h) >> 8) & 0xffu)
#define ADRENO_HFI_V1_HEADER_SEQNUM(h)    (((uint32_t)(h) >> 20) & 0xfffu)
#define ADRENO_HFI_V1_MSG_CMD             0u
#define ADRENO_HFI_V1_MSG_ACK            1u
#define ADRENO_HFI_V1_MSG_ACK_V1         2u             /* legacy ACK marker */
#define ADRENO_HFI_V1_F2H_MSG_ACK        126u
#define ADRENO_HFI_V1_F2H_MSG_ERROR      100u

/* ---- H2F message ids (a6xx_hfi.h) ---------------------------------------- */
#define ADRENO_HFI_V1_H2F_INIT            0u
#define ADRENO_HFI_V1_H2F_FW_VERSION      1u
#define ADRENO_HFI_V1_H2F_BW_TABLE        3u
#define ADRENO_HFI_V1_H2F_PERF_TABLE      4u
#define ADRENO_HFI_V1_H2F_TEST            5u
#define ADRENO_HFI_V1_H2F_START           10u           /* modern only       */
#define ADRENO_HFI_V1_H2F_CORE_FW_START   14u           /* modern only       */

/* a6xx_hfi.c:211 "Currently supporting version 1.10" */
#define ADRENO_HFI_V1_SUPPORTED_VERSION   0x100A0000u   /* (1<<28)|(1<<19)|(1<<17) */

/* ---- status -------------------------------------------------------------- */
typedef enum {
    AdrenoHfiV1Success = 0,
    AdrenoHfiV1Invalid,
    AdrenoHfiV1NoSpace,
    AdrenoHfiV1Empty,
    AdrenoHfiV1Corrupt,
    AdrenoHfiV1Unexpected,
    AdrenoHfiV1FirmwareError,
    AdrenoHfiV1NotifyError,
    AdrenoHfiV1WaitError,
    AdrenoHfiV1Timeout,
    AdrenoHfiV1Poisoned
} AdrenoHfiV1Status;

/* ---- legacy v1 startup sequence (a6xx_hfi.c:708-741 start_v1) ---------- */
typedef enum {
    kAdrenoHfiV1StepInit = 0,
    kAdrenoHfiV1StepFwVersion = 1,
    kAdrenoHfiV1StepPerfTable = 2,
    kAdrenoHfiV1StepBwTable = 3,
    kAdrenoHfiV1StepTest = 4,
    kAdrenoHfiV1StepCount = 5
} AdrenoHfiV1Step;

typedef struct {
    uint32_t msg_id;        /* H2F id for this step                         */
    int      has_payload0;  /* 1 when payload0 is a defined constant        */
    uint32_t payload0;      /* first payload dword, when well-defined       */
} AdrenoHfiV1StepDesc;

typedef struct {
    volatile uint32_t *shared;   /* ADRENO_HFI_V1_SHARED_BYTES, 4K-aligned  */
    uint32_t iova;               /* 32-bit GMU IOVA of shared[0]            */
    uint32_t sequence;           /* last seqnum handed out                   */
    uint32_t pending_header;     /* header of the message awaiting an ACK    */
    uint32_t poisoned;
    void *cookie;
    int (*notify)(void *);       /* stands in for HOST2GMU_INTR_SET write    */
    uint32_t skipped;            /* responses ignored                        */
    uint32_t skipped_header;
    uint32_t skipped_ret;
    uint32_t skipped_code;       /* F2H_MSG_ERROR code                       */
} AdrenoHfiV1;

/* Lay out the 2-queue legacy table in a zeroed shared buffer. No hardware. */
AdrenoHfiV1Status AdrenoHfiV1InitializeStopped(AdrenoHfiV1 *h,
    volatile uint32_t *shared, size_t shared_bytes, uint32_t iova,
    void *cookie, int (*notify)(void *));

/* (seqnum<<20)|(count<<8)|id - a6xx_hfi.c:181 */
uint32_t AdrenoHfiV1MakeHeader(uint32_t seqnum, uint32_t count, uint32_t id);

/* Enqueue into the command queue. Legacy: exactly count dwords, no padding,
 * no 4-align of write_index. Calls notify(cookie) as the GMU doorbell. */
AdrenoHfiV1Status AdrenoHfiV1Send(AdrenoHfiV1 *h, uint32_t id,
    const uint32_t *payload, size_t payload_words, uint32_t *wire_header);

/* Dequeue one response. Legacy: no 4-align of read_index. Skips F2H error
 * (id 100) and seqnum mismatches; copies count-3 payload dwords on success. */
AdrenoHfiV1Status AdrenoHfiV1Poll(AdrenoHfiV1 *h, uint32_t *payload,
    size_t capacity, size_t *payload_words, uint32_t *firmware_error);

/* Send + poll until a matching ACK, with a bounded wait loop. */
AdrenoHfiV1Status AdrenoHfiV1Exchange(AdrenoHfiV1 *h, uint32_t id,
    const uint32_t *payload, size_t payload_words, uint32_t poll_budget,
    int (*wait_once)(void *), uint32_t *reply, size_t capacity,
    size_t *reply_words);

/* The exact 5-step legacy startup table, in order. */
const AdrenoHfiV1StepDesc *AdrenoHfiV1StartupTable(void);
uint32_t AdrenoHfiV1StartupStepCount(void);
const char *AdrenoHfiV1StepName(AdrenoHfiV1Step step);
/* This module implements EXACTLY the legacy branch of a6xx_hfi_start(). */
int AdrenoHfiV1IsLegacyStart(void);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_HFI_V1_H */
