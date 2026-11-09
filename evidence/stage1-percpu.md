# Stage 1 CPU — per-core execution evidence

Device: OnePlus 6 (enchilada, SDM845)
OS profile: iPadOS 17.7.11 (21H461)
Status: **device-verified** (this document is the device evidence for the `cpu` area)

## What this document answers

The `cpu` area recorded this `next_action` until round 13:

> Add an explicit per-CPU checkpoint counter to the boot log to turn `procs=263` into per-core evidence.

That action is **already satisfied by code that has been in the image for many rounds**
(`package/BootLog.c:1308` calls `sched_pcpu_tick()`, implemented in
`patches/xnu-device/enchilada-pcpu.patch`). The counter was present; the *record* had
never pointed at the evidence. This document does that, and nothing in the kernel changed
to produce it.

## Why an aggregate process count was not enough

`procs=263` is a single number for the whole machine. It cannot distinguish:

* eight secondaries actually scheduled on, from one CPU running everything while the
  other seven sit in the idle loop after merely reaching their entry checkpoint; and
* a scheduler that loads all work onto CPU 0 (a plausible failure when only the boot CPU
  has a valid topology entry), from a scheduler that distributes it.

Per-CPU cumulative user time and a per-CPU online mask answer both. They also answer them
*without* requiring a busy machine: the counters are cumulative, so they survive quiet
windows, unlike a per-window utilisation percentage.

## The two log rows that carry the evidence

Both are emitted by `patches/xnu-device/enchilada-pcpu.patch` and are written into the
per-boot `slots.txt`/`latest.txt` region by `package/BootLog.c:983`, i.e. into the region
that survives the 128 KiB message-ring wrap (measured wrap points in this configuration:
101.2 s / 87.7 s / 58.8 s — see `evidence/stage7-gauge-device.md` for the measurement).

Verbatim from `work/log/persist/untagged-20eab26c/latest.txt` (boot 552252012, 899 s):

```
[sched] pcpu=0xf0 rcpu=0x0 online=0xff picks in=124595 out=48696 passed=25246 qavoid=3713 | 0=0/S0/s0 1=100/S0/s0 2=200/S0/s0 3=300/S0/s0 4=400/S0/s0 5=500/S0/s0 6=600/S0/s0 7=700/S0/s0 rec=0xff
[sched busy] win=5006ms user/busy% c0=0/0 c1=0/0 c2=0/0 c3=0/0 c4=0/1 c5=0/0 c6=0/1 c7=0/3 | user s 10.0 9.2 7.9 7.8 17.7 17.1 18.0 19.0
```

Verbatim from `work/log/stage4welcome/untagged-20ae6e8f/latest.txt`
(boot 548302479, 898 s):

```
[sched] pcpu=0xf0 rcpu=0x0 online=0xff picks in=220675 out=96111 passed=66814 qavoid=4931 | 0=0/S0/s0 1=100/S0/s0 2=200/S0/s0 3=300/S0/s0 4=400/S0/s0 5=500/S0/s0 6=600/S0/s0 7=700/S0/s0 rec=0xff
[sched busy] win=5006ms user/busy% c0=0/0 c1=0/0 c2=0/0 c3=0/0 c4=0/3 c5=0/0 c6=0/0 c7=0/1 | user s 19.5 17.7 16.9 16.0 34.6 35.6 32.9 33.8
```

Field meanings, each read off the patch source rather than guessed:

| field | source | meaning |
| --- | --- | --- |
| `pcpu` | `pcpu=` in the patch's `sched_pcpu_tick` | the boot-arg preferred-CPU mask; `0xf0` = CPUs 4–7, i.e. the four Kryo 385 Gold cores |
| `online` | `rcpu`/`online` masks | bit *k* set means CPU *k* was online when the row was written; `0xff` = all eight |
| `picks in/out/passed/qavoid` | scheduler counters | thread-pick attempt / rejection / acceptance counts for the boot |
| `k=phys/Tclu/pset` | `ml_get_topology_info()` → `ti->cpus[k]` | per-CPU `phys_id`, `cluster_type`, `cluster_id`, `processor_set` index |
| `rec` | `is_recommended` bitmap | which of the eight are recommended (none are held back) |
| `user s c0..c7` | `recount_processor_usage` → `RCT_LVL_USER.rm_time_mach` | **cumulative** user seconds per CPU since boot |

## The measurement

`work/tmp/_percpu_detail.py` reads `slots.txt` (the per-second region), takes the
maximum cumulative value seen for each CPU over the whole boot (not the last row — the
last row can land in a quiet window), and checks four invariants. Output:

