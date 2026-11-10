// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoGmuBootV1.h -- enchilada stage 5 (GPU) HOST-VERIFIED A630 LEGACY GMU
// cold-boot bring-up SEQUENCE model (round 42, 2026-11-10).
//
// This is NOT a GPU driver. It is a freestanding, pure-C encoding of the
// ordered MMIO operation stream a future A6XX_GEN1 kernel backend must issue
// to cold-boot the A630 GMU (6xx GEN1, legacy, HFI v1, SPTPRAC). It maps
// nothing and touches no hardware: every read/write/poll is routed through
// caller-supplied callbacks (the same stand-in pattern as AdrenoHfiV1), so
// the host test can replay the sequence against a synthetic bus and pin the
// EXACT register plan upstream issues.
//
// SOURCE PROVENANCE (all sha-pinned; see scripts/data/adreno-a630-inputs.json
// for the Linux v6.12 files and scripts/data/adreno-a630-pdc-inputs.json for
// the PDC/RSCC offsets from 3 independent kernel trees):
//   a6xx_gmu.c:530-655   a6xx_gmu_rpmh_init()  - RSC + PDC sequencer setup
//                        (A630: pdc_in_aop=false, pdc_address_offset=0x30080,
//                        legacy uCode tables, TCS1/TCS3 command setup).
//   a6xx_gmu.c:480-520   a6xx_rpmh_start()/a6xx_rpmh_stop() - RSCC handshake.
//   a6xx_gmu.c:204-240   a6xx_gmu_start() - CM3 reset + FW_INIT_RESULT poll
//                        with the DTCM-snapshot-dependent mask/reset pair.
//   a6xx_gmu.c:242-255   a6xx_gmu_hfi_start() - HFI_CTRL_INIT + status poll.
//   a6xx_gmu.c:265-344   OOB table + a6xx_gmu_set_oob() (legacy set/ack).
//   a6xx_gmu.c:365-406   a6xx_sptprac_enable()/disable() (legacy-only).
//   a6xx_gmu.c:409-424   a6xx_gmu_gfx_rail_on() (legacy-only).
//   a6xx_gmu.c:663-706   a6xx_gmu_power_config() + GMU_PWR_COL_HYST.
//   a6xx_gmu.c:725-779   a6xx_gmu_fw_load() legacy flat ITCM bulk write.
//   a6xx_gmu.c:781-902   a6xx_gmu_fw_start() cold path, fence, chipid massage.
//   a6xx_gmu.c:1012-1103 a6xx_gmu_resume() + cold/warm decision (:1066-1068).
//   a6xx_gmu.xml (registers/adreno/, word offsets): GMU register map.
//   a6xx_gmu.h:28-45     GMU_WARM_BOOT 0 / GMU_COLD_BOOT 1 /
//                        GMU_IDLE_STATE_ACTIVE 0 / _SPTP 2 / _IFPC 3.
//
// HONEST BOUNDARIES (2026-11-10, load-bearing):
//   - HOST-VERIFIED ONLY: sha-pinned source reading, NOT a device measurement.
//     gpu stays host-verified. A CI green build is a build artifact.
//   - The caller must ALREADY have done the AdrenoPowerGate 9-step power-up
//     (CX/GX genpd, clocks, RPMh rails) BEFORE the first op below; upstream
//     relies on pm_runtime/clk_bulk which this module does not model.
//   - The DTCM-snapshot mask/reset branch (:211-218) reads REAL hardware state
//     at boot; the model exposes it as a pure predicate the caller feeds.
//   - gmu_write_bulk (ITCM) is modelled as ONE plan op carrying bulk_words;
//     the per-word bus traffic is the caller's DMA/copy choice.
//   - TrustZone PAS auth of the vendor-signed ZAP chain (GPU_PAS_ID 13) and
//     the a6xx_hw_init() GPU-register sequence are OUT OF SCOPE here.

