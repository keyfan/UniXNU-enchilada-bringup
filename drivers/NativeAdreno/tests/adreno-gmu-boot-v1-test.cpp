// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * adreno-gmu-boot-v1-test.cpp - host tests for the enchilada (A630) LEGACY GMU
 *                              cold-boot bring-up sequence model. Pure plan/
 *                              predicate assertions plus a synthetic bus
 *                              replay; no hardware, no MMIO, no firmware.
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#include "AdrenoGmuBootV1.h"

static unsigned g_failures = 0u;
static unsigned g_checks = 0u;

static void check(bool ok, const char *label)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        std::printf("  FAIL %s\n", label);
    }
}

/* ---- synthetic bus recorder ---------------------------------------------- */

struct Recorder {
    uint32_t writes;
    uint32_t polls;
    uint32_t rmws;
    uint32_t bulks;
    uint32_t last_domain;
    uint32_t last_offset;
    uint32_t last_value;
    uint32_t poll_value;
    uint32_t poll_mask;
    uint32_t poll_target;
    uint32_t bulk_words;
    int fail_read;
};

static int rec_write(void *cookie, uint32_t domain, uint32_t offset,
                     uint32_t value)
{
    Recorder *r = (Recorder *)cookie;
    r->writes++;
    r->last_domain = domain;
    r->last_offset = offset;
    r->last_value = value;
    return 0;
}

static int rec_write_bulk(void *cookie, uint32_t domain, uint32_t offset,
                          const uint32_t * /*values*/, uint32_t words)
{
    Recorder *r = (Recorder *)cookie;
    r->bulks++;
    r->last_domain = domain;
    r->last_offset = offset;
    r->bulk_words = words;
    return 0;
}

static int rec_read(void *cookie, uint32_t domain, uint32_t offset,
                    uint32_t *out)
{
    Recorder *r = (Recorder *)cookie;
    (void)offset;
    (void)domain;
    if (r->fail_read) {
        return 1;
    }
    *out = r->poll_value;
    return 0;
}

static void testPurePredicates()
{
    uint32_t mask = 0u, reset = 0u;
    AdrenoGmuBootV1Cm3Expect(0x00000000u, &mask, &reset);
    check(mask == 0xffffffffu && reset == 0xbabefaceu,
          "DTCM snapshot 0 -> wide mask/babeface (:212-214)");
    AdrenoGmuBootV1Cm3Expect(0x20010004u, &mask, &reset);
    check(mask == 0xffffffffu && reset == 0xbabefaceu,
          "DTCM snapshot == 0x20010004 -> wide mask (:212 <=)");
    AdrenoGmuBootV1Cm3Expect(0x20010005u, &mask, &reset);
    check(mask == 0x1ffu && reset == 0x100u,
          "DTCM snapshot > 0x20010004 -> 0x1ff/0x100 (:216-217)");
    AdrenoGmuBootV1Cm3Expect(0xffffffffu, &mask, &reset);
    check(mask == 0x1ffu && reset == 0x100u, "DTCM snapshot max -> 0x1ff/0x100");
    check(AdrenoGmuBootV1Chipid(0x06030001u) ==
              (0x06030000u | 0x0u | 0x100u),
          "chipid 0x06030001 -> 0x06030100 (:857-859 minor/patch massage)");
    check(AdrenoGmuBootV1Chipid(0x06030002u) == 0x06030200u,
          "chipid 0x06030002 -> 0x06030200");
    check(AdrenoGmuBootV1FenceValue() ==
              (0x80000000u | (0xau << 18) | 0xa0u),
          "fence BIT(31)|upper<<18|lower with A630 0xa/0xa0 (:836-839)");
    check(AdrenoGmuBootV1BootState(1u) == ADRENO_GMU_BOOT_V1_WARM,
          "GENERAL_7==1 -> WARM (:1066-1068)");
    check(AdrenoGmuBootV1BootState(0u) == ADRENO_GMU_BOOT_V1_COLD,
          "GENERAL_7==0 -> COLD");
    check(AdrenoGmuBootV1BootState(0xdeadbeefu) == ADRENO_GMU_BOOT_V1_COLD,
          "GENERAL_7 garbage -> COLD");
    std::printf("  testPurePredicates: DTCM mask/reset, chipid, fence, boot state\n");
}

