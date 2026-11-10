// SPDX-License-Identifier: MIT
/*
 * adreno-gmu-caller-v1-test.cpp - host tests for the round-46 powered-GMU
 * backend CALLER (AdrenoGmuCallerV1). Pure fail-closed assertions plus a
 * synthetic-bus cold-boot replay; no hardware, no MMIO, no firmware.
 *
 * Groups:
 *   1. Precheck validation: NULL args, missing caller-asserted pre, GPUCC
 *      CX-off / GX-off cross-checks, happy path.
 *   2. BootState read + warm refusal.
 *   3. ColdBoot full replay: precheck -> GENERAL_7 -> cold plan executes
 *      against a real AdrenoGmuBusV1 window set with expected values.
 *   4. ColdBoot poll-retry: poll misses then hits on attempt N.
 *   5. ColdBoot fail-closed: bus read error, warm refusal, cold plan invalid.
 *   6. first_fail_op flattening (rpmh_ops + cold idx).
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstddef>

#include "AdrenoGmuCallerV1.h"
#include "AdrenoGmuBusV1.h"

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

/* ---- synthetic GPUCC reader ------------------------------------------------ */

struct GpuccSim {
    uint32_t cx;
    uint32_t gx;
    int fail;
};

static int gpucc_read(void *cookie, uint32_t byte_offset, uint32_t *out)
{
    GpuccSim *g = (GpuccSim *)cookie;
    (void)byte_offset;
    if (g->fail) {
        return 1;
    }
    *out = (byte_offset == ADRENO_GMU_CALLER_V1_CX_STATUS_BYTE) ? g->cx : g->gx;
    return 0;
}

/* ---- synthetic GMU bus over a real window set ------------------------------ */

#define GMU_WIN_WORDS 0x9400u        /* must span RSCC_CONTROL_ACK word 0x9308
                                      * (byte 0x24C20) in the GMU window: the
                                      * cold/rpmh plans address the RSCC regs
                                      * through the GMU domain (upstream writes
                                      * gmu->mmio + 0x9308 directly), so the
                                      * simulated window needs >= 0x24C24 bytes */
#define PDC_WIN_WORDS 8192u          /* byte 0x576c needed for TCS3 CMD0     */
#define RSCC_DELTA    ADRENO_GMU_BUS_V1_RSCC_DELTA

struct WindowSim {
    uint32_t gmu[GMU_WIN_WORDS];
    uint32_t pdc_seq[64];
    uint32_t pdc[PDC_WIN_WORDS];
    int fail_read;
    uint32_t read_count;
    uint32_t write_count;
};

static uint32_t g_itcm_image[0x2000u]; /* bulk image for the ITCM WriteBulk op */

[[maybe_unused]] static int ws_write(void *cookie, uint32_t domain, uint32_t offset,
                    uint32_t value)
{
    WindowSim *w = (WindowSim *)cookie;
    w->write_count++;
    (void)domain;
    (void)offset;
    (void)value;
    return 0;
}

[[maybe_unused]] static int ws_write_bulk(void *cookie, uint32_t domain, uint32_t offset,
                         const uint32_t *values, uint32_t words)
{
    WindowSim *w = (WindowSim *)cookie;
    (void)domain;
    (void)offset;
    (void)values;
    (void)words;
    w->write_count++;
    return 0;
}

[[maybe_unused]] static int ws_read(void *cookie, uint32_t domain, uint32_t offset,
                   uint32_t *out)
{
    WindowSim *w = (WindowSim *)cookie;
    (void)domain;
    (void)offset;
    w->read_count++;
    if (w->fail_read) {
        return 1;
    }
    *out = 0u;
    return 0;
}

/* ---- hand-rolled counting bus (hardware-response simulation) ---------------
 * The memory-backed adapter window cannot model hardware that SETS status
 * bits after a write (firmware ACK, HFI ready). This bus returns the
 * hardware-expected value for the known poll registers and counts every
 * callback, mirroring the boot-v1 Recorder approach. */
struct CountSim {
    int fail_read;
    uint32_t mid_fail_after; /* fail reads once reads exceed this (0 = never) */
    uint32_t reads;
    uint32_t writes;
    uint32_t bulks;
};

