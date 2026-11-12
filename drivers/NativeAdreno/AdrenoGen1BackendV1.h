// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// AdrenoGen1BackendV1 -- enchilada stage 5 (GPU) HOST-VERIFIED A6XX_GEN1
// BACKEND ADMISSION contract (round 69, 2026-11-12).
//
// This is NOT a GPU driver. It is a freestanding, pure-C statement of the
// ORDERED precondition set that an A6XX_GEN1 (Adreno A630, SDM845, enchilada)
// kernel backend must satisfy, with every item traced to a pinned source line
// and every item's CURRENT state recorded honestly. It maps nothing, writes no
// register, loads no firmware, authenticates nothing, submits no command, and
// is NOT an IOService.
//
// WHY THIS EXISTS (round-68 next_action step (1))
// -----------------------------------------------
// Round 68 closed with the same standing instruction it had carried since round
// 42: "the A6XX_GEN1 kernel backend that INSTANTIATES the derived plans behind
// full CX/GX genpd + 6 clocks + RPMh/PDC and TrustZone PAS auth for
// GPU_PAS_ID 13". Twenty-two derived contracts exist (rounds 20-67), but
// NOTHING in the tree states, in one tested place, which of them a real backend
// would consume, in what order, with which pinned line permitting the claim,
// and which are still missing. The risk that creates is specific and not
// hypothetical: a reader of the round-42..67 evidence can conclude "the plans
// exist, so the backend is nearly done", when in fact FIVE of the twelve
// prerequisites are modelled at host level and ZERO are satisfied by anything
// that can run on the phone.
//
// So this module does not add a thirteenth model. It ADJUDICATES the twelve.
// Two parallel flags per prerequisite, deliberately not collapsed:
//
//   modeled   -- a HOST-VERIFIED freestanding model of the item exists in this
//                tree (rounds 31/38/42/51/53/56/59/62/63/64/65/66/67).
//   satisfied -- a real, device-capable backend HOLDS the item: the code that
//                would run on the phone does the thing, not a stand-in.
//
// modeled != satisfied, and the module refuses to conflate them. Today the
// tally is modeled=5, satisfied=0, verdict BLOCKED -- which is exactly the
// state round 68 recorded, now machine-checkable instead of prose.
//
// THE ONE FACT THIS ROUND ADDS (it corrects a standing gap)
// ---------------------------------------------------------
// Round 68's next_action names "TrustZone PAS auth for GPU_PAS_ID 13". Earlier
// rounds could not cite a line for the value 13 from the UniXNU (A740/sheng)
// checkout -- correctly, because that checkout contains NO PAS concept at all:
// drivers/Adreno740 has zero occurrences of pas / PAS / trustzone / zap in
// code, and the value 13 appears there only as a PWRUP struct byte offset
// (NativeAdrenoPwrup.cpp:41). The value IS already asserted locally, three
// times, without ever being adjudicated as a prerequisite:
//   AdrenoBoardInputs.h:90   ADRENO_BOARD_ZAP_PAS_ID     13u
//   AdrenoFirmwareV1.h:81    ADRENO_FW_V1_GPU_PAS_ID     13u
//   AdrenoMetalSeamV1.h:240  ENCHILADA_METAL_A630_ZAP_PAS_ID 13u
// and it is derivable from two independent pins:
//   Linux v6.12 a6xx_gpu.c:17   #define GPU_PAS_ID 13
//   Linux v6.12 a6xx_gpu.c:819  adreno_zap_shader_load(gpu, GPU_PAS_ID)
//   enchilada.dts:23612         qcom,pas-id = <0x0d>      (0x0d == 13)
//   enchilada.dts:23613         qcom,firmware-name = "a630_zap"
// What no earlier round did is state the CONSEQUENCE, and it is load-bearing:
// an A630 backend cannot inherit sheng's PAS handling, because sheng's tree has
// none to inherit. PAS must be implemented fresh against qcom_scm, which is why
// prerequisite ZAP_AUTH is modelled=0 AND satisfied=0 -- not "partly done", but
// absent. That is the difference between a constant being known and a step
// being available, and it is the whole point of the two columns below.
//
// PROVENANCE (8 sha256 pins re-checked by
// scripts/verify-enchilada-gen1-backend.py)
// ----------------------------------------------------------------------
//  (P1) .cache/adreno-a630-source/drivers/gpu/drm/msm/adreno/a6xx_gpu.c
//       69087 B c33479377574ed193e82c272b3708e19a2b2153088f246e2639d8a43fef943a2
//       :17 GPU_PAS_ID 13; :811-819 a6xx_zap_shader_init() ->
//       adreno_zap_shader_load(gpu, 13); :1158-1180 the SECVID_TRUST_CNTL
//       fallback when no zap is used.
//  (P2) .cache/adreno-a630-source/drivers/gpu/drm/msm/adreno/a6xx_gmu.c
//       48981 B c14fbb253e602bfd2585d757da871dde08ea63b811ff3c6808f657f4522f78d4
//       :365-383 a6xx_sptprac_enable(): 0x778000 then poll (val & 0x38) == 0x28
//       at 1..100 us; :387-406 disable writes 0x778001; :1613 and :1736-1738
//       gmu->legacy = true for A630 ("HFI v1, has sptprac");
//       :1696/:1708-1709 dummy SZ_4K at IOVA 0x60000000; :1721-1725 the
//       porting NOTE that legacy GPUs need NO icache/dcache; :1741 A630 debug
//       SZ_16K at 0; :1747 log SZ_16K at 0; :1752 hfi SZ_16K at 0;
//       :1033-1052 a6xx_gmu_resume() resource order + reverse rollback;
//       :1066-1068 legacy warm/cold via REG_A6XX_GMU_GENERAL_7 == 1.
//  (P3) .cache/adreno-a630-source/drivers/gpu/drm/msm/adreno/a6xx_catalog.c
//       51692 B 6d9061ee08308126adc7b7250810b0223ee01c678e7afab6eb1738e53524c24c
//       :882-903 the A630 entry: chip_ids 0x06030001/0x06030002,
//       ADRENO_6XX_GEN1, revn 630, fw a630_sqe.fw + a630_gmu.bin,
//       gmem SZ_1M, zapfw "a630_zap.mdt", hwcg a630_hwcg, protect
//       a630_protect, gmu_cgc_mode 0x00020202, prim_fifo_threshold 0x00180000,
//       and NO .gmu_chipid (A630 has none).
//  (P4) .cache/adreno-a630-source/drivers/gpu/drm/msm/registers/adreno/a6xx.xml
//       307513 B 7f432c5e189f14ce559e0e7722c077ac778cfb63d64b2043c35d09b379bcd602
//       :2281-2285 CP_RB_BASE 0x0800 (reg64), CP_RB_CNTL 0x0802,
//       CP_RB_RPTR_ADDR 0x0804 (reg64), CP_RB_RPTR 0x0806, CP_RB_WPTR 0x0807.
//  (P5) notes/mu-silicium/enchilada.dts
//       784857 B e590ee3273499ee4230760aea9195073c0c195242eee11c95570e174ec202275
//       :23610-23616 qcom,kgsl-hyp: qcom,pas-id = <0x0d>, firmware-name
//       "a630_zap", memory-region; :23632-23657 qcom,kgsl-3d0: chipid 0x6030001,
//       reg 0x5000000+0x40000, SIX clocks; :23875-23890 qcom,gmu: FOUR clocks
//       gmu_clk/cxo_clk/axi_clk/memnoc_clk, reg 0x506a000+0x30000.
//  (P6) .cache/adreno-a630-source/drivers/clk/qcom/gpucc-sdm845.c
//       5176 B 80c2fbef5c65df986f4c391bbeedbf53e8744e5dd326f0e05dc6b4021f542a54
//       :119-128 gpu_cx_gdsc gdscr 0x106c, gds_hw_ctrl 0x1540; :130-139
//       gpu_gx_gdsc gdscr 0x100c with .power_on = gdsc_gx_do_nothing_enable.
//  (P7) .cache/adreno-a630-source/drivers/gpu/drm/msm/adreno/a6xx_hfi.c
//       22853 B f62c802d25475b86ac05cc58df012e088230ed64a0e9319456eb20ebb40c9faa
//       :59/:90-92 the legacy branches: v1 queues carry NO 0xfafafafa padding
//       and NO 4-dword alignment -- the wire difference that makes A630 "legacy".
//  (P8) .cache/adreno-a630-source/drivers/gpu/drm/msm/adreno/a6xx_gpu.h
//       3251 B 8909f9d70b6122899c70ec01fb034a6244f652eef9a0451065d22e86b546033e
//       :71-77 a6xx_has_gbif() returns FALSE for adreno_is_a630() and TRUE for
//       every other generation -- A630 has no GBIF block to program.
//
// HONEST BOUNDARY
// ---------------
// HOST-VERIFIED, ADJUDICATION ONLY. This module asserts what the pinned sources
// say and what this tree currently holds; it does not make anything true. The
// A630 client GPU windows stay ZERO (round 67 gate). CONFIG_XNU_NATIVE_ADRENO
// remains n. The gpu porting.toml area STAYS "host-verified". No device run, no
// flash, m39510 NOT exercised, no MMIO. It does NOT unblock stage 5.
// --------------------------------------------------------------------------