#ifndef ADRENO_GMU_BOOT_V1_H
#define ADRENO_GMU_BOOT_V1_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- GMU MMIO word offsets (registers/adreno/a6xx_gmu.xml) --------------- */
#define ADRENO_GMU_BOOT_V1_SPTPRAC_POWER_CONTROL   0x0081u
#define ADRENO_GMU_BOOT_V1_CM3_ITCM_START          0x0c00u
#define ADRENO_GMU_BOOT_V1_CM3_DTCM_START          0x1c00u
#define ADRENO_GMU_BOOT_V1_BOOT_SLUMBER_OPTION     0x23f8u
#define ADRENO_GMU_BOOT_V1_GX_VOTE_IDX             0x23f9u
#define ADRENO_GMU_BOOT_V1_MX_VOTE_IDX             0x23fau
#define ADRENO_GMU_BOOT_V1_ICACHE_CONFIG           0x4c00u
#define ADRENO_GMU_BOOT_V1_DCACHE_CONFIG           0x4c01u
#define ADRENO_GMU_BOOT_V1_SYS_BUS_CONFIG          0x4c0fu
#define ADRENO_GMU_BOOT_V1_CM3_SYSRESET            0x5000u
#define ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG         0x5001u
#define ADRENO_GMU_BOOT_V1_CM3_FW_INIT_RESULT      0x501cu
#define ADRENO_GMU_BOOT_V1_CM3_CFG                 0x502du
#define ADRENO_GMU_BOOT_V1_PWR_COL_INTER_FRAME_CTRL 0x50c0u
#define ADRENO_GMU_BOOT_V1_PWR_COL_INTER_FRAME_HYST 0x50c1u
#define ADRENO_GMU_BOOT_V1_PWR_COL_SPTPRAC_HYST    0x50c2u
#define ADRENO_GMU_BOOT_V1_SPTPRAC_PWR_CLK_STATUS  0x50d0u
#define ADRENO_GMU_BOOT_V1_RPMH_CTRL               0x50e8u
#define ADRENO_GMU_BOOT_V1_PWR_COL_CP_MSG          0x5100u
#define ADRENO_GMU_BOOT_V1_PWR_COL_CP_RESP         0x5101u
#define ADRENO_GMU_BOOT_V1_HFI_CTRL_STATUS         0x5180u
#define ADRENO_GMU_BOOT_V1_HFI_SFR_ADDR            0x5182u
#define ADRENO_GMU_BOOT_V1_HFI_QTBL_INFO           0x5184u
#define ADRENO_GMU_BOOT_V1_HFI_QTBL_ADDR           0x5185u
#define ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT           0x5186u
#define ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_CLR       0x5191u
#define ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_INFO      0x5192u
#define ADRENO_GMU_BOOT_V1_HOST2GMU_INTR_SET       0x5194u
#define ADRENO_GMU_BOOT_V1_GENERAL_7               0x51ccu
#define ADRENO_GMU_BOOT_V1_AO_HOST_INTERRUPT_CLR   0x9304u
#define ADRENO_GMU_BOOT_V1_AO_HOST_INTERRUPT_MASK  0x9306u
#define ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ        0x9307u
#define ADRENO_GMU_BOOT_V1_RSCC_CONTROL_ACK        0x9308u
#define ADRENO_GMU_BOOT_V1_AHB_FENCE_RANGE_0       0x9311u
#define ADRENO_GMU_BOOT_V1_GPU_CC_GX_GDSCR         0x9c03u

/* ---- RSCC domain (base = GMU mmio + 0x23000, dword-addressed <<2) -------- */
#define ADRENO_GMU_BOOT_V1_GMU_RSCC_DELTA          0x23000u
#define ADRENO_GMU_BOOT_V1_RSCC_STATUS0_DRV0       0x0004u
#define ADRENO_GMU_BOOT_V1_RSCC_PDC_SEQ_START_ADDR 0x0008u
#define ADRENO_GMU_BOOT_V1_RSCC_PDC_MATCH_VALUE_LO 0x0009u
#define ADRENO_GMU_BOOT_V1_RSCC_PDC_MATCH_VALUE_HI 0x000au
#define ADRENO_GMU_BOOT_V1_RSCC_PDC_SLAVE_ID_DRV0  0x000bu
#define ADRENO_GMU_BOOT_V1_RSCC_HIDDEN_TCS_CMD0_ADDR 0x000du
#define ADRENO_GMU_BOOT_V1_RSCC_HIDDEN_TCS_CMD0_DATA 0x000eu
#define ADRENO_GMU_BOOT_V1_RSCC_OVERRIDE_START_ADDR 0x0100u
#define ADRENO_GMU_BOOT_V1_RSCC_SEQ_BUSY_DRV0      0x0101u
#define ADRENO_GMU_BOOT_V1_RSCC_SEQ_MEM_0_DRV0     0x0180u