static int count_read(void *cookie, uint32_t domain, uint32_t offset,
                      uint32_t *out)
{
    CountSim *c = (CountSim *)cookie;
    (void)domain;
    c->reads++;
    if (c->fail_read) {
        return 1;
    }
    if (c->mid_fail_after != 0u && c->reads > c->mid_fail_after) {
        return 1;
    }
    if (offset == ADRENO_GMU_BOOT_V1_RSCC_CONTROL_ACK) {
        *out = 0x2u;                                   /* ACK bit set   */
    } else if (offset == ADRENO_GMU_BOOT_V1_CM3_FW_INIT_RESULT) {
        *out = 0xbabefaceu;                            /* fw responded  */
    } else if (offset == ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_INFO) {
        *out = 1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_ACK;
    } else if (offset == ADRENO_GMU_BOOT_V1_HFI_CTRL_STATUS) {
        *out = 0x1u;                                   /* HFI ready     */
    } else {
        *out = 0u;                                     /* SEQ_BUSY etc  */
    }
    return 0;
}

static int count_write(void *cookie, uint32_t domain, uint32_t offset,
                       uint32_t value)
{
    CountSim *c = (CountSim *)cookie;
    (void)domain; (void)offset; (void)value;
    c->writes++;
    return 0;
}

static int count_write_bulk(void *cookie, uint32_t domain, uint32_t offset,
                            const uint32_t *values, uint32_t words)
{
    CountSim *c = (CountSim *)cookie;
    (void)domain; (void)offset; (void)values; (void)words;
    c->bulks++;
    return 0;
}

/* ---- WARM-state bus: GENERAL_7 reads 1 (boot-v1 :1066-1068 branch) -------- */
struct WarmSim {
    uint32_t reads;
    uint32_t writes;
};

static int warm_read(void *cookie, uint32_t domain, uint32_t offset,
                     uint32_t *out)
{
    WarmSim *w = (WarmSim *)cookie;
    (void)domain;
    w->reads++;
    *out = (offset == ADRENO_GMU_BOOT_V1_GENERAL_7) ? 1u : 0u;
    return 0;
}

static int warm_write(void *cookie, uint32_t domain, uint32_t offset,
                      uint32_t value)
{
    WarmSim *w = (WarmSim *)cookie;
    (void)domain; (void)offset; (void)value;
    w->writes++;
    return 0;
}

static int warm_write_bulk(void *cookie, uint32_t domain, uint32_t offset,
                           const uint32_t *values, uint32_t words)
{
    WarmSim *w = (WarmSim *)cookie;
    (void)domain; (void)offset; (void)values; (void)words;
    w->writes++;
    return 0;
}

static void make_windows(struct AdrenoGmuBusV1Windows *win, WindowSim *w)
{
    win->gmu_mmio = (volatile uint8_t *)w->gmu;
    win->gmu_bytes = GMU_WIN_WORDS * 4u;
    win->pdc_seq_mmio = (volatile uint8_t *)w->pdc_seq;
    win->pdc_seq_bytes = sizeof(w->pdc_seq);
    win->pdc_mmio = (volatile uint8_t *)w->pdc;
    win->pdc_bytes = sizeof(w->pdc);
    /* round 46: register the bulk image the model contract's ITCM op needs */
    win->bulk_image = g_itcm_image;
    win->bulk_words = (uint32_t)(sizeof(g_itcm_image) / sizeof(g_itcm_image[0]));
}

/* Valid A630 cold inputs (mirrors the boot-v1 test's replay inputs). */
static void make_inputs(AdrenoGmuBootV1Inputs *in)
{
    in->hfi_iova = 0x60004000u;
    in->log_iova = 0x60010000u;
    in->log_pages = 1u;
    in->gx_arc_vote = 0x00000700u;
    in->chip_id = 0x06030001u;
    in->idle_level = ADRENO_GMU_BOOT_V1_IDLE_SPTP;
    in->boot_state = ADRENO_GMU_BOOT_V1_COLD;
    in->gmu_image_words = 0x2000u;
    in->dtcm_snapshot = 0u; /* <= 0x20010004 -> full mask/reset branch */
}

static void make_pre(struct AdrenoGmuCallerV1Pre *pre, uint32_t require_gx)
{
    pre->cx_genpd_on = 1u;
    pre->gx_genpd_on = 1u;
    pre->clocks_enabled = 1u;
    pre->require_gx = require_gx;
}