#ifndef ADRENO_GEN1_BACKEND_V1_H
#define ADRENO_GEN1_BACKEND_V1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- A630 catalog pins (P3:882-903) ------------------------------------- */
#define kEnchiladaGen1ChipId            0x06030001u /* P3:884 */
#define kEnchiladaGen1ChipIdAlt         0x06030002u /* P3:885 */
#define kEnchiladaGen1Revn              630u        /* P3:888 */
#define kEnchiladaGen1GmemBytes         0x00100000u /* P3:893 SZ_1M */
#define kEnchiladaGen1GmuCgcMode        0x00020202u /* P3:901 */
#define kEnchiladaGen1PrimFifoThreshold 0x00180000u /* P3:902 */
#define kEnchiladaGen1HasGmuChipid      0u          /* P3:898-903: absent */

/* ---- Pas / ZAP (P1:17, P1:819, P5:23612-23613) --------------------------- */
#define kEnchiladaGen1GPasId            13u         /* P1:17; P5:23612 = 0x0d */
#define kEnchiladaGen1ZapFirmwareName   "a630_zap.mdt"  /* P3:897 */
#define kEnchiladaGen1ZapDtsName        "a630_zap"      /* P5:23613 */
#define kEnchiladaGen1SqeFirmwareName   "a630_sqe.fw"   /* P3:890 */
#define kEnchiladaGen1GmuFirmwareName   "a630_gmu.bin"  /* P3:891 */