```
=== persist / untagged-20eab26c ===
  [sched] pcpu=0xf0 rcpu=0x00 online=0xff picks in=109203 out=47340 passed=24310 qavoid=3687
  cpus described: [0, 1, 2, 3, 4, 5, 6, 7]
  distinct phys ids: 8 -> ['0', '100', '200', '300', '400', '500', '600', '700']
  cumulative user s never decreased: True (violations=0)
  preferred(pcpu)= [4, 5, 6, 7]   others= [0, 1, 2, 3]
  cpu   phys type clu pset rec  user_s
  c0  0x    0    S   0    0 yes    10.0
  c1  0x  100    S   0    0 yes     9.2
  c2  0x  200    S   0    0 yes     7.9
  c3  0x  300    S   0    0 yes     7.8
  c4  0x  400    S   0    0 yes    17.7
  c5  0x  500    S   0    0 yes    17.1
  c6  0x  600    S   0    0 yes    18.0
  c7  0x  700    S   0    0 yes    19.0
  mean preferred = 17.9 s, mean others = 8.7 s, ratio = 2.06

=== stage4welcome / untagged-20ae6e8f ===
  [sched] pcpu=0xf0 rcpu=0x00 online=0xff picks in=164859 out=72354 passed=46357 qavoid=4690
  cpus described: [0, 1, 2, 3, 4, 5, 6, 7]
  distinct phys ids: 8 -> ['0', '100', '200', '300', '400', '500', '600', '700']
  cumulative user s never decreased: True (violations=0)
  preferred(pcpu)= [4, 5, 6, 7]   others= [0, 1, 2, 3]
  cpu   phys type clu pset rec  user_s
  c0  0x    0    S   0    0 yes    19.5
  c1  0x  100    S   0    0 yes    17.7
  c2  0x  200    S   0    0 yes    16.9
  c3  0x  300    S   0    0 yes    16.0
  c4  0x  400    S   0    0 yes    34.6
  c5  0x  500    S   0    0 yes    35.6
  c6  0x  600    S   0    0 yes    32.9
  c7  0x  700    S   0    0 yes    33.8
  mean preferred = 34.2 s, mean others = 17.5 s, ratio = 1.95

PERCPU_NUMBERS: PASS
```

## What each invariant rules out

1. **Eight distinct `phys_id` values `0x0..0x700`.** Eight entries sharing one `phys_id`
   would mean one core described eight times, not eight cores. They are distinct.
2. **`online=0xff` on both boots, plus eight CPU tokens in the same row.** A boot where
   the secondaries never came online logs `online=0x01` and describes one CPU. Ten of the
   seventy-four device boots in `work/log/` do exactly that (early boot, before secondaries
   start), which is why the mask is checked rather than assumed.
3. **All eight cumulative values non-zero.** A core that came online but was never
   scheduled keeps its counter at `0.0`.
4. **Cumulative time never decreases** across the boot's 509 per-second samples
   (`violations=0`). A snapshot bug would show values shrinking.
5. **Preferred/other ratio 2.06 and 1.95.** The image boots with `pcpu=0xf0`, so
   work *should* favour CPUs 4–7. It does, by roughly 2×, on both boots — while CPUs 0–3
   still accumulate 7.8–19.5 s. This distinguishes "preference is honoured" from both
   "preference is ignored (ratio ≈ 1)" and "non-preferred cores are starved (others ≈ 0)".

Together the five say: eight cores were online, all eight ran user code, the scheduler's
stated preference was visibly honoured, and no core was starved.

## Cross-checked against a second, independent reader

`work/tmp/_analyze_percpu_b.py` answers the same question by a different route: it reads
`slots.txt` rather than `latest.txt`, takes per-CPU maxima rather than final values, and
derives the Silver/Gold split from the `pcpu` mask instead of hard-coding CPUs 4–7. On the
five boots it examined it reports `nOn=8 nTopo=8` and preferred/other ratios of
**2.06 / 1.69 / 1.97 / 2.00 / 1.95**, i.e. the same conclusion from different arithmetic.
A third check, `work/tmp/_check_kernel_sched_strings.py`, confirms the *shipped* kernels
carry the emitting code: CI #212/#213/#215 kernels each contain `[sched busy]` (×1),
`[sched] pcpu=` (×1), `sched_pcpu_tick` (×6), `user/busy%` (×1) and `online=0x` (×1).

## Honest boundaries

* **Cumulative counters, not utilisation.** The per-window `user/busy%` columns read
  `0/0` for almost every sample in these boots (20 of 4072 non-zero on one boot, 208 on
  another; maxima 4 % and 10 %). This is a mostly idle machine writing a boot log, so the
  claim here rests on cumulative time and the online mask, never on a rate. Anything
  resembling "each core runs at N %" would not be supported by this data.
* **No SMP stress test.** Nothing here exercises contention: no parallel memory stress, no
  IPI storm, no CPU hotplug. What is shown is that all eight cores are online and all
  eight execute, not that the scheduler behaves correctly under load.
* **Cluster type is flat.** Both boots report `S` for all eight CPUs, with `pset 0` and
  `cluster_id 0`. XNU sees one processor set, matching the recorded `limits` entry that
  there is no `E`/`P` cluster-type patch for this flat 8-core SDM845 topology. This
  document does not change that limit and does not claim heterogeneous scheduling.
* **`rcpu=0x0`.** No CPU was held back by the recommended mask, and `rec=0xff` agrees.
  A boot where a core was deliberately parked would show a different bit here.
* **`win=5006ms` is a sampling window, not a measurement interval guarantee.** The row is
  written every 5 s of `sched_pcpu_tick`; the counters inside are cumulative since boot.

## Reproduce

```
python scripts/test-cycle.py persist --wait-return 1000      # boots with deadline=900
python -X utf8 work/tmp/collect_persist.py
python -X utf8 work/tmp/_percpu_detail.py
python -X utf8 work/tmp/_analyze_percpu_a.py
python -X utf8 work/tmp/_analyze_percpu_b.py
python -X utf8 work/tmp/_check_kernel_sched_strings.py
```

The image used (`payload-persist.img`) carries the ordinary arguments, including
`pcpu=0xf0`, `sheng-smp=1` and `enchilada-cpufreq=2`; no argument was added or removed to
produce this evidence. `payload-persist.img` sha256
`b037538b0c84ab0fc782829a38bfb0d458cfff31e044e2fcbd495538c93a1abb`,
kernel sha256 `64f40cc1054820e803c18b9b47332aedf110000d9285ec3377721be65b23b7ef`.