static const uint32_t RSC_UCODE[ADRENO_GMU_BOOT_V1_RSC_UCODE_WORDS] = {
    0x00000011u, 0x00000022u, 0x00000033u, 0x00000044u, 0x00000055u
};
static const uint32_t PDC_UCODE[ADRENO_GMU_BOOT_V1_PDC_UCODE_WORDS] = {
    0x00000aaau, 0x00000bbbu, 0x00000cccu, 0x00000dddu, 0x00000eeeu
};

/* ---- group 1: precheck ------------------------------------------------------ */

static void testPrecheck()
{
    std::printf("group 1: precheck fail-closed\n");
    struct AdrenoGmuCallerV1Pre pre;
    GpuccSim g;
    struct AdrenoGmuCallerV1Gpucc gp;

    g.cx = 0x80000000u;   /* BIT(31) PWR_ON_MASK -> CX on  */
    g.gx = 0x00010000u;   /* BIT(16) POWER_UP_COMPLETE     */
    g.fail = 0;
    gp.cookie = &g;
    gp.read = gpucc_read;

    make_pre(&pre, 1u);
    check(AdrenoGmuCallerV1Precheck(&pre, &gp) == kAdrenoGmuCallerV1Ok,
          "happy path accepted");
    check(AdrenoGmuCallerV1Precheck(0, &gp) == kAdrenoGmuCallerV1Invalid,
          "NULL pre refused");
    check(AdrenoGmuCallerV1Precheck(&pre, 0) == kAdrenoGmuCallerV1Invalid,
          "NULL gpucc refused");

    make_pre(&pre, 1u);
    pre.clocks_enabled = 0u;
    check(AdrenoGmuCallerV1Precheck(&pre, &gp) == kAdrenoGmuCallerV1NotReady,
          "missing clocks refused");
    make_pre(&pre, 1u);
    pre.cx_genpd_on = 0u;
    check(AdrenoGmuCallerV1Precheck(&pre, &gp) == kAdrenoGmuCallerV1NotReady,
          "missing CX genpd refused");
    make_pre(&pre, 1u);
    pre.gx_genpd_on = 0u;
    check(AdrenoGmuCallerV1Precheck(&pre, &gp) == kAdrenoGmuCallerV1NotReady,
          "missing GX genpd refused");

    make_pre(&pre, 1u);
    g.cx = 0x00000000u;   /* CX collapsed */
    check(AdrenoGmuCallerV1Precheck(&pre, &gp) == kAdrenoGmuCallerV1CxOff,
          "GPUCC CX-off cross-check refused");
    g.cx = 0x80000000u;
    g.gx = 0x00008000u;   /* BIT(15) POWER_DOWN_COMPLETE -> GX off */
    check(AdrenoGmuCallerV1Precheck(&pre, &gp) == kAdrenoGmuCallerV1GxOff,
          "require_gx + GPUCC GX-off refused");
    make_pre(&pre, 0u);
    check(AdrenoGmuCallerV1Precheck(&pre, &gp) == kAdrenoGmuCallerV1Ok,
          "require_gx=0 records GX-off without refusing");

    make_pre(&pre, 1u);
    g.gx = 0x00010000u;
    g.fail = 1;
    check(AdrenoGmuCallerV1Precheck(&pre, &gp) == kAdrenoGmuCallerV1BusError,
          "gpucc read failure forwarded");
    g.fail = 0;
}

/* ---- group 2: boot state ---------------------------------------------------- */