/* ---- SPTPRAC (P2:365-406) ----------------------------------------------- */
#define kEnchiladaGen1SptpracEnable     0x778000u   /* P2:373 */
#define kEnchiladaGen1SptpracDisable    0x778001u   /* P2:398 */
#define kEnchiladaGen1SptpracPollMask   0x38u       /* P2:376 */
#define kEnchiladaGen1SptpracPollTarget 0x28u       /* P2:376 */
#define kEnchiladaGen1SptpracPollUsecMin 1u         /* P2:376 */
#define kEnchiladaGen1SptpracPollUsecMax 100u       /* P2:376 */

/* ---- GMU resume rates (P2:1044-1046) ------------------------------------ */
#define kEnchiladaGen1CoreRateHz        200000000u  /* P2:1044 */
#define kEnchiladaGen1HubRateHz         150000000u  /* P2:1046 (non-A740) */
#define kEnchiladaGen1HubRateHzA740     200000000u  /* P2:1045-1046 */

/* ---- CP ring word offsets (P4:2281-2285) -------------------------------- */
#define kEnchiladaGen1CpRbBase          0x0800u     /* P4:2281 reg64 */
#define kEnchiladaGen1CpRbCntl          0x0802u     /* P4:2282 reg32 */
#define kEnchiladaGen1CpRbRptrAddr      0x0804u     /* P4:2283 reg64 */
#define kEnchiladaGen1CpRbRptr          0x0806u     /* P4:2284 reg32 */
#define kEnchiladaGen1CpRbWptr          0x0807u     /* P4:2285 reg32 */

