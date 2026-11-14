# ROUND 124 — PAS 实测落盘（DEVICE-VERIFIED）

**状态行：`DEVICE-VERIFIED`** —— `PAS_AUTH_AND_RESET(13)` 这一组 SMC **确实在真机上发出去了**
（`ran=1 calls=1`，设备原始行见 §2）。本文件只写实测到的字节与代码事实；
**不对 secure world 的内部行为做任何推断**，`t0` 的语义**未解析**。

**日期**：2026-11-14（ROUND 124 落盘；实测发生于 ROUND 123）
**设备**：`27f4f45d` / OnePlus 6（enchilada，SDM845）/ TWRP 3.4.0-3
**镜像**：CI #251（run `43579455091`，conclusion=success，10m39s）

> 本文件**取代** `evidence/stage5-gpu-pas-device.md` 的 17:42 草稿版本。
> 被取代的原因不是数值错，而是**证据链缺陷**：草稿引用了一次没有留下任何工件的启动
> （内核时间 `34.754237`），并把该行当成"第二次独立启动"的复现证据。详见 §6。

---

## 一、这次实验到底做了什么

在 `enchilada-adreno=6` **之外**再加第二把钥匙 `enchilada-pas-exec=1`（`board.toml:408`），
让 probe 的 stage-7 从"打印计划"进入"真的发 SMC"。

三段调用（`drivers/NativeAdreno/ArenoZapAuthV1.c` 的 `adreno_zap_pas_execute`）：

| 段 | 调用 | `svc` | `cmd` | `fnid` | `arginfo` | 语义 |
|---|---|---|---|---|---|---|
| 0 | `pas_init_image` | `0x02` | `0x01` | `0x0201` | `0x82` | ARGS(2, VAL, RW)：peripheral + metadata 物理地址 |
| 1 | `pas_mem_setup` | `0x02` | `0x02` | `0x0202` | `0x03` | ARGS(3)：peripheral + region 物理地址 + 字节数 |
| 2 | `pas_auth_and_reset` | `0x02` | `0x05` | `0x0205` | `0x01` | ARGS(1)：只有 peripheral |

- `x0 = base | fnid = 0xC2000000 | 0x02xx`（`kAdrenoZapSmcBaseDerived`，`AdrenoZapAuthV1.h:200`）
- `peripheral = kAdrenoZapPasId = 13`（`AdrenoZapAuthV1.h:131`，来自 `enchilada.dts:23612`）
- 寄存器映射按 P6（`qcom_scm-smc.c`）修正后的排布：`x0=fnid`、`x1=arginfo`、
  `x2=args[0]=peripheral`、`x3=args[1]`、`x4=args[2]`

---

## 二、设备实测行（逐字，261 字节，不可改写）

```
enchilada-adreno: stage7=exec why=- armed=1 exec_key=1 sym=1 blob=embedded seg=1968 region=8c415000+2000 stages=3 seq_ok=1 mdt_ck=0980cf98 seg_ck=5a8c8234 mdt_len=6684 x0=c2000201/c2000202/c2000205 rc=4 ran=1 calls=1 t0=ffffffffffcffff6 s0=0 t1=0 s1=0 t2=0 s2=0
```

**来源工件（可复核）**：`oneplus6-ios/work/tmp/r123_window.bin`
- 大小 **33,554,432 B**，sha256 **`84730f746badb1499b185d42acd15a025bc26f8acd35829f52171732a22ef2bf`**
- 该窗口内 `stage7=` 出现 **1** 次、`Darwin Kernel Version` 出现 **1** 次 → **单次启动**
- 内核时间戳 `[   35.573027]`

窗口内其余实测计数（本文件全部由 `r123_window.bin` 直接重数，不引用他人结论）：
`pas-exec`=0、`panic(cpu`=0、`Debugger message`=0、`BARK`=0、`BITE`=0、`CrashDump`=0。

---

## 三、六个 t/s 值逐一判读