static void testBootState()
{
    std::printf("group 2: boot-state read + warm refusal\n");
    WindowSim w;
    struct AdrenoGmuBusV1Windows win;
    AdrenoGmuBusV1 bus;
    std::memset(&w, 0, sizeof(w));
    make_windows(&win, &w);
    check(AdrenoGmuBusV1Make(&win, &bus) == 0, "windows accepted");

    w.gmu[ADRENO_GMU_BOOT_V1_GENERAL_7] = 0u;
    uint32_t bs = 0u;
    check(AdrenoGmuCallerV1BootState(&bus, &bs) == kAdrenoGmuCallerV1Ok,
          "GENERAL_7=0 read");
    check(bs == ADRENO_GMU_BOOT_V1_COLD, "decoded COLD");

    w.gmu[ADRENO_GMU_BOOT_V1_GENERAL_7] = 1u;
    check(AdrenoGmuCallerV1BootState(&bus, &bs) == kAdrenoGmuCallerV1Ok,
          "GENERAL_7=1 read");
    check(bs == ADRENO_GMU_BOOT_V1_WARM, "decoded WARM");

    check(AdrenoGmuCallerV1BootState(0, &bs) == kAdrenoGmuCallerV1Invalid,
          "NULL bus refused");
    check(AdrenoGmuCallerV1BootState(&bus, 0) == kAdrenoGmuCallerV1Invalid,
          "NULL out refused");

    /* Adapter bus reads real memory; simulate a read failure through a
     * hand-rolled counting bus (the adapter cannot fail reads). */
    AdrenoGmuBootV1Bus fbus;
    CountSim cs;
    std::memset(&cs, 0, sizeof(cs));
    cs.fail_read = 1;
    fbus.cookie = &cs;
    fbus.write = count_write;
    fbus.write_bulk = count_write_bulk;
    fbus.read = count_read;
    check(AdrenoGmuCallerV1BootState(&fbus, &bs) == kAdrenoGmuCallerV1BusError,
          "bus read failure forwarded");
    check(cs.reads == 1u, "exactly one read attempted");
}

/* ---- group 3: full cold-boot replay ----------------------------------------- */

static void testColdBootReplay()
{
    std::printf("group 3: cold-boot replay through the real bus adapter\n");
    WindowSim w;
    struct AdrenoGmuBusV1Windows win;
    AdrenoGmuBusV1 bus;
    AdrenoGmuBootV1Inputs in;
    struct AdrenoGmuCallerV1Pre pre;
    GpuccSim g;
    struct AdrenoGmuCallerV1Gpucc gp;
    uint32_t fail_op = 0xffffffffu;

    std::memset(&w, 0, sizeof(w));
    make_windows(&win, &w);
    check(AdrenoGmuBusV1Make(&win, &bus) == 0, "windows accepted");
    make_inputs(&in);
    make_pre(&pre, 1u);
    g.cx = 0x80000000u;
    g.gx = 0x00010000u;
    g.fail = 0;
    gp.cookie = &g;
    gp.read = gpucc_read;

    w.gmu[ADRENO_GMU_BOOT_V1_GENERAL_7] = 0u; /* COLD */
    /* Pre-set the RSC ACK so the memory-backed window passes the first poll
     * (the adapter writes real memory; nothing sets ACK for us). */
    w.gmu[ADRENO_GMU_BOOT_V1_RSCC_CONTROL_ACK] = 0x2u;

    /* Memory-sim honest boundary: the plan runs through the adapter until the
     * CM3_FW_INIT_RESULT poll — the point where real firmware would respond
     * (the preceding Write op zeroed the register; no firmware lives in the
     * sim). Assert the failure point and that the earlier ops LANDED. */
    for (uint32_t i = 0; i < 0x2000u; i++) g_itcm_image[i] = 0x45200000u + i;
    AdrenoGmuCallerV1Status st = AdrenoGmuCallerV1ColdBoot(&pre, &gp, &in,
        0, 0, &bus, 0u, &fail_op);
    check(st == kAdrenoGmuCallerV1PollTimeout, "memory sim fails at fw poll");
    {
        /* Locate the FW poll index inside the cold plan to assert exactly. */
        AdrenoGmuBootV1Op ops[ADRENO_GMU_BOOT_V1_MAX_OPS];
        uint32_t n = 0u;
        check(AdrenoGmuBootV1PlanCold(&in, ops, ADRENO_GMU_BOOT_V1_MAX_OPS, &n)
                  == kAdrenoGmuBootV1Success, "cold plan reproducible");
        uint32_t fw_idx = 0xffffffffu;
        for (uint32_t i = 0; i < n; i++) {
            if (ops[i].kind == kAdrenoGmuBootV1OpPoll &&
                ops[i].offset == ADRENO_GMU_BOOT_V1_CM3_FW_INIT_RESULT) {
                fw_idx = i;
                break;
            }
        }
        check(fw_idx != 0xffffffffu, "fw poll located");
        check(fail_op == 1u, "fail op is the first cold poll (diagnostic scan idx)");
    }
    /* The ITCM bulk op (registered bulk_image) landed in the window. */
    check(w.gmu[ADRENO_GMU_BOOT_V1_CM3_ITCM_START] == 0x45200000u &&
          w.gmu[ADRENO_GMU_BOOT_V1_CM3_ITCM_START + 1u] == 0x45200001u,
          "ITCM bulk image served through the adapter");
    /* The REQ write landed and the ACK poll consumed the pre-set value. */
    check(w.gmu[ADRENO_GMU_BOOT_V1_RSCC_CONTROL_REQ] == 0u,
          "REQ write then REQ=0 clear both landed in the window");

    /* WITHOUT the ucode tables the plan still runs (rpmh optional). */
    std::memset(&w, 0, sizeof(w));
    make_windows(&win, &w);
    check(AdrenoGmuBusV1Make(&win, &bus) == 0, "windows accepted (fresh)");
    w.gmu[ADRENO_GMU_BOOT_V1_GENERAL_7] = 0u;
    w.gmu[ADRENO_GMU_BOOT_V1_RSCC_CONTROL_ACK] = 0x2u;
    fail_op = 0xffffffffu;
    st = AdrenoGmuCallerV1ColdBoot(&pre, &gp, &in, 0, 0, &bus, 0u, &fail_op);
    check(st == kAdrenoGmuCallerV1PollTimeout,
          "cold boot without rpmh also reaches the fw poll");
}