/* ---- A630 GMU memory regions (P2:1696, :1708-1709, :1741, :1747, :1752) -- */
#define kEnchiladaGen1GmuDebugBytes     0x00004000u /* P2:1741 SZ_16K */
#define kEnchiladaGen1GmuLogBytes       0x00004000u /* P2:1747 SZ_16K */
#define kEnchiladaGen1GmuHfiBytes       0x00004000u /* P2:1752 SZ_16K */
#define kEnchiladaGen1GmuDummyBytes     0x00001000u /* P2:1696 SZ_4K (not widened) */
#define kEnchiladaGen1GmuDummyIova      0x60000000u /* P2:1708-1709 */
#define kEnchiladaGen1GmuUnassignedIova 0u          /* P2:1741/:1747/:1752 pass 0 */
#define kEnchiladaGen1GmuRegionBytes    0x0000d000u /* 0x4000*3 + 0x1000 */
#define kEnchiladaGen1GmuRegionCount    4u

/* ---- Ordered GMU resume steps (P2:1033-1052) ---------------------------- */
enum enchilada_gen1_resume_step {
    kEnchiladaGen1ResumeCxRuntimeGet = 0u,  /* P2:1033 */
    kEnchiladaGen1ResumeGxRuntimeGet = 1u,  /* P2:1040-1041, guarded */
    kEnchiladaGen1ResumeCoreRate     = 2u,  /* P2:1044 */
    kEnchiladaGen1ResumeHubRate      = 3u,  /* P2:1045-1046 */
    kEnchiladaGen1ResumeClocks       = 4u,  /* P2:1047 */
    kEnchiladaGen1ResumeStepCount    = 5u
};

/* ---- The twelve backend prerequisites ---------------------------------- */
enum enchilada_gen1_prereq_id {
    kEnchiladaGen1PrereqPowerPath = 0u,   /* rounds 51/62 model            */
    kEnchiladaGen1PrereqWindows = 1u,     /* round 63 model                */
    kEnchiladaGen1PrereqGmuBoot = 2u,     /* round 42 model                */
    kEnchiladaGen1PrereqHfiV1 = 3u,       /* round 31 model                */
    kEnchiladaGen1PrereqFirmware = 4u,    /* round 38 device-sourced       */
    kEnchiladaGen1PrereqZapAuth = 5u,     /* NOT modelled anywhere         */
    kEnchiladaGen1PrereqSmmuStage1 = 6u,  /* round 53 gate                 */
    kEnchiladaGen1PrereqCpRing = 7u,      /* NOT modelled                   */
    kEnchiladaGen1PrereqBoardVaMap = 8u,  /* round 67 zero gate            */
    kEnchiladaGen1PrereqDeviceProof = 9u, /* narrow sub-results only       */
    kEnchiladaGen1PrereqConfigOn = 10u,   /* CONFIG_XNU_NATIVE_ADRENO=n    */
    kEnchiladaGen1PrereqMacosFrontend = 11u, /* xcrun/codesign/hdiutil     */
    kEnchiladaGen1PrereqCount = 12u
};

struct enchilada_gen1_resume_op {
    uint32_t step;             /* enum enchilada_gen1_resume_step           */
    uint32_t takes_value;      /* 1 when `value` is a rate, else 0          */
    uint32_t value;            /* rate in Hz when takes_value                */
    uint32_t guarded;          /* 1 when the op is skipped if absent        */
};

struct enchilada_gen1_region {
    uint32_t name_id;          /* 0=debug 1=log 2=hfi 3=dummy               */
    uint32_t bytes;
    uint32_t iova;             /* 0 = "let the allocator choose" (P2)       */
    uint32_t executable;       /* 0 for every A630 region (no icache)       */
};

struct enchilada_gen1_prereq {
    uint32_t id;               /* enum enchilada_gen1_prereq_id             */
    uint32_t modeled;          /* a host-verified model exists in this tree */
    uint32_t satisfied;        /* a device-capable backend HOLDS it         */
};

/* Verdict. Deliberately a tri-state, because "5 of 12 modelled" and "12 of 12
 * satisfied" are different facts and a boolean would erase the difference. */
typedef enum {
    kEnchiladaGen1AdmitBlocked = 0,   /* satisfied < Count: no real backend  */
    kEnchiladaGen1AdmitReady = 1      /* satisfied == Count                  */
} EnchiladaGen1AdmitVerdict;