/* ---- PDC domain (gmu_pdc / gmu_pdc_seq MMIO, dword-addressed <<2) --------
 * Offsets sha-pinned from 3 independent kernel trees
 * (scripts/data/adreno-a630-pdc-inputs.json, modern generation). */
#define ADRENO_GMU_BOOT_V1_PDC_SEQ_MEM_0           0x0000u
#define ADRENO_GMU_BOOT_V1_PDC_ENABLE_PDC          0x1140u
#define ADRENO_GMU_BOOT_V1_PDC_SEQ_START_ADDR      0x1148u
#define ADRENO_GMU_BOOT_V1_PDC_TCS0_CONTROL        0x1540u
#define ADRENO_GMU_BOOT_V1_PDC_TCS1_CONTROL        0x1572u
#define ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD_ENABLE_BANK 0x1573u
#define ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD_WAIT_BANK  0x1574u
#define ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_MSGID     0x1575u
#define ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_ADDR      0x1576u
#define ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD0_DATA      0x1577u
#define ADRENO_GMU_BOOT_V1_PDC_TCS3_CONTROL        0x15d6u
#define ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD_ENABLE_BANK 0x15d7u
#define ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD_WAIT_BANK  0x15d8u
#define ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_MSGID     0x15d9u
#define ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_ADDR      0x15dau
#define ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD0_DATA      0x15dbu

/* ---- A630 legacy constants (a6xx_gmu.c line anchors in provenance) ------- */
#define ADRENO_GMU_BOOT_V1_PDC_ADDRESS_OFFSET      0x30080u /* :552          */
#define ADRENO_GMU_BOOT_V1_PWR_COL_HYST            0x000a1680u /* :663       */
#define ADRENO_GMU_BOOT_V1_PWR_COL_IFPC_CTRL       0x09c40400u /* :680       */
#define ADRENO_GMU_BOOT_V1_RPMH_VOTE_BITS          0x00000f11u /* :699-705   */
#define ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG_VALUE   0x02u    /* :822          */
#define ADRENO_GMU_BOOT_V1_CM3_CFG_VALUE           0x4052u  /* :845          */
#define ADRENO_GMU_BOOT_V1_FENCE_UPPER             0xau     /* :832          */
#define ADRENO_GMU_BOOT_V1_FENCE_LOWER             0xa0u    /* :833          */
#define ADRENO_GMU_BOOT_V1_SPTPRAC_ON_VALUE        0x00778000u /* :373       */
#define ADRENO_GMU_BOOT_V1_SPTPRAC_OFF_VALUE       0x00778001u /* :398       */
#define ADRENO_GMU_BOOT_V1_SPTPRAC_CLK_MASK        0x38u    /* :376          */
#define ADRENO_GMU_BOOT_V1_SPTPRAC_CLK_PATTERN     0x28u    /* :376          */
#define ADRENO_GMU_BOOT_V1_GX_RETENTION_BIT        0x00000800u /* :396 1<<11 */
#define ADRENO_GMU_BOOT_V1_IRQ_MASK                0x00000029u /* :907-910   */
#define ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_SET    22u      /* :288          */
#define ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_ACK    30u      /* :289          */
#define ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_CLEAR  30u      /* :290          */
#define ADRENO_GMU_BOOT_V1_TCS_MSGID               0x10108u /* :611,625      */
#define ADRENO_GMU_BOOT_V1_TCS_CMD0_ADDR           0x30010u /* :612,626      */
#define ADRENO_GMU_BOOT_V1_TCS_CMD1_ADDR           0x30000u /* :615,630      */
#define ADRENO_GMU_BOOT_V1_ENABLE_PDC_VALUE        0x80000001u /* :643       */
#define ADRENO_GMU_BOOT_V1_RSC_STATUS_WAKEUP       0x01000000u /* :561 BIT24 */
#define ADRENO_GMU_BOOT_V1_PDC_SEQ_START_VALUE     0x4520u  /* :573          */
#define ADRENO_GMU_BOOT_V1_PDC_MATCH_LO_VALUE      0x4510u  /* :574          */
#define ADRENO_GMU_BOOT_V1_PDC_MATCH_HI_VALUE      0x4514u  /* :575          */