probe 把两条通道**分开**上报（`EnchiladaAdrenoProbe.cpp:1012-1016` 的注释即为此）：
`t<j>` = `smc a0`（transport 层）、`s<j>` = `smc a1`（secure `result[0]`）。

| 段 | 调用 | `t`（transport, a0） | `s`（secure, a1） | 判读 |
|---|---|---|---|---|
| 0 | `pas_init_image` | `ffffffffffcffff6` = **−3145738** | `0` | **本段 transport 非零 → 失败** |
| 1 | `pas_mem_setup` | `0` | `0` | **从未执行**（fail-closed），`0` 是初始值 |
| 2 | `pas_auth_and_reset` | `0` | `0` | **从未执行**（同上），`0` 是初始值 |

### 3.1 为什么 `s0=0` 不能读作"secure world 接受"

`zapBridgeCall()`（`EnchiladaAdrenoProbe.cpp:320-357`）在 `rc != 0`（transport 被包装层拒绝）时
**直接 `return -1`，不写 `out_result0`**；只有在 `rc == 0` 时才 `out_result0[0] = secure`。
而 `adreno_zap_pas_execute` 在进入循环前就把 `out->stage[i].secure_result` 全部初始化为 `0`
（`AdrenoZapAuthV1.c:1219-1226`）。

因此 `s0=0` 是**未被改写的初始值**，不是 secure world 的表态。同理 `t1=t2=s1=s2=0`
是**从未尝试**的初始零，绝不是"后两段被接受"。

### 3.2 `t0` 是不是 `-EBUSY`？不是（已逐位核对）

| 量 | 值 |
|---|---|
| 实测 `t0`（u64） | `0xffffffffffcffff6` |
| 实测 `t0`（i64） | **−3145738** |
| 低 32 位 | `0xffcffff6`（int32 也是 −3145738） |
| `-EBUSY` | `0xfffffffffffffff0` = −16 |

**不相等**。probe 按 `EnchiladaAdrenoProbe.cpp:356`（`return static_cast<int>(transport & 0xFFFFFFFFu);`）
把 `smc a0` 截断成 32 位，因此这**不是 errno**，而是 secure world 原样回传的 `a0`。

### 3.3 `rc=4` 的含义（有代码锚点）

`AdrenoZapAuthV1.h:524`：`kAdrenoZapExecStageFailed = 4` —— "某一段的 transport/secure 结果 != 0"。
与 `t0 != 0` 完全一致，说明 fail-closed 按设计中止。

### 3.4 该返回码的性质：**未解析**

它不是 `-EBUSY/-EINVAL/-EIO/-EPERM` 中任何一个。它可能来自：
secure world 对该调用的拒绝码、`armcpu_scm_call` 包装层的错误路径、或 QCOM SCM 协议层。
**未解析就等于未证明。本文件不声称知道它的含义。**

---

## 四、与 ROUND 105（K3 重分类）的关系

ROUND 105 把 K3 从 `BLOCKED` 重分类为 **CONDITIONALLY FEASIBLE / IMPLEMENTABLE-WITH-EFFORT**，
并明确写下唯一的诚实未知（`evidence/stage5-gpu-k3-feasibility.md`）：
**"secure world 会不会接受这个 EL1 内核发起的 `PAS_AUTH_AND_RESET(13)`"**，
且该文件自己强调 `This audit does NOT unblock stage 5`。

**本次实验就是那个未知的直接实验。** 结果是：

- 未知被**部分地回答**：调用**送得进去**（`sym=1`、`ran=1`、`calls=1`）——
  即 R105 所说的"机制层为真"在真机上被证实到"能发出并拿回返回码"这一步。
- 但**关键的那一问没有得到肯定回答**：第一段就没有通过，
  后面两段根本没机会发出，因此**没有任何证据表明 secure world 接受 PAS**。
