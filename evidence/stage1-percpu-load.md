# Stage 1 CPU — per-core behaviour under real device load

Device: OnePlus 6 (enchilada, SDM845)
OS profile: iPadOS 17.7.11 (21H461)
Status: **device-verified** (this document is additional device evidence for the `cpu` area)

## What this document answers

Until round 16 the `cpu` area carried two `limits` entries and one `next_action`
that were written in round 13 from just two mostly-idle boots:

> No SMP stress test has been run, so nothing here exercises contention, IPI storms or
> CPU hotplug.

> The per-window user/busy percentage columns read zero for almost every sample on these
> mostly-idle boots, so the per-core claim rests on cumulative counters and the online
> mask, never on a rate.

> Stress the scheduler before claiming it is correct under load ...

Those statements are true of the two boots that were cited, but they are not true
of the corpus. A read-only re-analysis of **all 118 archived device boots** finds many
boots with genuine, sustained, eight-core load. No kernel code changed and no new
flash was performed to produce this document: the load was already present in the
archived `work/log/` data, exactly as `stage1-percpu.md` had already noted the
counter code was already present. Following "先核验再上机" (verify before flashing),
the existing device corpus was mined first instead of building a synthetic stress
image.

## Why the first pass was wrong (methodology, load-bearing)

`patches/xnu-device/enchilada-pcpu.patch` writes one `[sched busy]` row every 5 s
(`if (calls++ % 5 != 0) { return; }`, `win=5006ms`). But `slots.txt` is a
per-second dump: each 5-second window is re-written into roughly ten consecutive
one-second slots. So a boot with `slot_count=509` has 509 `[sched busy]` **lines**
but only **51–55 distinct** rows (`dict.fromkeys` on the lines). An early miner
counted lines as independent windows and reported inflated window counts; a first
independent verifier caught the 510-vs-509 discrepancy, which came from also
double-reading `latest.txt`. The final analysis:

* reads **only `slots.txt`** (the authoritative per-second region),
* **de-duplicates** identical rows with `dict.fromkeys`, and
* considers each distinct row one genuine 5-second window.

`win_ms` is 5005–5032 throughout, confirming the ~5 s cadence.

## What `user%` and `busy%` actually mean (checked against XNU source)

The patch computes, per CPU `id`, over window `win`:

```
recount_processor_usage(&p->pr_recount, &usage, &idle);
uint64_t user = usage.ru_metrics[RCT_LVL_USER].rm_time_mach,
         busy = recount_usage_time_mach(&usage);
up = (unsigned int)((user - last_user[id]) * 100 / win);
bp = (unsigned int)((busy - last_busy[id]) * 100 / win);
```

Third, independent check against the XNU source in `work/tmp/diag-box/osfmk/kern/`:

* `recount.h:104-115` declares `RCT_LVL_KERNEL, RCT_LVL_USER, [RCT_LVL_SECURE], RCT_LVL_COUNT`
  — there is **no `RCT_LVL_IDLE`** level.
* `recount.c:456-463` `recount_usage_time_mach()` sums `ru_metrics[i].rm_time_mach`
  for `i < RCT_LVL_COUNT` only.
* `recount.c:1236-1252` `recount_processor_usage()` returns idle via the separate
  output parameter `*idle_time_out` (from `pr->rpr_idle_time_mach`), not through
  `ru_metrics`.

Therefore `busy%` is genuine non-idle CPU time (kernel + user + secure), and
`user%` is the `RCT_LVL_USER` delta over the window. A row with `c4=67/99` means
CPU 4 spent 67 % of that 5-second window in user code and 99 % non-idle. This is
not idle-loop noise. The per-window columns are only "almost always zero" on the
two idle boots that round 13 happened to cite.

## Two independent readers

`work/tmp/_analyze_cpu_load_final.py` parses every distinct row twice, with two
independent parsers that must agree on the entire tuple `(win_ms, user[8],
busy[8], cumulative[8])` for every boot:

* **Parser A** — whitespace `split()` with prefix checks.
* **Parser B** — regexes `c([0-7])=(\d+)/(\d+)`, `win=(\d+)ms`, and a cumulative
  `| user s` capture.

`all_parsers_agree: True` across all 118 boots. A separate verifier
(`work/tmp/_verify_percpu_load.py`) independently re-derived the patch formulas
and cross-checked the per-core concurrency numbers on six named boots; its earlier
510-vs-509 failure is the dedup bug described above, now resolved.

## The measurement (verbatim)

Aggregate over the 118 device boots in `work/log/`:

```
boots_analysed: 118
total_distinct_windows: 3987
global_max_user_pct: 79
global_max_busy_pct: 100
boots_with_all8_user_ge5: 46
boots_with_all8_user_ge20: 31
boots_with_4core_user_ge5: 50
max_windows_all8_user_ge5_in_one_boot: 55
```

So 46 boots contain at least one 5-second window in which **all eight cores** were
at ≥5 % user simultaneously, and 31 boots contain a window with all eight at
≥20 % user. Heaviest boots (all are real UI/touch workloads: `latest.txt` contains
`strokes=`, `TOUCH HID`, `SpringBoard`, `root=1`):