/* Legacy RSC / PDC sequencer uCode (a6xx_gmu.c:590-594 / :601-605). */
#define ADRENO_GMU_BOOT_V1_RSC_UCODE_WORDS         5u
#define ADRENO_GMU_BOOT_V1_PDC_UCODE_WORDS         5u

/* Boot states + idle levels (a6xx_gmu.h:28-45). */
#define ADRENO_GMU_BOOT_V1_WARM                    0u
#define ADRENO_GMU_BOOT_V1_COLD                    1u
#define ADRENO_GMU_BOOT_V1_IDLE_ACTIVE             0u
#define ADRENO_GMU_BOOT_V1_IDLE_SPTP               2u
#define ADRENO_GMU_BOOT_V1_IDLE_IFPC               3u

/* Plan capacity: the exact cold stream below uses well under this. */
#define ADRENO_GMU_BOOT_V1_MAX_OPS                 128u
#define ADRENO_GMU_BOOT_V1_MAX_STATUS              32u

typedef enum {
    kAdrenoGmuBootV1Success = 0,
    kAdrenoGmuBootV1Invalid = 1,       /* NULL argument                     */
    kAdrenoGmuBootV1NoSpace = 2,       /* ops/status capacity exceeded      */
    kAdrenoGmuBootV1PollTimeout = 3,   /* caller poll reported no progress  */
    kAdrenoGmuBootV1BusError = 4,      /* caller read/write callback failed */
    kAdrenoGmuBootV1NotCold = 5        /* warm state handed to cold planner */
} AdrenoGmuBootV1Status;

/* Which address space an op belongs to. RSCC/PDC are dword-addressed by the
 * caller (offset<<2 from the GMU-mmio-derived bases). */
typedef enum {
    kAdrenoGmuBootV1DomainGmu = 0,     /* GMU MMIO base (0x506a000)         */
    kAdrenoGmuBootV1DomainRscc = 1,    /* gmu mmio + 0x23000                */
    kAdrenoGmuBootV1DomainPdcSeq = 2,  /* "gmu_pdc_seq" MMIO                */
    kAdrenoGmuBootV1DomainPdc = 3      /* "gmu_pdc" MMIO                    */
} AdrenoGmuBootV1Domain;

typedef enum {
    kAdrenoGmuBootV1OpWrite = 0,       /* write value                       */
    kAdrenoGmuBootV1OpWriteBulk = 1,   /* bulk_words dwords to offset (ITCM)*/
    kAdrenoGmuBootV1OpRead = 2,        /* read into *out_value              */
    kAdrenoGmuBootV1OpPoll = 3,        /* poll until (val & mask) == value  */
    kAdrenoGmuBootV1OpRmw = 4          /* gmu_rmw: value |= mask (clrmask=0)*/
} AdrenoGmuBootV1OpKind;

typedef struct {
    uint8_t  domain;                   /* AdrenoGmuBootV1Domain             */
    uint8_t  kind;                     /* AdrenoGmuBootV1OpKind             */
    uint32_t offset;                   /* word offset in the domain         */
    uint32_t value;                    /* write value / poll target / bulk0 */
    uint32_t mask;                     /* poll mask / rmw set-mask          */
    uint32_t bulk_words;               /* OpWriteBulk only                  */
} AdrenoGmuBootV1Op;