- R105 的重分类本身**不需要撤回**：它说的是"不是源码层的不可能"，
  本次也没有证明它是"不可能"。但从**资源决策**角度，本次结果没有把 K3 推向 `WIRED`。

---

## 五、这次实验证明了什么 / 没证明什么

### 5.1 已证明（DEVICE-VERIFIED，均有工件或代码锚点）

1. **固件字节真的在镜像里**：`blob=embedded`，`mdt_len=6684`。
   `mdt_ck=0980cf98`、`seg_ck=5a8c8234` 与主机用真实固件文件独立重算的 FNV-1a 32 **逐位一致**
   （本文件重算复核：`.mdt(6684B)→0980cf98`、`.b02(1968B)→5a8c8234`；空数组基准 `811c9dc5`）。
2. **`_armcpu_scm_call` 在真机上可调用**：`sym=1`。
3. **PAS 序列真的发出去了**：`ran=1 calls=1` —— 本项目第一次真正向 TrustZone 发起 PAS。
4. **第一段就被挡下**：`t0 != 0`。
5. **fail-closed 生效**：`ran=1`，后两段**没有**发出。
6. **同一次启动内 SMC 通道是健康的**：窗口内 `scm avail` **509 行、1 个 distinct tuple**
   （`sdi=1 dl=0 wr=1 sdi=0 tcsr=10 dl=-1 wr=0 tcsr=0`），`-99`（从未执行哨兵）计数 **0**。
   *（509 是 **快照数**，不是调用数 —— 该行由 1 Hz 文本重放打印。）*
   所以失败**不是**通道坏了。
7. **设备完全安全**：窗口内无 panic / Debugger message / BARK / BITE / CrashDump；
   自行回到 TWRP；BCB 重装填 `boot-recovery`；`boot_a` md5 未变。

### 5.2 未证明（不得含糊）

1. **`t0` 的语义** —— 未解析。
2. **secure world 是否"根本上拒绝" PAS** —— 只证明这一次调用在这组参数下失败；
   第一段失败也可能是**参数问题**，不是策略拒绝（见 §7）。
3. `pas_mem_setup` / `pas_auth_and_reset` 的行为 —— **从未执行**。
4. **GPU 能否工作** —— 差着整个 A6XX GEN1 backend。**"PAS 发出去了"不等于"PAS 通过了"，更不等于"GPU 能用"。**
5. **结果可复现** —— 见 §6，本文件**撤回**"两次独立启动同一返回码"的说法。
6. 准入表是否改变 —— **未变**，见 §8。
7. `CONFIG_XNU_NATIVE_ADRENO` —— **仍为 `n`**（`configs/enchilada_defconfig:40`）。

---

## 六、证据更正：撤回"两次独立启动复现"的说法

被本文件取代的 17:42 草稿写着"两次**不同启动**的 stage-7 行逐字节相同（261 字节）"，
依据是它自己记录的一行内核时间 `34.754237`。**该依据无法成立**：

1. **工作区里没有任何工件包含 `34.754237`。** 对 `oneplus6-ios/work/tmp`（848 个 ≤64 MiB 的
   `.bin/.txt/.md/.log`）逐文件字节搜索，`34.754237` 只命中 `work/tmp/r123-evidence.md`
   （它本身是在引用那份草稿）与草稿自身。**没有任何 32 MiB 设备窗口、日志或引导记录含该时间戳。**
2. **两次真实启动的时间戳都不是它**：
   - `r122_window.bin`（17:10:25 落盘）→ `[   33.197772]`，`stage7=armed-not-executed`、`ran=0`
   - `r123_window.bin`（18:04:36 落盘）→ `[   35.573027]`，`stage7=exec`、`ran=1 calls=1`
3. **该草稿声称的日期（17:30）早于它所必须依赖的 payload 落盘时间（18:03:03）。**
   17:12–18:03 之间工作区**没有任何 boot/flash/日志类产物**产生
   （该区间只落盘了 CI 工件与 `work/tmp` 下的脚本，见 §10；两份设备窗口 `r122_window.bin`
   17:10:25 与 `r123_window.bin` 18:04:36 都不在区间内）。