static void testRefusals()
{
    AdrenoGmuBootV1Inputs in;
    AdrenoGmuBootV1Op ops[ADRENO_GMU_BOOT_V1_MAX_OPS];
    uint32_t count = 0u;
    std::memset(&in, 0, sizeof(in));
    in.boot_state = ADRENO_GMU_BOOT_V1_COLD;
    in.gmu_image_words = 0x2000u;
    in.log_pages = 1u;
    in.idle_level = ADRENO_GMU_BOOT_V1_IDLE_SPTP;
    check(AdrenoGmuBootV1PlanCold((const AdrenoGmuBootV1Inputs *)0, ops,
              ADRENO_GMU_BOOT_V1_MAX_OPS, &count) == kAdrenoGmuBootV1Invalid,
          "NULL inputs refused");
    check(AdrenoGmuBootV1PlanCold(&in, (AdrenoGmuBootV1Op *)0,
              ADRENO_GMU_BOOT_V1_MAX_OPS, &count) == kAdrenoGmuBootV1Invalid,
          "NULL ops refused");
    in.boot_state = ADRENO_GMU_BOOT_V1_WARM;
    check(AdrenoGmuBootV1PlanCold(&in, ops, ADRENO_GMU_BOOT_V1_MAX_OPS,
              &count) == kAdrenoGmuBootV1Invalid,
          "warm boot refused (cold planner is COLD-only)");
    in.boot_state = ADRENO_GMU_BOOT_V1_COLD;
    in.gmu_image_words = 0x2001u; /* 0x8004 bytes > 0x8000 legacy gate */
    check(AdrenoGmuBootV1PlanCold(&in, ops, ADRENO_GMU_BOOT_V1_MAX_OPS,
              &count) == kAdrenoGmuBootV1Invalid,
          "image > 0x8000 bytes refused (:741)");
    in.gmu_image_words = 0u;
    check(AdrenoGmuBootV1PlanCold(&in, ops, ADRENO_GMU_BOOT_V1_MAX_OPS,
              &count) == kAdrenoGmuBootV1Invalid,
          "zero-size image refused");
    in.gmu_image_words = 0x2000u;
    in.log_pages = 0u;
    check(AdrenoGmuBootV1PlanCold(&in, ops, ADRENO_GMU_BOOT_V1_MAX_OPS,
              &count) == kAdrenoGmuBootV1Invalid,
          "zero log pages refused");
    in.log_pages = 1u;
    in.idle_level = 9u;
    check(AdrenoGmuBootV1PlanCold(&in, ops, ADRENO_GMU_BOOT_V1_MAX_OPS,
              &count) == kAdrenoGmuBootV1Invalid,
          "idle_level out of range refused");
    in.idle_level = ADRENO_GMU_BOOT_V1_IDLE_SPTP;
    check(AdrenoGmuBootV1PlanCold(&in, ops, 2u, &count) ==
              kAdrenoGmuBootV1NoSpace,
          "too-small capacity refused with NoSpace");
    check(AdrenoGmuBootV1PlanRpmhInit((const uint32_t *)0,
              (const uint32_t *)0, ops, ADRENO_GMU_BOOT_V1_MAX_OPS,
              &count) == kAdrenoGmuBootV1Invalid,
          "RpmhInit NULL ucode refused");
    std::printf("  testRefusals: fail-closed on every invalid input\n");
}