/* ---- group 4: poll retry ----------------------------------------------------- */

struct RetrySim {
    uint32_t polls;
    uint32_t hit_after; /* hit when polls >= hit_after */
};

static int retry_read(void *cookie, uint32_t domain, uint32_t offset,
                      uint32_t *out)
{
    RetrySim *r = (RetrySim *)cookie;
    (void)domain;
    r->polls++;
    /* Hardware-response table (see count_read); the ACK poll misses until
     * polls reaches hit_after, everything else answers immediately. */
    if (offset == ADRENO_GMU_BOOT_V1_CM3_FW_INIT_RESULT) {
        *out = 0xbabefaceu;
    } else if (offset == ADRENO_GMU_BOOT_V1_GMU2HOST_INTR_INFO) {
        *out = 1u << ADRENO_GMU_BOOT_V1_OOB_BOOT_SLUMBER_ACK;
    } else if (offset == ADRENO_GMU_BOOT_V1_HFI_CTRL_STATUS) {
        *out = 0x1u;
    } else if (offset == ADRENO_GMU_BOOT_V1_RSCC_CONTROL_ACK) {
        *out = (r->polls >= r->hit_after) ? 0x2u : 0u;
    } else {
        *out = 0u;
    }
    return 0;
}

static int retry_write(void *cookie, uint32_t domain, uint32_t offset,
                       uint32_t value)
{
    (void)cookie; (void)domain; (void)offset; (void)value;
    return 0;
}

static int retry_write_bulk(void *cookie, uint32_t domain, uint32_t offset,
                            const uint32_t *values, uint32_t words)
{
    (void)cookie; (void)domain; (void)offset; (void)values; (void)words;
    return 0;
}

static void testPollRetry()
{
    std::printf("group 4: poll retry exhausts then succeeds\n");
    RetrySim r;
    AdrenoGmuBootV1Bus bus;
    AdrenoGmuBootV1Inputs in;
    struct AdrenoGmuCallerV1Pre pre;
    GpuccSim g;
    struct AdrenoGmuCallerV1Gpucc gp;
    uint32_t fail_op = 0xffffffffu;

    std::memset(&r, 0, sizeof(r));
    bus.cookie = &r;
    bus.write = retry_write;
    bus.write_bulk = retry_write_bulk;
    bus.read = retry_read;
    make_inputs(&in);
    make_pre(&pre, 0u);
    g.cx = 0x80000000u;
    g.gx = 0x00010000u;
    g.fail = 0;
    gp.cookie = &g;
    gp.read = gpucc_read;

    /* Boot-state read is polls=1; the ACK poll (polls=2) hits immediately:
     * Execute succeeds on the FIRST attempt (retry machinery unexercised). */
    r.hit_after = 2u;
    AdrenoGmuCallerV1Status st = AdrenoGmuCallerV1ColdBoot(&pre, &gp, &in,
        0, 0, &bus, 3u, &fail_op);
    check(st == kAdrenoGmuCallerV1Ok, "cold boot succeeds with ACK on attempt 1");
    check(fail_op == 0u, "fail op cleared on success");

    r.polls = 0u;
    r.hit_after = 10u;
    fail_op = 0xffffffffu;
    st = AdrenoGmuCallerV1ColdBoot(&pre, &gp, &in, 0, 0, &bus, 2u, &fail_op);
    check(st == kAdrenoGmuCallerV1PollTimeout, "retry-2 exhausts -> timeout");
    check(fail_op == 1u, "failing ACK poll is cold-block poll idx 1");
    /* retry machinery: hit_after=3 -> attempt 1 (polls=2) misses, attempt 2
     * (polls=4... wait: each Execute re-runs ALL reads; boot-state happens
     * ONCE outside Execute; the ACK poll is Execute read #1: attempt 1 ->
     * polls=2 (miss), attempt 2 -> polls=3 (hit) -> success on attempt 2. */
    r.polls = 0u;
    r.hit_after = 3u;
    fail_op = 0xffffffffu;
    st = AdrenoGmuCallerV1ColdBoot(&pre, &gp, &in, 0, 0, &bus, 3u, &fail_op);
    check(st == kAdrenoGmuCallerV1Ok, "ACK hits on retry attempt 2");
    check(fail_op == 0u, "fail op cleared after retry success");
}