struct enchilada_gen1_admission {
    struct enchilada_gen1_prereq items[kEnchiladaGen1PrereqCount];
    uint32_t modeled_count;
    uint32_t satisfied_count;
    EnchiladaGen1AdmitVerdict verdict;
};

/* ---- API (freestanding; no libc, no MMIO, no IOKit) -------------------- */

/* Catalog facts for the backend (P3:882-903). Returns 0, or 1 on a NULL out. */
int enchilada_gen1_backend_a630_pins(uint32_t *chip_id, uint32_t *chip_id_alt,
                                     uint32_t *revn, uint32_t *gmem_bytes);

/* A630 has NO GBIF (P1:71-77 a6xx_has_gbif() returns false). Always 0. */
int enchilada_gen1_backend_has_gbif(void);
/* The generation-specific reason, for the one log line a driver publishes. */
const char *enchilada_gen1_backend_gbif_reason(void);

/* The ordered GMU resume plan (P2:1033-1052). Returns 0, or 1 on NULL/short
 * capacity; *count is 5 on success and 0 otherwise. */
int enchilada_gen1_backend_gmu_resume(struct enchilada_gen1_resume_op *ops,
                                      uint32_t capacity, uint32_t *count);

/* P2:1045-1046: A740-family 200 MHz, everything else (A630) 150 MHz. */
uint32_t enchilada_gen1_backend_hub_rate_hz(int a740_family);

/* The FOUR A630 GMU memory regions (P2). Returns 0, or 1 on NULL/short
 * capacity. There is deliberately NO icache and NO dcache region: P2:1721-1725
 * tells a porter that legacy GPUs need neither, and A630 takes the :1736 branch
 * which allocates only debug. */
int enchilada_gen1_backend_gmu_regions(struct enchilada_gen1_region *out,
                                       uint32_t capacity, uint32_t *count);
/* Always 0 for A630 (P2:1721-1725). A separate accessor so a caller cannot
 * mistake the answer for "we forgot". */
int enchilada_gen1_backend_has_icache_region(void);
/* Total bytes of the four regions: 0xd000. */
uint32_t enchilada_gen1_backend_gmu_region_bytes(void);

/* SPTPRAC value/poll contract (P2:365-406). enable=1 selects the :373 write
 * and the :375-376 poll; enable=0 selects the :398 disable write. Returns 0, or
 * 1 on NULL. */
int enchilada_gen1_backend_sptprac(uint32_t enable, uint32_t *write_value,
                                   uint32_t *poll_mask, uint32_t *poll_target);

/* GPU_PAS_ID (P1:17, P1:819, P5:23612). Always 13. */
uint32_t enchilada_gen1_backend_pas_id(void);
/* Where the value legitimately comes from. Recorded as a string because the
 * attribution is the point: it is NOT inherited from sheng's A740 tree, which
 * has no PAS concept, so PAS must be implemented fresh against qcom_scm. */
const char *enchilada_gen1_backend_pas_origin(void);

/* CP ring word offsets (P4:2281-2285). Returns 0, or 1 on NULL. */
int enchilada_gen1_backend_cp_ring_offsets(uint32_t *rb_base, uint32_t *rb_cntl,
                                           uint32_t *rptr_addr, uint32_t *rptr,
                                           uint32_t *wptr);

/* Adjudicate the twelve prerequisites. Returns 0, or 1 on a NULL out. Fills
 * every field on success; a caller can then log modeled_count/satisfied_count
 * rather than claiming a backend works. */
int enchilada_gen1_backend_admit(struct enchilada_gen1_admission *out);

/* Name of a prerequisite (0..11), or "unknown" out of range. */
const char *enchilada_gen1_backend_prereq_name(uint32_t id);
/* Pinned citation for a prerequisite, or "unknown" out of range. */
const char *enchilada_gen1_backend_prereq_anchor(uint32_t id);

/* Self-check. Returns 0 when every invariant holds, else the 1-based index of
 * the first failure; *check_count receives the number evaluated. */
int enchilada_gen1_backend_verify(uint32_t *check_count);

#ifdef __cplusplus
}
#endif

#endif /* ADRENO_GEN1_BACKEND_V1_H */