```
console172/untagged-20c1f871:   boot=549582961 uptime=399s distinct=54 maxUser=73% all8>=5:18 all8>=20:14
namei/untagged-20dc0bfc:        boot=551291900 uptime=298s distinct=54 maxUser=79% all8>=5:50 all8>=20:8
console193/untagged-20e46ae6:   boot=551840486 uptime=598s distinct=54 maxUser=65% all8>=5:15 all8>=20:8
dataresume/untagged-20aa45e1:   boot=548029921 uptime=298s distinct=54 maxUser=62% all8>=5:37 all8>=20:7
dataswipe/untagged-20d1d745:    boot=550623045 uptime=298s distinct=54 maxUser=59% all8>=5:12 all8>=20:7
dataswipe/untagged-20e883ca:    boot=552109002 uptime=298s distinct=55 maxUser=54% all8>=5:8  all8>=20:6
gaugeprobe240/untagged-20c1ac54:boot=549563476 uptime=238s distinct=48 maxUser=57% all8>=5:8  all8>=20:5
```

`work/log/console172/untagged-20c1f871/header.txt`: `magic=SHLOG001`,
`version=4`, `boot_id=549582961`, `slot_count=509`, `slots_written=755`,
`uptime_s=399`, `rounds=755`, `write_errors=0`, `msg_bytes=131072`, `state=2`.

**Strongest single window**, verbatim from that boot's `slots.txt`:

```
[sched busy] win=5006ms user/busy% c0=66/99 c1=64/99 c2=71/99 c3=67/100 c4=67/99 c5=68/99 c6=64/100 c7=67/99 | user s 28.0 23.9 22.0 20.4 69.5 70.9 70.5 70.2
```

All eight cores at 64–71 % user and 99–100 % non-idle in the same 5-second window.
Cumulative user time (the `| user s ...` tail) grows on every core across the boot:

```
cumulative first = [10.0, 8.0, 6.7, 6.0, 19.6, 19.7, 19.0, 20.6]
cumulative last  = [49.4, 45.1, 42.6, 40.5, 95.3, 96.5, 96.9, 96.2]
growth on 8 cores = [39.4, 37.1, 35.9, 34.5, 75.7, 76.8, 77.9, 75.6]
```

No core's cumulative user time decreases, and the preferred cores (4–7) still
accumulate roughly twice the user seconds of cores 0–3 (95.3–96.9 s vs 40.5–49.4 s,
ratio ≈ 1.96), so the `pcpu=0xf0` preference from `stage1-percpu.md` still holds
under load.

## What the evidence does and does not establish

**Establishes (device-verified):** the eight cores of this SDM845 do not merely come
online and execute once — under real SpringBoard / touch / storage workloads, all
eight cores execute user code **concurrently** in the same 5-second sampling
windows, at high utilisation (up to 79 % user / 100 % non-idle on a single core,
all eight ≥20 % user in 31 boots), with correct cumulative accounting and the
preferred-core bias intact. The per-window rate columns are meaningful on loaded
boots; they are not "always zero".

**Does NOT establish (honest boundaries — do not overclaim):**

* This is **not a controlled adversarial stress test**. There is no deliberately
  constructed parallel memory + IPI load with per-CPU progress checkpoints, so it
  does not prove scheduler correctness under contention, IPI storms, lock convoy,
  or race conditions.
* **No CPU hotplug** is exercised; every boot here is a steady eight-core run.
* The load is a **real but unmeasured** UI/touch workload; its composition is not
  instrumented, so "8 cores ran concurrently" is shown, while no claim is made about
  behaviour at saturation or freedom from deadlocks.
* `busy%` includes kernel and secure time; a high busy value with moderate user
  value is consistent with kernel work, not proof of user-level parallelism per se —
  the concurrent claim rests on the `user%` columns plus cumulative growth.
* **Cluster type is still flat** (all `S`, one processor set); this document does
  not add an E/P patch and does not claim heterogeneous scheduling.

Accordingly the `cpu` `next_action` is narrowed, not erased: a purpose-built
parallel memory + IPI checkpoint stress test and CPU hotplug remain open items, and
the question of whether the flat topology should ever be given cluster types is
still open.

## Reproduce

```
python -X utf8 work/tmp/_walk_strict_order.py          # confirm cpu is the earliest open area
python -X utf8 work/tmp/_mine_percpu_load.py           # corpus miner (slots.txt only)
python -X utf8 work/tmp/_verify_percpu_load.py         # independent verifier + patch-formula check
python -X utf8 work/tmp/_diag_rowcount.py              # 510-vs-509 double-count diagnosis
python -X utf8 work/tmp/_characterize_load.py          # distinct-window count + workload markers
python -X utf8 work/tmp/_analyze_cpu_load_final.py     # dedup + 2 parsers + XNU semantics + growth
```

The final script writes `work/tmp/cpu_load_final.json` and prints
`CPU_LOAD_FINAL: PASS`. No image was built or flashed for this document; every
number comes from the archived device logs in `work/log/`, cross-checked against
`patches/xnu-device/enchilada-pcpu.patch` and the XNU `recount` source in
`work/tmp/diag-box/osfmk/kern/`.
