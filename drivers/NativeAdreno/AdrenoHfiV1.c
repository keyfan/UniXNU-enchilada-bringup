// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * AdrenoHfiV1.c - freestanding byte-exact model of the enchilada (A630)
 *                 LEGACY GMU HFI v1 transport. See AdrenoHfiV1.h for the
 *                 sha-pinned source provenance and the 7 legacy deltas.
 *
 * HONEST BOUNDARY: this is a HOST-VERIFIED protocol model. It writes only to
 * a caller-owned volatile buffer, calls only caller-supplied notify/wait
 * callbacks, performs NO MMIO, loads NO firmware, starts NO GMU, and consumes
 * NO boot-arg. A future kernel backend must supply the DSB/DMB barriers around
 * the index publications below (marked "barrier:") and the real doorbell MMIO.
 */

#include "AdrenoHfiV1.h"

/* ---- legacy v1 startup (a6xx_hfi.c:708-741 a6xx_hfi_start_v1) ------------ */
static const AdrenoHfiV1StepDesc gStartup[kAdrenoHfiV1StepCount] = {
    { ADRENO_HFI_V1_H2F_INIT,       0, 0u },                          /* boot_state varies     */
    { ADRENO_HFI_V1_H2F_FW_VERSION, 1, ADRENO_HFI_V1_SUPPORTED_VERSION }, /* 0x100A0000    */
    { ADRENO_HFI_V1_H2F_PERF_TABLE, 0, 0u },                          /* perf_table_v1         */
    { ADRENO_HFI_V1_H2F_BW_TABLE,   0, 0u },                          /* a6xx_build_bw_table  */
    { ADRENO_HFI_V1_H2F_TEST,       0, 0u },                          /* last msg until boot  */
};

static AdrenoHfiV1Status poison(AdrenoHfiV1 *h, AdrenoHfiV1Status s)
{
    h->poisoned = 1u;
    return s;
}

/* Table header + both queue headers. Contrast upstream modern
 * kits/AdrenoFirmware/AdrenoHfi.c:26 which ALSO enforces (*read&3)||(*write&3);
 * legacy HFI v1 has NO 4-dword index alignment, so we check bounds only. */
static AdrenoHfiV1Status validate(AdrenoHfiV1 *h)
{
    volatile uint32_t *m;
    uint32_t q;

    if (h == NULL) {
        return AdrenoHfiV1Invalid;
    }
    if (h->poisoned || h->shared == NULL) {
        return AdrenoHfiV1Poisoned;
    }
    m = h->shared;
    if (m[0] != 0u || m[1] != ADRENO_HFI_V1_TABLE_SIZE_BYTES ||
        m[2] != ADRENO_HFI_V1_QHDR0_OFFSET_WORDS ||
        m[3] != ADRENO_HFI_V1_QHDR_SIZE_WORDS ||
        m[4] != ADRENO_HFI_V1_QUEUE_COUNT ||
        m[5] != ADRENO_HFI_V1_QUEUE_COUNT) {
        return poison(h, AdrenoHfiV1Corrupt);
    }
    for (q = 0u; q < ADRENO_HFI_V1_QUEUE_COUNT; ++q) {
        volatile uint32_t *v = m + ADRENO_HFI_V1_QHDR0_OFFSET_WORDS
                               + q * ADRENO_HFI_V1_QUEUE_HEADER_WORDS;
        const uint32_t type = (q == 0u) ? ADRENO_HFI_V1_CMD_QUEUE_TYPE
                                       : ADRENO_HFI_V1_RESP_QUEUE_TYPE;
        uint32_t read;
        uint32_t write;

        if (v[0] != 1u ||
            v[1] != h->iova + (q + 1u) * ADRENO_HFI_V1_PAGE_BYTES ||
            v[2] != type ||
            v[3] != ADRENO_HFI_V1_QUEUE_WORDS ||
            v[4] != 0u) {
            return poison(h, AdrenoHfiV1Corrupt);
        }
        read = v[10];
        write = v[11];
        /* LEGACY: bounds only - NO (*read & 3) / (*write & 3) rejection. */
        if (read >= ADRENO_HFI_V1_QUEUE_WORDS ||
            write >= ADRENO_HFI_V1_QUEUE_WORDS) {
            return poison(h, AdrenoHfiV1Corrupt);
        }
    }
    return AdrenoHfiV1Success;
}

static volatile uint32_t *queue_header(AdrenoHfiV1 *h, uint32_t q)
{
    return h->shared + ADRENO_HFI_V1_QHDR0_OFFSET_WORDS
           + q * ADRENO_HFI_V1_QUEUE_HEADER_WORDS;
}

