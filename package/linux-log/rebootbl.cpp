// SPDX-License-Identifier: MIT
// Leaving the reader: power-cycle the phone.
//
// The sheng reader reboots with reboot(LINUX_REBOOT_CMD_RESTART2, "bootloader") and lets
// its device tree's nvmem-reboot-mode node translate that into the boot mode the ABL
// reads from the PMIC SDAM reboot-reason cell. The enchilada device tree shipped inside
// the postmarketOS boot image has no such node -- `reboot` and `sdam` both occur zero
// times in its 121,416 bytes, and nvmem-reboot-mode.ko has nothing to bind to -- so the
// same syscall here would restart into whatever Android the reboot reason points at,
// not into fastboot.
//
// A plain cold restart is therefore the honest action: LINUX_REBOOT_CMD_RESTART, no
// argument. The operator gets the normal Android/TWRP boot and can issue
// `adb reboot bootloader`; scripts/read-enchilada-log.py prints that reminder when it
// runs this. Static, libc-free aarch64 Linux program, built like sheng's.
namespace {
long sys(long n, long a = 0, long b = 0, long c = 0, long d = 0) {
    register long x8 asm("x8") = n, x0 asm("x0") = a, x1 asm("x1") = b, x2 asm("x2") = c, x3 asm("x3") = d;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}
enum : long {SysWrite = 64, SysExit = 94, SysSync = 81, SysReboot = 142};
// <linux/reboot.h>: LINUX_REBOOT_CMD_RESTART = 0x01234567
enum : long {CmdRestart = 0x01234567, Magic1 = 0xfee1dead, Magic2 = 672274793};
}  // namespace

extern "C" [[noreturn]] void _start() {
    sys(SysSync);
    sys(SysReboot, Magic1, Magic2, CmdRestart, 0);
    static const char failed[] = "rebootbl: reboot failed\n";
    sys(SysWrite, 2, (long)failed, sizeof failed - 1);
    sys(SysExit, 1);
    for (;;) {}
}