static void testPlanColdSptp()
{
    AdrenoGmuBootV1Inputs in;
    AdrenoGmuBootV1Op ops[ADRENO_GMU_BOOT_V1_MAX_OPS];
    uint32_t count = 0u;
    std::memset(&in, 0, sizeof(in));
    in.hfi_iova = 0x5000u;
    in.log_iova = 0x6000u;
    in.log_pages = 4u;
    in.gx_arc_vote = 0x0000aa55u;
    in.chip_id = 0x06030001u;
    in.idle_level = ADRENO_GMU_BOOT_V1_IDLE_SPTP;
    in.boot_state = ADRENO_GMU_BOOT_V1_COLD;
    in.gmu_image_words = 0x2000u; /* a630_gmu.bin is exactly 0x8000 bytes */
    in.dtcm_snapshot = 0u;        /* fresh cold boot -> wide mask */

    check(AdrenoGmuBootV1PlanColdSize(&in) == 33u,
          "PlanColdSize == 33 ops (SPTP idle)");
    check(AdrenoGmuBootV1PlanCold(&in, ops, ADRENO_GMU_BOOT_V1_MAX_OPS,
              &count) == kAdrenoGmuBootV1Success,
          "cold plan builds");
    check(count == 33u, "plan emits 33 ops");

    /* rpmh_start :480-505 (REQ / poll ACK / poll RSCC busy / REQ=0). */
    check(ops[0].domain == kAdrenoGmuBootV1DomainGmu &&
              ops[0].offset == ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ &&
              ops[0].value == 0x2u,
          "op0 RSCC_CONTROL_REQ=BIT(1) (:485)");
    check(ops[1].kind == kAdrenoGmuBootV1OpPoll &&
              ops[1].offset == ADRENO_GMU_BOOT_V1_RSCC_CONTROL_ACK &&
              ops[1].mask == 0x2u && ops[1].value == 0x2u,
          "op1 poll CONTROL_ACK bit1 (:487-488)");
    check(ops[2].domain == kAdrenoGmuBootV1DomainRscc &&
              ops[2].offset == ADRENO_GMU_BOOT_V1_RSCC_SEQ_BUSY_DRV0 &&
              ops[2].mask == 0xffffffffu && ops[2].value == 0u,
          "op2 poll RSCC_SEQ_BUSY_DRV0 == 0 (:494-495)");
    check(ops[3].offset == ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ &&
              ops[3].value == 0u,
          "op3 RSCC_CONTROL_REQ=0 (:502)");

    /* TCM retention + ITCM bulk :800 + :747-748. */
    check(ops[4].offset == ADRENO_GMU_BOOT_V1_GENERAL_7 &&
              ops[4].value == 1u,
          "op4 GENERAL_7=1 TCM retention (:800)");
    check(ops[5].kind == kAdrenoGmuBootV1OpWriteBulk &&
              ops[5].offset == ADRENO_GMU_BOOT_V1_CM3_ITCM_START &&
              ops[5].bulk_words == 0x2000u,
          "op5 flat ITCM bulk write 0x2000 words (:747-748)");

    /* CM3 config :820-839. */
    check(ops[6].offset == ADRENO_GMU_BOOT_V1_CM3_FW_INIT_RESULT &&
              ops[6].value == 0u, "op6 FW_INIT_RESULT=0 (:821)");
    check(ops[7].offset == ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG &&
              ops[7].value == 0x02u, "op7 BOOT_CONFIG=0x02 (:822)");
    check(ops[8].offset == ADRENO_GMU_BOOT_V1_HFI_QTBL_ADDR &&
              ops[8].value == 0x5000u, "op8 HFI_QTBL_ADDR=hfi.iova (:825)");
    check(ops[9].offset == ADRENO_GMU_BOOT_V1_HFI_QTBL_INFO &&
              ops[9].value == 1u, "op9 HFI_QTBL_INFO=1 (:826)");
    check(ops[10].offset == ADRENO_GMU_BOOT_V1_AHB_FENCE_RANGE_0 &&
              ops[10].value == (0x80000000u | (0xau << 18) | 0xa0u),
          "op10 fence A630 0xa/0xa0 (:836-839)");
    check(ops[11].offset == ADRENO_GMU_BOOT_V1_CM3_CFG &&
              ops[11].value == 0x4052u, "op11 CM3_CFG=0x4052 (:845)");

    /* chipid + log :867-871. */
    check(ops[12].offset == ADRENO_GMU_BOOT_V1_HFI_SFR_ADDR &&
              ops[12].value == 0x06030100u,
          "op12 HFI_SFR_ADDR=massaged chipid (:868)");
    check(ops[13].offset == ADRENO_GMU_BOOT_V1_PWR_COL_CP_MSG &&
              ops[13].value == (0x6000u | 3u),
          "op13 CP_MSG=log.iova|(pages-1) (:870-871)");

    /* power_config :666-706. */
    check(ops[14].offset == ADRENO_GMU_BOOT_V1_SYS_BUS_CONFIG &&
              ops[14].value == 0x1u, "op14 SYS_BUS_CONFIG=1 (:672)");
    check(ops[15].offset == ADRENO_GMU_BOOT_V1_ICACHE_CONFIG &&
              ops[15].value == 0x1u, "op15 ICACHE_CONFIG=1 (:673)");
    check(ops[16].offset == ADRENO_GMU_BOOT_V1_DCACHE_CONFIG &&
              ops[16].value == 0x1u, "op16 DCACHE_CONFIG=1 (:674)");
    check(ops[17].offset == ADRENO_GMU_BOOT_V1_PWR_COL_INTER_FRAME_CTRL &&
              ops[17].value == 0x09c40400u,
          "op17 IFPC_CTRL base 0x9c40400 (:680)");
    check(ops[18].offset == ADRENO_GMU_BOOT_V1_PWR_COL_SPTPRAC_HYST &&
              ops[18].value == 0x000a1680u,
          "op18 SPTPRAC_HYST=0xa1680 (:690-692)");
    check(ops[19].kind == kAdrenoGmuBootV1OpRmw &&
              ops[19].offset == ADRENO_GMU_BOOT_V1_PWR_COL_INTER_FRAME_CTRL &&
              ops[19].mask == 0x3u,
          "op19 rmw IFPC_ENABLE|SPTPRAC_ENABLE pos0|pos2 (:693-695)");
    check(ops[20].kind == kAdrenoGmuBootV1OpRmw &&
              ops[20].offset == ADRENO_GMU_BOOT_V1_RPMH_CTRL &&
              ops[20].mask == 0x00000f11u,
          "op20 rmw RPMH_CTRL vote bits (:699-705)");

    /* gmu_start :220-234 (DTCM read is an input; mask from Cm3Expect). */
    check(ops[21].offset == ADRENO_GMU_BOOT_V1_CM3_SYSRESET &&
              ops[21].value == 1u, "op21 SYSRESET=1 (:220)");
    check(ops[22].offset == ADRENO_GMU_BOOT_V1_PWR_COL_CP_RESP &&
              ops[22].value == 0u, "op22 CP_RESP=0 non-A7xx (:228)");
    check(ops[23].offset == ADRENO_GMU_BOOT_V1_CM3_SYSRESET &&
              ops[23].value == 0u, "op23 SYSRESET=0 (:231)");
    check(ops[24].kind == kAdrenoGmuBootV1OpPoll &&
              ops[24].offset == ADRENO_GMU_BOOT_V1_CM3_FW_INIT_RESULT &&
              ops[24].mask == 0xffffffffu && ops[24].value == 0xbabefaceu,
          "op24 poll FW_INIT_RESULT wide mask/babeface (:233-234)");

    /* gfx_rail_on :414-423. */
    check(ops[25].offset == ADRENO_GMU_BOOT_V1_BOOT_SLUMBER_OPTION &&
              ops[25].value == 0u, "op25 BOOT_SLUMBER_OPTION=0 (:414)");
    check(ops[26].offset == ADRENO_GMU_BOOT_V1_GX_VOTE_IDX &&
              ops[26].value == 0x55u, "op26 GX_VOTE_IDX=vote&0xff (:419)");
    check(ops[27].offset == ADRENO_GMU_BOOT_V1_MX_VOTE_IDX &&
              ops[27].value == 0xaau, "op27 MX_VOTE_IDX=(vote>>8)&0xff (:420)");

    /* OOB BOOT_SLUMBER :328-341. */
    check(ops[28].offset == ADRENO_GMU_BOOT_V1_HOST2GMU_INTR_SET &&
              ops[28].value == (1u << 22u),
          "op28 OOB set 1<<22 (:328)");
    check(ops[29].kind == kAdrenoGmuBootV1OpPoll &&
              ops[29].offset == ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_INFO &&
              ops[29].mask == (1u << 30u) && ops[29].value == (1u << 30u),
          "op29 OOB poll ack bit30 (:331-332)");
    check(ops[30].offset == ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_CLR &&
              ops[30].value == (1u << 30u),
          "op30 OOB clear ack bit30 (:341)");

    /* SPTP idle -> NO sptprac ops (:888-892 skips at SPTP); hfi_start. */
    check(ops[31].offset == ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT &&
              ops[31].value == 1u, "op31 HFI_CTRL_INIT=1 (:247)");
    check(ops[32].kind == kAdrenoGmuBootV1OpPoll &&
              ops[32].offset == ADRENO_GMU_BOOT_V1_HFI_CTRL_STATUS &&
              ops[32].mask == 0x1u && ops[32].value == 0x1u,
          "op32 poll HFI_CTRL_STATUS bit0 (:249-250)");
    check(ops[33].kind == kAdrenoGmuBootV1OpWrite && ops[33].offset == 0u && ops[33].value == 0u, "op33 is beyond count (capacity zero-fill)");

    std::printf("  testPlanColdSptp: exact 34-op stream (rpmh->ITCM->config->start->rail->hfi)\n");
}