**结论**：`34.754237` 那一行**不可追溯**，不能作为复现证据。
**复现性从 `DEVICE-VERIFIED` 降级为"未验证"**。当前仍然成立的是：
**恰好一次**可追溯的启动里，`t0 = 0xffffffffffcffff6`。

> 说明：那一行的**行文本**与真实 R123 行 261 字符逐字节相同，所以它记录的内容本身是对的；
> 不能成立的是它声称自己来自**另一次独立启动**。

---

## 七、一处代码层面的前置条件缺口（实测，不是猜测）

这是本轮在核查代码时发现的、**下一次若要重试必须先补上**的东西，
**它不是本次失败的已证明原因**，只是唯一被定位到的具体缺口。

`pas_init_image` 的 `args[1]` 必须是**metadata（ELF 头 + hash 段）的物理地址**。
本次 probe 传的是：

```c
adreno_zap_pas_execute(&s7tr, s7base, kAdrenoZapRegionPhys, &s7region, &s7rep);   // EnchiladaAdrenoProbe.cpp:1008-1010
```

`kAdrenoZapRegionPhys = 0x8C415000`（`EnchiladaAdrenoProbe.cpp:166`）是那块 **8 KiB 保留区**，
而实测事实是：

1. **没有任何代码把固件字节写进那块区域。** 对 `EnchiladaAdrenoProbe.cpp` 搜索
   `memcpy|memmove|bcopy|wr32(|store`，命中全部是**注释**（:329/:330/:391/:685），
   没有一处真正的写操作。`adreno_zap_seg` / `adreno_zap_mdt` 只被 `zapFnv1a()` **读取**（:1026/:1028）。
2. **没有 loader**：`adreno_mdt_loader_available()` 返回 **0**（`AdrenoMdtV1.c`，注释原文
   "nothing reads a630_zap.bNN from anywhere"）。
3. 三段里 `init_image` 的 `x3 = metadata_phys`、`mem_setup` 的 `x3 = region->phys`，
   **二者在本次调用中是同一个地址** `0x8C415000`（`AdrenoZapAuthV1.c:1296,1300`）。
   上游语义里这是两个不同的东西：一个是**已填好 MDT 的 metadata 缓冲**，一个是**目标保留区**。

上游顺序是先把 MDT 元数据载入一处 secure world 可读的缓冲，再 `init_image(metadata_phys)`。
**本次跳过了"载入"这一步**，直接把一个内容为空的保留区地址当成 metadata 地址交了上去。

**因此**：如果将来要重试 PAS，先决条件是补一个**真正的 metadata 缓冲 + 载入路径**，
而不是改 `t0` 的期望值。这个缺口**大于**"再发一次 SMC"。

---

## 八、路线判定（本轮要回答的那一问）

### 8.1 准入表没有动

- `modeled = 9 / satisfied = 0 / BLOCKED` —— **未变**
  （`drivers/NativeAdreno/AdrenoGen1BackendV1.c:542-543` 逐字：
  `/* Admission: 9 modelled (round 77 added BOARD_VA_MAP's derivation),` /
  ` * 0 satisfied, BLOCKED. */`）
- `CONFIG_XNU_NATIVE_ADRENO = n` —— **未变**（`configs/enchilada_defconfig:40`）
- GPU **仍然从未渲染过任何一帧**

**为什么不变是应该的**：PAS 只是 Zap/GMU 签名链（K3）里的一个 gate，
而 K3 又只是 12 个前置之一。**通过 PAS 都不会让 `satisfied` 加一**，
何况本次没有通过。任何"PAS 发了 → 路线前进"的写法都是夸大。

### 8.2 剩下三个硬阻塞一个都没动