/* queue 0 data lives at byte SZ_4K, queue 1 at byte 2*SZ_4K
 * (a6xx_hfi.c:847-854). */
static volatile uint32_t *queue_data(AdrenoHfiV1 *h, uint32_t q)
{
    return h->shared + (q + 1u) * (ADRENO_HFI_V1_PAGE_BYTES / 4u);
}

AdrenoHfiV1Status AdrenoHfiV1InitializeStopped(AdrenoHfiV1 *h,
    volatile uint32_t *shared, size_t shared_bytes, uint32_t iova,
    void *cookie, int (*notify)(void *))
{
    volatile uint32_t *t;
    uint32_t i;
    uint32_t q;

    if (h == NULL || shared == NULL) {
        return AdrenoHfiV1Invalid;
    }
    if (shared_bytes != ADRENO_HFI_V1_SHARED_BYTES) {
        return AdrenoHfiV1Invalid;
    }
    if ((iova & (ADRENO_HFI_V1_PAGE_BYTES - 1u)) != 0u || notify == NULL) {
        return AdrenoHfiV1Invalid;
    }

    /* Deterministic stopped state: zero the whole 3-page extent. */
    for (i = 0u; i < ADRENO_HFI_V1_SHARED_BYTES / 4u; ++i) {
        shared[i] = 0u;
    }
    /* The handle MUST be wired before the loop: queue_header()/queue_data()
     * dereference h->shared. Wiring it only at the end would write through a
     * NULL pointer (caught by the host test, not by any device). */
    h->shared = shared;
    h->iova = iova;
    h->sequence = 0u;
    h->pending_header = 0u;
    h->poisoned = 0u;
    h->cookie = cookie;
    h->notify = notify;
    h->skipped = 0u;
    h->skipped_header = 0u;
    h->skipped_ret = 0u;
    h->skipped_code = 0u;

    t = shared;

    /* a6xx_hfi.c:838-844 - table header. Legacy has 2 queues, so
     * table_size = 24 + 2*48 = 120 (modern AdrenoHfi.c:20 pins 168 = 3 queues). */
    t[0] = 0u;                                      /* version              */
    t[1] = ADRENO_HFI_V1_TABLE_SIZE_BYTES;          /* size (bytes)         */
    t[2] = ADRENO_HFI_V1_QHDR0_OFFSET_WORDS;        /* sizeof(table) >> 2   */
    t[3] = ADRENO_HFI_V1_QHDR_SIZE_WORDS;           /* sizeof(qhdr)   >> 2  */
    t[4] = ADRENO_HFI_V1_QUEUE_COUNT;               /* num_queues = 2       */
    t[5] = ADRENO_HFI_V1_QUEUE_COUNT;               /* active_queues = 2    */

    for (q = 0u; q < ADRENO_HFI_V1_QUEUE_COUNT; ++q) {
        volatile uint32_t *v = queue_header(h, q);
        const uint32_t id = (q == 0u) ? ADRENO_HFI_V1_CMD_QUEUE_ID
                                     : ADRENO_HFI_V1_RESP_QUEUE_ID;
        v[0] = 1u;                                              /* status       */
        v[1] = iova + (q + 1u) * ADRENO_HFI_V1_PAGE_BYTES;      /* data IOVA    */
        v[2] = ADRENO_HFI_V1_QUEUE_TYPE(id);                    /* 0xa00 / 0xa04 */
        v[3] = ADRENO_HFI_V1_QUEUE_WORDS;                       /* 1024 dwords  */
        v[4] = 0u;                                              /* msg_size     */
        v[5] = 0u;                                              /* dropped      */
        v[6] = 1u;                                              /* rx_watermark */
        v[7] = 1u;                                              /* tx_watermark */
        v[8] = 1u;                                              /* rx_request   */
        v[9] = 0u;                                              /* tx_request   */
        v[10] = 0u;                                             /* read_index   */
        v[11] = 0u;                                             /* write_index  */
    }

    return AdrenoHfiV1Success;
}

uint32_t AdrenoHfiV1MakeHeader(uint32_t seqnum, uint32_t count, uint32_t id)
{
    /* a6xx_hfi.c:181 - (seqnum << 20) | (HFI_MSG_CMD << 16) | (dwords << 8) | id
     * HFI_MSG_CMD == 0, so the <<16 term is zero. */
    return ((seqnum & ADRENO_HFI_V1_SEQNUM_MOD) << 20) | ((count & 0xffu) << 8)
           | (id & 0xffu);
}

