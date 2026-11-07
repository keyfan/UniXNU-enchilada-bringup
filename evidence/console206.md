# console206 — device-verified boot of the CDC-ACM USB serial console

Recorded 2026-11-07 from `work/log/console206/untagged-20ea3b20/latest.txt`
(boot_id `0x20ea3b20`, uptime 599 s) and `work/tmp/hostwatch206.log`.

- Source revision of the built kernel:
  `7eeda1e01aa906ff3b2ac5fa58fdcfd244701c59` (`keyfan/UniXNU-enchilada-bringup`, main).
- CI run that produced the kernel: #206 (`42258192190`), kernel sha256
  `be5f23cc54a4a29dd1e92e6e8600e4089efd63795ad35f971631e6aefb404a30`
  == `payload.json` `kernel_sha256`.
- Command: `python scripts/test-cycle.py console206 --wait-return 900`
  (fastboot flash `system_b`, boot, wait for the 600 s deadline to return to recovery).
- Host command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File work/tmp/hostwatch.ps1 -Seconds 720 -Out work/tmp/hostwatch206.log`.

## Boot log lines (verbatim, `latest.txt:4-5, 70-85`)

```
time=601s idle=4s IRQ=1118518 monitor=600 lost=4314 root=1 procs=263 free=10006
UFS requests=302995 verified4K=1186660 failures=0 integrity=0 published=2 skips=0
U st=5 rc=0 mode=3 id=5533300a gctl=00102000 dcfg=00080804 dsts=00d3313c alep=0 cap=2 phy=1 gdsc=f8222000 smmu=6/4 fw=1 hp3=10420085 eps=16
G st=6 rc=0 ev=161373 rst=2 cmd=161323 bad=2 spd=0 addr=49 dcfg=00080988 dctl=80f00000 dsts=0002fd68
G ev loop polls=263439 gevent=00000004 last=0000c046 ep0=00000001
G phy before=00002500 after=00002400 susp=0->0
G qs hs=10000000->10100000 vbus=0->1 sel=1->1 ss=00000000->01000000 lane=0->1
C ev=186 setups=39 ans=39 cfg=1 last=21220003 addr=49 spd=0
C arm=161297 hwo=0 ctl=00000000
C cdc eps=1 armed=2 stuck=0 in=1 out=12035 lc=1 dtr=1
C io tx=2998527 rx=2 ev=1 q=3006974 drop=746 arm=12036 abort=0 last=0a0d0000
C dbg fail=00000601 armf=2 nr=74/0102 sa=39 sd=39 stuck=0 out=35
C setup n=39 a1210007 a1210007 21200007 a1210007 21220000 21200007 a1210007 21220000
C st nr in=11 out=28 skip=0
C notify armed=149147 done=149146
C lc baud=0001c200
G smmu slot=6 bank=4 plan=80000740/00000004 now=80000740/00000004 cb=000000e0
S st=2 dirs=6574 files=4214 mib=445 fail=0 err=0
```

## Host-side log (`hostwatch206.log`, abridged)

```
0.0s watch start
26.7s device present: OK / USB 串行设备 (COM5)
26.9s opened COM5 (DTR=True RTS=True)
26.9s wrote 2 bytes to the port
26.9s read 12288 bytes: #slot 1 t=0s tag= | [page] | UniXNU boot diagnostics...
... 189 occurrences of "read 16384 bytes" ...
627.1s device gone
720.1s watch end
```

No `open COM5 failed`, `semaphore timeout`, or `access denied` in the whole run
(console205 had 17 `The semaphore timeout period has expired.` and never opened).

## What this proves

- CPU / interrupts / timers / SMP: `root=1 procs=263`, monitor ran 600 s.
- Memory / DMA / SMMU: UFS and DWC3 streams use firmware bypass slots
  (`smmu=6/4`, `G smmu ... now=80000740/00000004`), `UFS ... failures=0`.
- Storage / root / Data: `UFS requests=302995 ... failures=0`,
  `S st=2 dirs=6574 files=4214 mib=445 fail=0`.
- Packaging: the CI-built payload was flashed and booted; kernel sha matched.
- Diagnostics / USB console: Windows enumerated `USB\VID_1209&PID_0001` as
  `USB 串行设备 (COM5)`, `SerialPort.Open()` succeeded at 115200
  (`C lc baud=0001c200`), and 189 consecutive 16 KiB reads streamed the boot log.