/* ---- group 5: fail-closed paths ---------------------------------------------- */

static void testFailClosed()
{
    std::printf("group 5: fail-closed cold-boot paths\n");
    WindowSim w;
    struct AdrenoGmuBusV1Windows win;
    AdrenoGmuBusV1 bus;
    AdrenoGmuBootV1Inputs in;
    struct AdrenoGmuCallerV1Pre pre;
    GpuccSim g;
    struct AdrenoGmuCallerV1Gpucc gp;

    std::memset(&w, 0, sizeof(w));
    make_windows(&win, &w);
    check(AdrenoGmuBusV1Make(&win, &bus) == 0, "windows accepted");
    make_inputs(&in);
    make_pre(&pre, 1u);
    g.cx = 0x80000000u;
    g.gx = 0x00010000u;
    g.fail = 0;
    gp.cookie = &g;
    gp.read = gpucc_read;

    /* Use the counting bus: this group exercises caller SEMANTICS, not the
     * adapter (hardware-response simulation lets the plan reach the warm
     * check and mid-plan ops). */
    AdrenoGmuBootV1Bus cbus;
    CountSim cs;
    std::memset(&cs, 0, sizeof(cs));
    cbus.cookie = &cs;
    cbus.write = count_write;
    cbus.write_bulk = count_write_bulk;
    cbus.read = count_read;

    /* invalid inputs -> refuse before any traffic */
    AdrenoGmuBootV1Inputs bad = in;
    bad.gmu_image_words = 0u;
    check(AdrenoGmuCallerV1ColdBoot(&pre, &gp, &bad, 0, 0, &cbus, 0u, 0)
              == kAdrenoGmuCallerV1Invalid,
          "invalid cold inputs refused");
    check(cs.reads == 1u && cs.writes == 0u && cs.bulks == 0u,
          "invalid inputs: only the boot-state GENERAL_7 read happened");

    /* NULL bus */
    check(AdrenoGmuCallerV1ColdBoot(&pre, &gp, &in, 0, 0, 0, 0u, 0)
              == kAdrenoGmuCallerV1Invalid,
          "NULL bus refused");

    /* precheck refused BEFORE any GMU traffic: CX off (reset the counter
     * first — the invalid-inputs sub-check consumed one boot-state read). */
    cs.reads = 0u;
    g.cx = 0u;
    check(AdrenoGmuCallerV1ColdBoot(&pre, &gp, &in, 0, 0, &cbus, 0u, 0)
              == kAdrenoGmuCallerV1CxOff,
          "CX-off refuses before GMU traffic");
    check(cs.writes == 0u && cs.reads == 0u,
          "no GMU traffic on refused precheck");
    g.cx = 0x80000000u;

    /* warm refusal happens AFTER the GENERAL_7 read (GENERAL_7==1 -> WARM) */
    cs.reads = 0u;
    check(AdrenoGmuBootV1BootState(1u) == ADRENO_GMU_BOOT_V1_WARM,
          "GENERAL_7=1 decodes WARM (upstream :1066-1068)");
    cs.fail_read = 0;
    {
        /* Simulate WARM by making the boot-state read return 1: route through
         * a bus whose GENERAL_7 read returns 1. */
        WarmSim ws;
        std::memset(&ws, 0, sizeof(ws));
        AdrenoGmuBootV1Bus wbus;
        wbus.cookie = &ws;
        wbus.write = warm_write;
        wbus.write_bulk = warm_write_bulk;
        wbus.read = warm_read;
        uint32_t fail_op2 = 0xffffffffu;
        check(AdrenoGmuCallerV1ColdBoot(&pre, &gp, &in, 0, 0, &wbus, 0u,
                  &fail_op2) == kAdrenoGmuCallerV1WarmUnsupported,
              "warm boot refused after exactly one GENERAL_7 read");
        check(ws.reads == 1u,
              "exactly the GENERAL_7 read before warm refusal");
        check(ws.writes == 0u, "no writes before warm refusal");
    }

    /* bus read failure mid-plan (counting bus, fail after precheck read) */
    cs.fail_read = 0;
    cs.mid_fail_after = 1u; /* fail the read AFTER the GENERAL_7 boot-state read */
    check(AdrenoGmuCallerV1ColdBoot(&pre, &gp, &in, 0, 0, &cbus, 0u, 0)
              == kAdrenoGmuCallerV1BusError,
          "bus failure forwarded as BusError");
    cs.fail_read = 0;
    cs.mid_fail_after = 0u;
}