AdrenoHfiV1Status AdrenoHfiV1Send(AdrenoHfiV1 *h, uint32_t id,
    const uint32_t *payload, size_t payload_words, uint32_t *wire_header)
{
    volatile uint32_t *v;
    volatile uint32_t *data;
    uint32_t read;
    uint32_t write;
    uint32_t space;
    uint32_t count;
    uint32_t seqnum;
    uint32_t header;
    size_t i;
    AdrenoHfiV1Status s = validate(h);

    if (s != AdrenoHfiV1Success) {
        return s;
    }
    if (id > 0xffu || payload_words > (size_t)(ADRENO_HFI_V1_MAX_PACKET_WORDS - 1u)) {
        return AdrenoHfiV1Invalid;
    }
    if (payload_words != 0u && payload == NULL) {
        return AdrenoHfiV1Invalid;
    }

    v = queue_header(h, 0u);
    data = queue_data(h, 0u);
    read = v[10];
    write = v[11];
    count = (uint32_t)payload_words + 1u;

    /* CIRC_SPACE(write, read, size) for a power-of-two ring
     * (a6xx_hfi.c:74-80). A full ring holds size-1 dwords. */
    space = (read - write - 1u) & (ADRENO_HFI_V1_QUEUE_WORDS - 1u);
    if (space < count) {
        v[5] = v[5] + 1u;                 /* dropped++                     */
        return AdrenoHfiV1NoSpace;
    }

    /* a6xx_hfi.c:178 atomic_inc_return(&queue->seqnum) % 0xfff */
    h->sequence = (h->sequence + 1u) % ADRENO_HFI_V1_SEQNUM_MOD;
    seqnum = h->sequence;
    header = AdrenoHfiV1MakeHeader(seqnum, count, id);
    h->pending_header = header;

    data[write] = header;
    for (i = 0u; i < payload_words; ++i) {
        data[(write + 1u + (uint32_t)i) & (ADRENO_HFI_V1_QUEUE_WORDS - 1u)] =
            payload[i];
    }

    /* barrier: dsb oshst before publishing write_index in the real caller. */
    /* LEGACY (a6xx_hfi.c:90-95): write_index advances by EXACTLY count - no
     * 0xfafafafa padding, no ALIGN(index,4). Modern would write to
     * (count + 3) & ~3 and pad the tail with 0xfafafafa. */
    v[11] = (write + count) & (ADRENO_HFI_V1_QUEUE_WORDS - 1u);

    if (wire_header != NULL) {
        *wire_header = header;
    }
    /* a6xx_hfi.c:98 gmu_write(REG_A6XX_GMU_HOST2GMU_INTR_SET, 0x01) */
    if (h->notify(h->cookie) != 0) {
        return AdrenoHfiV1NotifyError;
    }
    return AdrenoHfiV1Success;
}