static void testPlanColdActiveSptprac()
{
    AdrenoGmuBootV1Inputs in;
    AdrenoGmuBootV1Op ops[ADRENO_GMU_BOOT_V1_MAX_OPS];
    uint32_t count = 0u;
    std::memset(&in, 0, sizeof(in));
    in.log_pages = 1u;
    in.gmu_image_words = 0x100u;
    in.idle_level = ADRENO_GMU_BOOT_V1_IDLE_ACTIVE; /* < SPTP -> sptprac on */
    in.boot_state = ADRENO_GMU_BOOT_V1_COLD;
    check(AdrenoGmuBootV1PlanColdSize(&in) == 35u,
          "PlanColdSize == 35 ops (ACTIVE idle adds 2 sptprac ops)");
    check(AdrenoGmuBootV1PlanCold(&in, ops, ADRENO_GMU_BOOT_V1_MAX_OPS,
              &count) == kAdrenoGmuBootV1Success,
          "cold plan (ACTIVE) builds");
    check(count == 35u, "plan emits 35 ops");
    check(ops[31].offset == ADRENO_GMU_BOOT_V1_SPTPRAC_POWER_CONTROL &&
              ops[31].value == 0x00778000u,
          "op31 SPTPRAC_POWER_CONTROL=0x778000 (:373)");
    check(ops[32].kind == kAdrenoGmuBootV1OpPoll &&
              ops[32].offset == ADRENO_GMU_BOOT_V1_SPTPRAC_PWR_CLK_STATUS &&
              ops[32].mask == 0x38u && ops[32].value == 0x28u,
          "op32 poll (val&0x38)==0x28 (:375-376)");
    check(ops[33].offset == ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT,
          "hfi_start follows sptprac (:894)");
    std::printf("  testPlanColdActiveSptprac: idle<SPTP inserts sptprac pair (:888-892)\n");
}