`evidence/stage5-gpu-metal-publish.md:126-132` 列的四个硬阻塞，本次只碰到第 (2) 个（PAS），
其余三个**原样未动**：

1. **A6XX_GEN1 kernel backend 实例化**（legacy GMU HFI v1 / SPTPRAC / CP-PM4）—— 没有实现模块
2. ~~signed ZAP / TrustZone PAS（`GPU_PAS_ID 13`）~~ —— 本次尝试，**未通过**
3. **kgsl SMMU stage-1 context**（`arm,smmu-kgsl@5040000`，先要能 fault）
4. **macOS 前端 + 选定 OS 的 Metal/IOSurface ABI**（+ IOKit GPU 加速器 user client 无实现模块）

### 8.3 判定：**B 路线止损**

**一句话结论：B 路线（Adreno 真参与渲染）应当止损，转向软件渲染（text_fixup / berga-tools）。**

理由（三条，按权重）：

1. **gate 没有动，而且这次动不了。** 一轮设备实验换来的信息是"送得进去、第一段被挡"，
   `satisfied` 仍是 0。要再往前，先决条件是**补一个 metadata 缓冲 + 载入路径**（§7），
   那是一个**比 PAS 本身更大**的工程，而它做完之后仍要在 secure world 那里再过一次未知的门。
2. **代价与剩余距离严重不成比例。** 即便 PAS 明天通过，还有 A6XX GEN1 backend、
   macOS 前端、IOKit user client 三个硬阻塞，每一个都**不小于**整个 PAS 投入。
   已投入的轮次里 `satisfied` 始终为 0，这是信号，不是运气。
3. **`t0` 未解析意味着下一次尝试无法被设计。** 一个不能归因的返回码，
   只能靠穷举参数去试；在安全世界里穷举是不可接受的（每次都是一次 secure-world 交互）。

**止损不等于否认**：本次没有证明 PAS 不可能，`t0` 也许就是 §7 的参数缺口。
但"也许能修"不是继续投入的理由——**在没有 loader 的前提下，它不是一条能靠一轮实验推进的路线**。

**转向后该做什么**（(A) 路线，明确不是 GPU）：
- `text_fixup` / `berga-tools` 方向的软件渲染与 userland 收尾
- 这条线的目标必须写成 **(A)「UI 不崩 / 可用」**，
  **不得**被表述成 (B)「Adreno 渲染」——两者差着整个 backend。

---

## 九、诚实边界（不得删改）

- 本文件不含任何对 secure world 内部行为的推断。
- `t0 = 0xffffffffffcffff6` 的含义**未知**；本文件不声称知道。
- 一次调用失败**不等于**路线在原理上不可能；但也**不等于**路线可行。
- **不得**把"PAS 发出去了"写成"PAS 通过了"。
- **不得**把"PAS 通过了"（即使将来通过）写成"GPU 能工作了"。
- `34.754237` 那一行**已撤回**，不得再作为复现证据引用。
- 本文件所有设备侧数值均可在 §2 给出的窗口工件上逐字节复核。

---

## 十、本轮产物与硬约束

**只读 + 只落盘**：本轮未刷机、未 reboot、未发 SMC、未改设备状态、未派 CI、未改任何代码。

| 项 | 值 |
|---|---|
| 设备窗口（复核用） | `work/tmp/r123_window.bin`，33,554,432 B，sha256 `84730f74…f2bf` |
| kernel.macho | 126,128,128 B，sha256 `e66f39169fc9d229df8862ebe573720b74ad9b3b2c4ca2451f3ac66be379d848` |
| payload.img | 150,994,944 B，sha256 `93d90d3f56798e542d51ffdbb57218941ce18a5c994b7969b93a0042354dff96` |
| archive | `libEnchiladaAdreno.a (31 objects, sha256 d4e805b0cb6c856f)` |
| 固件头 | `AdrenoZapBlobV1.h` 56906 B，`PASS: ... EMBEDDED (11/11 self-checks)` |