AdrenoHfiV1Status AdrenoHfiV1Poll(AdrenoHfiV1 *h, uint32_t *payload,
    size_t capacity, size_t *payload_words, uint32_t *firmware_error)
{
    volatile uint32_t *v;
    volatile uint32_t *data;
    uint32_t packet[ADRENO_HFI_V1_MAX_PACKET_WORDS];
    uint32_t read;
    uint32_t write;
    uint32_t hdr;
    uint32_t count;
    uint32_t i;
    size_t n;
    AdrenoHfiV1Status s = validate(h);

    if (s != AdrenoHfiV1Success) {
        return s;
    }
    if (payload_words == NULL || (capacity != 0u && payload == NULL)) {
        return AdrenoHfiV1Invalid;
    }
    *payload_words = 0u;
    if (firmware_error != NULL) {
        *firmware_error = 0u;
    }

    v = queue_header(h, 1u);
    data = queue_data(h, 1u);
    read = v[10];
    write = v[11];
    if (read == write) {
        v[8] = 1u;                        /* rx_request = 1 (a6xx_hfi.c:34) */
        return AdrenoHfiV1Empty;
    }
    hdr = data[read];
    count = ADRENO_HFI_V1_HEADER_SIZE(hdr);
    if (count == 0u || count > ADRENO_HFI_V1_MAX_PACKET_WORDS ||
        count > ADRENO_HFI_V1_QUEUE_WORDS) {
        return poison(h, AdrenoHfiV1Corrupt);   /* upstream uses BUG_ON      */
    }
    for (i = 0u; i < count; ++i) {
        packet[i] = data[(read + i) & (ADRENO_HFI_V1_QUEUE_WORDS - 1u)];
    }
    /* LEGACY (a6xx_hfi.c:59-62): read_index advances by EXACTLY count, with
     * NO ALIGN(index, 4) % header->size. */
    v[10] = (read + count) & (ADRENO_HFI_V1_QUEUE_WORDS - 1u);

    /* F2H error message (id 100): skip and report, upstream :139-146. */
    if (ADRENO_HFI_V1_HEADER_ID(packet[0]) == ADRENO_HFI_V1_F2H_MSG_ERROR) {
        h->skipped += 1u;
        h->skipped_header = packet[0];
        h->skipped_ret = 0u;
        h->skipped_code = (count >= 2u) ? packet[1] : 0u;
        if (firmware_error != NULL) {
            *firmware_error = h->skipped_code;
        }
        return AdrenoHfiV1FirmwareError;
    }

    /* ACK matched on the seqnum of ret_header, NOT on the F2H header id
     * (a6xx_hfi.c:148-153). Contrast AdrenoHfi.c:84 (packet[1]>>20). */
    if (count < 3u ||
        ADRENO_HFI_V1_HEADER_SEQNUM(packet[1]) !=
            ADRENO_HFI_V1_HEADER_SEQNUM(h->pending_header)) {
        h->skipped += 1u;
        h->skipped_header = packet[0];
        h->skipped_ret = (count >= 2u) ? packet[1] : 0u;
        return AdrenoHfiV1Unexpected;
    }
    /* resp.error -> -EINVAL (a6xx_hfi.c:155-160). */
    if (packet[2] != 0u) {
        h->skipped += 1u;
        h->skipped_header = packet[0];
        h->skipped_ret = packet[1];
        h->skipped_code = packet[2];
        if (firmware_error != NULL) {
            *firmware_error = packet[2];
        }
        return AdrenoHfiV1FirmwareError;
    }

    /* payload begins at dword 3: struct a6xx_hfi_msg_response
     * { header; ret_header; error; payload[HFI_RESPONSE_PAYLOAD_SIZE]; }. */
    n = (size_t)count - 3u;
    if (n > capacity) {
        n = capacity;
    }
    for (i = 0u; i < n; ++i) {
        payload[i] = packet[3u + i];
    }
    *payload_words = n;
    return AdrenoHfiV1Success;
}

AdrenoHfiV1Status AdrenoHfiV1Exchange(AdrenoHfiV1 *h, uint32_t id,
    const uint32_t *payload, size_t payload_words, uint32_t poll_budget,
    int (*wait_once)(void *), uint32_t *reply, size_t capacity,
    size_t *reply_words)
{
    uint32_t ignored_header;
    uint32_t firmware_error;
    uint32_t budget;
    AdrenoHfiV1Status s = AdrenoHfiV1Send(h, id, payload, payload_words,
                                         &ignored_header);

    if (s != AdrenoHfiV1Success) {
        return s;
    }
    if (wait_once == NULL || reply_words == NULL) {
        return AdrenoHfiV1Invalid;
    }
    *reply_words = 0u;
    if (poll_budget == 0u) {
        return AdrenoHfiV1Timeout;
    }
    for (budget = 0u; budget < poll_budget; ++budget) {
        size_t n = 0u;
        s = AdrenoHfiV1Poll(h, reply, capacity, &n, &firmware_error);
        if (s == AdrenoHfiV1Success) {
            *reply_words = n;
            return AdrenoHfiV1Success;
        }
        if (s == AdrenoHfiV1Poisoned || s == AdrenoHfiV1Corrupt ||
            s == AdrenoHfiV1Invalid) {
            return s;
        }
        /* Empty / Unexpected / FirmwareError (skipped F2H errors) mirror
         * upstream a6xx_hfi_wait_for_ack's "continue" behaviour. */
        if (wait_once(h->cookie) != 0) {
            return AdrenoHfiV1WaitError;
        }
    }
    return AdrenoHfiV1Timeout;
}

const AdrenoHfiV1StepDesc *AdrenoHfiV1StartupTable(void)
{
    return gStartup;
}

uint32_t AdrenoHfiV1StartupStepCount(void)
{
    return (uint32_t)kAdrenoHfiV1StepCount;
}

const char *AdrenoHfiV1StepName(AdrenoHfiV1Step step)
{
    switch (step) {
    case kAdrenoHfiV1StepInit:      return "INIT";
    case kAdrenoHfiV1StepFwVersion: return "FW_VERSION";
    case kAdrenoHfiV1StepPerfTable: return "PERF_TABLE_V1";
    case kAdrenoHfiV1StepBwTable:   return "BW_TABLE";
    case kAdrenoHfiV1StepTest:      return "TEST";
    default:                        return "UNKNOWN";
    }
}

int AdrenoHfiV1IsLegacyStart(void)
{
    return 1;
}