/* Cold-boot inputs the caller owns (all verified against the DTB upstream). */
typedef struct {
    uint32_t hfi_iova;                 /* GMU IOVA of the HFI table         */
    uint32_t log_iova;                 /* GMU IOVA of the log buffer        */
    uint32_t log_pages;                /* log.size / SZ_4K (>= 1)           */
    uint32_t gx_arc_vote;              /* highest gx_arc_votes entry        */
    uint32_t chip_id;                  /* adreno_gpu->chip_id, 0x06030001   */
    uint32_t idle_level;               /* GMU_IDLE_STATE_*                  */
    uint32_t boot_state;               /* ADRENO_GMU_BOOT_V1_COLD only      */
    uint32_t gmu_image_words;          /* GMU firmware size / 4 (> 0)       */
    uint32_t dtcm_snapshot;            /* value read from DTCM+0xff8 :211   */
} AdrenoGmuBootV1Inputs;

/* Caller-supplied bus. All callbacks return 0 on success. Poll reports the
 * LAST read value via *out; a non-zero return aborts the sequence. For
 * WriteBulk ops the DATA lives in caller-owned memory resolved through the
 * cookie (the plan carries only offset + word count). */
typedef struct {
    void *cookie;
    int (*write)(void *cookie, uint32_t domain, uint32_t offset, uint32_t value);
    int (*write_bulk)(void *cookie, uint32_t domain, uint32_t offset,
                      const uint32_t *values /* may be NULL; cookie-resolved */,
                      uint32_t words);
    int (*read)(void *cookie, uint32_t domain, uint32_t offset, uint32_t *out);
} AdrenoGmuBootV1Bus;

/* Number of ops the cold plan will produce for these inputs (bounds check
 * helper; also validates the inputs). 0 on invalid inputs. */
uint32_t AdrenoGmuBootV1PlanColdSize(const AdrenoGmuBootV1Inputs *in);

/* Build the EXACT ordered cold-boot op stream (rpmh_start -> flat ITCM fw
 * load -> CM3 config -> fence -> chipid -> power_config -> gmu_start ->
 * gfx_rail_on -> [sptprac] -> hfi_start). ops capacity must be
 * >= PlanColdSize(in). Never emits hardware traffic. */
AdrenoGmuBootV1Status AdrenoGmuBootV1PlanCold(const AdrenoGmuBootV1Inputs *in,
    AdrenoGmuBootV1Op *ops, uint32_t capacity, uint32_t *count);

/* Build the probe-time RSC/PDC sequencer setup (:561-643). Same rules. */
AdrenoGmuBootV1Status AdrenoGmuBootV1PlanRpmhInit(
    const uint32_t *rsc_ucode, const uint32_t *pdc_ucode,
    AdrenoGmuBootV1Op *ops, uint32_t capacity, uint32_t *count);

/* Pure predicate for a6xx_gmu.c:211-218 (mask/reset from the DTCM snapshot). */
void AdrenoGmuBootV1Cm3Expect(uint32_t dtcm_snapshot, uint32_t *mask,
                              uint32_t *reset);

/* Pure chipid massage for a6xx_gmu.c:857-859 (A630 has no gmu_chipid). */
uint32_t AdrenoGmuBootV1Chipid(uint32_t chip_id);

/* Pure fence value for a6xx_gmu.c:836-839 (A630 upper=0xa lower=0xa0). */
uint32_t AdrenoGmuBootV1FenceValue(void);

/* Execute a plan against the caller's bus, in order. Poll ops call read and
 * apply the mask/target themselves through bus->read (the caller loops). */
AdrenoGmuBootV1Status AdrenoGmuBootV1Execute(const AdrenoGmuBootV1Op *ops,
    uint32_t count, const AdrenoGmuBootV1Bus *bus);

/* Warm-boot decision for a6xx_gmu.c:1066-1068 (legacy branch):
 * GENERAL_7 == 1 -> WARM, else COLD. */
uint32_t AdrenoGmuBootV1BootState(uint32_t general7_value);

#ifdef __cplusplus
}
#endif
#endif /* ADRENO_GMU_BOOT_V1_H */