/* ---- group 6: first_fail_op flattening ---------------------------------------- */

static void testFlatten()
{
    std::printf("group 6: fail-op flattening across rpmh + cold plans\n");
    RetrySim r;
    AdrenoGmuBootV1Bus bus;
    AdrenoGmuBootV1Inputs in;
    struct AdrenoGmuCallerV1Pre pre;
    GpuccSim g;
    struct AdrenoGmuCallerV1Gpucc gp;
    uint32_t fail_op = 0xffffffffu;

    std::memset(&r, 0, sizeof(r));
    bus.cookie = &r;
    bus.write = retry_write;
    bus.write_bulk = retry_write_bulk;
    bus.read = retry_read;
    make_inputs(&in);
    make_pre(&pre, 0u);
    g.cx = 0x80000000u;
    g.gx = 0x00010000u;
    g.fail = 0;
    gp.cookie = &g;
    gp.read = gpucc_read;

    /* rpmh ucode supplied: the rpmh block is writes-only; the first poll is
     * the cold plan's ACK (flattened idx = rpmh_ops + 0). */
    r.hit_after = 100u;
    AdrenoGmuCallerV1Status st = AdrenoGmuCallerV1ColdBoot(&pre, &gp, &in,
        RSC_UCODE, PDC_UCODE, &bus, 0u, &fail_op);
    check(st == kAdrenoGmuCallerV1PollTimeout, "cold ACK poll times out");
    {
        AdrenoGmuBootV1Op ops[ADRENO_GMU_BOOT_V1_MAX_OPS];
        uint32_t rn = 0u;
        check(AdrenoGmuBootV1PlanRpmhInit(RSC_UCODE, PDC_UCODE, ops,
                  ADRENO_GMU_BOOT_V1_MAX_OPS, &rn) == kAdrenoGmuBootV1Success,
              "rpmh plan reproducible");
        check(fail_op == rn + 1u, "fail op flattened to cold ACK poll (rpmh_ops+1)");
    }

    /* no rpmh: cold plan first poll failing op must be a poll op inside the
     * cold plan (idx >= 0 relative to the cold block). */
    r.polls = 0u;
    r.hit_after = 100u;
    fail_op = 0xffffffffu;
    st = AdrenoGmuCallerV1ColdBoot(&pre, &gp, &in, 0, 0, &bus, 0u, &fail_op);
    check(st == kAdrenoGmuCallerV1PollTimeout, "cold-first poll times out");
    check(fail_op < ADRENO_GMU_BOOT_V1_MAX_OPS, "fail op inside plan bounds");
}

int main()
{
    testPrecheck();
    testBootState();
    testColdBootReplay();
    testPollRetry();
    testFailClosed();
    testFlatten();

    std::printf("gmu-caller-v1: %u checks, %u failures\n",
                g_checks, g_failures);
    if (g_failures != 0u) {
        return 1;
    }
    std::printf("PASS: A6XX_GEN1 powered-GMU caller (6 test groups)\n");
    return 0;
}