static void testPlanRpmhInit()
{
    static const uint32_t rsc[5] = { 0xa7a506a0u, 0xa1e6a6e7u, 0xa2e081e1u,
        0xe9a982e2u, 0x0020e8a8u }; /* :590-594 */
    static const uint32_t pdc[5] = { 0xfebea1e1u, 0xa5a4a3a2u, 0x8382a6e0u,
        0xbce3e284u, 0x002081fcu }; /* :601-605 */
    AdrenoGmuBootV1Op ops[ADRENO_GMU_BOOT_V1_MAX_OPS];
    uint32_t count = 0u;
    check(AdrenoGmuBootV1PlanRpmhInit(rsc, pdc, ops,
              ADRENO_GMU_BOOT_V1_MAX_OPS, &count) == kAdrenoGmuBootV1Success,
          "RpmhInit plan builds");
    check(count == 36u, "RpmhInit emits 5+5+12+12+2 = 36 ops");
    /* RSC uCode first. */
    check(ops[0].domain == kAdrenoGmuBootV1DomainRscc &&
              ops[0].offset == ADRENO_GMU_BOOT_V1_RSCC_SEQ_MEM_0_DRV0 &&
              ops[0].value == 0xa7a506a0u,
          "RSC uCode word0 0xa7a506a0 (:590)");
    check(ops[4].value == 0x0020e8a8u, "RSC uCode word4 0x0020e8a8 (:594)");
    /* PDC uCode next, PdcSeq domain. */
    check(ops[5].domain == kAdrenoGmuBootV1DomainPdcSeq &&
              ops[5].offset == ADRENO_GMU_BOOT_V1_PDC_SEQ_MEM_0 &&
              ops[5].value == 0xfebea1e1u,
          "PDC uCode word0 0xfebea1e1 (:601)");
    check(ops[9].value == 0x002081fcu, "PDC uCode word4 0x002081fc (:605)");
    /* TCS1 block :608-620. */
    check(ops[10].offset == ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD_ENABLE_BANK &&
              ops[10].value == 7u, "TCS1 ENABLE_BANK=7 (:608)");
    check(ops[11].offset == ADRENO_GMU_BOOT_V1_PDC_TCS1_CMD_WAIT_BANK &&
              ops[11].value == 0u, "TCS1 WAIT=0 (:609)");
    check(ops[12].offset == ADRENO_GMU_BOOT_V1_PDC_TCS1_CONTROL &&
              ops[12].value == 0u, "TCS1 CONTROL=0 (:610)");
    check(ops[13].value == 0x10108u, "TCS1 CMD0 MSGID=0x10108 (:611)");
    check(ops[14].value == 0x30010u, "TCS1 CMD0 ADDR=0x30010 (:612)");
    check(ops[15].value == 1u, "TCS1 CMD0 DATA=1 (:613)");
    check(ops[17].value == 0x30000u && ops[18].value == 0u, "TCS1 CMD1 ADDR=0x30000 (:615), CMD1 DATA=0 (:616)");
    check(ops[20].value == 0x30080u && ops[21].value == 0u,
          "TCS1 CMD2 ADDR=pdc_address_offset 0x30080 (:619), DATA=0 (:620)");
    /* TCS3 block :622-638 (A630 else-branch DATA 0x3). */
    check(ops[22].offset == ADRENO_GMU_BOOT_V1_PDC_TCS3_CMD_ENABLE_BANK,
          "TCS3 starts at ENABLE_BANK (:622)");
    check(ops[25].value == 0x10108u, "TCS3 CMD0 MSGID (:625)");
    check(ops[27].value == 2u, "TCS3 CMD0 DATA=2 (:627)");
    check(ops[30].value == 0x3u && ops[29].value == 0x30000u, "TCS3 CMD1 ADDR=0x30000 (:630), DATA=0x3 A630 else (:635)");
    check(ops[32].value == 0x30080u, "TCS3 CMD2 ADDR=0x30080 (:637)");
    check(ops[33].value == 0x3u, "TCS3 CMD2 DATA=0x3 (:638)");
    /* setup_pdc :641-643. */
        check(ops[34].offset == ADRENO_GMU_BOOT_V1_PDC_SEQ_START_ADDR &&
              ops[34].value == 0u,
          "SEQ_START_ADDR=0 (:642)");
    check(ops[35].offset == ADRENO_GMU_BOOT_V1_PDC_ENABLE_PDC &&
              ops[35].value == 0x80000001u,
          "ENABLE_PDC=0x80000001 (:643)");
    std::printf("  testPlanRpmhInit: RSC+PDC uCode + TCS1/TCS3 + enable (%u ops)\n", count);
}

