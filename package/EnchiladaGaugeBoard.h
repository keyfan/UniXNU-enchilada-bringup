// SPDX-License-Identifier: BSD-2-Clause-Patent
// enchilada (OnePlus 6, SDM845) fuel-gauge I2C bus board facts, two independent
// sources each. Pure C inline helpers, no hardware access, no link deps.
//
//   sdm845.dtsi:1877 (Linux v6.12, GitHub contents API) i2c10: i2c@a88000
//     compatible="qcom,geni-i2c", reg=<0 0x00a88000 0 0x4000>,
//     clocks=<&gcc GCC_QUPV3_WRAP1_S2_CLK>, pinctrl-0=<&qup_i2c10_default>.
//   sdm845-oneplus-common.dtsi:507 &i2c10 { clock-frequency=<100000>;
//     bq27441-battery@55 { compatible="ti,bq27411"; reg=<0x55>; } }
//   vendor DTB enchilada.dts:4102 i2c@a88000, :4213 bq27541-battery@55
//     (compatible "ti,bq27541-battery") -> the two-source MODEL CONFLICT.
//   gcc-sdm845.c: gcc_qupv3_wrap1_s2_clk { enable_reg 0x5200c BIT(24),
//     halt_reg 0x18274 }; _s2_clk_src.cmd_rcgr 0x18278; M_AHB BIT(20)
//     halt 0x1800c; S_AHB BIT(21) halt 0x18010. (Touch SE12 uses S4 BIT(26),
//     NOT this — the load-bearing SE10 correction.)
//   pinctrl-sdm845.c: PINGROUP(55/56, NORTH, qup10, ...), function 1;
//     qup_i2c10_default pins "gpio55","gpio56", function "qup10".
#ifndef ENCHILADA_GAUGE_BOARD_H
#define ENCHILADA_GAUGE_BOARD_H
#include <stdint.h>
#include <stddef.h>

#define GB_QUP_WRAPPER_BASE 0x00ac0000ULL   // geniqup@ac0000 (QUP wrapper 1)
#define GB_SE_BASE          0x00a88000ULL   // i2c10 = i2c@a88000 (SE10)
#define GB_SE_SIZE          0x4000ULL
#define GB_I2C_ADDR         0x55u
#define GB_I2C_HZ           100000u         // &i2c10 clock-frequency

#define GB_GCC_BASE       0x00100000ULL
#define GB_GCC_VOTE       0x5200cu
#define GB_VOTE_M_AHB     (1u << 20)
#define GB_VOTE_S_AHB     (1u << 21)
#define GB_VOTE_S2        (1u << 24)        // gcc_qupv3_wrap1_s2_clk (NOT S4 BIT26)
#define GB_VOTE_ALL       (GB_VOTE_M_AHB | GB_VOTE_S_AHB | GB_VOTE_S2)
#define GB_HALT_M_AHB     0x1800cu
#define GB_HALT_S_AHB     0x18010u
#define GB_HALT_S2        0x18274u
#define GB_S2_RCG         0x18278u
#define GB_TCXO_HZ        19200000u
#define GB_CBCR_CLK_OFF   (1u << 31)
#define GB_CBCR_HW_CTL    (1u << 1)
// clk-rcg2 DFS register offsets relative to the RCG (cmd_rcgr) base.
#define GB_RCG_DFSR       0x14u
#define GB_RCG_CFG        0x4u
#define GB_RCG_PERF_CFG(l) (0x1cu + 4u * (l))
#define GB_RCG_PERF_M(l)   (0x5cu + 4u * (l))
#define GB_RCG_PERF_N(l)   (0x9cu + 4u * (l))
#define GB_DFS_LEVELS     8u

#define GB_TLMM_BASE      0x03400000ULL
#define GB_TILE_NORTH     0x00500000u
#define GB_PIN_STRIDE     0x1000u
#define GB_GPIO_SDA       55
#define GB_GPIO_SCL       56
#define GB_FUNC_QUP10     1
#define GB_PULL_NONE      0u
// CTL: pull 1:0, function 5:2, drive 8:6 ((mA/2)-1), OE bit 9.
static inline uint32_t gb_pin_cfg(unsigned func, unsigned drive_ma, unsigned pull, int output) {
    unsigned code = drive_ma >= 2 ? drive_ma / 2 - 1 : 0;
    if (code > 7) code = 7;
    return (pull & 3u) | ((func & 15u) << 2) | (code << 6) | (output ? (1u << 9) : 0u);
}
static inline uint32_t gb_pin_offset(unsigned gpio) {return GB_TILE_NORTH + GB_PIN_STRIDE * gpio;}

// A branch is running when CLK_OFF is clear (or hardware-gated idle).
static inline int gb_branch_running(uint32_t cbcr) {
    return !(cbcr & GB_CBCR_CLK_OFF) || (cbcr & GB_CBCR_HW_CTL);
}
// gcc_parent_map_0 source rates: 0 TCXO 19.2 MHz, 1 GPLL0 600 MHz, 6 GPLL0/2.
static inline uint64_t gb_src_hz(unsigned sel) {
    return sel == 0 ? GB_TCXO_HZ : sel == 1 ? 600000000ULL : sel == 6 ? 300000000ULL : 0;
}
// clk-rcg2 calc_rate: half-integer hid divider, then M/N (N stored as ~(N-M)).
static inline uint64_t gb_rcg_hz(uint32_t cfg, uint32_t mreg, uint32_t nreg) {
    const unsigned src = (cfg >> 8) & 7u, mode = (cfg >> 12) & 3u, hid = cfg & 0x1fu;
    uint64_t rate = gb_src_hz(src);
    if (hid) rate = rate * 2 / (hid + 1);
    if (mode) {
        const uint32_t m = mreg & 0xffffu, n = ((~nreg) & 0xffffu) + m;
        if (n) rate = rate * m / n;
    }
    return rate;
}
// DFS level whose rate is exactly 19.2 MHz (TCXO), as geni_se_clk_freq_match
// finds it. Without DFS the current config must already be 19.2 MHz -> level 0.
// Returns the level index or -1. (Byte-for-byte the device-verified touch helper.)
static inline int gb_xo_level(uint32_t dfsr, const uint32_t *cfg, const uint32_t *m,
                              const uint32_t *n, unsigned levels, uint32_t cfg_now) {
    if (!(dfsr & 1u))
        return gb_rcg_hz(cfg_now, 0, 0xffffu) == GB_TCXO_HZ && !((cfg_now >> 12) & 3u) ? 0 : -1;
    for (unsigned l = 0; l < levels; l++)
        if (gb_rcg_hz(cfg[l], m[l], n[l]) == GB_TCXO_HZ) return (int)l;
    return -1;
}
#endif