static void testExecute()
{
    Recorder r;
    AdrenoGmuBootV1Bus bus;
    AdrenoGmuBootV1Inputs in;
    AdrenoGmuBootV1Op ops[ADRENO_GMU_BOOT_V1_MAX_OPS];
    uint32_t count = 0u;
    std::memset(&r, 0, sizeof(r));
    std::memset(&bus, 0, sizeof(bus));
    std::memset(&in, 0, sizeof(in));
    bus.cookie = &r;
    bus.write = rec_write;
    bus.write_bulk = rec_write_bulk;
    bus.read = rec_read;
    in.log_pages = 1u;
    in.gmu_image_words = 0x2000u;
    in.idle_level = ADRENO_GMU_BOOT_V1_IDLE_SPTP;
    in.boot_state = ADRENO_GMU_BOOT_V1_COLD;
    check(AdrenoGmuBootV1PlanCold(&in, ops, ADRENO_GMU_BOOT_V1_MAX_OPS,
              &count) == kAdrenoGmuBootV1Success,
          "plan for execute");
    r.poll_value = 0x0u; /* ACK bit1 clear -> probe misses */
    check(AdrenoGmuBootV1Execute(ops, 2u, &bus) == kAdrenoGmuBootV1PollTimeout,
          "poll probe reports PollTimeout until target matches");
    check(r.writes == 1u, "REQ write issued");
    /* Replay op-by-op with satisfying values. */
    r.writes = 0u; r.bulks = 0u;
    r.poll_value = 0x2u;
    check(AdrenoGmuBootV1Execute(ops, 1u, &bus) == kAdrenoGmuBootV1Success,
          "op0 write succeeds");
    check(r.last_offset == ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ &&
              r.last_value == 0x2u,
          "op0 recorded REQ=BIT(1)");
    r.poll_value = 0x0u;
    check(AdrenoGmuBootV1Execute(&ops[2], 1u, &bus) == kAdrenoGmuBootV1Success,
          "op2 RSCC busy==0 satisfied");
    check(r.fail_read == 0, "no read failures");
    r.fail_read = 1;
    check(AdrenoGmuBootV1Execute(&ops[1], 1u, &bus) == kAdrenoGmuBootV1BusError,
          "bus read failure -> BusError");
    r.fail_read = 0;
    /* rmw executes read|mask write. */
    {
        AdrenoGmuBootV1Op rmw;
        std::memset(&rmw, 0, sizeof(rmw));
        rmw.kind = kAdrenoGmuBootV1OpRmw;
        rmw.offset = 0x50e8u;
        rmw.mask = 0xf11u;
        r.poll_value = 0x0u;
        check(AdrenoGmuBootV1Execute(&rmw, 1u, &bus) ==
                  kAdrenoGmuBootV1Success,
              "rmw op succeeds");
        check(r.last_value == 0xf11u, "rmw wrote read|mask");
    }
    std::printf("  testExecute: synthetic bus replay (write/poll/rmw/error paths)\n");
}

static void testBusNullRefusal()
{
    AdrenoGmuBootV1Op op;
    std::memset(&op, 0, sizeof(op));
    op.kind = kAdrenoGmuBootV1OpWrite;
    check(AdrenoGmuBootV1Execute((const AdrenoGmuBootV1Op *)0, 1u,
              (const AdrenoGmuBootV1Bus *)0) == kAdrenoGmuBootV1Invalid,
          "Execute NULL refused");
    AdrenoGmuBootV1Bus bus;
    std::memset(&bus, 0, sizeof(bus));
    check(AdrenoGmuBootV1Execute(&op, 1u, &bus) == kAdrenoGmuBootV1Invalid,
          "Execute missing callbacks refused");
    op.kind = 99u;
    AdrenoGmuBootV1Inputs in;
    std::memset(&in, 0, sizeof(in));
    in.log_pages = 1u;
    in.gmu_image_words = 1u;
    in.boot_state = ADRENO_GMU_BOOT_V1_COLD;
    AdrenoGmuBootV1Op ops[ADRENO_GMU_BOOT_V1_MAX_OPS];
    uint32_t count = 0u;
    check(AdrenoGmuBootV1PlanCold(&in, ops, ADRENO_GMU_BOOT_V1_MAX_OPS, &count) ==
              kAdrenoGmuBootV1Success,
          "plan builds");
    ops[0].kind = 99u;
    Recorder r;
    std::memset(&r, 0, sizeof(r));
    bus.cookie = &r;
    bus.write = rec_write;
    bus.write_bulk = rec_write_bulk;
    bus.read = rec_read;
    check(AdrenoGmuBootV1Execute(ops, 1u, &bus) == kAdrenoGmuBootV1Invalid,
          "unknown op kind refused");
    std::printf("  testBusNullRefusal: fail-closed bus contract\n");
}

int main()
{
    testPurePredicates();
    testRefusals();
    testPlanColdSptp();
    testPlanColdActiveSptprac();
    testPlanRpmhInit();
    testExecute();
    testBusNullRefusal();
    std::printf("PASS: enchilada A630 gmu-boot-v1 host tests (%u checks)\n",
                g_checks);
    return g_failures ? 1 : 0;
}
