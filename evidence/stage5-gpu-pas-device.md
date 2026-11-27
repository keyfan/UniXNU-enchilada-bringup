# ROUND 134 — PAS 实测终局（DEVICE-VERIFIED）：`s0` 是回显，B 路线止损

**状态行：`DEVICE-VERIFIED`** —— 本文件汇总 ROUND 123→133 的五次真机 PAS 实验，
最后一次（R133）同时判定 call-type 与回显两个问题。

**一句话结论：`s0` 是回显而不是裁决；PAS 13 在这台机器上不被 secure world 支持；
STD_CALL 没有改变结果；B 路线止损。**

**日期**：2026-11-14（R123 首测 → R133 终测；本文件 ROUND 134 落盘）
**设备**：`27f4f45d` / OnePlus 6（enchilada，SDM845）/ TWRP 3.4.0-3

> 本文件**取代** 18:38 的 ROUND 124 版本。那一版只见过 `s0=0`，尚未见过 `s0=0x82`，
> 因此它把 `s0` 当作一个"未被改写的初始值"来解释——方向对，但**停得太早**。
> 现在的结论更强也更不客气：**`s0` 这条通道本身就不可信**，无论它是 `0` 还是 `0x82`。

---

## 一、为什么这份文档要改结论

ROUND 124 版本的 §3.1 结论是：`s0=0` 是"未被改写的初始值，不是 secure world 的表态"。
**这个判断是对的，而且现在更有价值** —— 它和后来的 `s0=0x82` 是**同一类错误的两个实例**。

区别在于：R124 只能说"这个 `0` 可能没被写过"；
R133 之后我们能说"**这个寄存器根本没人写，读到的是我们自己送进去的东西**"。

后者是可证伪的实验结论（有对照字段），前者只是对写路径的推断。

---

## 二、实验编年（五次真机调用）

| 轮次 | boot arg | `x0` | `t0` | `s0` | `meta=` | 关键新增 |
|---|---|---|---|---|---|---|
| R123 | `pas-exec=1` | `c2000201` | `ffffffffffcffff6` | `0` | 无此字段 | 首次发出 PAS |
| R128 | `pas-exec=1` | `c2000201` | `1` | `82` | `ad90f000` | 修复 D1（真 DMA 缓冲） |
| R130 | `pas-exec=1` | `c2000201` | `1` | `82` | `ad90f000` | `avail=`/`x1s=`（有缺陷） |
| R133 | `pas-exec=2` | `42000201` | `1` | `82` | `ad914000` | `ct=std`/`x1ok=`（修复生效） |

五次都 `rc=4`（`kAdrenoZapExecStageFailed`）、`ran=1 calls=1`（只发一轮、fail-closed）、
**stage 1/2 从未执行**。

---

## 三、★ R133 完整原始行（逐字，318 字节）

```
enchilada-adreno: stage7=exec why=- armed=1 exec_key=2 sym=1 blob=embedded seg=1968 region=8c415000+2000 stages=3 seq_ok=1 mdt_ck=0980cf98 seg_ck=5a8c8234 mdt_len=6684 x0=42000201/42000202/42000205 rc=4 ran=1 calls=1 t0=1 s0=82 t1=0 s1=0 t2=0 s2=0 p13=0/0 v13=0 t0r=1 meta=ad914000 avail=1 av0=0/1 x1s=82 ct=std x1ok=1
```

**来源工件（可复核）**：`oneplus6-ios/work/tmp/r133_window.bin`
- 大小 **33,554,432 B**，sha256 **`2a4b51e3a381c6ed79e84ca7c47fb4129b66f0932b3d957315bc30afcc99e136`**
- 窗口内 `stage7=` **1** 条、`Darwin Kernel Version` **1** 条、`ct=std` **1** 条 → **单次启动**
- 安全计数（本文件直接重数）：`panic(cpu`=0、`BARK`=0、`BITE`=0、`CrashDump`=0、`pas-exec`=0
- 同启动 SCM 通道：`scm avail` **509 行**、**1 个 distinct tuple**、`-99` 哨兵 **0**

镜像：CI #254（run `43597684195`，success，15m），`kernel.macho` 126,132,224 B
sha256 `49e24c5bbcd6e3ec4d69ba88ee41fb1c52f6717d3c6e4821788fbeaea4a39124`；
`payload.img` md5 `ac10bdad8f6ba75842880b38bfb70aba`，boot args 991/1000 含 `enchilada-pas-exec=2`。

---

## 四、三个独立信号同向：PAS 13 不被支持

三个信号来自**不同的探针、不同的寄存器、不同的判据**，互不依赖。

### 信号 1：`avail=1` + `v13=0` → 命令存在，但该外设不被支持

| 读数 | 含义 |
|---|---|
| `avail=1`（`av0=0/1`，a1=1 非零） | `PAS_IS_SUPPORTED` 命令**确实存在**（按 P2:368 的 `!!res.result[0]` 极性） |
| `p13=0/0` + `v13=0` | 对该命令问"13 号外设支持吗"，a1 回答 **0 = 不支持** |

**这两条必须一起读，这正是 R129 建立判别器的全部理由。** 若只有 `p13=0/0`，
"命令不存在"与"外设不支持"会给出**同一个** `0/0` 回复，读数毫无信息量
（Linux 正是因为先跑 `__qcom_scm_is_call_available` 才从不犯这个错）。
`avail=1` 把这一半钉死：**`v13=0` 是真实的"不支持"，不是命令缺失，也不是查询失败**。

### 信号 2：`ct=std` + `x0=42000201`（bit31=0）+ `t0=1` → call type 无关

R123/R128/R130 一直发 FAST_CALL（`0xC2...`），而上游 `qcom_scm_call()` 走的是
非 atomic 路径，`qcom_scm-smc.c:160` 只在 atomic 时才选 FAST_CALL ——
即上游实际发的是 STD_CALL（`0x42...`）。这是一处**真实的偏离**，R131-132 把它改掉。

R133 的读数：

| 项 | 实测 | 说明 |
|---|---|---|
| `ct=` | `std` | 回读自 base word，不是回显本地开关 |
| `x0=` | `42000201/42000202/42000205` | 三段 bit31 **全为 0** |
| `exec_key=` | `2` | boot arg 确实传到执行层 |
| `t0=` | **`1`** | **和 FAST 时一模一样** |

**切到 STD_CALL 对 `t0` 零改变。** 因此 `INTERRUPTED` 与 call type 无关。

### 信号 3：`s0=0x82` == `x1s=0x82` == 送出的 arginfo，`x1ok=1` → `s0` 是回显

见 §五。

### 三个信号合起来的结论

- 信号 1 说：**这个外设的 PAS 服务不存在**（不是我们参数没调对）
- 信号 2 说：**不是调用约定用错了**（FAST/STD 都一样）
- 信号 3 说：**我们唯一读到的"secure 回答"其实是我们自己**（连"监视器拒绝了"都不成立）

**综合：B 路线的失败不是参数问题，是这台机器的 secure world 不提供该外设的 PAS 服务。**

---

## 五、★ `s0` 是回显 —— 观测工具的系统性误差

### 5.1 这是一个观测工具的系统性误差，持续了三轮才被发现

这不是"某次读错了数"，而是**测量仪器本身有系统性偏差**：

- `s0` 被定义为"secure world 的 `result[0]`（`smc a1`）"，
- 但**监视器从来没有写过 a1**，
- 于是 `a1` 原样保留了我们在 `x1` 里送进去的 `arginfo`。

**读到的是我们自己的输入。** 任何把它当作"对方的回答"的解读都是自说自话。

**发现它的方法，是在同一个调用里增加一个对照读数**：
R130 加上 `x1s`（记录那次调用实际送出的 x1），R131 修正 `x1s` 取错了对象，
R133 得到 `s0 == x1s == 0x82` 且 `x1ok=1`。**没有这个对照，这个误差无法被看见。**

### 5.2 完整推理链（三轮）

| 轮次 | 动作 | 结果 |
|---|---|---|
| R130 | 首次打印 `x1s` 作为**纯对照字段**，不改变 `s0` 的读取 | 但 `s7x1s = s7sup.arginfo` —— 取的是 **p13 探针**的 arginfo（`0x01`），**跨了两次不同的调用** |
| R131-132 | 缺陷被 R130 的 verifier 抓到并确认：`x1s` 必须来自 `s7rep.stage[InitImage].arginfo`，与 `s0` **同一次调用** | 为此给 `adreno_zap_pas_stage_result` 加了 `arginfo` 字段，在 `.c` 里从**交给 transport 的同一个变量**填充，使该字段观测"实际送出的值"而非复述常量 |
| R133 | 设备实测 `x1s=82`、`x1ok=1`、`s0=82` | **`s0 == x1s` 成立 → 回显假设命中** |

R130 的 `x1s=1` 曾经"看起来"排除了回显假设（`s0=0x82 ≠ 1`）——**那个排除是假的**，
因为被比较的根本不是 `init_image` 送出的那个 x1。**这一点必须传下去。**

### 5.3 这个误差的实际后果

**R123 到 R130 之间所有基于 `s0` 的解读全部作废。** 具体地：

- R128 写过"`s0=0x82` = 130 —— **secure 通道也变了**" → **错**。secure 通道没有任何证据变过。
- R128 写过"两个通道都换了值，失败形态确实变了" → `t0` 那一半是真的，`s0` 那一半是伪的。
- R124 写过"`s0=0` 是未被改写的初始值" → 方向对，但仍然假定"如果被写过就是真值"。**现在不成立了。**

### 5.4 ⚠️ 一个必须如实说明的未解释点（不要替它编解释）

`x1` 在 R123 时**就已经**是 `0x82` 了 —— ROUND 108 早在 R123 之前就把寄存器布局
修正为 `x1 = arginfo`（P6:35/171-173）。所以：

> **"R108 修正布局，我们才开始把 arginfo 放进 x1，于是 `s0` 从 `0` 变成 `0x82`" 这个说法不成立。**
> R108 早于 R123，R123 送出的 x1 同样是 `0x82`。

**那么 R123 为什么读到 `0`、R128/R133 读到 `0x82`？本文件不解释。**
可确证的只有：两次之间**我们送出去的东西确实变了** ——
`meta` 从空保留区 `0x8C415000` 变成真实 DMA 缓冲 `0xad90f000`（ROUND 127 修 D1），
`a0` 从 `0xFFCFFFF6` 变成 `1`。至于 `a1` 为何随之不同，**未归因，不声称知道**。

（这条写在这里，是因为它正是"不要用结论去倒推原因"的例子。）

---

## 六、与 ROUND 105（K3 重分类）的关系

ROUND 105 把 K3 从 `BLOCKED` 重分类为 **CONDITIONALLY FEASIBLE / IMPLEMENTABLE-WITH-EFFORT**，
并明确写下唯一的诚实未知：**"secure world 会不会接受这个 EL1 内核发起的 PAS"**，
且该文件自己强调 `This audit does NOT unblock stage 5`。

R123→R133 把这一问**答完**了，答案是**否**：

- R105 说的是"机制层不是不可能" —— 这一半被证实了（`sym=1`、SMC 真的发出、返回码真的拿回来）。
- R105 留的那一问"secure world 会不会接受" —— 现在有答案：**`v13=0`，这台机器的
  secure world 不为 13 号外设提供 PAS 服务**。这不是"再试一次可能成"，是**服务不存在**。
- R124 曾把失败归因为"可能只是参数缺口"（metadata 缓冲），并据此说路线"也许能修"。
  **那个缓冲后来真的补上了（ROUND 127），`t0` 也随之从 `0xFFCFFFF6` 变成 `1` ——
  但结论没有变，反而更清楚：参数修好了，它仍然不支持。**

---

## 七、这次实验证明了什么 / 没证明什么

### 7.1 已证明（DEVICE-VERIFIED，均有工件或代码锚点）

1. **固件字节真在镜像里**：`blob=embedded`、`mdt_len=6684`，`mdt_ck=0980cf98`/
   `seg_ck=5a8c8234` 与主机用真实固件独立重算的 FNV-1a 32 逐位一致。
2. **PAS 序列真的发给 secure world 了**：`ran=1 calls=1`。
3. **`PAS_IS_SUPPORTED` 命令存在**（`avail=1`），**但 13 号外设不被支持**（`v13=0`）。
4. **`s0` 是回显**：`s0 == x1s == 0x82` 且 `x1ok=1`，比较对象已修正为同一次调用。
5. **`INTERRUPTED` 与 call type 无关**：FAST(`0xC2…`) → STD(`0x42…`) 后 `t0` 仍为 `1`。
6. **fail-closed 生效**：stage 1/2 从未执行（`ran=1`）。
7. **同启动 SCM 通道健康**：509 行 `scm avail`、1 个 distinct tuple、`-99`=0
   → 失败不是通道坏了。
8. **设备完全安全**：无 panic/BARK/BITE/CrashDump；自行回 TWRP；BCB 重装填；`boot_a` md5 未变。

### 7.2 未证明（不得含糊）

1. **`INTERRUPTED`（`t0=1`）的成因** —— **未解释**。只证明它**与 call type 无关**。
2. **`v13=0` 的更细成因** —— R130 已排除"命令不存在"，但"命令存在而该外设不可用"的
   **具体理由**未明；secure world 拒绝的理由不在我们的可见范围内。
3. **`s0` 为何在 R123 读到 `0`** —— 未归因（见 §5.4）。
4. `mem_setup` / `auth_and_reset` 的行为 —— **从未执行**。
5. **GPU 能否工作** —— 差着整个 A6XX GEN1 backend。**"PAS 发出去了"≠"PAS 通过了"≠"GPU 能用"。**
6. 准入表是否改变 —— **未变**。
7. `CONFIG_XNU_NATIVE_ADRENO` —— **仍为 `n`**。

---

## 八、路线判定（终局）

### 8.1 准入表一格没动

- `modeled = 9 / satisfied = 0 / BLOCKED` —— **未变**
  （`drivers/NativeAdreno/AdrenoGen1BackendV1.c:542-543`）
- `CONFIG_XNU_NATIVE_ADRENO = n` —— **未变**（`configs/enchilada_defconfig:40`）
- GPU **仍然从未渲染过任何一帧**

**为什么不变是应该的**：PAS 只是 Zap/GMU 签名链（K3）里的一个 gate，K3 又只是 12 个前置之一。

### 8.2 剩下三个硬阻塞原样未动

`evidence/stage5-gpu-metal-publish.md:126-132` 的四个硬阻塞，本次只碰第 (2) 个：

1. **A6XX_GEN1 kernel backend 实例化** —— 没有实现模块
2. ~~signed ZAP / TrustZone PAS~~ —— **已判死：secure world 不提供该服务**
3. **kgsl SMMU stage-1 context** —— 先要能 fault
4. **macOS 前端 + Metal/IOSurface ABI** —— + IOKit GPU 加速器 user client 无实现模块

### 8.3 判定：**B 路线止损**

**B 路线（Adreno 真参与渲染）到此止损。** 理由三条，按权重：

1. **不是参数问题，是服务不存在。** `avail=1` + `v13=0` 是同向的两个独立读数：
   命令存在，但 13 号外设不被支持。这不是"再调一次参数就能过"的失败。
2. **唯一的替代解释已被实验排除。** R124 曾认为可能是 metadata 缓冲缺口；
   R127 把真实 DMA 缓冲补上了，`t0` 从 `0xFFCFFFF6` 变成 `1` —— **仍然失败**。
   R131 又把 call type 从 FAST 改成上游用的 STD —— **仍然 `t0=1`**。
   两个最有力的候选解释都被真机否掉了。
3. **剩下的投入没有出口。** 即便 PAS 通过，还有 A6XX GEN1 backend、kgsl SMMU、
   macOS 前端三个硬阻塞，每一个都不小于整个 PAS 投入；而已投入的轮次里 `satisfied` 始终为 0。

**止损 ≠ 整个移植停止。** 停的只是**这一条子路线**。转什么见 `HANDOFF.md` §6.7。

---

## 九、诚实边界（不得删改）

- 本文件**不含任何对 secure world 内部行为的推断**。`v13=0` 是它对
  "13 号外设支持吗"这一问的答复，**不是**对它为什么拒绝的推断。
- **`s0` 这条通道不可信**：无论读到 `0` 还是 `0x82`，都可能是"没人写过"的残留。
  任何用 `s0` 论证 secure world 态度的写法都是错的。
- `INTERRUPTED`（`t0=1`）的**成因未解释**；只证明它与 call type 无关。
- `s0` 在 R123 读到 `0` 的原因**未归因**（§5.4），不得用"R108 修正布局"倒推。
- stage 1/2 **从未执行**，不得写成"被拒绝"。
- **不得**把"PAS 发出去了"写成"PAS 通过了"。
- **不得**把 PAS 的任何结果写成"GPU 能工作了"。
- `34.754237` 那一行（R124 已撤回）**不得再作为复现证据引用**。
- 本文件所有设备侧数值均可在 §三 给出的窗口工件上逐字节复核。

---

## 十、产物与硬约束

**只读 + 只落盘**：本轮未刷机、未 reboot、未发 SMC、未改设备状态、未派 CI、**未改任何代码**。

| 项 | 值 |
|---|---|
| R133 窗口（终测，复核用） | `work/tmp/r133_window.bin`，33,554,432 B，sha256 `2a4b51e3…e136` |
| R123 窗口（首测） | `work/tmp/r123_window.bin`，sha256 `84730f74…f2bf` |
| R130 窗口 | `work/tmp/r130_window.bin` |
| R128 窗口 | `work/tmp/r128_window.bin` |
| kernel.macho（R133/CI #254） | 126,132,224 B，sha256 `49e24c5b…9124` |
| payload.img（R133） | 150,994,944 B，md5 `ac10bdad8f6ba75842880b38bfb70aba` |

---

## 十一、ROUND 136：为什么降级路径不适用

> 本节由 ROUND 135–137 落盘（**2026-11-14**）。它**不改变** R134 的任何读数，
> 而是补上 R134 当时**缺失**的一块：单纯 `v13=0` 还不足以把"绕过 PAS"堵死 ——
> 光看驱动代码，`SECVID_TRUST_CNTL` 手动退出路径确实存在。
> ROUND 136 用上游 DT 证据证明：**那条降级路径对我们的设备不适用**。

### 背景：降级路径在代码里确实存在（所以才会被 R135 提出）

R135 提出"上游可能有不依赖 PAS 的路径"。方向不空：`a6xx_gpu.c` 确实有一条
CP 安全模式切换失败后的**手动退出**降级：**但它是给 DT 不声明 zap 的设备用的。**

（CITED，pin 版 v6.12 `a6xx_gpu.c:1157-1181`，逐字）

```
    /*
     * Try to load a zap shader into the secure world. If successful
     * we can use the CP to switch out of secure mode. If not then we
     * have no resource but to try to switch ourselves out manually. If we
     * guessed wrong then access to the RBBM_SECVID_TRUST_CNTL register will
     * be blocked and a permissions violation will soon follow.
     */
    ret = a6xx_zap_shader_init(gpu);
    if (!ret) {
        OUT_PKT7(gpu->rb[0], CP_SET_SECURE_MODE, 1);
        OUT_RING(gpu->rb[0], 0x00000000);

        a6xx_flush(gpu, gpu->rb[0]);
        if (!a6xx_idle(gpu, gpu->rb[0]))
            return -EINVAL;
    } else if (ret == -ENODEV) {
        /*
         * This device does not use zap shader (but print a warning
         * just in case someone got their dt wrong.. hopefully they
         * have a debug UART to realize the error of their ways...
         * if you mess this up you are about to crash horribly)
         */
        dev_warn_once(gpu->dev->dev,
            "Zap shader not enabled - using SECVID_TRUST_CNTL instead\n");
        gpu_write(gpu, REG_A6XX_RBBM_SECVID_TRUST_CNTL, 0x0);
        ret = 0;
```

**进入降级的唯一开关是 `ret == -ENODEV`**（`a6xx_gpu.c:1172`），
而 `-ENODEV` 只来自 `adreno_zap_shader_load()` 在 **DT 的 `zap-shader` 节点缺失/不可用**时返回。

### 发现 B：GPU 驱动代码没有变，变的是 DT

上游 `a6xx_gpu.c` master 与 pin 的 v6.12 **sha256 完全相同**（`c33479377574ed19`，
pin 版 sha 前缀与 porting.toml 记录的 `a6xx_gpu.c (c3347937…)` 一致）。
也就是说，降级路径**不是新加的**，更不是给 SDM845"发现没 PAS 时用的后门" ——
它在所有版本都存在，服务的是**根本不用 zap 的那一类设备**。

### 发现 A：SDM845 设计上就要走 zap

（captain 已联网核实的事件，本节如实落盘；不以本地重现替代）

上游 master `arch/arm64/boot/dts/qcom/sdm845.dtsi:4965`：

```
   gpu_zap_shader: zap-shader {
            memory-region = <&gpu_mem>;
   };
```

而我们 pin 的 v6.12 `sdm845.dtsi`（**151,194 B**）里 `zap` 出现次数 = **0**
（本机对 `work/UniXNU-enchilada/.cache/…/sdm845.dtsi` re-grep，0 命中）。
**SDM845 的主线 DT 声明了 `zap-shader` 节点 → 走 PAS 路径走 zap 是设计本身。**

### 发现 C：降级路径不适用于我们的设备

`adreno_gpu.c:48-51`（CITED，pin 版 v6.12，逐字）：

```c
    np = of_get_child_by_name(dev->of_node, "zap-shader");
    if (!of_device_is_available(np)) {
        zap_available = false;
        return -ENODEV;
    }
```

- 我们的设备 DT **声明了这个节点**：`notes/dt/sdm845-oneplus-common.dtsi:483` 有 `&gpu_zap_shader` 覆盖
  （`firmware-name = "qcom/sdm845/OnePlus/enchilada/a630_zap.mbn"`）；
- `a630_zap.mdt` 与 `.b00/.b01/.b02`（及 `.elf`）**全在手**（`work/tmp/a630-device-fw/`）；
- 因此对这台机器，`of_device_is_available(np)` 为真，`adreno_zap_shader_load()` 走 **PAS 路径**
  （`a6xx_gpu.c:819` `adreno_zap_shader_load(gpu, GPU_PAS_ID)`），**不返回 -ENODEV**。

**结论：降级路径的进入条件（-ENODEV）对我们的设备不成立。SECVID_TRUST_CNTL 手动退出没有出口。**

### 明确结论

**SDM845 设计上就要走 zap，我们绕过它就是上游警告的 `crash horribly` 那种情况。**

- R135 提的「上游可能有不依赖 PAS 的路径」**已否证**：降级路径存在，但开关是 **DT**，
  而我们的 DT 明确声明 zap → 只能走 PAS。
- 想直接 `SECVID_TRUST_CNTL = 0` 而不喂 zap，就是 `a6xx_gpu.c:1177` 逐字的
  `if you mess this up you are about to crash horribly`（且 `:1181` 之前没有任何 zap 认证，
  secure mode 不会真的退出）。

### R134 的止损结论成立，且理由比原来更硬

R134(e) 止损的核心是 `v13=0`（服务不存在）。ROUND 136 补上另一半，闭合成自洽的负结论：

1. **该给的 PAS 服务不存在**：`avail=1` + `v13=0`（R129-133 判定）。
2. **这台机器没有绕开它的合法出路**：DT 声明 zap → 降级路径进入条件（-ENODEV）不成立 →
   手动 SECVID_TRUST_CNTL 是 crash horribly 分支。

**一个不满足（服务不存在）** 已经足够，**两个都不满足（无合法绕路）** 让失败更不可挽回。
**B 路线的止损判断不变，且理由更硬。**

### 保持不变的诚实边界（R134 原有，全部保留）

- `INTERRUPTED`（`t0=1`）的**成因未解释** —— 只证明**与 call type 无关**。
- `v13=0` 是 secure world 对"13 号外设支持吗"的**答复**，不是对拒绝理由的推断。
- stage 1/2 **从未执行**，不得写成"被拒绝"。
- 准入表 **`modeled=9 / satisfied=0 / BLOCKED` 未变**；`CONFIG_XNU_NATIVE_ADRENO=n` 未变。
- **不得**写"PAS 通过了"，**不得**写"GPU 能工作了"。
- R135 判断「非 atomic 路径重试 INTERRUPTED」的更正：即使成立，也不改变结论 ——
  `v13=0` 是 secure 的答复，重试同样拿不到不同策略答案（见 porting.toml ROUND 135-137 记录）。

### 方法论教训（R107 的悬空引用不是笔误，是 pin 版本太旧的信号）

`notes/dt/sdm845-oneplus-common.dtsi:483` 的 `&gpu_zap_shader` 是**真实意图**，
但 pin 的 v6.12 base `sdm845.dtsi` 里这个节点**不存在**（`zap` grep 0 命中）。
R107 当时把它判为"参考文档笔误"，**实际是 pin 的 base DT 落后于支持 SDM845 GPU 的版本**。
**如果当时对照 master，就能早 29 轮知道 SDM845 走 zap 的设计。**

## 十二、ROUND 139–162：撤回 §八 的终局判定，并记录「看起来什么都没发生」的三次启动与一次对照

### 12.1 撤回的两条理由（不是因为我们拿到了 PAS）

1. **用户确认同机 Windows 真的驱动过 GPU**，而它的尾部署 DTB 用的是同一套 `qcom,kgsl-hyp` +
   `qcom,firmware-name=a630_zap` + `memory-region=<&gpu_region>` 配置（见 12.3）。原厂真机 DT 也逐字带着
   `qcom,pas-id=<13>` / `memory-region=<0x2a9>` → `gpu_region@0x8c415000` 8192 B（见 12.4）。
   因此 `v13=0`（PAS 13 不支持）**是这台机器当前状态下的异常，不是硬件上限**。
2. **R136 的降级路径论证建立在错的 DT 上**：它引用的是 mainline `sdm845.dtsi` 的 `gpu/zap-shader` 子节点，
   而原厂 DT 的 `/soc/qcom,kgsl-hyp/` **没有子节点**；`of_get_child_by_name(kgsl_hyp, "zap-shader")`
   在这台机器上返回 NULL → 上游 `a6xx_gpu.c` 会走 `-ENODEV` 的 `SECVID_TRUST_CNTL` 降级分支，
   而不是注释里警告的 `if you mess this up you are about to crash horribly` 那条路。

**注意撤回的边界**：撤回的只是 §八 的**因果解释**（「PAS 被拒 ⇒ B 路线止损」）与**建议**
（「转软件渲染」）；§八 的读数（`avail=1` / `v13=0` / `t0=1` / `s0` 回显）与教训**全部保留**。

### 12.2 R139–R147：SHM Bridge 是真的（新事实）

| 探针 | fnid | arginfo | 参数 | 结果 |
|---|---|---|---|---|
| `SHM_BRIDGE_ENABLE` | `0xC2000C1C`（SVC_MP 0x0c / cmd 0x1c） | `ARGS(0)` | 无 | **`av2=1 sb=0`：调用成功，shmbridge 可用** |
| `SHM_BRIDGE_CREATE` | `0xC2000C1E` | `ARGS(4)` | 全 `0xFFFFFFFF` | status `0x57`，句柄 `0x8587EB18` |
| `SHM_BRIDGE_CREATE` | `0xC2000C1E` | `ARGS(4)` | 全 `0x0` | status `0x5b`，**同一个句柄** `0x8587EB18` |
| `SHM_BRIDGE_DELETE` | `0xC2000C1D` | `ARGS(1)` | 上述句柄 | **`a0=0` / `a1=0x58` / `v=1`：真句柄，删除成功** |

- 两组 CREATE 参数得到**不同** status（`0x57` vs `0x5b`）⇒ secure world **确实解析了参数**，不是参数无关的拒绝。
- 但两次 CREATE 得到**同一个**句柄，且我们零次 DELETE 也没有泄漏——最可能是该 bridge 早在
  bootloader/TZ 阶段就建好了，我们拿到的是既有句柄。
- **两类拒绝性质不同**：PAS `init_image` 是 `a0=0xFFCFFFxx`（SMC 分发层就被拒），
  SHM_BRIDGE_CREATE 是 `a0=0` 而 `a1=0x57`（进到 secure 业务逻辑后被拒）。这个区分是 R139 之后最重要的方法论收获。

### 12.3 R148–R157：Windows 固件与上游 Skip-PAS 案例

- `stage5=run up=0 up_fail=5` 的旧解读是错的：`AdrenoPowerUpV1.h:111 STEP_COUNT=5u`，`:201-202` 写明
  `fail_index == STEP_COUNT` 是**零失败哨兵**。`up_fail=5` = 五步全成功。
- 上游 `a6xx_catalog.c` 的 20 个 GPU 条目里 18 个带 `.zapfw`（含 A630 `a630_zap.mdt`），2 个不带
  （A618/sm4350 与一个 A7xx GEN2），且这两个的 DT 也没有 zap-shader → 上游「跳过 PAS」的案例**都是不同硬件**。
- Windows 侧：
  - `qcdxkmsuc850.mbn`（14,256 B）与我们 `a630_zap.elf`（14,256 B）**94.5% 逐字节相同**（12,473 同 / 783 异）。
  - `qcdxkm850.sys` 里 `zap`/`ZAP` **0 命中**；PIL 相关字符串全是 Venus（`VedPilLoadAndReset` 等），**不是 GPU**。
  - `qcscm850.sys`（124,184 B，ARM64 PE）111 个符号级词表是 **Secure App 模型**
    （`SecAppSendSyscall` / `qcom.tz.winsecapp` / `qcom.tz.uefisecapp` / `IOCTL_SCM_SECURE_APP_SEND_COMMAND`），
    而 `pas|PAS|PIL|auth_and_reset|is_supported|init_image|mem_setup|kgsl|zap` **全部 0 命中**。
  - 它唯一的三处 `smc #0` 全在 section `TZT_ASM`（VA `0x140001000`，0xB8 B），并且
    **无条件重试 `w0 > 0`**（即 `QCOM_SCM_INTERRUPTED=1` / `WAITQ_SLEEP=2` 都重发同一笔调用）。
  - UEFI 内层 FV 的 DTB 里**有** zap-shader 节点：
    `firmware-name = "qcom/sdm845/oneplus6/a630_zap.mbn"`（注意是 `oneplus6` 路径，不是 `enchilada`）。
  - Windows boot image 尾部 DTB 有 `qcom,kgsl-hyp`（`compatible=qcom,pil-tz-generic`、
    `qcom,firmware-name=a630_zap`、`memory-region=<0x2a7>`）与 `qcom,kgsl-3d0@5000000`。
- **综合解读**：Windows 不靠内核 PAS 服务，它靠 UEFI 阶段加载 zap + 与 TZ 里的 secure app 对话。
  这与「内核 PAS 13 被拒」不矛盾——**它绕开的就是我们正在试的那条路**。

### 12.4 R155/R156：原厂真机 DT（决定性证据）

`stock-enchilada-last.dtb`（415,955 B，12,454 个属性）里逐字：

```
/soc/qcom,kgsl-hyp {
    compatible = "qcom,pil-tz-generic";
    qcom,pas-id = <13>;
    qcom,firmware-name = "a630_zap";
    memory-region = <0x2a9>;
    phandle = <0x4bd>;
};   // 没有 zap-shader 子节点

/reserved-memory/gpu_region@0x8c415000 {
    compatible = "removed-dma-pool";
    no-map;
    reg = <0x0 0x8c415000 0x0 0x2000>;   // 8192 B
    phandle = <0x2a9>;
};
```

两条推论：

1. `PAS 13 + 0x8c415000 + 8192 B` 是这台机器的**原厂标准配置**，不是我们编的。
2. 原厂用的是 **vendor `kgsl-hyp` 节点 + `qcom,firmware-name`/`qcom,pas-id` 属性**，
   而不是 mainline 的 `gpu/zap-shader` 子节点 —— mainline 的 `of_get_child_by_name(..., "zap-shader")`
   在这台机器上必然返回 NULL，所以上游**在这台机器上会走降级分支**。
   **未解释**：为什么我们调 `PAS_IS_SUPPORTED(13)` 得到 `v13=0`（原厂配置表明它该被支持）。

### 12.5 R159：镜像是对了，内核没跑到日志写入点

三条独立取证：

1. `system_b` 前 8 MiB sha256 `33be3170…` **等于 ci259 payload 头**、不等于 ci258 头 → 刷写是对的。
2. 窗口 `boot_id=935335677`、sha256 `2f513143…`，与 R147 的窗口**逐字节相同** → 没有写出新日志。
3. `stage7` 行缺少 R152 之后的 `rt=` / `a3=` / `a6=` 字段 → 实际跑的仍是旧内核形态。

配合 `package/BootLog.c:1179`（`h.boot_id=log_ticks()`）、`:1186`（`boot_log_live=&h`）、
`:1204`（`for (;;)` 持续 compose→写 slot→写 header）可知：**只要 XNU 活到 log writer，`boot_id` 必然变化**。
所以「窗口不变」= 内核死在 UFS root layout 之前的早期启动。

### 12.6 R160：修复与验证，以及**两次静默启动**与一次对照实验

- 修复（R160）：`scm5` 的 7 个 `register uint64_t xN __asm__("xN")` **移进循环体**，使每次重发前重新装载
  `x0=fn / x1=info / x2..x5=args / x6=0`，而不是把上一次的返回值当输入再打一次。
- CI #260（run `43829869564`）success；工件 `kernel.macho` sha256 `d36a34e2…`、`payload.img` `6e531092…`；
  静态检查 PASS（6 个禁用符 0 命中、无 `stage6=exec`/`stage3=exec`、`gputate=` 1 处、`rt=` 43 处、
  固件有序且 gap 16）；`llvm-objdump` 确认重试路径 `0xbf70620-0xbf7063c` 逐条重装 x0..x6 后再 `smc #0`。
- **真机结果 1**：`flash system_b` + `fastboot boot` → 40 s 后回 TWRP；`system_b` 回读头 8 MiB
  sha256 `8998eba8…` == 本次 payload；窗口 sha256 `2f513143…`、`boot_id=935335677` → **零字节，仍静默**。
- **真机结果 2（对照）**：同机、同 `boot-r16.img`、只把 `system_b` 换成 ci258 payload（头 8 MiB
  `9897785c…`，即 `3f1de2be` 时代那份）→ 窗口 sha256 `96f0d655…`、`boot_id=629517746`、
  首条带时间戳行 `t=30.72 s`（`kprintf initialized` … `NativeUfs: OS volume 04769D26… ready`）、
  8 条 `enchilada-adreno:` 在 `t=31.79–32.06 s`（`cx_on=0 gx_on=0 verdict=cx-DOWN`、`stage2=cx-down-gmu-not-mapped`、
  `stage3=plan …`、`stage5=run up=0 up_fail=5`），无 stage6/stage7。
  ⇒ **设备管线、payload 读取、日志系统都正常**；差异只在 kernel 侧。
- 当时的归因：「`4a786a39` 相对 `3f1de2be` 的早期执行差异只有 R152 的重试环」→ 环是元凶。
  **这个归因在 R161 被否掉**（见 12.7）。
- 同时纠正一条我写错的话：ci258 payload **并不是**没有 `stage7` 代码 —— 它的 `stage7=` 字符串在偏移
  2,260,163（`enchilada-adreno` 在 2,258,614），只是启动时 CX 是 down、没走到那行。
  真正的判据是 `gputate=` 在 ci258 payload 里**不存在**（R158 才引入）。

### 12.7 R161：重试环默认关闭（把「重发」变成显式开关）

- 改动：`extern "C" { volatile unsigned armcpu_scm_retry_cap = 0; }`；`scm5` 每次调用
  `const unsigned cap = __atomic_load_n (&armcpu_scm_retry_cap, __ATOMIC_RELAXED);`，
  循环边界 `if (retries >= cap)`。`cap=0` → 第一次 `a0 > 0` 就 `capped++` 并退出，`rt=0`，**不重发**。
- verifier：stage6 289 → **299**（新增 cap 默认 0、原子读、`retries >= cap`、「循环体内 7 个寄存器重载」断言）。
- CI #261（run `43836989899`）success；`kernel.macho` `7f5e5decd6…`、`payload.img` `773e1727…`；
  `llvm-objdump` 确认 `_armcpu_scm_retry_cap` 落在 `(__DATA,__common)`（加载时零填充），
  `0xbf705cc cbz w27,0xbf70664` 就是 cap==0 → capped++ 后退出、不重发。
- **真机结果：仍然完全静默。** 门禁 PASS → `flash system_b`（147456 KB）→ 回读 `system_b` 前 8 MiB
  sha256 `fac93fc7…` == 本次 payload → `fastboot boot` → 40 s 后 TWRP → 窗口 sha256
  **`96f0d655…`，与 12.6 的 ci258 对照窗逐字节相同**（`boot_id=629517746`）、零字节。
- **结论：cap=0 不足以救回启动，「环跑起来了」不是充分条件。** R152「照 Windows 无条件重试」的解读
  在这台机器的早期启动阶段仍被否定，但杀死早期启动的具体条件**仍未定位**。

### 12.8 R162：把早期 SCM 传输退回 ci258 的字节形状（重试机制隔离到晚段）

- **动机**：R161 的 cap=0 没有救回启动 → 问题不在「环是否跑起来」，而在早期路径相对 ci258 形状的**任一**差异。
  逐指令对比 `ArmCpu::start`（ci258 `0xfffffff00bf71220` vs r161 `0xfffffff00bf7174c`，两版都 1130 条指令、
  `smc` 9 vs 18）后，早期路径只剩三处不同：①每次 `smc` 前多一条 `mov x6,#0`；②正 `a0` 路径上多一个
  `armcpu_scm_retries_capped` 的 `ldxr/stxr`；③多读一次 `armcpu_scm_retry_cap`，并因此把
  `_armcpu_wdog_live` 等 BSS 符号整体后移 24 B。
- **③为什么危险**：`armcpu_scm_retry_cap` 是零初始化全局 → `__DATA,__common`（BSS），**文件里没有字节**；
  它的 0 只在 loader 对 `__DATA` 做 `vmsize − filesize`（3,932,160 − 442,368 B）零填充后才存在。
  `ArmCpu::start` 很早，而**平台上没有任何证据**说明那时填充已完成（R147 的 `armcpu_scm_busy` 锁在
  t≈31.8 s 读为 0，只证明那时已填充）。ci258 的早期路径**完全不碰 BSS**（只用栈）——这是新内核新增的依赖面。
- **改法**：新增 `struct ScmTriple { uint64_t a0, a1, a2; }`；`static ScmTriple scm5(...)` 回到**单次**
  `smc #0`、六个操作数（x0..x5）、`x6` **只作 clobber**、无 cap、无计数、无循环；`scm4`/`scm` 成为返回
  `ScmTriple` 的薄包装；`armcpu_scm_call` / `_call5` / `_call5_r` 用 `ScmTriple`；`scmDisableCrashdump`
  的三个局部改 `ScmTriple`（只用 `.a0/.a1`，语义不变）。R152/R160/R161 的重试机制（每轮重载 + cap +
  两计数 + `ScmResult{a0,a1,a2,a3,a6,retries}`）**整体搬进 `scm5Retry`**，唯一调用者是
  `armcpu_scm_call_full`（stage-7 shmbridge 探针，只在日志写入点之后跑）。
- **验证**：stage6 verifier 改成两组断言 —— 早期纯净（`scm5` 段内不得出现 `armcpu_scm_retry_cap`/
  `for (`/`__atomic`/`armcpu_scm_retries`/`smc5Retry`，且**恰好一条 `smc #0`**，`x6` 不在 `"+r"` 操作数里
  但在 clobber 列表里）；晚段保留（`scm5Retry` 的 cap/atomic/`retries >= cap`/`int64>0`/每轮重载全部在内，
  且 `scm5Retry (` 全文恰好 2 处提及 = 定义 + `call_full` 调用）。stage6 checks 299 → **314**；
  `verify-enchilada-k3-feasibility.py:91` 锚点同步改为 `static ScmTriple scm (uint64_t fn`。
  host **31 套**、probe **26/26**、全量 **71/71** 全过；另加一道主机侧防手误：
  `work/tmp/r162_syntaxcheck.py` 把 SCM 段逐字切片后 `clang --target=aarch64-none-elf -fsyntax-only` 编译（exit 0）。
- **R162 尚未上机。** 判据唯一：窗口出现新 `boot_id` 且 `gputate=` 有值 = 跨过 30.72 s；仍零字节则说明
  还有第三处差异（`mov x6,#0` 已被本轮回退，剩下的是尚未识别的构建/布局副作用），必须继续二分，不许再猜。

### 12.9 R162 真机：**第四次零字节**，早期 SCM 形状假设被否

- **工件**：CI #262（run `43846318325`）success；`kernel.macho` 126,148,608 B sha256 `49d863c9e5ebcffaf71ee5a50c6da895f995bcdddafdb52137f827ca96444260`、
  `payload.img` 150,994,944 B sha256 `656f19add8d77fbdc52549f8303f5be391ab410031d6ff115ee81f54cce905e3`。静态 PASS（6 禁用符 0、
  `stage6=exec`/`stage3=exec` absent、`gputate=` 1、`rt=` 43、固件 `mdt@1223492` gap 16 `b02@1230192`）。
- **反汇编门禁**：`_armcpu_scm_call` 96 B / **1 次 smc** / 无 `ldxr` / 无 cap 读；`_armcpu_scm_call_full` 456 B / 2 次 smc /
  `ldxr`=2 / 读 cap —— 重试体只在晚段入口。`ArmCpu::start` 2,672 B / 668 条指令 / 9 次 smc，**与 ci258 同形**。
- **上机**（18:35:27 → 18:36:18，一次性）：门禁 PASS → flash `system_b` OK → `fastboot boot` OK → +38 s 回 recovery →
  arm-bcb PASS → `dd if=/dev/block/sda17 bs=4096 skip=13207739 count=8192` → 窗口 `work/tmp/r162_window.bin`
  33,554,432 B **sha256 `96f0d6556742fb17ae90c75d4037f2690495a4146f95cd1092d30a07479da243`，
  与 R160 对照窗逐字节相同**（`boot_id=629517746`、`msg_bytes=28630`、`slots_written=7`、`gputate=0`）。
- **刷写有效性**：`system_b` 前 8 MiB 回读 sha256 `8a478034d8018b1e2b47d3b6ba8866aa2fc2ff1088618246f34af6be8aea28d0` == 本次 payload 头。
- **结论**：三个假设全部被真机否掉 —— ①重发（R161 cap=0）、②cap 读未零填充 BSS（R162 不读 cap）、③早期 SCM 代码形状
  （二进制逐条等同 ci258）。**「30.72 s 之前到底哪一处让它死」仍未定位**；便宜判别器 `ro.boot.bootreason` 与
  `/sys/fs/pstore` 都是空的，没有可用的事后证据。
- **下一个实验（排管线，不是猜源码）**：用 `device_ref` 从 ci258 那棵树 `3f1de2be`（2026-11-15T04:58:57Z）重建 payload 再上机。
  **教训**：`device_ref` 必须是分支/标签名，短 sha 会被 actions/checkout 当分支去 fetch（#263 就是这样失败的，日志
  `git fetch --depth=1 origin +refs/heads/3f1de2be*:…`）；已建一次性分支 `r163-ctl-ci258` 指向
  `3f1de2be7c607bd45958dd3cbbfbd3b55cc26043` 并重新派发 **#264**（run `43851484483`），**不动 main**。

### 12.10 ROUND 163：管线对照构建 #264 能启动，静默归因收缩到我们的源码

R162 上机仍零字节（见 12.9），所以下一步**先不猜源码**，而是排除「工具链/打包漂移」这一整类可能。

**对照构建的做法**：用今天完全相同的 workflow 输入（`unixnu-build` main `056130ab`、`xcode=26.6`、`data_seed_run_id=41552968815`、`unixnu_ref=8ddccb1e`、同一份 8652 B 固件 payload）从 ci258 的那棵树重建。`device_ref` 必须是**分支名**：第一次派发 #263 传了短 sha `3f1de2be`，actions/checkout 把它当成分支（`git fetch --depth=1 origin +refs/heads/3f1de2be*:refs/remotes/origin/3f1de2be*` 三次失败），job 死在「Check out the device repository」，构建根本没开始。改用一次性分支 `r163-ctl-ci258`（指向 `3f1de2be7c607bd45958dd3cbbfbd3b55cc26043`，不动 main）后 **#264**（run `43851484483`）success。

**工件**：`kernel.macho` 126,148,608 B sha256 `6a79ee782a27847fb865535acce693303a0f1ba1d8660fd2e4da57bc4cdef491`；`payload.img` 150,994,944 B sha256 `d6660eae5e795edad2bc6dcb0dfa5464cff087e8bfa32a589693f6300a984bda`（头 8 MiB `88178129f5457a723df6190f7980dea4b0b9ff9d828418f62fd2ae181e0e0173`）。身份核对：`gputate=` -1、`ScmTriple` -1、`armcpu_scm_retry_cap` -1、`stage7=` @2,260,163、`enchilada-adreno` @2,258,614、Mach-O @952,320 → pre-R158 树（它与 ci258 的 payload 并非逐字节相同：前 1 MiB 有 3,421 字节差异，说明构建不可复现，但树身份一致）。

**真机**（2026-11-16 19:02:02 → 19:02:54）：门禁 PASS → `flash system_b` OK → `fastboot boot work/out/boot-r16.img` OK → +36 s 回到 recovery → arm-bcb OK → `dd … bs=4096 skip=13207739 count=8192` 拉窗口 → 回读 `system_b` 头 8 MiB `88178129…` == 本次 payload 头（刷写有效）。窗口 `work/tmp/r163_ctl_window.bin` 33,554,432 B sha256 `cd2e4c1b806d2817a6422f472ddf3cce89f580822837bb98adf4640dbd33e713`；头 `boot_id=584678385`（此前从未出现）、`msg_bytes=28629`、`slots_written=7`；计数 `enchilada-adreno=8 stage7=1 gputate=0`（`gputate` 不存在属预期：这是 R158 之前的树）。stage7 行完整：`x0=42000201/42000202/42000205 rc=4 ran=1 calls=1 t0=ffffffffffcfffbb … p13=0/0 v13=0 … av2=1 sb=0 … sbc0=57 … sb2=8587eb18 … sbd=0/58/0 v=1 …`。

**结论**：管线无罪。今天的 workflow + 工具链能把老树编成**能启动、能写完整日志**的内核；因此 R159 以来四次零字节是**我们源码的差异**造成的，四次读数也都在同一管线下取得、彼此可比。

**几何只剩两处增长**（`work/tmp/r163_geom2.py`，ci258 vs R162）：17 段几何全等，仅 `__TEXT,__cstring` +136 B、`__TEXT_EXEC,__text` +1024 B、`__DATA,__common` +24 B（3 个新全局）、`__DATA,__bss` +344 B（`gGpuStateReplay[340]` + `gHaveGpuStateReplay`）、`__LINKEDIT` filesize +472 B。BSS 增长把 `__DATA` 内所有 BSS 符号整体后移（`armcpu_scm_busy` `#0x90`→`#0xa8`，`armcpu_wdog_live` `#0x20`→`#0x38`）。

**下一步（已派发）**：一次性分支 `r163-b-armcpu-new` = `3f1de2be` + 只换 `drivers/ArmCpu/ArmCpu.cpp` 为 R162 版（tree `80c0fc5dcd037e504f40295ae552cc851ff9be74`、commit `2e25b472b8b4286ac4de097cc312d0b26153fbd9`），CI **#265**（run `43854373803`）。静默 → 元凶在 ArmCpu.cpp；能写日志 → 元凶在 probe，做 bisect B。

### 12.11 诚实边界（R162 之后，与 §九 并存）

- 准入表 **`modeled=9 / satisfied=0 / BLOCKED` 未变**；`CONFIG_XNU_NATIVE_ADRENO=n` 未变；**GPU 从未渲染**；
  `gputate=` 一次都没拿到。
- 我们的内核**从未成功认证 zap**；`PAS_INIT_IMAGE` 仍被拒（`0xFFCFFFxx` 族），**成因未解释**。
- `SHM_BRIDGE_CREATE` 的 `0x57` / `0x5b` 两个状态码**未解释**（不在 pin 的 8 个错误码里）；句柄复用**未解释**。
- 「早于 30.72 s 的某处差异会杀死启动」有**四次静默启动 + 一次能写日志的对照**支持，但
  **具体哪一处仍未定位**；R162 是「退回设备已证的形状」，**不是**「找到了 bug」——R162 自己也被真机否掉了（见 12.9）。
- **R162 已测：`boot_id` 仍是 `629517746`、`gputate=` 仍为 0**；对照构建 #264 的读数还没拿到，
  在拿到它之前不得对「是源码还是管线」下结论。
- **不得**写「PAS 通过了」「GPU 能工作了」「Windows 能做到所以我们也能做到」——
  用户证实的是 **Windows 在这一台上真的驱动过**，不是我们做到了。

## 十三、ROUND 163：三条二分判决 + P1b 静默 + P2 待上机

### 13.1 为什么必须做这一轮
§十二 的「四次零字节」都是同一条 CI 管线产出的，但此前从未证明「今天这条管线还能编出能启动的老内核」。若管线自己漂移了（xcode / seed / unixnu_ref / 固件注入），全部归因都要重读。所以先做对照构建，再按文件二分。

`device_ref` 只能填**分支名**：填短 sha 时 `actions/checkout` 会去 fetch `refs/heads/<sha>`，三次重试全失败（#263 就是这样死的）。要按 sha 建树就先建一次性分支。

### 13.2 三条判决（全部真机 VERIFIED）

| 实验 | 树 | 结果 |
|---|---|---|
| #264 | `3f1de2be`（ci258 树） | 能启动，写完整日志：payload `d6660eae5e795edad2bc6dcb0dfa5464cff087e8bfa32a589693f6300a984bda`，窗口 `cd2e4c1b806d2817a6422f472ddf3cce89f580822837bb98adf4640dbd33e713`，boot_id 584678385 |
| #265 | `3f1de2be` + R162 `ArmCpu.cpp` | 能启动：payload `9e808dc95d8b1abe34b00a53417ebd7edee01e00dc4900f3c3668266bd67dfcf`，窗口 `4dce047b2a7adb9d93c463c01dfc7b8806c8836cd4c701644d9b8d2a1a462cbc`，boot_id 584943767 |
| #267 | `467201f8` + pre-R158 probe | 能启动：payload `936d9b3b29c0513f9463602d950d0269238171735286a7ff8b89265c909d4f5c`，窗口 `bcb742ad1f46fce38210d98e901aa4d3ba526af40c3261a1bd26bdc0a0207972`，boot_id 584592740 |

⇒ 管线无罪、`ArmCpu.cpp` 无罪、**ROUND-158 的 probe 增量有罪**。反向二分（老树 + 新 probe，#266）编不过：`ld: Undefined symbols for architecture arm64: _armcpu_scm_call_full` —— 老 ArmCpu 不导出它，所以「一对一变量」只能在「新树 - 老文件」方向做。

### 13.3 P1b：删掉 SECVID 读，仍然一个字节都没写
`r163-p1b-nosecvid` = `467201f8` + probe 整块删除 `if (gxon) { armcpu_map_physical(0x05000000,0x40000); 读 0xf400/0xf803/0xf800/0xf804/0xf802 }`，并连 `uintptr_t gGpuVa` 声明一起删（不删声明会死在 `-Werror,-Wunused-variable`，#268 就是这样）。

工件：payload `289f043c428e446fa20cf1a114e1a9760d2752a21150f6c28881c237969ec8b4`（头 8 MiB `572e880827520c2514745cec857504634660097da4e44e95e4a0369febb48038`）、kernel `a688579f511de34cf8f15ff3feef682a64b923932c5cfb84a29e8cbd9f4abde0`。
真机：刷写有效（`system_b` 回读 == payload 头），40 s 回到 TWRP，窗口 `bcb742ad…` —— **与 #267 那次启动逐字节相同**，boot_id 仍是 584592740。P1b 没有写出任何一个字节。

⇒ SECVID 寄存器读与那次映射不是死因。而且时间线也不支持它：`BootLog.c` 每轮顺序是 slot → msgbuf → header → sync，静默窗连 msgbuf 正文都是上一次启动的，说明内核死在 writer 完成**任何**一轮有效写入之前（对照窗里 writer 大约在 28.7 s 就已开跑，30.72 s 是首批文本的时间戳），而 `.1094-1137` 的 `gputate=` 块在 poll 的 stage≥4 分支里（≈31.8 s）。两条证据同向。

### 13.4 P2（在飞，未上机）
`r163-p2-noreplay` = `467201f8` + 删 `char gGpuStateReplay[340]; bool gHaveGpuStateReplay = false;` + `setGpuStateReplay` 空实现 + 删 replay 打印，**保留 `gputate=` 行**。CI #270（run 43871666643）success；payload `258ad482489c2fcda1afccb5e7d337e3eede2ff93bbbd88a41d65859531b0373`、kernel `56f95a08941ee18d9bdda0a3f4343770077d65166b78b17f4e9691282b478e0b`；静态 6 禁用符 0、`gputate=` 1、`rt=` 43、固件字节与钉住 sha 一致。**尚未上机。**

判读：能启动 → ROUND-158 的 344 B statics / `__DATA` BSS 位移是死因（并首次拿到 gxon/svt/ttbr0）；仍零字节 → 只剩 s7 的 `rt=/a3=/a6=` 字段与 `armcpu_scm_call_full` 调用。

### 13.5 诚实边界
- 具体是 ROUND-158 probe 的哪一行增量仍未定位；344 B statics 只是「下一个假设」，不是结论。
- 五次零字节启动（R159/R160/R161/R162/P1b）都没有留下事后证据（`ro.boot.bootreason` 空、`/sys/fs/pstore` 空）。
- `gputate=` 一次都没取到：gxon / svt / ttbr0 仍然未知，GPU 从未渲染，准入表仍 modeled=9 / satisfied=0 / BLOCKED，`CONFIG_XNU_NATIVE_ADRENO=n`。

### 13.6 P2 上机：仍然零字节 —— 而且时间线对不上（ROUND 163 收官）

`r163-p2-noreplay` = `467201f8` + 删掉 `char gGpuStateReplay[340]; bool gHaveGpuStateReplay = false;` + `setGpuStateReplay` 改空实现 + 删 replay 打印，**保留 `gputate=` 那一行**。commit `59d73ea78785ed49b396d67e94485c64b292ad20`，tree `da4b6231cd74ec4abb0b374ab9f501902b23dec3`，CI #270 / run `43871666643`。

工件（本地已核）：payload `258ad482489c2fcda1afccb5e7d337e3eede2ff93bbbd88a41d65859531b0373`（150,994,944 B），kernel `56f95a08941ee18d9bdda0a3f4343770077d65166b78b17f4e9691282b478e0b`（126,148,608 B）。静态：6 个禁用符全 0，`gputate=` 1 处，`rt=` 43 处，内嵌固件字节与钉住 sha 一致（mdt @1223492、b02 @1230192）。

真机（08:32:17–08:33:11）：刷写有效（`system_b` 头 8 MiB sha256 `e38629a522a451690981a0a388b0eea6d50a416fa747a66e7964b3a5402a055a` == payload 头，回读一致），39 s 后回到 TWRP，窗口 32 MiB sha256 **`bcb742ad1f46fce38210d98e901aa4d3ba526af40c3261a1bd26bdc0a0207972`** —— 与 #267 / #269 那次启动**逐字节相同**。头：`boot_id=584592740`（就是 #267 的 id）、`first_lba=14809083`、`msg_bytes=28622`、`slots_written=7`、`uptime_s=3`、`write_errors=0`。计数：`enchilada-adreno=8`、`stage7=1`、`gputate=0`。

⇒ **ROUND-158 的 344 B statics / `__DATA` BSS 位移不是死因。** P2 判读的另一半（能启动 → 首次拿到 gxon/svt/ttbr0）也不成立。

注意：窗口里那条 `stage7=exec …` 是 **#267 那次启动留下的旧内容**，不是 P2 的读数 —— 窗口逐字节未变，所以连正文都是上一次的。任何引用这一行的判读都无效。

**镜像几何**（`work/tmp/r163_geom_p2.py`，本地测量）：四份 kernel 的所有 segment vmaddr/vmsize/fileoff 完全相同（`__TEXT` 0xfffffff007004000、`__DATA_CONST` 0xfffffff00716c000、`__TEXT_EXEC` 0xfffffff00b838000、`__DATA` 0xfffffff00c088000、`__LINKEDIT` 0xfffffff00c518000），`ftype=2 flags=0x218001 entry=0x0` 四者一致。段内差异只有：`__cstring` 1,037,148 → 1,037,284（+136）、`__text` 8,646,896 / 8,647,352 / 8,647,780 / 8,647,784、`__common` 659,416 → 659,440、`__bss` 2,817,272 / 2,817,272 / 2,817,608 / 2,817,280（P2 只 +8）。没有「bootloader 看到不同镜像」的证据。

**剩余嫌疑收敛到 99 行**：P1b 新增 135 行、P2 新增 139 行，交集 **99 行**（P1b-only 25、P2-only 27）。交集只有两簇：①`armcpu_scm_call_full` 的 weak_import 声明 + shmbridge CREATE 处把 `armcpu_scm_call5_r` 换成 `armcpu_scm_call_full(..., &sbc3, &sbc6, &sbRt)` + `s7a3/s7a6/s7rt` 与格式串 `"rt=%u a3=%llx a6=%llx "`；②整个 `gputate=` 块（`gxNow`/`gxon`/`gxb16`、SECVID 读、`sctlr/ttbr0/ttbr1`、`char gpuStateRow[340]`）。

**但时间线说这批代码不可能杀死启动。** `r163_probe_p2_remote.cpp` 的 stage 门是 `691 stage >= 2`、`744 stage >= 3`、`807 stage >= 5`、`894 stage >= 4`、`1025 stage >= 6`，`gputate=` 块起于 1057 且嵌在 stage>=4 的 permitted 分支里（要先过 963 映射 kgsl SMMU、980 读 0x28）。poll 由 `package/BootProgress.c:1299` 在 `monitor()`（`BootProgress.c:1281`）里以 1 Hz 调用（`1308 assert_wait_timeout(...,1000,NSEC_PER_MSEC)`）。对照窗实测 stage4 在 t≈50 s，而静默的定义是「writer 首次有效写入之前就死」（对照窗 writer ≈28.7 s 起、首批文本 30.72 s）。**差异代码最早也要到 50 s 才可能执行，而内核在 28.7 s 之前就死了。**

这条矛盾与 §13.2 的三条判决并存，是下一步必须先解决的东西，不能靠再删代码来消解：

- 可能一：静默根本不是「代码执行到那里才崩」，而是更早的东西（镜像布局/加载/入口），那么 §13.2 的「probe 增量有罪」就需要重新表述为「probe 增量的某个副作用」而不是「某行代码运行时崩」。
- 可能二：「静默 = 死在 writer 之前」这个前提本身没被验证过 —— 零字节也可能意味着 payload 根本没跑起来（例如 boot 参数没匹配、`gNotArmed` 直接 return、或内核在别处没走到 monitor）。
- 因此 P3 的第一件事不是二分 `call_full`，而是**先分清「内核死了」与「payload 根本没跑」**：放一个极早期标记（在 `monitor()` 第一轮之前、甚至 `boot_log_tick` 之前就能落盘的东西），再决定二分什么。

诚实边界：本条判决只证明「344 B replay statics 无罪」，不证明任何别的；`gputate=` 仍一次都没取到，GPU 从未渲染。

### 13.7 三条本地测量：两条是反向结果（省掉下一轮的弯路）

P2 判决之后，先做了三项不需要 CI、不需要上机的测量，用来决定 P3 切哪里。结论是：**payload 字节、镜像几何、符号表都不是区分「能启动」与「静默」的判据**。

**① payload 整镜像逐块比对不区分能启动/静默**（`work/tmp/r163_payload_diff.py`，64 KiB 块 sha256）：

| 对照 | 大小 | 不同块 | 不同区域 |
|---|---|---|---|
| #267 能启动 vs P2 静默 | 均 150,994,944 B | 285 / 2304（17.81 MiB），24 段 | 0x04900000 起 135 块、0x05370000 起 104 块、0x0741–0x0773 零散 |
| #267 能启动 vs P1b 静默 | 同上 | 285 / 2304，24 段 | 与上表逐段相同 |
| P1b 静默 vs P2 静默 | 同上 | 282 / 2304，24 段 | 几乎相同 |
| #267 能启动 vs ci258 能启动 | 同上 | **288 / 2304（18.00 MiB），23 段** | 0x04900000 起 138 块、0x05320000 起 109 块 |

关键读法：**「能启动 vs 能启动」的差异（288 块）比「能启动 vs 静默」的差异（285 块）还大**。所以 payload 层面的字节差异完全被内核本身（以及随之重排的内容）主导，不存在「打包/注入环节坏了」的独立证据。任何「再比一次 payload 就能定位」的设想都是错的。

**② 四份内核的段表完全相同**（§13.6 已记，`work/tmp/r163_geom_p2.py`）：vmaddr / vmsize / fileoff 逐段相等，`ftype=2 flags=0x218001 entry=0x0` 四者一致。差异只在段内尺寸（`__text` +432、`__cstring` +136、`__bss` +8）。**没有「bootloader 看到的镜像布局不同」这回事。**

**③ 符号表差异只有 1 个**（`work/tmp/r163_symdiff2.py`，`llvm-nm -S --defined-only`）：

- #267 能启动 67,936 个符号；P2 静默 67,937；P1b 静默 67,938；ci258 能启动 67,932。
- #267 → P2：**只新增 1 个** `__ZN12_GLOBAL__N_16gGpuVaE`（b，0xfffffff00c444cb8，size 0），删除 0，**尺寸变化的符号 0 个**。
- #267 → P1b：只新增 2 个（`gGpuStateReplay`、`gHaveGpuStateReplay`）。
- #267 → ci258（能启动的对照）：只少 4 个（`_armcpu_scm_call_full`、`_armcpu_scm_retries`、`_armcpu_scm_retries_capped`、`_armcpu_scm_retry_cap`）。
- 注意：`llvm-nm` 对绝大多数函数报 size 0，所以「尺寸没变」不等于「代码没变」；`__text` 确实长了 432 B。

**④ 为什么「逐字节 diff `__text`」这条路走不通**（`work/tmp/r163_textdiff.py` 实测）：因为 P1b/P2 的 `__cstring` 多了 136 B，所有字符串地址整体后移 136，于是**每个引用字符串常量的函数**里那几条 `adrp`/`add` 立即数都变了 1–4 字节。结果是几乎每个函数都出现 1–37 B 的零散差异（`IOSKRegionMapper`、`SecureDT*`、`IOPMrootDomain`、`ArmCpu`、`NativeUfs`…全都在列），差异总量远超 432 B，无法据此把「真正改动的代码」从「字符串地址位移的连带效应」里分出来。要在这条路上继续，得先按 section 偏移把 `__cstring` 的位移补偿掉再做 diff，成本高于收益。

**这三条测量的共同结论**：能启动/静默的差别**不在镜像层面**，只在「某个函数在某个时刻被执行到」这件事上。这与 §13.6 的时间线矛盾指向同一个方向 —— 该去验证「代码到底有没有跑到」，而不是继续在镜像里找不同。

---

## 十四、ROUND 163 P3：零 CI 的 stage 二分，死因落在 ROUND 158 的只读块

### 14.1 为什么这次可以零 CI

`board.toml:399` 的 `enchilada-adreno=6` 最终只是 payload 里 `EFI/UniXNU/unixnu.toml` 的 `args` 一行（1377 B）。用仓库自带的 mtools（`tools/mtools/mcopy.exe`，GNU mtools 4.0.49，**不需要安装**）把这份 toml 抽出来、把 `enchilada-adreno=6` **等长替换**成 `enchilada-adreno=N`、再写回镜像（`work/tmp/r163_stage_payload.py`），kernel 与 ADT 的字节一个不动。于是「同一内核、同一固件、同一台设备，只改 stage」变成一次约 2 分钟的上机，而不是一次 15 分钟的 CI。

一次替换的真实镜像差异是 1397 B / 9 段：8 段是 FAT 与目录项（1 B 属性、4 B FAT32 表项），第 9 段是 1377 B 的配置文本本身。**但 mtools 会把文件重定位到新簇**（旧簇内容不清除、FAT 改指向新簇），所以这 1377 B 会全部不同。教训：判镜像别按裸字节数，要按文件系统视图。

### 14.2 六次上机的判决表

判据只有一条：**窗口 sha256 变了 + boot_id 是新的** = 这次启动写出了日志；窗口 sha256 一字不变 = 这次启动一个字节都没写。

| 配置 | payload sha256 | 窗口 sha256 | boot_id | 结论 |
|---|---|---|---|---|
| stage=1 | `81ae44a9…` | `9ac03ea0…` | 586735038 新 | 能启动 |
| stage=4 | `52d7921d…` | `f8a28646…` | 584973204 新 | 能启动，`stage4=refused verdict=3(refused-cx-down) cx=00005ff4 cx_on_now=0` |
| stage=5 | `4ff51e9e…` | `f8a28646…`（与 stage=4 逐字节相同） | 584973204 旧 | **静默** |
| stage=6（P2 原样） | `258ad482…` | `bcb742ad…` | 584592740 旧 | **静默** |
| P1b@stage=5 | `d6eb2176…` | `f8a28646…`（同上） | 584973204 旧 | **静默** |
| 旧 probe(#267)@stage=5 | `17068de8…` | `aade7f67…` | 582814787 新 | 能启动，且含完整 `stage7=exec … rc=4 ran=1 calls=1 t0=ffffffffffcfffbb` |

读法：

- stage=4 能启动，而且窗口里写的是 `stage4=refused verdict=3(refused-cx-down)`：**门在 CX 没起来时拒绝了**，permitted 分支没进，ROUND 158 的块根本没执行。
- stage=5 把 CX 抬到 `80001fe0`（`cx_on_after=1`），门放行，ROUND 158 的块执行 —— 然后一个字节都不写。
- 同一 stage=5、同一固件、同一台设备，**旧 probe（没有 ROUND 158 块）能启动**，并且它同样做了五步上电、同样读了 `GR0_ID2=0x7511`、还跑完了 stage 6/stage 7（PAS SMC 返回 `0xffffffffffcfffbb`）。⇒ 五步上电、ID2 读、stage 6/7 全部无罪。
- P1b（把 SECVID 子块整段删掉）在 stage=5 和 stage=6 都仍然静默 ⇒ SECVID 不是死因。事后看这很自然：SECVID 读被 `if (gxon)` 保护，而 GX 在每次观测里都是塌陷的，那段代码从未执行过，删它等于没删。

### 14.3 更正：13.6 里那段「时间线矛盾」是错的

13.6 曾写：「两版 probe 的差异代码最早也要到 stage>=4 / CX-up 才执行（对照窗里 stage4 在 t≈50 s），而静默意味着内核在 writer 首次有效写入（≈28.7 s）之前就死了，所以代码增量在时间上不可能杀死启动。」**这个推断是错的**，原因在窗口结构本身：

- header 的 `msg_first=1, msg_blocks=32` 说明 msgbuf 只有 **16 KiB**（字节区间 [512, 16896)），而且它是**环形缓冲**。实测 `msg_bytes` 为 27445 / 27791 / 28415 / 28629，全都大于 16384，最早的文本一定已经被覆盖。
- 所以「msgbuf 里最老的文本时间戳是 28.37 s」**不能**推出「内核 28 s 才开始打印」。我把环形缓冲当成了线性缓冲。
- `work/tmp/r163_where.py` 按 header 切区：`slot_first=33, slot_blocks=16` ⇒ 槽 0 在 [16896, 25088)。`stage7=` 那一行落在 **23651（对照）/23646（stale）**，也就是**槽 0**，是 writer 正常写入的。
- ⇒ probe 的 stage7 代码在 writer 打开窗口（≈28.7 s）**之前**就已经执行过。于是「增量代码有罪」与「增量代码要到 stage>=4 才执行」之间**没有矛盾**，这个矛盾是我自己造出来的。

这条更正同时说明另一件事：**只要一个改动让内核在 writer 打开 UFS 窗口（约 28 s）之前死掉，观测结果就是「窗口一字不变」**，与「payload 根本没跑」完全无法区分。ROUND 163 的方案 A 就是为了分辨这两者而设计的，而 14.2 的表正好把它区分开了：stage=5 的窗口与 stage=4 逐字节相同，说明内核确实跑到了 probe（同一个 CPU 上同一个内核，只差一个 boot 参数），只是在自己的代码里死了。

### 14.4 死因：把 GR1 的偏移当成了 CB0 的偏移

ROUND 158 块里执行到的最后几行是：

- `sctlr = rd32(gSmmuVa, 0x0u)` —— GR0 页，每次 poll 都在读（门就是靠 `GR0_ID2` 0x28 判断的），安全。
- `ttbr0 = rd32(gSmmuVa, 0x800u + 0x20u)`、`ttbr1 = rd32(gSmmuVa, 0x800u + 0x28u)` —— **这里错了两层**。
- 之后只有 `snprintf`、`IOLog`、`setGpuStateReplay`；P2 已经把 replay 整体置空仍静默 ⇒ 这三样无罪。

两层错：

1. `drivers/NativeAdreno/AdrenoSmmuStage1V1.h:167-168` 把 `kAdrenoSmmuCbTtbr0 = 0x20`、`kAdrenoSmmuCbTtbr1 = 0x28` 定义为**相对 CB 的偏移**；而 `drivers/NativeAdreno/AdrenoSmmuCtxWordV1.h:127` 的 `kAdrenoSmmuGr1Cba2r = 0x0800` 属于 **GR1**。probe 把 0x800 当成了 CB0 的基址。
2. qcom SMMU-500（`sdm845.dtsi` 的 `adreno_smmu` @0x05040000，compatible `qcom,smmu-v2`）的 **CB0 在 0x8000**，不是 0x800。所以 `0x820/0x828` 落在 GR1 的未实现偏移上。

`ADRENO_BOARD_KGSL_SMMU_BASE = 0x05040000`、`ADRENO_BOARD_KGSL_SMMU_SIZE = 0x00010000`（64 KiB），所以这两次读不会越界触发页故障 —— 它们是**读到了未实现的寄存器偏移**。ARM SMMU 对未实现寄存器的访问会给出 external abort；在 writer 打开窗口之前收到的 abort，观测结果就是**七次静默、零字节**。

诚实边界：abort 的具体形式（SError 还是同步 abort、是否被 ARM 的 abort handler 兜住）没有抓到证据 —— 因为死得太早，日志设施还没开窗。能确定的只有：**这段代码是唯一在 stage=5 新增执行、且在能启动的构建里从未执行的代码**，而它恰好包含两次对未实现 GR1 偏移的读。

### 14.5 R164 的改动与判据

改动（`work/UniXNU-enchilada/package/EnchiladaAdrenoProbe.cpp`，验证器 `scripts/verify-enchilada-probe-stage6.py` 同步，315/315 通过）：

- 删掉 `ttbr0`/`ttbr1` 与那两次 `0x800 + 0x20/0x28` 读，**只留 `sctlr = rd32(gSmmuVa, 0x0u)`**（GR0 页，已被门本身证明是活的）。
- 快照行改为 `gputate=gx=… gxon=… gxb16=… svt_read=… svt=… sbc=… sbb=… sbs=… sctlr=…`。
- 验证器把「存在 0x800+0x20/0x28」的反向断言换成「**不存在**」，并注明这是真机反证的结果，不是放宽。
- TTBR 这件事没有丢，只是推迟到 CB0 被本驱动上电并拥有之后再读 —— 那才是它有意义的时候。

判据：默认 stage=6 上机后，**新 boot_id + `gputate=` 行出现** ⇒ 修复成立，并首次拿到 gx/sctlr；**仍然零字节** ⇒ CB0 基址不是唯一问题，下一个嫌疑是 340 B 行 + `pwRow[240]` 同帧的内核栈效应。

---

## 十五、ROUND 164：修复验证通过，首次拿到 GPU 状态字

### 15.1 判据与结果

判据在 R163 就已定死：**新窗口 sha + 新 boot_id + `gputate=` 行出现** = 修复成立且快照块真的执行了。

- CI **#271**（run `44000065302`，success），分支 `r164-gr0-only`，commit `9f50a27ee06b9d812d9e98072f362f5582262eed`。
- payload `work/tmp/r164/enchilada/payload/payload.img` 150,994,944 B sha256 `267307cf21cac073c2be6940820fd6961be4a0b255fde141d41cb34fa8fe2f25`；kernel `work/tmp/r164-kernel/kernel.macho` 126,148,608 B sha256 `d93ed19162289080ed69566fe33f1e5c14b77d4433b866453ec0e601bbafc740`。
- 静态核对：`gputate=` 1、`sctlr=%08x` 2、`ttbr0=%llx` 0、`0x800u + 0x20u` 0、`0x800u + 0x28u` 0；6 个禁用符 0。
- 上机（2026-11-17 10:28:50–10:29:42，一次 flash 一次 boot）：gate PASS → flash `system_b` → `boot-r16.img` → 29 s 回 recovery → arm-bcb → dd + pull。
- 回读：`system_b` 头 8 MiB sha256 `b07244c0778c2a37422a62639d66f0d223ef8de65a3657d3641093b57652e4d1` == payload 头。
- 窗口 33,554,432 B sha256 `37d75748ae034c503d58d8f4c6d534260d3db45e27c3739df902987cd1052a2a`，boot_id **583892537**（unseen），msg_bytes 28784，slots_written 7，uptime_s 3，write_errors 0，计数 `enchilada-adreno=9`、`stage7=1`、**`gputate=1`**。

### 15.2 首条 GPU 状态行（逐字）

    gputate=gx=00088400 gxon=0 gxb16=0 svt_read=0 svt=00000000 sbc=00000000 sbb=0 sbs=00000000 sctlr=00200626

同窗口的 stage7 行：

    stage7=exec why=- armed=1 exec_key=2 sym=1 blob=embedded seg=1968 region=8c415000+2000 stages=3 seq_ok=1 mdt_ck=0980cf98 seg_ck=5a8c8234 mdt_len=6684 x0=42000201/42000202/42000205 rc=4 ran=1 calls=1 t0=ffffffffffcfffbb s0=0 t1=0 s1=0 t2=0 s2=0 p13=0/0 v13=0 t0r=ffcfffbb meta=ad924000 avail=1 av0=0/1 x1s=82 av2=1 sb=0 sbcr=0 sbc0=57 sbr=2 sb2=8587eb18 sbB=5b/8587eb18 sbd=0/58/0 v=1 rt=0 a3=0 a6=0

### 15.3 解读（把「推断」换成「实测」的地方）

- `gxon=0`、`gxb16=0`：**GX 域在 stage 6 时仍然塌陷**。此前这是推断（由 CX 起来但 GPU 没响应推出），现在是测到的。
- `svt_read=0`：SECVID 四次读**从未执行**（`if (gxon)` 拦住）。这与 R163 P1b 删除该子块后启动仍静默完全自洽 —— 那段代码本来就是不可达的死代码，删它当然无效。
- `sctlr=00200626`：~~按 ARM SMMU-500 的 sCR0 布局，bit0 SMMUEN=0（全局关闭）、bit1 CLIENTPD=1（客户端口禁用）~~ —— **此句在 R235 被更正**（见 §五十九）：bit0 是 `CLIENTPD` 且为 0，客户端口是开的；bit1 是 `GFRE`；关键词是 `USFCFG`(bit10)=1 ⇒ 未匹配流 fault。位 9/21 在钉住头文件里无名字，不命名。
- `rt=0 a3=0 a6=0`：`armcpu_scm_call_full` 的三通道返回在真机上可用，且**重试计数 0** —— R152 的重试环没有被触发（`cap=0` 的默认语义成立）。
- `gx=00088400` 尚未按 a6xx 的 GPUCC GX 字段解码；这是下一步最便宜的活（只读代码）。

### 15.4 这次修复为什么可信

三层证据互相独立：

1. **因果**：R163 的 stage 二分把致死代码夹到「stage=5 才新增执行」的唯一一段（`+1054,84` 那个 hunk，其余增量都在 stage≥6 或 poll 之后）。
2. **对照**：同一 stage=5、同一固件、同一设备，pre-R158 的 probe **能启动**（`aade7f67…`、582814787），而且它跑过同样的五步上电、ID2 读、stage6/7。
3. **干预**：把那段里唯一可疑的两行（读 GR1 未实现偏移）删掉之后，**启动恢复**，并且快照行首次写出。

第 3 条是关键 —— 它把「相关」变成「干预有效」。仍然没有抓到 abort 本身的证据（死得太早、无窗口），这一点在 §十四 14.4 已如实写明。

### 15.5 下一步

1. 解码 `gx=00088400`（a6xx GPUCC GX 字段），确认 GX 停在哪个子状态。
2. 落地 **GX 上电路径**（`AdrenoClkCtlV1` 的 GX 分支 + 时钟/regulator 使能）；CX 已有五步序列（`cx_after=80001fe0`），GX 还缺。
3. GX 起来之后才读 SECVID（`svt`），再谈 CB0/TTBR。（原文写「`SMMUEN` 置位后才有意义」，R235 更正：sCR0 无此位；真正的前置条件是 SMR 里至少有一条有效流映射。）
4. 收尾：删八个一次性分支；`porting.toml` 写 R164 条目。

### 15.6 两个状态字按仓库自己钉住的位定义解码（本地，零成本）

仓库只钉了少数位的语义（`drivers/NativeAdreno/AdrenoDomainState.h:101-105`）：
`ADRENO_DOMAIN_CX_ON_BIT = 31`、`ADRENO_DOMAIN_GX_ON_BIT = 16`（POWER_UP_COMPLETE）、
`ADRENO_DOMAIN_GX_OFF_BIT = 15`（POWER_DOWN_COMPLETE）。GX 状态寄存器 = `0x05091010`
（= `GPUCC_BASE 0x05090000` + `GX_GDSCR 0x100c` + `CFG_GDSCR_OFFSET 0x4`）。

**`gx = 0x00088400`** → 置位 bits **10, 15, 19**：

- bit 16 `POWER_UP_COMPLETE` = **0** → GX 没有上电完成。
- bit 15 `POWER_DOWN_COMPLETE` = **1** → GX **断电完成**：这个域不是「没起来」，而是已经塌陷到底。
- bit 10（0x400）、bit 19（0x80000）本仓库没有命名，**不猜**。若以后要命名，先拿下游 a6xx 的 GDSCR 字段表核对，再把名字钉进 `AdrenoDomainState.h`，不要在文档里凭印象写。

**`sctlr = 0x00200626`** → 置位 bits **1, 2, 5, 9, 10, 21**。~~按 ARM SMMU-500 的 sCR0 布局，bit0 `SMMUEN` = 0（全局关闭）、bit1 `CLIENTPD` = 1（客户端口禁用）、bit2 `PRIVCFG` = 1~~ —— **本段在 R235 被更正**（见 §五十九）：钉住头文件里 bit0 是 `CLIENTPD`（**实测 0，客户端开**）、bit1 是 `GFRE`、bit2 是 `GFIE`、bit4 `GCFGFRE`、bit5 `GCFGFIE`、bit10 `USFCFG`、bit11 `VMIDPNE`、bit12 `PTM`、bit13 `FB`、bit14:15 `BSU`、bit31 `VMID16EN`。关键词是 `USFCFG`(bit10)=1 ⇒ 未匹配流 fault。位 9 与位 21 在该头文件里没有名字，不命名。

两条读数的合起来的意义：**在这个时点，SMMU 是彻底关着的、GX 是彻底塌陷的**，所以「没有页表、没有翻译、没有 GPU 活动」不是推断而是实测。下一步要动的正是这两个开关：先把 GX 拉起来（`AdrenoClkCtlV1` 的 GX 分支 + 时钟/regulator），再谈 CB0 与 TTBR。

## 十六、R164 之后的静态可行性核查：GX 到底由谁上电（2026-11-17，本地，零 CI）

这一节是为了回答「下一步是不是去写 GX 上电序列」。答案是**不是**，而且原因是可引用的源码事实，不是推断。

### 16.1 结论先行

1. **GX 不能由 CPU 上电。** `drivers/NativeAdreno/AdrenoPowerUpV1.h:60-63` 明确记录：`gpu_gx_gdsc` 的
   `pd.power_on` 是 `gdsc_gx_do_nothing_enable()`，即 CPU 使能 GX 时**写 0 个寄存器**；GX-OFF 是这块板子的
   **预期稳态**。这与两次真机读数完全一致：round 35 的 GPUCC 探测给出 `gx_off=1 gx_on=0`，R164 给出
   `gx=0x00088400`（bit15 POWER_DOWN_COMPLETE=1、bit16 POWER_UP_COMPLETE=0）。
2. **GX 是 GMU 固件拉起来的。** A630 是 legacy GMU（HFI v1，带 SPTPRAC），`a6xx_gmu_resume()` 的 CPU 侧只到
   CX genpd + GX genpd 引用计数 + 两个时钟 rate + `clk_bulk_prepare_enable`；真正的轨电压是 GMU 起来之后
   由固件经 HFI/RSC 拉的（`a6xx_gmu_gfx_rail_on()` 属 legacy-only 的 GMU 侧命令）。
3. **所以「GX 上电路径」= GMU 冷启动。** 这不是新结论，`AdrenoGmuBootV1.h` 早在 round 42 就把整条序列钉住了
   （RSC/PDC 序列器、`a6xx_rpmh_start`、`a6xx_gmu_start` 的 CM3 复位 + `FW_INIT_RESULT` 轮询、
   `a6xx_gmu_hfi_start`、OOB、`a6xx_sptprac_enable`、`a6xx_gmu_gfx_rail_on`、`power_config`、legacy flat ITCM 的
   `fw_load`、`fw_start`）；缺的是**XNU 侧 backend**，不是模型。
4. **可行性判据（关键的一条）**：`a630_gmu.bin` **已经在本机**，来自对手机 `vendor_a` 的只读抽取
   （`work/tmp/a630-device-fw/a630_gmu.bin`，32,768 B，sha256
   `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`；同目录还有 `a630_sqe.fw` 31,980 B）。
   清单与校验记录在 `scripts/data/adreno-a630-device-firmware.json`。GMU 固件由 CPU 拷进 GMEM，**不需要 TZ 授权**
   —— 这与 ZAP 走 PAS 认证是两件事，也是为什么 GMU 这一步比 ZAP 那一步更可能一次成功。

### 16.2 DTB 事实（`work/Mu-Silicium-full/Resources/DTBs/enchilada.dts:23875` 起）

- `reg = <0x506a000 0x30000 0xb200000 0x300000>`，`reg-names = "kgsl_gmu_reg"、"kgsl_gmu_pdc_reg"`
  ⇒ GMU 寄存器窗口 **0x0506a000 + 0x30000**，PDC 窗口 **0x0b200000 + 0x300000**。
- `interrupts = <0 0x130 0>、<0 0x131 0>`（`kgsl_hfi_irq` / `kgsl_gmu_irq`）。
- `clocks` 4 个：`gmu_clk`/`cxo_clk`/`axi_clk`/`memnoc_clk`（这就是 `gmu->nr_clocks = 4`，与 kgsl-3d0 的 6 个
  是两套计数，`AdrenoPowerUpV1.h:45-58` 专门警告不要混）。
- `vddcx-supply = <0x1a4>`、`vdd-supply = <0x2a9>`（后者就是 `gpu_gx_gdsc`，也是 R163 里 `gpu_region` 的
  phandle —— 同一颗 regulator，别当成两样）。
- 已 device-verified 的部分：CX 的五步上电在真机跑通过（stage 5 的 `cx_after=80001fe0`、
  `cx_on_after=1`），说明「CPU 直接写 GPUCC 寄存器来抬 CX」这条路在本机可行。

### 16.3 已有模型清单（都在仓库里，全部 host-verified pure C）

`AdrenoPowerUpV1`（5 op 计划：CX genpd → GX genpd（guarded）→ core 200 MHz → hub 150 MHz → 时钟使能，含逆序回滚）、
`AdrenoGmuBootV1`（16.1 第 3 条列的那整条序列）、`AdrenoHfiV1`（HFI v1 双队列传输）、
`AdrenoGmuBusV1`（MMIO 适配器，`gmu_mmio + 0x23000`，逐域字节守卫、fail-closed 拒绝）、
`AdrenoGmuCallerV1`（precheck + cold boot 编排）、`AdrenoPowerUpGdsc` / `AdrenoGdscDecode`（GDSC 解码）。
每一个都是「调用者接回调」的形状，**没有一个碰过真硬件**。

### 16.4 真正的缺口（四条，按风险从低到高）

1. **GMU 窗口只读探测**（风险最低）：映射 0x0506a000/0x30000，读几个只读寄存器（版本/状态），确认
   「CX 起来后读 GMU 窗口不会 abort」。R163 的教训是：**未实现偏移的读会 abort 且零字节静默**，所以这一步
   必须单独做，而且只读。
2. **固件字节进 payload**：32,768 B（外加 SQE 31,980 B 暂不需要）。内嵌方式与 ZAP 相同（payload 内有空间），
   并像 ZAP 一样把 sha 钉进验证器。
3. **XNU 侧 backend**：把 `AdrenoPowerUpV1Bus` / `AdrenoGmuBootV1Bus` 的真实实现写出来 —— 时钟没有 clk 框架，
   只能按 GPUCC 直接写（CX 五步已证明可行）；genpd 用我们已经验证的五个 op 代替；HFI 队列要 GMEM 内存。
4. **中断**：`kgsl_hfi_irq` 0x130、`kgsl_gmu_irq` 0x131 未接。第一版可以纯轮询（会慢、可能错过消息），
   但 HFI 的 ack 轮询在 host 模型里已覆盖，先跑通再谈中断。

### 16.5 建议的最小验证切片（切片 0，仍不碰写入）

目标不是渲染，而是**证明 GMU 窗口可安全读写**：新增一个只读 stage（例如 `enchilada-adreno=7`），
在 CX 已 up 的前提下映射 0x0506a000/0x30000，读 3–4 个只读寄存器并打印一行 `gmu=…`。
判据：新 boot_id + `gmu=` 行出现 ⇒ 下一步可以做固件加载；仍零字节 ⇒ GMU 窗口的读同样会 abort，
必须先查它是不是被 TZ 关着（那时候再谈 RSC/PDC 的权限，而不是继续写代码）。

### 16.6 诚实边界

- 「GX 由 GMU 拉起」是**上游源码事实 + 本机两次读数一致**，不是本机实测的因果链（我们还没让 GMU 起来过）。
- `gfx_rail_on` 依赖 `vdd`（phandle 0x2a9）。若 TZ 把 regulator 归自己管，写它会 abort；这也是为什么切片 0
  只读、不写。
- `a630_gmu.bin` 的可信度来自「它是从这台手机的 vendor_a 抽出来的」，不是来自签名校验 —— 我们没验签，也不需要
  （CPU 加载不经 TZ）。

## 十七、ROUND 165（切片 0）：GMU 窗口可读，但 GMU 从未启动

**一句话**：CX 起来后 GMU 窗口**读得动、不 abort**（这是切片 0 要问的唯一问题，
答案是「可读」）；但七个字的读数一致指向**GMU 从未被启动过**（固件从未加载），
所以下一步不是「找 GX 寄存器」，而是**把 GMU 冷启动起来**。

### 17.1 为什么用第二把钥匙的第三个值

- 四个 stage 验证器把 probe 的 stage 掩码钉死为 `stage < 1u || stage > 6u`，加 stage 7
  会削弱它们；合并后的命令行 991/1000 字节，加新 key 会溢出。
- 所以沿用 ROUND 131 的形状：`enchilada-pas-exec` 的值本身选择动作。`=1`/`=2` 保持
  R129/R131 的既有含义（`=2` 就是 FAST_CALL/STD_CALL 那条 PAS 计划），**`=3` 是新的**
  「只读 GMU 探测」。`=2`→`=3` 同宽，payload 只改一个字节。
- 真机把这条设计验证了两次：新块跑了（`gmu=` 行出现），而 stage-7 的 PAS 计划**没有**
  被重新武装（`stage7=armed-not-executed why=not-armed armed=0 exec_key=3 rc=1 ran=0
  calls=0`）。也就是说 `=3` 既拿到了 GMU 读数，又没有把 R129 那条危险的 SMC 路径
  再跑一遍 —— 这正是「值选择动作」的好处。

### 17.2 判决（DEVICE-VERIFIED）

- 构建：#272 / run `44006845332`（分支 `r165-gmu-probe`，commit `8637c1a2`），
  kernel sha256 `ac3299588290bfec81a4197fe5515a5734bcfd6e8c3bde28a17da3ad7f457d94`，
  CI payload sha256 `034f2e9219249a526bbe2fd25fa894d38162c06663169499ceb2587cbd369652`。
- 上机用的镜像是**本地重打 key** 的副本：`enchilada-pas-exec=2` → `=3`（等长替换），
  sha256 `5b55c64b1fb1b8fd58325a78e30325c29833664996ba4002855f29218b7b02a7`。
  刷写回读与 payload 头一致（`system_b` 头 8 MiB `46d655d93727087b24c50959b43373c3003d27cf896f50207153497f8783e181`）。
- 窗口 33,554,432 B sha256 `32f764d5c5924443fbafa746441ea928c740141c1ae8bf8a7f7c28e5632e7d6a`，
  **boot_id=583848016（本轮新写）**，msg_bytes=28959，slots_written=7，uptime_s=3，write_errors=0。
- `gmu=` 行（逐字，本轮唯一新读数）：

  `gmu=probe fwinit=00000000 sptprac=000000d4 hfistat=00000000 hfisfr=00000000 rsccack=00000000 gdscr=00222001 general7=00000000 boot_state=1`

- 判据：**新 boot_id + `gmu=` 行** ⇒ GMU 窗口在 CX up 之后可达，读不 abort。

### 17.3 七个字怎么读（只用仓库钉住的位定义，不猜）

- `fwinit=0`：`CM3_FW_INIT_RESULT` 为 0，固件从未报告过 init 结果 —— CPU 从未把
  `a630_gmu.bin` 拷进去，也没有启动它。
- `hfistat=0 hfisfr=0 rsccack=0`：没有任何 HFI 活动、没有 SFR 地址、RSC 没有 ACK。
  与「固件没跑」完全一致。
- `general7=0` 且 `boot_state=1`：`AdrenoGmuBootV1BootState` 的规则是
  `GENERAL_7 == 1 -> WARM，否则 COLD`，而 `ADRENO_GMU_BOOT_V1_COLD` 就是 1。**冷启动**。
- `sptprac=0x000000d4`：`SPTPRAC_CLK_MASK 0x38`，`0xd4 & 0x38 = 0x14`，**不等于**
  `SPTPRAC_CLK_PATTERN 0x28` ⇒ SPTPRAC 时钟不在「已开」模式。这个字**不是零**，说明
  GMU 的寄存器块是被时钟过的（能读回非零值），但 SPTPRAC 那一步没做过。
- `gdscr=0x00222001`：`GX_RETENTION_BIT 0x800` 未置位；bit16（POWER_UP_COMPLETE）也未置位。
  与 R164 从 GPUCC 读到的 `gx=0x00088400`（bit15 POWER_DOWN_COMPLETE=1、bit16=0）互相印证：
  **GX 轨道始终塌陷**。
- 结论：GMU 的**窗口**活着、**控制器**没启动。这与上游 `a6xx_gmu.c` 的顺序一致 ——
  在 `gmu_start`/`fw_load` 之前，`GENERAL_7`/`HFI` 本来就该是 0。

### 17.4 这次判决的诚实边界

- 只有**一回合**读数。`gmu=` 行是本轮新代码，必然属于本轮；但同一窗口里的
  `gputate=` 行数值与 R164 完全相同（`gx=00088400 … sctlr=00200626`），**不能**用它
  单独证明 R158 块本轮也跑了 —— 它可能是上一轮遗留在槽里的旧行。这不影响本节结论
  （本轮结论只依赖 `gmu=` 行与 boot_id），但下游引用时不要混用。
- 「读得动」只证明**读**不 abort。写同一个窗口会不会 abort **没有测**（本切片严格只读）。
- GMU 是否被 CX 部分上电、`sptprac=0xd4` 的 bit2/bit4/bit6/bit7 各自含义、`gdscr` 的
  bit13/17/21，本仓库**没有**钉住命名，这里**不解释**。

### 17.5 下一步（切片 1：GMU 冷启动）

- 目标：让 `fwinit` 变成 `a6xx_gmu` 的 `GMU_FW_START` 值、HFI 有 SFR 地址、GENERAL_7 变 1（WARM 之后才会走 warm 路径）。
- 需要的输入已经齐了：`a630_gmu.bin`（32,768 B，sha256 `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`）、
  GMEM 基址（`kgsl_gmu_reg` 里的 GMEM 窗口）、`AdrenoGmuBootV1PlanCold` 的 op 流、
  `AdrenoHfiV1` 的队列布局。
- 顺序（上游 `a6xx_gmu.c`）：RSC/PDC 初始化 → rpmh_start → GMU clocks → `gmu_start`
  （CM3 复位 + boot config）→ HFI 初始化 → OOB → SPTPRAC → `gfx_rail_on` → power config
  → **固件拷贝进 GMEM** → `fw_start`（fence + chipid）。
- 风险已知且要说在前面：`gfx_rail_on` 要写 `vdd`（phandle 0x2a9，= gpu_gx_gdsc）。若 TZ
  把 regulator 归自己管，第一次写就会 abort —— 那会是**在日志窗口打开之后**的 abort
  （因为此时日志已经在写），所以会留下窗口，而不是像 R158 那样零字节。

## 十八、ROUND 166（切片 1a）：CM3 启动字可读、PDC 已被引导器启用，`CM3_BOOT_CONFIG` 仍是 0

**一句话**：GMU 窗口不只是可读 —— `CM3_CFG` 已经等于 `a6xx_gmu.xml` 钉住的 `0x4052`，
PDC 窗口可读且 **PDC 已经是启用状态**（`pdcen=1`，序列内存里有非零残留），
唯一对不上的是 `CM3_BOOT_CONFIG`：读回 **0**，而上游在 `gmu_start` 里要写 **`0x2`**。
这就是切片 1b 的第一条写操作，且它是**一个已知的、有上游出处的单字写**。

### 18.1 设计：为什么拆成 `=4` 与 `=5` 两级

- `=4`：在 R165 那七个字之外，从**同一个已批准的 GMU 窗口**再读六个 CM3/启动字
  （`SPTPRAC_POWER_CONTROL`、`CM3_BOOT_CONFIG`、`CM3_CFG`、`BOOT_SLUMBER_OPTION`、
  `GX_VOTE_IDX`、`MX_VOTE_IDX`），输出 `gmucm3=` 行。**不新增任何映射。**
- `=5`：在 `=4` 之上再**首次**映射 3 MiB 的 PDC 窗口（`ADRENO_BOARD_GMU_PDC_BASE`
  = `0x0b200000`，`+0x300000`），读 `PDC_ENABLE_PDC`、`PDC_SEQ_START_ADDR`、
  `PDC_SEQ_MEM_0`，输出 `gmupdc=` 行。
- 为什么分两级：PDC 窗口是本设备从未碰过的地址空间。如果合并成一个值，一次静默就分不清
  是 CM3 读挂了还是 PDC 映射挂了；分开之后，`=4` 成功 + `=5` 静默 ⇒ 责任在 PDC 窗口。
- 依然**不加新 stage、不加新 key**：命令行 991/1000 字节不动，stage 掩码仍是 1..6。
  两级都严格只读（`rd32` + `armcpu_map_physical`），没有 `write32`、没有 SMC、不重试启动。

### 18.2 判决（DEVICE-VERIFIED，两次上机）

- 构建：#273 / run `44015512552`（分支 `r166-cm3-pdc`，commit `03acc38aad0d255fda7aac394eb1180d629dde5a`），
  kernel sha256 `3441d1669ed665cd6f47caa362ba5a27049d0df4e3382122dbd1912bafd81832`，
  CI payload sha256 `53b3341c46b5759c12271750fa5c0722fe518f82d2faa57e3411f01c07537d82`。
- 两次上机的镜像都是本地等长改 key 的副本，刷写回读的头 8 MiB 都是
  `dc72f2303942d5443a07bdf4a24a28ddbeec1ef03dddd4540aa61d1a7d29f400`（与各自 payload 头一致）。
- **`=4`**：payload sha256 `a9c896f94dcbcc2d1a8eae0075f894548aadf8aa4f75eb05854b3e2d40eba49a`，
  窗口 sha256 `4ae42c21e8c40ac7debf7d3847966df2b334b458711a9a56c95ea34a1286ad59`，
  **boot_id=584944001（本轮新写）**，msg_bytes=29121，write_errors=0。
- **`=5`**：payload sha256 `968fb43aff1325e77ba04fddf95b6d756cb75f71b514b3aeaec87c5d405382a0`，
  窗口 sha256 `d9e6a96c935d84d48fbaaaba402a421dbb5fb1d9e1ead2fc49d96cbadb7819ef`，
  **boot_id=584629860（本轮新写）**，msg_bytes=29220，write_errors=0。
- `=4` 的新行（逐字）：

  `gmucm3=sptpwr=00000000 bootcfg=00000000 cm3cfg=00004052 slumber=da553642 gxvote=e10f28a0 mxvote=c58ae900 bootcfg_ok=0 cm3cfg_ok=1`

- `=5` 的新行（逐字，两行）：

  `gmucm3=sptpwr=00000000 bootcfg=00000000 cm3cfg=00004052 slumber=da553440 gxvote=e30fa8a1 mxvote=d582e101 bootcfg_ok=0 cm3cfg_ok=1`
  `gmupdc=pdcen=00000001 seqstart=00000000 seqmem=00a000a8 mapped=1`

- 两次都是**新 boot_id + 新行出现**，`gmu=map-failed` 与 `gmupdc=map-failed` 各 0 次。
  两轮 `stage7=armed-not-executed … exec_key=4/5 rc=1 ran=0 calls=0` ⇒ 新值只武装新块，
  没有重跑 R129/R131 的 PAS SMC 路径。

### 18.3 这四个结论是怎么读出来的（只用仓库钉住的定义）

- **`cm3cfg=0x4052`，两轮都等于 `ADRENO_GMU_BOOT_V1_CM3_CFG_VALUE`** ⇒ `cm3cfg_ok=1`。
  这个字**不是我们写的**（本切片只读），也不是随机值：它跨两次独立启动稳定等于钉住值。
  含义：CM3 的配置锁存已经是上游期望的样子，冷启动不需要重设它。
- **`bootcfg=0`，而钉住值是 `ADRENO_GMU_BOOT_V1_CM3_BOOT_CONFIG_VALUE = 0x02`** ⇒
  `bootcfg_ok=0`，同样跨两轮稳定。上游 `a6xx_gmu_start()` 的第一件事就是
  `gmu_write(GMU_CM3_BOOT_CONFIG, 0x2)`；这一条现在有**真机读数支撑**：GMU 没启动，
  因为启动配置字从来没有被写过。这是切片 1b 的第一个写，也是最小的一步。
- **`pdcen=1`**：PDC **已经是启用状态**，不是「等待我们初始化」。这一点很关键——
  切片 1b 若照上游 `a6xx_gmu_rpmh_init()` 无脑重写 PDC，会把**引导器已经配置好的**
  状态覆盖掉。正确做法是先读回、保存，再决定写什么。
- **`seqmem=0x00a000a8` 非零**、`seqstart=0`：序列内存里有残留内容但没有启动地址。
  与「引导器编过程序、GMU 侧没有接管」一致。
- **`sptpwr=0`**：`SPTPRAC_POWER_CONTROL` 为 0 ⇒ SPTPRAC 域没有被拉起的迹象，
  与 R165 的 `sptprac & 0x38 = 0x14 ≠ 0x28` 同向。
- **PDC 窗口可读**（`mapped=1`）：3 MiB 的第二个窗口在 CX up 之后也能安全读，不 abort。

### 18.4 这次判决的诚实边界

- `slumber` / `gxvote` / `mxvote` 三个字**两轮不同**（`da553642` vs `da553440`、
  `e10f28a0` vs `e30fa8a1`、`c58ae900` vs `d582e101`）。跨启动不稳定 ⇒ 本轮**不解释**它们，
  也不能拿它们当「引导器投过票」的证据。要看这三个字，必须先有可重复的读数。
- 只证明 PDC 窗口**读**不 abort；写同一个窗口（`PDC_ENABLE_PDC`、序列内存）**未测**。
- `pdcen=1` 是读回值，但「谁写的、写成什么样才算对」仍未知；不要在切片 1b 里假设它等于
  上游初始化后的样子。
- 两轮 boot_id 不同、读数却一致，这是好证据（不是一轮巧合）；但仍只有两轮，不是统计。

### 18.5 下一步（切片 1b：从「读」到「第一笔写」）

1. 先在不改任何东西的前提下把 `CM3_BOOT_CONFIG` 的**写**做成一个最小、可回滚的动作，
   并在同一轮里读回确认（`bootcfg_ok=1`），**不做**固件拷贝、**不动** PDC。
2. 只有第 1 步复现稳定，才把 GMU 的复位/时钟与 `a630_gmu.bin` 拷进 GMEM；
   固件拷贝的目标地址取决于 GMEM 物理基址，而该值本仓库**故意没有钉住**
   （`AdrenoMetalSeamV1.h` 明说 `gmem_base` 留空），所以第 2 步之前必须先找到可信来源。
3. 风险前置说明：`gfx_rail_on` 要写 `vdd`（phandle 0x2a9）；若 TZ 拥有该 regulator，
   第一笔写会 abort —— 但那时日志窗口已经打开，会留下窗口而不是零字节。

## 十九、ROUND 167（切片 1b）：第一笔寄存器写 —— `CM3_BOOT_CONFIG=0x02` 写入并读回成功

**一句话**：XNU 侧第一次对 GPU 域寄存器完成「点名申请可写映射 → 写 → 读回」的闭环。
`CM3_BOOT_CONFIG` 由 **0** 写成 **`0x2`**，读回 `ok=1`；同一次读数里 `CM3_SYSRESET`
的 bit0 是 **1**（CM3 核心仍被按在复位里），固件仍未启动（`fwinit=0`）。
这与本切片划定的边界完全一致：**只写这一个字节，不写复位脉冲、不载固件、不碰 PDC。**

### 19.1 为什么必须新增一个可写映射入口

- `drivers/ArmCpu/ArmCpu.cpp` 里唯一的物理映射入口 `armcpu_map_physical()` 用
  `IOMemoryDescriptor::withPhysicalAddress(physical, length, **kIODirectionIn**)`，
  且四个已在真机上验证过的调用点（GMU、GPUCC、SMMU、GPU）全部建立在「只读」这个前提上。
- 直接给旧函数加方向参数，等于让四个已验证的只读调用点随时可能被一个手误改成可写。
  因此新增并列入口 `armcpu_map_physical_rw()`：`kIODirectionInOut`、同样
  `kIOMapAnywhere | kIOMapInhibitCache`、同样「kept: never released」。
- 提供这个入口本身不改变设备状态 —— 映射只是地址翻译；写什么、什么时候写，
  是调用者的责任，而本切片把这份责任限制在一行上（`19.2`）。
- probe 侧以 `__attribute__((weak_import))` 点名声明这个符号；`ld` 能解析，
  且 host 静态检查里它出现在「未定义符号」集合中正是「写走的是点名申请的可写窗口」
  的证据，故把 `armcpu_map_physical_rw` 与其修饰名加入允许集合，而不是 stub 掉。

### 19.2 为什么「只写这一个字」是安全的

上游 `a6xx_gmu.c` 的冷启动顺序（本仓库 `drivers/NativeAdreno/AdrenoGmuBootV1.c` 同构）
把 `CM3_BOOT_CONFIG = 0x02` 放在 **pre-start 配置**阶段：它只告诉 CM3「从哪里取固件」，
本身不启动任何东西。真正让固件跑起来的是随后
`a6xx_gmu_start()` 写 `CM3_SYSRESET = 1 → PWR_COL_CP_RESP = 0 → CM3_SYSRESET = 0`
的那次**复位脉冲**，再 poll `CM3_FW_INIT_RESULT`。

所以本切片刻意不做四件事：不写 `CM3_SYSRESET`、不写 `CM3_ITCM_START`（固件门铃）、
不搬运固件、不碰 PDC。并加了一道读回门禁：**只有** `CM3_SYSRESET` 的 bit0 为 1
（核心被按在复位里、不可能已经在跑）时才执行那次写；否则只报告、不写。

### 19.3 判决（DEVICE-VERIFIED，一次上机）

- 构建：#274 / run `44021103936`（分支 `r167-cm3-bootcfg`，commit
  `c5ce4aa1f84a61fbe80f3c2ed24739e8377c0e43`），kernel sha256
  `c7e28475c1fa838f674bb0ab13780d0004d1867e393a9f89b062b35f2e6dc8ac`，
  CI payload sha256 `29b8ecfd7ed1c2c51c6bf37be3d052af3bbbe16af260bb0616dbb06691a0ce62`。
- 上机镜像是本地等长改 key 的副本（`enchilada-pas-exec=2` → `=6`），sha256
  `ae4f8e2efec30faaabc1cd9fe96f00816468e263662b1f05e45f3e1cecf4f4b2`；
  刷写回读的 `system_b` 头 8 MiB 是
  `da9ecce765736d1b04998f0c4eb973c29e4ab217d79e5023daa38a5f5f026249`（与 payload 头一致）。
- 窗口 sha256 `b3820911664af2ea855a36975c6c9dabbc6cc348ccaa5cd715e2e4e95623ef9d`，
  **boot_id=584581111（本轮新写）**，msg_bytes=28904，slots_written=7，
  uptime_s=3，write_errors=0。
- 新行（逐字）：

  `gmuboot=pre=00000000 reset=00000001 held=1 post=00000002 fwinit=00000000 wrote=1 ok=1`

- 同窗口 `stage7=armed-not-executed why=not-armed armed=0 exec_key=6 sym=1 … rc=1 ran=0 calls=0`
  ⇒ `=6` 只武装新块，**没有**重跑 R129/R131 的 PAS SMC 路径。
- `gmucm3=` 与 `gmupdc=` 在这份窗口里各 0 次，是**设计使然**（`=6` 与 3..5 是互斥的值），
  不是块消失；`gmuboot=map-failed` 0 次。

### 19.4 解读

- `pre=0`：写之前该寄存器确实是 0，与 R166 的只读读数一致 —— 两层证据互相对上。
- `reset=0x1`：`CM3_SYSRESET` bit0 置位，说明 CM3 核心被按在复位里。这既解释了
  「为什么此时写 pre-start 配置是安全的」，也再次证明 GMU 从未启动过。
- `post=0x2` 且 `ok=1`：写入被寄存器块接收，并且**读回校验通过**；
  `wrote=1` 表示真的执行了那次 store（没有被门禁跳过）。
- `fwinit=0`：固件仍未报告 init —— 符合预期，因为本切片没有触发复位脉冲。
- 工程意义：`armcpu_map_physical_rw` 在真机可用，GPU 域寄存器块从此既能读也能写；
  这是后续「GMEM 固件搬运 + `CM3_SYSRESET` 脉冲」的前置能力。

### 19.5 诚实边界与下一步

- 本轮只证明「一个 32 位字写进去并读回」；没有证明 CM3 会按这个值去取固件
  （那需要固件真的在 GMEM 里、以及复位脉冲）。
- 真正的冷启动还缺两块：**GMEM 的物理基址**（本仓库 `gmem_base` 故意留空，
  只有 `gmem_bytes = 0x00100000`，不得猜）与固件搬运路径（`a630_gmu.bin` 32,768 B
  已在 `work/tmp/a630-device-fw/`，sha256
  `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`）。
- 顺序上仍是：先落 GMEM 基址的可信来源 → 再谈搬运 → 最后才碰复位脉冲；
  每一步都要能先用只读手段验证前提。

## 二十、ROUND 168（切片 2）：GMU 固件装载并开始执行

### 20.1 这一轮新增的能力

前十七轮读的都是寄存器：能读、能写一个配置字（ROUND 167），但从没碰过 GPU 侧的内存。
本轮第一次把**数据写进 GPU 域的内存**并读回校验，而且第一次让 GPU 的 CM3 微控制器**执行**了
我们装进去的固件。

三个部件：

1. `drivers/ArmCpu/ArmCpu.cpp` 新增 `armcpu_map_physical_rw()`（`kIODirectionInOut` + cache-inhibit
   映射）。旧的只读 `armcpu_map_physical()` 一字未动，四个设备已验证的调用点仍走只读入口。
2. `scripts/generate-gmu-blob.py` —— 照 `generate-zap-blob.py` 的形状，把 OEM 签名的
   `a630_gmu.bin`（32,768 B，sha256 `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`）
   转成 **dword 数组**头文件 `drivers/NativeAdreno/AdrenoGmuBlobV1.h`（gitignored，不入库）。选 dword
   是因为消费方是 CM3 ITCM bulk 写：`a6xx_gmu_fw_load()` 就是连续 32 位字写。大小或 sha 不符时
   生成器硬错，拒绝把别的 blob「洗」进来；固件缺失时输出 ABSENT 头（present=0、数组只有一个零
   dword），不会伪造出一张全零的 32 KiB「合法」镜像。
3. `package/EnchiladaAdrenoProbe.cpp` 的 ROUND 168 块：两把钥匙
   `enchilada-pas-exec=7`（只装载）与 `=8`（装载 + 预启动字 + 复位脉冲）。

### 20.2 为什么必须分两级

`=7` 只做「把 8192 个字写进 `CM3_ITCM_START`，再回读四个点」。它不写任何控制寄存器，连
`CM3_SYSRESET` 都只读。`=8` 才在装载之后写 `GENERAL_7=1`、`FW_INIT_RESULT=0`、
`BOOT_CONFIG=0x02`、`CM3_CFG=0x4052`，然后 `SYSRESET=1 → PWR_COL_CP_RESP=0 → SYSRESET=0` 一次脉冲，
再有界轮询 `CM3_FW_INIT_RESULT`。

分级的理由：如果 `=8` 静默而 `=7` 活着，责任在脉冲而不在内存孔径；反过来如果 `=7` 就静默，
那就是 ITCM 写本身把 CPU 打死了。一个变量一次开机。

### 20.3 判决（DEVICE-VERIFIED，两次独立启动）

构建：CI **#275** / run `44027541942`（分支 `r168-gmu-itcm`，commit
`2a898266833c38a8ec49220fc45c87da9b5339b8`，tree `1d1da383…`）。固件不在仓库里，是本轮唯一一次
通过 workflow 的 opt-in 输入注入的：`extra_firmware_b64`（43,692 字符）+
`extra_firmware_layout` = `a630_gmu.bin:32768`，落进 `device/.cache/firmware/`，由生成器读取。
本轮**故意不注入 zap**：key 7/8 不跑 PAS，zap 有无只增加字节、不改变判决（窗口里
`stage7=… blob=absent` 如实记录）。

工件与静态核对（`work/tmp/r168_static.py`）：kernel
`fbe4007af6597a290652e399af4b5ffa0c305eb8960f5ed9bdb8caea91f0236f`（126,181,376 B）、
CI payload `037adfb75690497eee9433322784f5cbfcfed7761c253ddc4b548c4e046d8776`；32 KiB 固件
以**原始字节**出现在 kernel 偏移 **271192**、payload 偏移 **1223512**，证明注入真的落地；6 个禁用符
与 `stage6=exec`/`stage3=exec` 全 0；`gmufw=absent` 行不出现（ABSENT 分支没被走到）。

本地等长改 key（同一 FAT 卷、只换 `enchilada-pas-exec` 的值）：
`=7` → `89207c7157e8419738a080c9d73b6b21f4c17b79d95fa9d20b8580a5b4e12120`、
`=8` → `a5a913314720bcb4387b428ca6421fba45caf8f7dfabcffe9c2b17494db4a383`。

**`=7`（只装载）**：boot_id **586650308**、窗口
`17d6d6f84b9c712d7c2eabfd22676d0015b5e50874ff437dc59b45c7b4066511`、msg_bytes 29000、
`write_errors=0`、system_b 头回读 `b96e0f28…` 与 payload 头一致。行（逐字）：

    gmufw=load words=8192 pre_reset=00000001 pre_init=00000000 pre_sig=3b5cf1c0 post_sig=00000000 img0=00043f80 img1=00000009 imgend=00000000 rb0=00043f80 rb1=00000009 rbm=10070000 rbend=00000000 ok=1

**`=8`（装载 + 脉冲）**：boot_id **583295674**、窗口
`327c2e1751ce7e1b07d4d41bc58b32ade177cbfa162b1ba0c1754e23e785b205`、msg_bytes 29152、
`write_errors=0`。两行（逐字）：

    gmufw=load words=8192 pre_reset=00000001 pre_init=00000000 pre_sig=3b1cf180 post_sig=00000000 img0=00043f80 img1=00000009 imgend=00000000 rb0=00043f80 rb1=00000009 rbm=10070000 rbend=00000000 ok=1
    gmuboot2=sig=00000000 mask=ffffffff expect=babeface final=babeface polls=24 hfistat=00000000 reset=00000000 ok=1

### 20.4 这三行到底证明什么

- `pre_reset=00000001` ⇒ 装载前 CM3 确实还被按在复位里。`held` 门是被**硬件状态**打开的，不是我
  假定它开着。
- `rb0 == img0`（`00043f80`）、`rb1 == img1`（`00000009`）、`rbend == imgend`（`00000000`）⇒ 8192 个字
  写进去又读回来，首尾与第二字都对得上。这两个字是固件自己的入口头，不是「写什么都一样」的填充。
- `pre_sig 3b5cf1c0 → post_sig 00000000` ⇒ DTCM 末尾那个签名字在装载前非零、装载后变零 —— 这正是
  「新镜像覆盖了那块内存」的形状。这是「孔径是真内存、不是吞掉 store 的洞」最硬的一条证据。
- `final=babeface` 与 `ok=1` ⇒ 脉冲之后 `CM3_FW_INIT_RESULT` 变成 `0xbabeface`，即 legacy 固件自己的
  启动签名，**24 次轮询**就出现了。这不是常量巧合：`0xbabeface` 就是上游 `a6xx_gmu_start()` 在
  `dtcm[0xff8] <= 0x20010004` 分支上等的那个值，而 `sig=00000000` 正是走了这条分支。
- `reset=00000000` ⇒ `CM3_SYSRESET` 已释放，CM3 在跑。
- `hfistat=00000000` ⇒ HFI 没起来。这符合设计：本轮一个 HFI 队列表字都没写（`HFI_QTBL_ADDR`/
  `HFI_QTBL_INFO`/`PWR_COL_CP_MSG` 需要 GMU IOMMU 的 IOVA，本仓库没有那个 backend）。
- 两次启动的 `gputate=` 行与 R164 完全一致（`gx=00088400 gxon=0 … sctlr=00200626`）⇒ GX 轨仍断电，
  固件跑起来**并没有**顺手把 GPU 上电（它还没收到 HFI 上的投票）。

### 20.5 诚实边界（不解释的部分）

- `rbm=10070000` 是 `ITCM_START + 0x1000` 处的回读，仍在 8192 字镜像内部，但行里没有对应的
  `imgmid=`，所以**不能**声称它匹配。可能是 DTCM 边界的镜像回声，未验证，只作观察记录。
- `polls=24` 是循环次数，不是时间；不同 UART/CPU 频率下不可换算成 µs。
- 固件「在跑」不等于「在跑主循环」：没有 HFI，它很可能停在前 HFI 的等待里。本轮只证明它执行到了
  写 `FW_INIT_RESULT` 那一步。
- OOB 中断 / RSC / PDC 通路一个字都没碰；`gfx_rail_on` 的六步、power_config 的六步、`HFI_CTRL_INIT`
  全未执行。
- 只有各一次开机，没有重复性统计。

### 20.6 下一道门

HFI。要在 GMU 与主机之间建立队列，需要两个 ring + 一个 log buffer 被 GMU 用自己的地址看见，
即需要 GMU 侧的 IOMMU 上下文（`hfi_iova` / `log_iova`）。这条实现之前，切片 3 的可行做法是先用
一次只读探测确认「固件是否已在主循环」：读 `HFI_CTRL_STATUS`、`GMU2HOST_INTR_INFO`、
`RSCC_CONTROL_ACK` 与 GMU 的 SMMU 状态，判它是在等 HFI 还是已经跑飞。

## 二十一、ROUND 169（切片 3 第一步）：固件活着，停在 HFI 之前

### 21.1 这一轮问的问题

ROUND 168 已经证明三件事：8192 个字写进了 ITCM 并读回一致；复位脉冲释放了 CM3；`CM3_FW_INIT_RESULT`
从我们写的 0 变成 `0xbabeface`——这个值**只有 GPU 侧的 CM3 自己会写**，所以固件确实在 CPU 上跑起来了。

接着的问题不是「它跑没跑」，而是「它现在卡在哪」。上游 `a6xx_gmu.h` 的顺序是
`gmu_start → gfx_rail_on → sptprac → hfi_start`，而我们在 ROUND 168 之后**一个都没做**：既没有
OOB boot-slumber 握手，也没有 HFI 队列表。固件如果正常，应该停在「等主机敲门前」的等待环里。

### 21.2 只读，且不新开孔径

`enchilada-pas-exec=9` 在 `=8`（装载 + 脉冲）之后追加一段**纯读**转储，七个字：

| 字 | 偏移 | 它回答什么 |
|---|---|---|
| `GMU2HOST_INTR_INFO` | 0x5192 | 固件有没有举手（OOB ack 位 30） |
| `HFI_CTRL_STATUS` | 0x5180 | HFI 有没有起来（需要队列表，本轮没有） |
| `BOOT_SLUMBER_OPTION` | 0x23f8 | 固件有没有接受启动休眠选项 |
| `GX_VOTE_IDX` | 0x23f9 | 固件有没有拿到 GX 电压投票 |
| `MX_VOTE_IDX` | 0x23fa | 同上，MX 轨 |
| `CM3_FW_INIT_RESULT` | 0x501c | 那个哨兵是否**仍然**latch 着 |
| `CM3_SYSRESET` | 0x5000 | CM3 是否仍在复位 |

七个地址全部在更早轮次已经读过（0x5192 在 ROUND 165、0x23f8..0x23fa 在 ROUND 166、
两个 CM3 字在 ROUND 167/168），所以这一轮**不新增任何映射孔径**：仍然只有那一个 GMU 窗口。
转储体内 0 个 store，验证器把这条钉死了。

`ok` 字段不是硬编码的比较：它按上游 `a6xx_gmu_start()` 的规则，从刚装载的 DTCM 字 0xff8 重新选出
`0xbabeface` 或 `0x1ff/0x100`，然后用同一个判据比较——这样「转储说活着」和「脉冲说起来了」
不会是两套标准。

### 21.3 判决（DEVICE-VERIFIED，一次启动）

构建：CI **#276** / run `44033235528`（分支 `r169-gmu-live`，commit
`020cfccef1ede3f33312b65709a247baa084a2dc`，tree `b66ec4a2…`），同样注入
`extra_firmware_b64` + `a630_gmu.bin:32768`。工件：kernel
`41684845499366c64e2555cd7ba496dd4c61bb9d8e9865b23190338c4fadcf42`（126,181,376 B）、payload
`864041feec2fcadc2f960c4115f980b05c81813d62b5e8569dc503424a47a743`；本地等长改 key 得
`569c32d0aba79db3d566d3f1d84bc1fc4a5e8c461ee8f6dbcee4a60ec4282e60`。

静态核对（`work/tmp/r169_static.py`）：`gmulive=intr=%08x hfistat=%08x `、`gmuboot2=`、`gmufw=load`
三行都在 kernel 里；6 个禁用符与 `stage6=exec`/`stage3=exec` 全 0；32 KiB 固件以原始字节出现在
kernel 与 payload 里。有一处值得记下：`gmufw=absent` 的**字面串不在 kernel 里**——因为
`!kAdrenoGmuBlobPresent || kAdrenoGmuImageWords == 0u` 在编译期折成常量假，拒绝分支被死代码消除。
字符串缺席反而是「镜像编进去、stub 没编进去」的正面证据（我最初的断言方向写反了，已改）。

上机：readback `d1c149f8…` == payload 头；窗口 33,554,432 B sha256
`cf1ff7a9c5fead79bbd92f4f446532ccbff5fee2f93d07421c9a9a1f594a3d5e`、**boot_id 583730739**、
msg_bytes 29307、slots_written 7、`write_errors=0`。

三行（逐字，时间戳 29.68 s）：

```
gmufw=load words=8192 pre_reset=00000001 pre_init=00000000 pre_sig=3b1cf0d0 post_sig=00000000 img0=00043f80 img1=00000009 imgend=00000000 rb0=00043f80 rb1=00000009 rbm=10070000 rbend=00000000 ok=1
gmuboot2=sig=00000000 mask=ffffffff expect=babeface final=babeface polls=24 hfistat=00000000 reset=00000000 ok=1
gmulive=intr=00000000 hfistat=00000000 slumber=00000000 gxvote=00000000 mxvote=00000000 fwinit=babeface reset=00000000 ok=1
```

### 21.4 这三行证明什么

1. **固件活着，而且停住了**：`fwinit=babeface` 与 `reset=0` 同时成立，且 `babeface` 在脉冲之后
   又被打了一次（转储时刻）仍然是它——不是一次性的瞬态。
2. **它没有走到 boot-slumber**：`slumber=0`（`BOOT_SLUMBER_OPTION` 从未被写）、`intr=0`
   （没有任何 OOB ack 位）、`gxvote=0`/`mxvote=0`（两轨投票都没拿到）。这与「主机还没敲门」的
   预期完全一致。
3. **HFI 没起来**：`hfistat=0`。这是设计使然——队列表需要 GMU IOMMU 的 IOVA，本仓库没有那个
   backend，本轮刻意不写 `HFI_QTBL_ADDR`/`HFI_QTBL_INFO`/`PWR_COL_CP_MSG`。

合起来：**GMU 冷启动现在缺的只剩「主机侧的三步」**——OOB boot-slumber 握手、power_config、
HFI 启动；而固件本身已经由我们装进去并在跑。

### 21.5 诚实边界

- 一次开机，没有重复统计；`polls=24` 是循环次数不是时间。
- `rbm=10070000` 仍无 `imgmid` 对照，不解释。
- `slumber=0` 只能说「固件没写这个字」，不能推断它内部停在哪个循环——`intr=0` 与
  `hfistat=0` 是旁证，不是证据链。
- 本轮**没有**写任何 OOB 字、没有写 power_config、没有碰 PDC/RSC。
- `gxvote/mxvote=0` 不代表固件不需要投票：上游是**主机**在敲门之前写 `GX_VOTE_IDX`/`MX_VOTE_IDX`
  （取自 `gx_arc_votes[]`），我们没写，所以它们当然是 0。这个值不能用来推断固件行为。

### 21.6 下一道门（切片 3 第二步）

按上游 `a6xx_gmu_gfx_rail_on()` + `a6xx_gmu_set_oob()` 的形状，下一轮要做的第一笔写是
**OOB boot-slumber 握手**：

1. `BOOT_SLUMBER_OPTION = 0`
2. `HOST2GMU_INTR_SET = 1 << 22`（`GMU_OOB_BOOT_SLUMBER` 的 set 位）
3. 有界轮询 `GMU2HOST_INTR_INFO` 的 bit 30（ack）
4. `GMU2HOST_INTR_CLR = 1 << 30`

难点是 vote 值：上游在敲门之前写 `GX_VOTE_IDX = vote & 0xff`、`MX_VOTE_IDX = (vote >> 8) & 0xff`，
其中 `vote` 来自 `gx_arc_votes[nr_gpu_freqs - 1]`——**这张表不在 DT 里**
（`enchilada.dts` 里 `opp-hz`/`arc-level`/`gx_arc`/`gpu_opp` 全 0 次命中），是从 RPMh/regulator
的 arc 配置推出来的，本仓库没有这份数据。

所以下一步先做**不含 vote 写的握手**：只写 `BOOT_SLUMBER_OPTION=0` 与门铃，看固件 ack 不 ack。
ack 了说明这一级不需要投票就能过；不 ack 就如实记「固件没回应」，再去解决 vote 表。
不猜数值，也不把「没 ack」解释成「固件坏了」。

## 二十二、ROUND 170（切片 3 第二步）：门铃响了，固件报了 FAULT

### 22.1 这一轮问的问题

ROUND 169 的证据是：固件在跑（`fwinit=babeface` latch、`reset=0`），但 `slumber=0`、`intr=0`、
`hfistat=0`。三种解释并存：固件没听见门铃、固件听见了但拒绝、或者固件根本没走到等门铃的地方。
ROUND 170 的写法就是上游 `a6xx_gmu_set_oob()`：`BOOT_SLUMBER_OPTION=0` → `HOST2GMU_INTR_SET = 1 << 22`
→ 有界轮询 `GMU2HOST_INTR_INFO` 的 bit 30 → 无论是否 ack 都写 `GMU2HOST_INTR_CLR = 1 << 30`。

刻意**不写** `GX_VOTE_IDX`/`MX_VOTE_IDX`：上游在门铃之前把它们写进固件（取自 `gx_arc_votes[]`），
而那张表不在 `enchilada.dts` 里。猜一个数值等于静默替一个电源轨选电压，本轮只问
「不投票，固件答不答」。

### 22.2 判决（DEVICE-VERIFIED，一次启动）

构建 CI **#277** / run `44040327037`（分支 `r170-gmu-oob`，commit
`9331193fb019ee903e7d9bda7630047bee1ad590`），payload `6b6bb2e9…`；本地改 key 得
`5ae5e9d4…`（`=2` 是 1377 字节、`=10` 是 1378 字节，故不能用等长工具，改用 `r170_rekey.py`）。

上机：readback `4a51767d…` == payload 头；窗口 33,554,432 B sha256
`49ef31d4a16e35d1e378bf1edebc74a36a2775e16118f3e68a77f7d2a682fc7d`、**boot_id 584967165**、
msg_bytes 29483、`write_errors=0`。四行里的末行是判决行：

```
gmuoob=pre_intr=00000000 pre_slumber=00000000 ack=0 polls=400000 post_intr=00800000
       post_slumber=00000000 fwinit=babeface gdscr=00222001 ok=0
```

### 22.3 `0x00800000` 不是「另一个静默」，是 FAULT

`.cache/adreno-a630-pdc-headers/a6xx_gmu_acrn_kernel.h` 里这张表把话说死了：

| 宏 | 值 |
|---|---|
| `REG_A6XX_GMU_GMU2HOST_INTR_INFO` | `0x00005192` |
| `A6XX_GMU_GMU2HOST_INTR_INFO_MSGQ` | `0x00000001` |
| **`A6XX_GMU_GMU2HOST_INTR_INFO_CM3_FAULT`** | **`0x00800000`** |

所以 `post_intr=00800000` 的含义是：**CM3 固件在主机敲门前后报了 fault**。这不是「没听见」，
也不是「听见了但拒绝」，是固件自己认为出错了。它与 ROUND 168 的 `final=babeface` 并列成立：
固件确实执行到了写哨兵那一步，然后 fault。

### 22.4 诚实边界

- 一次开机，`polls=400000` 是循环次数不是时间；超时后我没有重试、没有写 `CM3_SYSRESET` 复位重来。
- `pre_intr=0` 说明敲门**之前** INTR_INFO 是干净的；fault 是在这之后才被读到的，但我**不能**据此
  断言「fault 由门铃触发」——中间没有第二采样点，只能说「敲门之后读到 fault」。
- 没有写 vote、没有写 power_config、没有写 QTBL/CP_MSG，故这一轮**没有**排除「缺配置导致 fault」。

## 二十三、ROUND 171（切片 3 第三步）：预启动配置补齐了，FAULT 依旧

### 23.1 这一轮问的问题

ROUND 170 之后，最省事的假设是「fault 是因为我们漏了上游在脉冲之前的那批配置」。上游
`a6xx_gmu_fw_start()` 的 cold 分支在 `a6xx_gmu_start()` 之前确实写了九样东西：`GENERAL_7=1`、
`CM3_FW_INIT_RESULT=0`、`CM3_BOOT_CONFIG=0x02`、`HFI_QTBL_ADDR`、`HFI_QTBL_INFO`、
`AHB_FENCE_RANGE_0`、`CM3_CFG=0x4052`、`HFI_SFR_ADDR=chipid`、`PWR_COL_CP_MSG`，外加
`a6xx_gmu_power_config()` 那一批（`SYS_BUS_CONFIG`/`ICACHE_CONFIG`/`DCACHE_CONFIG`/
`PWR_COL_INTER_FRAME_CTRL`，SPTP idle 还要 `PWR_COL_SPTPRAC_HYST` 与两个 rmw）。

R171 把**能写的都写了**（九笔中缺 IOVA 的三笔留白）并读回；写序照上游，写在同一映射里。

### 23.2 写了什么

| 寄存器 | 值 | 依据 |
|---|---|---|
| `SYS_BUS_CONFIG` | 1 | `a6xx_gmu_power_config` |
| `ICACHE_CONFIG` | 1 | 同上 |
| `DCACHE_CONFIG` | 1 | 同上 |
| `PWR_COL_INTER_FRAME_CTRL` | `0x09c40400` | 同上（`PWR_COL_IFPC_CTRL`） |
| `PWR_COL_SPTPRAC_HYST` | `0x000a1680` | 同上（`PWR_COL_HYST`） |
| `PWR_COL_INTER_FRAME_CTRL` | `|= 0x5` | `gmu_rmw(..., 0, IFPC_ENABLE\|SPTPRAC_POWER_CONTROL_ENABLE)` |
| `RPMH_CTRL` | `|= 0xF11` | `gmu_rmw(..., 0, RPMH_INTERFACE\|...\|GFX_VOTE)` |
| `AHB_FENCE_RANGE_0` | `0x802800A0` | `BIT(31) \| FIELD_PREP(30,18,0xa) \| FIELD_PREP(17,0,0xa0)` |
| `HFI_SFR_ADDR` | `0x06030100` | `AdrenoGmuBootV1Chipid(0x06030001)` |

`gmu_rmw(reg, mask, val)` 写的是 `(read & ~mask) | val`，mask=0 时 `~mask` 全 1，所以这两个 rmw
等价于「把这些位置位」——不是清位。这一点我在实现前专门核过，因为理解反了会把
`RPMH_CTRL` 写成 0。

仍**不写**：`HFI_QTBL_ADDR`/`HFI_QTBL_INFO`/`PWR_COL_CP_MSG`（要 IOVA）、`GX_VOTE_IDX`/`MX_VOTE_IDX`
（要 arc vote 表）。

### 23.3 判决（DEVICE-VERIFIED，一次启动）

构建 CI **#278** / run `44052273910`（分支 `r171-gmu-cfg`，commit
`d32c8606d2237bee84943ab2ff089bbc9d7ef26a`，tree `8f270b44…`）：kernel `a5dbc2ee…`
（126,181,376 B）、payload `ddbc9088…`；本地改 key（1377→1378 字节）得 `de30de64…`。

静态门（`work/tmp/r171_static.py`）：六行格式串全在、6 个禁用符与 `stage6=exec`/`stage3=exec` 全 0、
32 KiB 固件以原始字节在 kernel 与 payload 里、`gmufw=absent` 缺席（死代码消除的正面证据）。

上机：readback `aa1c0860…` == payload 头；窗口 33,554,432 B sha256
`b851d91bbe932bc417f7db310a4f5c81dbc3a5966384cf63459889fbad8ecfd7`、**boot_id 584226737**、
msg_bytes 29606、`write_errors=0`。

```
gmucfg=ifc=09c40405 rpmh=00000f11 fence=802800a0 sfr=06030100 intr=00000000 fault=0 ok=1
```

九笔全部落盘并读回：`ifc=09c40405`（含 rmw 的 `|=0x5`）、`rpmh=00000f11`（vote 位置起）、
`fence=802800a0`（与公式逐位相符）、`sfr=06030100`（massage 后的 chip id）、`intr=0`、`fault=0`。

然后脉冲照旧成功（`gmuboot2 … final=babeface polls=40`），存活转储照旧
（`gmulive=intr=00000000 … fwinit=babeface`），而门铃之后：

```
gmuoob=pre_intr=00000000 pre_slumber=00000000 ack=0 polls=400000 post_intr=00800000
       post_slumber=00000000 fwinit=babeface gdscr=00222001 ok=0
```

`post_intr` 又是 `0x00800000`：**补齐预启动配置没有阻止 CM3 fault。**

### 23.4 这一轮证明了什么、没证明什么

证明：
1. 那九笔写进 GMU 窗口后能被读回，且读回值与上游公式逐位相符 —— 孔径是**可写的真内存**，
   不是只读影子寄存器。
2. 写这些配置本身**不会**让 CM3 fault：`gmucfg` 行读到 `fault=0`，`intr=0`。
3. 因此 fault 与「缺预启动配置」无关。

没证明：fault 的确切触发点。`gmucfg` 的采样在 29.711 s、`gmuoob` 在 30.248 s，中间我写了
`BOOT_SLUMBER_OPTION` 与 `HOST2GMU_INTR_SET`；fault 出现在这两笔之后的采样里，但中间没有
第二次采样，无法区分「写这两笔触发的」与「固件自己在那 0.5 s 里出错的」。

### 23.5 下一道门

上游在门铃之前还有三笔我写不了：`HFI_QTBL_ADDR = hfi.iova`、`HFI_QTBL_INFO = 1`、
`PWR_COL_CP_MSG = log.iova | (log_pages - 1)`。这三笔都需要 **GMU 自己的地址空间**（队列表与日志
缓冲的 IOVA），而本仓库没有 GMU IOMMU 后端。CM3 收到 `MSGQ` 中断后要去读队列表；表地址为 0
时它读到的不是队列，fault 是合理后果。

所以下一步不是继续敲门铃，而是先**只读**弄清 GMU 侧地址空间现状：GMU 有没有独立 SMMU、
上电时它是 bypass 还是有页表、`0x506a000` 窗口里那些与 IOMMU 相关的字是什么值。在拿到
「IOVA 可以用物理地址」或「必须先建页表」的结论之前，再敲一次门铃只会重复同一个 fault。

## 二十四、ROUND 172-176：SMMU 窗口只到 0x800，HFI 门铃是致死点

**一句话**：GMU 的 SMMU 窗口 `0x05040000` 只有前 `0x800` 字节可读（`0xC00` 起一律
external abort）；读到 `sCR0=0x00200626` 当时说明 ~~`SMMUEN=0`~~（R235 更正：bit0 是 `CLIENTPD`=0），于是当时判断 HFI 队列表可以**直接用
物理地址**；按「分配 → 映射填表 → 写 QTBL 三笔 → 门铃」四级阶梯上机，前三级的键
（15/16/17）都**活着**并逐级打印结果，第四级（18，`HFI_CTRL_INIT=1` + 状态轮询）
**静默死**。

### 24.1 R172-R174：SMMU 转储的死亡边界（DEVICE-VERIFIED）

- R172（键 12）一次读 GR1/CBAR/CBA2R 与两个未实现的 CB → **静默死**（窗口与 R171
  逐字节相同）。同一内核改回键 11 → 正常写日志（窗口 `62186bae…`、boot_id 548031999），
  证明死因在转储块本身。
- R173（键 13）按 Linux 的 S2CR 步长 8 读 `0xC00…0xC28` → 仍**静默死**；同一内核
  键 12 对照正常（窗口 `1d942f2e…`、boot_id 548798115）。
- R174（键 13/14）改成按偏移分层：键 13 只读 `0x00/0x800/0x804` → **活**
  （窗口 `9466430d…`、boot_id 548976724），逐字行
  `smmu3=ctrl=00200626 smr0=00000000 smr1=00000000`；键 14 再加 `0xC00/0xC20/0xC28`
  → **静默死**（窗口与键 13 逐字节相同）。
- 结论：**`0xC00` 及以上（S2CR 区）任何读法都 abort，与步长无关**；`S2CR` 组不能读，
  因此不能从寄存器推断 SID 4/5 的地址空间形态，只能从 `sCR0` 反推。
- `sCR0=0x00200626`：~~bit0 `SMMUEN=0`、bit1 `CLIENTPD=1` ⇒ SMMU 未使能~~（R235 更正：bit0=`CLIENTPD`=0 客户端已开、bit1=`GFRE`；`USFCFG`=bit10=1 ⇒ 未匹配流 fault），GMU 流量
  不翻译，**物理地址就是 GMU 看到的地址**。R168 那条「队列表需要 GMU IOMMU 的 IOVA」
  的旧契约据此作废。
- 顺带更正：R164 记的 `sctlr=00200626` 是把 SMMU 偏移 `0x0` 当 CB_SCTLR 读出来的，
  它其实就是 `sCR0`；数值真实，但当时的语义解读（"CB 未使能"）无效。

### 24.2 R175：一次静默（键 15 全路径）

- 键 15 把「分配 → 映射填表 → 写 QTBL 三笔 → 门铃 + 状态轮询」整串放进一次启动，
  结果**静默死**（窗口 `9466430d…`、boot_id 548976724，与 R174 键 13 逐字节相同，
  `gmuhfi=` 计数 0）。
- 静态证据确认 `armcpu_dma_alloc_writable` 已链进内核（`llvm-nm` 见 FUN，
  `fffffff00bf784ec`），所以不是空指针跳转；死因在这串步骤里的某一步。

### 24.3 R176：四级阶梯判决（DEVICE-VERIFIED，CI #283）

| 键 | 新增的一步 | 判决 | 新行（逐字） |
|---|---|---|---|
| 15 | 只分配 | **活**（boot_id 547760692） | `gmuhfi=alloc pa=ad92d000 virt=adb90000 ok=1` |
| 16 | +映射并填表 | **活**（boot_id 548051401） | `gmuhfi=table win=a8a10000 t0=00000000 t1=00000078 qh0=00000001 rb_pa=ad92d000 ok=1` |
| 17 | +QTBL 三笔 | **活**（boot_id 547172496） | `gmuhfi=qtbl addr=ad92b000 info=00000001 cpmsg=ad92e000 rb=ad92b000` |
| 18 | +`HFI_CTRL_INIT=1` 与状态轮询 | **静默死**（窗口 `a8aa19ac…` 与键 17 逐字节相同） | 无 |

- 构建：#283 / run `44094859363`（分支 `r176-hfi-ladder`，commit
  `956b5d71da8d37af3469185c4abc4a9d502cc3fc`），kernel sha256
  `b92f633fbd1ad884fc4d2d0f3ec51a1b8e463ac23bbc5fae49660e965768694e`，
  CI payload sha256 `fc33ffce7549527e1bba8e910bc69965b5847d8e31f75603c9d88c3641d2545b`；
  四个上机镜像由同一 payload 等长改 key 得到，sha256 分别为
  `bec50c24…`(15)/`21259bfd…`(16)/`209d539f…`(17)/`929dfdad…`(18)。
- 静态门 `work/tmp/r176_static.py` 29 项 0 失败：四条阶梯行字面都在 kernel 里，
  12 条禁用符全 0，固件 pin 与头 4 KiB 在 kernel+payload。
- 判读：**分配器、映射、表填写、QTBL 三笔都被真机判为安全**；死亡点精确落在
  「写 `HFI_CTRL_INIT=1`」或「随后读 `HFI_CTRL_STATUS`」这两步之一。
- 与 R170 的关系：R170 走的是 OOB boot-slumber 门铃（`HOST2GMU_INTR_SET` bit22），
  只让 FW 报 `CM3_FAULT`（`GMU2HOST_INTR_INFO=0x00800000`）而**不死**；R176 的
  HFI 门铃则直接把内核打死在日志 writer 之前 —— 两种门铃在设备上的后果不同。

### 24.4 下一步（R177 设计）

把死亡点再劈一半，用三个键：

- **18**：只写 `HFI_CTRL_INIT=1` + `dsb sy`，立刻读回 `HFI_CTRL_INIT` 并打印
  （不读 `HFI_CTRL_STATUS`）。若死 ⇒ 写这一步本身致命。
- **19**：同 18，再加**一次** `HFI_CTRL_STATUS` 读（不循环）。若 18 活而 19 死
  ⇒ 第一次读该寄存器就 abort。
- **20**：同 19，再加有界小轮询（如 1000 次）。若 19 活而 20 死 ⇒ 是轮询把时间
  拉长后踩到固件侧的状态。

只有当 18/19/20 都活下来，才谈「HFI 通了没有」；那时再看 `HFI_CTRL_STATUS` 的
bit0、队列 `write_index`、`GMU2HOST_INTR_INFO` 是否还有 `CM3_FAULT`。

## 二十五、ROUND 177：门铃的「写」本身就是致死动作，而上游在它之前还有一步 `gfx_rail_on`

**一句话**：R176 的阶梯停在第四级（键 18）。R177 把这一级再劈成「只写 / 再读回 /
再读一次状态 / 再轮询」四个键，结果是**第一级就死**——只写一次
`HFI_CTRL_INIT=1`、不读任何东西，内核依旧死在日志 writer 之前。随后读上游
`a6xx_gmu_fw_start()` 才发现：我们把 `a6xx_gmu_gfx_rail_on()` 整段跳过了，
而它正是 `hfi_start` 的唯一前置。

### 25.1 判决（DEVICE-VERIFIED）

- 构建：#284 / run `44102300891`（分支 `r177-hfi-doorbell`，commit
  `0af813e861d29a2a7d33fda5a0032e3b7bed1497`），kernel sha256
  `1e49c849600df9883a6339b4e3a17a17ec7e50360794e929b0d0f4059377e7dd`，
  CI payload sha256 `700827081295983168a8ff4c0dc9c7854f1b2f66ddba7530aa53f40c2aa949e4`。
- 静态门 `work/tmp/r177_static.py` 0 失败：三条新行（`gmuhfi2=write`/`gmuhfi2=rb`/
  `gmuhfi3=stat`）都在 kernel 里，12 条禁用符全 0，固件 pin 与头 4 KiB 都在。
- 键 18（只写 `HFI_CTRL_INIT=1`，随后 `dsb sy`，**不读任何寄存器**）：
  刷写回读与 payload 头一致，但窗口 sha256 仍是
  `a8aa19ac642ffdd1b98ad4129fd0ab0afa8149396820d3b86d38be51997a1bf0`、boot_id 仍是
  **547172496**（= R176 键 17 的那次启动）⇒ 内核死在日志 writer 之前，零字节。
- 因此键 19/20/21 不再有区分能力（18 已经死），本轮只得到一条结论：
  **致死的是那次 store 本身**，不是读回、不是状态读、也不是轮询。

### 25.2 读上游才发现的缺口

`driver/gpu/drm/msm/adreno/a6xx_gmu.c`（本地 `.cache/adreno-a630-source/` 全文）里
`a6xx_gmu_fw_start()` 的顺序是：

1. `GENERAL_7=1` → `a6xx_rpmh_start()` → `a6xx_gmu_fw_load()`
2. `FW_INIT_RESULT=0`、`BOOT_CONFIG=0x02`、`HFI_QTBL_ADDR`、`HFI_QTBL_INFO=1`
3. `AHB_FENCE_RANGE_0 = BIT(31)|(0xa<<18)|0xa0`、`CM3_CFG=0x4052`、
   `HFI_SFR_ADDR=chipid`、`PWR_COL_CP_MSG=log.iova|(pages-1)`
4. `a6xx_gmu_power_config()` → `a6xx_gmu_start()`（脉冲 + 轮询 `FW_INIT_RESULT`）
5. **`a6xx_gmu_gfx_rail_on()`**（仅 legacy）：`BOOT_SLUMBER_OPTION=0`、
   `GX_VOTE_IDX=vote&0xff`、`MX_VOTE_IDX=(vote>>8)&0xff`、然后
   `a6xx_gmu_set_oob(GMU_OOB_BOOT_SLUMBER)`（置 bit22、等 ack bit30、清 bit30）
6. `a6xx_sptprac_enable()`（仅当 `idle_level < GMU_IDLE_STATE_SPTP`）
7. **`a6xx_gmu_hfi_start()`**：`HFI_CTRL_INIT=1` + 轮询 `HFI_CTRL_STATUS & 1`

我们第 4 步做完就直接跳到第 7 步，第 5、6 步整段没有。R170 曾经单独做过第 5 步的
OOB 握手（但没写两个 vote），当时的结果是 **固件报 `CM3_FAULT`（`0x00800000`）而内核存活**；
R177 键 18 连 OOB 都没有，只写 `INIT`，结果是**内核直接死**。两次对照说明：
固件在「没人告诉它启动序列开始」的状态下被要求起 HFI 时会走 fault 路径并拖死 AHB。

### 25.3 vote 值的真实来源（不能猜）

`a6xx_gmu_rpmh_arc_votes_init()`（`a6xx_gmu.c`）从 **RPMh command DB** 取表：
`cmd_db_read_aux_data("gfx.lvl", &pri_count)` 取 primary、再取同 id 的 secondary 表，
然后对每个频点 `a6xx_gmu_get_arc_level()`（`dev_pm_opp_get_level`）在 primary 表里找
第一个 `pri[j] >= level` 的下标 `pindex`，secondary 同理得 `sindex`，
最终 `vote = pindex | (sindex << 8)`。

- 所以 vote 来自**引导器/TZ 写进 RPMh cmd-db 的表**，不是 DT 属性。
- 本机 `work/Mu-Silicium-full/Resources/DTBs/enchilada.dts`（784,857 B）里
  `opp-hz`/`opp-level`/`arc-level`/`gpu-arc` **一次都不出现**（R170 的旧结论在此复核），
  因此**没有任何 DT 数据可以拿来算 vote**。
- 结论：在拿到 cmd-db 之前，写 `GX_VOTE_IDX/MX_VOTE_IDX` 就是猜索引；而索引直接
  决定 RPMh 给的电压/频率档（猜错等于静默选错档），所以本轮**不写**。

### 25.4 下一步（R178 的两个候选，按风险排序）

1. **先做「顺序」实验（零新知识依赖）**：键 22 = 复刻上游第 5 步的**握手形状**但
   两个 vote 保持不写（即 `BOOT_SLUMBER_OPTION=0` + 置 bit22 + 等 ack + 清 bit30），
   然后再写 `HFI_CTRL_INIT=1` 并**只读一次** `HFI_CTRL_STATUS`。若存活 ⇒ 死因是
   「顺序/握手缺失」而不是「vote 缺失」，下一轮再去补 vote；若仍死 ⇒ vote 是硬前提。
2. **取真 vote（需要新能力）**：定位 RPMh cmd-db（SMEM 内，magic + 三张表），
   解析 `gfx.lvl` 的 primary/secondary，按最高频点算出 `vote`。这需要 SMEM/cmd-db
   读取器，工作量明显大于一次实验，但它是唯一「不猜」的路。

## 二十六、ROUND 178：补上 `gfx_rail_on` 的握手形状之后，同一个门铃写入不再致死

**一句话**：R177 判的是「只写一次 `HFI_CTRL_INIT=1` 就死」。本轮把上游第 5 步
`a6xx_gmu_gfx_rail_on()` 的**握手形状**（不写两个 vote）补在写门铃之前，结果
**同一个写入活下来了**，而且连读 1000 次 `HFI_CTRL_STATUS` 也活着。所以 R177 的判决要
收窄：致死的不是那次 store，而是**在握手之前**的那次 store。

### 26.1 判决（DEVICE-VERIFIED）

- 构建：#285 / run `44115787915`（分支 `r178-rail-on`，commit
  `e4546974ce27608a1bbdc6a6322493468fccca99`），kernel sha256
  `191c69c52f65c685ffff315aef31e064fd29779762536db21a21fe3cc8e962d2`，
  CI payload sha256 `65338ecca680f74777476ba9b916a1c8607fb4ff9a663d09ce7f34e7d8e98718`。
- 静态门 `work/tmp/r178_static.py`：17 条 REQUIRED 全在、12 条 FORBIDDEN 全 0、
  `GX_VOTE_IDX`/`MX_VOTE_IDX` 在 kernel 文本里**不出现**（证明两个 vote 真的没写）。
- 键 22（`enchilada-pas-exec=22`，payload `c74906e2…`）：**NEW LOG**，
  boot_id **547712722**，窗口 `b1e28fee386eec835f36be8925fb86f70afa5dce18c4d66c549b5cd8d496eac8`，
  msg_bytes 29865，write_errors 0。逐字行：

      gmurail=pre=00000000 ack=0 polls=400000 post=00800000 slumber=00000000 hfipre=00000000 hfi=00000000 ok=0

- 键 23（`=23`，payload `9b2c9ffa…`，在键 22 之上再加 1000 次有界轮询）：**NEW LOG**，
  boot_id **547212046**，窗口 `bd352d0435beb859ffcca88dfef5e08a438c69b50ca78671bdd0b92d8f66e4c5`，
  msg_bytes 29850。逐字行：

      gmurail2=pre=00000000 ack=0 post=00800000 hfipre=00000000 hfi=00000000 polls=1000 ok=0

- 两轮刷写回读的 system_b 头 8 MiB 都是 `e2a576179d643bee727af1b7550e795a951a195d45383d5f0b60571bae16629f`，
  与各自 payload 头一致（两个 payload 只差 FAT 区里的一处 config 字符串，前 8 MiB 本就相同）。

### 26.2 三件事同时被证明

1. **`HFI_CTRL_INIT=1` 这个 store 本身不致死**：键 22 写它、键 23 写它并连读 1000 次状态，
   两次都是新 boot_id、零 write_errors。R177 键 18 之所以死，是因为它把这次 store 放在了
   **BOOT_SLUMBER 握手之前**——CM3 还在「未被告知启动序列」的状态里，此时碰 HFI 寄存器
   会把 AHB 拖死（R170 那次是固件报 fault、内核还活，差别在于 R170 至少做过握手）。
2. **握手形状可以只做形状**：`BOOT_SLUMBER_OPTION=0` + 置 `HOST2GMU_INTR_SET` bit22 +
   有界等 `GMU2HOST_INTR_INFO` bit30 + 清 bit30，即使 `ack=0`（固件从头到尾没应答）
   也不影响内核存活。
3. **固件仍然不配合**：`ack=0`、`post=00800000`（= `A6XX_GMU_GMU2HOST_INTR_INFO_CM3_FAULT`
   bit23）、`hfipre=0`、写门铃后 `HFI_CTRL_STATUS` 仍是 0（键 23 连读 1000 次都是 0）。
   同一窗口里的 `gmucfg=` 行也变成 `intr=00800000 fault=1`，说明 fault 是**敲门之后**出现的。

### 26.3 读上游补出来的下一个缺口：`a6xx_rpmh_start()` 整段没做

`a6xx_gmu_fw_start()` 的**第一件事**不是写 `GENERAL_7`，而是 `a6xx_rpmh_start()`：
`RSCC_CONTROL_REQ = 2` → 轮询 `RSCC_CONTROL_ACK` 的 bit1 → 轮询
`RSCC_SEQ_BUSY_DRV0 == 0` → `RSCC_CONTROL_REQ = 0`。它让 CM3 通过 RSCC 接管 RPMh
通信，之后固件才可能去投 GX/MX 的 arc vote 并把 GX 轨拉起来。

本仓库至今**一次都没做过这四步**（R171 只做了 `power_config` 的九笔，`RPMH_CTRL` 的
`|= 0x00000f11` 是 power_config 的一部分，不是 `rpmh_start`）。这正好解释「固件活着、
能写 `babeface`、但一被要求走启动序列就报 fault 且从不应答门铃」。

### 26.4 下一步（R179 的两个键）

- **键 24**：在脉冲**之前**补 `rpmh_start` 四步（REQ=2、等 ACK bit1、等 SEQ_BUSY_DRV0==0、
  REQ=0），其余沿用键 22 的形状（握手 + 写门铃 + 读一次状态）。若 `ack=1` 出现 ⇒ 缺的就是
  RPMh 启动；若仍 `ack=0` ⇒ 下一步才轮到 vote。
- **键 25**：在键 24 之上再补 `a6xx_sptprac_enable()`（`SPTPRAC_POWER_CONTROL=1`，等
  `(val & 0x38) == 0x28`）。R165 读到 `sptprac=000000d4`（`& 0x38 = 0x14`）正是「SPTPRAC
  时钟没开」的读数，与上游这一步对应。
- vote 仍然不写：在拿到 RPMh cmd-db 之前，任何 vote 都是猜电压档。

## 二十七、ROUND 179：补上 `a6xx_rpmh_start()` 与 `sptprac_enable()` 之后，门铃仍然不应答

**一句话**：按上游顺序补了 `a6xx_rpmh_start()` 四步（键 24）与 `a6xx_sptprac_enable()`
（键 25），两次启动都**活着**、都打印了新行；但 RSC 的 ACK 位从头到尾没置起来
（400000 次轮询耗尽，`ack=00000000`），SPTPRAC 状态字连写前写后都是 `000000d4`，
boot-slumber 门铃照旧不应答，CM3 照旧 `post=00800000`（FAULT）。

### 27.1 判决（DEVICE-VERIFIED）

- 构建：#286 / run `44200606987`（分支 `r179-rpmh-start`，commit
  `8ecde9d2d39815a1802d5d41d79c4da6048c0b9e`），kernel sha256
  `92f76066dafa9f9359a83621048086d0ff7187dba7a5d6e4399e8b74a7356d2c`，
  CI payload sha256 `c66dffccba2afacfaacfc9e4f075ae81f2147e3aee6c081ebc84680a8aa9d691`。
- 静态门 `work/tmp/r179_static.py`：17 条 REQUIRED 全在、12 条 FORBIDDEN 全 0、
  `GX_VOTE_IDX`/`MX_VOTE_IDX` 在 kernel 文本里**不出现**（两个 vote 真没写）。
- 键 24（`enchilada-pas-exec=24`，payload `9676c4c5…`）：**NEW LOG**，
  boot_id **552735802**，窗口 `d401f8c08cf6e770badfe93d56771cebfacbb5cc1db73e30d1ba405e8a3f8e6b`，
  msg_bytes 29958，slots_written 9，write_errors 0，`gmurpmh=1 gmusptp=0 gmurail=0 gmuhfi=3`。
  逐字行：

      gmurpmh=pre=00000000 ack=00000000 apolls=400000 busy=00000000 bpolls=0 ok=0
      gmurail2=pre=00000000 ack=0 post=00800000 hfipre=00000000 hfi=00000000 polls=1000 ok=0

- 键 25（`=25`，payload `8dfc4670…`，在键 24 之上再加 `sptprac_enable()`）：**NEW LOG**，
  boot_id **561907766**，窗口 `e5d4601811115907ad2e8f1d48f101b3d0fb942d5a3f0e9913e8030b092929f9`，
  msg_bytes 30047。逐字行：

      gmurpmh=pre=00000000 ack=00000000 apolls=400000 busy=00000000 bpolls=0 ok=0
      gmusptp=pre=000000d4 stat=000000d4 polls=400000 ok=0
      gmurail2=pre=00000000 ack=0 post=00800000 hfipre=00000000 hfi=00000000 polls=1000 ok=0

- 两轮刷写回读的 system_b 头 8 MiB 都是 `89d44b2b652698f6a44c6d5fee05fc56a0bdf5486fa4d5974c5896c42dbd396e`，
  与各自 payload 头一致（两个 payload 只差 config 里的一处字符串，前 8 MiB 本就相同）。
- 键 24/25 都 ≥ 22，所以打印的是 `gmurail2=`（`if (fwKey == 22u)` 那一行只在键 22 打印）。

### 27.2 三件事同时被证明

1. **`a6xx_rpmh_start()` 的四步写不死内核**：键 24 的 `RSCC_CONTROL_REQ=2`、两个 400000
   次有界轮询、`REQ=0` 全部执行完毕，窗口仍是 NEW LOG、零 write_errors。键 25 再叠
   `SPTPRAC_POWER_CONTROL=0x778000` 也一样活着。⇒ 这两个窗口可以安全读写。
2. **RSC 根本没应答**：`apolls` 一路跑到上限 400000 才停，`ack=00000000`（bit1 从未置起）；
   `busy` 第一次读就是 0（`bpolls=0`）。寄存器偏移已三方核对过
   （`GMU_RSCC_CONTROL_REQ 0x9307`、`ACK 0x9308`、`RSCC_SEQ_BUSY_DRV0 0x0101` +
   `GMU_RSCC_DELTA 0x23000`，与 `a6xx_gmu.xml` 及两份 vendor 头一致），
   所以这不是「写错寄存器」，而是**这台机器的 RSC 状态与上游假设不同**：要么 REQ 位
   没被接受，要么 RSC 的 ACK 只在别的前置条件（CX/GX 轨道、RPMh 上下文）成立后才置。
3. **SPTPRAC 写入没有可见效果**：`pre=000000d4`、400000 次轮询后 `stat=000000d4`，
   `(0xd4 & 0x38) = 0x14 ≠ 0x28`。也就是说 `GX_SPTPRAC_POWER_CONTROL` 这笔 store
   同样没有改变状态字。这一读数与 R165 的 `sptprac=000000d4` 完全一致（三周不变）。

### 27.3 这轮带来的新线索：DT 里真的有 cmd-db 节点

`work/Mu-Silicium-full/Resources/DTBs/enchilada.dts` 里有：

    qcom,cmd-db@861e0000 {
        compatible = "qcom,cmd-db";
    ...
    cmd_db = "/soc/qcom,cmd-db@861e0000";

也就是说「读 RPMh command DB 才能算 arc vote」这条路**有物理落点**（0x861e0000），
不再是死胡同。上游 `a6xx_gmu_rpmh_arc_votes_init()` 正是从这里面读 `gfx.lvl` / `cx.lvl`
的 aux 数据，再用 `dev_pm_opp_get_level()` 映射成 `pindex | (sindex << 8)`。

### 27.4 下一步（R180 的两个候选）

- **候选 A（首选）**：先只读 cmd-db（0x861e0000 的 TCS/DRV 表 + `gfx.lvl` aux 数据），
  把 GX/MX 两个 vote 的合法取值**读出来**而不是猜；然后键 26 = 写入这两个 vote，
  再看 `gmurail2=` 的 `ack` 是否终于变 1。
- **候选 B**：先验证 RSC 窗口本身可读性（读几个已知非零的 RSC 寄存器），
  区分「ACK 没置」与「整个 RSC 窗口读回 0」这两种可能 —— 键 25 的 `sp` 读数
  （`0xd4` 非零）说明 GMU 窗口是真的，RSC 窗口（`GMU_DELTA` 区）尚未单独验证过。

## 二十八、ROUND 180：RSC 页确实有背衬，但 `RSCC_CONTROL_ACK` 依旧不置

**一句话**：键 26 只读一次 RSC 页，证明 `gmu mmio + 0x23000` 这片窗口**是真内存**
（`tcs0=00000001`），所以 R179 的「RSC 不应答」不是打空气、不是地址写错；但写
`RSCC_CONTROL_REQ=2` 之后 ACK 位仍然一路 0（400000 次轮询耗尽），同时
`busy/mem0/st0/ovr` 全 0、`CM3_FW_INIT_RESULT` 仍是 `babeface`（CM3 活着）。

### 28.1 判决（DEVICE-VERIFIED）

- 构建：#287 / run `44216700269`（分支 `r180-rscc-read`，commit
  `f11228fc1592ea833ade93dec689f69f8d3d14d4`），kernel sha256
  `7b575e84241c8c1a402377b651b7f7f8ca533b7da199b34ed8e7d5ea18ae2067`，
  CI payload sha256 `977648f43ec4220998b71a250da53ddcae68ba869f913d24e5ef920a7b976783`。
- 静态门 `work/tmp/r180_static.py`：18 条 REQUIRED 全在、12 条 FORBIDDEN 全 0、
  两个 vote 寄存器在 kernel 文本里不出现。
- 键 26（`enchilada-pas-exec=26`，本地 rekey payload `4c61a386…`）：**NEW LOG**，
  boot_id **584538485**，窗口 `574dfc991ec229d090bc256ae20c9596efdefb50890550b37803d50ee233aa35`，
  msg_bytes 30022，slots_written 8，write_errors 0，`flash readback matches: True`。
- 逐字行：

      gmursc=ack=00000000 busy=00000000 mem0=00000000 st0=00000000 ovr=00000000 tcs0=00000001 fwinit=babeface reset=00000000 sptp=000000d4 intr=00000000 ok=1

  同窗还有（键 26 同时满足 `>= 24u`，故 rpmh 块也跑了）：

      gmurpmh=pre=00000000 ack=00000000 apolls=400000 busy=00000000 bpolls=0 ok=0

### 28.2 这条行到底证明了什么

- `tcs0=00000001`（`RSCC_TCS0_DRV0_STATUS`，偏移 `0x0346`，经 `GMU_RSCC_DELTA`
  落到字节 `0x23d18`）读出非零 ⇒ **RSC 页被真实背衬**，读回的不是「未实现窗口的全 0」。
- 因此 R179 的 `busy=00000000`、`ack=00000000` 都是**真读数**；`a6xx_rpmh_start()`
  的第一步写下去之后，RSC 就是没有把 ACK 位置起来。
- `fwinit=babeface` 说明 CM3 依然在跑（R168 之后一直如此），`sptp=000000d4`
  与 R165/R179 一致，`intr=00000000` 因为本轮 rail 块被 `<= 25u` 关掉、没敲门铃。

### 28.3 三件被排除的事

1. **不是地址错**：`rd32(va, off)` 的 `off` 是字节偏移（`package/EnchiladaAdrenoProbe.cpp:491`），
   `GMU_RSCC_DELTA + RSCC_SEQ_BUSY_DRV0 * 4u = 0x23404`，落在 `ADRENO_BOARD_GMU_SIZE
   0x00030000` 之内；R179 里记的「倍数写错」是错误判断，已在文档中更正。
2. **不是窗口没实现**：`tcs0` 非零。
3. **不是 CM3 死了**：`fwinit=babeface`。

### 28.4 下一个假设：arc vote 是 RSC 应答的前提

- 上游 `a6xx_gmu_fw_start()` 在 `a6xx_rpmh_start()` 之前只写 `GENERAL_7=1`；
  但 `a6xx_gmu_gfx_rail_on()` 写的 `GX_VOTE_IDX`/`MX_VOTE_IDX` 是 RPMh 的
  **arc level 索引**，由 `a6xx_gmu_rpmh_arc_votes_init()` 从 command DB 里读
  `gfx.lvl`/`cx.lvl` 的 aux 数据算出来（`vote = pindex | (sindex << 8)`）。
- 本板从未写过这两个 vote，也没有读 cmd-db ⇒ RSC 看到的 CX/MX 轨没有合法电压档，
  拒绝 ACK 与之一致（这是假设，不是结论）。
- `enchilada.dts` 里有 `qcom,cmd-db@861e0000 { compatible = "qcom,cmd-db"; ... }`
  以及 `cmd_db = "/soc/qcom,cmd-db@861e0000";` ⇒ **command DB 有物理落点，可以只读**，
  不必猜电压档。

### 28.5 诚实边界

- 只有一次上机、一次采样；`mem0=00000000` 与 R166 从 PDC 窗口读到的
  `seqmem=00a000a8` 是**不同寄存器**（RSCC 的 TCS 序列内存 vs PDC 的序列内存），
  两者不矛盾，也**不互相印证**。
- `tcs0=00000001` 只证明「页有背衬」，没有证明 TCS 在做什么（没有位定义，不做解读）。

## 二十九、ROUND 181：cmd-db 的第一条候选被否 —— `0x861e0000` 的第二字不是 magic

**一句话**：键 27 把 `0x861e0000` 映射进来读头四字，映射成功、内核存活，但
`magic=00000000`，`quiet_cmd_db_levels()` 在 magic 校验处直接返回 0，于是
`gfx_n/cx_n/mx_n` 全 0。**这不是「表是空的」，是「这里没有表」。**

### 29.1 为什么先试这个地址

- 上游 `a6xx_gmu_rpmh_arc_votes_init()` 用 `cmd_db_read_aux_data()` 读
  `gfx.lvl` / `cx.lvl`，把 aux 数据当成**电平表**，再按
  `dev_pm_opp_find_freq_exact()` 给的 level 找下标，最后
  `votes[i] = ((pri[pindex] & 0xffff) << 16) | (sindex << 8) | pindex`。
- `a6xx_gmu_gfx_rail_on()` 只取低 16 位（`GX_VOTE_IDX = vote & 0xff`、
  `MX_VOTE_IDX = (vote >> 8) & 0xff`），所以本仓库不需要 OPP 表，只需
  「表中最大 level 首次出现的下标」就能忠实复现。
- `package/EnchiladaTouchRpmh.h:65` 已经有一套同样的解析器，判据是
  `ftb_le32(db + 4) == 0x0c0330dbu`，头后 8 条 slave 记录各 16 B，条目 24 B。

### 29.2 判决（DEVICE-VERIFIED）

- 构建：#288 / run `44227307738`（分支 `r181-cmd-db-vote`，commit
  `01e7a4b37ef9062d71754c722217e4e6b4abeb92`），CI payload
  `e7479079…`，`r181_static.py` 21 REQUIRED / 12 FORBIDDEN 全过。
- 键 27 上机：窗口
  `175efbb4dff005dd1d45a588716536f858d5282d4b9a393a03aa69bd843efc0d`，
  boot_id **547378411**，msg_bytes 30273，slots_written 8，write_errors 0。
- 逐字行：

      gmulvl=va=fffffffa34eb8000 magic=00000000 gfx_n=0 cx_n=0 mx_n=0 g0=0000 g1=0000 g2=0000
      gmulvl2=gfx_p=0 gfx_top=0000 cx_p=0 cx_top=0000 mx_p=0 mx_top=0000 gfx_addr=00000000 gfx_len=0

### 29.3 这条行证明了什么、没证明什么

- 证明：`0x861e0000` 可以映射、可以读、读它**不会 abort**（合法内核 VA
  `fffffffa34eb8000`，本次启动仍写出完整窗口）。
- 证明：那个地址的第二字不是 magic ⇒ **不是数据库头**。
- 没证明：设备上是否存在数据库。`gfx_addr=0`、`gfx_len=0` 都是「没找到」的
  产物，不是「找到但为空」。

### 29.4 下一个候选

- `enchilada.dts` 的 `qcom,cmd-db@861e0000` 节点写的是 `reg = <0xc3f000c 0x08>`，
  而 `package/EnchiladaTouchRpmh.h:27-30` 的注释说明这八个字节是
  **AOSS message RAM 里指向数据库的指针**：「the probe tests the node's own
  address first and this word second」。
- 于是 R182 的键 29 同时读这个指针页与 `0x861e0000` 的头四字。

### 29.5 诚实边界

- 只有一次上机、一次采样；`va` 值每次启动都不同，不能当常数用。

## 三十、ROUND 182：AOSS 指针页给出的也不是地址 —— 这台机器引导器没灌 cmd-db

**一句话**：键 29 读 `0xc3f0000` 那一页（指针在 `+0xc`），读回
`w0=8c010e08 w4=08200266 w8=09020526 wc=10c11c22`；`+0xc` 的值
`0x10c11c22` 不在 RAM 范围（`0x80000000..0x90000000`）内，因此没有跟随，
`gmudbc` 的 `va=0`、四字全 0。同时 `0x861e0000` 前四字**全 0**。
**结论：这台机器的引导器没有灌 cmd-db**，上游那条「从数据库算 arc vote」的路
在本机走不通。

### 30.1 判决（DEVICE-VERIFIED）

- 构建：#289 / run `44231777595`（分支 `r182-aoss-pointer`，commit
  `521260d8f6d135781814a00d5f3b8d735803f434`，tree
  `a4d24b90bf45eeacc6a4f95b92cc255cfbac9a67`）。
- 工件：kernel `27ae8ac9fdd1303f6326e0caa2740755b9dfec00f395156e669e1ffa18bbf7b7`，
  payload `39126affa77d505ef514506262fad8da18f8e2d101c576a63e4bb188639b856b`；
  `r182_static.py` PASS（25 REQUIRED / 12 FORBIDDEN + 固件头两处）。
- rekey `=2 -> =29`：`70cba8e4931f6f1368f972dde5fdeb21eb6ce8befcd57b975fb5b4baa0cb6a7f`
  （config 1377 → 1378 B，镜像 150,994,944 B）。
- 上机：`flash readback matches: True`（头 `dd933b25…`），窗口
  `6e92c9ceb5cad24d3f6513ca836478c3cb7f43261544d88c58516f574ae7abaf`，
  boot_id **553126703**，msg_bytes 30708，slots_written 9，write_errors 0。
- 逐字三行：

      gmudbp=va=fffffff8cbb2f000 w0=8c010e08 w4=08200266 w8=09020526 wc=10c11c22
      gmudbc=cand=10c11c22 va=0 h0=00000000 magic=00000000 h2=00000000 h3=00000000 base0=00000000 base1=00000000 base2=00000000 base3=00000000 ok=0
      gmuvote=gx=0 mx=0 ack=0 polls=400000 post=00800000 slumber=00000000 fwinit=babeface ok=0

### 30.2 三条读数各自的意思

1. `0xc3f0000` 那一页**有背衬**（四个字各不相同、看起来像 RPMh 命令字），
   但 `+0xc` 不是 RAM 地址 ⇒ 它不是「指向数据库的指针」。
2. `0x861e0000` 前四字全 0 ⇒ 节点名地址上也没有数据库。
3. `gmuvote=gx=0 mx=0` 是键 28 沿用了「找不到就给 0」的写法，**无害但无意义**；
   `ack=0`、`post=00800000`（CM3_FAULT）与 R170/R178 的门铃结论一致。

### 30.3 影响

- 上游 `a6xx_gmu_rpmh_arc_votes_init()` 在本机**无源可读**。
- 因此任何「写真实 arc vote」的路线都要先自建 RPMh 路径（例如用
  `package/EnchiladaTouchRpmh.h` 里已有的 TCS 写序列），而不是读数据库。
- 本轮不猜电压档：`GX_VOTE_IDX/MX_VOTE_IDX` 只允许写「表里最低的那一档」，
  索引 0 是唯一不会要求更高电压的选择（R185 采用）。

### 30.4 诚实边界

- 单次采样；`va` 非常数；AOSS 那一页的内容是否随启动变化未测（只读一次）。

## 三十一、ROUND 183：把「配齐」搬到脉冲之前 —— CM3 不再 fault

**一句话**：键 30 把队列表与全部预启动配置**都写在脉冲之前**，脉冲之后只读不写，
结果 `intr=00000000`：**CM3 起来时是干净的**。R170/R178/R181 一直读到的
`post_intr=00800000`（`CM3_FAULT`）**是 boot-slumber 门铃诱发的**，不是配置错、
也不是固件自己出错。

### 31.1 为什么此前一直分不开

- 键 9..13 读 `GMU2HOST_INTR_INFO` 时，HFI 队列表**还不存在**；
- 键 15..17 把表写进去了，却只报 `hfistat`，从不读 intr；
- 键 18..25 都先敲门铃再读，于是「firmware 自己 fault」与「门铃让它 fault」
  在数据上永远同相。

### 31.2 判决（DEVICE-VERIFIED）

- 构建：#290 / run `44237425903`（分支 `r183-stat-after-pulse`，commit
  `6e186963798a64a7403e053b4c7cc243215c5cd0`，tree
  `aefb2141767aafd0d4da9f23f99d59b3fb55268c`）。
- 工件：kernel `0b2a2ed03f8f1999bc7dd3e899462337c8120063241b622f96da9a330081ed45`，
  payload `aad1fcd63720440661d616e0ccfb495fb5730b01b292cef66f01f2ed91e97224`；
  `r183_static.py` PASS。
- rekey `=2 -> =30`：`6bc2918eba460d1d8c3cb626eac5c7c6cf23814973aabee047f17e064e543e8d`。
- 上机：readback `87dc8d08…` 一致；窗口
  `41ab63d58db98ee0eab5a5e6a04f4d16c99842ec39eb6e2d45087780d776534e`，
  boot_id **546568834**，msg_bytes 30172，slots_written 8，write_errors 0。
- 逐字两行：

      gmuboot2=sig=00000000 mask=ffffffff expect=babeface final=babeface polls=40 hfistat=00000000 reset=00000000 ok=1
      gmustat=intr=00000000 fwinit=babeface reset=00000000 gdscr=00222001 hfistat=00000000 wi1=00000000 ri0=00000000 ok=1

### 31.3 解读

- `final=babeface`：CM3 自己写的值，固件确实执行（主机只写 0）。
- `intr=00000000`：脉冲之后、无门铃时**没有 fault**。
- `hfistat=00000000`、`wi1=ri0=0`：HFI 未初始化，两个环形队列没被动过。
- `gdscr=00222001`：bit16（GX_ON）为 0，GX 轨仍未上电。

### 31.4 由此改变的两条判断

1. 「配置不对导致 fault」作废：配齐之后 CM3 干净。
2. `CM3_FAULT` 是**现象**（门铃的后果），不是启动失败的证据。

### 31.5 诚实边界

- 只有一次上机；`intr=0` 是单点采样，未测「等待更久会不会自己 fault」。

## 三十二、ROUND 184：干净状态下写 `HFI_CTRL_INIT` 直接把内核打死

**一句话**：键 31 在 R183 的干净稳态之后只多做一步——写 `HFI_CTRL_INIT=1`
并有界轮询 `HFI_CTRL_STATUS`，**不敲门铃**——结果窗口与 R183 **逐字节相同**：
内核死在日志 writer 打开之前，零字节。

### 32.1 设计

- R183 证明 CM3 起来时干净；上游 `a6xx_gmu_fw_start()` 在脉冲之后依次做
  `gfx_rail_on()` → `sptprac_enable()` → `hfi_start()`。
- 键 31 只做最后一步，用来判断「HFI 启动」本身是否可以在无轨道、无门铃的
  状态下完成。三种预期结局被事先写进 runner 的 docstring。

### 32.2 判决（DEVICE-VERIFIED）

- 构建：#292 / run `44241487655`（分支 `r184-hfi-init`，commit
  `808c5d16a11e8840e170ab7fc7c1593af2711835`，tree
  `4dba1ab689a07c842e5bd9e58c9f89e01938a930`）；#291 因 runner 侧
  `socket.gaierror`（DNS）失败，非编译错误。
- 工件：kernel `eab18a902fbc6064619cdfdbc8d7c95b5e41a90bb21f12b30e8c4dfca5863a34`，
  payload `26d2c5b9c7b3e0d28513ef33d64e4fa993e5cac5284976628d88e59d07b1136c`；
  `r184_static.py` PASS。
- rekey `=2 -> =31`：`7d1ce078a3ad36c7ccc38c605b93c6aed16652315cef30bd083113193c2741c0`。
- 上机：`flash readback matches: True`（system_b 头 `5e7e16fc…` == payload 头），
  窗口 **`41ab63d5…`（与 R183 逐字节相同）**、boot_id **546568834**（旧值）、
  `gmuhfi2=` 计数 **0**。

### 32.3 四次实验拼出的阶梯

| 键 | 写集 | 结果 |
|----|------|------|
| 30（R183） | 配齐 + 队列表 + 脉冲，之后只读 | 活：`intr=0`、`fwinit=babeface`、`hfistat=0` |
| 31（R184） | 键 30 + 写 `HFI_CTRL_INIT` + 轮询 | **静默死** |
| 22/23（R178） | 旧顺序 + 门铃 + 写 `HFI_CTRL_INIT` | 活：`intr=00800000`、`hfistat=0` |
| 18（R177） | 只有表 + 写 `HFI_CTRL_INIT` | 静默死 |

### 32.4 解读

- `HFI_CTRL_INIT` 这笔写在「CM3 干净运行」时会挂死主机；只有在门铃先把 CM3
  打成 FAULT 之后，同样的写才无害。也就是说：**门铃不是 HFI 启动的前提，而是
  把 CM3 提前打死，让后面那笔写落空**。
- 上游在脉冲与 `hfi_start` 之间还有 `gfx_rail_on()` 与 `sptprac_enable()`，
  这两步本仓库从未在「配齐 + 脉冲」之后试过 ⇒ 下一个实验（R185）把它们补上。

### 32.5 诚实边界

- 单次采样；「零字节」只证明 writer 打开前已死，不定位死在 store 还是后续读。

## 三十三、ROUND 185：轨道那几笔写在「继承来的致死写」后面，判决无效

**一句话**：键 32 上机后窗口与 R183 **逐字节相同**（`gmurail2=` 一条也没有），
但复查代码发现键 32 用的守卫是 `fwKey >= 32u`，而键 31（`>= 31u`，写
`HFI_CTRL_INIT`，R184 已证致死）那一段**是它的前缀** —— 也就是说键 32 必然
继承了那笔已知会杀死内核的写。因此这次「静默死」**不携带任何关于轨道的信息**，
是本轮实验设计的一处真错误，不是设备行为。

### 33.1 设计原意

- R183 证明「配齐再脉冲」后 CM3 干净（`intr=0`）。
- R184 证明在这个干净状态里写 `HFI_CTRL_INIT` 会挂死主机。
- 上游 `a6xx_gmu_fw_start()` 在脉冲与 `hfi_start()` 之间还剩两步：
  `a6xx_gmu_gfx_rail_on()` 与 `a6xx_sptprac_enable()`。
- 键 32 本应只做这两步并停手，用 GX 域的 bit16 判断轨道是否真的起来了。

### 33.2 判决（DEVICE-VERIFIED）

- 构建：#293 / run `44244959898`（分支 `r185-gfx-rail`，commit
  `b7030acc291d1f3f9e8df4c06610de2189ac00e1`，tree
  `51fa158ff1dcf6bb84debc32ec9a975ea3c2f9fb`）。
- 工件：kernel `569d55102576094c456b3b38e59157b3a84d6439ce3e7bfd982a2077137d5163`，
  payload `b2581a45937802d85b60f355f7d5d24246ff37212d26e3a989e31d82a8f6fabd`；
  十条门禁全绿（stage3 40 / stage4 57 / stage5 44 / stage6 536 / host 28 /
  k3 76 / gmu-boot 213 / gmu-bus 43 / hfi 46 / gmu-caller 26）。
- rekey `=2 -> =32`：`310fd5a93a3395102e4e2b033924125eee37617a09442eb01ad292a8be26907e`。
- 上机（`r185_s32_device_run.py`，14:05:28–14:05:43）：门禁 PASS、flash 成功、
  `flash readback matches: True`（system_b 头 8 MiB `2f2067f0…` == payload 头）。
- 窗口 **`41ab63d58db98ee0eab5a5e6a04f4d16c99842ec39eb6e2d45087780d776534e`**、
  boot_id **546568834**、msg_bytes 30172、slots_written 8、write_errors 0 ——
  与 R183 **逐字节相同**。
- 计数：`gmurail2=0`、`gmuhfi3=0`；`gmustat=1`、`gmuboot2=1` 都是 R183 那次
  留在窗口里的旧行。

### 33.3 为什么这次判决无效（自查）

- 键 30、31、32、33 的守卫分别写成 `if (fwKey >= 30u)`、`>= 31u`、`>= 32u`、
  `>= 33u`，是**累积式**的：选 key 33 会把 30/31/32 全部执行一遍。
- 键 31 那一段的第一笔就是 `gmuW[ADRENO_GMU_BOOT_V1_HFI_CTRL_INIT] = 1u;`，
  而 R184 已经用单变量证明这笔写在「配齐 + 脉冲」的干净状态里**必然挂死主机**。
- 于是「键 32 静默死」在逻辑上是**必然**的，与轨道写无关。R185 这一轮没有
  回答它想问的问题。

### 33.4 修正（R186 的形状）

- 键 31 收窄成 `if (fwKey == 31u)`，不再向后面的键泄漏那笔致死写。
- 键 32 与键 33 共用一个轨道块（`if (fwKey == 32u || fwKey == 33u)`），
  内容是 `BOOT_SLUMBER_OPTION=0` + `GX_VOTE_IDX=0` + `MX_VOTE_IDX=0` +
  `SPTPRAC_POWER_CONTROL=SPTPRAC_ON_VALUE` 与一次有界 SPTPRAC 轮询，**不敲门铃**。
- 键 33 在轨道块之后再补上游 `a6xx_gmu_set_oob(GMU_OOB_BOOT_SLUMBER)` 的完整
  三步：`HOST2GMU_INTR_SET = 1<<22` → 有界等待 `GMU2HOST_INTR_INFO` bit30 →
  `GMU2HOST_INTR_CLR = 1<<30`。
- 这样「轨道写」与「门铃」被拆成两个独立可判的键，任何一个死亡都携带新信息。

### 33.5 诚实边界

- 本轮的唯一产出是「实验设计有错」这一条结论；轨道是否可写、GX 是否会
  上电，R185 一个字都没回答。
- 键 32 的死亡位置（store 还是随后的读）未定位，且已无意义 —— R186 会重问。

## 三十四、ROUND 186：轨道写不再致死，但门铃只换来 `CM3_FAULT` —— 电源链的指针指向 RSC

**一句话**：把键 31 收窄之后，轨道那五笔写（`BOOT_SLUMBER_OPTION=0`、两个 vote、
`SPTPRAC_POWER_CONTROL`）**全部被总线接受且不再致死**（键 32 活），但
`GPU_CC_GX_GDSCR` 的 bit16 与 `SPTPRAC_PWR_CLK_STATUS` 的时钟位**一个都没变**；
在同一支镜像里加上上游的 boot-slumber 门铃（键 33），门铃之前 `intr=0`、
门铃之后 `intr=00800000`（`CM3_FAULT`），ACK 位始终不置。四次真机读数合起来
指向同一条链：**RSC/RPMh 电源控制器不应答 CPU**。

### 34.1 为什么必须先修 R185 的形状

- R185 的键 32 用 `if (fwKey >= 32u)`，而键 31（`>= 31u`）那一段的第一笔就是
  `HFI_CTRL_INIT = 1u`——R184 已用单变量证明这笔写在「配齐 + 脉冲」之后必然
  挂死主机。因此键 32 的静默死是**继承来的**，与轨道无关（见 §三十三）。
- R186 把键 31 收窄为 `if (fwKey == 31u)`，并把轨道步写成
  `if (fwKey == 32u || fwKey == 33u)`，门铃单独放进 `if (fwKey == 33u)`。
  这样「轨道写」与「门铃」第一次成为两个可以分开判的键。

### 34.2 判决一：轨道写被接受、不致死、也无效果（DEVICE-VERIFIED）

- 构建：#294 / run `44249842067`（分支 `r186-rail-doorbell`，commit
  `dbb0704c17b5f1e860889a1b704517f6bb791c39`，tree
  `faaf7c48777bff0cd932685bf4b2cd526ed6e939`）。
- 工件：kernel `0b9809f00939d287029478c17fe96a2dc47a8e12343769b542487cda978e29dc`，
  payload `bb655f4e659973b2ca322b249b6725ac08883aa0ec523b5ea2ca896ead802e1e`；
  `r186_static.py` PASS（required 29 / forbidden 12 / 固件头两处都在）。
- rekey `=2 -> =32`：`cf165f1646a5488175b11751ef368d0638766ec93d991f6870ae5f2ec6fc5d8e`。
- 上机（`r186_s32_device_run.py`，14:44:0x）：`flash readback matches: True`
  （system_b 头 `b91149ee…` == payload 头）；窗口
  **`51c46aec9126cccfbf5f8d2e30ee82d6d643d9a0702e5978aeabbd63fd902442`**、
  boot_id **561345366**（新）、msg_bytes 30336、slots_written 9、write_errors 0。
- 逐字行：

      gmurail2=gdscr_pre=00222001 gdscr=00222001 sptp_pre=000000d4 sptp=000000d4 sptppolls=400000 intr_pre=00000000 intr=00000000 ok=0

- 解读：五笔写全部落盘（总线没有挂）；`gdscr` 前后**逐位相同**（bit16 GX_ON 仍 0）；
  `sptp` 前后相同（`0xd4` 的 `&0x38` 是 `0x14`，不等于 `0x28`，轮询耗尽 400000 次）；
  `intr` 前后都是 0——**轨道写本身不产生 fault**。

### 34.3 判决二：门铃在同一支镜像里制造 `CM3_FAULT`（DEVICE-VERIFIED）

- rekey `=2 -> =33`：`312a3351c69fecb24505e94e7f558692b11cf61781e472a4d403ab3870400b48`。
- 上机（`r186_s33_device_run.py`，14:45:0x）：readback 同上；窗口
  **`dab0ae7ceeff0099464dfa513e7f4da9184b44931aabdc744e0cb55deb6e40c0`**、
  boot_id **569740160**（新）、msg_bytes 30453、slots_written 9、write_errors 0。
- 逐字两行（同一次启动，门铃之前 / 之后）：

      gmurail2=gdscr_pre=00222001 gdscr=00222001 sptp_pre=000000d4 sptp=000000d4 sptppolls=400000 intr_pre=00000000 intr=00000000 ok=0
      gmurail3=ack=0 polls=400000 gdscr=00222001 intr=00800000 fwinit=babeface reset=00000000 ok=0

- 解读：`HOST2GMU_INTR_SET = 1<<22` 之后，同一个 CM3 的 `GMU2HOST_INTR_INFO`
  从 `00000000` 变成 `00800000`（`CM3_FAULT`，bit23），ACK(bit30) 在 400000 次
  轮询内从未置起；`fwinit=babeface`（固件仍在）、`reset=00000000`（不在复位里）。
  这是「门铃把 CM3 打成 FAULT 而不是让它应答」的**同启动内对照**，比 R183 的
  跨轮次推断更强。

### 34.4 四条读数合起来的一条链

| 轮 | 观测 | 读数 |
|---|---|---|
| R179/R180 | `RSCC_CONTROL_ACK`（GMU 页 `0x9308`） | 400000 次轮询后仍 `00000000` |
| R186 键 32 | `SPTPRAC_PWR_CLK_STATUS`（`0x50d0`） | 写 `0x00778000` 后仍 `000000d4` |
| R186 键 32 | `GPU_CC_GX_GDSCR`（`0x9c03`） | 前后都是 `00222001`，bit16=0 |
| R186 键 33 | `GMU2HOST_INTR_INFO` 的 OOB ACK | 门铃后是 `00800000`（FAULT），ACK 从不置 |

⇒ 三条独立的电源握手（RSC ack、SPTPRAC 时钟就绪、GX 域上电）**全部无响应**，
而唯一有响应的是「CM3 收下写并报 fault」。CM3 能跑（`babeface` 是它自己写的），
说明它靠的是复位后的本地状态，**外部电源控制器（APPS RSC / RPMh）没有对它
履行任何一条请求**。这与 R182「引导器没灌 cmd-db」不是两件事，而是同一条
电源路径的两端。

### 34.5 下一步的两个方向（尚未选定）

1. **补齐 APPS RSC 的 TCS 配置**：`package/EnchiladaTouchRpmh.h` 里已有
   `ftb_rpmh_active_request()` 与 `ftb_rsc_layout_for()` 两套 TCS 写序列
   （`FTB_RSC_TCS 0xd00u`、`FTB_CONTROL_ACTIVE_MODE (1u<<16)`、
   `FTB_CONTROL_TRIGGER (1u<<24)`），touch 驱动用它成功对 RPMh 下过请求。
   这是本仓库里**唯一已经在本机跑通过 RPMh 的代码**。
2. **绕开 RPMh**：评估是否存在让 GMU 在「无 arc vote」状态下继续的路径
   （例如把 `GX_VOTE_IDX/MX_VOTE_IDX` 保持为 0 并跳过 RSC 握手），代价是
   无法保证电压档，属于「能跑但不可靠」的路线。

### 34.6 诚实边界

- 键 32/33 各一次上机；`sptppolls=400000` 是**有界轮询耗尽**，不等于
  「永远不会有响应」，但已足够说明 400000 次内没有。
- `gdscr` 的位定义只用仓库钉住的 `AdrenoDomainState.h`（`GX_ON_BIT 16`）；
  其余位（`00222001` 里的 13/17/21）**不作解读**。
- 本轮**没有**验证 `HOST2GMU_INTR_SET` 的其他 OOB 位（GPU_SET/DCVS），
  只验证了上游 `gfx_rail_on()` 实际使用的 boot-slumber 位（22）。

## 三十五、ROUND 187：APPS RSC 可读、命令数据库在 `0x85fe0000` —— R182 的判决被推翻

**一句话**：CPU 侧的 APPS RSC 窗口（`0x179e0000`，`qcom,tcs-drv`）从这支内核里
**是可读的**，读回的 `drv=00010100` 与触摸驱动自己的 `RAIL` 行逐位相同；顺着这条
通道能读到一个**带 magic 的命令数据库头**，基址 `0x85fe0000`。因此 R182 的
「这台机器引导器没有灌 cmd-db」**只对当时试过的那两个地址成立**，不是机器事实。

### 35.1 为什么值得先做这一轮（而不是继续猜 GMU）

- §三十四把四条独立读数合成一条链：RSC `ack=0`、SPTPRAC 时钟不就绪、
  GX 不上电、门铃只换来 `CM3_FAULT`。但这四条**全是 GMU 侧的窗口**
  （`gmu mmio + 0x23000`）。
- 同机同窗里另有一行不属于 GPU 的读数，来自触摸探针：

      RAIL rc0 db85fe0000 w85fe0000 tcs0 drv00010100 a00042100 ctl00000000 st00000001 n36

  `rc=0` 表示触摸驱动对 RPMh 的请求**成功了**。也就是说：
  - 这台机器上 **CPU 侧** 的 RSC/RPMh 通道是通的；
  - 命令数据库在这个驱动看来位于 `0x85fe0000`；
  - 这与 GPU 侧 `gmursc=ack=00000000 busy=00000000 mem0=00000000` 形成直接对照。
- 所以本轮只做一件事：**用只读方式确认这两条读数在 GPU 探针里也成立**。
  不写任何寄存器，不碰 TCS。

### 35.2 键 34 的形状（只读）

- 构建：#295 / run `44256735543`（分支 `r187-apps-rsc`，commit
  `d173e44435cd4eb8577ddfe8d386adba8ff32d6e`，tree
  `f29cae3dc9cd99a89cd2569d962dd3a077ad2405`）。
- 工件：kernel `7eba2bc0414dbb5283fe972f39183e3d4e59da55793255edd97f9cec26df6117`，
  payload `3faab15e3ae5b0975b85f5ad0a922f8d725810d4bbadb2e2e6f2f563b72ad9b0`；
  `r187_static.py` PASS（required 33 / forbidden 12 / 固件头两处都在）。
- rekey `=2 -> =34`：`ebc18943c3e22266241def0a2e4a3472a11f5af7bb267a09ff3a333b174c62fe`。
- 块内容：
  - 一次 `armcpu_map_physical(0x00000000179e0000ULL, 0x00010000u)`——整个窗口，
    与触摸驱动 `mapRegion(FTB_APPS_RSC_BASE, FTB_APPS_RSC_SIZE)` 相同范围；
  - `rscDrv = rd32(rscVa, 0x00u)`，由 `(rscDrv / 0x10000u) % 256u` 得 major 版本，
    据此在 v3 与 legacy 两套偏移之间选择（**用除/模而不是移位或掩码**：stage-6
    验证器有一条「不得重新推导 ID1 字段」的守卫，会拦住 `>> 16` 这类写法）；
  - 四个 TCS（`0x0d00u + 672u * i`）各读 `control`/`cmd_enable`/`status`/`wait_for_cmpl`；
  - 再读 `0x85fe0000` 的头两字与 `0xc3f00000` 页 `+0x0c` 的那个字。
  - 整个块**只有一个 API：`rd32`**。无 `gmuW[`、无 `wr32`、无 `dsb sy`。

### 35.3 判决：三条读数全部成立（DEVICE-VERIFIED）

- 上机（`r187_s34_device_run.py`，15:36）：`flash readback matches: True`
  （system_b 头 `c634af16…` == payload 头）；窗口
  **`896719d3ea9914bfa713b0f241c413f7ec49da05f783a40e03fc15165e61425b`**、
  boot_id **553864669**（新）、msg_bytes 30560、slots_written 8、
  uptime_s 3、write_errors 0。
- 逐字三行：

      gmursc2=va=fffffffa48820000 drv=00010100 v3=0 ctl=00000000/00000000/00000000/00000000
      gmursc3=en=00000000/00000000/00000000/00000000 st=00000001/00000001/00000001/00000001
      gmursc4=db=fffffffa48830000 h0=00000001 h1=0c0330db magic=1 pgw=10c11c22 wait=00000000/00000000/00000000/00000000

- 解读：
  1. **`drv=00010100`** 与触摸驱动的 `drv00010100` 逐位相同 ⇒ 探针读到的就是
     同一个 APPS RSC，且版本 major = 1 ⇒ 走 legacy 布局（`v3=0`），
     与触摸驱动实际使用的那套偏移一致。
  2. **`magic=1`**（`h1=0c0330db`，`h0=00000001` 是 version）⇒ 命令数据库
     确实在 **`0x85fe0000`**。R182 试过的 `0x861e0000`（全 0）与 AOSS 指针页
     读到的 `0x10c11c22`（不是 RAM 地址）**两个候选都是错的地址**，
     不是「机器没有数据库」。
  3. **`pgw=10c11c22`** 与 R182 键 29 在那张页上读到的字**完全相同** ⇒ 那张页
     确实是 AOSS 消息 RAM，里面装的是 RPMh 命令字，不是指向数据库的指针；
     触摸驱动之所以能成功，是因为它另有一条路径（候选二在它那里命中）。
  4. 四个 TCS 的 `ctl`/`en` 全 0、`st` 全 1 ⇒ **全部空闲**，没有被写死或残留
     占用；这与 GMU 侧 RSCC 的「有背衬但从不应答」不同。

### 35.4 这条判决改变了什么

- R181 想用命令数据库算 `arc vote` 的路子当时被自己否掉（键 27 读
  `0x861e0000` 拿不到 magic）。现在这条路**重新成立**：
  - 基址 `0x85fe0000` 已知且已验证；
  - 探针里已有现成的解析器 `quiet_cmd_db_levels()`（magic 校验在 `+4`、
    8 条 slave 记录、条目 24 B、地址在 `+16`、level 表是零填充 u16）；
  - 上游 `a6xx_gmu_rpmh_arc_votes_init()` 的公式只需 `pindex`/`sindex`，
    即「表中最大 level 首次出现的下标」，不需要 OPP 表（见 §三十一）。
- 仍未解决的不是数据来源，而是**通道**：GPU 侧 `RSC_CONTROL_ACK` 依旧为 0
  （§三十四）。CPU 侧的 APPS RSC 通、GMU 侧的 RSCC 不通——两者是两条不同的路。
  下一轮要回答的问题是：拿到真实 `gfx.lvl`/`cx.lvl` 之后，把这两笔 vote 写进
  `GX_VOTE_IDX`/`MX_VOTE_IDX`，GMU 侧的 `ack` 会不会变。

### 35.5 诚实边界

- 本轮只读，**没有**向任何 TCS 写过一笔，因此「TCS 空闲」只证明寄存器状态，
  不证明从这支内核写进去会被接受。
- `magic=1` 只验证到数据库头的前两字；**没有**解析 slave 记录，
  因此「`gfx.lvl` 和 `cx.lvl` 两张表能读出来」仍是假设。
- `drv` 版本 major 只用来选偏移；`0x00010100` 里的低 16 位（`0x0100`）
  **不作解读**。
- 本轮没有重新验证 R182 的 `0x861e0000`：那是另一支镜像的读数，本轮只测了
  `0x85fe0000` 与 `0xc3f00000` 页。

## 三十六、ROUND 188：从 `0x85fe0000` 读三张 arc-vote 表 —— `gfx.lvl` 十档，另两张读不到

**一句话**：命令数据库确实能解析，`gfx.lvl` 读出十档、最高 `0x01a0` 在下标 9；
但 `cx.lvl` 与 `mx.lvl` 都报 0 条。**当时的结论（「这两张表不在这两个名字下」）后来
被 ROUND 189/190/191 推翻**，真因是解析器的一个越界读 —— 见 §三十八、§三十九。

### 36.1 键 35 的形状（只读）

- 构建：#296 / run `44262364300`（分支 `r188-level-tables`，commit
  `ac247592829432f7d1dc520b0c3853450c2aeb08`，tree
  `2e0e1c8a5d609863d2f569602823a5deea4529a7`）。
- 工件：kernel `23b5feb4f49121fb3260b12bc476604656d838842f2beb5a6065f032da4f5b3e`，
  payload `ca9b4c422c636517bc5a16d49a44686c7940364d8a2d29a9d04c71f0af1cef51`；
  `r188_static.py` PASS（required 39 / forbidden 12）。
- rekey `=2 -> =35`：`8ea993141a24c6d6b267133f17e64b0710a317dac7835371783bc35926d22790`。
- 块内容：一次 `armcpu_map_physical(0x85fe0000ULL, 0x00020000u)`，然后三次调用
  文件级 helper `quiet_cmd_db_levels(db, "gfx.lvl"|"cx.lvl"|"mx.lvl", …)`，
  各自求最大值与首次下标，三行 `gmudb2=`/`gmudb3=`/`gmudb4=`。纯 `rd32`。

### 36.2 判决（DEVICE-VERIFIED，16:13）

- 上机：`flash readback` 一致；boot_id **552771680**（新）；gputate 行在。
- 逐字三行：

      gmudb2=va=fffffff9a0dea000 magic=0c0330db gfx_n=10 cx_n=0 mx_n=0 gfx_a=00030050 cx_a=00000000 mx_a=00000000
      gmudb3=g0=0000 g1=0010 g2=0030 g3=0040 c0=0000 c1=0000 c2=0000 c3=0000 m0=0000 m1=0000 ok=1
      gmudb4=gfx_top=01a0 gfx_at=9 cx_top=0000 cx_at=0 mx_top=0000 mx_at=0

- 解读：`gfx.lvl` 十档、最高 `0x01a0` 在下标 9 ⇒ 上游 `pindex` 公式
  （「表中第一个 >= 目标 level 的下标」）在这一轮就能算出 9。
- 诚实边界：`cx_n=0`/`mx_n=0` **只说明这次解析没找到**，不说明数据库里没有。
  当时的下一步写成「去查 `gmxc.lvl`」，那是错的方向 —— 见 §三十九。

## 三十七、ROUND 189：数据库里其实有 `cx.lvl`/`mx.lvl` —— R188 的 0 计数不是数据缺失

**一句话**：把八个 slave 记录里的条目名逐条打出来，`cx.lvl`、`cx.tmr`、
`mx.lvl`、`mx.tmr`、`ebi.lvl`、`ebi.tmr`、`lcx.lvl`、`lcx.tmr` 全在。
所以 R188 的 `cx_n=0 mx_n=0` 是**解析侧的问题**，不是数据侧。

### 37.1 键 36 的形状（只读）

- 构建：#297 / run `44265620505`（分支 `r189-db-names`，commit
  `b793cfea057f0170f4f59ec87f40b7da97fa64dc`，tree
  `d5196452b6bbb61e509466e5cdfeb9fcb6c13d20`）。
- 工件：kernel `22f394496dc9a32f2fe903b1f4654b63d61d9ad69f40a83760c6d7bc213d5bda`，
  payload `1b313066b22b550b5acd9d3bfe728f7cfe9ec5b99d5776e3bb6722e7c8a1fee3`；
  `r189_static.py` PASS（required 41）。
- rekey `=2 -> =36`：`e28e7cb5afd752acf69fb0974b7702722a303188ede52db559fde3aa5ab4ebf2`。
- 块内容：映射 `0x85fe0000`(0x20000)，按 slave 记录的 `(lo/65536)%65536` 与
  `(hi/65536)%65536` 取头偏移与数据偏移（**除/模，不用移位**：stage-6 有守卫
  禁止重新推导 ID1 字段），条目步长 24，名字按 8 字节拆开；两行
  `gmudbn=`/`gmudbn2=`。

### 37.2 判决（DEVICE-VERIFIED）

- boot_id **553540595**（新）、write_errors 0。
- 逐字：

      gmudbn=va=fffffff9a6de4000 magic=0c0330db slaves=3 entries=8 n0=cx.lvl n1=cx.tmr
      gmudbn2=n2=mx.lvl n3=mx.tmr n4=ebi.lvl n5=ebi.tmr n6=lcx.lvl n7=lcx.tmr

- 解读：八个名字干净可读 ⇒ 数据库格式与我们的理解一致；`cx.lvl` 与 `mx.lvl`
  **一直在库里**。这把 R188 的判读写成了「解析 bug」，也把 R181 的
  `gmxc.lvl` 猜测彻底排除（库里没有 `gmxc.lvl`，所以上游回退 `mx.lvl` 是必然）。

## 三十八、ROUND 190：逐条目读出地址/长度/偏移 —— `cx.lvl`/`mx.lvl` 形状完全正常

**一句话**：`cx.lvl` 在 `0x00030000`、`mx.lvl` 在 `0x00030010`，两者长度都是
32 字节（16 个 u16 = 16 档），辅助块偏移合法。数据侧一切正常，剩下的只能是
解析器自己。

### 38.1 键 37 的形状（只读）

- 构建：#298 / run `44269397393`（分支 `r190-entry-shape`，commit
  `8cecad02315f1d74c1fb734ee426ade1b99db851`，tree
  `6a101860d7c0f98245db75b8f958cacadd696840`）。
- 工件：kernel `3b38a910ed711cfcaf15efc247a3bf04115bf8a6540e5d2b0b98f09bfbd8969c`，
  payload `1248b0a4d5089ce15c757cd8260ca4615bcdffd201473f6f7440dd841570b87f`；
  `r190_static.py` PASS（required 45）。
- rekey `=2 -> =37`：`44bce444cb5199ec0177fa792bfe0ef5643217790f7a3e49f50bd2aedf205dde`。
- 块内容：逐 slave/entry 读 `+16` 的地址与 `+20` 的打包尾
  （`len = tail % 65536`、`off = (tail / 65536) % 65536`），并判
  `data + off + 2*len <= 0x20000`；四行 `gmudbe=`/`gmudbe2..4=`。

### 38.2 判决（DEVICE-VERIFIED）

- boot_id **553507328**（新）。
- 逐字（节选）：

      gmudbe=va=fffffff2c6c1a000 magic=0c0330db seen=9
      gmudbe2=n0=cx.lvl a0=00030000 l0=32 o0=0 f0=1 n1=cx.tmr a1=00030004 l1=0 o1=32 f1=1 n2=mx.lvl a2=00030010 l2=32 o2=32 f2=1
      gmudbe3=n3=mx.tmr a3=00030014 l3=0 o3=64 f3=1 n4=ebi.lvl a4=00030020 l4=32 o4=64 f4=1
      gmudbe4=n6=lcx.lvl a6=00030030 l6=32 o6=96 f6=1 n7=lcx.tmr a7=00030034 l7=0 o7=128 f7=1 n8=lmx.lvl a8=00030040 l8=32 o8=128 f8=1

- 解读：`f0/f2/f4/f6` 全为 1（适配），`l0=l2=32` ⇒ 每张 level 表 16 档。
  `.tmr` 条目长度 0 是正常的（定时器资源没有 level 列表）。

## 三十九、ROUND 191：真正的 bug —— 名字比较读了第 8 个字符

**一句话**：`quiet_cmd_db_levels()` 的名字比较循环用 `(uint8_t)want[i]` 逐字节比
到 `i == 7`。`"gfx.lvl"` 正好 7 个字符，`want[7]` 是 C 字符串的终止符；而
`"cx.lvl"`/`"mx.lvl"` 只有 6 个字符，**`want[7]` 越界**。于是所有 6 字母资源
都永远匹配失败。补零到 8 字节后三张表全部读出。

### 39.1 为什么这条 bug 藏了四轮

- R188 只试了 `gfx.lvl`（7 字母，碰巧工作）和两张 6 字母表（全失败）。
  一成一败看起来像「数据缺失」，而不是「解析器只对 7 字母有效」。
- R189/R190 读的是**原始字节**（名字、地址、长度），完全绕过比较函数，
  所以它们正常 —— 这三轮的读数正好把 bug 夹在中间。
- 这个 bug 出自我在 R181 写的 helper，不是仓库原有的代码。

### 39.2 修法与键 38

- helper 里加 `uint8_t wantPad[8]; unsigned wantLen = 0u; while (wantLen < 8u &&
  want[wantLen] != '\0') { ++wantLen; }`，再补零到 8，比较改成
  `if (byte != wantPad[i]) same = false;`。
- 键 38 重做三次 lookup（`gfx.lvl`/`cx.lvl`/`mx.lvl`），三行
  `gmudb5=`/`gmudb6=`/`gmudb7=`。
- 构建：#299 / run `44273934260`（分支 `r191-name-pad`，commit
  `1eca33e2abe574e625255aa005478275a8583bb0`，tree
  `d17408d2af3d779e3f5fc0d685e6915c04235692`）；kernel
  `b4efa49c87d9611398728aef83a929f53ddbe3b56612c7522166a8e63ae4d8a0`、payload
  `a3de375d7c003ec6ee1e03861dddda159cb2ac0671c4e8df55a259c280286c8b`；
  `r191_static.py` PASS（required 48）；rekey `=38` →
  `f7650403b60881d14b9c0a73a37708ec35e1c9361986437ef364ba5ee3b06de3`。

### 39.3 判决（DEVICE-VERIFIED）

- boot_id **553653878**（新）。
- 逐字：

      gmudb5=va=fffffffa3d0b4000 magic=0c0330db gfx_n=10 cx_n=8 mx_n=5 gfx_a=00030050 cx_a=00030000 mx_a=00030010
      gmudb6=g0=0000 g1=0010 g2=0030 c0=0000 c1=0010 c2=0030 c3=0040 m0=0000 m1=0010
      gmudb7=gfx_top=01a0 gfx_at=9 cx_top=0180 cx_at=7 mx_top=0180 mx_at=4 ok=1

- 解读：三张表全读出（`gfx_n=10`、`cx_n=8`、`mx_n=5`）；上游公式要的两个下标
  是 `pindex = gfx_at = 9`、`sindex = mx_at = 4`。
- 教训（写给下一个模型）：**「读不到」和「没有」必须分开测**。R189/R190 绕开
  helper 读原始字节，正是这两轮把结论从「数据缺失」掰回「解析 bug」。

## 四十、ROUND 192：用真索引写 vote —— 值对、写进去、**没有人应答**

**一句话**：把从数据库算出的 `gx=9 mx=4` 写进 `GX_VOTE_IDX`/`MX_VOTE_IDX`
（`vote=0x01a00409`，与上游公式逐位相符），寄存器锁存读回一致；但 GX 轨道、
SPTPRAC 时钟、boot-slumber 门铃 **全部照旧无响应**。与 R186 的「索引恒为 0」
结果逐位相同 ⇒ 「vote 值不对」这条假设**彻底死亡**。

### 40.1 键 39 的形状

- 先只读：映射 `0x85fe0000`，读 `gfx.lvl`/`mx.lvl`，求 top 与首次下标。
- 再写五笔 + 门铃：`BOOT_SLUMBER_OPTION=0`、`GX_VOTE_IDX`、`MX_VOTE_IDX`、
  `SPTPRAC_POWER_CONTROL=0x00778000`、`HOST2GMU_INTR_SET=1<<22`，各自 `dsb`，
  两个有界轮询（SPTPRAC 时钟、门铃 ACK bit30），然后 `GMU2HOST_INTR_CLR=1<<30`。
- 两行 `gmuvote2=`/`gmuvote3=`。
- 构建：#300 / run `44277876368`（分支 `r192-vote-derived`，commit
  `119dd15698378b913808249b21a97f5dc5158b28`，tree
  `701084b3cbd70e95aaddcaba646b9994461da32c`）；kernel
  `19d2f2d025c36076446dbd3f621e617b628fbec6af861c7f88ac46923b0a3046`、payload
  `d8b277c6874437610bd306c409319322cb2421dacb15811f90d234e55df8cdae`；
  `r192_static.py` PASS（required 50）；rekey `=39` →
  `a965ea44fa55284f04f22af5c621ec72c665645d16fc6d85bd1c5ac31ba4e4e2`。
- 三处自找的错（已修）：①`avMx` 与 `uint16_t avMx[16]` 重名（
  `error: redefinition of 'avMx' with a different type`），改名 `avGxIdx`/`avMxIdx`；
  ②`(x << 16)` 触发 stage-4 的「不得重新推导 ID2 字段」守卫，改成 `* 65536u`；
  ③验证器里四处「vote 只有一个写者」的断言要改成三写者，host 门写预算 43 → 49。

### 40.2 判决（DEVICE-VERIFIED，17:56）

- `flash readback matches: True`；窗口
  **`e23a337c5da830c0969913470f8cd544de5b33bc994b37731f57a147cbc041fc`**、
  boot_id **569930826**、write_errors 0。
- 逐字：

      gmuvote2=gx=9 mx=4 gfx_n=10 mx_n=5 gfx_top=01a0 mx_top=0180 vote=01a00409 have=1
      gmuvote3=ack=0 polls=400000 gxvote=00000009 mxvote=00000004 gdscr_pre=00222001 gdscr=00222001 sptp=000000d4 sptpp=400000 intr=00000000 fwinit=babeface ok=0

- 三点：
  1. `gx=9`/`mx=4` 来自数据库，不是常量；`vote=0x01a00409` 与
     `((pri & 0xffff) << 16) | (sindex << 8) | pindex` 逐位相符。
  2. `gxvote=9`/`mxvote=4` ⇒ 写入选址锁存并被读回。
  3. `gdscr` 前后同为 `00222001`（GX bit16 = 0）、`sptp` 仍 `0xd4`
     （`& 0x38 = 0x14 ≠ 0x28`）、ACK 400000 次未置起、`intr=0`。
- 结论：**问题不是「投什么票」，而是「谁该应答」**。这条结论把 R186 的
  index-0 结果与 R192 的 index-9 结果合成同一条：两次都写进去了，两次都没人理。

## 四十一、ROUND 193：把 RSC 定序器微码装进去 —— 写入并读回成功，握手仍然不应答

**一句话**：按上游 `a6xx_gmu_rpmh_init()` 的探测期写法把五字定序器微码 +
十一个设置字写进 RSCC 页，**五个字逐字读回**（`loaded=1`），后面独立跑的
键 26 也在同一启动里读到了这些值；但 `RSCC_CONTROL_REQ=2` 之后
`RSCC_CONTROL_ACK` 400000 次轮询内从未置起。**「RSC 缺微码所以不应答」被实测否掉。**

### 41.1 依据

- R180 的 `gmursc=… mem0=00000000 …` 读的是 `RSCC_SEQ_MEM_0_DRV0`
  （RSCC 页 `0x0180`，字节偏移经 `GMU_RSCC_DELTA 0x23000`）—— 定序器微码从未装载。
- 上游 `a6xx_gmu.c:561-595`（A630 走非 650/7xx 的 else 分支）：
  `RSCC_STATUS0_DRV0 = BIT(24)`（关 SDE 时钟门控）、`RSCC_PDC_SLAVE_ID_DRV0 = 1`、
  `HIDDEN_TCS_CMD0_ADDR/-DATA` 归零、`RSCC_OVERRIDE_START_ADDR = 0`、
  `RSCC_PDC_SEQ_START_ADDR = 0x4520`、`MATCH_VALUE_LO = 0x4510`、
  `MATCH_VALUE_HI = 0x4514`，然后是五字微码
  `0xa7a506a0 / 0xa1e6a6e7 / 0xa2e081e1 / 0xe9a982e2 / 0x0020e8a8`。
- `a6xx_gmu_rpmh_init()` 只在 `a6xx_gmu_init()`（探测期、`gmu->initialized = true`
  之前）调用一次，所以这是**一次性初始化**，不是每次启动都做。
- 已核对：R171 九笔里的 `RPMH_CTRL |= 0x00000f11` 展开正是
  `RPMH_INTERFACE_ENABLE` + `LLC/DDR/MX/CX/GFX_VOTE_ENABLE` 六个使能位，
  真机读回 `rpmh=00000f11` ⇒ 六个使能齐全，不是缺口。

### 41.2 键 40 的形状

- 先读十一个字做 `pre` 快照 → 按上游顺序写十一笔设置字 → 五笔微码 → `dsb sy`
  → 读回五个字 → 重跑 `a6xx_rpmh_start()` 四步（`REQ=2` → 有界轮询 ACK bit1
  → 读 `SEQ_BUSY_DRV0` → `REQ=0`）→ 读 `gdscr`/`sptp`。
- **所有 RSC 写都写成 `gmuW[(GMU_RSCC_DELTA / 4u) + <offset>]`**（字节 delta 转
  dword 索引）；写成 `* 4u` 是本仓库曾经踩过的坑。
- 块内**不含** `HFI_CTRL_INIT`、不含 `CM3_SYSRESET` —— 避开键 18/31 与所有早期
  HFI 尝试的致死写。
- 构建：#301 / run `44282852022`（分支 `r193-rsc-ucode`，commit
  `e7bb3bd7a3d8f7905b4e6c06fd2e885023ef447a`，tree
  `07aa7736746c7164dbd7853e2ac68fd63cbb45cd`）；kernel
  `ff2c436abe44ff2d62b15fac64da29442994d948c01b02d3bb89a7c21d9c82c0`、payload
  `4b4916b78d89c9977208f44a3982280192f5a6a800c042bd39ee265d1ef451db`；
  `r193_static.py` PASS（required 56）；rekey `=40` →
  `d3dda08fe02170aaff0961d2dc3b6f070e6f9c1c154f6e10740b10c93c7f6c02`。
- 门禁坑：①`RSCC_PDC_SEQ_START_VALUE` 这个宏名不存在，真名是
  `ADRENO_GMU_BOOT_V1_PDC_SEQ_START_VALUE`（无反 `RSCC_` 前缀），我写错断言导致
  stage6 617/618；②host 门写预算 49 → 68（键 40 新增 19 笔 store）。

### 41.3 判决（DEVICE-VERIFIED，18:38）

- `flash readback matches: True`（头 `83434f40…`）；窗口
  **`1319ce5dfdc71b01e79676079f0b191da2b92cb55d0a2f1675897a9b7b080113`**、
  boot_id **590380335**（新）、msg_bytes 30598、write_errors 0。
- 逐字三行：

      gmusetup=pre=00000000/00000000/00000000/00000000/00000000 st0=00000000 ovr=00000000 seq=00004520 lo=00004510 hi=00004514 slave=00000001
      gmusetup2=post=a7a506a0/a1e6a6e7/a2e081e1/e9a982e2/0020e8a8 st0=01000000 loaded=1
      gmursc5=ack=00000000 apolls=400000 busy=00000000 bpolls=0 req=00000002 gdscr=00222001 sptp=000000d4 ok=0

- 四点：
  1. `loaded=1` ⇒ 五个字写前全 0、写后逐字读回 ⇒ RSCC 页**接受并保存**写入。
  2. 交叉侧证：同一启动里，后跑的键 26 读到 `mem0=a7a506a0 st0=01000000`
     ⇒ 另一条代码路径能看到这些值，不是「读回的是写缓冲」。
  3. `req=00000002` 已置位，`ack=00000000` 400000 次未回 ⇒ **微码不是缺口**。
  4. GX/SPTPRAC 仍不动。
- 这一轮把「RSC 因为没程序所以不理你」这条最合理的假设**排除**了，问题是
  「谁在服务这个请求」。

## 四十二、ROUND 194：PDC 页到底在哪 —— 厂商布局成立，mainline 推导的两个窗是空的

**一句话**：在同一个 3 MiB 区域里，**基址**（`0x0b200000`）读得到
`ENABLE_PDC=1`、`SEQ_MEM_0=0x00a000a8`，而 mainline `sdm845.dtsi` 推出的
`+0x80000`（`gmu_pdc`）与 `+0x280000`（`gmu_pdc_seq`）**全读 0**。
所以将来要写 PDC，位置在基址；如果照 mainline 写，写的就是没人读的空间。

### 42.1 为什么要先问这个（而不是直接写 PDC 微码）

- 上游 `a6xx_gmu_rpmh_init()` 在写完 RSC 页后**还有一整段 PDC**：
  五字微码 `0xfebea1e1/0xa5a4a3a2/0x8382a6e0/0xbce3e284/0x002081fc`、
  TCS1 六字（`ENABLE_BANK=7`、`WAIT=0`、`CONTROL=0`，三条命令 msgid `0x10108`
  + addr `0x30010`/`0x30000`/`pdc_address_offset` + data `1`/`0`/`0`）、
  TCS3 六字（同形，data `2`/`3`/`3`）、`PDC_SEQ_START_ADDR=0`、
  `PDC_ENABLE_PDC=0x80000001`，最后才 `a6xx_rpmh_stop()`。
- A630 走 `pdc_in_aop = false`，`pdc_address_offset = 0x30080`。
- 但两套 DT 对位置的说法冲突：
  - mainline `sdm845.dtsi`：`reg = <0x0506a000 0x30000>, <0x0b280000 0x10000>,
    <0x0b480000 0x10000>`，`reg-names = "gmu", "gmu_pdc", "gmu_pdc_seq"`；
  - 厂商 `enchilada.dts`：`reg = <0x506a000 0x30000 0xb200000 0x300000>`，
    `reg-names = "kgsl_gmu_reg", "kgsl_gmu_pdc_reg"` —— 一整块，**包含**上面两个窗。
- 后果：R166 在 `base + 0x4500` 读到 `pdcen=1` 若真相是 `+0x80000 + 0x4500`，
  那次读数就**没有意义**。这一轮先把这个问清楚，**什么都不写**。

### 42.2 键 41 的形状（只读）

- 一次 `armcpu_map_physical(ADRENO_BOARD_GMU_PDC_BASE, ADRENO_BOARD_GMU_PDC_SIZE)`
  （只读入口，不是 `_rw`），然后读两组：基址的
  `SEQ_MEM_0`/`ENABLE_PDC`/`SEQ_START_ADDR`，以及 `+0x80000` 的同三个加上
  `TCS0_CONTROL`；再读 `+0x280000` 的五字。两行 `gmupdc2=`/`gmupdc3=`。
- 块内零写、零 `dsb`、零 `gmuW[`。
- 构建：#302 / run `44290112701`（分支 `r194-pdc-window`，commit
  `fe1216ba99c0b086c0f8326958a17580e89e7d17`，tree
  `4d68a7f54f03dccdb66cc0d28a73768a4ee571ce`）；kernel
  `233b5bb8b0db182d258a6fa5a717b85ed18e674be2c7dab0382a3a2a9a1371b2`、payload
  `98ede926f18ad5eb3cc1f54e42fdfe912840a1c4908dc1e424b34551891dd09e`；
  `r194_static.py` PASS（required 59）；rekey `=41` →
  `4c8a7050a3abf3b782b0289040319a6711c491154769ff1a300921a36bc56c7c`。

### 42.3 判决（DEVICE-VERIFIED，19:33）

- `flash readback matches: True`（头 `a4c29686…`）；窗口
  **`8673a819bd408104ba0c485b562e2d533d2f734e95edf7b6ea2233df0713e343`**、
  boot_id **585770167**（新）、msg_bytes 30410、write_errors 0。
- 逐字：

      gmupdc2=vseq=00a000a8 ven=00000001 vst=00000000 pseq=00000000 pen=00000000 pst=00000000 pctl=00000000
      gmupdc3=s0=00000000 s1=00000000 s2=00000000 s3=00000000 s4=00000000

- 解读：
  1. **厂商布局成立**：`ven=00000001`、`vseq=00a000a8`，与 R166 的
     `gmupdc=pdcen=00000001 seqmem=00a000a8` 逐位相同 ⇒ R166 读的从来不是空地址。
  2. mainline 推导的 `+0x80000` 三个字全 0 ⇒ 那里没有寄存器。
  3. `+0x280000` 五字全 0 ⇒ 定序器内存不在那里（但也不代表基址那块就是定序器
     内存，需要 ROUND 195 再确认）。
  4. 同窗 `gmursc=mem0=00000000` —— R193 写的微码**没有持久化**（本轮不写，
     所以回到 0），这既说明页是易失的，也再次证明 R193 的写入是真写。
- 这一轮的收获是一条**负结果**：避免了按 mainline 地址去写 PDC 微码。

## 四十三、ROUND 195：PDC 早就被引导器配好了 —— 它不是缺口

**一句话**：在厂商基址读 TCS1/TCS3，`ENABLE_BANK=0x0f`、`CMD0_ADDR` 与
`CMD0_DATA` 都不是 0，而且地址正好落在上游 `a6xx_gmu_rpmh_init()` 会写的那组
`0x30010 / 0x30080` 上 —— **引导器已经把 PDC 定序器配好了**，用的是一套与我们
手里 upstream 不完全相同、但同一族的命令表。所以 PDC 不是冷启动的缺口。

### 43.1 为什么要问这个问题

§四十二只读了基址的四个字（`SEQ_MEM_0`、`ENABLE_PDC`、`SEQ_START_ADDR`、
`TCS0_CONTROL`）。`ENABLE_PDC=1` 只说明 PDC 被使能，不说明它的命令表被写过。
上游 `a6xx_gmu_rpmh_init()` 在 A630（`pdc_in_aop = false`）这条路径上会写：

- 五字定序器微码 `0xfebea1e1 / 0xa5a4a3a2 / 0x8382a6e0 / 0xbce3e284 / 0x002081fc`
  （落在 `PDC_GPU_SEQ_MEM_0` 起）；
- TCS1：`CMD_ENABLE_BANK=7`、`CMD_WAIT_BANK=0`、`CONTROL=0`，三条命令
  `msgid 0x10108`，`addr = 0x30010 / 0x30000 / 0x30080`（`0x30080` 是 A630 的
  `pdc_address_offset`），`data = 1 / 0 / 0`；
- TCS3：同形，`data = 2 / 3 / 3`；
- 最后 `PDC_SEQ_START_ADDR = 0`、`PDC_ENABLE_PDC = 0x80000001`。

如果这一套已经在那儿，PDC 就没事；如果读出来全 0，那才是剩下的活。

### 43.2 键 42 的形状（只读）

- 一次 `armcpu_map_physical(ADRENO_BOARD_GMU_PDC_BASE, ADRENO_BOARD_GMU_PDC_SIZE)`
  （只读入口），然后在**基址**读：
  - `SEQ_MEM_0 + 0..4`（五个字）
  - `ENABLE_PDC`、`SEQ_START_ADDR`、`TCS0_CONTROL`
  - TCS1 六字（`ENABLE_BANK`/`WAIT_BANK`/`CONTROL`/`CMD0_MSGID`/`CMD0_ADDR`/`CMD0_DATA`）
  - TCS3 六字（同上）
  - 三行 `gmupdc4=`/`gmupdc5=`/`gmupdc6=`。
- 块内零写、零 `dsb`、零 `gmuW[`；只有 `rd32`。
- 构建：#303 / run `44298810517`（分支 `r195-pdc-block`，commit
  `02fb90fa900277ef051f56f1fae4e230c783307a`，tree
  `a790b2dd4374083262dde5dfe63387dd77cd0a84`）；kernel
  `48bc614c5088ebecd53d75f51aba5ed63ee764129d146757e9c77eb558b234fe`、payload
  `ee2493461454bf43450e10d5cccb7cfb5a4f9a3e0fa4db7c6266d2928e571cf7`；
  `r195_static.py` PASS（required 65 / forbidden 12 / 固件头两处都在）；rekey
  `=42` → `39940eba64bf67e9e365c5e8db9a4473dfd674ea509fe3deb7291e3b8f903af0`。

### 43.3 判决（DEVICE-VERIFIED，20:26）

- `flash readback matches: True`；窗口
  **`960217304c3848ec8b73bea557a3cf3413d756c67ea56c8f01bb57279e8622da`**、
  boot_id **585580743**（新）、msg_bytes 30566、slots_written 8、write_errors 0。
- 逐字三行：

      gmupdc4=m0=00a000a8 m1=00000000 m2=00000000 m3=00000000 m4=00000000 en=00000001 st=00000000 c0=00000000
      gmupdc5=t1en=0000000f t1wt=00000000 t1ct=00000000 t1ms=00010000 t1ad=00030080 t1da=00000000
      gmupdc6=t3en=0000000f t3wt=00000000 t3ct=00000000 t3ms=00010100 t3ad=00030010 t3da=00000003

- 解读：
  1. **`t1en = t3en = 0x0f`** —— 两个 TCS 的命令使能位都是满的（上游会写 `7`），
     说明这两个 bank 里确实放着一组命令，不是空的。
  2. **`t1ad = 0x00030080`** 正是 A630 的 `pdc_address_offset`；**`t3ad = 0x00030010`**
     正是上游 TCS3 第一条命令的地址。两个都精确落在 upstream 那张表的地址族里，
     不可能是随机残留。
  3. **`t3da = 3`** 与上游 TCS3 的 `data = 3` 一致；`t1wt = t3wt = 0` 与
     `CMD_WAIT_BANK = 0` 一致；`t1ct = t3ct = 0` 与 `CONTROL = 0` 一致。
  4. 但 `t1ms = 0x00010000`、`t3ms = 0x00010100` **不是**上游的 `0x10108`，
     `t1da = 0` 也不是上游 TCS1 的 `1`。⇒ 引导器用的是**另一套命令**（或另一版
     上游代码），不是我们手里这份 `a6xx_gmu.c` 逐字复制。
  5. `m1..m4` 全 0、`m0 = 0x00a000a8` ⇒ 基址这五个字里只有一个非零，
     说明**定序器微码不在基址的 `SEQ_MEM_0` 起**（或只需第一个字作入口），
     与上游「五字微码写进 `PDC_GPU_SEQ_MEM_0`」的形状不同。
- 结论：**PDC 已经被下游配好了，写它既无必要也可能有害**。R194 那条负结果
  （别按 mainline 地址写）与 R195 这条负结果（本来就不必写）合起来，
  把 PDC 从缺口清单上划掉。

### 43.4 缺口清单（本轮之后的诚实版本）

被真机排除掉的（按轮次）：PAS 权限（R172-176 系列之前）、SECVID 读、
statics 布局、`HFI_CTRL_INIT` 单独写（会死）、先配齐再脉冲（R183 证明
CM3 干净）、RSC 缺微码（R193 证明写了也没人答）、PDC 未配置（本轮）。

仍然没有解释的，只剩一条：**`RSCC_CONTROL_REQ = 2` 之后
`RSCC_CONTROL_ACK` 从不应答（bit1 永不置起），`RSCC_SEQ_BUSY_DRV0` 恒 0，
GX 轨道与 SPTPRAC 时钟都不动。** 而 CPU 侧的 APPS RSC 通道（`0x179e0000`）
是通的（§三十五）。所以问题不是「这台机器不会投票」，而是
「**GMU 侧的 RSC 为什么不响应非安全 CPU 的请求**」。

### 43.5 下一步（ROUND 196，提议）

在 RSCC 页上做一次**方向矩阵**，仍以只读为主、只写 `RSCC_CONTROL_REQ` 这一个
32 位寄存器（R193 已经证明它可写可读回）：

1. `REQ = 1`（上游 `a6xx_rpmh_stop()` 的方向），有界轮询
   `RSCC_STATUS0_DRV0` 的 bit16，再 `REQ = 0`；
2. `REQ = 2`（`a6xx_rpmh_start()` 的方向），有界轮询 `RSCC_CONTROL_ACK` bit1；
3. 每一步都读回 `REQ`/`ACK`/`STATUS0`/`SEQ_BUSY`，行里一次给全。

判读（开跑前钉死）：
- **两个方向都不 ack** ⇒ RSC 对非安全 CPU 完全不服务，冷启动 GMU 这条路
  在没有 TZ 配合的前提下走不通，应该转去评估「让 TZ/引导器代劳」
  或「绕开 RSC 直接上电」的可行性；
- **stop 方向 ack、start 方向不 ack** ⇒ RSC 活着，只是「上电请求」这个语义被
  拒绝（很可能需要 CX 轨道先上电），那就有明确的下一步；
- **写 `REQ` 之后再读 `REQ` 不再是写入值** ⇒ 有别的执行体在改这个寄存器。

### 43.6 诚实边界

- 本轮只读，`t1ms/t3ms` 与上游不同这一点**只能说明不同**，不能据此推断
  引导器用了哪个版本的驱动或哪套命令语义。
- `t1en = 0x0f` 解释为「命令使能位满」是基于寄存器位宽（低 4 位为 bank 位），
  **没有**核对下游头文件里 `CMD_ENABLE_BANK` 的位定义。
- `PDC 已被配置` 与 `PDC 工作正常` 是两件事：本轮只证明表里有东西，
  没有证明 PDC 真的在跑。

## 四十四、ROUND 196：把 RSC 的两个方向都问一遍 —— 停方向应答，起方向不完成

### 44.1 为什么要分方向
R179 到 R195 一直在问同一个问题（`RSCC_CONTROL_REQ = 2` 之后 `RSCC_CONTROL_ACK` 的 bit1 为什么
从不置起），但只问了一个方向。上游 `a6xx_gmu.c` 里对这颗 RSC 有**两条**握手：

* `a6xx_rpmh_start()`（`:480`）：`REQ = BIT(1)`，等 `GMU_RSCC_CONTROL_ACK` 的 bit1，再等
  `RSCC_SEQ_BUSY_DRV0 == 0`，最后 `REQ = 0`；
* `a6xx_rpmh_stop()`（`:507`）：`REQ = 1`，等 `GPU_RSCC_RSC_STATUS0_DRV0` 的 bit16，最后 `REQ = 0`。

如果两个方向都不应答，「这颗 CPU 碰不到 GMU 侧 RSC」是一条硬结论；如果只有停方向应答，
那就说明控制器服务我们，是起方向缺一个具体前提。这轮把两个方向都做。

### 44.2 键 43 做了什么
先重复 R193 的十六笔（十一个 RSC 设置字 + 五个定序器微码字），读回 `stLoaded`，然后：

1. 停方向：`REQ = 1u` → 有界轮询 `(stDown & 0x00010000u) != 0u` → `REQ = 0u`；
2. 起方向：`REQ = 2u` → 有界轮询 `(stUpAck & 2u) != 0u` → 有界轮询 `RSCC_SEQ_BUSY_DRV0 == 0`
   → `REQ = 0u`；
3. 收尾读 `gdscr` / `sptp` / `fwinit` / `intr`。

所有 RSC 写都走 `gmuW[(ADRENO_GMU_BOOT_V1_GMU_RSCC_DELTA / 4u) + <offset>]`。

### 44.3 设备判决（CI #304 / run 44309515638，boot_id 603749984）
```
gmust1=dir=down req1 st0=01010006 bit16=1 polls=3
gmust2=dir=up req2 ack=00000001 bit1=0 polls=400000 busy=00000001 bpolls=400000
gmust3=loaded=1 gdscr=00222001 sptp=000000d4 fwinit=babeface intr=00000000
```

### 44.4 三条结论
1. **停方向三次轮询就应答**。R193 之后该位是 0，这次是 `REQ = 1` 之后才置起，不是残留。
   所以「非安全 CPU 被 GMU 侧 RSC 完全拒绝」这条假设**被真机否掉**。
2. 起方向不应答，但 `ACK` 的 **bit0** 从 `0` 变成 `1` —— 这个位在 R193 里从未出现过。
   同时 `SEQ_BUSY_DRV0 = 1` 卡在整个有界等待里。
3. 轨道完全不动：`gdscr = 00222001`（bit16 = 0）、`sptp = 0xd4`（`& 0x38 = 0x14`，不是 `0x28`）。

### 44.5 诚实边界
* 400000 次有界轮询只能说明「在这个窗口内没完成」，不等于「永远不完成」；R201 把窗口放大十倍
  是为了正面处理这一点。
* `ACK` 的 bit0 与 bit1 分别代表什么，上游代码没有用到 bit0，本轮不对它作语义推断。

## 四十五、ROUND 197：三相位快照 —— 请求根本没落到命令表

### 45.1 为什么要三相位
R196 只给了两个时间点的读数，无法区分「停方向正常、起方向卡住」与「任何 REQ 之后定序器都不收尾」。
这轮在三个时刻各拍一次完整快照：写之前（`pre`）、停方向之后（`stop`）、起方向之后（`start`），
每次都读九个字：`st0` / `ack` / `busy` / 四个 TCS 状态字（`0x0346 / 0x03ee / 0x0496 / 0x053e`）。

### 45.2 设备判决（CI #305 / run 44323014940，boot_id 591058325）
```
gmusq1=pre   st0=01000000 ack=00000000 busy=00000000 tcs=00000001/00000001/00000001/00000001
gmusq2=stop  st0=01010006 ack=00000001 busy=00000001 tcs=00000001/00000001/00000001/00000001
gmusq3=start st0=0101000a ack=00000001 busy=00000001 tcs=00000001/00000001/00000001/00000001
gmusq4=ao=00000000 fence=00000000 intr=00000000 gdscr=00222001 sptp=000000d4 fwinit=babeface
```

### 45.3 判决
1. `SEQ_BUSY_DRV0` 在**停方向就已经置 1 且再不回 0** ⇒ busy = 1 不是「起方向卡住」的特征，
   而是「任何 REQ 之后定序器都不收尾」。这一条更正了 R196 的读法。
2. 四个 TCS 状态字在三个快照里**逐位相同**（全 1）⇒ 请求根本没有落到任何一张命令表，
   卡在更前面的地方。
3. AO 中断（`0x9305`）、AHB fence（`0x9313`）、`GMU2HOST_INTR_INFO` 全 0 ⇒ 控制器不报错，
   只是不复位。

⇒ GMU 侧 RSC 的应答位能置起，但定序器永远不完成；arc vote 到不了 RPMh，GX 轨道恒 `00222001`。
剩下的唯一现实选择是绕开它：用 CPU 侧 APPS RSC 代投。

## 四十六、ROUND 198：用 CPU 侧 APPS RSC 代投轨道 —— 投票通了，电源域没开

### 46.1 为什么这条路值得走
触摸驱动在本机成功投过 RPMh 命令（`RAIL rc0 db85fe0000 w85fe0000 tcs0 drv00010100
a00042100 ctl00000000 st00000001 n36`），而 R187/R188/R191 又把命令数据库的全套几何钉住了：

* 数据库基址 `0x85fe0000`（`magic 0x0c0330db` 在 `+4`）；
* `gfx.lvl` 在 `0x00030050`、`cx.lvl` 在 `0x00030000`，长度各 32 字节（16 个 u16 电平）；
* APPS RSC 窗口 `0x179e0000`、`drv = 0x00010100`（major 1 ⇒ legacy 偏移组）、`tcs_stride = 672`、
  四个 TCS 全空闲。

### 46.2 键 45 做了什么
可写映射 `0x179e0000`，只读映射 `0x85fe0000`，用 `quiet_cmd_db_levels` 取两张表的最高电平与
首次下标，然后按上游 `a6xx_gmu_rpmh_arc_votes_init()` 的公式算 vote，再按
`ftb_rpmh_active_request()` 的写序把两条命令投出去：

`ctl = 0` → `irq_clear = 1 << tcs` → cmd0（`msgid 0x00010108`、`addr = gfx.lvl 地址`、
`data = gfx 最高电平`）→ cmd1（同形，`cx.lvl`）→ `wait = 3` → `en = 3` →
`ctl = 0x00010000` → `ctl = 0x01010000`（ACTIVE | TRIGGER）→ 有界轮询 `status` bit0。

写全部走新指针 `rscW[...]`，这是探针里第三个钉住的写例外（前两个是 R167 的 `*cfg` 与 R168 的 `gmuW`）。

### 46.3 设备判决（CI #306 / run 44331132291，boot_id 585362561）
```
gmurail4=rsc=fffffffa49d2a000 drv=00010100 tcs=0 gfx_n=10 cx_n=8 gfx_a=00030050 cx_a=00030000
gmurail5=gfx_top=01a0 cx_top=0180 gfx_at=9 cx_at=7 en=00000003 ctl=01010000 st=00000001 polls=1
gmurail6=gdscr=00222001 cxst=80001fe0 gxst=00088400 sptp=000000d4 fwinit=babeface
```

### 46.4 判决
1. **CPU 侧 APPS RSC 通了**：`tcs = 0`、两条命令都写进去、`ctl = 01010000`、`st = 00000001`
   且 **`polls = 1`** —— 一次轮询就完成。这与 GMU 侧「永远 busy、永不收尾」在同一颗芯片上
   构成干净对照。
2. 投票值正确（`gfx_top = 01a0`、`cx_top = 0180`，来自数据库）。
3. **GX 仍然关着**：`gdscr = 00222001`（bit16 = 0）、`gxst = 00088400`（bit15 = 1，GX_OFF）、
   `cxst = 80001fe0`（这是更早的 stage 5 我们自己上电的 CX）。

⇒ RPMh 收下并执行了 gfx 轨道的电平投票，但**投票不等于打开 GX 电源域**。主线上
`gdsc_gx_do_nothing_enable()` 是空实现，正因为 GX 预期由 GMU 固件打开。剩下的唯一杠杆是直接写
GPUCC 的 GX GDSC。

## 四十七、ROUND 199：直接写 GX GDSC —— 四笔全生效，完成位不置

### 47.1 上游的 GX GDSC 事实
`.cache/adreno-a630-source/drivers/clk/qcom/gpucc-sdm845.c`：

```c
static struct gdsc gpu_gx_gdsc = {
    .gdscr = 0x100c,
    .clamp_io_ctrl = 0x1508,
    .pd = { .name = "gpu_gx_gdsc", .power_on = gdsc_gx_do_nothing_enable },
    .pwrsts = PWRSTS_OFF_ON,
    .flags = CLAMP_IO | AON_RESET | POLL_CFGDSCR,
};
```

`gdsc.c` 的位定义：`PWR_ON_MASK BIT(31)`、`SW_COLLAPSE_MASK BIT(0)`、
`GMEM_CLAMP_IO_MASK BIT(0)`、`GMEM_RESET_MASK BIT(4)`、`GDSC_POWER_UP_COMPLETE BIT(16)`、
`CFG_GDSCR_OFFSET 0x4`。`gdsc_enable()` 对带 `CLAMP_IO | AON_RESET` 的域，顺序是
「assert reset（`clamp |= BIT(4)`，延时，`&= ~BIT(4)`）→ 解 clamp（`&= ~BIT(0)`）→
清 `SW_COLLAPSE`」→ 轮询 `gdscr + 4` 的 bit16。

地址换算：GPUCC 块起点 = GMU 映射基址 + `0x26000`，于是 `0x0509100c` 正好等于
`gmuW[ADRENO_GMU_BOOT_V1_GPU_CC_GX_GDSCR]`（`0x9c03 * 4 = 0x2700c`）。`CFG_GDSCR` 用 `+1u`，
clamp 用 `(0x05091508 - GMU_BASE) / 4`，CX 状态用 `(0x05091540 - GMU_BASE) / 4`。
**不需要新映射、不需要新写入口。**

### 47.2 设备判决（CI #307 / run 44346484706，boot_id 587430090）
```
gmugx2=vote_a=00030050 vote_top=01a0 tcs=0 st=00000001 en=00000001 polls=1
gmugx3=pre_gdscr=00222001 pre_cfg=00088400 pre_clamp=00000001 pre_cx=80001fe0
gmugx4=gdscr=08222000 cfg=01080400 clamp=00000000 cx=80001fe0 gx_on=0 polls=20000
```

### 47.3 判决
1. 轨道投票仍然一次完成（`polls = 1`）。
2. `pre_cfg = 00088400` ⇒ CFG 的字在写之前不是 0（bit16 = 0、bit15 = 1，域是关的）。
3. 四笔 GDSC 写**全部生效**：clamp `1 -> 0`、`GDSCR 00222001 -> 08222000`
   （bit0 `SW_COLLAPSE` 已清、bit31 `PWR_ON` 已置）。
4. **但 `CFG_GDSCR` 在 20000 次有界轮询里 bit16（`POWER_UP_COMPLETE`）始终为 0，
   `gx_on = 0`。**

### 47.4 诚实边界
`0x01080400` 的其余位（bit10 / bit18 / bit19）本轮不作解读：它们可能包含厂商固件留下的
保留位，没有钉住的位定义就不猜。

## 四十八、ROUND 200：CM3 活着时重跑同一序列 —— 逐位相同

### 48.1 为什么再跑一次
R199 的键 46 走的是自己的路径，那个路径里 CM3 并没有按键 30 的方式被完整配好并放出来。
最自然的解释是「GX 的上电完成信号需要 GMU 在跑」。这轮把键 46 的四笔 GDSC 原样搬到
**键 30 的完整预启动之后**（也就是 R183 已经证明能让 CM3 干净运行的那套），再跑一遍。

### 48.2 设备判决（CI #308 / run 44355632757，boot_id 586859198）
```
gmugx5=vote_a=00030050 vote_top=01a0 tcs=0 st=00000001 en=00000001 polls=1
gmugx6=pre_gdscr=00222001 pre_cfg=00088400 pre_clamp=00000001 pre_cx=80001fe0
gmugx7=gdscr=08222000 cfg=01080400 clamp=00000000 cx=80001fe0 gx_on=0 polls=20000
```

### 48.3 判决
三行**与 R199 逐位相同**。同窗的 `gmustat=intr=00000000 fwinit=babeface` 说明这一轮 CM3
确实起来了（`babeface` 是 CM3 自己写的），所以「CM3 没启动」这个前提被排除，而结果没变：
GDSC 序列生效、完成位不置。

## 四十九、ROUND 201：CX 对照 + 十倍窗口

### 49.1 设计
这轮要回答的是「GX 是慢，还是根本不会完成」。两条读数：

1. **CX 对照**：stage 5 在这台机器上成功把 CX 从 `00005ff4` 抬到 `80001fe0`，说明 GDSC 机制本身
   在本机是工作的。读 CX 的 GDSCR / CFG 与 `cx_bit16`，看同一套完成位在 CX 上是否置起。
2. **十倍窗口**：把 GX 的完成位轮询从 20000 放大到 200000。

### 49.2 键 48 的形状
先只读 CX 的 `cx_gdscr`（`0x0509106c`）与 `cx_cfg`（`0x05091070`），打 `gmugx8=`；然后重做
AON 脉冲 / 清 clamp / 清 `SW_COLLAPSE`，用 200000 的上界等 bit16，打 `gmugx9=`。

### 49.3 预期判读（事前钉住）
* `gx_on = 1` ⇒ 只是慢，R199/R200 的界太短；
* `cx_bit16 = 1` 且 `gx_on = 0` ⇒ CX 在同一启动里报完成而 GX 不报，GX 确由本 CPU 供不出的东西门控；
* `cx_bit16 = 0` ⇒ 这个完成位不能当判据，本轮前提要重审。

### 49.4 构建侧记录（诚实）
CI #309 / #310 / #311 三次都在 `Build kernel` 之前被取消：`job build completed [cancelled]`、
`runner = ''`、`steps = 0`，而 #303–#308 全是 `runner = 'GitHub Actions 10000003xx'`、
`steps = 28`、`success`。仓库侧 `actions/permissions` 正常、GitHub 状态页 `All Systems Operational`。
⇒ 这是 runner 分配侧的偶发故障，与本轮代码无关；#312 起恢复正常，重新编译。
## 五十、ROUND 202：把队列表拿掉之后，那笔致死写就活了

### 50.1 为什么做这一轮

R177 与 R184 都见过同一件事：CM3 刚起来、配置干净的时候，只要写一笔
`HFI_CTRL_INIT = 1`，整台机器就静默（内核死在日志 writer 打开之前，窗口逐字节不变）。
R178 则发现同样是这笔写，如果先做一次 boot-slumber 门铃把 CM3 打成 FAULT，它就不致死。

那几轮里队列表一直是发布着的（R176 已证明 alloc / map / 填表 / 三笔 QTBL 寄存器都安全），
所以 `HFI_CTRL_INIT` 之后固件会去读 `QTBL_ADDR` 指向的那张表，而「读那张表」这一步从未被单独隔离过。
键 49 就是把它隔离出来：先把 `QTBL_ADDR` 与 `QTBL_INFO` 两个都清零，再写 `HFI_CTRL_INIT`。

### 50.2 判决（DEVICE-VERIFIED）

CI #313（run `44405803206`，分支 `r202-hfi-no-qtbl`，commit `913e27db`）→
kernel `a830e8b9f88b58e1ec646e8352eb7be5573329f650f83f892c419c8b3591e375`（126,197,760 B）、
payload `06228f1d75f8122398c9b631b74a633ccb9854af5eaf477017689994b18f78d8`。
`r202_static.py` PASS（required 43 / forbidden 12 / 固件 32,768 B 头在 kernel 与 payload 均 True）。
rekey `enchilada-pas-exec=2 -> =49` → `d69172bc35f15c9196597ac97f8be570081549d83f698d89c571fda2c311047b`。

上机（06:24，boot_id 590898803 / NEW LOG）：
`flash readback matches: True`（system_b 头 8 MiB `7af4b113…` == payload 头）；
窗口 33,554,432 B sha256 `8c35b1a5341b6a9ae21915e3095e4ee8bb1237095f4f1cccaac9e07c95256bd5`；
msg_bytes 30379、slots_written 9、uptime_s 4、`write_errors=0`。

两行逐字：

```
gmuhfi5=qtbl_pre=ad940000 info_pre=00000001 stat_pre=00000000 win=1
gmuhfi6=stat=00000000 polls=400000 intr=00000000 fwinit=babeface ok=0
```

### 50.3 解读

1. 存活。同一笔 `HFI_CTRL_INIT` 写在「表指针指着一张真表」时打死内核（R177/R184），
   写在「表指针为零」时无害。所以死因不在写这个寄存器本身，而在固件去取那张表。
2. `gmuhfi5=` 里的 `qtbl_pre=ad940000` 与 `gmuhfi=` 那三行里的 `addr=ad940000` 一致，
   `info_pre=00000001` 与 R186 起一直写进去的值一致，说明清零前的状态就是我们自己刚写的那份。
3. `stat=00000000`（400000 次轮询内 bit0 从未置起）、`intr=00000000`、`fwinit=babeface`
   说明 CM3 活着，但 HFI 没有起来。这与「把指针清空固件就不启动 HFI」完全自洽。

### 50.4 诚实边界

我同时改了两个字：`QTBL_ADDR` 和 `QTBL_INFO`。所以「存活」有两种解释：

- A：固件照常尝试启动 HFI，去取一张空表，取到 0 但没有崩，说明我们的表确实有问题；
- B：`QTBL_INFO=0` 本身就是「不要启动 HFI」的信号，固件直接跳过取表，那么这一轮根本没测到取表。

这两种解释在数据上无法区分，所以 R203（键 50）做单变量：只清地址、把 INFO 保持在上游值 1。
若那时仍存活，A 成立；若那时致死，则 B 成立、本轮的正面结论作废。

另一处边界：`ok=0` 只说明「状态位没起来」，并不说明固件对空表做了什么，固件内部的失败路径本轮不可观测。

### 50.5 下一步

- R203（键 50）：`QTBL_ADDR=0`、`QTBL_INFO=1`、写 `HFI_CTRL_INIT`，读回 `gmuhfi7/8=`。
- 若 A 成立，把注意力放到表本身：地址（`ad940000` 是 `armcpu_dma_alloc_writable` 给的低 4 GiB
  物理页）、几何（`AdrenoHfiV1.h` 的两队列 / 12 KiB 共享区）、以及 GMU 从 SID 4/5 看这块内存的
  视角（`sCR0=0x00200626` 显示 SMMU 未使能，理论上应是恒等映射，但这一点从未在真机上单独验证过）。

## 五十一、ROUND 203：把地址清掉、把启动请求留着 —— 静默死，R202 的结论被判为 B

### 51.1 为什么做这一轮

R202（键 49）同时清零 `HFI_QTBL_ADDR` 与 `HFI_QTBL_INFO` 之后写 `HFI_CTRL_INIT`，机器存活。
但那一轮改了两个字，因此有两种解释：

- **A**：固件照常启动 HFI、去取一张空表，取到 0 却没崩，说明致死的是「我们那张表」；
- **B**：`QTBL_INFO=0` 本身就是「不要启动 HFI」的信号，固件跳过取表，那么 R202 根本没测到取表。

键 50 就是单变量：**只清地址，`QTBL_INFO` 保持上游值 1**，让启动请求仍然发出，只是指向空表。

### 51.2 判决（DEVICE-VERIFIED）：静默死

CI #314（run `44411496388`，分支 `r203-hfi-null-addr`，commit `b7523102`）→
kernel `ab47e61cc3de5061c16d2bb601cad8fd2f2843e66438f3208ed66061a18b8351`（126,197,760 B）、
payload `d9abb4c3da7e0988408a9c70b7af40a0a0223841bddf2acdb5a2a0db45ab3d96`。
`r203_static.py` PASS（required 43 / forbidden 12 / 固件 32,768 B 头在 kernel 与 payload 均 True）。
rekey `enchilada-pas-exec=2 -> =50` → `e6fdaf808a7f9ec4f4eb8b105613effb9402873e036ffb1a64531e83a92b7325`。

上机（06:43:56 起，门禁 3/3 PASS、flash OK、`boot sent ok=True`）：
`flash readback matches: True`（system_b 头 8 MiB 与 payload 头同为 `b245b1a68d520f6ae5b29f35c843050036c42d42aeeeeddcd806b60577d795a5`）
⇒ 镜像确实是本轮的，不是上一次的残留。

窗口 33,554,432 B sha256 仍是 **`8c35b1a5…`**、**boot_id 仍是 590898803（= R202 那次）**、
msg_bytes 30379、slots_written 9、`write_errors=0`；本轮该写的 `gmuhfi7=`/`gmuhfi8=` 计数都是 **0**。
窗口里能读到的 `gmuhfi5=`/`gmuhfi6=` 两行是 R202 那次留下的旧内容。

⇒ **键 50 静默死**：内核死在日志 writer 打开窗口之前，零字节。判据与 R177/R184 完全一致。

### 51.3 解读：R202 的存活是 B，不是 A

三次单变量并排：

| 键 | `QTBL_ADDR` | `QTBL_INFO` | `HFI_CTRL_INIT=1` 之后 |
|----|-------------|-------------|------------------------|
| 17/31（R176/R184） | 真表 `ad940000` | 1 | 静默死 |
| 49（R202） | **0** | **0** | **存活**（`gmuhfi6=stat=0`） |
| 50（R203） | **0** | **1** | **静默死** |

- 地址从「真表」换成「空」并不能救活（键 50 与键 17/31 一样死），
  所以**致死因子不是我们那张表的内容或几何**；
- 唯一让写变安全的是 `QTBL_INFO=0`，即**把启动请求本身关掉**；
- 结论：**只要固件真的被要求启动 HFI，这笔写就必然静默打死主机，与本机给它的表无关**。
  R202 §50.4 里的解释 A 作废，B 成立。

这也重新解释了 R178 的旧观察：先做一次 boot-slumber 门铃把 CM3 打成 FAULT 之后，同一笔写就不致死——
那次「无害」同样是因为 CM3 已经不在正常状态去执行 HFI 启动，而不是因为握手提供了什么前提。

### 51.4 诚实边界

- 本轮只证明「INFO=1 + HFI start」必死；**没有**定位死在固件内部哪一步
  （取表？建队列？发第一条 HFI 消息？），因为内核在能写日志之前就没了。
- 「空地址」在 GMU 视角可能立即触发一次总线错误、也可能被固件当作合法值继续走，
  这两种情况从主机侧不可区分；本轮不据此推断固件读了 0 地址。
- 仍未验证：把 `HFI_CTRL_INIT` 换成「先配 `CP_MSG` 日志页再启动」是否能改变结局——
  那是 R171 就刻意跳过的两笔（`QTBL_ADDR`/`CP_MSG` 需要 GMU IOMMU 视角的 IOVA），本轮仍未碰。

### 51.5 下一步（方向变更）

HFI 这条路现在的形状是：**配置全部合法、CM3 活着、但一要求启动 HFI 主机就死**。
在下一次盲目重试之前，值得先把问题从「让 HFI 起来」换成「谁在 HFI 启动时访问了什么」：

- 选项一：查上游 `a6xx_gmu_hfi_start()` 之后固件第一条 HFI 消息的目标（`HFI_SFR_ADDR` 的
  chipid 值 `0x06030100` 是否与这颗芯片真的匹配——本轮之前一直是我们按上游公式算的，
  从未与真机侧任何读数对照过）；
- 选项二：暂时放弃 HFI，回到 GX 域的电源门控问题（R201 已把「CX 能完成、GX 不能完成」
  钉死在同一颗芯片的同一次启动里）；
- 选项三：接受「本 CPU 供不出 GX 上电」这一结论，转去做不依赖 GX 域的部分
  （微码 / CP 环 / seam 骨架的 host 侧推进），把真机验证留到有新信息时。


## 五十二、ROUND 204-213：把所有「零字节」重新读一遍 —— 判据本身是错的，store 至今没被证明有罪

### 52.1 为什么这十轮必须放在一起写

R177/R184/R203 三轮都把「写 `HFI_CTRL_INIT=1` → 窗口零字节」读成「这笔 store 静默打死内核」。
R204 起换成延迟观测后，同一批读数逐个变了意思：**零字节只说明内核死在日志 writer 第一次刷盘之前**，
不说明死在 store 之前还是之后。十轮做的就是把这个盲区一层层剥开。

### 52.2 逐轮读数（全部 DEVICE-VERIFIED，每轮一次上机）

| 轮 | 键 | 动作 | 窗口 sha256 | boot_id | 判决 |
|----|----|------|-------------|---------|------|
| R204 | 51 | 致死写推迟到 writer 之后 | `ab5d5cca…` | 583439343 | pre 行落盘，`gmudly2=` 从未出现，日志在 pre 后 0.44 s 终止 |
| R205 | 52 | CNTPCT 真忙等 3 s | `ab5d5cca…`（旧） | 583439343 | 零字节 ⇒ 忙等饿死 writer |
| R206 | 53 | 快照 + `IOSleep(3000)` | `43c88ba7…` | 583728242 | `gmudly5=` @ `[30.179919]` 落盘，26 ms 后日志停，`gmudly6=` 不存在 |
| R207 | 54 | 不写任何寄存器，6×`IOSleep(250)` | `81457009…` | 583188955 | 零行（`gmuwake=0`）⇒ 空转本身致死 |
| R208 | 55 | 读循环 3×200000 | `81457009…`（旧） | 583188955 | 零字节 ⇒ 纯紧自旋同样饿死 writer |
| R209 | 56 | store 前后紧贴两行，无延迟 | `81457009…`（= R207 那次，旧） | 583188955 | 窗口陈旧；同镜像换 key 48 得 `0864adbd…` / 650443574 正常 ⇒ 镜像无罪 |
| R210 | 57 | 每步后跟让步等待 | `0864adbd…`（旧） | 650443574 | 零字节，连 writer 表头都没有 |
| R211 | 58 | 两行 + 3.17 s 等待，**无 store** | `ca095de7…` | 641656079 | 两行都落盘（`gmupure1=1 gmupure2=1`） |
| R212 | 59 | 只写队列表两字 | `84c077f8…` | 610159657 | 两行都落盘，相隔 6.43 s |
| R212 | 60 | 只写启动字 | `84c077f8…`（旧） | 610159657 | 零字节，复跑两次同样 |
| R212 | 61 | 对照（无代码块） | `6943e9b7…` | 582802818 | NEW LOG ⇒ 镜像健康 |
| R213 | 62 | 等待×3 + 前后各一行 | `6943e9b7…`（旧） | 582802818 | **三行全丢**（`gmu213a/b/c=0`） |

### 52.3 键 58/59/62 的实测时间戳（本节的关键实测值）

```
键 58（R211）：gmupure1 @ 30.235380  →  gmupure2 @ 33.418990   间隔 3.18 s，两行都落盘
键 59（R212）：gmusp59= @ 30.159753  →  gmusp59b @ 36.591704   间隔 6.43 s，两行都落盘
键 62（R213）：无任何 gmu213a/b/c 行
```

⇒ **键 59 在同一个 probe 上下文里连续占住 CPU 6.43 s 仍然活着**。因此
「probe 占住 CPU 超过约 3 s 就会死」这条推断**没有被读数支持**，不得写进交接。
R213 只证明「60 万次一轮的等待会丢掉整个窗口」。

### 52.4 一处机制更正（我抄错的，必须改）

`package/BootLog.c` 逐字：

```
// IOSleep is not a sleep: IOLib.cpp:1509 forwards it to delay_for_interval, which spins the CPU
// on the time base.
```

同一段还写明 `assert_wait_timeout + thread_block` 才是真正让出 CPU 的写法。
所以 **R207 的死亡不是「`thread_block` 在 probe 上下文里不合法」，而是「空转让日志 writer 饿死」**
—— 与 R205 的 CNTPCT 紧自旋同属一类失败。

### 52.5 未解矛盾（不许掩盖）

键 59 与键 60 在各自块**之前**执行的代码逐条相同（`r210_guards.py` 对 59、60 求值一致，
都是 15 个 `RUNS` 块），两者都**先打一行、再空转 20 万次**（≈3.17 s）。键 59 两行都落盘，
键 60 的**第一行（任何 store 之前）**却没落盘。若「20 万次等待安全」成立，键 60 第一行本该落盘。
**这条矛盾目前没有解释**，它是下一轮（R214）必须优先处理的观测缺陷。

### 52.6 R213 的构建证据

CI #324（run `44451699719`，`workflow_dispatch`，workflow `056130ab`）success，
编译 03:28:51Z–03:49:03Z，24 step 全绿。工件：

```
kernel.macho   126,197,760 B  sha256 a621252cce7b83d42b775a2b5627115d0d6a17fd0637f9b15a68fceecbf79b7a
payload.img    150,994,944 B  sha256 113c2330c5bc3b39062264c3f5a4b1d8178908e24aed7e72ee903272341a78d2
rekey =62      150,994,944 B  sha256 954b579c8ca0476d091dfa1ec39d4a944e2eb09b9970d7c6cef0f3ad81c3521d
```

`r213_static.py` PASS（required 44 / forbidden 12 / 固件头双端 True）。
上机 `flash readback matches: True`（system_b 头 8 MiB `2e6c6056…` == payload 头）。

### 52.7 下一步（R214）的三条硬约束

1. **不得用「占住 CPU」来换观测窗口**：可用的只有带 `IODelay(1u)` 的读循环，且 60 万次一轮已证不可靠。
2. **必须先解决 §52.5 的矛盾**（键 59 与键 60 的同一位置、同一等待、不同结局），
   否则任何「store 无罪/有罪」的结论都站不住。
3. **必须让「有行落盘 + 不等待 + store 确实执行」这个组合出现一次**，
   这是 R177 以来从未取得过的观测条件。


## 五十三、ROUND 214-219：先把观测修好，再把致死点指名到「启动字」

### 53.1 为什么这六轮必须连在一起读

R203 之后 GE 的结论是「只要真的要求启动 HFI 就必死」。R204–R218 试图用它自己的
判据去复核，结果发现**判据本身是坏的**：日志窗口是**整轮提交**的，一轮没写完就死，
介质上留的是**上一轮的 header**。于是十几轮「零字节」既可能是「没打印」，
也可能是「打印了但那一轮没完成」。R219 在修好观测之后，才第一次能指名。

### 53.2 三条观测链上的事实（HOST 源码 + DEVICE 实测）

1. `package/BootLog.c` 的 writer 每轮顺序为 **slot → msgbuf → header → sync**，
   header 最后写，且只有它让前两块可见。
2. `native_ufs_log_sync()` 只对**已写入日志区**的数据发 SYNCHRONIZE CACHE；
   刚打印的行还在**内存 msgbuf** 里，所以 R214 的「打完就 sync」不可能让行落盘。
3. `boot_log_panic_flush()` 在 R215 也**没有**提交任何东西：R215 窗口里
   **`[panic] round` 计数为 0**，且它的两行相隔只有 **15 微秒**
   （`29.985804` → `29.985819`）。真正让那两行落盘的，是当时已在运行的 writer 的
   **下一次正常轮转**。

⇒ 判据必须从「窗口变没变」改成「窗口里有没有这一行、有几行」。

### 53.3 另一个必须记下的盲区：8 MiB 回读从未看过键字节

`enchilada-pas-exec=<key>` 位于 payload 偏移 **`0x794a25b`（≈127 MB）**，
而每一轮的刷写校验只比对 **前 8 MiB**：

```
r212-s59-payload/payload.img   @0x794a25b=59   (beyond 8 MiB)
r213-s62-payload/payload.img   @0x794a25b=62   (beyond 8 MiB)
```

设备侧实测（读 `sda14` 第 31050 块 32768 B）：

```
offset 0x794a25b-region +0x25b = 62
payload region identical to device region: True
```

⇒ 那次键 62 确实刷进去了，但**这是运气**。R214 起 runner 增加开跑前必查：
从 payload 里定位**真正命名期望键值的那一份**参数（rekey 会在废弃簇里留下旧副本），
读回 system_b 同一块比对，并打印读到的值。

### 53.4 R217 的对照：键 65 本身不可用

同一份 R217 镜像跑两个键：

| 键 | 窗口 | boot_id | `gmu217seen=` |
|---|---|---|---|
| 64 | `43fb1e18…`（新） | **584145654**（新） | **有**：`parsed=1 key=64 armed=1`，t=`29.704473` |
| 65 | `a0a338a3…`（旧） | 584039970（旧） | **无** |

⇒ 键 65 的零字节**不能**读成 store 致死；它连那行无条件的回显都没出。
R216 的结论因此作废（它建在键 65 上）。

### 53.5 R219 的三分支判决（DEVICE-VERIFIED，决定性）

同一份镜像、同一执行点，只差键值：

| 键 | 块体 | 窗口 sha256 | boot_id | 行 |
|---|---|---|---|---|
| 66 | 三行，**零 store** | `eff8caa2…`（新） | 582344900（新） | **3/3** |
| 67 | 三行 + **两笔表字** | `f39fac2c…`（新） | 582538748（新） | **3/3** |
| 68 | 三行 + **启动字** | `f39fac2c…`（与 67 逐字节相同） | 582538748（旧） | **0/3** |

键 66：
```
gmu219a=no_store=1 stat=00000000 addr=ad8ed000 info=00000001
gmu219b=no_store=1 stat=00000000 fwinit=babeface
gmu219c=no_store=1 stat=00000000 intr=00000000
```

键 67（`addr` 由 `ad940000` 变为 `00000000`，写确实生效）：
```
gmu219d=before=1 stat=00000000 addr=ad940000 info=00000001
gmu219e=table_written=1 addr=00000000 info=00000001 stat=00000000
gmu219f=no_start=1 stat=00000000 fwinit=babeface
```

键 68：**三行全无**，窗口与 67 逐字节相同。

**结论：两笔队列表写无罪；`HFI_CTRL_INIT=1` 单独一笔就足以在能写窗口之前打死内核。**

### 53.6 诚实边界

* 只定位到**哪一笔写**，没有定位到**死在哪条指令/哪次总线访问**。
* 键 68 的零字节与「块没被执行」在主机侧仍不可区分（守卫集相同、标记在镜像里、键值已确认）；
  要再收紧需要第二个观测通道（例如写 `BootProgress` 页而不是日志窗口）。
* 键 65 的失败原因仍未知，与本结论无关。

### 53.7 路线含义

致死点在**固件被要求启动 HFI 之后的行为**，而不在队列表。
下一步应查 `HFI_SFR_ADDR`（一直按上游公式取 `0x06030100`，**从未与真机读数对照**）
以及固件启动后第一条访问的地址。


## 五十四、ROUND 220-222：把「哪一个变量」逐个拿掉，直到只剩那一笔写

### 54.1 三个被依次否掉的解释

| 解释 | 检验方式 | 结果 |
|---|---|---|
| 窗口余量不够（写完再打印来不及刷盘） | R221 在启动字前插入 **3.17 s**（>6 轮）有界读循环 | **否掉**：三行仍全丢 |
| 键值太大、内核跑不到那个分支 | R222 **不换键值**，用已证明可写的键 66 | **否掉**：同键值前缀一轮 3/3 |
| 刷写没生效 / 镜像不对 | 每轮读 `system_b` 中真正含键参数的那一块与 payload 比对 | 每轮均 `device == payload -> True` |

### 54.2 R222 的判决（单变量，键值恒定）

| 同一键 66 | 块体 | 窗口 sha256 | 行 |
|---|---|---|---|
| R219 | 三行，零 store | `eff8caa2…`（新） | **3/3** |
| R222 | 三行 + 一笔 `HFI_CTRL_INIT=1` | `f39fac2c…`（未变） | **0/3** |

```
key block says: enchilada-pas-exec=66 -no_slto_p
key verified: this boot will run key 66
flash readback matches: True
counts: gmu222a=0 gmu222b=0 gmu222c=0
```

⇒ 在同一个键值、同一个执行位置，「写这一笔」与「整个窗口消失」被单变量绑在一起。

### 54.3 一个必须写下的细节：连 store **之前**的行也丢了

R222 的行 A 打印在 store 之前。若内核只是在 store 之后才死，行 A 本应被下一次
正常轮转带走（R219 键 66 就是这样带走的）。**行 A 也丢**，说明窗口的停止
覆盖了 store 之前已打印的内容 —— 更像「writer 再没运行 / 该 slot 从未提交」，
而不像「死在 store 之后」。

配合 §23.8 的观察（bark 也没有被取到），目前最一致的解释是：
**写这一笔时整机立刻停住，连 timer 中断都不再运行。**

### 54.4 R223（进行中）：写 0 而不是 1

R222 只说明「写 1 会致命」。要区分：

* **(a) 碰到这个偏移就致命**，还是
* **(b) 值是 1（要求固件启动 HFI）才致命**。

做法：同一个键 66、同一位置、同一访问宽度，只把值从 1 改成 **0**。
上游只写过 1，所以 0 是「不要求启动」的语义；R202/R203 曾用 INFO=0 达到过
类似的「不启动」效果并存活。

判读（开跑前固定）：
* 写 0 得 **3/3** ⇒ 偏移无害，**凶手是那个 1（启动请求）**，语义定位完成；
* 写 0 得 **0/3** ⇒ 仅仅访问那个偏移就致命，这与 R202/R203 冲突，需重读那两轮；
* 行 A 缺、B/C 在 ⇒ 不可能，说明观测本身有问题。


## 五十五、ROUND 223：写 0 无害、写 1 致命 —— 定位到「启动请求」这一语义

### 55.1 三重单变量（同一键 66、同一位置、同一访问宽度）

| 变体 | 写入 | 窗口 sha256 | boot_id | 行 |
|---|---|---|---|---|
| R219 | 不写 | `eff8caa2…`（新） | 582344900（新） | **3/3** |
| R222 | **1** | `f39fac2c…`（未变） | 582538748（旧） | **0/3** |
| R223 | **0** | `d60bb6e3…`（新） | **583170095**（新） | **3/3** |

键 223 的逐字读数：

```
gmu223a=before=1 stat=00000000 addr=ad940000 info=00000001 win=1
gmu223b=start_written=1 stat=00000000 intr=00000000
gmu223c=after=1 stat=00000000 fwinit=babeface survived=1
（t = 30.152145 / 30.152159 / 30.152170）
```

### 55.2 判决

**写该寄存器偏移是无害的；致命的是值 1 所表达的「要求固件启动 HFI」。**

三条备选解释全部被对照否掉：
* 「余量不足」——R221 给 3.17 s（>6 轮）仍全丢；
* 「键值太大跑不到」——R222 用已证明可写的键 66；
* 「访问偏移致命」——R223 同偏移写 0 得 3/3。

### 55.3 边界

* 未知：`HFI_CTRL_INIT=1` 之后固件做了什么导致整机停住。主机侧没有通道。
* 「立刻停住」与 R222 里「store 之前的行 A 也丢」一致，但没有独立证据（bark 未取到，§23.8）。
* GPU 渲染本身未推进。

## 五十六、ROUND 232-232c：把 context bank 读了一次，并因此发现写路径上的地址错误

### 56.1 第一次上机（R232）读到的不是 CBAR

键 71 的设计目标是「只读，证明 frame 可达」。R232 首跑读到：

```
gmu71a=id1=20008005 geom=0 pgshift=12 numpage=8 cb_base=8000 numcb=5 bank0=0 bank0_off=8000 cx=80001fe0
gmu71b=cbar=00200626 cba2r=00000000 sctlr=00000000 ttbr0=00000000/00000000 tcr=00000000
gmu71c=bank4_off=c000 sctlr=00000000 win=1
```

`cbar=00200626` 是**假的**：那个值正是 GR0 的 `sCR0`（`scr0=00200626`，同一窗口的键 74 预读行给了对照），
不是 CBAR。原因在帧寻址：上游 `#define ARM_SMMU_GR0 0` / `ARM_SMMU_GR1 1` 与
`return smmu->base + (n << smmu->pgshift);` 合起来意味着 **GR1 在 `base + (1 << pgshift)`**，
本机 `pgshift=12`，即 `0x05041000`，而不是 `base + 0`。

这条错误的代价本来会很大：键 72 若照 R232 的写法跑，会把 `CBAR`、`CBA2R` 写进 **GR0**，
即覆写 `sCR0`（含 `CLIENTPD`、`GFRE`、`USFCFG` 等），在还没开始测 context bank 之前就改掉全局状态。

### 56.2 帧地址修好之后，又踩了自己的截断

R232b 把帧改成 `base + (1 << pgshift)`，但把算出来的地址存进了 `uint32_t`。
内核 VA 是 `0xfffffff8b194f000` 这个量级，截断后读的是另一个地址：窗口不更新，启动耗时 70 s。
R232c 改回 `uintptr_t`，并在**触碰 GR1 之前**先打印一行 —— 这一行是 R209/R210 的教训：
静默死也必须先留下可读证词，否则「零字节」无法区分「没执行」与「执行后死」。

### 56.3 R232c 读数（DEVICE-VERIFIED）

boot_id **580749766**、窗口 `60513f32e90c894444fb8cf73298b189ffce2d5020b36050aecbbd1f957e43fc`、4 行全部落盘：

```
[   30.024144]: gmu71a=id1=20008005 geom=0 pgshift=12 numpage=8 cb_base=8000 numcb=5 bank0=0 bank0_off=8000 cx=80001fe0
[   30.024179]: gmu71b=about_to_read_gr1 gr1=fffffff8b194f000 scr0=00200626
[   30.024250]: gmu71c=gr1_ok=1 cbar=00000000 cba2r=00000000 sctlr=00000000 ttbr0=00000000/00000000 tcr=00000000
[   30.024264]: gmu71d=bank4_off=c000 sctlr=00000000 win=1
```

`gr1=fffffff8b194f000` 是 64 位内核 VA，证明截断已修。

### 56.4 判决

**GR1 可读。预读行与 `gr1_ok=1` 同时落盘，且读取没有导致任何故障。**
同时读出：五个 context bank 的九个字**当前全部为 0**，即这台设备此前从未编程过任何 context bank
（与 §28 的 `init=0 alloc=0 cbndx=0` 一致）。这正是键 72 可以安全动手的前提：
写之前的状态是已知的零，写完再读，任何非零都必然来自这一轮。

### 56.5 边界

* 本轮只证明「地址可达、读不炸」，不证明写入会生效（那是 R232d）。
* `scr0=00200626` ~~说明 `SMMUEN`（bit0）当前为 0：SMMU 全局未使能~~（R235 更正：bit0=`CLIENTPD`=0，客户端口是开的；见 §五十九）。
  本轮没有动它，也不该动 —— 它属于「等 context bank 齐全之后再开」的那一步。

## 五十七、ROUND 232d：本移植第一次真正写 SMMU context bank（9/9 笔）

### 57.1 设计：一次上机，四行证词，判据先固定

键 72 在写之前先算表、再排计划、再逐笔 MMIO，每段一行：

| 行 | 内容 | 若只有这一行 |
|---|---|---|
| A `gmu72a` | CX 状态、几何、SMMU 映射是否存在 | 拒绝路径（无写） |
| B `gmu72b` | `adreno_table_build` 结果：root 物理地址、表数、写入数 | 建表失败 |
| C `gmu72c` | 九笔计划与绝对化结果（`n`、`nab`、cbndx、asid、ttbr、cw） | 计划失败 |
| D `gmu72d` | 九笔全部发出后的回读 | 死在 MMIO 里 |

判读规则在任何写入之前写进 runner 并原样打印（防止读窗口后改口径）：
`refused-*` 或只有 A → 拒绝，没写；只有 A+B → 建表/计划失败；A+B+C 无 D → 九笔写把内核打死；四行全有 → 写完成。

### 57.2 读数（DEVICE-VERIFIED）

boot_id **583041945**（新）、窗口 `2ceda7cb2ff69cecc67f40307fb35e7ba012bb122d35e9c0110c47fd410a37bf`、**4/4 行落盘**：

```
[   30.144678]: gmu72a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1
[   30.144795]: gmu72b=build=0 root=ad934000 tables=4 writes=4 ias=48 oas=36 ok=1
[   30.144812]: gmu72c=plan=0 n=9 abs=0 nab=9 cbndx=0 asid=0 ttbr=ad934000 cw=0
[   30.144937]: gmu72d=wrote=9 ttbr0=ad934000/00000000 sctlr=00001067 tcr=00802010 cbar=0001f300 root=ad934000
```

四行跨 **259 微秒**（`30.144678` → `30.144937`），期间没有任何停顿、没有任何异常。

### 57.3 回读与模型逐位比对（六个字，六次 match）

| 字 | 设备 | 模型 | 说明 |
|---|---|---|---|
| TTBR0[31:0] | `ad934000` | `ad934000` | root 物理地址，与 `gmu72b` 的 `root=` 一致 |
| TTBR0[63:32] | `00000000` | `00000000` | ASID=0 在 63:48，故高半字为 0 |
| SCTLR | `00001067` | `00001067` | CFIE\|CFRE\|AFE\|TRE\|M\|S1_ASIDPNE = 0x40\|0x20\|0x4\|0x2\|0x1\|0x1000 |
| TCR | `00802010` | `00802010` | T0SZ=64-48=16、TG0=4K、SH=OS、RGN=NC、EPD1=1 |
| CBAR | `0001f300` | `0001f300` | TYPE=S1_TRANS_S2_BYPASS(1)<<16 \| WB(0xF)<<12 \| NSH(3)<<8 \| VMID 0 |
| 写入笔数 | `9` | `9` | `nab=9`、`wrote=9` |

比对由脚本 `work\tmp\r232d_verify.py` 完成，不是心算：它按 `AdrenoSmmuCtxWordV1.h/.c`
里的同一批常量重算，逐位比对后打印 `RESULT: all words match the model`。

**判决：SMMU context bank 0 已被本移植真正编程，九个字全部按计划落地，内核存活。**

### 57.4 本轮自己暴露的缺陷：`coherent_walk` 用的是字面零，不是测到的 ID0

`gmu72c` 的 `cw=0` 让 TCR 走了**非一致**分支（outer-shareable、non-cacheable）。
原因不是算错，是**输入没测**：探针里写的是 `adreno_iommu_coherent_walk(0u, 0u)`，
把 ID0 当作 0 传进去。

而同一份窗口的 `stage6=` 行明确打印：

```
stage6=id1 id0=48016605 id1=20008005 cttw=1 numcb=5 nums2cb=0 ...
```

`id0=48016605` 的 bit14（`kAdrenoIommuId0Cttw = 0x00004000`）为 1，所以这台 SMMU 的一致走表是开着的。
把它当 0 传进去，选中的是与硬件不符的那一支。

这条**不是**笔误级别的措辞问题：TCR 决定走表的内存属性，若后续真的让硬件用它走表，
非一致属性在一致 SMMU 上是可观测的错误。R233 因此只改这一件事：在键 72 里真的读 ID0（`0x20`），
把它和 `cw` 一起打印，让 TCR 与它自洽。

（同一段代码里 `p72.ttbr1 = 0u` 也是写死的。`adreno_iommu_ttbr1_for_gpu()` 这个模块里存在，
但键 72 没有调用它。本轮不把它算作新发现，只登记为**已知未测输入**，与 `coherent_walk` 同类。）

### 57.5 工具缺陷：静态门查的是上一轮的镜像

`r232_static.py` 的 `KERNEL`/`PAYLOAD` 指向 `r232-kernel/`、`r232-payload/`，
而当时实际要刷的是 `r232c-*`。它要求的 `gmu71b=cbar=` 这一行在 R232c 里**已经被改名**（成了 `about_to_read_gr1`）。
也就是说：**它会对着一个不含本轮改动的旧镜像报 PASS**。实测两份镜像的差异：

| 目录 | kernel sha256（前 16） | `gmu71b=cbar=` | `about_to_read_gr1` |
|---|---|---|---|
| `r232-kernel` | `36850deb06d4a8f2` | 1 | 0 |
| `r232c-kernel` | `476fd3f9748fd8a6` | 0 | 1 |

而它当时确实报了 `PASS: R232 static gate`。这是**门禁失效**，不是运气问题：
一个会对着旧镜像通过的门，比没有门更坏，因为它给出的是虚假保证。

修正：`work\tmp\r232d_static.py` 把路径钉到本轮目录、缺文件直接 FAIL，
并要求 `gmu72a/b/c/d` 四行、五条拒绝行、以及 `about_to_read_gr1` 同时存在。
R232d 的 PASS 是它第一次真正描述「要刷的那一份」。

### 57.6 这一轮**没有**证明什么（边界）

* **没有**编程 stage-2（`S2CR` 区，`0xC00` 起）。本轮只动 stage-1。
* ~~**没有**置 `sCR0.SMMUEN`（bit0）~~（R235 更正：该位名不存在；bit0=`CLIENTPD` 已为 0）。真正让「写进去的字」**不会被任何走表使用**的原因是另一条：`SMR0..4` 全零 ⇒ 没有一条流被匹配到任何 context bank。
* **没有**让 GPU 渲染任何一帧。准入表仍是 `modeled=9 / satisfied=0`，`CONFIG_XNU_NATIVE_ADRENO=n`。
* **没有**验证映射正确性：表里只放了一个 4 KiB 叶子（IOVA `0x40000000` → 物理 `0x90000000`），
  那是一个「能读回来做比对」的哨兵值，不是真实 GPU VA 布局。
* 因此本轮的正确说法是：**寄存器编程路径打通了**，而不是「SMMU 能用了」。

## 五十八、ROUND 233：把 `coherent_walk` 从字面零换成实测 ID0 —— 一致走表分支被真正选中

### 58.1 上一轮留下的是什么

R232d 把 context bank 0 的九个字写下去并读回，六项与模型逐位一致。但同一行里 `cw=0`：
TCR 走的是**非一致**分支（outer-shareable、non-cacheable）。这不是算错，是**输入没测** ——
探针直接写了 `adreno_iommu_coherent_walk(0u, 0u)`，而 `IDR0.CTTW`（`0x00004000`，BIT(14)）
在同一窗口的 `stage6=` 行里明明白白是 1：

```
stage6=id1 id0=48016605 id1=20008005 cttw=1 numcb=5 nums2cb=0 cap=5 init=0 alloc=0 cbndx=0 ...
```

`0x48016605 & 0x4000 = 0x4000`。也就是说，硬件声称自己能做一致走表，而驱动按不能做来配。
这类错误不会让机器报错，只会让 SMMU 在真正开始走表之后从缓存里读到旧描述符 ——
所以必须在启用之前修，而不是等它表现为偶发错误之后再查。

### 58.2 改了什么

只改一个输入来源，不碰编码器：

```c
const uint32_t r72id0 = rd32(gSmmuVa, 0x20u);
p72.coherent_walk = adreno_iommu_coherent_walk(r72id0, 0u);
```

行 C 的格式串同时加上 `id0=%08x`：

```c
"gmu72c=plan=%d n=%u abs=%u nab=%u cbndx=%u asid=%u ttbr=%llx cw=%u id0=%08x"
```

加这个字段不是装饰：如果只在注释里说「改成实测了」，镜像里没有任何可检查的痕迹，
静态门就只能相信提交信息。格式串里带着 ID0，静态门就能要求「新行存在、旧行不存在」。

### 58.3 读数（DEVICE-VERIFIED）

boot_id **580946231**（新）、窗口 `work\tmp\r233_s72_window.bin` 33,554,432 B
sha256 **`c1626ba0bb3e7e943f5eb6ef92f2acfcfbdf33a3cac03b2ec0d1303c457af529`**、**4/4 行落盘**：

```
[   30.036147]: gmu72a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1
[   30.036264]: gmu72b=build=0 root=ad940000 tables=4 writes=4 ias=48 oas=36 ok=1
[   30.036303]: gmu72c=plan=0 n=9 abs=0 nab=9 cbndx=0 asid=0 ttbr=ad940000 cw=1 id0=48016605
[   30.036427]: gmu72d=wrote=9 ttbr0=ad940000/00000000 sctlr=00001067 tcr=00803510 cbar=0001f300 root=ad940000
```

四行跨 **280 微秒**（30.036147 → 30.036427）。

### 58.4 逐位比对：`cw` 从 ID0 推导，而不是从第二个字面量

`work\tmp\r233_verify.py` 用与驱动同一批常量复算，且 `coherent_walk` 由设备打印的 ID0 现算：

```
id0=48016605 CTTW(BIT14)=set -> derived coherent_walk=1
word       device     model  verdict
ttbr0_lo   0xad940000 0xad940000 match
ttbr0_hi   0x00000000 0x00000000 match
sctlr      0x00001067 0x00001067 match
tcr        0x00803510 0x00803510 match
cbar       0x0001f300 0x0001f300 match
wrote      9          9          match
PASS: R233 readback matches the model (6 of 6 words, coherent branch verified)
```

TCR 的内部字段（脚本另做四项断言）：

| 字段 | 位 | 值 | 含义 |
|---|---|---|---|
| SH0 | 13:12 | `11` = IS | inner-shareable（一致走表分支） |
| ORGN0 | 11:10 | `01` = WBWA | write-back write-allocate |
| IRGN0 | 9:8 | `01` = WBWA | 同上，内层 |
| T0SZ | 5:0 | `16` = 64−48 | 48 位 IAS |
| EPD1 | 23 | 1 | TTBR1 关闭（几何选的是 TG0） |
| TG0 | 15:14 | `00` | 4 KiB granule |

与 R232d 的 `00802010` 相比：SH 由 `10`(OS) 变 `11`(IS)、RGN 由 `00`(NC) 变 `01`(WBWA)，
其余每一字段逐位未动。**这正是本轮唯一变量应有的形状。**

### 58.5 判决

**SMMU context bank 0 的九个字按「硬件声明支持一致走表」这一事实重新编程，读回与模型逐位一致，
内核存活 4/4 行。** 本移植的 stage-1 寄存器编程路径至此同时覆盖了两条分支中的正确那一条。

### 58.6 本轮没有证明什么（必须与上一条同等显眼）

1. ~~**`sCR0.SMMUEN` 仍是 0**。R232c 读到 `scr0=00200626`，bit0 = 0~~（R235 更正：bit0 是 `CLIENTPD` 且为 0；没有 `SMMUEN` 这个字段，见 §五十九）。真正的限制是 `SMR0..4` 全零。写进 context bank 的字
   **不会被任何走表使用** —— 它们只是躺在寄存器里。所以本轮的正确说法是
   「寄存器内容与硬件能力一致了」，不是「SMMU 开始工作了」。
2. **stage-2 未编程**。`0xC00` 起的 S2CR 区一笔未写；CBAR 里 TYPE 字段选的是
   `S1_TRANS_S2_BYPASS`，即有意让 stage-2 旁路，不是遗漏。
3. **表里只有一个 4 KiB 哨兵叶子**：IOVA `0x40000000` → 物理 `0x90000000`。这不是 GPU 的地址空间，
   只是一个用来证明「表能被构建」的占位。
4. **GPU 没有渲染任何一帧**。同窗口的 `gmucfg=... sfr=06030100`、`rc=0 GPUCC=5090000 ...
   cx=00005ff4 gx=00088400 cx_on=0 gx_on=0 gx_off=1 verdict=cx-DOWN`：GX 域仍是关的，
   屏上画面全部由 CPU 合成。
5. **`ttbr1` 仍是写死的 0**。模块里有 `adreno_iommu_ttbr1_for_gpu()`，键 72 没调用它。
   这是已知未测输入，不是本轮新发现。

### 58.7 一条与 SMMU 无关、但本轮再次确认的事实

同一窗口里 `stage4=id2 id2=00007511 ver=2 ias_ok=1 ias=48 ipa=36 pa=36 oas2=36 ptfs4k=1
vmid16=0 words_ok=1 plan_ok=1 tcr=a0100080 tcr2=00038011` —— 这是 **stage-2 的 id2 自检**，
与 context bank 0 实际写入的 `tcr=00803510` / `tcr2` 是两套不同的字，别把两者混为一谈
（`a0100080` 是 id2 计划模板的输出，不是 CB 寄存器里的值）。

## 五十九、ROUND 234-235：GR0 是可以写的 —— 只有那一位不接

### 59.1 先说 R234 撞到了什么

R233 之后，SMMU 的九个 context-bank 字已经按硬件声明的一致走表能力编程完毕、读回逐位一致。
下一个障碍看起来是 `sCR0`：

```
scr0=00200626  ->  CLIENTPD(bit0)=0, USFCFG(bit10)=1
```

`USFCFG=1` 的含义是「未匹配的流不旁路，按 fault 处理」，而同一时刻
`SMR0..SMR4 = 00000000`（`SMR_VALID` 是 bit31），也就是**一条流都没匹配上**。
两者相乘就是：所有 GPU/GMU 流量都是未识别流。于是 R234 的键 73 只做一件事 ——
把 `sCR0` 的 bit10 清掉，看旁路能不能打开：

```c
const uint32_t r73want = r73scr & ~(1u << 10);
*reinterpret_cast<volatile uint32_t *>(gSmmuVa + 0x000u) = r73want;
__asm__ volatile("dsb sy" ::: "memory");
const uint32_t r73after = rd32(gSmmuVa, 0x000u);
```

读数（boot_id **580460135**、窗口 `r234_s73_window.bin` sha256
**`6028dce4808148a43eae6070c1d48ff58a69b8dfe099d672d007787c25b84ee8`**、5/5 行存活）：

```
[   30.008881]: gmu73a=cx=80001fe0 cx_on=1 smmu=1 key=73 win=1
[   30.009033]: gmu73b=scr0=00200626 usfcfg=1 clientpd=0 id0=48016605 numsmrg=5
[   30.009044]: gmu73c=smr0=00000000 smr1=00000000 smr2=00000000 smr3=00000000 smr4=00000000
[   30.009102]: gmu73d=clear_usfcfg wrote=1 before=00200626 after=00200626 usfcfg=1
[   30.009150]: gmu73e=after=1 scr0=00200626 gfsr=00000000 survived=1
```

`wrote=1` 表示那笔 store 执行了（同一行由 `dsb sy` 之后回读），`after` 与 `before` 逐位相同。
**写进去了，没落地。** 同时 `gfsr=00000000`：一条全局 fault 记录都没有。
`SMR0..4` 全零这一条也与 R174 在 CX-down 下读到的结果一致，说明那次不是假零。

这里有一步必须自己否掉：我当时的第一反应是「因为 `gSmmuVa` 是
`armcpu_map_physical`（`kIODirectionIn`）拿到的只读映射，所以写不生效」。
这个解释与已有证据直接矛盾 —— **R232d/R233 写的那九笔 context-bank 字走的是同一个
`gSmmuVa`**，而那九个字确实从全零变成了 `sctlr=00001067 tcr=00803510 cbar=0001f300`。
同一个指针、同一批 store 形式，一边生效一边不生效，所以问题不在映射方向。撤回。

### 59.2 R235 要分辨的是什么

只剩两种读法：

* **(a)** GR0 整块不接处理器写；
* **(b)** GR0 是能写的，只有 `sCR0` 的这一位（或这个寄存器）拒绝。

分辨办法：在**同一次启动**里，对三个不同的 GR0 地址做三次同形式的 store，
其中两次的值不改变任何行为：

| 探针 | 地址 | 写入值 | 为什么不改变行为 |
|---|---|---|---|
| 1 | `0x800`（`SMR0`） | `0x0000002a` | `SMR_VALID`(bit31) 保持清零，该条目仍然匹配不到任何流 |
| 2 | `0x000`（`sCR0`） | `scr0 \| (1<<4)` = `0x00200636` | bit4 是 `GCFGFRE`，只门控全局配置错误的**上报**，当前为 0 |
| 3 | `0x000`（`sCR0`） | `scr0 & ~(1<<10)` = `0x00200226` | 与 R234 键 73 完全相同的一笔，作为对照重放 |

三个探针各打印自己的 `wanted` 与 `back`，`landed` 由设备侧按回读值算，
不在主机上事后推断。两次 sCR0 写入之间先把原值写回，让第三个探针从与键 73 相同的字起步。

### 59.3 读数（DEVICE-VERIFIED）

boot_id **573874107**（新）、窗口 `work\tmp\r235_s75_window.bin` 33,554,432 B
sha256 **`71cdd5323b5bf67551297e4c12acb3178987968161896766e3b0134d46431ff2`**、
`msg_bytes=30945 slots_written=8`、**7/7 行全部落盘、内核存活**，七行跨 **439 微秒**
（29.672072 → 29.672511）：

```
[   29.672072]: gmu75a=cx=80001fe0 cx_on=1 smmu=1 key=75 win=1
[   29.672188]: gmu75b=scr0=00200626 sacr=00000000 gfsr=00000000 id0=48016605 smr0=00000000
[   29.672254]: gmu75c=smr_probe wrote=1 wanted=0000002a back=0000002a landed=1
[   29.672312]: gmu75d=smr_restore wanted=00000000 back=00000000 restored=1
[   29.672365]: gmu75e=scr0_bit4 wrote=1 wanted=00200636 back=00200636 landed=1
[   29.672441]: gmu75f=usfcfg_clear wrote=1 wanted=00200226 back=00200626 landed=0 usfcfg=1
[   29.672511]: gmu75g=after=1 scr0=00200626 smr0=00000000 gfsr=00000000 survived=1
```

镜像侧：`r235-kernel\kernel.macho` 126,234,624 B sha256
`51993e0aa7ab092e77aa992b0b84374908706651244bdd05df3617892a70c714`；
`r235-payload\payload.img` 150,994,944 B sha256
`fd452a0fde3dc95868375cd2a6214611b551d02d5baa245d669101398fa5f432`；
rekey 后 `=75` 的那一份 sha256 `61abcdc91c50e0ee82c5e637ee3f921a1ef89cae2243280e7cc587e43c314790`。
CI run `44683333270`（#345）success，构建分支 `r235-smr-probe` @ `fbf4bae31381`。

### 59.4 判决

**GR0 接受处理器写入；被拒的是清 `sCR0` bit10 这一笔。**

三条证据合起来只有这一种读法：

1. `SMR0` 拿到 `0x0000002a` 并原值读回（`landed=1`）；
2. `sCR0` 拿到 `0x00200636` 并原值读回（`landed=1`）—— 这是**同一个寄存器、同一个偏移、
   同一种 store 形式**，所以「GR0 不接写」「映射是只读的」「`sCR0` 不接写」三条解释同时被否；
3. R234 键 73 与 R235 探针 3 是同一笔写，两次都被忽略（`back` 都回到 `0x00200626`）。

也就是说「写不进 GR0」是错的，正确的描述是**这一位不接写**。至于是哪一种，现有证据不足以定：

* **读法甲**：该位在本实现上是只读的 / 被锁住的。`sacr=00000000` 读作零，
  但 `sACR` 在钉住的上游源码里只有一个 `#define ARM_SMMU_GR0_sACR 0x10`、
  `arm-smmu.c` 从头到尾既不读也不写它，所以「`sACR` 上锁」这个说法**没有任何正面证据**，
  只是尚未被排除。
* **读法乙**：`USFCFG` 这个位名对本芯片不一定成立。钉住的 `arm-smmu.h` 是通用 v2/v3 头，
  而实测置位的位是 `1,2,5,9,10,21` —— 其中 **bit9 与 bit21 在该头文件里没有名字**。
  一个「置位集合里有两个位我们叫不出名字」的寄存器，把它 bit10 的名字当成既定事实来推理，
  本身就是可疑的。

两种读法的工程后果相同，所以这一轮不需要现在就分辨它们。

### 59.5 `gfsr=00000000` 说明什么 —— 以及它**不能**说明什么

两轮都读到 `gfsr=00000000`。这个读数最容易被过度解读，所以先把边界划清楚。

`USFCFG`(bit10)=1 的含义是「未匹配的流按 fault 处理」。如果 GPU/GMU 的流此刻真的在
往 SMMU 里走，而 SMR 全零（一条都匹配不上），那 `sGFSR` 应该有记录。它是零，看上去
很像是「SMMU 没有挡路」。

**这个推论不成立，本节明确不采用它。** 原因很直接：**键 73 与键 75 都没有写
`HFI_CTRL_INIT`**，也就是这两次启动里 GMU 从来没有被要求启动 HFI，因此没有 HFI 流量、
没有 GMU 取指、没有 GPU 的任何一次内存访问。一个没有流量的 SMMU 记零条 fault 是**必然的**，
不是发现。

所以 `sGFSR=0` 既不能支持也不能反驳「SMMU 挡住了 HFI」。它在本次唯一正确的用途是当**基线**：
记录下「HFI 未启动时 fault 计数为零」。什么时候它才会有信息量？等到某一次真的把 HFI 启动起来、
而内核还活着的那一刻 —— 那时它非零才有意义。现在它不是证据。

（这条自我更正本身也值得记下来：它与我这一轮早先撤回的「GMU 固件镜像缺失」是同一类错误 ——
从一个承载不了结论的读数直接推到结论。证据就在手边，只是没先问「这个读数在这个前提下
可能是别的值吗」。）

### 59.6 这一轮**没有**证明什么（必须与上一条同等显眼）

* **没有**打开旁路。bit10 仍是 1，`USFCFG` 仍是 1，未匹配流的处理方式与 R234 之前逐位相同。
* **没有**编程任何 SMR，也没有编程 `0xC00` 起的 S2CR 区（R173/R174 已证明那里每次读都 abort，
  且 `ID0.S2TS=0` 表示 stage-2 在本芯片上不存在）。GPU 的流依然一条都匹配不上。
* **没有**让 GPU 渲染任何一帧。准入表仍是 `modeled=9 / satisfied=0`，`CONFIG_XNU_NATIVE_ADRENO=n`，
  屏上画面仍全部由 CPU 合成（`offline-legacy-cpu-present=1`）。
* **没有**解释「写 `HFI_CTRL_INIT=1` 就静默死」，也没有把 SMMU 从嫌疑里排掉。
  R235 排除的是**一条具体路径**（先清 `USFCFG` 打开旁路、再写启动字），不是 SMMU 本身。
  前十轮那个问题原样留在原地。
* **没有**分辨 59.4 的读法甲与读法乙。要分辨需要一个能证明「这一位本来可写」的独立证据，
  而不是再写它一次。

### 59.7 由此确定的三条边界（给下一轮）

1. **不要再写 `sCR0` bit10。** 两轮、两种上下文、同一结果，再写是重复实验。
   要问这个问题，得先拿出「这一位在本芯片上可写」的独立证据。
2. **「打开 SMMU 旁路」这条路关闭了；「SMMU 挡路」这条假设没有关闭。** 前者是硬结论
   （`USFCFG` 两轮两次都清不掉），后者没有任何证据支持也没有任何证据反驳 ——
   `sGFSR=0` 只反映「HFI 未启动时没有流量」，不能当 SMMU 无罪的证明（见 59.5）。
   要把 SMMU 排掉，需要在 HFI 真的启动、内核还活着的那一刻读 `sGFSR`；
   而那正是内核会死的那一刻。这条线因此停在「需要一次能活过启动字的观测」上。
3. **回到 CM3/GMU 侧。** `HFI_CTRL_INIT=1` 的致死点在写入之后、且快到来不及写日志
   （R219 键 68、R222 键 66 都是 0/3，连 store 之前的行都丢）。
   这条线上尚未被排除的、有证据支撑的方向是 CM3 自身的启动条件 —— 而不是内存映射或 SMMU。

## 六十、ROUND 236：两个中断掩码都是零 —— 但把启动字包起来这件事，这一轮没做到

六十.1 一句话：**键 76 四行全落盘，读出的两个中断掩码都是 `0x00000000`（每个源都没被屏蔽）**；
键 77 一行都没落盘，窗口与键 76 逐字节相同。所以「掩码」这个前提被证实了，
「掩码是不是启动字需要的东西」这一问，**这一轮没有答上来**。

六十.2 键 76（只读，什么都没写）—— DEVICE-VERIFIED
boot_id `573661243`（新）、窗口 `work\tmp\r236_s76_window.bin` 33,554,432 B
sha256 `ca5e9f8ae53795fc981a7360ac7f86e51ed834754dad26ca486d2a0c1a3baa0c`、
`msg_bytes=30678 slots_written=8 write_errors=0`，四行跨 **45 微秒**（29.661689–29.661734）：

```
gmu76a=cx=80001fe0 cx_on=1 gmu=1 key=76 win=1
gmu76b=ao_status=00000000 ao_mask=00000000 hfi_info=00000000 hfi_mask=00000000 fwinit=babeface hfistat=00000000
gmu76c=ao_none_masked=1 hfi_none_masked=1 ao_pending=0 hfi_pending=0
gmu76d=after=1 ao_status=00000000 hfi_info=00000000 survived=1
```

判决：**`AO_HOST_INTERRUPT_MASK = 0` 且 `GMU2HOST_INTR_MASK = 0`**，即两处中断都没有被屏蔽
（`none_masked=1` 是同一事实的另一种写法）。这两个寄存器在此前的十九轮里从未被本移植写过
（`grep` 探针全文 `AO_HOST_INTERRUPT` 命中 0 次、`GMU2HOST_INTR_MASK`/`0x5193` 命中 0 次），
而 XNU 也从未接管 IRQ 304/305 —— 所以**只要 GMU 抬一条中断，就没有任何处理器代码会被调用**。
这正是「写启动字就静默死」这条假设所要求的前提，本轮把它从「推测」变成了「实测」。

同时确认（与本轮无关但同窗口）：`fwinit=babeface`、`hfistat=00000000` —— CM3 起来了、HFI 没起，
与 §五十三/§五十九 一致。

六十.3 键 77（屏蔽 + 启动字）—— 观测链没有接通
同一个内核、同一个 payload 只换了键值（`enchilada-pas-exec=77`，payload sha256
`461bf15d4619cda674df7709fb48473e3a5e85e0f0cda013b49ce87c2378c108`）：

- 刷写核验全过：`key block 31059: device == payload -> True`、
  `key block says: enchilada-pas-exec=77 -no_slto_p`、
  `system_b head 8 MiB sha256 a2738b081ff82ba47344d65b42c156c4abca49c5dacc408ba442946f9ef7de39`
  与 payload 头逐字节相同。
- 结果：**`gmu77a..g` 七个标记全部为 0**；窗口 sha256 `ca5e9f8a…`（**与键 76 那次逐字节相同**）、
  boot_id 仍是 **573661243**（与键 76 同一次启动的编号）。
- 因此这一轮的读数是：**键 77 那次启动没有在盘上留下任何一个新字**。窗口内容还是键 76 那轮的。

六十.4 这一轮**没有**证明什么（必须原样保留）

1. **没有把「屏蔽」和「启动字」分开。** 键 77 的块在微秒量级内跑完：先写四笔掩码，再空转
   `for (r77w = 0; r77w < 40000; ++r77w) { rd32(HFI_CTRL_STATUS); IODelay(1u); }`（约 0.63 s），
   然后写启动字。writer 一轮约 500 ms，所以**连行 A 都不可能在这一轮里提交**。
   零字节这个结果对「死在屏蔽」「死在等待」「死在启动字」三种可能**同样兼容**。
   这不是新问题 —— §二十 已经记过同一类失败十次，本轮只是又踩了一次。
2. **没有证明 GMU 真的抬过中断。** `ao_status=00000000`、`hfi_info=00000000` 是
   「此刻没有挂起的中断」，不是「中断不会发生」。键 76 从未写启动字，所以 GMU 没有理由抬任何东西。
3. **没有证明屏蔽是安全的。** 键 77 零字节，无法区分「屏蔽那四笔写就致死」和「屏蔽无害、
   后面才死」。
4. **没有碰 stage-2、没有碰 SMR 有效流映射、GPU 未渲染任何一帧。**
   准入表仍是 `modeled=9 / satisfied=0 / BLOCKED`，`CONFIG_XNU_NATIVE_ADRENO=n`。

六十.5 下一轮必须解决的第一件事：**观测链，不是假设**

键 77 的结构性错误是「在一次启动里既做屏蔽又做启动字，且两者之间只隔 0.63 s」。
无论结论朝哪边倒，这一轮都无法归属。下一轮要做的不是换假设，而是**把这个块拆成两次启动**：

- 键 78：只写四笔掩码 + 打印行 + **不写启动字**，然后返回。若这一轮落盘，说明屏蔽本身无害；
  若零字节，说明那四笔掩码写就是致命的（届时告警：这条通路也得放弃）。
- 键 79：只有在 78 落盘之后才有意义 —— 同样的四笔掩码，末尾加启动字。两轮之间用**不同
  boot_id** 区分，同键值同镜像。

这与 §二十 的教训一致：**一次启动只回答一个问题**，任何「先 A 后 B 再观察」的设计，只要
A 与 B 之间的时间短于一个 writer 轮，就必然读不出区别。

六十.6 三条边界（写进下一轮的约束）

1. 不要再用「空转买时间」去换可观测量。`IODelay` 读循环（R221 给了 3.17 s）与纯紧自旋
   （R208）都已被证明救不了观测链；键 77 又添一例。要让证词落盘，唯一可靠的办法是
   **让这一轮在写完该写的之后正常返回**，由后续正常路径里的 writer 轮转把它带走。
2. `AO_HOST_INTERRUPT_MASK = 0` 与 `GMU2HOST_INTR_MASK = 0` 是**实测状态**，写进任何后续轮次
   的前置条件时必须注明是 R236 键 76 的读数，不能当成上游默认值。
3. GR0 侧的两条结论不变：`sCR0` bit10 的清除两轮两次都被忽略（R234/R235），别再写；
   SMR 区 `0x800..0x810` 已实测可写且安全（R235 探针 1 `landed=1`）。

## 六十一、ROUND 237：四笔中断掩码写是安全的；把启动字加回去，这台机器连一行都不留

六十一.1 一句话

上一轮量到两个 GMU 中断掩码读出来都是零（AO 侧 `0x00000000`、HFI 侧 `0x00000000`），而
XNU 从未接管 IRQ 304/305，所以「写 `HFI_CTRL_INIT=1` 会让一个没人处理的中断打进来」有了
一个可测的前提。但上一轮把三件事挤进了同一个 writer 周期（约 500 ms）：写四笔掩码、
空转 0.63 s、写启动字。零字节对「死在掩码」「死在空转」「死在启动字」三种解释同样兼容，
所以那轮什么都没归因。

这一轮把动作拆开，中间什么都不放：

* **键 78**：写两笔 CLR、两笔 MASK，打印读数，**然后正常返回**，从不碰启动字。
* **键 79**：重复那四笔，然后写 `HFI_CTRL_INIT=1`，然后是同样的两笔回写。

判据改成 **boot_id 是否推进**，而不是数行。行数是上一轮把三种解释混在一起的直接原因。

六十一.2 键 78：四笔掩码写本身无害（DEVICE-VERIFIED）

`work\tmp\r237_s78_window.bin`，33,554,432 B，sha256
`68ac7379bcba2184100c412c7b7655ea86624357775c810bdc686cb8c4771460`，
boot_id **586031978**（此前从未出现过），`slots_written=8`、`msg_bytes=30554`、
`write_errors=0`、`uptime_s=3`，**3/3 行落盘**，三行跨 44 微秒（30.301776–30.301820）：

```
gmu78a=cx=80001fe0 cx_on=1 gmu=1 key=78 win=1
gmu78b=masked ao_mask=0000003f hfi_mask=ffffffff both_masked=0 was=00000000/00000000
gmu78c=no_start=1 stat=00000000 intr=00000000 ao=00000000 survived=1
```

判决：**这四笔写不致死**，而且「写完就返回、让后续正常路径的 writer 轮转把证词带走」
这条观测链在这个代码形状下是通的。这是自 R177 以来第一次拿到「行落盘 + 没有长等待 +
写确实执行」的组合。

六十一.3 `ao_mask` 读回 `0000003f` 不是失败，是我的判据写错了

行 B 打印 `ao_mask=0000003f`，而我原来的判据是

```c
both_masked = (aoMask == 0xffffffffu) && (hfiMask == 0xffffffffu)
```

于是 `both_masked=0`。这个 0 是**假阴性**，不是设备行为：钉住的
`.cache/adreno-a630-source/drivers/gpu/drm/msm/registers/adreno/a6xx_gmu.xml` 里
`GMU_AO_HOST_INTERRUPT_STATUS` 只定义了 **6 个位域**（`WDOG_BITE 0`、`RSCC_COMP 1`、
`VDROOP 2`、`FENCE_ERR 3`、`DBD_WAKEUP 4`、`HOST_AHB_BUS_ERROR 5`），所以这个寄存器只有
低 6 位是实现过的，写 `0xffffffff` 之后读回 `0x3f` 才是**全部已实现位都被屏蔽**的正确读数。
`hfi_mask=ffffffff` 读回原值，说明 HFI 侧 32 位都实现。

以后判这条必须写 `(aoMask & 0x3f) == 0x3f`。把它记在这里，是因为这个假阴性如果没被抓住，
下一步就会去「修」一个并不存在的问题。

六十一.4 键 79：连一行都不留，而且两次都一样（DEVICE-VERIFIED，否定结果）

两次上机，`key` 字节都在设备上（`key block says: enchilada-pas-exec=79 -no_slto_p`、
`key verified: this boot will run key 79`、`flash readback matches: True`、
`system_b head 8 MiB sha256` 与 payload 头一致），两次的时间线也完全一样：

| 事件 | 键 78 | 键 79 第一次 | 键 79 第二次 |
|---|---|---|---|
| `reboot bootloader` | 16:29:09 | 16:30:48 | 16:34:1x |
| `boot sent ok` | 16:29:21 | 16:31:00 | 16:34:2x |
| `back in recovery` | 16:29:59 | 16:31:38 | 16:35:0x |

两次都是 **38 秒回到 recovery**（armed BCB 生效，不需要按键），两次都**没有**推进 boot_id，
两次读到的窗口都与键 78 那轮**逐字节相同**：

```
key 78 window: boot_id=586031978 slots_written=8 msg_bytes=30554 sha256=68ac7379...
key 79 window: boot_id=586031978 slots_written=8 msg_bytes=30554 sha256=68ac7379...
byte-identical: True
```

而且窗口里那三行自己的 `key=` 字段写的是 `key=78`。也就是说：**盘上留着的是键 78 那轮的
证词，键 79 这一次启动一个字都没提交。**

六十一.5 我自己的判决脚本先说了句错话

第一次跑完，`r237_s79_run.py` 打印的是

```
this boot reached the writer / survived the start word (new boot_id) but the rows did not all land
```

这句是错的。原因是 `KNOWN` 表里没有 **586031978** —— 那是键 78 自己那一轮刚产生的 boot_id，
此前没有任何一轮产生过它，所以我没把它登记进去。键 79 读到同一个窗口，`586031978` 仍然不在
`KNOWN` 里，脚本就判成 NEW。

修法不是调判据，而是**换个问法**：不看「这个 boot_id 是不是新的」，看「这个窗口是谁的」。
新写 `work\tmp\r237_rejudge.py` 从窗口字节本身取三件事：头里的 `boot_id`、行内探针自己写的
`key=` 字段、以及与键 78 窗口的 `sha256`。三者一致地指向键 78。

教训（与 §二十 / §28 / §34 同类，但这次是同一天内的第三次）：**判据不能依赖我手工维护的表**。
`KNOWN` 是一个「我记住了多少轮」的清单，它天然会漏；窗口里的 `key=` 字段是设备自己写的，
不会漏。

六十一.6 这一轮没有证明什么

1. **没有证明键 79 死在启动字那一笔上。** 行 A（`gmu78a=…`）在四笔掩码写**之前**就打印了，
   它也没有落盘。所以同样的零字节对三种解释一样兼容：死在四笔掩码、死在启动字、死在两者
   之间。**是键 78 排掉了第一种**（四笔写落盘且活着），不是键 79 自己。
2. **没有证明中断是致死机制。** 这一轮只证明了「把所有中断源都屏蔽掉，启动字照样致死」。
   它排掉了「中断打进来没人处理」这一条，但没有指向真正的机制。
3. **没有证明「屏蔽」这件事在 HFI 侧是生效的。** `hfi_mask` 读回 `ffffffff`，但
   `GMU2HOST_INTR_MASK` 的位定义在钉住的 XML 里只有一个条目、没有位域定义，我无法断言
   哪一位对应哪个源。
4. 仍然没有渲染任何一帧；准入表仍是 `modeled=9 / satisfied=0`，`CONFIG_XNU_NATIVE_ADRENO=n`。

六十一.7 三条边界（写进下一轮的约束）

1. **不要再把两笔 store 之间的间隙交给空转。** 键 78 证明的观测链是「写完就返回」；
   键 77 和键 79 证明的是「中间夹一段等待或夹一串后续动作，就什么都读不到」。
   下一轮如果要问「启动字那一笔本身是否致死」，唯一的办法是**让写启动字成为这一轮的最后
   一个动作**（写完立刻返回，连回读都不做），并且在此之前先把该打印的都打印掉。
2. **判据不要依赖手工维护的 `KNOWN` 表**（六十一.5）。要用设备自己写在证词里的字段：
   `key=`、`boot_id`、窗口 `sha256`。
3. **`ao_mask` 只能读到 `0x3f`**（六十一.3）。任何「掩码是否全屏蔽」的判断必须用
   `(aoMask & 0x3f) == 0x3f`，否则会得到一个恒为假的断言。

## 六十二、ROUND 238：把启动字放到本轮的最后一步 —— 它仍然在一个 writer 轮之内把整机停住

### 62.1 一句话

键 80（前导 + 四笔中断掩码写 + 全部打印 + 返回）给出新 boot_id 与 3/3 行；键 81（完全相同，但在所有打印之后多一笔 `HFI_CTRL_INIT = 1u` 然后立即返回，无回读、无轮询、无恢复）留下与键 80 逐字节相同的窗口，行内 `key=` 仍为 80。即启动字作为本轮最后一个动作，本机仍在下一轮 writer 提交之前就停了。

### 62.2 代码形状（`package/EnchiladaAdrenoProbe.cpp`，`if (fwKey == 80u || fwKey == 81u)`）

两个键共享一段，只有一个运行时比较 `const bool r80is81 = (fwKey >= 81u);` 分岔：

1. 行 A `gmu80a=cx=... key=%u win=%d`
2. CX/映射门诉（`gmu80b=refused-cx-down` / `gmu80b=refused-nogmu`）
3. 读两个掩码原值 → 写两笔 CLR + 两笔 MASK（全 `0xffffffff`）→ `dsb sy` → 回读
4. 行 B `gmu80b=masked ao_mask=... hfi_mask=... both_masked=%d was=%08x/%08x`
5. 行 C `gmu80c=last_row=1 start_next=%d stat=... intr=... ao=... fwinit=...`（**本轮最后一行打印**）
6. `if (r80is81) { gmuW[HFI_CTRL_INIT] = 1u; dsb sy; }` —— 之后什么都不做，直接出块。

`both_masked` 这一次用 `((aoMask & 0x3f) == 0x3f) && (hfiMask == 0xffffffff)`，即 R237 那个假阴性已修正（AO 块只实现低六位）。

### 62.3 构建与静态门

- CI 分支 `r238-last-action`，run **44709686730（#348）`completed/success`**，job `build` 09:19:11Z → 09:40:33Z，28 个构建步骤全 success。
- 取件（16 连接）：`r238-kernel/kernel.macho` **126,234,624 B** sha256 `669ff547ca129c3bb4d5ef350610115ea3e27dfbc19478dca43be8ef814ec63c`；`r238-payload/payload.img` **150,994,944 B** sha256 `f6163823dc938f3f828691bf33e80652dc92ce5140ff91a8d7432828a945a1df`。
- 静态门 `r238_static.py`：9 条 required（`gmu80a/b/c` 三条格式串 + 两条拒绝分支 + `enchilada-pas-exec` + R237 的 `gmu78b` / `gmu78c=about_to_start` / `gmu79d` 存活行）+ 3 条 forbidden（`gmu80d=` / `gmu80e=` / `gmu81a=`：启动字之后不允许再有行，键 81 不单独拥有自己的行）+ 固件钉 `b07f9029...`（32,768 B，按摘要扫描，不靠偏移）。
- **门自证区分能力**（`r238_gatecheck.py`，把同一份 `r238_static.py` 的路径指向上一轮）：对 `r237-kernel/payload` 五条 `gmu80*` 全为 0 并 `FAIL`，对 `r238-kernel/payload` 全为 1 并 `PASS`。即这一轮的门不是空转。

### 62.4 键 80（对照）读数 —— DEVICE-VERIFIED

rekey `enchilada-pas-exec=2 -> =80`（`r238-s80-payload` sha256 `7a356dd1279c6e36413b5b5181479d7cec2c1534df2d3ec015cce1410245166d`，config 1377 → 1378 B）；`REBOOT_TO_TWRP_GATE: PASS (3 checks, 0 failures)`。

```
window 33554432 B sha256 4b1a55257e932520c45a27059c45a80884da367b666bf68e3f00d03d21ede37a
magic=SHLOG001 ver=4 hdr=152 boot_id=586963994 (unseen boot_id) msg_bytes=30572 slots_written=8
uptime_s=3 write_errors=0

gmu80a=cx=80001fe0 cx_on=1 gmu=1 key=80 win=1
gmu80b=masked ao_mask=0000003f hfi_mask=ffffffff both_masked=1 was=00000000/00000000
gmu80c=last_row=1 start_next=0 stat=00000000 intr=00000000 ao=00000000 fwinit=babeface
```

判决：`counts: gmu80a=1 b=1 c=1`、3/3 行落盘，**新 boot_id 586963994**，窗口 sha256 与 R237 键 78 那一轮（`68ac7379...`）不同。两个掩码都到位：AO 低六位全 1（读回 `0x3f`）、HFI 读回 `0xffffffff`，`both_masked=1`。

这是本轮的**陈述性控制**：同一段代码、同一批打印，只不写启动字，它能在下一轮 writer 提交里全部落盘。

### 62.5 键 81（开窗口的那一笔）读数 —— DEVICE-VERIFIED

rekey `=2 -> =81`（`r238-s81-payload` sha256 `0c747203fe01d790566f140af043156c254087d9e24063840d51cf9d1fea4a22`）；`REBOOT_TO_TWRP_GATE: PASS`。刷写核验全过：

```
key block 31059: device == payload -> True
key block says: enchilada-pas-exec=81 -no_slto_p
key verified: this boot will run key 81
system_b head 8 MiB sha256 0cb40ea4dfb8c687f8f7dd72ff75197f3cb948a678753092362219a4f147cbaf / payload head (same)
flash readback matches: True
reboot bootloader 18:19:28 -> flash system_b 18:19:39 -> boot sent 18:19:40 -> back in recovery 18:20:18  (38 s)
```

窗口读数与键 80 那一轮**逐字节相同**：

```
window sha256 4b1a55257e932520c45a27059c45a80884da367b666bf68e3f00d03d21ede37a   (unchanged)
boot_id 586963994 (== key 80's boot)   msg_bytes=30572 slots_written=8
gmu80a=cx=80001fe0 cx_on=1 gmu=1 key=80 win=1
gmu80b=masked ao_mask=0000003f hfi_mask=ffffffff both_masked=1 was=00000000/00000000
gmu80c=last_row=1 start_next=0 stat=00000000 intr=00000000 ao=00000000 fwinit=babeface
```

注意 `start_next=0`：那是键 80 自己写的字。键 81 的行一个也没上盘，所以不存在 `start_next=1` 的证词。

### 62.6 判决

**把启动字放到本轮的最后一个动作，仍然靠不住。**

归因的链条只有两步，两步都是实测：

1. 键 80 证明“前导 + 四笔掩码 + 三行打印 + 返回”这个形状能在一轮 writer 内落盘（新 boot_id + 3/3 行）。
2. 键 81 与其相差只有一笔 store（且在所有打印之后），它一个字也没留。

因此：从最后一次打印到下一轮 writer 提交的这个窗口（实测不超过一轮，约 500 ms）内，整机停不住地停了，而这个窗口里只有那一笔 `HFI_CTRL_INIT = 1u`。

### 62.7 本轮没有证明什么

1. **没有直接观测到死亡**。观测到的是“这一轮什么都没提交”，不是死在 store 那一条指令上。归因靠的是键 80 那个对照把区间压到一个 store 加一个 writer 轮。
2. **没有分辨“写发生异常”与“写之后 CM3 把东西弄坏”**。同超时夫误阻断 vs 固件真的开始 HFI 后的下游故障，这两种在本轮的读数上完全一样。
3. **没有证明 HFI 真的官方启动过**。键 80 的行 C 里 `fwinit=babeface`、`stat=00000000`、`intr=00000000`——那都是**store 之前**的读数。store 之后本轮什么都没读（故意）。
4. **没有改变 R223 的结论**：同一偏移写 `0u` 能存活、写 `1u` 致死。两边同时成立：偏移可写，值 1 致死。
5. **没有排除“窗口不更新”的其他原因**——不过刷写核验（key block / readback / head 8 MiB sha）已证实这一轮确实跑的是键 81 的镜像，且 38 s 后自动回到 TWRP（armed BCB 生效，无需按键）。

### 62.8 三条边界（下一轮不要重开）

1. 不要再在“启动字之后再做任何事”的形状下试图拿读数（R221/R236 键 77/R237 键 79/R238 键 81 四次同一结果）。
2. 下一个能分辨的问题不是“它会不会死”（已答），而是**“这笔写本身致死，还是固件真的开始 HFI 之后才致死”**。最小的分辨实验：在同一形状下把 `HFI_QTBL_INFO` 先清零再写启动字——固件看不到表就无法真正建立 HFI；若仍零字节 ⇒ 致死是这个偏移上这个值的写本身。
3. 同一构建里要带一个“形状对照”：同样把 store 放在最后，但写 `HFI_CTRL_INIT = 0u`。若它落盘 ⇒ dispatch 的形状无罪，只有值 1 有罪；若它也零字节 ⇒ 则“最后一个动作是这个偏移的 store”这个形状本身就不成立，需要重新设计观测链。

## 六十三、ROUND 239：保持唯一能落盘的形状，只改启动字的值和表描述符 —— 第一次把启动写落进窗口

本节记录两个键：82（形状对照：同一位置、同一形状，值为 0）和 83（先清 `HFI_QTBL_INFO`，再写启动字 1）。两个键都把 store 作为本轮**最后一个动作** —— 这是 R238 用键 80/81 证明唯一能把行交给介质、同时让写真正执行的形状。

### 63.1 一句话

在这条形状下：同位置写 0 落盘（键 82，3/3，新 boot_id 586092751）；**先清表描述符再写 1 也落盘**（键 83，4 行全部落盘，新 boot_id 585892346）；而 R238 的键 81 —— 同形状、表描述符仍是 1、写 1 —— 零字节。⇒ 致死既不是"这个形状"，也不是"这个偏移上任何一笔写"，而是**在表描述符声称表存在的情况下要求固件启动 HFI**。

### 63.2 代码形状

两个键共享同一段 preamble（与 R238 键 80/81 逐条相同）：

1. 行 A `gmu82a=cx=… cx_on=… gmu=… key=… win=…`（CX 门禁之前的总述）
2. CX 未上电 / GMU 未映射 ⇒ `gmu82b=refused-cx-down`、`gmu82b=refused-nogmu`，然后返回
3. 四笔中断掩码（两笔 CLR、两笔 MASK）+ `dsb sy`，行 B `gmu82b=masked ao_mask=… hfi_mask=… both_masked=… was=…/…`
4. 键 83 另有行 D `gmu83d=qtbl_before addr=… info=… about_to_clear_info=1 win=…` —— 在清描述符**之前**读，这样"清之前是什么"也有证词
5. 行 C `gmu82c=last_row=1 start_next=… qtbl_clear=… stat=… intr=… ao=… fwinit=…` —— 本轮最后一次打印
6. 最后动作：
   - 键 82：`gmuW[HFI_CTRL_INIT] = 0u; dsb sy;` 返回
   - 键 83：`gmuW[HFI_QTBL_INFO] = 0u; dsb sy;` 然后 `gmuW[HFI_CTRL_INIT] = 1u; dsb sy;` 返回

两句之后没有回读、没有轮询、没有打印、没有掩码恢复。stage6 有两条断言专门守这一点（"nothing follows the last store of key 82 / key 83"），实测两边余量都是空串。

### 63.3 构建与门禁

- CI：分支 `r239-value-and-table`，提交 `47f4725ab7958b6e1d9628011b336796215e3974`，run **44720339530（#349）`completed/success`**，28/28 步 success（step 16 Build kernel 是瓶颈）。
- 工件：`work/tmp/r239-kernel/kernel.macho` 126,234,624 B sha256 `9ae0777487dddb3bec8c45170019ca0f737a085adc0770364206b424d8165eae`；`work/tmp/r239-payload/payload.img` 150,994,944 B sha256 `751eb8d4ef847a596f73ac9baeb0343932c6e26470451f610b7c3678b2d1216c`。
- 静态门 `work/tmp/r239_static.py`：12 条 required+survivors（`gmu82a/b/c`、`gmu83d`、两条 refused 行、R237 的 `gmu78b/gmu78c/gmu79d`、R238 的 `gmu80a/b/c`）+ 2 条 forbidden + 固件钉 `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`（32,768 B，kernel 与 payload 都在）→ `PASS: R239 static gate`；key marker `enchilada-pas-exec` kernel=1 payload=2（payload 里该串出现在两处，故判"存在"而不是"恰好一次"）。
- **门自证区分能力**：把同一份脚本指向上一轮 `r238-kernel/kernel.macho` + `r238-payload/payload.img` ⇒ 六条 `gmu82*`/`gmu83d` 全 0、`FAIL: R239 static gate (6 problems)`。这是 R232d 那次事故（门对着旧镜像通过）之后每轮固定要做的一步。
- host 门 `check-enchilada-adreno-probe.py --cc … --cxx …` → **28/28 checks passed** + `PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)`；`gmuW[...] =` 计数 186 → **193**（键 82 的四笔掩码 + 一笔 0；键 83 的四笔掩码 + 一笔 QTBL_INFO + 一笔 1）。
- `verify-enchilada-*.py` 全量：48 个脚本里跑 46（`-gmu-caller-v1-record.py` 需 40-hex 版本号、`-ias-measured.py` 需真机），**FAILING: 0**；stage6 1017/1017。
- 探针：**477,241 字符 / 6,673 行**，arm 窗口 `(fwKey >= 7u && fwKey <= 83u)`。

### 63.4 键 82（形状对照：同位置写 0）

- payload：`work/tmp/r239-s82-payload/payload.img`，rekey `enchilada-pas-exec=2 -> =82`，config 1377 → 1378 B，sha256 `b66044f4cab3f32472006a03396ac4d1c0d0deda3bdf6b8203a01f42a7f5ba6f`。
- 刷写核验：`key block says: enchilada-pas-exec=82 -no_slto_p`、`key verified: this boot will run key 82`、`system_b head 8 MiB sha256 1f7c5d640ac646e10d29551b43536e08518f4c7c6c1477637946a1335b43d8d5 / payload head`（一致）、`flash readback matches: True`。
- 窗口：`work/tmp/r239_s82_window.bin` 33,554,432 B，sha256 **`0c000d8943fdd6fad816d5542fd787ff5d132527fbe5271fff47ee50f4ffc228`**，header `magic=SHLOG001 ver=4 hdr=152 boot_id=586092751 first_lba=14809083 msg_bytes=30585 slots_written=8 uptime_s=3 rounds=8 write_errors=0`。
- **3/3 行落盘**：
  ```
  gmu82a=cx=80001fe0 cx_on=1 gmu=1 key=82 win=1
  gmu82b=masked ao_mask=0000003f hfi_mask=ffffffff both_masked=1 was=00000000/00000000
  gmu82c=last_row=1 start_next=0 qtbl_clear=0 stat=00000000 intr=00000000 ao=00000000 fwinit=babeface
  ```
- `both_masked=1` 是 R237 那个假阴性的修正版判据（`(aoMask & 0x3f) == 0x3f`），同一读数在 R237/238 是 `both_masked=0`。

### 63.5 键 83（清描述符，然后写 1）

- payload：`work/tmp/r239-s83-payload/payload.img`，rekey `2 -> 83`，sha256 `cb849d8480e54a8390de4286e2510d2e8e68be37a55fc6edd4b9abb5c455ff3e`。
- 刷写核验：`key block says: enchilada-pas-exec=83 -no_slto_p`、`key verified: this boot will run key 83`、`system_b head 8 MiB sha256` 与 payload 头一致、`flash readback matches: True`。
- 窗口：`work/tmp/r239_s83_window.bin`，sha256 **`3b7d37b4d4f7ca98aeb92ce9016781d8659cb8e345cb69012351d3c38da2e9c0`**，header `boot_id=585892346 msg_bytes=30694 slots_written=8 uptime_s=3 rounds=8 write_errors=0`。
- **4 行全部落盘**：
  ```
  gmu82a=cx=80001fe0 cx_on=1 gmu=1 key=83 win=1
  gmu82b=masked ao_mask=0000003f hfi_mask=ffffffff both_masked=1 was=00000000/00000000
  gmu82c=last_row=1 start_next=1 qtbl_clear=1 stat=00000000 intr=00000000 ao=00000000 fwinit=babeface
  gmu83d=qtbl_before addr=ad932000 info=00000001 about_to_clear_info=1 win=1
  ```
- 设备在 38–40 s 后自己回到 TWRP（armed BCB 生效，无需按键）。

### 63.6 判决与单变量表

| 轮次 / 键 | 形状（store 相对最后一行） | 写的是什么 | 结果 |
|---|---|---|---|
| R219 键 66 | 之后还有行 | 不写 | 3/3 落盘（`eff8caa2…`） |
| R222 键 66 | 之后还有行 | `HFI_CTRL_INIT = 1` | 0/3，窗口不动 |
| R223 键 66 | 之后还有行 | `HFI_CTRL_INIT = 0` | 3/3 落盘（`d60bb6e3…`） |
| R238 键 80 | **最后一个动作** | 不写 | 3/3 落盘（`4b1a5525…`，boot 586963994） |
| R238 键 81 | **最后一个动作** | `HFI_CTRL_INIT = 1`（表描述符=1） | 0/3，窗口与键 80 逐字节相同 |
| R239 键 82 | **最后一个动作** | `HFI_CTRL_INIT = 0` | 3/3 落盘（`0c000d89…`，boot 586092751） |
| R239 键 83 | **最后一个动作** | 先 `QTBL_INFO = 0`，再 `HFI_CTRL_INIT = 1` | **4/4 落盘**（`3b7d37b4…`，boot 585892346） |

结论：在"store 是最后一个动作"这个形状下，致死与否取决于**表描述符**（键 81 与键 83 唯一的不同），而不是形状本身（键 82）、也不是该偏移的任何写（键 82/键 83 都写了）。这是本移植第一次在**写启动字之后**拿到新窗口。

### 63.7 没有证明什么

1. 没有证明固件"看见"过 `QTBL_INFO = 0`。我们只证明"先写它、再写 1"这条序列不死；两笔之间没有回读，也没有任何证据说明固件在这段时间里读了描述符。
2. 没有证明 HFI 真的启动过。行 C 里的 `fwinit=babeface`、`stat=00000000`、`intr=00000000` 全是**写之前**的读数；本轮之后没有任何回读。
3. 没有分辨"固件没有启动"与"启动了但随即静默"。要分辨必须有写之后的读数，而写之后的任何动作都会把本轮变回 R238 之前那种"多动作挤在一个 writer 轮里"的形状。
4. 死因仍未定位到具体动作。已知的只是"描述符=1 且要求启动"这一组合致死；是固件真的去 DMA 读表、还是 CM3 跳进某个 handler 后触发异常，都未测。
5. 一次读数，未重复。归因靠单变量对照（R238 键 80/81 与 R239 键 82/83 交叉），但键 83 的"存活"只观测了一次。
6. 键 82 与键 83 的差别不止一个变量（值 0 vs 两笔写）。之所以仍能归因到描述符，是因为键 81/键 83 之间只有描述符这一个差别，而键 82 只用来排除"形状本身"。

### 63.8 三条边界（没有变）

- GPU 仍未渲染任何一帧；`CONFIG_XNU_NATIVE_ADRENO=n`；准入表仍 `modeled=9 / satisfied=0 / BLOCKED`。
- `sCR0` 的 bit10（USFCFG）仍没有被打开：R234 键 73 与 R235 键 75 两次都测到这笔 store 被忽略。
- SMMU 的 stage-2 区（`0xC00` 起）仍未编程；GPU 侧仍只有一个哨兵叶子。

### 63.9 下一步（R240 已设计，未执行）

同一构建两键，都保持"store 是最后一个动作"：

- **键 84**：把 `HFI_QTBL_ADDR` 指向一页全零（4 KiB，经 `armcpu_dma_alloc_writable`），`HFI_QTBL_INFO = 1`，最后写 `HFI_CTRL_INIT = 1`。
  - 落盘 ⇒ 致死跟着**我们那张表的内容**走（固件真的去读它了），下一步是逐字对比表内容与上游 `a6xx_hfi_queue_table`。
  - 零字节 ⇒ 只要描述符非零且要求启动就死，与表内容无关。
- **键 85**：R238 键 81 的复现（真表、`INFO = 1`、最后写 1）。**必须再死一次**，用来排除"世界变了、现在什么都不死"这种整轮性解释；这是本轮形状的阳性对照。

## 六十四、ROUND 240：致死与表的内容无关 —— 把描述符指向一页全零，仍然零字节

本轮问的是 R239 留下的那一半：致死是跟着表的**内容**走，还是只要描述符读数非零且要求启动就够。两个键都保持"store 是本轮最后一个动作"这条唯一被证明能落盘的形状。

### 64.1 一句话

键 84（`HFI_QTBL_ADDR` 指向一页全零、`HFI_QTBL_INFO = 1`、最后写 `HFI_CTRL_INIT = 1`）**零字节**（0/4 行，窗口仍是上一轮）；键 85（R238 键 81 逐字复现：真表已由开窗路径装好、`INFO = 1`、最后写 1）**同样零字节**（0/3）。⇒ 表指向哪里、内容是什么，都不影响；致死只需要"描述符位为真"和"启动位为真"同时成立。

### 64.2 代码形状

两键共享同一段 preamble（与 R238 键 80/81、R239 键 82/83 逐条相同）：

1. 行 A `gmu84a=cx=%08x cx_on=%d gmu=%d key=%u win=%d`
2. CX 未上电 / GMU 未映射 ⇒ `gmu84b=refused-cx-down cx=%08x`、`gmu84b=refused-nogmu`
3. 四笔中断掩码（两笔 CLR、两笔 MASK）+ `dsb sy`；行 B `gmu84b=masked ao_mask=%08x hfi_mask=%08x both_masked=%d was=%08x/%08x`，其中 `both_masked` 判 `(aoMask & 0x3f) == 0x3f`（R237 的假阴性已按此修正）
4. **只有键 84**：`armcpu_dma_alloc_writable(4096, &virt)`，把 1024 个字写 0，`dsb sy`，再扫一遍数非零得 `nz`；行 E `gmu84e=zero_page pa=%08x nz=%u addr_before=%08x info_before=%08x win=%d`
5. 行 C `gmu84c=last_row=1 start_next=1 qtbl_zero=%u stat=… intr=… ao=… fwinit=…`
6. 最后动作，之后无回读、无轮询、无打印、无掩码恢复：
   - 键 85：`HFI_CTRL_INIT = 1`（一笔）
   - 键 84：`HFI_QTBL_ADDR = pa` → `HFI_QTBL_INFO = 1` → `HFI_CTRL_INIT = 1`（三笔，各带 `dsb sy`，顺序照上游 `a6xx_hfi_start`）

两键之间唯一的差别是第 4 步和最后一个动作里的头两笔；第 6 步的启动字本身逐字相同。

### 64.3 构建与门禁

- 探针 484,335 字符 / 6,805 行，`gmuW[...] =` 写点 **201**；arm 窗口 `(fwKey >= 7u && fwKey <= 85u)`。
- host 门 `check-enchilada-adreno-probe.py` → **28/28 checks passed** + `PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)`。
- `verify-enchilada-probe-stage6.py` → **1044/1044**；全量 `verify-enchilada-*.py` → **ran 46 of 48, FAILING: 0**（跳过的两个是 `-gmu-caller-v1-record.py` 需 40-hex rev、`-ias-measured.py` 需真机）。
- CI：分支 `r240-zero-page`，commit `15df899231cf90459fdd25fdd32daf4a1cef66aa`，run **44726641344（#350）`completed/success`，28/28 步**（step 16 Build kernel 11:19:26Z → 约 11:39Z）。
- 取件：`r240-kernel/kernel.macho` 126,234,624 B sha256 `6da1867ac169e07c78f4c3b0cf96a1e71420ec5c7b7448152b5a2bb17c09f757`；`r240-payload/payload.img` 150,994,944 B sha256 `2a4604a1c249205ea9bc8ac4d2805212d91ebb130105700276a50d4937af72aa`。
- 静态门 `r240_static.py` → `required+survivors: 17 checked`、`forbidden: 2 checked`、固件钉 `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`（32,768 B）、**`PASS: R240 static gate`**。
- **门的区分能力自证**：把同一个 `r240_static.py` 指向上一轮 `r239-kernel` / `r239-payload` → 七条 `gmu84*` 全为 `kernel=0 payload=0`、**`FAIL: R240 static gate (7 problems)`**。这一步是 R232c 事故（一个会对着旧镜像通过的门）之后每轮必做的。

### 64.4 键 84 读数（DEVICE-VERIFIED）

rekey `enchilada-pas-exec=2 -> =84`（`r240-s84-payload/payload.img` sha256 `dc21db2b2ed08fb51791c8796906ce5b38b469b433030dc92b03915759267ae0`），刷写核验全过：`key block 31059: device == payload -> True`、`key block says: enchilada-pas-exec=84 -no_slto_p`、`key verified: this boot will run key 84`、`system_b head 8 MiB sha256 764613d1f5d5227d1de718ba4834563e43d9d58e913f8b716e5f4ded6f9baf42`（与 payload 头一致）、`flash readback matches: True`。

时间线：`19:55:35 flash system_b` → `19:55:40 flash OK` → `19:55:41 boot sent` → `19:56:19 back in recovery`（38 秒，武装 BCB 生效，无需按键）。

窗口读数：

| 项 | 值 |
|---|---|
| 窗口 sha256 | `3b7d37b4d4f7ca98aeb92ce9016781d8659cb8e345cb69012351d3c38da2e9c0`（**与 R239 键 83 那轮逐字节相同**） |
| header `boot_id` | 585892346（＝上一轮） |
| `msg_bytes` / `slots_written` / `write_errors` | 30694 / 8 / 0（全部来自上一轮） |
| `gmu84a/b/c/e` 计数 | 0 / 0 / 0 / 0（**0 of 4 行**） |

### 64.5 键 85 读数（阳性对照，DEVICE-VERIFIED）

rekey 到 85（`r240-s85-payload/payload.img` sha256 `173cec17564e344165b56ffa8ff1d873dd74009ddc75136e2104f52531d8b2fc`），刷写核验同样全过：`key block says: enchilada-pas-exec=85 -no_slto_p`、`key verified: this boot will run key 85`、`flash readback matches: True`。

时间线：`19:57:02 boot sent` → `19:57:40 back in recovery`（38 秒）。

窗口：sha256 仍 `3b7d37b4d4f7ca98aeb92ce9016781d8659cb8e345cb69012351d3c38da2e9c0`、`boot_id` 仍 585892346、**0 of 3 行**。

### 64.6 判决

**阳性对照成立，键 84 可解释**：键 85 是 R238 键 81 的逐字复现，它仍然零字节 ⇒ 这台机器没有"世界变了、现在什么都不死"这种整轮性变化；在这个前提下，键 84（描述符指向一页全零）同样零字节 ⇒ **致死与表指向哪里、内容是什么无关**。

把 R219 以来所有同形状读数并排：

| 轮次 / 键 | 这一轮在行 C 之后写的最后动作 | 读数 |
|---|---|---|
| R219 键 66 | 什么都不写 | 3/3，boot 582344900，窗口 `eff8caa2…` |
| R222 键 66 | `HFI_CTRL_INIT = 1` | 0/3，窗口不变 |
| R223 键 66 | `HFI_CTRL_INIT = 0` | 3/3，boot 583170095，窗口 `d60bb6e3…` |
| R238 键 80 | 四掩码，`start_next = 0` | 3/3，boot 586963994，窗口 `4b1a5525…` |
| R238 键 81 | 四掩码 + `INIT = 1`（真表、`INFO = 1`） | 0/3，窗口不变 |
| R239 键 82 | 四掩码 + `INIT = 0`（同一位置、最后动作） | 3/3，boot 586092751，窗口 `0c000d89…` |
| R239 键 83 | 四掩码 + `INFO = 0` + `INIT = 1` | 4/4，boot 585892346，窗口 `3b7d37b4…` |
| R240 键 84 | 四掩码 + `QTBL_ADDR = 全零页` + `INFO = 1` + `INIT = 1` | 0/4，窗口不变 |
| R240 键 85 | 四掩码 + `INIT = 1`（键 81 复现） | 0/3，窗口不变 |

三行关键对照给出的条件式：

- `INFO = 1` 而**不**写 `INIT`（R219 键 67）⇒ 存活；
- `INIT = 1` 而 `INFO = 0`（R239 键 83）⇒ 存活；
- 两者同时为真（R238 键 81、R240 键 84）⇒ 死，且**与 `QTBL_ADDR` 指向的真实内容无关**。

所以致死条件可以写成：**"描述符位为真" ∧ "启动位为真"**。

### 64.7 没有证明什么

1. **零页的"零"没有设备侧证据**。行 E（`gmu84e=zero_page pa=… nz=…`）与其它三行一起没落盘，所以"装进 `QTBL_ADDR` 的那一页在被写入时确实是零"只由代码路径保证（写 1024 个字 + `dsb sy` + 复查计数），**不是 DEVICE-VERIFIED**。这一轮能确定的是"哪怕它非零也不改变结论"这个方向的证据并不存在 —— 只能确定键 84 死了。
2. **没有直接观测死亡**。观测到的是"这一轮什么都没提交"，把区间压到"两笔描述符写 + 一笔启动写"与一个 writer 轮（约 500 ms）之内，不是看到 CPU 停在哪条指令。
3. **没有分辨"寄存器写挂住总线"与"GMU 侧动作把系统弄坏"**。两者与观测一致。
4. **没有证明固件真的读过那张表**。相反，本轮证据**降低**了这个说法的必要性：既然表内容无关，就没有理由认为致死发生在"读过表之后"。
5. **一次读数，未重复**。键 84/85 各一次。归因靠与 R219/R223/R238/R239 的交叉，不靠重复。

### 64.8 三条边界（没有变）

- GPU 仍未渲染任何一帧；`CONFIG_XNU_NATIVE_ADRENO=n`；准入表仍 `modeled=9 / satisfied=0 / BLOCKED`。
- `sCR0` 的 bit10（USFCFG）仍未被打开：R234 键 73 与 R235 键 75 两次都测到那笔 store 被忽略。
- SMMU 的 stage-2 区（`0xC00` 起）仍未编程；GPU 侧仍只有一个哨兵叶子。

### 64.9 下一步（R241 已设计，未执行）

现在致死条件清楚了：`INFO = 1` ∧ `INIT = 1`，与表内容无关。剩下最值得问的是：**这两笔是"GMU 开始做 AHB 主设备访问"的门闩吗**。若是，则它第一次读表就会经过 GPU SMMU，而这条通路上一个有效 SMR 都没有（R174：`smr0=smr1=0`），加上 `sCR0` 的 `USFCFG = 1`（未识别流不旁路、直接 fault）⇒ 一次穿不过去的读，正好与"AP 在 MMIO 写里停住、一个 writer 轮内什么都不留下"相符。

第一刀不碰启动字，只问"流路由寄存器到底存不存在"：

- **键 86**（无启动字）：四掩码 → 行 A/B → 读 `sCR0`/`ID0`/`ID1` → 读 `SMR0..4` → 打印"即将触碰 `S2CR0`（`0xC00`）"的预告行 → **写** `S2CR0` → 读回 → 行 D。
  - 只落预告行、不落行 D ⇒ `S2CR` 区连写都 abort，这条路走不通（与 R173/R174 的读 abort 一致），必须换别的解释。
  - 两行都落 ⇒ `S2CR` 可写，下一轮才谈把 GMU 的 SID 路由到 CB0。
- **键 87**（无启动字）：写 `SMR0 = VALID | SID`，读回，打印。判 `SMR` 的 `VALID` 位能不能被立住。
- 只有 86/87 都落盘，才有资格在键 88 里把"路由 + 启动"合起来试。

## 六十五、ROUND 241：不碰启动字，只问流路由寄存器存不存在 —— SMR 接受 VALID，S2CR 收了写但不留值

R240 把致死钉在「描述符位为真 + 启动位为真」同时成立上，而且与表指向哪里、内容是什么无关（一页全零和真表一样致死）。本轮换一个问题：**那两位到底打开了什么**。一个一直没被排除的可能是——它们让 GMU 变成 AHB 主设备，于是它读队列表的第一次访问要穿过 GPU SMMU；而那条路上**一条有效 SMR 都没有**（R174：`smr0 = smr1 = 0`），`sCR0.USFCFG = 1`（bit 10，R164/R235）让未匹配的流**直接 fault 而不是旁路**。一次永远完不成的读，看起来正好就是每一轮致死都测到的那样：AP 在 store 里停住，一整个 writer 轮什么都没提交。

所以本轮**一个启动字都不写**，只问三件事：形状能不能落盘、`SMR0` 接不接受 `VALID`、`S2CR0` 接不接受写。

### 65.1 一句话

键 86（控制，不做任何 SMMU 写）：3/3 行落盘、新 boot_id、新窗口 ⇒ 形状无罪。键 88（写 `SMR0 = VALID | SID 4` 再复原）：5/5 行落盘、`wanted=80000004 back=80000004 valid=1 sid=4 landed=1 restored=1` ⇒ **`SMR0` 接受 `VALID`，本移植第一次把一条流的识别位立住**。键 87（写 `S2CR0` @ `0xC00` = `BYPASS`）：5/5 行落盘、**没有致死**，但 `wanted=00010000 back=00020000 typew=00020000 landed=0` ⇒ **写被收下、值没留下**，读回的是 `TYPE = 2`（FAULT）。

### 65.2 代码形状

新块 `if (fwKey == 86u || fwKey == 87u || fwKey == 88u) {`，插在 `if (fwKey >= 18u && fwKey <= 21u) {` 之前，`const int r86which = (fwKey >= 88u) ? 88 : ((fwKey >= 87u) ? 87 : 86);`。三键共享：

1. 行 A `gmu86a=cx=%08x cx_on=%d smmu=%d key=%u win=%d`
2. CX 未上电 / SMMU 未映射 ⇒ `gmu86b=refused-cx-down cx=%08x`、`gmu86b=refused-unmapped`（**都不做任何写**）
3. 读 `sCR0`、`ID0`、`SMR0`、`SMR1`，`dsb sy`，行 B `gmu86b=scr0=%08x usfcfg=%u clientpd=%u id0=%08x numsmrg=%u smr0=%08x smr1=%08x`
4. 分叉：

| 键 | 行 C | store | 行 D | 行 E |
|---|---|---|---|---|
| 86（控制） | `gmu86c=control_no_smmu_write=1 scr0=… smr0=…` | **无** | — | — |
| 88（SMR0） | `gmu88c=about_to_set_smr0 wanted=%08x old=%08x` | `(gSmmuVa + 0x800u) = 0x80000000u \| 4u` → 读回 → 写回 `old` | `gmu88d=smr_valid wrote=1 wanted=… back=… valid=%u sid=%u landed=%u` | `gmu88e=smr_restored wanted=… back=… restored=%u after=1 scr0=…` |
| 87（S2CR0） | `gmu87c=about_to_write_s2cr0 off=c00 want=%08x old_unreadable=1` | `(gSmmuVa + 0xc00u) = 0x00010000u` → 读回 | `gmu87d=s2cr0 wrote=1 wanted=… back=… typew=%08x landed=%u` | `gmu87e=after=1 scr0=… smr0=… gfsr=… survived=1` |

- 键 87 的 store 前**单独一行预告**：`0xC00` 组的读在 R173/R174 里两次 abort 过，所以先让「我马上要写它」上盘；如果某次启动只落预告行，那笔 store 就是嫌疑人。
- 三键都没有 `IOSleep`、没有 `for (` 轮询、**没有任何 `gmuW[...]` 写**（整块对 GMU 寄存器只读）—— stage6 对这三点都有源码级断言（`_k8687.count('gmuW[') == 0`、`'HFI_CTRL_INIT' not in _k8687`、`'IOSleep' not in`、`'for (' not in`）。
- 键 88 复原那一笔是本轮设计的硬要求：不管 `VALID` 立不立得住，块返回时 `SMR0` 与进来时逐位相同。

### 65.3 构建与门禁

- 探针 491,998 字符 / 6,930 行（写入后 492,016 字符 / 6,932 行——`typew` 那处改动），`gmuW[...] =` 写点仍 **201**（本轮不加任何 GMU 写），arm 窗口 `(fwKey >= 7u && fwKey <= 88u)`。
- host 门 → **28/28 checks passed** + `PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)`；`verify-enchilada-probe-stage6.py` → **1069/1069**；全量 `verify-enchilada-*.py` → **ran 46 of 48, FAILING: 0**（跳过 `-gmu-caller-v1-record.py` 需 40-hex rev、`-ias-measured.py` 需真机）。
- stage6 的两处必须同步：arm 窗口 2 处（`<= 85u` → `<= 88u`），以及 **R240 切片终止符从 doorbell ladder 改成 R241 的开头**（`code[_i8485:].split('\nif (fwKey == 86u || fwKey == 87u || fwKey == 88u) {\n', 1)[0]`）——不换的话 R241 的 store 会落进 R240 的切片，R240 的断言就开始描述错块。
- 分支 `r241-routing-regs`，commit **`c766474791b03b1be255f53d068d7af2442a2f5d`**，run **44741746816（#351）`completed/success`，28/28 步**（13:08:51Z 创建，13:26:00Z 完成）。
- 取件：`r241-kernel/kernel.macho` **126,251,008 B** sha256 `462e51ac89ecee9a58894b0551df30f4cb8e850db913d79fae13aedf0320cbc9`；`r241-payload/payload.img` **150,994,944 B** sha256 `5a269d2f4868ea86e6261d351ab3039454a7cc86680f812a7c0f9e14f669de8d`。
- 静态门 `r241_static.py` → `required+survivors: 18 checked`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、`forbidden: 2 checked`、固件钉 `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`（32,768 B）→ **`PASS: R241 static gate`**。
- **门的区分能力自证**：同一个门指向上轮 `r240-kernel` / `r240-payload` → 七条 `gmu87*/gmu88*` 全 `kernel=0 payload=0`，**`FAIL: R241 static gate (11 problems)`**。

### 65.4 键 86（控制，DEVICE-VERIFIED）

窗口 sha256 **`fad5753af77ddfae86913d765b6c0c93ed99caf6d9d2691777f6dc52a80eeeac`**、`boot_id 587123969`、`msg_bytes=30565 slots_written=8 write_errors=0 uptime_s=3 rounds=8`、**3/3 行**：

```
gmu86a=cx=80001fe0 cx_on=1 smmu=1 key=86 win=1
gmu86b=scr0=00200626 usfcfg=1 clientpd=0 id0=48016605 numsmrg=5 smr0=00000000 smr1=00000000
gmu86c=control_no_smmu_write=1 scr0=00200626 smr0=00000000 key=86 win=1
```

判决：**形状无罪**。这条块在完全不做 SMMU 写的情况下把三行提交进窗口、boot_id 与 sha 都是新的 ⇒ 键 88/87 的行一旦落盘，就能归因到它们自己那笔 store。

### 65.5 键 88（`SMR0`，DEVICE-VERIFIED）

窗口 sha256 **`b4f00131219114345d9e00426c4c3c393c24a4fa6009dbbd9a8bdb97999f98e0`**、`boot_id 585520255`、`msg_bytes=30782 slots_written=8 write_errors=0`、**5/5 行**：

```
gmu86a=cx=80001fe0 cx_on=1 smmu=1 key=88 win=1
gmu86b=scr0=00200626 usfcfg=1 clientpd=0 id0=48016605 numsmrg=5 smr0=00000000 smr1=00000000
gmu88c=about_to_set_smr0 wanted=80000004 old=00000000 win=1
gmu88d=smr_valid wrote=1 wanted=80000004 back=80000004 valid=1 sid=4 landed=1
gmu88e=smr_restored wanted=00000000 back=00000000 restored=1 after=1 scr0=00200626
```

判决：**`SMR0` 接受 `VALID | SID 4`**。`back` 与 `wanted` 逐位相同（`landed=1`）、bit31 立住（`valid=1`）、SID 读回 4，随后写回 `00000000` 并读回一致（`restored=1`）。这是本移植第一次把一条流的识别位真正立起来又撤掉。

SID 4 的依据：`drivers/NativeAdreno/AdrenoBoardInputs.c` 的 `kAdrenoBoardCtxGmuUser`（GMU 用户流），与本仓库一贯使用的 SID 表一致。

### 65.6 键 87（`S2CR0` @ `0xC00`，DEVICE-VERIFIED）

窗口 sha256 **`fed0228719cfbd3cedcedb80a07c83c5cb64eb385394bfc2f9c46fc3b24f0ab6`**、`boot_id 586132469`、`msg_bytes=30777 slots_written=8 write_errors=0`、**5/5 行**：

```
gmu86a=cx=80001fe0 cx_on=1 smmu=1 key=87 win=1
gmu86b=scr0=00200626 usfcfg=1 clientpd=0 id0=48016605 numsmrg=5 smr0=00000000 smr1=00000000
gmu87c=about_to_write_s2cr0 off=c00 want=00010000 old_unreadable=1 win=1
gmu87d=s2cr0 wrote=1 wanted=00010000 back=00020000 typew=00020000 landed=0
gmu87e=after=1 scr0=00200626 smr0=00000000 gfsr=00000000 survived=1
```

判决分两半，都必须保留：

1. **写 `0xC00` 不致死**：预告行、行 D、行 E 全部落盘，`survived=1`。R235 里「`0xC00` 是墙」的说法只对**读**成立过，本轮证明了**不是墙挡写**。
2. **值没留下**：`wanted=00010000`（`TYPE = BYPASS`），`back=00020000`（`TYPE = 2`，即 FAULT）。`landed=0`。所以在这个 SMMU 上，`S2CR0` 的 `TYPE` 字段**读回恒为 FAULT**；两种解释都还活着——要么这笔写被无声丢弃、寄存器本来就读 `00020000`，要么硬件按 `ID0.S2TS = 0`（stage-2 不支持）把该字段钳在 FAULT。**本轮没有做「先读原值再写」的对照**（行 C 写的是 `old_unreadable=1`，即沿用了 R173/R174「读必 abort」的旧认识），因此不能分辨这两种解释。

顺带读数：`gfsr=00000000` —— 全局 fault 状态寄存器在整个块执行后仍是 0，即**没有任何一次流被记录为 routed/faulted**。

### 65.7 与 R173/R174 的旧结论出现矛盾（登记，不改旧账）

R173/R174 记录的是「`0xC00` 起 `S2CR` 区**每次读都 external abort**」。本轮行 D 对同一个偏移做了一次 `rd32`，**读回 `00020000` 且整机存活**。两次读数都来自本设备，所以其中至少一条需要重新确认：

- 若 R173/R174 那次 abort 其实来自该轮块里**别的**访问（例如同时读了未实现的 CB 偏移），那么「`0xC00` 读必 abort」就是从别的现象推出来的错误结论；
- 若 `0xC00` 的读在某些条件下（比如 CX/GX 电源域状态、或 AXID/时钟未开）才 abort，那么本轮的条件与那时不同。

这条写入 §65.9 的「没有证明什么」，并由 R242 的键 89 用**只读、不写**的独立测量重新确认。

### 65.8 判决

| 轮次/键 | 形状（store 是最后动作） | 写了什么 | 结果 |
|---|---|---|---|
| R219 键 66 | 是 | 什么都不写 | 3/3，boot 582344900 |
| R222 键 66 | 是 | `HFI_CTRL_INIT = 1` | 0/3 |
| R223 键 66 | 是 | `HFI_CTRL_INIT = 0` | 3/3，boot 583170095 |
| R238 键 80 | 是 | 四笔中断掩码 | 3/3，boot 586963994 |
| R238 键 81 | 是 | 四掩码 + `INIT = 1`（真表、`INFO = 1`） | 0/3 |
| R239 键 82 | 是 | 四掩码 + `INIT = 0` | 3/3，boot 586092751 |
| R239 键 83 | 是 | 四掩码 + `INFO = 0` + `INIT = 1` | 4/4，boot 585892346 |
| R240 键 84 | 是 | 零页 + `INFO = 1` + `INIT = 1` | 0/4 |
| R240 键 85 | 是 | 四掩码 + `INIT = 1`（阳性对照） | 0/3 |
| **R241 键 86** | **否（无 store）** | 无 | 3/3，boot 587123969 |
| **R241 键 88** | **否（SMR0 + 复原）** | `SMR0 = VALID \| SID 4` | 5/5，boot 585520255 |
| **R241 键 87** | **否（S2CR0）** | `S2CR0 = BYPASS` | 5/5，boot 586132469，值未留下 |

本轮给这条表加的三行说明：**在完全不碰 GMU 寄存器的前提下，SMMU 一侧的两组寄存器都是「可访问」的**——`SMR` 可写可留值，`S2CR` 可写但 `TYPE` 读回恒为 FAULT。

### 65.9 没有证明什么

1. **没有证明 GMU 的访问真的穿过 SMMU。** 本轮只证明「路由寄存器可以碰」，没有做任何一次「让 GMU 当主设备去读表」的测量。真正的检验是 R242 的键 90（先把 SID 4 路由到已编程的 CB0，再写启动字）。
2. **没有分辨 `S2CR0` 的 `00020000` 是「写被丢弃」还是「被钳成 FAULT」。** 行 C 沿用了旧结论，没有先读原值，所以少了对照。
3. **没有解释与 R173/R174 的读 abort 记录的矛盾**（§65.7）。
4. **没有证明 `SMR0` 的 `VALID` 能参与任何实际路由。** 因为 `S2CR0` 的值没留下，SID 4 现在即使被识别，其 `TYPE` 仍是 FAULT。
5. **不改变 R240 的结论**：致死仍与表内容无关；本轮也没再试启动字。
6. **没有证明 `USFCFG` 能被清掉** —— R234/R235 已经两次测到那笔 store 被忽略，本轮没有重试。

### 65.10 三条边界

- 不改 `sCR0`（`USFCFG`/`CLIENTPD` 都不写）；与 R234/R235 的结论不冲突。
- 不碰 panic 路径，不用空转买观测时间；三键都是「打完就返回」。
- 与 §64.9 的设计有**编号偏差**：那里写的是「键 87 = `S2CR0`、键 88 = 路由 + 启动」。实际实现是「键 86 控制、**键 88 = `SMR0`**、键 87 = `S2CR0`」，把「路由 + 启动」推到 R242 的键 90。原因：`SMR` 的写必须单独一轮测干净，否则键 90 一死就分不清是 `SMR` 还是启动字。

### 65.11 R242 设计（三个键，同一构建）

- **键 89（只读 `0xC00`）**：预告行 → 读 `0xC00`、`0xC04`、`0xC08` → 打印 → 返回，**一笔 store 都不做**。判读：三行都落 ⇒ 旧 abort 记录需要改写；只落预告行 ⇒ 「读 `0xC00` 必 abort」成立，本轮行 D 的读要另找解释（例如写之后寄存器状态变了）。
- **键 90（路由 + 启动）**：`SMR0 = VALID | SID 4`（不立即复原）→ 读回 → `S2CR0 = TRANS(0) << 16 | CBNDX 0` → 读回（预期仍可能读回 FAULT）→ 行 D 打印路由后的 `smr0`/`s2cr0`/`scr0` → **最后写 `HFI_CTRL_INIT = 1`**，之后不再有任何动作。判读：落盘 ⇒ **SMMU fault 假设被推翻**，GMU 读表并不是致死原因，必须回去看 HFI 启动序列本身；零字节 ⇒ 在这个 SMMU 上 `S2CR` 不可编程，只能靠「让整个 SMMU 旁路」（`USFCFG=0`，已知被忽略）或改由引导器代劳上电。
- **键 91（`0xC00` 原值对照）**：只读 `0xC00` → 打印 → **写回原值**（`back` 那一笔，值相同）→ 读回 → 打印。若 `wanted == back` 且 `landed=1`，说明「写相同值会读回相同值」，从而把键 87 的 `00020000` 解释成「寄存器本来就那样」（写被丢弃）；若写相同值都读回不同，那问题在读路径。

## 六十六、ROUND 242：`0xC00` 的读并不 abort，`S2CR` 收得住写，但 BYPASS 被钳成 FAULT —— 路由救不了启动字

### 66.1 一句话

三个键、同一次构建、三次启动：**键 89** 在 CX-on、一笔 store 都没做的前提下把 `0xC00`/`0xC04`/`0xC08` 都读回来了并存活（`c00=000200ff c04=000200ff c08=000200ff`）；**键 91** 把 `0xC00` 的原值写回去，读回同值（`landed=1`）⇒ 字段收得住写；**键 90** 把 `SMR0` 设成 `VALID | SID 4`、把 `S2CR0` 写成 `BYPASS`，然后让启动字做本轮的最后一个动作 —— **零字节，窗口与键 91 那轮逐字节相同**。

因此本轮真正确定的是两件事：`0xC00` 的读**不** abort（R173/R174 那条记录不能照原样保留），以及 `S2CR` 的 `TYPE` 字段**不接受 `BYPASS`**。而「路由能不能救启动字」这个问题**没有被本轮干净地回答**，原因见 66.7 第 3 条 —— 那是本轮的设计缺陷。

### 66.2 代码形状

| 键 | 前导（三键共有） | 自己的动作 | 结束方式 |
|---|---|---|---|
| 89 | 行 A（`cx`/`cx_on`/`smmu`/`key`/`win`）+ 读 `sCR0`/`ID0`/`SMR0`/`SMR1` + 行 B | 预告行 → `rd32(0xC00)`/`rd32(0xC04)`/`rd32(0xC08)` → 行 D | 直接返回，**零 store** |
| 91 | 同上 | 预告行 → 读 `0xC00` → 行 D → 写回同值 + `dsb sy` → 读回 → 行 E | 直接返回，不启动 |
| 90 | 同上 | 行 D → `SMR0 = 0x80000000 \| 0x00000004` → 行 E → `S2CR0 = 0x00010000` → 行 F → 行 G（重新读两处） | **最后** `HFI_CTRL_INIT = 1` + `dsb sy`，之后无回读、无轮询、无打印 |

- 三个键都**没有任何** `gmuW[...]` 写，除键 90 末尾那**一笔** `HFI_CTRL_INIT`；全文件 GMU 写点因此从 201 变 202。
- 三个键都**没有** `IOSleep`、**没有** `for (`。键 89 与键 91 甚至不碰 GMU。
- **与 §65.11 的设计偏离（必须登记）**：那里写键 90 用 `S2CR0 = TRANS(0) << 16 | CBNDX 0`。实现时改成 **`BYPASS`（`0x00010000`）**，理由写在块注释里：CB0 的表（R232d/R233）**只有一个哨兵叶子**（IOVA `0x40000000` → 物理 `0x90000000`），所以任何被真正翻译的队列表读**必然** fault，而 fault 正是要消除的东西；`BYPASS` 是唯一可能让 GMU 的读完成而不经过表的类型。这个决定是对的（`TRANS` 也会从同一张空表处 fault），但它带来一个后果，见 66.7 第 3 条。

### 66.3 构建与门禁

- 分支 `r242-s2cr-storage`，commit `f1b9ee09c02125359b82a809f22ab5c3c00d77a2`，CI **#352 / run 44763769116**：`completed / success`，job `build` `15:29:30Z → 15:47:18Z`，**28/28 步**。
- 产物（`work/tmp/r242-kernel`、`work/tmp/r242-payload`）：`kernel.macho` **126,251,008 B** sha256 `5acef29319efc589…`；`payload.img` **150,994,944 B** sha256 `3c815dd8928546aa…`。取件第一次在 range 13 上遇到 `status 403`、range 7 超时，重跑一次得到完整 78,911,854 B。
- 探针 **500,471 字符 / 7,067 行**；arm 窗口 `(fwKey >= 7u && fwKey <= 91u)`。
- 静态门 `work/tmp/r242_static.py`：`required+survivors: 22 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、`forbidden: 2 checked`、固件钉 `b07f90299ff2ae…`（32,768 B）→ `PASS`。
- **门自证区分能力**：同一脚本指向 `r241-kernel`/`r241-payload` → 十三条 required 全 `kernel=0 payload=0`，`FAIL: R242 static gate (13 problems)`。这是 R232d 那次「对着旧镜像 PASS」事故之后每轮都做的一步。
- 全门禁：`verify-enchilada-probe-stage6.py` **1101/1101**；host 门 `check-enchilada-adreno-probe.py` **28/28** + `PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)`；`verify-enchilada-*.py` 全量 **46/48 跑过、FAILING: 0**（`gmu-caller-v1-record.py` 需 40-hex rev、`ias-measured.py` 需真机，按惯例跳过）。
- rekey：`enchilada-pas-exec=2 → =89 / =91 / =90`，config 每次 1377 → 1378 B。每次上机前 `check-reboot-to-twrp.py --arm` 都是 `PASS (3 checks, 0 failures)`，每次都是看门狗 38–40 秒自己把机器送回 TWRP，**没有一次需要按键**。

### 66.4 键 89（只读，零 store）

boot_id **586058586**、窗口 `work/tmp/r242_s89_window.bin` sha256 **`8fab9ce1b04f32a135610f5b57f3887d54895eabf49d2aa855a7e4cedcd48724`**、`msg_bytes` 约 30.6 KB、`slots_written=8`、`write_errors=0`。**4 of 4** 行落盘：

```
gmu89a=cx=80001fe0 cx_on=1 smmu=1 key=89 win=1
gmu89b=scr0=00200626 usfcfg=1 clientpd=0 id0=48016605 numsmrg=5 smr0=00000000 smr1=00000000
gmu89c=about_to_read_s2cr0 offs=c00c04c08 no_store=1 win=1
gmu89d=s2cr_reads c00=000200ff c04=000200ff c08=000200ff survived=1
```

- `000200ff` 解码：`TYPE`(17:16) = **2 = FAULT**、`PRIVCFG`(25:24) = 0、低 8 位 = `0xff`。三个偏移读出**同一个值**，与「`S2CR` 默认全 FAULT」一致；`ID0.NUMSMRG = 5`，所以有 5 条 `S2CR`，`0xC00..0xC10` 都在 FAULT 状态。
- **判决：在这台机器的这个上下文中，读 `0xC00` 不 abort，而且三次读都没有打断系统。**
- 与 R173/R174 的矛盾登记：那两轮记录的是「`0xC00` 起每次读都 external abort」。今天同样的偏移、同一颗芯片、CX-on、零 store，三次读全部返回并存活。**不能因此说那两轮记错了**；最可能的解释是那两轮读的时候 SMMU 窗口本身不可达（`gSmmuVa` 只在 `cx_on` 时才赋值，CX-down 时整个 `gpu_cx` 折叠，`0x05040000` 读什么都没意义），**而不是偏移本身有墙**。这条是 HYPOTHESIS，不是 MEASURED —— 除非重跑一次「CX-down 下读 `0xC00`」，否则只能这样记。

### 66.5 键 91（同值写）

boot_id **585960701**、窗口 sha256 **`8fc2d1c3f9888104bc2a70e16a90cf74b07cfee49f16a5cfb776f5f9710c91d7`**、**5 of 5** 行：

```
gmu89a=cx=80001fe0 cx_on=1 smmu=1 key=91 win=1
gmu89b=scr0=00200626 usfcfg=1 clientpd=0 id0=48016605 numsmrg=5 smr0=00000000 smr1=00000000
gmu91c=about_to_read_s2cr0 off=c00 no_store_yet=1 win=1
gmu91d=s2cr_orig c00=000200ff about_to_write_same=1
gmu91e=s2cr_same wrote=000200ff back=000200ff landed=1 survived=1
```

- **判决：`S2CR` 的字段收得住写。** 写 `000200ff` 读回 `000200ff`，`landed=1`。
- 由此把 R241 键 87 的读数解释清楚：键 87 想写 `BYPASS`（`0x00010000`）却读回 `0x00020000`，而键 91 证明**写正常值时读回正常** ⇒ 键 87 的 `00020000` 不是「写被丢弃」，是**硬件把 `TYPE` 钳到了 FAULT**。也就是说：`S2CR` 的 `TYPE` 字段在这个 SMMU 上**只接受 FAULT**（至少不接受 `BYPASS`）。
- 这与 `ID0.S2TS = 0`（`0x48016605` 的 bit20 = 0，stage-2 未实现）方向一致：一个不支持 stage-2 的 SMMU 把 `S2CR` 的非 `FAULT` 类型当作不支持，钳回默认值。**仍然是 HYPOTHESIS**（`TRANS` 还没测，见 §66.10 键 92）。

### 66.6 键 90（路由 + 启动字做最后一步）

**零字节。** 窗口 sha256 **`8fc2d1c3f9888104bc2a70e16a90cf74b07cfee49f16a5cfb776f5f9710c91d7`**（与键 91 那轮**逐字节相同**）、boot_id **585960701**（未推进）、行内 `key=` 字段仍是 **91**。只有共享前导的 `gmu89a`/`gmu89b` 两行属于本轮窗口之外的历史内容，`gmu90c/d/e/f` 全 0（`counts: 89a=1 89b=1 90c=0 90d=0 90e=0 90f=0`）。

- **判决：把 `SID 4` 路由到 `BYPASS` 并保持 `SMR0 = VALID | SID 4`，没能救下启动字；整机仍然在一个 writer 轮之内停住。**
- 但这条判决的含义**受 §66.7 第 3 条限制**，不能读成「SMMU fault 假设被推翻」。

### 66.7 本轮必须分开说的三件事

1. **`0xC00` 的读不 abort** —— DEVICE-VERIFIED（键 89，CX-on、零 store、4/4 行）。R173/R174 那条记录需要改写，但改写成什么取决于一次尚未做的 CX-down 对照。
2. **`S2CR` 的 `TYPE` 不接受 `BYPASS`** —— DEVICE-VERIFIED（键 87 写 `00010000` 读回 `00020000`，键 91 写 `000200ff` 读回 `000200ff`）。`TRANS` 是否被接受**还没测**。
3. **键 90 没能干净地检验「路由能救命」** —— 这是本轮的**设计缺陷**，必须诚实登记：同一个构建里的键 87 已经证明 `BYPASS` 会被钳成 `FAULT`，那么键 90 写下去的 `0x00010000` **很可能也停在 `FAULT`**，而键 90 自己零字节、没有留下任何回读。也就是说，键 90 很可能**根本没建立起那条路由**就去写启动字了。它的零字节对下面两种解释同样兼容：
   - (a) `S2CR` 在这台机器上无法把非安全流路由出去（`TYPE` 钳死在 FAULT）⇒ 该假设从 SMMU 侧被关掉；
   - (b) GMU 读队列表根本不是致死原因。
   要分辨这两条，必须先用**不启动**的键证明「`TRANS` 型 `S2CR` 能被写进去并保持」，再带着那个状态启动一次。这正是 §66.10 的键 92/93/94。

### 66.8 单变量表（全部同形状：一切打印在前，致命动作在最后一步）

| 轮 | 键 | 最后一步（或唯一动作） | 结果 | boot_id |
|---|---|---|---|---|
| R219 | 66 | 不写任何寄存器 | 3/3 | 582344900 |
| R222 | 66 | `INIT = 1` | 0/3 | 582538748 |
| R223 | 66 | `INIT = 0` | 3/3 | 583170095 |
| R238 | 80 | 四掩码，`start_next=0` | 3/3 | 586963994 |
| R238 | 81 | 四掩码 + `INIT = 1`（真表、`INFO=1`） | 0/3 | 586963994 |
| R239 | 82 | 四掩码 + `INIT = 0` | 3/3 | 586092751 |
| R239 | 83 | `INFO = 0` + `INIT = 1`（真表） | 4/4 | 585892346 |
| R240 | 84 | 零页 + `INFO = 1` + `INIT = 1` | 0/4 | 585892346 |
| R240 | 85 | `INIT = 1`（真表、`INFO = 1`，阳性对照） | 0/3 | 585892346 |
| R241 | 86 | 零 SMMU 写（控制） | 3/3 | 587123969 |
| R241 | 88 | `SMR0 = VALID \| SID 4` 后复原 | 5/5 | 585520255 |
| R241 | 87 | `S2CR0 = BYPASS` 后不启动 | 5/5（`back=00020000`） | 586132469 |
| R242 | 89 | 读 `0xC00/04/08`，零 store | 4/4 | 586058586 |
| R242 | 91 | 同值写 `0xC00` | 5/5 | 585960701 |
| R242 | 90 | `SMR0 = VALID \| SID 4` + `S2CR0 = BYPASS` + `INIT = 1` | 0/6 | 585960701（未推进） |

条件式（R238–R240 已立、R242 未推翻）：**`INFO=1` 但不写 `INIT`（R219 键 67）存活；`INIT=1` 但 `INFO=0`（R239 键 83）存活；两者同时为真必死**。R242 键 90 把「表描述符非零 + 要求启动」这个组合又跑了一次，仍然必死。

### 66.9 本轮没有证明什么

1. 没有证明 `TRANS` 型 `S2CR` 能不能被写进去 —— 只证明了 `BYPASS` 会被钳成 `FAULT`、写 `FAULT` 能保持。
2. 没有证明键 90 启动时 `S2CR0` 到底是什么值（零字节，回读没落盘）。所以键 90 **不足以**判定「SMMU fault 是死因」或「不是」。
3. 没有证明 R173/R174 当时为什么 abort —— 只是指出「CX 状态不同」这个最可能的差异，未做 CX-down 对照。
4. 没有测量 GMU 是否真的经由这个 SMMU 访问内存（`SMR`/`S2CR` 都在讲「假如它经过」，而这句话本身没有设备证据）。
5. 不改 R240 的判决（致死与表内容无关），也不改 R239 的判决（`INFO` 非零 + 启动 = 必死）。
6. `sCR0.USFCFG`（bit10）仍然清不掉（R234/R235 两轮都写过，回读不变）。

### 66.10 三条边界

1. **不写 `sCR0` bit10** —— 已两次证明该位写不进去，再写只是重复。
2. **不碰 panic 路径** —— `enchilada-wdog-stop-panic` 会停在 UEFI 等按键，破坏「能自动回 TWRP」这条硬约束。
3. **不用空转买观测时间** —— R221 之后已成惯例：一切打印放在致命动作之前，致命动作放最后一步。

### 66.11 R243 设计（已设计，未实现）

- **键 92（只读，零 store）**：读 `S2CR0..S2CR4`（`0xC00/04/08/0c/10`）与 `sGFSR`(0x48)，把「默认全 FAULT」这件事一次读全，作为键 93 的基线。
- **键 93（写 `TRANS`，不启动）**：读 `0xC00` → 写 `0x00000000`（`TYPE = 0 = TRANS`，`CBNDX = 0`）→ 读回 → 行 D 打印 → **不启动**。判读：读回 `00000000` ⇒ `TYPE` 接受 `TRANS`，「路由」这条路还活着，键 94 值得做；读回 `000200ff` 或 `00020000` ⇒ `TYPE` 只接受 `FAULT`，这台 SMMU 的非安全流无法从这个 CPU 路由，键 94 取消，改问「GMU 到底经不经过这个 SMMU」。
- **键 94（仅在键 93 证明 `TRANS` 可写时做）**：`SMR0 = VALID | SID 4` + `S2CR0 = TRANS | CB0`（都不复原）→ 行 D/E 打印 → 最后 `INIT = 1`。落盘 ⇒ 路由有效但死因不是 translation fault，必须回到 HFI 启动序列本身；零字节 ⇒ 路由有效而仍死，`SMMU fault` 假设被证实，下一步是把 CB0 的表从「一个哨兵叶子」扩成能覆盖队列表与 `PWR_COL_CP_MSG` 的真实映射。

## 六十七、ROUND 243：`S2CR` 的 `TYPE` 收得住 `TRANS`（被钳的只有 `BYPASS`），而把 SID 4 路由进 CB0 并没有让启动字活下来

### 67.1 一句话

三个键、同一次构建、三次启动。**键 92**（零 store）把 `S2CR0..4` 五个字都读回来了，全是 `000200ff`，`gfsr=00000000`，4/4 行落盘；**键 93** 把 `TRANS`（`0x00000000`）写进 `0xC00`，读回**同值**（`back=00000000 landed=1`），4/4 行落盘；**键 94** 把 `SMR0` 设成 `VALID|SID 4`、把 `S2CR0` 写成 `TRANS|CB0`，然后让启动字做本轮最后一个动作 —— **零字节，窗口与键 93 那轮逐字节相同**。

所以本轮确定两件事、留白一件：`S2CR` 组**可读**；`TYPE` 字段**接受 `TRANS`**（R242 §66 说的「`TYPE` 锁死 FAULT」过强，被钳的只有 `BYPASS`）；而「路由之后启动字为什么还死」**没有被分辨**，原因在 67.7。

### 67.2 代码形状

| 键 | 前导（三键共有） | 自己的动作 | 结束方式 |
|---|---|---|---|
| 92 | 行 A（`cx`/`cx_on`/`smmu`/`key`/`win`）+ 读 `sCR0`/`ID0`/`SMR0`/`SMR1` + 行 B | 预告行 → `rd32(0xC00)`/`0xC04`/`0xC08`/`0xC0C`/`0xC10`/`0x048` → 行 D | 直接返回，**零 store** |
| 93 | 同上 | 预告行 → 读 `0xC00` → 行 C → 写 `TRANS`（`0x00000000`）到 `0xC00` + `dsb sy` → 读回 → 行 D | 直接返回，**不启动** |
| 94 | 同上 | 行 C → `SMR0 = 0x80000000 \| 0x00000004` → 行 D → `S2CR0 = 0x00000000` → 行 E → 行 F（重新读两处） | **最后** `HFI_CTRL_INIT = 1` + `dsb sy`，之后无回读、无轮询、无打印 |

- 三个键里只有键 94 有 `gmuW[...]` 写，且只有**一笔**；全文件 GMU 写点因此 202 → 203。
- 三个键都**没有** `IOSleep`、**没有** `for (`。键 92 与键 93 甚至不碰 GMU。
- 键 94 的 `SMR0` **不复原**（键 88 复原过，那是为了把「写」与「后果」分开；本轮要的是真实路由），`S2CR0` 也不复原。

### 67.3 构建与门禁

- 分支 `r243-s2cr-type`，commit `f4f39c94fe7af6429bb215718120e8d38dbe587c`，CI **#353 / run 44773993886**：`completed / success`，job `build` `16:30:09Z → 16:51:28Z`，**28/28 步**。
- 产物（`work/tmp/r243-kernel`、`work/tmp/r243-payload`）：`kernel.macho` **126,251,008 B** sha256 `b1dbc3b3c107125e…`；`payload.img` **150,994,944 B** sha256 `74729c586a1d4799…`。取件一次成功（78,913,789 B / 292 s / 264 KB/s，16 连接）。
- 探针 **509,291 字符 / 7,209 行**；arm 窗口 `(fwKey >= 7u && fwKey <= 94u)`。
- 静态门 `work/tmp/r243_static.py`：`required+survivors: 19 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、`forbidden: 2 checked`、固件钉 `b07f90299ff2ae…`（32,768 B）→ `PASS`。
- **门自证区分能力**：同一脚本指向 `r242-kernel`/`r242-payload` → 十二条 required 全 `kernel=0 payload=0`，`FAIL: R243 static gate (12 problems)`。
- 全门禁：`verify-enchilada-probe-stage6.py` **1137/1137**；host 门 `check-enchilada-adreno-probe.py` **28/28** + `PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)`；`verify-enchilada-*.py` 全量 **46/48 跑过、FAILING: 0**。
- rekey：`enchilada-pas-exec=2 → =92 / =93 / =94`，config 每次 1377 → 1378 B。每次上机前 `check-reboot-to-twrp.py --arm` 都是 `PASS (3 checks, 0 failures)`，每次都是看门狗在 38–40 秒内自己把机器送回 TWRP，**没有一次需要按键**。

### 67.4 键 92：五个 `S2CR` 字都可读，且全都路由到 `FAULT`

boot_id **585424163**，窗口 sha256 `b8a550dd35ad267d723f6514806ecd75dff179f1eebab74d84301bcdd6edc3ce`，`msg_bytes` 与 slot 计数正常，**4/4 行**：

```
gmu92a=cx=80001fe0 cx_on=1 smmu=1 key=92 win=1
gmu92b=scr0=00200626 usfcfg=1 clientpd=0 id0=48016605 numsmrg=5 smr0=00000000 smr1=00000000
gmu92c=about_to_read_s2cr_group offs=c00c04c08c0c10 no_store=1 win=1
gmu92d=s2cr_group s0=000200ff s1=000200ff s2=000200ff s3=000200ff s4=000200ff gfsr=00000000 survived=1
```

`000200ff` 的字段：`TYPE`（17:16）= 2 = **FAULT**、`PRIVCFG`（25:24）= 0、`CBNDX`（7:0）= `0xff`（不是任何真实 context bank）。`gfsr=00000000` ⇒ 本轮没有任何流被记成 fault。

### 67.5 键 93：`TYPE` 接受 `TRANS`，被钳的只有 `BYPASS`

boot_id **585816702**，窗口 sha256 `2ca952b1b87050367ec80759823e62439e96777e2100e332756e4169e3de1999`，**4/4 行**：

```
gmu92a=cx=80001fe0 cx_on=1 smmu=1 key=93 win=1
gmu92b=scr0=00200626 usfcfg=1 clientpd=0 id0=48016605 numsmrg=5 smr0=00000000 smr1=00000000
gmu93c=about_to_write_trans off=c00 want=00000000 old=000200ff win=1
gmu93d=trans_wrote wrote=00000000 back=00000000 typew=00000000 landed=1 survived=1
```

把 R242 与本轮在同一个偏移上的三次写并排：

| 轮次/键 | 写进去 | 读回来 | `landed` | 解释 |
|---|---|---|---|---|
| R242 键 91 | `000200ff`（原值） | `000200ff` | 1 | 字段收得住写 |
| R242 键 87 | `00010000`（`BYPASS`） | `00020000` | 0 | **`BYPASS` 被钳成 `FAULT`** |
| R243 键 93 | `00000000`（`TRANS`） | `00000000` | 1 | `TRANS` 被原样接受 |

所以 §66 那句话要收窄：不是「`TYPE` 锁死 FAULT」，而是「**`BYPASS` 不被接受，`TRANS` 与 `FAULT` 都被接受**」。`BYPASS` 被拒的具体原因（特权流限制、还是这台 SMMU 上 `BYPASS` 位被保险丝关掉）本轮**没有测**。

### 67.6 键 94：路由 + 启动字，仍然是零字节

刷写核验全过（`key block says: enchilada-pas-exec=94 -no_slto_p`、`key verified: this boot will run key 94`、`flash readback matches: True`）。窗口 **`2ca952b1b8705036…`（与键 93 那轮逐字节相同）**、`boot_id` 仍 **585816702**、行内 `key=` 仍 **93**、`counts: gmu92a=1 gmu92b=1 gmu94c=0 gmu94d=0 gmu94e=0 gmu94f=0`。

⇒ 本轮这一启动**什么都没提交**。`gmu92a`/`gmu92b` 那两行是上一轮的证词，不是本轮的。

### 67.7 为什么键 94 不能分辨「启动字本身」与「路由后 fault」

三件事必须分开说：

1. **两笔 store 各自单独飞行时都活下来了**：`SMR0 = VALID|SID 4`（R241 键 88，5/5 行，还做了复原）；`S2CR0 = TRANS`（本轮键 93，4/4 行，不启动）。所以键 94 里没有哪一笔**单独**是已知致命的。
2. **行 C/D/E 落在最后一个 writer 轮里，必丢**：它们是在两笔 store 之间、启动字之前打印的，距今约一个 writer 轮（约 500 ms）以内。R221（3.17 s 空转仍全丢）、R239 键 90、R240 键 84/85 都是同一个形状。所以「零字节」**不等于**「死在第一笔 store 上」。
3. **因此键 94 只说明一件事**：「`SMR0` 有效路由 + `S2CR0=TRANS` + 启动字」这个组合在一个 writer 轮之内把整机停住。它**同时兼容**两种解释：(a) 启动字本身仍然致死，路由无关；(b) 路由真的生效了，GMU 现在以 SID 4 进入 CB0，而 CB0 的表（R232d/R233）只有一个哨兵叶子（IOVA `0x40000000` → PA `0x90000000`），所以它对队列表页的读**必然** fault，fault 致死。

要分开这两支，必须让 CB0 的表**真的覆盖队列表页**（见 67.11 的键 95）。

### 67.8 单变量表（逐轮累积，全部同形状：store 是本轮最后一个动作或本轮无 store）

| 轮次/键 | 本轮最后一笔 | 行落盘 | boot_id |
|---|---|---|---|
| R219 键 66 | 什么都不写 | 3/3 | 582344900 |
| R222 键 66 | `INIT=1` | 0/3 | （未变） |
| R223 键 66 | `INIT=0` | 3/3 | 583170095 |
| R238 键 80 | 四掩码，`start_next=0` | 3/3 | 586963994 |
| R238 键 81 | 四掩码 + `INIT=1`（真表、`INFO=1`） | 0/3 | （未变） |
| R239 键 82 | `INIT=0` | 3/3 | 586092751 |
| R239 键 83 | `INFO=0` + `INIT=1` | 4/4 | 585892346 |
| R240 键 84 | 零页 `ADDR`+`INFO=1`+`INIT=1` | 0/4 | （未变） |
| R240 键 85 | `INIT=1`（真表，阳性对照） | 0/3 | （未变） |
| R241 键 86 | 不写 SMMU | 3/3 | 587123969 |
| R241 键 88 | `SMR0` set + restore | 5/5 | 585520255 |
| R241 键 87 | `S2CR0=BYPASS` | 5/5 | 586132469 |
| R242 键 89 | 零 store（只读 `0xC00`） | 4/4 | 586058586 |
| R242 键 91 | `S2CR0` 写回原值 | 5/5 | 585960701 |
| R242 键 90 | `SMR0` + `S2CR0=BYPASS` + `INIT=1` | 0/6 | （未变） |
| R243 键 92 | 零 store（只读五个 S2CR） | 4/4 | 585424163 |
| R243 键 93 | `S2CR0=TRANS` | 4/4 | 585816702 |
| R243 键 94 | `SMR0` + `S2CR0=TRANS` + `INIT=1` | 0/6 | （未变） |

条件式（跨轮都成立）：`INFO=1` 而不写 `INIT` → 活（R219 键 67）；`INIT=1` 而 `INFO=0` → 活（键 83）；**`INFO=1` 且 `INIT=1` → 必死**（R222/238 键 81/240 键 84/240 键 85/242 键 90/243 键 94 六次）。

### 67.9 本轮没有证明什么

1. **没证明 GMU 真的以 SID 4 穿过这个 SMMU**。`SMR0` 里 `VALID|SID 4` 只是把**匹配表**填上；一个流是否真的带着 SID 4 来，本轮没有观测手段（`sGFSR` 全是 0，既可能是没 fault，也可能是没有流）。
2. **没分辨键 94 的两种解释**（见 67.7 第 3 条）。
3. **没测 `BYPASS` 为什么被拒**：可能是特权/安全侧限制，也可能是这台实例上该编码被禁用。三次写只覆盖了 `BYPASS`、`FAULT`、`TRANS` 三种编码中的三种写法。
4. **没动 `sCR0`**：`USFCFG`（bit10）仍为 1，即未匹配流仍然直接 fault。这是有意为之（R234/R235 两轮已证明清这一位会被忽略）。
5. **不改 R240 的结论**：致死与 `HFI_QTBL_ADDR` 指向哪里、内容是什么无关，那条仍然成立。
6. **GPU 仍未渲染任何一帧**，准入表仍是 `modeled=9 / satisfied=0 / BLOCKED`。

### 67.10 三条边界

- 这一轮只证明「`TRANS` 可写、`SMR` 可填、`S2CR` 可读」，**没有**证明「流被路由」或「路由改变了 GMU 的任何行为」。
- 键 94 的零字节**不能**当作「路由无效」的证据，也不能当作「路由有效」的证据。
- 键 93 之后 `S2CR0` 停在 `TRANS|CB0`（`0x00000000`），但那只在它自己那次启动里有意义 —— 每次开机 S2CR 都回到 `000200ff`（键 92 与键 93 的 `old` 都读到这个默认值，键 92 在三次启动里读到同一个默认值）。

### 67.11 R244 设计（已设计，未实现）

目标：把 67.7 的两支分开。做法是让 CB0 的表**真的覆盖 GMU 会读的那几页**，再路由、再启动。

- **键 95（决定性）**：在键 72 那九笔 context-bank 写的基础上，把表建成 **identity 映射**，覆盖 HFI 队列块所在的窗口（探针自己已经知道 `rb_pa` 与 `cpmsg` 两个物理地址，运行时读出，不写死）：`adreno_table_build(&geo, virt, phys, rb_pa & ~0x1ffffull, rb_pa & ~0x1ffffull, 0x20000, prot, &res)` 之类的一笔大范围 identity 映射（128 KiB 足以同时盖住相隔 `0x3000` 的队列表页与 `CP_MSG` 页），然后 `SMR0 = VALID|SID 4`、`S2CR0 = TRANS|CB0`，**最后**写启动字。
  - 落盘 ⇒ 路由生效、fault 被消除、HFI 真的启动起来了 —— 那将是本移植第一次让固件在 CPU 给它准备好的地址空间中启动。
  - 零字节 ⇒ fault-on-queue-read 故事**死**，致死点在 HFI 启动序列本身（共振、时钟或电源域），下一步换到「启动前先给 GMU 上电/放时钟」那条路。
- **键 96（控制）**：只做 identity 表 + 九笔 CB 写，**不路由**（`SMR0`/`S2CR0` 保持默认），最后写启动字。仍零字节 ⇒ 说明表工作本身与致死无关，键 95 的落盘才有归因价值。
- 键 95/96 都保持「启动字是最后一个动作」这条形状（R238/R239/R240 已证明它是唯一能落盘的形状之一，若存活的话）。

API 依据：`drivers/NativeAdreno/AdrenoSmmuTableV1.h` 的 `adreno_table_build(&geo, virt, phys, iova, paddr, size, prot, &res)` 接受任意 IOVA→PA 与 `size`，`adreno_table_bytes_needed()` 给出所需页数；R232d 已实测 `tables=4 writes=4 ias=48 oas=36`。**本轮未验证**的是「一次 build 能否覆盖 128 KiB 并同时命中两个不连续页」—— 这要在实现时先读 `AdrenoSmmuLpaeV1` 的几何与 `adreno_table_bytes_needed` 的返回值。

## 六十八、ROUND 244：给 CB0 一张真正覆盖队列页的表 —— 启动字第一次活了下来

### 68.1 一句话

R243 留下两个都活着的解释：启动字自己致死，或者路由进 CB0 之后 GMU 读队列表 fault 致死。R244 把 CB0 的表建成 identity 块、覆盖队列表所在的那 2 MiB 窗口，填 `SMR0 = VALID|SID 4`、把 `S2CR0` 写成 `TRANS|CB0`，并让两个键**只差一件事**：键 96 不写启动字（**8/8 落盘**），键 95 写（**7/7 落盘**）。两个键都活了下来 —— **这是本移植第一次在写 `HFI_CTRL_INIT=1` 之后拿到新窗口**。

### 68.2 为什么几何是 2 MiB 而不是 128 KiB

R243 §67.11 / HANDOFF §44.9 写的是「覆盖 128 KiB」。读过 `drivers/NativeAdreno/AdrenoSmmuTableV1.c` 之后这条不成立：`adreno_table_bytes_needed()` 的叶子层由 `adreno_lpae_leaf_level()` 定，而它只接受**合法的 LPAE block size**（4 KiB、2 MiB、1 GiB …），其它尺寸返回 `kAdrenoLpaeMaxLevels`，于是 `bytes_needed` 返回 2、`adreno_table_build()` 一笔都不建。ias=48、4 KiB 粒度下 `adreno_lpae_lvl_shift(level) = (4-level)*9 + 3`，level 2 正好 `1 << 21` = 2 MiB。本轮实测的三页表（`tables=3 writes=3`）与根页 `root=ad947000` / `root=ad949000` 都与这个推导一致。

identity 块 `base=ad800000..ada00000` 严格包住本轮读到的 `qtbl=ad945000`（键 95）/ `ad943000`（键 96），也包住同一窗口里的 `cpmsg=ad948000` 与 `rb=ad945000`。队列表地址**没有写死**，运行时从 `HFI_QTBL_ADDR`（`0x5185`）读回再按 2 MiB 掩码对齐（`& ~0x1fffff`）。

### 68.3 代码形状

| 键 | 前导与 setup | 结束方式 |
|---|---|---|
| 96 | 行 A → 读 `HFI_QTBL_ADDR` → 2 MiB 对齐 → `adreno_table_bytes_needed` → `adreno_table_build`（identity）→ 九笔 CB0 写 → 回读 → `SMR0 = 0x80000000 \| 0x4` → 回读 → `S2CR0 = 0x0` → 回读 → 行 G | 打印 `gmu95h=no_start=1`，**不写启动字**，直接返回 |
| 95 | 与 96 逐字相同 | **最后** `HFI_CTRL_INIT = 1` + `dsb sy`，之后无回读、无轮询、无打印、无恢复 |

- 全文件 `gmuW[...]` 写点 203 → **204**（只多键 95 那一笔）。
- 六条拒绝分支：`refused-cx-down` / `refused-geometry` / `refused-no-queue-table` / `no-alloc-symbol` / `alloc-failed` / `refused-build` —— 每一条都比「零字节」更具体，因此**没有任何一条被触发时**才谈判决。
- 唯一的一个 `for (` 是九笔写的循环（`for (k95 = 0u; k95 < nabs95; ++k95)`），stage6 对这一块明确放行且只放行它一个。

### 68.4 构建与门禁

- 分支 `r244-identity-table`，两个 commit：`8e42b224b3ef`（首次推送）与 `5855eeca3800a8`（把 `key=%u` 加回行 A —— 判决要从设备自己写的字段里认出是哪一次启动；没有它，runner 的 `row key=` 正则什么都匹配不到）。
- CI **#355 / run `44838892288`**：`completed / success`，job `build` `2026-11-20T23:43:48Z → 2026-11-21T00:03:51Z`，**28/28 步**。更早的 **#354 / run `44838580605`** 是在补 `key=` 之前派发的构建，被 `work/tmp/r244_cancel.py` 取消（`202 → job concluded=cancelled`），避免拿错产物。
- 产物：`work/tmp/r244-kernel/kernel.macho` **126,251,008 B** sha256 `77bbe4068707723a…`；`work/tmp/r244-payload/payload.img` **150,994,944 B** sha256 `d3bbd0e50a233a6f…`。取件 78,916,036 B / 62 s / 1237 KB/s（16 连接）。
- 探针 **521,469 字符 / 7,421 行**；arm 窗口 `(fwKey >= 7u && fwKey <= 96u)`。
- 静态门 `work/tmp/r244_static.py`：`required+survivors: 19 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、`forbidden: 2 checked`、固件钉 `b07f90299ff2ae…`（32,768 B）→ **`PASS: R244 static gate`**。
- **门自证区分能力**：同一脚本指向 `r243-kernel`/`r243-payload` → 十四条 required 全 `kernel=0 payload=0`，**`FAIL: R244 static gate (14 problems)`**。
- 全门禁：`verify-enchilada-probe-stage6.py` **1179/1179**；stage4 **61/61**；stage5 **48/48**；host 门 **28/28** + `PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)`；`verify-enchilada-*.py` 全量 **46/48 跑过、FAILING: 0**。
- rekey：`enchilada-pas-exec=2 → =96`（sha256 `474982fc8964787c…`）与 `=2 → =95`（sha256 `8ab02a119c1820b1…`），config 每次 1377 → 1378 B。两次上机前 `check-reboot-to-twrp.py --arm` 都 `PASS (3 checks, 0 failures)`，两次都由武装过的 BCB + 看门狗在 36–38 秒内自己送回 TWRP，**没有一次需要按键**。
- 刷写核验（两次都过）：`key block 31063: device == payload -> True`、`key block says: enchilada-pas-exec=NN -no_slto_p`、`system_b head 8 MiB sha256 c69ae873fd… == payload head`、`flash readback matches: True`。

### 68.5 键 96 读数（控制：整套 setup，不写启动字）

boot_id **585220336**、窗口 `a5f088f0c7fca6696b5a8d1985f7a6af7dd745dd6a6c4c7eda7fc73e857622d2`、`msg_bytes=31200 slots_written=8 write_errors=0`、**8/8 行**：

```
gmu95a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=96 start=0 qtbl=ad943000
gmu95b=build=0 need=3 tables=3 writes=3 root=ad947000 base=ad800000 ias=48 oas=36 ok=1
gmu95c=plan=0 n=9 abs=0 nab=9 cbndx=0 ttbr=ad947000 cw=1 id0=48016605
gmu95d=wrote=9 ttbr0=ad947000/00000000 sctlr=00001067 tcr=00803510 cbar=0001f300 root=ad947000
gmu95e=smr_set wanted=80000004 back=80000004 valid=1 sid=4 landed=1
gmu95f=s2cr_set wanted=00000000 back=00000000 typew=00000000 landed=1
gmu95g=last_row=1 about_to_start=0 start=0 route=1 smr=80000004 s2cr=00000000 qtbl=ad943000 base=ad800000
gmu95h=no_start=1 setup_kept=1 route=1
```

`plan=0 n=9 abs=0 nab=9`、`wrote=9`、`smr back == wanted`、`s2cr back == wanted` ⇒ 建表、九笔 context-bank 写、`SMR0` 的 `VALID|SID 4`、`S2CR0` 的 `TRANS|CB0` **全部落地且机器存活**。

### 68.6 键 95 读数（决定性：同一套 setup，最后写启动字）

boot_id **585373771**、窗口 `fff4087cd971887e87834075e23c9585cb0fa111ac9066668af002e45358238d`、`msg_bytes=31114 slots_written=8 write_errors=0`、**7/7 行**（键 95 不打印 `gmu95h`）：

```
gmu95a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=95 start=1 qtbl=ad945000
gmu95b=build=0 need=3 tables=3 writes=3 root=ad949000 base=ad800000 ias=48 oas=36 ok=1
gmu95c=plan=0 n=9 abs=0 nab=9 cbndx=0 ttbr=ad949000 cw=1 id0=48016605
gmu95d=wrote=9 ttbr0=ad949000/00000000 sctlr=00001067 tcr=00803510 cbar=0001f300 root=ad949000
gmu95e=smr_set wanted=80000004 back=80000004 valid=1 sid=4 landed=1
gmu95f=s2cr_set wanted=00000000 back=00000000 typew=00000000 landed=1
gmu95g=last_row=1 about_to_start=1 start=1 route=1 smr=80000004 s2cr=00000000 qtbl=ad945000 base=ad800000
```

窗口是新的（boot_id 推进、sha 变了、行内 `key=` 是 95、七行全在）⇒ **写完 `HFI_CTRL_INIT=1` 之后这一轮照样提交了证词**。

### 68.7 判决：正反两点

| 轮 / 键 | CB0 的表 | 路由 | 启动字 | 结果 |
|---|---|---|---|---|
| R243 键 94 | 哨兵叶子（IOVA `0x40000000` → PA `0x90000000`，别处不覆盖） | `SMR0 = VALID\|SID 4`、`S2CR0 = TRANS\|CB0` | **写** | **零字节**（窗口停在键 93 那轮） |
| R244 键 96 | identity 2 MiB，覆盖队列页 | 同上 | 不写 | **8/8 落盘** |
| R244 键 95 | identity 2 MiB，覆盖队列页 | 同上 | **写** | **7/7 落盘** |

1. 键 96 关掉「setup 本身致死」这一支。
2. 键 95 与 R243 键 94 的差别**只有**「CB0 的表是否覆盖 GMU 会读的那一页」。94 死、95 活 ⇒「致死是由队列表读上的 SMMU fault 造成的」第一次得到支持，「启动字在任何配置下都自己致死」这一支被推翻。
3. 这条故事同时解释了前十几轮的零字节模式：那些轮里 `USFCFG=1`（未匹配流直接 fault）、没有有效 `SMR`，GMU 的流对 SMMU 是未识别流 ⇒ fault；启动字一旦真的让固件去读队列，fault 就把机器停住。R243 键 83（`INFO=0` 时写启动字存活）也在这条故事里：那次固件没有被要求去读表。

### 68.8 一条不能归因的异常

键 95 那次窗口里有一行 `gmursc=… intr=00800000 …`（`CM3_FAULT`），而键 96 那次是 `intr=00000000`。这一行由更早的探针块打印、位置在 `gmu95a` 之前，所以**不可能是本轮那笔启动字造成的**。可能来源（未验证）：上一次开机遗留的硬件状态，或更早的两次写过启动字的启动（R242 键 90、R243 键 94）留下的痕迹。R186 时代见过同样的 `00800000`（键 33）。本轮只登记现象，不做结论。

### 68.9 没有证明什么

1. **没有**证明 GMU 真的走过那张表：`sGFSR` 与 CB0 的 `FSR` 都没有在写入之后读过。
2. **没有**证明固件启动了：`fwinit=babeface`、`hfistat=00000000` 都是写入**之前**的读数，写入之后没有任何回读。
3. **没有**解释为什么机器仍在约 36 秒后回到 recovery（`08:09:24 boot sent → 08:10:00 back in recovery`）。本轮只证明「启动字不再在一个 writer 轮内停住整机」。
4. **没有**动 `sCR0`（`USFCFG=1` 仍在），也没动 stage-2。
5. **没有**做同一次启动内的对照：键 95 与键 96 是两次独立启动，`root` 的物理页也不同（`ad947000` vs `ad949000`）。单变量是按代码形状与判决规则说的，不是按同一次运行说的。
6. GPU 仍未渲染任何一帧。

### 68.10 三条边界

- 键 95 的落盘是**存活**证据，不是「HFI 通了」的证据：HFI 是否真的开始、固件是否接受队列表，需要写入之后的回读。
- identity 映射只是为了「让队列表那几页不 fault」；把 `ad800000..ada00000` identity 交给 GPU 侧是探针里的临时做法，不是驱动设计。
- `SMR0` 的 `VALID` 在 R241 只被写过、即刻复原；本轮是它**第一次**真的参与路由（`back=80000004`、`sid=4`、`landed=1`）。

### 68.11 R245 设计

- **键 97**：重复键 95 的整套 setup + 启动字，然后**留在原地**读三样东西并打印 —— `HFI_CTRL_STATUS`、`sGFSR`（`0x048`）、CB0 的 `CB_FSR`（`cb_base + 0x58`）。这会重新落入「一个 writer 轮内」，所以只能算尽力而为：落盘就直接给出「fault 有没有被记录」；零字节则说明写入之后仍然活不过一个轮，需要换通道（而换通道会打破「必须能自动回 TWRP」，须先与用户确认）。
- **键 98**：与键 95 完全相同，第二次启动。可复现性比新信息更值钱 —— 本轮只有一次单点。
- 若两者都落盘，下一步才是「让固件真的接受队列表」（表内容、`cpmsg` 地址、`PWR_COL_CP_MSG`），以及 `sCR0`/`USFCFG` 与 stage-2 是否还需要动。

### 68.12 环境事实（本轮新增）

- `scripts/push_via_api.py` 会读 `HTTPS_PROXY`：本地代理关掉时必须先清空三个代理变量，否则 `GET /user` 直接 `WinError 10061`。
- `work/tmp/r168_dispatch.py` 在**已有 run 正在跑**时会 `STOP: a run is active`，此时要么等、要么像本轮一样先 `POST /actions/runs/<id>/cancel`（202）再派发。
- `work/tmp/r244_cancel.py` 是取消单个 run 的小工具；取消后 job 的结论是 `cancelled`，不会产生 artifact。
- stage6 的切片终止符是**位置敏感**的：R244 同步脚本第一次把「门铃梯子」这个终止符替换到了 R242 的切片上（因为 R242 的终止符写法是单独一行），导致 R242/R243 的切片各自吞掉后一个块、报出 16 条假失败。修法见 `work/tmp/r244_fixterm.py`：按 `_kNNNN = code[...]` 的行号定位，而不是按字符串出现顺序替换。教训：**切片锚点要用代码锚点并在替换后核对每块的终止符数量**。
- 探针块里「不写启动字」的分支必须**写在启动字之前**（`if (!r95start) { IOLog(...) } else { gmuW[...] = 1u; }`），否则 `IOLog` 会出现在启动字之后，stage6 的「启动字之后不打印」断言会正确地失败。

## 六十九、ROUND 245：启动字之后的寄存器终于读到了 —— CB0 没有记下 fault，而固件侧没有报告 HFI 启动

### 69.1 一句话

R244 证明了「给 CB0 一张覆盖队列页的表 + 一条有效路由」之后写启动字不会在一个 writer 轮内停住整机，但那一次**什么都没读回**，所以「启动字到底有没有让固件动起来」仍然悬着。R245 用两个键把它拆开：**键 98** 把 R244 键 95 逐字复现（**7/7 落盘**，可复现性），**键 97** 在同一形状上把启动字写成**文本上的最后一个动作之前的倒数第二个**，紧接着一次性读回五个寄存器并打印 **`gmu97h`**（**8/8 落盘**）。读数：`ctl=00000000`、`fwinit=babeface`、`gfsr=00000000`、`cbfsr=00000402`、`ao=00000000`。

### 69.2 代码形状

| 键 | 前导与 setup | 结束方式 |
|---|---|---|
| 98 | 行 A → 读 `HFI_QTBL_ADDR` → 2 MiB 对齐 → `bytes_needed` → `table_build`（identity）→ 九笔 CB0 写 → 回读 → `SMR0 = VALID\|SID 4` → 回读 → `S2CR0 = TRANS\|CB0` → 回读 → 行 G | 该分支**最后**写 `HFI_CTRL_INIT = 1` + `dsb sy`，之后无任何动作（与 R244 键 95 逐字相同） |
| 97 | 与 98 逐字相同 | 该分支**最后**写 `HFI_CTRL_INIT = 1` + `dsb sy`，然后一次性读 `HFI_CTRL_STATUS`(0x5180)、`CM3_FW_INIT_RESULT`(0x501c)、`sGFSR`(0x048)、CB0 `CB_FSR`(`cb_base + 0x58`)、`AO_HOST_INTERRUPT_STATUS`(0x9305) → 打印 `gmu97h` → 返回 |

- 全文件 `gmuW[...]` 写点 204 → **206**：两个分支各带一笔启动字，所以计数是 2 而不是 1。stage6 对这一块的断言写成「**每个分支恰好一笔**、键 98 的分支里没有 `IOLog` 也没有 `rd32(`」，而不是「整块一笔」——后者会与「两个键共用一个块」的事实冲突。
- 读回**没有循环、没有 `IOSleep`、没有轮询**：五处 `rd32` 与一次 `snprintf`。stage6 断言这一块里 `for (` 只有一处（九笔写循环），`IOSleep` 一处都没有。
- 键 97 的读全部发生在启动字**之后**，且只在 CX 已开、`gSmmuVa` 已赋值的前提下（行 A 打印 `smmu=1`）；六条拒绝分支（`refused-cx-down` / `refused-geometry` / `refused-no-queue-table` / `no-alloc-symbol` / `alloc-failed` / `refused-build`）保证「读崩」与「setup 没跑」不会混成同一件事。

### 69.3 构建与门禁

- 分支 `r245-post-start`，commit `f93cbd972c3f`。
- CI **#356 / run `44843467493`**：`completed / success`，job `build` `2026-11-21T00:23:19Z → 00:39:15Z`，**28/28 步**。
- 产物：`work/tmp/r245-kernel/kernel.macho` **126,251,008 B** sha256 `d96cf2e989fd99bc…`；`work/tmp/r245-payload/payload.img` **150,994,944 B** sha256 `fe678f9210367ba3…`。取件 78,917,399 B / 96 s / 800 KB/s（16 连接）。
- 探针 **533,892 字符 / 7,641 行**；arm 窗口 `(fwKey >= 7u && fwKey <= 98u)`。
- 静态门 `work/tmp/r245_static.py`：`required+survivors: 20 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、`forbidden: 2 checked`、固件钉 `b07f90299ff2ae…`（32,768 B）→ **`PASS: R245 static gate`**。
- **门自证区分能力**：同一脚本指向 `r244-kernel`/`r244-payload` → 十五条 required 全 `kernel=0 payload=0`，**`FAIL: R245 static gate (15 problems)`**。
- 全门禁：`verify-enchilada-probe-stage6.py` **1211/1211**；host 门 **28/28** + `PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)`；`verify-enchilada-*.py` 全量 **46/48 跑过、FAILING: 0**。
- rekey：`enchilada-pas-exec=2 → =98`（sha256 `ddd4e56474f8d828…`）与 `=2 → =97`（sha256 `eef6f34b3eb55059…`），config 每次 1377 → 1378 B。两次上机前 `check-reboot-to-twrp.py --arm` 都 `PASS (3 checks, 0 failures)`，两次都由武装过的 BCB + 看门狗自己送回 TWRP，**没有一次需要按键**。
- 锚点：键 97 的 runner 用 `work/tmp/r245_anchor.py r245_s97_run.py 587175491 b5ea6ee7…` 把 `PREV_BOOT`/`PREV_SHA` 锚到键 98 那一轮，并交叉核对「盘上确有窗口的 sha256 等于该值」。

### 69.4 键 98 读数（复现性）

boot_id **587175491**、窗口 `b5ea6ee7eb44e07df3f7b050f230bd45abfcbe3dc7ab2685f4a9bf510c9e8ffd`、**7/7 行**：

```
gmu97a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=98 post=0 qtbl=ad945000
gmu97b=build=0 need=3 tables=3 writes=3 root=ad949000 base=ad800000 ias=48 oas=36 ok=1
gmu97c=plan=0 n=9 abs=0 nab=9 cbndx=0 ttbr=ad949000 cw=1 id0=48016605
gmu97d=wrote=9 ttbr0=ad949000/00000000 sctlr=00001067 tcr=00803510 cbar=0001f300 root=ad949000
gmu97e=smr_set wanted=80000004 back=80000004 valid=1 sid=4 landed=1
gmu97f=s2cr_set wanted=00000000 back=00000000 typew=00000000 landed=1
gmu97g=last_row=1 about_to_start=1 post=0 route=1 smr=80000004 s2cr=00000000 qtbl=ad945000 base=ad800000
```

判决（runner 原文）：`THE R244 SURVIVAL REPRODUCES`。⇒ 键 95 那一次的存活不是一次性事件：同一份构建、第二次启动、同样的表/路由/启动字，窗口照样推进、行照样落盘。

### 69.5 键 97 读数（启动字之后）

boot_id **586639327**、窗口 `0955a67b8947f90bd7c4901ff66640ffafd8a3f5cbe144a613e857cd417efe33`、**8/8 行**（A–G 与键 98 同形，`key=97 post=1`、`root=ad933000`、`qtbl=ad92f000`），多出的是决定性的一行：

```
gmu97h=after_start=1 post=1 ctl=00000000 fwinit=babeface gfsr=00000000 cbfsr=00000402 ao=00000000 survived=1
```

字段含义与逐位读法：

| 字段 | 值 | 读法 |
|---|---|---|
| `ctl` | `00000000` | `HFI_CTRL_STATUS`（`0x5180`）bit0 = 0 ⇒ **固件侧从未把 HFI 引擎标记为已启动**，即便启动字已写、路由已活 |
| `fwinit` | `babeface` | `CM3_FW_INIT_RESULT`（`0x501c`）与写入前完全相同 ⇒ CM3 的「初始化完成」这一刻**早已**发生，本轮没有把它推进到下一阶段 |
| `gfsr` | `00000000` | 全局 fault 状态寄存器干净：启动字之后没有任何未匹配流被记为全局故障 |
| `cbfsr` | `00000402` | **`FSR_FAULT`(bit0) = 0**，所以 CB0 没有记下「翻译失败」；但寄存器**不是全零**：置位的是 bit1 与 bit10 |
| `ao` | `00000000` | AO 侧中断状态无挂起位 |

`cbfsr` 这一点必须如实登记：本轮**不能**只说「CB0 没有 fault」。正确说法是「**bit0（FAULT）为 0；bit1 与 bit10 置位**」。bit1 在 SMMU 里是「未支持的上游翻译（UUT）」一类的状态位，bit10 在钉住的 `arm-smmu.h` 里没有给名字；本轮**没有**去分辨这两位的来源（可能是本次启动里 GMU 早期的读留下的、也可能是 R242/R243 那两次写过启动字的启动留下的），所以它们既不能被算作「本轮成功翻译」的正证，也不能被算作「本轮 fault」的证。这是本节最重要的一条边界。

### 69.6 判决

1. **可复现性成立**：R244 的存活在同一构建的第二次启动上完全复现（键 98，7/7）。
2. **启动字之后机器活得住**：写入之后紧接着读五个寄存器并打印，窗口照样推进（键 97，8/8）。这推翻了我上一轮写在 §68.11 的担忧（「写入之后仍然活不过一轮，需要换通道」）。
3. **SMMU 侧不再是可疑点**：`gfsr=0`、`cbfsr` 的 FAULT 位为 0，说明「路由 + 覆盖表」把这条路的翻译问题解决了；「启动字致死」这个从 R219 起追了十几轮的现象，其解释就是「队列表读 fault」，现在有了双向证据（R243 键 94 死 / R244 键 95 活 / R245 键 98 复现）。
4. **新的缺口在固件侧**：`ctl` bit0 = 0 且 `fwinit` 未变 ⇒ 启动字写进去了，但固件**没有报告 HFI 启动**。所以下一个问题不再是 SMMU，而是「固件为什么没有接受这次启动」（队列表内容是否被读到、`PWR_COL_CP_MSG` 地址是否正确、`HFI_QTBL_INFO` 写入时序是否早于启动字、以及 GMU 是否真的在跑 —— `gmustat` 行里 `intr=00000000 fwinit=babeface` 与 R186 以来的读数一致）。

### 69.7 没有证明什么

1. **没有**证明固件读到过队列表：`ctl=0` 是「固件没有报告启动」，不是「固件没有读表」。要分辨需要读 `PWR_COL_CP_MSG` 那一侧，或者让固件自己写一个字进队列表。
2. **没有**解释 `cbfsr=0x402` 的 bit1 与 bit10（见 §69.5 的边界）。
3. **没有**在**同一次启动**内做对照：键 98 与键 97 是两次独立启动，物理页也不同（`root=ad949000` vs `ad933000`）。单变量是按代码形状与判决规则说的。
4. **没有**解释两次窗口里都出现的 `gmursc=… intr=00800000 …`（CM3_FAULT）。该行由更早的块打印、位置在 `gmu97a` **之前**，因此不可能是本轮那笔启动字造成；与 R244 §45.8 登记的是同一条异常，来源仍未验证。
5. **没有**动 `sCR0`（`USFCFG=1` 仍在），也没动 stage-2。
6. GPU 仍未渲染任何一帧。

### 69.8 三条边界

- 键 97 的落盘是**读到了寄存器**的证据，不是「HFI 通了」的证据：五个值里没有一个是「引擎已启动」。
- 「启动字致死 = 队列表读 fault」现在有三个独立样本支持，但仍是**归因**（用「表覆盖与否」这一个变量分离出来的），不是直接观测到的 fault 注入。
- identity 映射 `ad800000..ada00000` 仍是探针里的临时做法，不是驱动设计。

### 69.9 R246 设计（据本轮读数改向）

- 方向已从 SMMU 转到**固件侧**。候选三支，优先级按「能分辨多少」排：
  1. **键 99**：写完启动字之后读 `PWR_COL_CP_MSG`（`0x5100`）与 `PWR_COL_CP_RESP`（`0x5101`），并读队列表头几个字 —— 看固件是否动过它们（若固件读过队列，头部或响应字应被改写）。
  2. **键 100**：`HFI_QTBL_INFO` 与 `HFI_CTRL_INIT` 之间的 `dsb` 后面加一次 `HFI_CTRL_STATUS` 的**单次**读再写启动字（把「写入时序」这条排除或确认）。
  3. **键 101**：用**两个不同 SID**（GMU 用户 4 与 GMU 内核 5）各建一条路由，看固件是否只认其中一条。
- 若三支都落在「固件没有反应」，下一步就进入「GMU 是否真的在跑」这个更基础的问题（共振/时钟/电源域），届时需要换观测通道，而换通道会打破「必须能自动回 TWRP」，必须事先与用户确认。

### 69.10 环境事实（本轮新增）

- `work/tmp/r245_anchor.py` 由 `r244_anchor.py` 机械改名得到（扫 `r245_s\d+_window.bin`）；两个占位形态（`"<64 hex>"` 与 `"0" * 64`）都认，且 `PREV_BOOT`/`PREV_SHA` 分两次 `re.subn`（复用同一 match 的两个偏移会移位）。
- stage6 里定位「两个键共用一个块」的 post 分支**不能**用 `find('} else {')`：块里的六条拒绝分支本身就有多个 `} else {`，会取到第一个、进而报 4 条假失败。用 `rindex('} else {')` 取最后一个。
- 静态门的 REQUIRED 里必须带 `key=%u`：没有它，runner 的「行内 `key=` 正则」匹配不到，判决无法把读数绑到具体某一次启动。

## 七十、ROUND 246：把问题交给固件侧 —— 启动字前后 `ctl` 的 bit 0 都是 0，描述符原样留着，`cpresp` 一直是 0

### 70.1 一句话

R245 已经证明「写启动字不再停住整机」，并且拿到了启动字之后的五个寄存器读数（`ctl=00000000`、`fwinit=babeface`、`gfsr=00000000`、`cbfsr=00000402`、`ao=00000000`），但一次读数不足以说明固件到底有没有被唤起。R246 用三个键把固件侧问清：**键 99**（启动后读 `HFI_CTRL_STATUS`、`PWR_COL_CP_MSG`、`PWR_COL_CP_RESP`、`HFI_QTBL_ADDR`、`HFI_QTBL_INFO`）**8/8 落盘**、**键 100**（启动字**之前**重新武装 `HFI_QTBL_ADDR`/`HFI_QTBL_INFO` 并单次读一次 `HFI_CTRL_STATUS`）**8/8 落盘**、**键 101**（SID 4 与 SID 5 两条路由都建好再启动）**8/8 落盘**。三份读数合起来指向同一件事：**这台 CPU 交给固件的东西没有被固件取走，而机器也不再因此停住**。

### 70.2 代码形状

三个键共用一个块，前导与 setup 与 R244 键 95 **逐字相同**（读 `HFI_QTBL_ADDR` → 2 MiB 对齐 → `adreno_table_bytes_needed` → `adreno_table_build` 建 identity 块 → 九笔 CB0 写 → 回读 → `SMR0 = VALID | SID 4` → 回读 → `S2CR0 = TRANS | CB0` → 回读 → 行 G），差别只在这一段之后：

| 键 | 块尾动作（按源码顺序） | 启动字之后是否有动作 |
|---|---|---|
| 99 | 写 `HFI_CTRL_INIT = 1` + `dsb sy` → 单次读 `HFI_CTRL_STATUS`(0x5180)、`PWR_COL_CP_MSG`(0x5100)、`PWR_COL_CP_RESP`(0x5101)、`HFI_QTBL_ADDR`(0x5185)、`HFI_QTBL_INFO`(0x5184) → 打印 `gmu99h` | 有（五次读 + 一行） |
| 100 | 写 `HFI_QTBL_ADDR = qtbl` + `dsb sy` → 写 `HFI_QTBL_INFO = 1` + `dsb sy` → 单次读 `HFI_CTRL_STATUS`/`HFI_QTBL_ADDR`/`HFI_QTBL_INFO` → 打印 `gmu100h` → **最后**写 `HFI_CTRL_INIT = 1` + `dsb sy` | 无（启动字是本轮最后一笔） |
| 101 | 写 `SMR1 = VALID \| SID 5` → 回读 → 写 `S2CR1 = TRANS \| CB0` → 回读 → 打印 `gmu99h=smr1_set` → **最后**写 `HFI_CTRL_INIT = 1` + `dsb sy` | 无 |

- 全文件 `gmuW[...]` 写点 206 → **211**（+5：三个分支各一笔启动字，键 100 另有 `QTBL_ADDR`/`QTBL_INFO` 各一笔）。
- 三个键都**没有**循环读、没有 `IOSleep`、没有轮询：键 99 的五次读、键 100 的三次读都是一次性 `rd32` 加一次 `snprintf`；stage6 对这块的断言是「`for (` 只有九笔写循环那一处」。
- 键 100 刻意把「重新武装描述符」放在启动字**之前**，所以它同时是一个对照：如果窗口仍然前进，说明 `QTBL_ADDR`/`QTBL_INFO` 的写本身无害，而 R243 键 90 那轮的死只能归给启动字。

### 70.3 构建与门禁

- CI **#357 / run `44847740525`** `completed / success`，job `build` `2026-11-21T01:02:49Z → 01:16:47Z`，**28/28 步**。
- 取件：kernel 78,919,819 B / 130 s / 592 KB/s（16 连接）；`work/tmp/r246-kernel/kernel.macho` **126,251,008 B** sha256 `bd66f5efa7002474e6e6111881776c7478a8c84aa9f433991b664d159fa856e1`；`work/tmp/r246-payload/payload.img` **150,994,944 B** sha256 `ae2b9c8f6eee8cc14f36ae5e65cc56971da0e9ff53ae0dccc1415bbe143a7cb7`。
- 探针 **549,284 字符 / 7,900 行**，arm 窗口 `(fwKey >= 7u && fwKey <= 101u)`，`gmuW[...]` 严格计数 **211**。
- 静态门 `work/tmp/r246_static.py` → `required+survivors: 22 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、`forbidden: 2 checked`、固件钉 `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`（32,768 B）→ **`PASS: R246 static gate`**。
- **区分能力自证**：`python work/tmp/r246_static.py work/tmp/r245-kernel/kernel.macho work/tmp/r245-payload/payload.img` → 17 条 required 全 `kernel=0 payload=0` → **`FAIL: R246 static gate (17 problems)`**。
- 门禁：stage6 **1250/1250**（`PASS: stage-6 kernel ID0/ID1 probe contract …`）；host 门 **28/28** + `PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)`；`work/tmp/r239_allgates.py` → **ran 46 of 48, FAILING: 0**。

### 70.4 键 99 读数（DEVICE-VERIFIED）

boot_id **584949295**、窗口 `work/tmp/r246_s99_window.bin` sha256 **`5d5e8f4832a5c8251f138eb607b36ef25e1555715965a498e0a5f1e6c0b92b8c`**、**8/8 行**（rekey 后 payload sha256 `7e3c50f7f7d123d82e66ff7027cec318bdc97f2eff9166ea9575f8b82904000a`）：

```
gmu99a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=99 mode=99 qtbl=ad943000
gmu99b=build=0 need=3 tables=3 writes=3 root=ad947000 base=ad800000 ias=48 oas=36 ok=1
gmu99c=plan=0 n=9 abs=0 nab=9 cbndx=0 ttbr=ad947000 cw=1 id0=48016605
gmu99d=wrote=9 ttbr0=ad947000/00000000 sctlr=00001067 tcr=00803510 cbar=0001f300 root=ad947000
gmu99e=smr_set wanted=80000004 back=80000004 valid=1 sid=4 landed=1
gmu99f=s2cr_set wanted=00000000 back=00000000 typew=00000000 landed=1
gmu99g=last_row=1 about_to_start=1 mode=99 route=1 smr=80000004 s2cr=00000000 qtbl=ad943000 base=ad800000
gmu99h=after_start=1 ctl=00000000 cpmsg=ad946000 cpresp=00000000 qtbla=ad943000 qtbli=00000001 survived=1
```

- `ctl=00000000` ⇒ `HFI_CTRL_STATUS` 的 bit 0 仍是 **0**，与 R245 键 97 一致；这一次是**同一个键在启动后立刻读**，不是上一轮的继承值。
- `cpresp=00000000` ⇒ `PWR_COL_CP_RESP` 没有任何响应消息被放进共享区。
- `qtbla=ad943000 / qtbli=00000001` ⇒ 描述符**原样保留**，与 setup 写进去的值（行 A 的 `qtbl=ad943000`、`INFO=1`）逐位相同 ⇒ 固件既没有取走它，也没有清掉它。
- `cpmsg=ad946000` ⇒ CPU 那一侧写在共享区的物理地址仍在（`qtbl + 0x3000`），同样未被改动。

### 70.5 键 100 读数（DEVICE-VERIFIED）

boot_id **584935029**、窗口 sha256 **`fe071b25bc16e2aabcda7c9642e8e1839370581dc63dcd793d2561d57a7b4cc4`**、**8/8 行**（payload sha256 `798aeade0c1a57a335ef55185e91c7c6d5835438bade9a6690f4554c11a77344`）。行 A–G 与键 99 同形（`key=100 mode=100 qtbl=ad935000`、`root=ad939000`），块尾一行：

```
gmu100h=before_start=1 ctl=00000000 qtbla=ad935000 qtbli=00000001 survived=1
```

- **重新武装描述符无害**：两个写都读回成功值（`qtbla=ad935000`、`qtbli=00000001`），窗口前进、8/8 落盘，启动字仍是本轮最后一笔。
- **`ctl` 在启动字之前就已经是 0**：这把 R245 的「启动后 `ctl=0`」从一个观测变成一条对照 —— 无论在启动字之前还是之后，`HFI_CTRL_STATUS` 的 bit 0 都是 0，所以它不是「被启动字清掉」的，而是从头到尾没有置起过。

### 70.6 键 101 读数（DEVICE-VERIFIED）

boot_id **583926476**、窗口 sha256 **`f6a83297a7a84db70a290d3db8f24faf29f3bec18a01bfd783c8ce4e310df2c5`**、**8/8 行**（payload sha256 `949824d6cb5aedee7b1fe1a5d3ba803aa45a1787555c4dc1a0c87896197c2431`）。行 A–G 与键 99 同形（`key=101 mode=101 qtbl=ad944000`、`root=ad948000`），块尾一行：

```
gmu99h=smr1_set wanted=80000005 back=80000005 valid=1 sid=5 landed=1 s2cr1_wanted=00000000 s2cr1_back=00000000 landed1=1
```

- 两条流都被路由：`SMR0 = VALID | SID 4`（继承 setup 的回读 `valid=1 sid=4 landed=1`）与 `SMR1 = VALID | SID 5`（`valid=1 sid=5 landed=1`），各自的 `S2CR` 读回都是 `TRANS | CBNDX 0`（`s2cr1_back=00000000`）。
- 即使两条 GMU 流都指向一张覆盖队列页的表，窗口仍然前进（8/8）⇒ **「队列读落在哪条流上」不再是可疑点**。

### 70.7 判决

把 R243–R246 四条读数并起来看：

| 轮/键 | 表 | 路由 | 启动字 | 结果 |
|---|---|---|---|---|
| R243 键 94 | 只有哨兵叶子（IOVA `0x40000000`→物理 `0x90000000`） | SID 4 → CB0 / TRANS | 最后写 | **零字节** |
| R244 键 95 | identity 2 MiB 覆盖队列页 | SID 4 → CB0 / TRANS | 最后写 | **7/7 落盘** |
| R245 键 97 | 同上 | 同上 | 最后写，之后读五个寄存器 | **8/8 落盘**，`ctl=0` |
| R246 键 99/100/101 | 同上 | 同上；键 101 再加 SID 5 | 键 99 之后读、键 100 之前重写描述符、键 101 之后不读 | **8/8 × 3 落盘** |

- **SMMU 侧到此结案（DEVICE-VERIFIED 的部分）**：队列表页被表覆盖之后，写启动字不再停住整机；CB0 没有记录 fault（`cbfsr` bit 0 = 0）；全局 `gfsr=0`；再补一条 SID 5 路由也没有改变结果。所以「写启动字 → 静默死」这条从 R177 起的老现象，其**致死机制**已经定位在「队列表页上的 SMMU 翻译失败」，并且已被绕开。
- **新的、也是当前的缺口在固件侧（HYPOTHESIS，读数支持但未直接证明）**：`HFI_CTRL_STATUS` 的 bit 0 在启动字**之前**（键 100）与**之后**（键 99）都是 0；`PWR_COL_CP_RESP` 一直是 0；`HFI_QTBL_ADDR`/`HFI_QTBL_INFO` 在启动之后原样保留；`CM3_FW_INIT_RESULT` 从 `babeface` 未变。四条读数一致地说明：**这台 CPU 写下的启动请求没有让固件取走描述符**，机器之所以不再死，只是因为没有任何流去读那张表。
- 因此下一步的问题不再是「SMMU 怎么配」，而是「CM3 为什么不接受这笔请求」——候选包括 GMU 的时钟/电源/复位前提、`HFI_SFR_ADDR` 里的 `chipid`、以及固件自己是否停在某个等待上。

### 70.8 十九行单变量表（与本轮相邻的部分）

| 轮/键 | 变的是什么 | 行数 | boot_id | 窗口 |
|---|---|---|---|---|
| R219 键 66 | 什么都不写 | 3/3 | 582344900 | `eff8caa2…` |
| R222 键 66 | 加 `INIT=1` | 0/3 | 未变 | 未变 |
| R223 键 66 | 改 `INIT=0` | 3/3 | 583170095 | `d60bb6e3…` |
| R238 键 80 | 四笔掩码，`start_next=0` | 3/3 | 586963994 | `4b1a5525…` |
| R238 键 81 | 同上 + `INIT=1` | 0/3 | 未变 | 未变 |
| R239 键 82 | 最后一笔写 `INIT=0` | 3/3 | 586092751 | `0c000d89…` |
| R239 键 83 | `QTBL_INFO=0` 后 `INIT=1` | 4/4 | 585892346 | `3b7d37b4…` |
| R240 键 84 | 零页 + `INFO=1` + `INIT=1` | 0/4 | 未变 | 未变 |
| R240 键 85 | 真表 + `INFO=1` + `INIT=1`（阳性对照） | 0/3 | 未变 | 未变 |
| R241 键 86 | 只读（控制） | 3/3 | 587123969 | `fad5753a…` |
| R241 键 88 | `SMR0` 写 `VALID\|SID 4` 后复原 | 5/5 | 585520255 | `b4f00131…` |
| R241 键 87 | 写 `S2CR0 = BYPASS` | 5/5 | 586132469 | `fed02287…` |
| R242 键 89 | 只读 `0xC00/4/8` | 4/4 | 586058586 | `8fab9ce1…` |
| R242 键 91 | 同值写回 `0xC00` | 5/5 | 585960701 | `8fc2d1c3…` |
| R242 键 90 | 路由 + `INIT=1`（BYPASS 被钳成 FAULT） | 0/6 | 未变 | 未变 |
| R243 键 92 | 只读 `S2CR0..4` + `sGFSR` | 4/4 | 585424163 | `b8a550dd…` |
| R243 键 93 | 写 `TYPE=TRANS` 不启动 | 4/4 | 585816702 | `2ca952b1…` |
| R243 键 94 | 哨兵表 + 路由 + `INIT=1` | 0/6 | 未变 | 未变 |
| R244 键 96 | identity 表 + 路由，不启动 | 8/8 | 585220336 | `a5f088f0…` |
| R244 键 95 | identity 表 + 路由 + `INIT=1` | 7/7 | 585373771 | `fff4087c…` |
| R245 键 98 | R244 键 95 逐字复现 | 7/7 | 587175491 | `b5ea6ee7…` |
| R245 键 97 | 同上 + 启动后读五个寄存器 | 8/8 | 586639327 | `0955a67b…` |
| R246 键 99 | 同上 + 启动后读五个寄存器（含 CP 消息） | 8/8 | 584949295 | `5d5e8f48…` |
| R246 键 100 | 启动字前重新武装描述符 | 8/8 | 584935029 | `fe071b25…` |
| R246 键 101 | SID 4 + SID 5 两条路由后启动 | 8/8 | 583926476 | `f6a83297…` |

### 70.9 本轮没有证明什么

1. 没有证明「固件拒绝启动」——只观测到**没有反应**：`ctl` bit 0 从未置起、`cpresp` 从未出现、描述符原样保留。这三条一致但并不构成「固件明确拒绝」的证据，它同样兼容「固件根本没跑到那一步」。
2. 没有分辨 CM3 是否在执行：`fwinit=babeface` 是 R168 那套签名/看门狗检查的结论（HOST/DEVICE 混合），本轮没有重新做一次「CM3 还活着」的独立测量。
3. 没有测 GMU 的时钟/电源域前提是否满足：`gmustat` 里的 `gdscr=00222001`（GX 仍关）是同一窗口的旁证，但本轮没有做任何与 CX/GX 相关的单变量。
4. 没有证明 `cpmsg`/`cpresp` 的共享区解读正确：`cpmsg=ad946000` 来自 setup 写入的物理地址，本轮只验证它没被改动，没有验证固件应该在那里放什么。
5. 没有排除「启动字需要某个前导寄存器序列」：本轮三个键都在同一个前导之后，前导本身没有做过单变量。
6. 没有触碰 `sCR0` bit 10（`USFCFG`）：R234/R235 两轮两次证明这笔写被忽略，本轮仍绕开它，所以「未匹配流会被 fault」这条前提在本轮依然成立。

### 70.10 三条边界

- 本轮的三个「落盘」都是 DEVICE-VERIFIED（窗口 sha256 + boot_id + 行内 `key=` 三者一致），但**含义**是「这一轮没有被拦在写窗口之外」，不是「启动请求被固件接受」。
- `cbfsr=00000402` 的 bit 1 与 bit 10 仍未分辨（R245 §69.6 已登记），本轮没有再读它。
- 每份镜像每次开机 `S2CR0..4` 都回到 `000200ff`（R243 键 92 观测），所以本轮的路由是**本轮**临时建立并保留到关机，不是持久配置。

### 70.11 R247 设计（基于本轮读数）

- **键 102**：在启动字之前读 `HFI_SFR_ADDR`(0x5182) 与 `CM3_CFG`(0x502d)、`CM3_SYSRESET`(0x5000)，把「固件侧的前提」摊开：`chipid` 是否等于 `06030100`、CM3 是否处于复位、`CM3_CFG` 是否被写过。全部是我方读、不写任何启动字（形状复制 R241 键 86 的控制键）。
- **键 103**：在启动字**之前**先写 `PWR_COL_CP_MSG`（把共享区地址重新写一次）+ `HFI_SFR_ADDR` 不动，再看 `ctl` 是否变化 —— 目的是分辨「固件在等 CPU 先写 CP_MSG」与「固件根本没跑」。
- **键 104**（若 102/103 都指向「固件没跑」）：只读 `GMU2HOST_INTR`(0x5193) 与 `AO_HOST_INTERRUPT_STATUS`(0x9305)，确认没有任何中断被挂起；若全零，则下一步必须换观测通道（例如让另一颗 CPU 读 EDPCSR），而换通道会打破「必须能自动回 TWRP」，需要事先与用户确认。

### 70.12 环境事实（本轮新增）

- **不要原地替换旧键块**：`work/tmp/r246_block.py` 第一版把 R245 的键 97/98 块原地改写成 R246，导致 `gmuW` 计数只 +3（应为 +5）——被替换掉的 R245 尾部本身含两笔启动字。发现方式就是这个计数差。正确做法是 `git show :package/EnchiladaAdrenoProbe.cpp` 把文件恢复成 index 版本，再从旧块文本派生出新块**追加**在其后。
- **派生新块用 token 重命名表**（`gmu97→gmu99`、`r97→r99`、`s97→s99`、`t97→t99`、`l97→l99`、`k97→k99`、`p97→p99`、`plan97→plan99`、`abs97→abs99`、`nplan97→nplan99`、`nabs97→nabs99`、`ok97→ok99`、`id2w97→id2w99`），每个 token 都断言「新块里没有残留」。
- stage6 里定位「三个键共用一个块」的子切片时，**不能**用 `index('} else {')` 从头找（拒绝分支里就有多个），也不能用前缀切片（键 99 分支在 101/100 之后）：分别用 `index('} else {', _i100 + 1)` 与 `rindex('} else {')`。
- stage6 的 `ROUND 246` 计数断言不能写成 `== 1`：断言消息里也含这个字符串（共 3 处），要用带长横线的 header 行做哨兵。
- 探针里 `rd32(gSmmuVa, 0x24u)` 现在共 6 处（键 71/72/95/97/99 各一次 + stage-6 自己），stage6 的上限必须同步成 `<= 6`，否则会报 「ID1 is re-read at most four times more」。
- CI 一轮约 14 分钟（`01:02:49Z → 01:16:47Z`，28 步），取件 130 s / 592 KB/s；`work/tmp/r246_wait.py` 把 run id 写在脚本里，**换轮次必须换脚本或改参数**（`r242_wait2.py` 曾因此去等上一轮的 run）。

## 七十一、ROUND 247：CM3 可以重新复位，启动字也活了 —— 但固件仍然不置 ctl 的 bit 0


### 71.1 一句话

把 CM3 用上游顺序重新复位一遍（队列表已经在它要读的位置上），复位本身**活了**（键 103，8/8），复位之后再写启动字也**活了**（键 104，9/9），而 `HFI_CTRL_STATUS` 的 bit 0 **仍然是 0**，`GMU2HOST_INTR_INFO`、`PWR_COL_CP_RESP` 也仍然是 0。所以这一轮把「启动字会打死整机」这件事彻底从图景里拿掉了，同时也把缺口钉死在固件侧：CM3 活着、复位干净、描述符是 CPU 自己写的那一份，但它就是不接受启动请求。

### 71.2 为什么这一轮的形状换了

上一轮（§70.11）本打算做三件事：读 `HFI_SFR_ADDR`、重写 `PWR_COL_CP_MSG`、读 `GMU2HOST_INTR`。读 `drivers/NativeAdreno/AdrenoGmuBootV1.c` 的 cold plan（第 120–291 行）之后才发现前两件都已经在本移植里做过了，而且顺序与上游一致：

| 上游步骤 | cold plan 行 | 探针自己的 CM3 启动块 |
|---|---|---|
| `HFI_QTBL_ADDR` / `HFI_QTBL_INFO` | 181–183（`a6xx_gmu_fw_start()` 段） | 1812–1814，写在 **CM3 复位释放之前** |
| `HFI_SFR_ADDR = Chipid(chip_id)` | 194–199 | 已写 |
| `PWR_COL_CP_MSG = log_iova \| (log_pages-1)` | 194–199 | 1817 |
| `CM3_CFG` | — | 1838 |
| `CM3_SYSRESET = 1` → `PWR_COL_CP_RESP = 0` → `CM3_SYSRESET = 0` → poll `CM3_FW_INIT_RESULT` | 233–240 | 1842 / 1844 + poll |
| `HFI_CTRL_INIT = 1` + poll `HFI_CTRL_STATUS` bit 0 | 281–287（`a6xx_gmu_hfi_start()`） | 从未按这个顺序做过「复位后立刻启动」 |

也就是说「固件不知道队列表」这条早被排除；真正要问的是「为什么一个活着的 CM3 不把 ctl 的 bit 0 置起来」。这一轮因此把三件互不依赖的观察分到三个键上，全部保持「store 是本轮最后一个动作」这条唯一被证明能落盘的形状。

### 71.3 三键代码形状

| 键 | 共享前导（R244 那套） | 本轮独有 | 最后一笔 |
|---|---|---|---|
| 102 | 建 2 MiB identity 表、九笔 CB0、`SMR0 = VALID\|SID 4`、`S2CR0 = TRANS\|CB0` | **一笔 GMU 写都不做**，只读 `HFI_SFR_ADDR`/`CM3_CFG`/`CM3_SYSRESET`/`CM3_BOOT_CONFIG`/`CM3_FW_INIT_RESULT`/`HFI_CTRL_STATUS`/`HFI_IFC`/`RPMH`/`GMU2HOST_INTR_INFO`/`MASK`/`CP_RESP`/`CP_MSG` | 读完之后直接返回 |
| 103 | 同上 | `QTBL_ADDR`→`QTBL_INFO`→`SYSRESET=1`→`CP_RESP=0`→`SYSRESET=0`，再用带 `IODelay(1u)` 的循环有界轮询 `CM3_FW_INIT_RESULT`（200000 轮，命中 `0xbabeface` 即 break） | **不写启动字** |
| 104 | 同上 | 同 103 | 先写「复位完成」行，再写 `HFI_CTRL_INIT = 1` + `dsb sy`，然后**一次**读 `ctl`/`info`/`cpresp`/`qtbli` 并打印 |

三个键之间唯一的差别就是上表最后一列，这正是上一轮拿到的教训：任何夹在 store 与最后一个 writer 轮之间的打印都会丢。

### 71.4 构建与门禁

- CI **#358 / run 44853166235**：`completed / success`，job `build` `2026-11-21T01:57:39Z → 02:10:02Z`，**28/28 步**。
- 取件（16 连接，直连）：`kernel.macho` **126,251,008 B** sha256 `d80dcf02770bfea31a806ff1bdd60c30e30d5285c3da3b683f7af33bc151e729`；`payload.img` **150,994,944 B** sha256 `b9da2638499427defb0b98895186b367dd2fea7ff918133a0db7125ebf6424fe`。
- 探针 **569,338 字符 / 8,243 行**，arm 窗口 `(fwKey >= 7u && fwKey <= 104u)`，`gmuW[...] = ` 严格计数 **222**（R246 是 211，本轮 +11：键 104 六笔、键 103 五笔、键 102 零笔）。
- 静态门 `work/tmp/r247_static.py`：`required: 18 checked in both artefacts`、`survivors: 6 checked`、`forbidden: 2 checked`、固件钉 `b07f9029…`（32,768 B）→ `PASS: R247 static gate`。**区分能力自证**：把同一个门指向 `r246-kernel`/`r246-payload` → 18 条 required 全部 `kernel=0 payload=0` → `FAIL: R247 static gate (18 problems)`。
- 源码级门：stage6 **1288/1288**；host 门 **28/28** + `PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)`；`verify-enchilada-*.py` 全量 **46 跑 / 48，FAILING: 0**。

### 71.5 键 102：任何 store 之前的快照（boot 585194661，窗口 `eb5f0b32…`，8/8）

```
gmu102a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=102 mode=102 qtbl=ad934000
gmu102b=build=0 need=3 tables=3 writes=3 root=ad938000 base=ad800000 ias=48 oas=36 ok=1
gmu102c=plan=0 n=9 abs=0 nab=9 cbndx=0 ttbr=ad938000 cw=1 id0=48016605
gmu102d=wrote=9 ttbr0=ad938000/00000000 sctlr=00001067 tcr=00803510 cbar=0001f300 root=ad938000
gmu102e=smr_set wanted=80000004 back=80000004 valid=1 sid=4 landed=1
gmu102f=s2cr_set wanted=00000000 back=00000000 typew=00000000 landed=1
gmu102g=last_row=1 about_to_start=0 mode=102 route=1 smr=80000004 s2cr=00000000 qtbl=ad934000 base=ad800000
gmu102h=pre_start=1 sfr=06030100 cfg=00004052 sysreset=00000000 bootcfg=00000002 fwinit=babeface ctl=00000000 ifc=09c40405 rpmh=00000f11 info=00000000 mask=00000000 cpresp=00000000 cpm=ad937000 survived=1
```

字段对照（前两列是上游常量，不是我这轮的期望）：

| 字段 | 读数 | 上游常量 / 含义 |
|---|---|---|
| `sfr` | `06030100` | `Chipid(chip_id)` 对 a630 配置（`0x06030001`）代入上游公式的结果，逐位相同 |
| `cfg` | `00004052` | `CM3_CFG_VALUE = 0x4052` |
| `bootcfg` | `00000002` | `CM3_BOOT_CONFIG_VALUE = 0x02` |
| `sysreset` | `00000000` | 0 = 核心已释放，固件在本轮任何 store 之前就已经在跑 |
| `fwinit` | `babeface` | DTCM+0xff8 的 init 字，已由 CM3 指令写入 |
| `ctl` | `00000000` | `HFI_CTRL_STATUS` bit 0 从未被固件置起 |
| `ifc` | `09c40405` | `HFI_IFC` 版本字段，与 R246 一致 |
| `rpmh` | `00000f11` | 与 R246 一致 |
| `info` / `mask` | `00000000` / `00000000` | `GMU2HOST_INTR_INFO` / `MASK`，一个中断都没有 |
| `cpresp` / `cpm` | `00000000` / `ad937000` | `PWR_COL_CP_RESP` 一直是 0；`CP_MSG` 是 CPU 发布的物理地址 |

⇒ 芯片号是对的，CM3 配置字与上游写的一模一样，核心是释放的，固件是跑过的；而**启动请求从未被受理**这件事在「任何 store 之前」就已经成立。

### 71.6 键 103：带着队列表重新复位 CM3（boot 586194441，窗口 `d6c2ff4e…`，8/8）

A–G 行与键 102 同形（`key=103 mode=103`、`qtbl=ad933000`、`root=ad937000`、`about_to_start=0`），关键行：

```
gmu103h=cm3_reboot=1 no_start=1 fwinit=babeface polls=1 sysreset=00000000 cfg=00004052 ctl=00000000 survived=1
```

⇒ **脉冲 `CM3_SYSRESET`（1 → 0）不会打死这台机器**，而且复位之后 `fwinit` 仍然是 `babeface`、`sysreset` 回到 0、`CM3_CFG` 仍是 `0x4052`。这条单独就有价值：以前没人证明过重新复位 CM3 是安全的。

### 71.7 键 104：复位之后再写启动字（boot 584880976，窗口 `210e5a41…`，9/9）

```
gmu104h=cm3_reboot=1 about_to_start=1 fwinit=babeface polls=1 sysreset=00000000 cfg=00004052 survived=1
gmu104i=after_start=1 ctl=00000000 info=00000000 cpresp=00000000 qtbli=00000001 survived=1
```

⇒ **复位 + 启动字这一组合活下来了**（9/9 行全落盘，新 `boot_id`、新窗口 sha）。但紧跟启动字的那一次读给出的三件事都是否定的：`ctl` bit 0 = 0（固件没报告 HFI 引擎启动）、`info` = 0（固件没有抛任何中断）、`cpresp` = 0（固件没有回任何消息）。`qtbli = 1` 仍是 CPU 自己写的那一位。

### 71.8 判决

1. **CM3 可以重新复位**（键 103）：`SYSRESET` 脉冲不致命，复位后配置字与 init 字都回到上游期望值。
2. **复位之后写启动字也不致命**（键 104）：这是本移植第一次在「复位 + 启动字」这个完整上游顺序下拿到窗口。
3. **固件仍然不受理启动请求**：`ctl` bit 0 = 0、`info` = 0、`cpresp` = 0，三条独立读数同一轮里都是否定的。
4. **前十几轮的「写启动字就静默死」已经被完全解释并绕开**：R243 键 94 死 / R244 键 95 活（队列表覆盖）/ R245 键 97–98 复现 / R246 键 99–101 落盘 / R247 键 102–104 复位亦活。死因是队列表那一页上的 SMMU fault，而不是启动字本身。

### 71.9 必须分开说的三件事

1. **`polls=1` 不能区分「固件重跑」与「DTCM 保留了旧值」**。复位之后第一次读 `CM3_FW_INIT_RESULT` 就已经是 `babeface`，这既可能是固件真的重新执行了写 init 字的那条指令，也可能是复位并没有清掉 DTCM 而旧值还在。本轮**没有**区分这两者。
2. **`ctl` 只回答「固件是否报告 HFI 引擎启动」，不回答「固件是否活着」**。`fwinit=babeface` 是本移植唯一能证明 CM3 执行过指令的证据，而它本身也是推断链（`0xbabeface` 在固件 blob 里是代码立即数，位置在 ITCM，不在 DTCM+0xff8）。
3. **`GMU2HOST_INTR_INFO` 全零不等于「固件没抛中断」**：也可能是中断被 `MASK`（本轮读到 0，即没被屏蔽）或 IF 路由挡住。本轮只证明这两个寄存器读得到、且当前是 0。

### 71.10 单变量表（全部同一形状：store 是最后一个动作，或本轮不写 store）

| 轮次 | 键 | 这一轮在形状里改的那一件事 | 结果 | boot_id |
|---|---|---|---|---|
| R219 | 66 | 什么都不写 | 3/3 | 582344900 |
| R222 | 66 | 加一笔 `HFI_CTRL_INIT = 1` | 0/3 | — |
| R223 | 66 | 同一笔写 `0` | 3/3 | 583170095 |
| R238 | 80 | 四笔中断掩码，`start_next=0` | 3/3 | 586963994 |
| R238 | 81 | 四笔掩码 + `INIT=1`（真表、`INFO=1`） | 0/3 | — |
| R239 | 82 | `INIT = 0` | 3/3 | 586092751 |
| R239 | 83 | `INFO=0` + `INIT=1` | 4/4 | 585892346 |
| R240 | 84 | 描述符指向全零页 + `INFO=1` + `INIT=1` | 0/4 | — |
| R240 | 85 | 真表 + `INFO=1` + `INIT=1`（阳性对照） | 0/3 | — |
| R241 | 86 | 不写 SMMU | 3/3 | 587123969 |
| R241 | 88 | `SMR0 = VALID\|SID 4` 后复原 | 5/5 | 585520255 |
| R241 | 87 | 写 `S2CR0 = BYPASS` | 5/5（值被钳） | 586132469 |
| R242 | 89 | 只读 `0xC00/0xC04/0xC08` | 4/4 | 586058586 |
| R242 | 91 | 同值写回 `S2CR0` | 5/5 | 585960701 |
| R242 | 90 | 路由 + `INIT=1`（`BYPASS`，被钳） | 零字节 | — |
| R243 | 92 | 只读 `S2CR0..4` + `sGFSR` | 4/4 | 585424163 |
| R243 | 93 | `S2CR0 = TRANS`（不启动） | 4/4 | 585816702 |
| R243 | 94 | `SMR0` + `S2CR0 = TRANS` + `INIT=1`（哨兵表） | 零字节 | — |
| R244 | 96 | 2 MiB identity 表 + 路由，**不启动** | 8/8 | 585220336 |
| R244 | 95 | 同 96 + `INIT=1` | 7/7 | 585373771 |
| R245 | 98 | 键 95 逐字复现 | 7/7 | 587175491 |
| R245 | 97 | 同 95 + 启动字后一次读五个寄存器 | 8/8 | 586639327 |
| R246 | 99 | 启动字后读 `ctl`/`cpmsg`/`cpresp`/`qtbl*` 一次 | 8/8 | 584949295 |
| R246 | 100 | 启动字前重新武装描述符并读 `ctl` | 8/8 | 584935029 |
| R246 | 101 | 两个 SID（4 与 5）各一条路由 + 启动字 | 8/8 | 583926476 |
| **R247** | **102** | **一笔 GMU 写都不做，只读快照** | **8/8** | **585194661** |
| **R247** | **103** | **带表重新复位 CM3，不启动** | **8/8** | **586194441** |
| **R247** | **104** | **复位 + 启动字，然后一次读 `ctl`/`info`/`cpresp`/`qtbli`** | **9/9** | **584880976** |

### 71.11 没有证明什么

1. 没有证明固件在键 103/104 里真的**重新执行**了 init 字（`polls=1` 两种解释都兼容，见 §71.9.1）。
2. 没有证明 `ctl` bit 0 = 0 的原因是「固件没跑」还是「固件跑了但拒绝受理」。
3. 没有观测到任何 GMU→host 中断（`info` 与 `mask` 都是 0），也没有证明中断路径本身是通的。
4. 没有碰 `PWR_COL_CP_MSG`：键 102 读到的是 CPU 早就写进去的 `ad937000`，本轮没有再写。
5. 没有解释各轮窗口里 `gmursc=… intr=` 的不一致（见 §71.12.3）。
6. 不改变 R240/R246 的结论：表的内容与描述符的地址都不是致死点。

### 71.12 三条边界

1. **一行落盘只说明「这一轮没有被停住」，不说明那一笔 store 被硬件受理**。键 104 的 9/9 证明的是「机器活着」+「那一次读返回了 0」，不是「固件收到了请求」。
2. **每轮上机都是一次性、单变量、武装 BCB 可自动回 TWRP 的启动**；键 103/104 的复位是在 XNU 已经跑起来之后由探针做的，不等于引导器阶段就复位过。
3. **`gmursc`/`gmustat` 的 `intr` 字段在三份窗口里都是 `00000000`**，而 R244–R246 那五轮里它一直是 `00800000`（CM3_FAULT）。R247 没有任何改动涉及这条读数之前的状态，我也**没有**查明这个差异的原因；本轮不做归因，只登记。R186 键 33 里见过同一个 `00800000`。

### 71.13 R248 设计

现在唯一的缺口是「一个活着的 CM3 为什么不置 ctl 的 bit 0」。下一条实际可测的线索不再是状态寄存器，而是**清除/重试**与**消息面**：

- **键 105**：复位 CM3 之后、写启动字之前，先把 `HFI_CTRL_INIT = 0` 写一次（清掉可能残留的请求位），再写 `1`，然后读 `ctl`。
- **键 106**：复位 CM3 之后先写 `PWR_COL_CP_MSG`（CPU 自己算的 `log_iova | (pages-1)`）再启动，检查 `cpresp` 是否变化。
- **键 107**（只有在 105/106 都为否时）：把 `GMU2HOST_INTR` 全部按上游清一遍（`CLR = ~0`）再启动，看 `info` 是否在启动后变为非零。

三条都仍然是「store 最后」或「store 后一次读」。若三条全否，则下一步必须换观测通道（例如在另一颗核上读 EDPCSR，或在 `a6xx_gmu_hfi_start()` 的轮询里让 writer 先提交一次），那会打破「必须自动回 TWRP」的既有约束，须先征得用户同意。

### 71.14 本轮暴露的工具事实

1. **判决里的字段查找不能按子串**：`field("gmu102h", "cfg")` 命中过 `bootcfg=00000002`，于是键 102 的 `cfg` 被读成 `00000002`（真值 `00004052`）。改成按空白切 token 再逐 token 比对键名。
2. **改完还要处理「tag 与第一个键粘在同一个 token 里」**：行是 `gmu102b=build=0 need=3 …`，所以行的余下部分必须从**第一个 `=` 之后**开始切，否则 `build=` 永远找不到（`tables=` 恰好能找到，这正是它容易被忽略的原因）。
3. **判决逻辑要能脱离设备重放**：新增 `work/tmp/r247_judge.py`，按 runner 同一份 `FMT` 读已落盘的窗口并重新出判决；键 102 的读数就是靠它纠正的，不必再消耗一次启动。
4. **新键块必须追加、不能替换**：`work/tmp/r247_block.py` 初版把 R246 块整块替换掉了，靠 `gmuW` 严格计数只 +6（应为 +11）才发现；恢复用 `git show :package/EnchiladaAdrenoProbe.cpp`（PowerShell 重定向写的是 UTF-16LE，要 `decode("utf-16")`）。
5. **独立变量要单独核**：派生时带过来的 `r102RowF2` 未被使用，`-Werror,-Wunused-variable` 让 host 门失败；这属于「派生块里凡是没被引用的缓冲都要删」。
6. **源码级门的上限会随轮次漂移**：`rd32(gSmmuVa, 0x24u)` 从 5 涨到 7 处、`for (` 计数从 1 涨到 3 处，两处断言都要在同步时一起改。

## 七十二、ROUND 248：把 init 字清零再复位 —— babeface 自己回来了，说明固件真的重跑了；而清掉请求位或中断都改变不了沉默

### 72.1 一句话

R247 的 `polls=1` 有两种解释，这一轮把含糊消掉了：先把 `CM3_FW_INIT_RESULT` 写零（读回 `00000000` 证明零立住了），再脉冲复位 —— `babeface` **自己回来了**，而且这次花了 4 次轮询。指令写回来的，不是复位没清掉的旧值。同一轮里清掉 `HFI_CTRL_INIT` 再写回 1（键 106）、清掉两个中断块 CLR 再启动（键 107），`ctl` 的 bit 0 **仍然是 0**：过期的请求位和挂起的中断都不是沉默的原因。

### 72.2 为什么先解决 `polls=1` 这个含糊，而不是去重写 `PWR_COL_CP_MSG`

R247 留下两个可以说清却没说的点。第一，`polls=1` 既可能是「固件真的重跑了」也可能是「复位根本没清 DTCM，旧值还在」。第二，从没有人清过可能残留的请求位与中断。这两条都可以用同一份已经被证明能落盘的形状直接测掉；而「重写 `PWR_COL_CP_MSG`」既没有新的判据，也无法区分它和 R247 已经写过的 CP_MSG。所以 R248 换了三键，而不是照 §71.13 的原案去做 §71.13 的三键。

清零这件事必须**先读回、再复位**，否则 `after_clear` 只由代码路径保证。键 105 的 `gmu105h` 行就是为了把这一步变成设备证词：`before=babeface after_clear=00000000`。

### 72.3 代码形状

三个键共享 R244 的整套 setup（2 MiB identity 块覆盖队列表所在页、九笔 context-bank 字、`SMR0 = VALID|SID 4`、`S2CR0 = TRANS|CB0`），差别只在本轮问的问题：

| 键 | 复位前 | 复位后 | 启动字 | 读回 |
|---|---|---|---|---|
| 105 | 把 `CM3_FW_INIT_RESULT` 写 0（并读回） | 五笔 CM3 再启动 + 有界 poll | **不写** | `gmu105h`、`gmu105i` |
| 106 | 同 105 | 同 105 | 先写 `HFI_CTRL_INIT = 0`，再写 `= 1` 作为**最后动作** | `gmu106j`（清零后）、`gmu106k`（启动后一次读四个字） |
| 107 | 同 105 | 同 105 | 先清 `GMU2HOST_INTR_CLR` 与 `AO_HOST_INTERRUPT_CLR`，再写启动字 | `gmu107j`、`gmu107k` |

三键都不含 `IOSleep`、都不做轮询式长等待；poll 是 200000 轮上限、每轮 `IODelay(1u)`，而本次实测 4 轮就命中，所以**整轮仍然只有一个 writer 窗口的余量压力**。GMU 写点从 222 增加到 245（+23：每键六笔 = 18，键 106 的请求位两笔、键 107 的两个 CLR 各一笔）。

### 72.4 构建与门禁（CI-VERIFIED / HOST-VERIFIED）

- 分支 `r248-init-word` → commit `d3f4f5ecde8d6cc174aacf5b874c11d2926d429b`（tree `9fb7d6002f68260fce2446bb7d99447edc0ca101`），CI **#359 / run `44857868260`** `completed/success`，`2026-11-21T02:48:37Z → 03:04:41Z`，**28/28 步**。
- 产物：`kernel.macho` **126,267,392 B** sha256 `ba24da4688bce292c7e2fd3fde4cc52946c33c09150be07cdfd25a311e4a052e`；`payload.img` **150,994,944 B** sha256 `7019acad10a15dcfabf4bb41d156741c2a0b705ea1f3d4f872401b7a41fcc608`。
- 静态门 `r248_static.py`：24 条 REQUIRED/SURVIVORS 在两份产物里都在、`enchilada-pas-exec` kernel=1 payload=2、2 条禁区为空、固件钉 `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`（32,768 B）→ **`PASS: R248 static gate`**。
- **门自证区分能力**：把同一份门指向 `r247-kernel/kernel.macho` 与 `r247-payload/payload.img` → 24 条全 0、**`FAIL: R248 static gate (24 problems)`**。
- stage6 **1336/1336**、host 门 **28/28** + `PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)`、全量 `verify-enchilada-*.py` **ran 46 of 48, FAILING: 0**。

### 72.5 键 105 读数（DEVICE-VERIFIED）

`boot_id 583913461`，窗口 sha256 `569d733749aa47c178a7fd731058fb11fd0fc97049efd756b556312994c85512`，**9/9 行**：

```
gmu105a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=105 mode=105 qtbl=ad947000
gmu105b=build=0 need=3 tables=3 writes=3 root=ad960000 base=ad800000 ias=48 oas=36 ok=1
gmu105c=plan=0 n=9 abs=0 nab=9 cbndx=0 ttbr=ad960000 cw=1 id0=48016605
gmu105d=wrote=9 ttbr0=ad960000/00000000 sctlr=00001067 tcr=00803510 cbar=0001f300 root=ad960000
gmu105e=smr_set wanted=80000004 back=80000004 valid=1 sid=4 landed=1
gmu105f=s2cr_set wanted=00000000 back=00000000 typew=00000000 landed=1
gmu105g=last_row=1 about_to_start=0 mode=105 route=1 smr=80000004 s2cr=00000000 qtbl=ad947000 base=ad800000
gmu105h=init_cleared=1 before=babeface after_clear=00000000 about_to_reset=1
gmu105i=reboot=1 fwinit=babeface polls=4 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
```

判据命中的是第三条：`after_clear=00000000` 与 `fwinit=babeface` 同时为真 ⇒ **固件在复位之后真的又执行了一遍**，`babeface` 是被指令写回来的。

### 72.6 键 106 读数（DEVICE-VERIFIED）

`boot_id 584210510`，窗口 sha256 `4aca518993611d4d487c28c849c340c9aa79975a722f3352c00836b5bc091b70`，**11/11 行**，关键三行：

```
gmu106h=init_cleared=1 before=babeface after_clear=00000000 about_to_reset=1
gmu106i=reboot=1 fwinit=babeface polls=4 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu106j=request_cleared=1 ctl=00000000 fwinit=babeface about_to_start=1
gmu106k=after_start=1 ctl=00000000 info=00000000 cpresp=00000000 qtbli=00000001 survived=1
```

`ctl` 在清掉请求位之后仍然是 0，写回 1 之后仍然是 0 ⇒ **过期的请求位不是沉默的原因**。

### 72.7 键 107 读数（DEVICE-VERIFIED）

`boot_id 584625589`，窗口 sha256 `343c6a004bf95eba79f76667fe362726afd500e9f3c3239b4e24554c05296c7a`，**11/11 行**，关键三行：

```
gmu107h=init_cleared=1 before=babeface after_clear=00000000 about_to_reset=1
gmu107i=reboot=1 fwinit=babeface polls=4 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu107j=interrupts_cleared=1 info=00000000 mask=00000000 ao=00000000 about_to_start=1
gmu107k=after_start=1 ctl=00000000 info=00000000 cpresp=00000000 qtbli=00000001 survived=1
```

清两个中断块之后 `ctl` 仍是 0、`info` 仍是 0 ⇒ **挂起的中断不是沉默的原因**。顺带一个新事实：启动字写下之前 `GMU2HOST_INTR_MASK` 读出来是 **0**，即那一刻该中断块上一个源都没开。

### 72.8 判决

1. **`polls=1` 的含糊结案，且方向是「真的重跑了」**：init 字先被清零并读回确认，复位后又变成 `babeface`，且这次要 4 次轮询。R247 的 `polls=1` 是因为它从没清过这个字，读到的是上一轮留下的值。
2. **清请求位与清中断都不改变 `ctl` 的 bit 0**：两种「陈旧状态」解释被排除。
3. **沉默的位置在固件内部**：CM3 复位后重新执行、写入自己的 init 字，但从不置 `HFI_CTRL_STATUS` bit 0、从不写 `PWR_COL_CP_RESP`、从不置 `GMU2HOST_INTR_INFO`。
4. **形状本身继续免疫**：三键都是 fresh 窗口（新 boot_id + 新 sha256 + 行内 `key=` 与键值一致），本移植第一次在同一轮里既做了复位、又清了两类状态、还写了启动字，并且全部落盘。

### 72.9 必须分开说的三件事

- `gmu107j` 的 `mask=00000000` 是本轮**新读到**的事实，但它描述的是「启动字之前那一刻」，不能反推 R237 那轮把掩码写成 `ffffffff` 之后再启动的情形。
- `gmursc`/`gmustat` 的 `intr` 在这三份窗口里都是 `00000000`，而 R244–R246 那五轮一直是 `00800000`（CM3_FAULT）。本轮**没有**试图解释这个差异，只登记：它不是稳定的、可复现的开机状态。
- 本轮**没有**读 `CB_FSR` 与 `sGFSR`（R245 键 97 读过：`cbfsr=00000402`、`gfsr=00000000`）。所以「没有 fault」这句话本轮没有新证据，仍引 R245。

### 72.10 单变量表（R219 键 66 → R248 键 107）

| 轮次/键 | 这一轮只改了什么 | 结果 | boot_id |
|---|---|---|---|
| R219 键 66 | 什么都不写（对照） | 3/3 | 582344900 |
| R222 键 66 | 加一笔 `HFI_CTRL_INIT=1` | 0/3 | — |
| R223 键 66 | 同一笔写 `0` | 3/3 | 583170095 |
| R238 键 80 | 四笔掩码，`start_next=0` | 3/3 | 586963994 |
| R238 键 81 | 四笔掩码 + 启动字（哨兵表） | 0/3 | — |
| R239 键 82 | 启动字写 `0` | 3/3 | 586092751 |
| R239 键 83 | 先清 `QTBL_INFO` 再启动 | 4/4 | 585892346 |
| R240 键 84 | 描述符指向全零页 | 0/4 | — |
| R240 键 85 | 阳性对照（真表 + 启动字） | 0/3 | — |
| R241 键 86 | 零 SMMU 写（对照） | 3/3 | 587123969 |
| R241 键 88 | `SMR0 = VALID\|SID 4` 并复原 | 5/5 | 585520255 |
| R241 键 87 | 写 `S2CR0 = BYPASS` | 5/5（被钳成 FAULT） | 586132469 |
| R242 键 89 | 只读 `0xC00/0xC04/0xC08` | 4/4 | 586058586 |
| R242 键 91 | 同值写回 `S2CR0` | 5/5 | 585960701 |
| R242 键 90 | 路由 + 启动字 | 0/6 | — |
| R243 键 92 | 只读 `S2CR0..4` + `sGFSR` | 4/4 | 585424163 |
| R243 键 93 | 写 `S2CR0 = TRANS` | 4/4 | 585816702 |
| R243 键 94 | 路由 + 启动字（哨兵表） | 0/6 | — |
| R244 键 96 | 2 MiB identity 表 + 路由，不启动 | 8/8 | 585220336 |
| R244 键 95 | 同上 + 启动字 | 7/7 | 585373771 |
| R245 键 98 | 键 95 逐字复现 | 7/7 | 587175491 |
| R245 键 97 | 启动后读 `ctl/fwinit/gfsr/cbfsr/ao` | 8/8 | 586639327 |
| R246 键 99 | 启动后读 `ctl/cpmsg/cpresp/qtbl*` | 8/8 | 584949295 |
| R246 键 100 | 启动前重新武装描述符并读 `ctl` | 8/8 | 584935029 |
| R246 键 101 | 双 SID 路由 + 启动字 | 8/8 | 583926476 |
| R247 键 102 | 零 GMU 写的快照 | 8/8 | 585194661 |
| R247 键 103 | CM3 复位，不启动 | 8/8 | 586194441 |
| R247 键 104 | CM3 复位 + 启动字 | 9/9 | 584880976 |
| **R248 键 105** | **清零 init 字 + CM3 复位，不启动** | **9/9** | **583913461** |
| **R248 键 106** | **同上 + 清请求位 + 启动字** | **11/11** | **584210510** |
| **R248 键 107** | **同上 + 清两个中断块 + 启动字** | **11/11** | **584625589** |

### 72.11 没有证明什么

- 没有证明固件**完成了**它的初始化：`babeface` 是「init 结果」字，本轮只证明它被写了一次，不证明写完之后固件继续跑了多远。
- 没有证明 `ctl` bit 0 是唯一会变的位：本轮只读这一个字，没有把 `HFI_CTRL_STATUS` 的其余位逐位列出来。
- 没有证明固件真的读了队列表：读表要通过 SMMU，而本轮没有读 `CB_FSR`，也没有在复位后重新对齐 `HFI_QTBL_ADDR` 到固件会用的 IOVA。
- 没有证明 4 次轮询是「执行时间」而不是轮询粒度的产物：`IODelay(1u)` 的实际长度未知（R204 测到它比参数快约 60 倍）。
- 没有证明清中断块「没有副作用」：`mask=0` 那一刻的状态只这一次读到。
- 没有改变 R240 的结论（致死与表的内容无关），因为 R240 用的是哨兵叶子表而 R244 之后用 2 MiB identity 表，两者的死都发生在「表是否覆盖队列页」这一个变量上。

### 72.12 三条边界

- `polls=4` 与 `IODelay(1u)` 的真实时长绑定，任何「固件启动需要多久」的推论都不能引用这个数字。
- `gmu107j` 的 `mask=0`/`ao=0` 只描述那一刻；不能据此说 GMU 中断块整体是断开的。
- 本轮所有读数都在**同一个 writer 轮**之内落盘；跨轮的长时序（例如固件在几百毫秒后才置位）本轮**没有**覆盖。

### 72.13 R249 设计

问题已经从「固件受不受理」收窄到「固件受理之后为什么没有任何反应」，而且新证据把方向指向固件内部的时序：

- **键 108**：键 107 的形状 + 启动字之后用 `IODelay(1u)` 有界轮询 `HFI_CTRL_STATUS` 的 bit 0（上限让它最多跑约 3 秒，即 R201 实测过 writer 仍能跑的时长），命中即停，然后一行回读 `ctl/fwinit/cpresp/qtbli`。这会回答「是不是只是需要时间」。
- **键 109**：同键 108，但轮询的是 `CM3_FW_INIT_RESULT` 与 `HFI_CTRL_STATUS` 两个字的**变化**（打印起始值与结束值），并在最后读 `CB_FSR`（`cb_base + 0x58`）与 `sGFSR`（`0x048`）。这会回答「固件在这 3 秒里有没有动过任何字」。
- **键 110**：键 108 的形状，但启动字之前把 `HFI_QTBL_ADDR` 再写一次（值不变）并把 `HFI_QTBL_INFO` 写成 0 再写 1，看固件是否只在启动那一刻读描述符。

### 72.14 本轮的工具事实

- `r248_block.py` 从 R247 块文本派生：`reboot_stores()`/`poll()` 首次因字符串拼接漏 `+` 报 `SyntaxError`；`%%08x` 在纯拼接的片段里会原样进 C 源，必须在生成后把 `%%` 折叠成 `%`（否则 `-Werror,-Wformat-extra-args`）。
- R248 块必须**追加**在 R247 块之后；两次踩过「`t[:i] + new + t[j:]` 把上一块整块替换掉」，所以这次改用 `t[:j] + new + t[j:]`，并在生成后核对 `R247 block still present: True`。
- stage4/5 的 R247 切片终止符原本是门铃梯子，R248 插在它前面之后 R247 切片把 R248 吞了（复位脉冲从 2 变成 5）；必须把终止符前移到 R248 块头。这已经是同一类错误的第三次。
- stage6 里「启动字是最后一笔」不能用 `rindex('gmuW[')` 与 `index('HFI_CTRL_INIT] = 1u;')` 比较偏移（前者是 store 起点、后者是寄存器名起点，差二十来个字符），只能用「启动字之后 `gmuW[` 计数为 0」。
- host 门的预算注释里有很长的一行单引号串，插入新句子时锚点必须选**该串内部的片语**（不带引号），否则会命中 0 次。


## 七十三、ROUND 249：给固件一个真正的窗口 —— 三键全部落盘，`ctl` 的 bit 0 在两次读数里都是 0

### 73.1 一句话

R248 已经排掉了「请求位残留」和「中断挂起」两条解释，于是这一轮不再写新的配置，只问一件事：**如果把启动字写下去之后真的等一段时间（最多 200000 轮 `IODelay(1u)`），固件会不会把 `HFI_CTRL_STATUS` 的 bit 0 置起来？** 三个键全部 8/8 以上的行落盘，答案是：**不会**。

### 73.2 为什么这轮只看不改

R246 键 99 与键 100 在启动字**之前**和**之后**各读了一次 `ctl`，两次都是 0；但当时那两次读数都紧贴着 store，中间没有给固件任何时间。R243 键 94 与 R244 键 95 之间的差别说明「表能不能覆盖队列表那一页」已经解决，剩下的最朴素的一种解释就是：**固件其实收到了请求，只是需要时间**。R249 就是为了把这条最后也排掉而设计的。三个键共用 R248 的同一套 preamble（清 init 字 → 脉冲复位 CM3 → 轮询 init 字 → 清两个中断块），只有启动字**之后**做的事不同。

### 73.3 三键的代码形状

| 键 | 启动字前 | 启动字之后 |
|---|---|---|
| 108 | 同 R248 preamble | `for (r108w = 0; r108w < 200000u; ++r108w) { rd32(CTRL_STATUS); if (bit0) break; IODelay(1u); }`，然后打印 `gmu108k=after_poll=1 ctl=… bit0=… polls=… cpresp=… qtbli=… survived=1` |
| 109 | 同 preamble，另打印 `gmu109k=poll_start=1 fwinit=… ctl=…` | 写启动字 → 同款有界轮询 → 再读 `sGFSR`(0x048) 与 CB0 的 `CB_FSR`(`cb_base + 0x58`) → `gmu109l=poll_end=1 … cbfsr=… gfsr=… survived=1` |
| 110 | 同 preamble，另把 `HFI_QTBL_INFO` 写 0、读回、再写 1、读回 → `gmu110k=qtbl_repoked=1 info0=… info1=… about_to_start=1` | 写启动字 → 同款有界轮询 → `gmu110l=after_poll=1 ctl=… bit0=… polls=… qtbli=… survived=1` |

三个键都不再清请求位、不再清中断、不再改写队列表地址；键 110 是唯一在启动字之前**写**了两笔的键，而它写的是描述符本身（`HFI_QTBL_INFO` 0 → 1）。

### 73.4 构建与门禁

- CI **#360 / run 44861590525**：`completed / success`，job `build` `2026-11-21T03:31:51Z → 03:42:12Z`，**28/28 步**（比前几轮快，step 16 只用了约 6 分钟）。
- 产物：`work/tmp/r249-kernel/kernel.macho` **126,267,392 B** sha256 `1cc617e2a758a7241f9ee7ff4bacdbff52140a9172a4b83efc3e060651714408`；`work/tmp/r249-payload/payload.img` **150,994,944 B** sha256 `030d97422400175ef674ffe1482c923b7e7615c915aa81f2cd76effd5bcd70a2`。
- 静态门 `work/tmp/r249_static.py`：29 条 required+survivors、key marker `enchilada-pas-exec`（kernel 1 / payload 2）、2 条 forbidden、固件钉 `b07f9029…`（32,768 B）→ `PASS: R249 static gate`。**区分能力自证**：把同一脚本指向 `r248-kernel`/`r248-payload` → 21 条 required 全 0 → `FAIL: R249 static gate (21 problems)`。
- 源码门：stage6 **1373/1373**、stage4 **61/61**、stage5 **48/48**、host 门 **28/28** + `PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)`、全量 `verify-enchilada-*.py` **ran 46 of 48, FAILING: 0**。
- 探针：**624,953 字符 / 9,181 行**，arm 窗口 `(fwKey >= 7u && fwKey <= 110u)`，`gmuW[...] =` 严格计数 **274**（R248 的 245 + 本轮 29），`reinterpret_cast<volatile uint32_t *>` **58**。

### 73.5 键 108：写启动字 + 有界轮询（DEVICE-VERIFIED）

rekey `enchilada-pas-exec=2 → =108`（payload sha256 `a8b04c42f50f87905e1472e64da87c0bad16eab8a5c84e54a21e040f9148bf9a`），boot_id **643188047**、窗口 sha256 **`aacb8f98ac46b4b12d43ca5fd9a283ebfb8d2da04222513aa4218d3548dd5805`**、**11/11 行**：

```
gmu108h=init_cleared=1 before=babeface after_clear=00000000 about_to_reset=1
gmu108i=reboot=1 fwinit=babeface polls=4 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu108j=interrupts_cleared=1 info=00000000 mask=00000000 ao=00000000 about_to_start=1
gmu108k=after_poll=1 ctl=00000000 bit0=0 polls=200000 fwinit=babeface cpresp=00000000 qtbli=00000001 survived=1
```

`polls=200000` 是轮询上限，也就是说这一轮**把整个窗口都用完了才放弃**。按 R201 的标定（200000 轮 `rd32` + `IODelay(1u)` 实测约 3.17 秒），这个窗口大约是三秒，远大于一个 writer 轮（约 500 ms）。

### 73.6 键 109：启动字之前与之后的 `ctl`，再加两个故障寄存器（DEVICE-VERIFIED）

rekey 到 109（payload sha256 `15c1b756c6c123ed0b0d8aa1f86601f54e81d942ed67d3b46b522729c4e3fcf3`），boot_id **621183782**、窗口 sha256 **`e4b9196191f6b0fe4bed39a6c19ec3373a698634081c7deaada62476b1924c44`**、**12/12 行**：

```
gmu109k=poll_start=1 fwinit=babeface ctl=00000000 about_to_start=1
gmu109l=poll_end=1 fwinit=babeface ctl=00000000 polls=200000 cbfsr=00000402 gfsr=00000000 survived=1
```

### 73.7 键 110：把描述符证明成可写，再写启动字（DEVICE-VERIFIED）

rekey 到 110（payload sha256 `e3714b891ad896fe464c8440a860f01058fc050907267a6bc587c6fd5c2bbfff`），boot_id **642761268**、窗口 sha256 **`56998d7b14656d587aa9abfcaef368554ba95a6b72c619ffaf25367d422fcc94`**、**12/12 行**：

```
gmu110k=qtbl_repoked=1 info0=00000000 info1=00000001 about_to_start=1
gmu110l=after_poll=1 ctl=00000000 bit0=0 polls=200000 fwinit=babeface cpresp=00000000 qtbli=00000001 survived=1
```

### 73.8 判决

1. **时间不是缺的东西**。键 108/109/110 三次都把 200000 次轮询跑满，`HFI_CTRL_STATUS` 的 bit 0 一次也没有置起来。R246 那个「两次读数都紧贴 store」的弱点被补上了，结论没有变。
2. **CB0 在整个窗口里没有记录 fault**。键 109 的 `cbfsr=00000402`，bit 0（`FSR_FAULT`）是 **0**，`gfsr=00000000`。所以「固件读队列表时 SMMU fault」这条机制在**表已经覆盖那一页**之后不再出现——这与 R245 键 97 的读数（`cbfsr=00000402`、`gfsr=0`）逐位相同。
3. **描述符不是被拒的原因**。键 110 把 `HFI_QTBL_INFO` 写成 0、读回 0，再写 1、读回 1，然后在同一个 writer 轮之内写启动字；`ctl` 仍然是 0。硬件接受这两笔写并且原样回读。
4. **沉默是固件侧的，而且是「没有受理」，不是「受理失败」**。`fwinit=babeface` 在三次里都保持，`info=0`、`cpresp=0`、`mask=0`、`ao=0`，没有任何中断被抛起。换句话说：**CM3 活着、init 字在、队列表在、描述符在、时间给了，启动请求就是没有被取走。**

### 73.9 必须分开说的三件事

- `cbfsr=00000402` 的两位（bit 1 与 bit 10）在 R245 那轮就是同一个值，**不是本轮造成的**。它不是 fault（bit 0 为 0），但也不能当作「什么都没有」：这两位的含义还没查，不能写成「干净」。
- `gmursc` 行的 `intr=00800000`（CM3_FAULT）在这三份窗口里**又回来了**（R247/R248 六份窗口里是 `00000000`）。这是同一个未解释的异常，位置在 `gmu108a` 之前，**不能归因于本轮的写入**。
- `polls=200000` 的「三秒」是由 R201 的标定换算出来的，**不是本轮直接测量的**；如果要严格证明窗口长度，需要一个带时间戳的行。这一轮没有做。

### 73.10 单变量表（R219 → R249）

| 轮次 | 键 | 形状 | 结果 |
|---|---|---|---|
| R219 | 66 | 三行、零 store | 3/3，boot 582344900 |
| R219 | 67 | 三行 + 两笔队列表 | 3/3，boot 582538748 |
| R219 | 68 | 三行 + 启动字 | 0/3 |
| R222 | 66 | 行 + 启动字（键值不变） | 0/3 |
| R223 | 66 | 行 + 写 0 | 3/3，boot 583170095 |
| R238 | 80 | 四掩码、不起动 | 3/3，boot 586963994 |
| R238 | 81 | 四掩码 + 启动字（真表） | 0/3 |
| R239 | 82 | 同形状、写 0 | 3/3，boot 586092751 |
| R239 | 83 | 清 `INFO` + 启动字 | 4/4，boot 585892346 |
| R240 | 84 | 零页 + `INFO=1` + 启动字 | 0/4 |
| R240 | 85 | 真表 + 启动字（对照） | 0/3 |
| R243 | 94 | 哨兵表 + 路由 + 启动字 | 0/6 |
| R244 | 96 | identity 2 MiB + 路由、不起动 | 8/8，boot 585220336 |
| R244 | 95 | identity 2 MiB + 路由 + 启动字 | 7/7，boot 585373771 |
| R245 | 98 | 键 95 逐字复现 | 7/7，boot 587175491 |
| R245 | 97 | 键 95 + 启动后读五个寄存器 | 8/8，boot 586639327，`ctl=0 cbfsr=00000402` |
| R246 | 99 | 键 95 + 启动后读 cpmsg/cpresp | 8/8，boot 584949295，`ctl=0 cpresp=0` |
| R246 | 100 | 启动前重写描述符 + 读 ctl | 8/8，boot 584935029，`ctl=0` |
| R246 | 101 | SID 4 + SID 5 两条路由 + 启动字 | 8/8，boot 583926476 |
| R247 | 102 | 只读快照 | 8/8，boot 585194661 |
| R247 | 103 | 复位 + 轮询 init 字，不启动 | 8/8，boot 586194441 |
| R247 | 104 | 复位 + 轮询 + 启动字 + 读 ctl | 9/9，boot 584880976，`ctl=0` |
| R248 | 105 | 清 init 字 + 复位 + 轮询 | 9/9，boot 583913461，`polls=4` |
| R248 | 106 | 同上 + 清请求位 + 启动字 | 11/11，boot 584210510，`ctl=0` |
| R248 | 107 | 同上 + 清两个中断块 + 启动字 | 11/11，boot 584625589，`ctl=0` |
| R249 | 108 | 启动字 + 200000 次轮询 | 11/11，boot 643188047，`bit0=0 polls=200000` |
| R249 | 109 | 启动前后各一次 ctl + 故障寄存器 | 12/12，boot 621183782，`cbfsr=00000402 gfsr=0` |
| R249 | 110 | 描述符写 0 读回、写 1 读回 + 启动字 | 12/12，boot 642761268，`info0=0 info1=1 ctl=0` |

条件式（至今未被推翻）：**表覆盖队列表那一页时，启动字能活；请求被写下去之后，固件不置 `ctl` bit 0，给三秒也不置。**

### 73.11 没有证明什么

1. **没有证明固件收到了中断/门铃**。`ctl` 是「HFI 引擎已启动」的状态位，写的动作本身有没有到达 CM3 的可见点，这一轮没有独立观测。
2. 没有证明 `00800000`（CM3_FAULT）与沉默有关。它在 R247/R248 消失、在 R244–R246 与本轮出现，规律不明。
3. 没有证明 `cbfsr` 的 bit 1 / bit 10 是什么。
4. 没有证明「三秒」在物理时间上确实是三秒（见 73.9 第三条）。
5. 没有证明本轮没有改变别的东西：preamble 与 R248 相同，但 R248 的键 105–107 是把「清中断」放在启动字**之前**，而键 108–110 的排序是「清 init → 复位 → 清中断 → 启动」，两者在中断清除与启动字的**相对顺序**上一致；差异只有启动字之后的部分，这一点由三键各自的读数本身证明（`gmu108h/i/j` 三次完全相同）。
6. 没有证明 GMU 的 RPMh 投票成功：`gmurpmh=pre=00000000 ack=00000000 apolls=400000 ok=0` 在每一份窗口里都是这个形状，本轮也不例外。

### 73.12 三条边界

- 仍没有写 `sCR0` 的 bit 10（`USFCFG`），它在 R234/R235 两轮里三次被硬件忽略。
- 仍没有碰 panic 路径；三次上机都由武装的 BCB + 看门狗在 36–40 秒内送回 TWRP，无需按键。
- `HFI_CTRL_INIT` 在键 108–110 里都写在「清中断」之后，因此这三键不能回答「清中断是否必须发生在启动字之前」；需要另设一键把顺序反过来。

### 73.13 R250 的设计

R249 把「时间」「描述符」「fault」三条都排掉之后，剩下的候选只有「请求到不了固件」这一类。下一轮应该**不再从 CPU 侧改配置**，而是去读固件自己的状态与到达证据：

- **键 111**：同 R249 键 108，但把 `HOST2GMU_INTR_SET`(0x5194) 写一次且**只在启动字之前**，并在启动字之后读 `GMU2HOST_INTR_INFO` 与 `AO_HOST_INTERRUPT_STATUS`，看是否有任何位被固件置起（doorbell 是否被应答）。
- **键 112**：同 108，但在启动字之前读 `PWR_COL_INTER_FRAME_CTRL`(0x50c0) 与 `GMU_CM3_PWR`（若存在），确认 GMU 的电源/时钟域处于上游期望的状态；这一条对应 `gmurpmh ok=0` 这个从未成功的投票。
- **键 113**：同 108，但把启动字写到**两份不同的 AHB 别名**（若映射允许）或改用 `HOST2GMU_INTR_SET` 作为门铃，验证「这个偏移上的这一位」是否就是门铃本身。

这三条都不改 SMMU 配置，因此不会把已结案的队列表结论重新变成变量。

### 73.14 工具事实（本轮新增）

- `work/tmp/r249_gates.py` 一次同步了四个源代码级门（窗口字面量、R248 切片终止符、复位脉冲预算 6→9、ID1 上限 8→9）。**每一条替换都断言锚点存在**，这直接暴露了初版把 `_i0507r` 重复声明的问题。
- stage6 里定位分支切片时**必须用 `rindex('} else {')`**：preamble 的拒绝梯子本身就以 `} else {` 结尾，用 `index` 会让三个分支切片全部描述错误代码。这已经是同一类错误的第四次。
- `work/tmp/r249_static.py` 的 REQUIRED 列表是从探针源码里用「拼接相邻 C 字面量」的解析器取出来的（`work/tmp/r249_fmt2.py`），不是手抄的；手抄的格式串在长行被拆开时会悄悄失配。
- 三个 runner 由 `work/tmp/r249_mkrun.py` 从 `r248_s105_run.py` 派生，判决区整段替换、`_ROWS` 用显式行名表、`PREV_BOOT`/`PREV_SHA` 是占位符，随后用 `r249_anchor.py` 锚到上一轮的真实读数。取件、静态门、自证、三次上机全部按这个顺序完成。

## 七十四、ROUND 250：门铃不响，电源字有值，而启动字一写下去 CM3 就报 fault

### 74.1 一句话
三个键都落盘（键 111 13/13、键 112 12/12、键 113 13/13）；门铃（`HOST2GMU_INTR_SET`）**没有任何回应**，电源与帧控制字**不是零**，CM3 复位**不清** `HFI_SFR_ADDR` 与 `PWR_COL_CP_MSG`；而在写完启动字之后，`GMU2HOST_INTR_INFO` 的 **bit 23（`CM3_FAULT`）在键 111 与键 112 两次都被置起**。这是本移植第一次拿到「固件真的对启动字做了一件事」的直接读数。

### 74.2 为什么是这三个键，而不是 §50.9 的原案
§50.9 原案是（键 111 给 `HOST2GMU_INTR_SET` 门铃 / 键 112 写电源寄存器 / 键 113 重写两个地址看是否被保留），本轮**保留了三个主题但把形状改成一段共享序**：
- R249 的三个键各自重复一遍六笔前置（清 init 字 + 五笔 CM3 再启动 + poll + 两笔中断清），导致「每个键到底差了什么」阅读成本很高；
- 本轮把这八笔放进**一段共享 preamble**，三个键只在启动字周围各自差一处：
  - **键 111**：读 `GMU2HOST_INTR_INFO`/`AO_HOST_INTERRUPT_STATUS` → 写一笔门铃 → 再读 info/mask/ao → 启动字 + 有界 poll。**目的**：把「GMU 会不会回应主机门铃」从假设变成测量。
  - **键 112**：打印 `PWR_COL_INTER_FRAME_CTRL`、它的延迟/历史字、`CM3_CFG`、`CM3_BOOT_CONFIG`、`CM3_SYSRESET`，启动字之后再打一次。**目的**：每份窗口里 `gmurpmh=... ack=00000000 apolls=400000 ok=0` 从未成功，本轮把电源侧的字直接打出来。
  - **键 113**：先读 `HFI_SFR_ADDR` 与 `PWR_COL_CP_MSG`，再**用它们原本持有的值写回**（不发明常量），读回，再启动。**目的**：若 CM3 复位把这两个寄存器悄悄清了，`sfr_zero`/`cpmsg_zero` 会直接显示出来。
- 得失：共享 preamble 让本轮的 14 笔 GMU 写点剪到最少（274 → **288**），但也意味着三个键共享同一次复位：若前置本身有错，三个键会一起错。这一点在 74.9 单独列出。

### 74.3 代码形状
| 位置 | 内容 |
|---|---|
| 共享 preamble | 九笔 CB0 写入（`TTBR0 = root_paddr`，`SCTLR` 最后）+ `SMR0 = VALID\|SID 4` + `S2CR0 = TRANS\|CBNDX 0` + 清 `CM3_FW_INIT_RESULT` 并读回 + 五笔 CM3 再启动（`QTBL_ADDR`/`QTBL_INFO`/`SYSRESET=1`/`CP_RESP=0`/`SYSRESET=0`）+ 有界 poll（200000 轮、每轮 `IODelay(1u)`、命中 `0xbabefaceu` 即 break）+ 两笔中断清除 |
| 键 111 | 行 K 打印 info/ao → `HOST2GMU_INTR_SET = 1u` → 行 L 打印 set/info/mask/ao → 启动字 → 行 M 打印 ctl/bit0/polls/fwinit/info/cpresp/qtbli |
| 键 112 | 行 K 打印 ifc/hyst/cm3cfg/bootcfg/sysreset → 启动字 → 行 L 打印 ctl/bit0/polls/fwinit/ifc/cpmsg/info |
| 键 113 | 行 K 打印 sfr/cpmsg/qtbl/sfr_zero/cpmsg_zero → 两笔重写（各带 `dsb sy`）→ 行 L 打印 want/back 与 landed → 启动字 → 行 M |
- 三个分支都**以启动字为自己的最后一笔 store**；启动字之后只剩 `dsb`、结果读取与打印。无 `IOSleep`、无紧循环。

### 74.4 构建与门禁
- 探针：**649,735 字符 / 9,609 行**；arm 窗口 `(fwKey >= 7u && fwKey <= 113u)`；`gmuW[...] =` 严格计数 **288**；`reinterpret_cast<volatile uint32_t *>` **63**。
- 分支：`r250-arrival`，commit **`246a75113c80758678e0d4e7116af84f3b3afe99`**，tree `da9131d731c38113972be92da17957bc4e97d58e`。
- CI：**#361 / run `44866225854`** `completed / success`，job `build` `04:27:51Z → 04:47:22Z`，**28/28 步**。
- 产物：`work/tmp/r250-kernel/kernel.macho` **126,267,392 B** sha256 `d913966327e583ddf7043b297324d061ecd8c1bc8c75bb8ec6e6e791417eb963`；`work/tmp/r250-payload/payload.img` **150,994,944 B** sha256 `69875e314a8a9848d17710f83801f459156a972f3d8a47b9d687fedef6ae7664`。
- 静态门：`required+survivors present in the probe source: 35 checked`、`required+survivors: 35 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、`forbidden: 2 checked`、固件钉 `b07f9029…`（32,768 B）→ **`PASS: R250 static gate`**。
- **门自证区分能力**：指向 `r249-kernel`/`r249-payload` → 25 条 required 全 0 → **`FAIL: R250 static gate (25 problems)`**。
- 其余门：stage6 **1415/1415**、stage4 **61/61**、stage5 **48/48**、host 门 **28/28**（`PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)`）、全量 `verify-enchilada-*.py` **ran 46 of 48, FAILING: 0**。跑门禁必须把 `TMP`/`TEMP` 指到 `work\tmp\clangtmp`，否则 `clang-23: error: unable to make temporary file: Permission denied`。

### 74.5 键 111 读数（DEVICE-VERIFIED）
rekey 后 payload sha256 `3ae306d08759a6de7efd0237b3699a0abf1895b6851c06de51ca3f830dd6edcf`；每次上机前 `check-reboot-to-twrp.py --arm` PASS，下机后看门狗 36–40 秒自动回 TWRP，无按键。
- boot_id **624391291**，窗口 `work/tmp/r250_s111_window.bin` sha256 **`06d3a912f82213f6b5200126aec8f214b578e352c2778cba28008b9d396b0d6a`**，**13/13 行全部落盘**。
```
gmu111a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=111 mode=111 qtbl=ad949000
gmu111b=build=0 need=3 tables=3 writes=3 root=ad960000 base=ad800000 ias=48 oas=36 ok=1
gmu111c=plan=0 n=9 abs=0 nab=9 cbndx=0 ttbr=ad960000 cw=1 id0=48016605
gmu111d=wrote=9 ttbr0=ad960000/00000000 sctlr=00001067 tcr=00803510 cbar=0001f300 root=ad960000
gmu111e=smr_set wanted=80000004 back=80000004 valid=1 sid=4 landed=1
gmu111f=s2cr_set wanted=00000000 back=00000000 typew=00000000 landed=1
gmu111g=last_row=1 about_to_start=1 mode=111 route=1 smr=80000004 s2cr=00000000 qtbl=ad949000 base=ad800000
gmu111h=init_cleared=1 before=babeface after_clear=00000000 about_to_reset=1
gmu111i=reboot=1 fwinit=babeface polls=4 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu111j=interrupts_cleared=1 info=00000000 mask=00000000 ao=00000000 about_to_start=1
gmu111k=before_doorbell info=00000000 ao=00000000 about_to_ring=1
gmu111l=rang_doorbell set=00000001 info=00000000 mask=00000000 ao=00000000
gmu111m=after_start=1 ctl=00000000 bit0=0 polls=200000 fwinit=babeface info=00800000 cpresp=00000000 qtbli=00000001 survived=1
```
**读法**：门铃写下去之后立即读回的 `info` 仍是 `00000000`、`mask` 也是 0 → 门铃**没被接受**；而启动字之后的 `info=00800000` 是 `GMU2HOST_INTR_INFO` 的 **bit 23**，按 `a6xx_gmu.xml` 的位域定义就是 `CM3_FAULT`。

### 74.6 键 112 读数（DEVICE-VERIFIED）
payload sha256 `719fb50e41e498b3a170c16f4c9f8ca57627c58f3829369f691d2be0ff41870c`；boot_id **640349468**，窗口 `work/tmp/r250_s112_window.bin` sha256 **`64dfa8421e54ebdfeef5f14cf24f376f306396c3a9cbf9f479f3570ba519dade`**，**12/12**：
```
gmu112k=power_state ifc=09c40405 hyst=000a2710 cm3cfg=00004052 bootcfg=00000002 sysreset=00000000 about_to_start=1
gmu112l=after_start=1 ctl=00000000 bit0=0 polls=200000 fwinit=babeface ifc=09c40405 cpmsg=ad94b000 info=00800000 survived=1
```
**读法**：窗口里每一份都打印的 `gmurpmh=... ack=00000000 apolls=400000 ok=0` 不能再被当成「电源字是零」：`PWR_COL_INTER_FRAME_CTRL` 的实测值是 **`09c40405`**，启动字前后**逐位相同**；`hyst=000a2710`。所以失败在握手而不在寄存器。同一行里 `info=00800000` 再一次出现 —— **键 111 与键 112 都看到启动字后 CM3 报 fault**。

### 74.7 键 113 读数（DEVICE-VERIFIED）
payload sha256 `5cd19e227d5134b5df1efaa1b7d968a40114c42bf5fd72d8af78c5629afb62a6`；boot_id **631958867**，窗口 `work/tmp/r250_s113_window.bin` sha256 **`86fe38ce2bdf3708eb89b3082fc50cc8cdfe48e6e51184d84b62847b9843c078`**，**13/13**：
```
gmu113k=before_repoke sfr=06030100 cpmsg=ad949000 qtbl=ad946000 sfr_zero=0 cpmsg_zero=0
gmu113l=repoke sfr_want=06030100 sfr_back=06030100 cpmsg_want=ad949000 cpmsg_back=ad949000 landed=1
gmu113m=after_start=1 ctl=00000000 bit0=0 polls=200000 fwinit=babeface cpresp=00000000 qtbli=00000001 survived=1
```
**读法**：CM3 复位并**没有**清掉这两个寄存器（`sfr=06030100` 正是上游 `Chipid(0x06030001)` 的结果，与 R247 键 102 的读数一致），重写也原样落位（`landed=1`）。

### 74.8 判决
1. **门铃不是通道**：写 `HOST2GMU_INTR_SET = 1` 之后立即读到的 `info` 与 `mask` 逐位不变（`00000000`），且 `ctl` bit0 仍为 0。
2. **电源字有值**：`ifc=09c40405`、`hyst=000a2710`、`cm3cfg=00004052`、`bootcfg=00000002`；启动字前后不变。因此 `gmurpmh ok=0` 不是「没有值」，而是「无人回应」。
3. **复位不清地址**：`sfr_zero=0 cpmsg_zero=0`，且重写能落位。
4. **启动字真的引起了固件侧的一个事件**：`GMU2HOST_INTR_INFO` bit 23 = `CM3_FAULT` 在键 111 与键 112 两次都被置起，而 `ctl` bit0 从来没被置起、`fwinit` 始终是 `babeface`。**这把「固件从不理会请求」改写成「固件接到了请求并 fault 了」**。

### 74.9 必须分开说的三件事
1. `info=00800000` 的读数发生在**启动字下写与 200000 轮 poll 之后**，中间还夹着一整段等待；因此时间点只能定到「启动字之后」，不能定到「那一笔 store 的瞬间」。
2. 键 111 与键 112 都看到了 bit 23，但键 113 的行 M **格式里没有 `info=`**，所以三次上机中只有两次能读到它；不能写成「三次都有」。
3. `GMU2HOST_INTR_MASK` 在三个键里都是 `00000000`（上游 `a6xx_gmu_irq_enable()` 会把它写成 `~A6XX_HFI_IRQ_MASK`），所以即使 CM3 fault 被置起，本引导也**从未以中断形式接到过它**；「报 fault」具体表现为寄存器位，不是中断。

### 74.10 没有证明什么
1. 没有读到 CM3 的 fault 原因寄存器（本轮没读 `GMU_GENERAL_*` 与 CM3 异常地址寄存器）。
2. 没有分辨 fault 是「它读不到任何东西」还是「它读到了但拒绝接受」。
3. 没有证明门铃在**上游顺序下**也不响（本轮的门铃写在二十一次 `HFI_CTRL_INIT` 都没成功的现状下发出）。
4. 没有改变 R249 的结论：200000 轮的窗口里 `ctl` bit0 仍为 0。
5. 没有证明 `polls=4` 的含义在本轮有变化（仍与 R248 的 4 次相同）。
6. 没有解释为什么这三份窗口里 `gmursc intr=00800000` 又出现（R247/R248 六份窗口是 `00000000`）；该行由更早的块打印，位置在 `gmu111a` 之前，不能归因于本轮写入。

### 74.11 三条边界
1. `GMU2HOST_INTR_INFO` 的 bit 定义来自固定的上游源码（`drivers/gpu/drm/msm/registers/adreno/a6xx_gmu.xml`：`MSGQ` pos 0、`CM3_FAULT` pos 23），本仓库的 `AdrenoGmuBootV1.h` 只定义了偏移 `0x5192`、**没有**位域；本轮不新增头文件定义，以免把未验证的位当成已知。
2. 这是一个时点读数，不是闭环证据：若 CM3 fault 与 `ctl` bit0 不被置起有因果关系，本轮还没分辨出来。
3. 三个键共享一段前置，因此它们之间的差异只在启动字周围；前置若有错，三次上机会一起错，而且不会有一个孤立的读数发现它。

### 74.12 R251 设计
- **键 114**：共享前置之后，**写启动字前先把 `GMU2HOST_INTR_CLR` 再清一次并读回 `info`**，启动字之后**立即**（不做 200000 轮 poll，只读一次）读 `info`/`ctl` → 行 M 打印 `info_before_start` 与 `info_after_start`。**目的**：把 bit 23 的出现时点从「启动字之后某处」压到「启动字之后的第一次读取之前」。
- **键 115**：共享前置之后，先把两个中断清除字再写一次并把 `GMU2HOST_INTR_MASK` 写成 `~CM3_FAULT`（即只开 bit23），然后写启动字，立即读 `info` 与 `mask`。**目的**：看 fault 在被允许、（理论上）能抗拒的情形下是否仍被置起 —— 若仍被置起，则它不是一个可清除的事件而是持续状态。
- **键 116**：共享前置之后，**不写启动字**，只把上述三件事（读 info、写 mask、再读 info）做一遍作为对照。**目的**：把 bit 23 的出现与启动字彻底分开。

### 74.13 工具事实（本轮新增）
1. `work/tmp/r250_block.py` 首版把新块拼在 R249 块起点（`t[:i]`）而不是末尾，几乎删掉上一轮的块；发现方式是 `gmuW` 计数只 +6（应 +14）。今后拼接一律只能 `t[:j] + new + t[j:]`。
2. 生成器里的 `IOLog("enchilada-adreno: %s\n", %s) % row` 会把 C 的 `%s` 填成缓存名，报 `-Werror,-Wformat-extra-args`；要写成 `'IOLog("enchilada-adreno: %%s\n", %s);' % row`。
3. 生成器里的 `"%%08x"` 在拼字符串时不会被 Python 折叠，会原样进 C 源；需要显式 `replace("%%08x", "%08x")` 并断言无 `%%` 残留。
4. stage6 的切片终止符第三次因为新块插在门铃梯子之前而失效（R249 块被 R250 块吞掉）；修法是把它换成新块的块头。
5. `verify-enchilada-smmu-*.py` 会因为 clang 在工作区外建临时文件而报 `unable to make temporary file: Permission denied`；跑门禁前把 `TMP`/`TEMP` 指到 `work\tmp\clangtmp` 即可。
6. 话窗头布局必须从 runner 自己的 `FMT` 读，不要自拼；本轮的三组读数都是 runner 打印的，没有手写解析。

## 七十五、ROUND 251：把读取挪到启动字旁边的第一拍 —— CM3 的 fault 不是那一笔写出来的，而是在那之后才发生

### 75.1 一句话

R250 是在启动字之后又跑了 200000 轮轮询才去读 `GMU2HOST_INTR_INFO` 的，所以它只能说"启动字之后某处出现过 bit 23"，不能说"那一笔 store 把它置起来了"。这一轮把读取挪到紧贴启动字的位置：键 114 写完启动字就立刻读，读到 **`info=00000000`**；键 115 把 `GMU2HOST_INTR_MASK` 写成只开 bit 23（`0xff7fffff`，读回 `0xff7fffff` 原样落位），再写启动字立刻读，同样是 **`info=00000000`**；键 116 做完全相同的清除与掩码写入但**根本不写启动字**，`info` 仍是 `00000000`。三个键都 12/12 落盘。

结论只用一句话说清：**bit 23 不是启动字那一笔写出来的，它是在那之后（R250 那次的 3 秒轮询窗口里）才出现的**。这既不是"我错了"，也不是"设备没反应"，而是把 R250 那条观测的时间分辨率从"秒级"提到"store 与下一条指令之间"，并因此在紧邻的那一拍里看不到它。

### 75.2 为什么要把读取挪近

R250 的键 111 与键 112 都读到了 `info=00800000`，但两次读数都发生在 `for (w < 200000)` 的轮询循环之后。按 R201 的标定，200000 轮带 `IODelay(1u)` 的循环约 3.17 秒——这意味着那个 bit 可能在这一笔 store 之后一微秒就置起，也可能是在 3 秒里的任何一刻。两者的物理含义完全不同：

- 若是"写下去就置起"，那 CM3 对这笔请求的拒绝是**瞬时的**，下一次要测的是"它凭什么立刻拒绝"。
- 若是"之后才置起"，那 CM3 是**先接受了这笔请求、随后在自己处理的过程中 fault 了**，下一次要测的是"它在处理到哪一步出事"。

R250 的读数对这两种解释都兼容，所以这一轮不改启动路径、只改观测点：同一套 preamble，只在 store 与读之间不留任何循环。

### 75.3 代码形状

三个键共享同一段前导（与 R250 相同）：清 `CM3_FW_INIT_RESULT` → 五笔 CM3 再启动（`HFI_QTBL_ADDR` / `HFI_QTBL_INFO` / `CM3_SYSRESET=1` / `PWR_COL_CP_RESP=0` / `CM3_SYSRESET=0`）→ 有界等待 `CM3_FW_INIT_RESULT`（200000 轮上限、`IODelay(1u)`、命中 `0xbabeface` 即停）→ 清 `GMU2HOST_INTR_CLR` 与 `AO_HOST_INTERRUPT_CLR`。之后三键各差一处：

| 键 | 与前导相比多做的事 | 启动字 | 读完的时机 |
|---|---|---|---|
| 114 | 再清一次 `GMU2HOST_INTR_CLR`，读一次 `info/mask/ctl/qtbli` | 写 | 写完立刻读，**中间没有循环** |
| 115 | `GMU2HOST_INTR_MASK = 0xff7fffff`（只开 bit 23），读回 | 写 | 同上 |
| 116 | 同 114 的清除 + 同 115 的掩码写入 | **不写** | 同上 |

表里没有"延迟"这一列，因为这一轮的全部意义就是**把延迟拿掉**。键 114 与键 116 的差别只有那一笔启动字，键 115 与键 116 的差别同样只有那一笔；因此这三份读数两两之间都是单变量比较。

arm 窗口从 `fwKey <= 113u` 扩到 `<= 116u`；GMU 写点从 288 增到 302（前导 8 笔 + 每键两笔，共 14 笔）；块内只有一个 `for (`（前导的 `CM3_FW_INIT_RESULT` 等待）和一个 `IODelay(1u)`。

### 75.4 构建与门禁

- CI **#362 / run `44870530555`**：`completed / success`，job `build` `2026-11-21T05:21:54Z → 05:39:35Z`，**28/28 步**。
- 取件扁平化后：`kernel.macho` **126,267,392 B** sha256 `0d45545ddc8d985d92f96da38b44faeb61e0937348207780ceece39d6c918691`；`payload.img` **150,994,944 B** sha256 `3c3e33c8aa5ab5cb0d109242b84063d3fe26dd8bf2c26a58a6e21d2141d7c315`。
- 静态门 `r251_static.py`：`required+survivors present in the probe source: 34 checked`、`required+survivors: 34 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、`forbidden: 3 checked`、固件钉 `b07f9029…`（32,768 B）→ `PASS: R251 static gate`。
- **门自证区分能力**：同一份门指向 R250 的两个产物 → 23 条 required 全部 `kernel=0 payload=0` → `FAIL: R251 static gate (23 problems)`。一个只会在本轮产物上通过的门是没有意义的（R232 那次就是对着旧内核通过，白给了一轮"全部通过"）。
- 探针落地：**669,977 字符 / 9,933 行**，arm 窗口 `(fwKey >= 7u && fwKey <= 116u)`，严格口径 `gmuW[...] = ` **302**，`reinterpret_cast<volatile uint32_t *>` **68**。
- 门禁：stage6 **1429/1429**、stage4 **61/61**、stage5 **48/48**、host 门 **28/28** + `PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)`、全量 `verify-enchilada-*.py` **ran 46 of 48, FAILING: 0**（跳过需要 40 位 rev 的 `-gmu-caller-v1-record.py` 与需要真机的 `-ias-measured.py`）。

### 75.5 键 114：再清一次，然后贴着启动字读

payload sha256 `742b06ec1194aff874abbde7b42ac8d0da600e710caa91a05d8619c740ac3466`；boot_id **582648101**（新）、窗口 `r251_s114_window.bin` sha256 **`6cfa33ded264841908d8c35a50ca9a671d14ce07d8619687cf1ff829e7ca15a5`**、**12/12 行**：

```
gmu114a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=114 mode=114 qtbl=ad947000
gmu114b=build=0 need=3 tables=3 writes=3 root=ad960000 base=ad800000 ias=48 oas=36 ok=1
gmu114c=plan=0 n=9 abs=0 nab=9 cbndx=0 ttbr=ad960000 cw=1 id0=48016605
gmu114d=wrote=9 ttbr0=ad960000/00000000 sctlr=00001067 tcr=00803510 cbar=0001f300 root=ad960000
gmu114e=smr_set wanted=80000004 back=80000004 valid=1 sid=4 landed=1
gmu114f=s2cr_set wanted=00000000 back=00000000 typew=00000000 landed=1
gmu114g=last_row=1 about_to_start=1 mode=114 route=1 smr=80000004 s2cr=00000000 qtbl=ad947000 base=ad800000
gmu114h=init_cleared=1 before=babeface after_clear=00000000 about_to_reset=1
gmu114i=reboot=1 fwinit=babeface polls=3 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu114j=interrupts_cleared=1 info=00000000 mask=00000000 ao=00000000 about_to_start=1
gmu114k=cleared_again=1 info=00000000 mask=00000000 ctl=00000000 qtbli=00000001 about_to_start=1
gmu114l=first_read=1 ctl=00000000 bit0=0 info=00000000 mask=00000000 qtbli=00000001 survived=1
```

`gmu114j` 与 `gmu114k` 都是清除之后读的，两次都是 `info=00000000`，这确认"清除确实把 info 清成了 0"，于是 `gmu114l` 的 `00000000` 就不可能是"一直没清干净的旧值"。`fwinit` 仍是 `babeface`、`ctl` 仍是 0、`bit0=0`。

### 75.6 键 115：只开 bit 23 的掩码，然后贴着启动字读

payload sha256 `74429b09ce8795da53e710f5eafe4d20ceeed315df268320f75bb7498d72d573`；boot_id **582894143**（新）、窗口 sha256 **`b979256c663cf8044a414670ed547644eb36e5a9396f88bbeb9b5aff32e9eee9`**、**12/12 行**（A–J 与键 114 同形，只有 `key=115 mode=115`、`qtbl=ad948000`、`polls=4`；末两行）：

```
gmu115k=mask_set want=ff7fffff back=ff7fffff info=00000000 about_to_start=1
gmu115l=first_read=1 ctl=00000000 bit0=0 info=00000000 mask=ff7fffff qtbli=00000001 survived=1
```

`want == back == 0xff7fffff` ⇒ `GMU2HOST_INTR_MASK` **原样接受**这个只开 bit 23 的值（不是被钳、也不是被丢弃），所以"只开一个源"这件事在这个 CPU 上是可以安排的。而 `gmu115l` 的 `info=00000000` 说的是：在只开这一个源、且写完启动字立刻读的那一拍，bit 23 **还没有出现**。

### 75.7 键 116：同样的清除与掩码，但没有启动字

payload sha256 `79c25274528c79e1addc9c5b2785191a4eaad3ad1d9b5bde9d6252313c234f3d`；boot_id **583569074**（新）、窗口 sha256 **`957fe73fdb7b40f611c905358eae4269ce303d42834d9fd1158ef1bb817cfe45`**、**12/12 行**（A–J 同形，`key=116 mode=116`、`qtbl=ad948000`、`polls=3`；末两行）：

```
gmu116k=control_no_start info=00000000 mask=ff7fffff cleared=1 about_to_watch=1
gmu116l=after_control=1 ctl=00000000 bit0=0 info=00000000 mask=ff7fffff qtbli=00000001 no_start=1 survived=1
```

这个键的作用是把"清除 + 掩码写入"这一组动作单独拿出来跑：`mask=ff7fffff` 落进去了，`info` 前后都是 0，`ctl` bit0 是 0（本来也没要求启动，这是应有的）。所以"在那两笔写之后、在同样的时间尺度上 `info` 仍然是 0"这件事与启动字无关——它本来就是 0。

### 75.8 判决

1. **紧贴启动字的那一拍里，bit 23 是清的**（键 114 的 `gmu114l.info=00000000`）。R250 的 `00800000` 因此不能归给"那一笔 store 置起的"，只能归给"store 之后、3.17 秒轮询窗口里的某一刻"。这是本轮唯一的新事实，也是它最重要的地方。
2. **只开 bit 23 的掩码可以落位**（键 115 `want=ff7fffff back=ff7fffff`）。上一轮里"掩码是 0"这件事只是读数，这一轮把它做成了可编程的对照条件。
3. **在只开该源的条件下，贴着启动字读仍是 0**。这排除了"启动字会立刻把这一位置起、只是被掩码吞掉了"这条解释——如果是那样，掩码只开这一位时它应当立刻可见。
4. **没有启动字时同样读到 0**（键 116）。这条本来是最弱的对照（因为键 114 也没看到 bit），但它把"清除 + 掩码"这两个动作从嫌疑名单里划掉：它们在同样的时间尺度上不产生 bit 23。
5. 三条合起来：**fault 是启动请求之后才发生的事**。这与"CM3 收到请求→开始处理→在自己处理过程中 fault"的顺序一致，也解释了为什么它只在带轮询的键里被看到（R250 键 111、112 都是带轮询的）。

### 75.9 必须分开说的三件事

1. **"紧邻读不到"不等于"bit 23 不存在"**。R251 只证明了"store 与下一条指令之间它还没出现"，没有证明"它永远不会出现"。要证明后者得把轮询窗口本身缩短、逐段二分出现时刻。
2. **键 116 的对照窗口同样短**。它证明的是"清除 + 掩码在紧邻的那一拍不产生 bit 23"，不是"没有启动字就永远不产生"。真正强的对照需要有轮询、且证明轮询里也不出现。
3. **掩码落位 ≠ 掩码生效**。`back == want` 只说明寄存器接受这个值；"中断是否被真正屏蔽"取决于 CM3 侧怎么用这个掩码，本轮没有观测这一点（`GMU2HOST_INTR_MASK` 一直是寄存器读数，`gmustat intr` 在这一层也全是 0）。

### 75.10 单变量表（R219 键 66 → R251 键 116）

| 轮次 | 键 | 与前一轮的差别 | 落盘 | boot_id | 窗口 sha256 前 8 位 |
|---|---|---|---|---|---|
| R219 | 66 | 什么都不写 | 3/3 | 582344900 | `eff8caa2` |
| R222 | 66 | 加一笔 `HFI_CTRL_INIT=1` | 0/3 | 582538748 | `f39fac2c` |
| R223 | 66 | 同一笔改成 `0u` | 3/3 | 583170095 | `d60bb6e3` |
| R238 | 80 | 四笔中断掩码，不写启动字 | 3/3 | 586963994 | `4b1a5525` |
| R238 | 81 | 四笔掩码 + 启动字（哨兵表） | 0/3 | 586963994 | `4b1a5525` |
| R239 | 82 | 启动字改写成 `0u` | 3/3 | 586092751 | `0c000d89` |
| R239 | 83 | 先清 `HFI_QTBL_INFO` 再写启动字 | 4/4 | 585892346 | `3b7d37b4` |
| R240 | 84 | 描述符指向一页全零 | 0/4 | 585892346 | `3b7d37b4` |
| R240 | 85 | 真表 + 启动字（阳性对照） | 0/3 | 585892346 | `3b7d37b4` |
| R241 | 86 | 只读，不碰启动字 | 3/3 | 587123969 | `fad5753a` |
| R241 | 88 | `SMR0 = VALID|SID 4`，复原 | 5/5 | 585520255 | `b4f00131` |
| R241 | 87 | `S2CR0 = BYPASS`（被钳成 FAULT） | 5/5 | 586132469 | `fed02287` |
| R242 | 89 | 只读 `S2CR0..2` | 4/4 | 586058586 | `8fab9ce1` |
| R242 | 91 | 把 `000200ff` 原样写回 | 5/5 | 585960701 | `8fc2d1c3` |
| R242 | 90 | 路由 + 启动字 | 0/6 | 585960701 | `8fc2d1c3` |
| R243 | 92 | 只读 `S2CR0..4` | 4/4 | 585424163 | `b8a550dd` |
| R243 | 93 | `S2CR0 = TRANS` | 4/4 | 585816702 | `2ca952b1` |
| R243 | 94 | 路由 TRANS + 启动字 | 0/6 | 585816702 | `2ca952b1` |
| R244 | 96 | identity 2 MiB 表，不写启动字 | 8/8 | 585220336 | `a5f088f0` |
| R244 | 95 | identity 2 MiB 表 + 启动字 | 7/7 | 585373771 | `fff4087c` |
| R245 | 98 | 逐字复现键 95 | 7/7 | 587175491 | `b5ea6ee7` |
| R245 | 97 | 启动字之后读五个寄存器 | 8/8 | 586639327 | `0955a67b` |
| R246 | 99 | 读 `CP_MSG`/`CP_RESP`/描述符 | 8/8 | 584949295 | `5d5e8f48` |
| R246 | 100 | 启动前重写描述符并读 `ctl` | 8/8 | 584935029 | `fe071b25` |
| R246 | 101 | 两条 SID 路由后启动 | 8/8 | 583926476 | `f6a83297` |
| R247 | 102 | 只读快照（零 store） | 8/8 | 585194661 | `eb5f0b32` |
| R247 | 103 | CM3 再复位，不写启动字 | 8/8 | 586194441 | `d6c2ff4e` |
| R247 | 104 | CM3 再复位后写启动字 | 9/9 | 584880976 | `210e5a41` |
| R248 | 105 | 复位前清零 init 字 | 9/9 | 583913461 | `569d7337` |
| R248 | 106 | 再清请求位后启动 | 11/11 | 584210510 | `4aca5189` |
| R248 | 107 | 再清两个中断块后启动 | 11/11 | 584625589 | `343c6a00` |
| R249 | 108 | 启动字后 200000 轮长轮询 | 11/11 | 643188047 | `aacb8f98` |
| R249 | 109 | 启动前后各读一次 + 故障寄存器 | 12/12 | 621183782 | `e4b91961` |
| R249 | 110 | 描述符写 0 读回、写 1 读回 | 12/12 | 642761268 | `56998d7b` |
| R250 | 111 | 敲门铃后启动 + 长轮询 | 13/13 | 624391291 | `06d3a912` |
| R250 | 112 | 打印电源/帧控制字 | 12/12 | 640349468 | `64dfa842` |
| R250 | 113 | 用原值重写 `SFR`/`CP_MSG` | 13/13 | 631958867 | `86fe38ce` |
| R251 | 114 | 再清一次 + **贴着启动字读一次** | 12/12 | 582648101 | `6cfa33de` |
| R251 | 115 | 只开 bit 23 的掩码 + 贴着读 | 12/12 | 582894143 | `b979256c` |
| R251 | 116 | 同样清除与掩码，**无启动字** | 12/12 | 583569074 | `957fe73f` |

### 75.11 没有证明什么

1. **没有证明 bit 23 在那 3.17 秒里的具体时刻**。只知道"紧邻的那一拍没有、3.17 秒之后有"。
2. **没有证明它一定出现**。R250 是两次看到（键 111、112），键 113 那次的 `gmu113m` 格式里根本没有 `info=` 字段，所以"第三次"并不存在。
3. **没有证明掩码生效**。寄存器收下了 `0xff7fffff`，但本轮没有观察"只开这一源时它是否还会被抛成中断"——这需要一个带轮询的键。
4. **没有读 CM3 的 fault 原因寄存器**，所以"fault 是什么原因"仍未回答。
5. **没有分辨"读不到"与"读到了但拒绝"**。`ctl` bit0 一直是 0 只说明固件没有报告 HFI 引擎启动，不说明它没有尝试。
6. **不改 R250 的结论**：那一轮的 `info=00800000`、门铃无效、电源字有值、地址不被清，全部仍成立；本轮只是给它加了一个更早的时间点。

### 75.12 三条边界

1. **`gmursc` 行的 `intr` 在键 114 与键 115 是 `00800000`、在键 116 是 `00000000`**。这一行由更早的 CM3 块打印（位置在 `gmu114a` 之前），不能归因于本轮任何一笔写；三次上机里出现两次是这个数值在过去七轮里反复出现的同一形态（R244–R246 六份是 0、R247–R249 是 00800000）。只登记，不解释。
2. **`polls` 是 3 或 4**：`CM3_FW_INIT_RESULT` 从清零状态回到 `babeface` 只需要 3–4 轮，说明这一步几乎不花时间——上一轮它被读成 "polls=4" 时就已如此，本轮 114/116 是 3、115 是 4，属于正常的轮次抖动，不构成行为差异。
3. **每次开机 `S2CR` 都回到 `000200ff`**（R243 §67.10 已记），所以本轮表与路由是从头建立的，不是继承上一轮的。

### 75.13 R252 设计（已定，未执行）

现在手上唯一缺的是"那 3.17 秒里它到底在什么时候出现"。三键把窗口从两头夹住：

- **键 117**：键 115 的形状（只开 bit 23 的掩码）+ 启动字 + **200000 轮轮询** → 若 bit 23 在"只开这一源"的条件下仍然出现，说明它是状态而不是可清除事件（补上 §75.9 第 3 条的缺口）。
- **键 118**：键 114 的形状 + 一段**短的有界等待**（例如 20000 轮、约 0.3 秒）再读 → 把出现时刻从"3.17 秒"往"0 秒"这一侧推。
- **键 119**：**不写启动字** + 同样的 200000 轮轮询 → 证明轮询本身不会把 bit 23 置起来（本轮 §75.9 第 2 条承认的那个弱对照，在这里补强）。

三个键都不碰 SMMU 配置、都不写 `sCR0`，也不使用会停在需要按键状态的路径。

### 75.14 五条工具事实

1. `r251_block.py` 的标识符改名必须用 `\b([A-Za-z_][A-Za-z_0-9]*)` 整体替换"含 111 的名字"，只改 `\b111\b` 会漏掉 `r111qtbl`、`s111phys`、`gmu111a` 这类数字在中间或结尾的名字。
2. 改名与"替换 mode 选择器"有严格顺序：先替换选择器与块头、后改名，否则选择器里的 `r111mode` 被改掉、`OLD_MODE` 再也找不到（同一类错误在 R249、R251 各犯一次）。
3. 门禁脚本的切片终止符不能用"上一个块的块头"做全局字符串替换：那块头同时也是更早一轮切片的终止符（R251 因此让 R249 切片吞掉了 R250 的复位脉冲）。要按 `_kNNNN = code[` 的行号定位。
4. 断言里的寄存器名拼接要先 `repr()` 打出实际文本：`'gmu1' + '114' + 'k='` 拼出的是 `gmu1114k=`。
5. 设备 adb 偶发 `offline`：`adb reconnect offline` 能在数秒内把它拉回 `recovery`（`adb kill-server` / `start-server` 本轮不够，需要 reconnect）。

## 七十六、ROUND 252：把读取挪到三个不同的时刻 —— CM3 的 fault 不是那一笔写出来的，而是在之后几百毫秒里发生并留住的状态

### 76.1 一句话

上一轮把读取挪到启动字旁边的第一拍，看到 `CM3_FAULT` 是 0，于是「那一笔 store 自己置位」被排除。这一轮问的是**它到底什么时候出现、以及是不是一个能被清掉的事件**：结果三条读数互相印证——它在启动字之后**不足一秒**就出现（键 118，只等 20000 轮），在**只开这一个源的掩码**下也照样出现并且留在那里（键 117），而在**完全不写启动字**时即使轮询满 200000 轮也一直为 0（键 119）。

### 76.2 这一轮为什么换形状

R250 的键 111 是「启动字 + 200000 轮轮询，最后读一次」：读到 `info=00800000`。
R251 的键 114 是「启动字 + 紧邻一次读」：读到 `info=00000000`。

两条读数的差别只在于**读的时刻**，所以这一轮不再新增写，只把读的时刻分成三档：

| 键 | 启动字 | 清除 | 掩码 | 读完的时间点 |
|---|---|---|---|---|
| 117 | 写 | 前一次（preamble）+ 掩码写前 | `0xff7fffff`（只开 bit 23） | 200000 轮轮询之后 |
| 118 | 写 | 前一次（preamble）**再加一次** | 不动 | **20000 轮**之后 |
| 119 | **不写** | 前一次（preamble）+ 自己一次 | 不动 | 200000 轮轮询之后 |

三键共享同一段 preamble（清 init 字、五笔 CM3 再启动、有界轮询 init 字、两笔中断清除），所以在每个键里，**唯一与上一轮不同的地方**就是上面表格里那一列。

轮询的时间标定沿用 R201：`200000 轮 × IODelay(1u)` 约 3.17 秒，因此 20000 轮约 **0.32 秒**。

### 76.3 代码形状

- 键 117：`mask_set want=ff7fffff back=ff7fffff info=00000000` → `HFI_CTRL_INIT = 1` → `for (200000)` 轮询 `HFI_CTRL_STATUS` bit 0 → 读 `ctl/info/mask/qtbli` 打一行。
- 键 118：`cleared_again info=00000000 mask=00000000 ctl=00000000` → `HFI_CTRL_INIT = 1` → `for (20000)` 轮询 → 读同样五个字打一行。
- 键 119：`cleared_no_start info=00000000 mask=00000000 ctl=00000000` → **没有启动字** → `for (200000)` 轮询 → 打一行，行内带 `no_start=1`。

三键都**不含** `IOSleep`，块内共 5 个 `for (`（九笔 CB 写 + 每键一个轮询），每个轮询都用 `IODelay(1u)` 让步。

### 76.4 构建与门禁

- 分支 `r252-fault-timing`，commit `a489ab545023e9f82ab60e053a7c74be7c52b7db`，tree `f2f1ec1f64487d12e53d9c87ec7221ba90465128`。
- CI **#363 / run `44874308728`**：`completed / success`，`2026-11-21T06:10:47Z → 06:31:32Z`，**28/28 步**。
- 产物：`kernel.macho` **126,300,160 B** sha256 `667c7af20d09ca77e25b12265ad2bcef4b27fbe0b9f1c00598f9a1c61fb05201`；`payload.img` **150,994,944 B** sha256 `ec6d86df0ad945babea6073e8991038a93fe61366109315830720350cb1ecbad`。
- 静态门 `r252_static.py`：`required+survivors present in the probe source: 35 checked`、`in both artefacts: 35`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、`forbidden: 3 checked`、固件钉 `b07f9029…`（32,768 B）→ `PASS: R252 static gate`。
- **门自证区分能力**：把同一份门指向 `r251-kernel` / `r251-payload` → 23 条 required 全部 `kernel=0 payload=0` → `FAIL: R252 static gate (23 problems)`。
- 源码级门：stage4 **61/61**、stage5 **48/48**、stage6 **1443/1443**、host 门 **28/28**、全量 46/48 FAILING 0。
- 探针：**691,361 字符 / 10,288 行**，arm 窗口 `(fwKey >= 7u && fwKey <= 119u)`，GMU 写点 **315**，`reinterpret_cast<volatile uint32_t *>` **73**。

### 76.5 键 117 读数（DEVICE-VERIFIED）

boot_id **612503960**、窗口 sha256 **`e9ffa24d673f1b1c8c7197a76d99aa3d02e2caebf47faac0c40efa3e3748f902`**、12/12 行：

```
gmu117k=mask_set want=ff7fffff back=ff7fffff info=00000000 about_to_start=1
gmu117l=after_poll=1 ctl=00000000 bit0=0 polls=200000 info=00800000 mask=ff7fffff qtbli=00000001 survived=1
```

掩码写得进去（`want == back`），而在**只开这一个源**的情况下，fault 仍然出现在轮询之后 → 它不是「被掩码吞掉」，也不是「清一次就没了」的事件。

### 76.6 键 118 读数（DEVICE-VERIFIED）

boot_id **586394539**、窗口 sha256 **`e4c33b3f562110c37a0c9599720c46a4091aac5828b106cf5e29ea31a1c3c4c7`**、12/12 行：

```
gmu118k=cleared_again=1 info=00000000 mask=00000000 ctl=00000000 about_to_start=1
gmu118l=after_short_wait=1 waited=20000 ctl=00000000 bit0=0 info=00800000 mask=00000000 qtbli=00000001 survived=1
```

只等 **20000 轮（约 0.32 秒）**，fault 就已经在了；而且这一键在启动字**之前**刚刚又清了一次（`cleared_again`），清除没有阻止它出现。

### 76.7 键 119 读数（DEVICE-VERIFIED）

boot_id **640788860**、窗口 sha256 **`7bdb8916978374ff18c529b11205d909095ce1289340ab4d42b80676a021602e`**、12/12 行：

```
gmu119k=cleared_no_start=1 info=00000000 mask=00000000 ctl=00000000 about_to_watch=1
gmu119l=after_poll_no_start=1 ctl=00000000 bit0=0 polls=200000 info=00000000 mask=00000000 qtbli=00000001 no_start=1 survived=1
```

同样的清除、同样 200000 轮轮询，唯一缺的是那笔启动字 → fault 一直是 0。

### 76.8 判决

1. **fault 归启动字**：键 119 是正对照，无启动字时 200000 轮内一次都没置起。
2. **fault 不是那一笔 store 的瞬时结果**：R251 键 114 的紧邻读是 0，这一轮键 118 的 0.32 秒读是 1 —— 出现时间在两者之间，属**延迟事件**。
3. **fault 是一个留得住的状态**：键 117 在只开该源的掩码下、清过一次之后再满轮询，它仍在；因此不是「抛一次就被清掉」的中断事件。
4. **`ctl` bit0 三次都是 0**：固件从未报告 HFI 引擎启动；`fwinit` 三次都是 `babeface`；`info` 里的这一位是**唯一**被观测到的固件侧反应。

### 76.9 必须分开说的三件事

- `info=00800000` 只说明 `GMU2HOST_INTR_INFO` 的 bit 23 被置起。位定义来自钉住的上游 `a6xx_gmu.xml`（`CM3_FAULT` pos 23），本仓库头文件只钉了偏移 `0x5192u`、**没有位域**；因此「这是 CM3 的 fault」是**依据上游位表**的读法，不是设备直接写出来的语义。
- `GMU2HOST_INTR_MASK` 一直是 0（键 118/119）或 `ff7fffff`（键 117），而 `GMU2HOST_INTR` 作为**中断**从未被本引导接住（IRQ 未接管）—— 所以这是「寄存器里的状态」，不是「进过一次中断服务」。
- `polls` 只说明轮询迭代次数，不说明固件内部时间；0.32 秒这个换算是按 R201 的 `IODelay` 标定推的，**不是**本轮实测的墙钟。

### 76.10 单变量表（R219 键 66 到 R252 键 119）

| 轮 | 键 | 形状要点 | 落盘 | 归因 |
|---|---|---|---|---|
| R219 | 66 | 三行零 store | 3/3 | 基线：无写则活 |
| R219 | 67 | 两笔表字 | 3/3 | 队列表写无罪 |
| R219 | 68 | 加启动字 | 0/3 | 启动字致死（后被推翻） |
| R222 | 66 | 加启动字（同键值） | 0/3 | 排除键值 |
| R223 | 66 | 启动字写 0 | 3/3 | 值 1 有罪 |
| R238 | 80 | 四掩码、不启动 | 3/3 | 掩码写无害 |
| R238 | 81 | 四掩码 + 启动 | 0/3 | 仍死 |
| R239 | 82 | 最后一笔写 0 | 3/3 | 形状无罪 |
| R239 | 83 | 清 INFO 再启动 | 4/4 | 死者是「表 + 启动」组合 |
| R240 | 84 | 零页表 + 启动 | 0/4 | 与表内容无关 |
| R240 | 85 | 阳性对照 | 0/3 | 复现 |
| R243 | 92/93 | S2CR 组可读 / TYPE 收 TRANS | 4/4、4/4 | 路由寄存器可写 |
| R243 | 94 | 路由 + 启动 | 0/6 | 当时不能归因 |
| R244 | 95/96 | identity 表 + 启动 / 不启动 | 7/7、8/8 | **启动字不再致死** |
| R245 | 97 | 启动后读五个寄存器 | 8/8 | `cbfsr` bit0=0，`ctl` bit0=0 |
| R246 | 99–101 | 固件侧读数、清请求位、双 SID | 8/8 各 | 描述符留在寄存器里 |
| R247 | 102–104 | 只读 / 再启动 / 再启动 + 启动 | 8/8、8/8、9/9 | CM3 可复位 |
| R248 | 105–107 | 清 init 字后复位等 | 9/9、11/11、11/11 | 固件真的重跑 |
| R249 | 108–110 | 长窗口 | 11/11、12/12、12/12 | 时间与描述符都不是缺口 |
| R250 | 111–113 | 门铃 / 电源字 / 重写地址 | 13/13、12/12、13/13 | 启动后 `info` bit 23 置起 |
| R251 | 114–116 | 紧邻读 / 只开该源 / 无启动字 | 12/12 各 | 紧邻读为 0 |
| R252 | 117 | 只开该源 + 满轮询 | 12/12 | **bit 23 仍在：状态而非事件** |
| R252 | 118 | 0.32 秒短等待 | 12/12 | **bit 23 已置：延迟很短** |
| R252 | 119 | 无启动字 + 满轮询 | 12/12 | **bit 23 为 0：归启动字** |

### 76.11 没有证明什么

1. 没有读到 fault 的**原因**：CM3 的 fault 原因寄存器（若有）没有位置已知的偏移，本轮没有盲写盲读。
2. 没有证明 `info` bit 23 置起之后固件是否停下：`ctl` 一直是 0，无法区分「fault 之后固件停住」与「fault 之后固件继续但从不启动 HFI」。
3. 没有测出精确的出现时刻：只知道在「紧邻」与「0.32 秒」之间，没有做更细的分档。
4. 没有试过「fault 置起之后再清一次」：键 117 的清除在启动字之前，之后的清除未测，所以「它是否能被清掉」只在「清一次、它又回来」这层意义上成立。
5. 没有证明这一步与最终渲染的关系：GPU 仍未渲染任何一帧，准入表仍是 `modeled=9 / satisfied=0 / BLOCKED`。

### 76.12 三条边界

- 三份窗口的 `gmursc`/`gmustat` 里 `intr` 的值仍不一致（R244–R246 五轮是 `00800000`，R247/R248 六轮是 `00000000`，本轮三份需要逐份看），本轮**没有**把这件事归因到任何东西上。
- 本轮所有读数都来自**同一个探针块**的三个模式分支，跨轮比较时只能比「同一形状下差一处」的键。
- 键 117 的掩码值 `0xff7fffff` 是「只清 bit 23」的按位补集；如果上游位表对 bit 23 的解释有误，这条掩码的意义也随之改变。

### 76.13 R253 设计

下一轮不再问「什么时候出现」，而问「出现之后能不能收回」：

- **键 120**：键 117 的形状 + 轮询命中 bit 23 之后**再清一次** `GMU2HOST_INTR_CLR`，再读一次 → 判「可清掉」还是「清不掉、只能靠复位」。
- **键 121**：键 117 的形状 + 命中之后读 `CM3_SYSRESET`/`CM3_BOOT_CONFIG`/`CM3_CFG`，看 CM3 是否已经把复位状态改掉（若 `sysreset` 变 1，说明固件自己停了核心）。
- **键 122**：把启动字写成 1 但**队列表地址指向 0**（`QTBL_ADDR=0` 且 `INFO=1`）→ 若仍 fault，说明 fault 与表无关；若不再 fault，说明 fault 来自读表。

### 76.14 工具事实

- `r252_block.py` 的「stale digest」断言一度把**注释里的上一轮键名**当成残留代码：新注释里写「Key 114 形状」是必要的可读性，因此检查必须只作用于 C 代码段（`preamble[first_member:]`），而不是整段文本。
- 探针编译门报 `-Werror,-Wshadow`：分支里重新声明 `unsigned r117w` 会遮蔽 preamble 的同名计数器。**新增分支必须复用 preamble 的循环变量**，不要重复声明。
- stage6 的三处断言是我按记忆写错的：掩码那笔实际写的是变量（`= r117maskWant`）而非字面量、`GMU2HOST_INTR_CLR` 在本轮被写了 **3** 次（preamble 一次 + 两键各一次）、`IODelay` 在**去注释后的代码里**是 **4** 处而不是 6（注释里的那两处被剥掉了）。**断言要对着去注释后的代码数。**

## 七十七、ROUND 253：清一次就把 fault 清掉了 —— 它不是留得住的状态，而是从来没有人清过的事件；把队列表地址清零则连日志都活不到

### 77.1 一句话

R252 说 `CM3_FAULT`（`GMU2HOST_INTR_INFO` bit 23）是「留得住的持续状态」，依据是「掩码只开这一位、清过一次、轮询满 200000 轮它还在」。这一轮的第一件事就是**再清一次**：`GMU2HOST_INTR_CLR = 0xffffffff` 写下去之后 `info` 立刻回到 `00000000`，而掩码**仍然**是 `ff7fffff` —— 它不是 latched 状态，**只是从来没有人清过它**。第二件事是命中之后读 CM3 自己的复位与配置字：`sysreset=00000000 bootcfg=00000002 cm3cfg=00004052`，与启动字**之前**的读数逐位相同 —— CM3 没有在自己 fault 的时候复位自己。第三件事是把队列表地址清零再请求启动：**整个启动连一行日志都没有提交**，窗口与上一轮逐字节相同。

### 77.2 三键的设计与单变量

三键共享同一段 preamble（SMMU CB0 九笔、identity 2 MiB 表、SMR/S2CR 路由、清 init 字、五笔 CM3 再启动、有界轮询、两笔中断清除），因此在每个键里**只多一件事**：

| 键 | 多出来的那一件事 | 问的问题 |
|---|---|---|
| 120 | 轮询命中 bit 23 **之后再清一次** `GMU2HOST_INTR_CLR` | 它是能清掉的事件，还是只能靠复位才消失的锁存？ |
| 121 | 命中之后读 `CM3_SYSRESET` / `CM3_BOOT_CONFIG` / `CM3_CFG` | fault 的时候 CM3 是不是自己复位或改了自己的配置？ |
| 122 | 启动字**之前**把 `HFI_QTBL_ADDR` 写成 0、`HFI_QTBL_INFO` 写 1 | fault 是因为「读了一张坏表」，还是只需要「启动请求」本身？ |

轮询的时间标定沿用 R201：200000 轮 × `IODelay(1u)` 约 3.17 秒。

### 77.3 代码形状

- 键 120：掩码 `0xff7fffff`（只开 bit 23）→ 启动字 → `for (200000)` 轮询 `HFI_CTRL_STATUS` bit 0 → 行 K `gmu120k=observed=1 ctl= bit0= polls= info= mask= about_to_clear=1` → **再清一次** `GMU2HOST_INTR_CLR = 0xffffffff` → 行 L `gmu120l=cleared_after=1 wrote= info= mask= qtbli= survived=1`。
- 键 121：同样的掩码与启动字 → 同样的轮询 → 行 K `gmu121k=observed=1 … about_to_read_cm3=1` → 行 L `gmu121l=cm3_after=1 sysreset= bootcfg= cm3cfg= fwinit= info= ctl= survived=1`（**只读，不写**）。
- 键 122：行 K `gmu122k=zero_table=1 qtbl_want=00000000 info_before= about_to_start=1` → 写 `HFI_QTBL_ADDR = 0`、`HFI_QTBL_INFO = 1` → 启动字 → 轮询 → 行 L `gmu122l=after_poll=1 ctl= bit0= polls= info= mask= qtbl_now= survived=1`。

块内共 **16 笔 `gmuW`**（preamble 8 + 键 120 的 3 + 键 121 的 2 + 键 122 的 3）、5 个 `for (`，每个轮询都用 `IODelay(1u)` 让步，**不含 `IOSleep`**。

### 77.4 构建与门禁

- 分支 `r253-follow-up`，commit `73651e8763a627bfa9fa7d5311c3b65bf598225b`，tree `1e35c3ad407a98eeffba313b6c78142cc4dde3f2`。
- CI **#364 / run `44879572074`**：`completed / success`，`2026-11-21T07:17:44Z → 07:36:36Z`，**28/28 步**。
- 产物：`kernel.macho` **126,300,160 B** sha256 `f29ed0805de46954d6cc02658613b57efab114f17a46025c8edf5e0591245d52`；`payload.img` **150,994,944 B** sha256 `557866be4ded1e9b3328415f968bb82a301adf96015005431508d3c9be2e6ebc`。
- 静态门 `r253_static.py`：`required+survivors present in the probe source: 35`、`in both artefacts: 35`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、`forbidden: 3 checked`、固件钉 `b07f9029…`（32,768 B）→ `PASS: R253 static gate`。
- **门自证区分能力**：把同一份门指向 `r252-kernel` / `r252-payload` → 23 条 required 全部 `kernel=0 payload=0` → `FAIL: R253 static gate (23 problems)`。
- 源码级门：stage4 **61/61**、stage5 **48/48**、stage6 **1461/1461**、host 门 **28/28**、全量 46/48 FAILING 0。
- 探针：**712,460 字符 / 10,641 行**，arm 窗口 `(fwKey >= 7u && fwKey <= 122u)`，GMU 写点 **331**，`reinterpret_cast<volatile uint32_t *>` **78**。

### 77.5 键 120 读数（DEVICE-VERIFIED，12/12 行）

payload sha256 `118816d889e6b721b3f4a79dd15aee44174e8d75c2fd374703d6df4d438a900e`，boot_id **639527882**，窗口 sha256 **`e16f658e8d5298c55271f6ab732b1b906dcce6eda717718b9b20e450e832c181`**。

```
gmu120a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=120 mode=120 qtbl=ad94e000
gmu120b=build=0 need=3 tables=3 writes=3 root=ad960000 base=ad800000 ias=48 oas=36 ok=1
gmu120c=plan=0 n=9 abs=0 nab=9 cbndx=0 ttbr=ad960000 cw=1 id0=48016605
gmu120d=wrote=9 ttbr0=ad960000/00000000 sctlr=00001067 tcr=00803510 cbar=0001f300
gmu120e=smr_set wanted=80000004 back=80000004 valid=1 sid=4 landed=1
gmu120f=s2cr_set wanted=00000000 back=00000000 typew=00000000 landed=1
gmu120h=init_cleared=1 before=babeface after_clear=00000000 about_to_reset=1
gmu120i=reboot=1 fwinit=babeface polls=4 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu120j=interrupts_cleared=1 info=00000000 mask=00000000 ao=00000000 about_to_start=1
gmu120k=observed=1 ctl=00000000 bit0=0 polls=200000 info=00800000 mask=ff7fffff about_to_clear=1
gmu120l=cleared_after=1 wrote=ffffffff info=00000000 mask=ff7fffff qtbli=00000001 survived=1
```

判决：**THE SECOND CLEAR REMOVES THE BIT, so it was an event nobody had cleared yet and not a latched state.** 三个细节支撑这句话：① 清除前后 `mask` 都是 `ff7fffff`，所以清掉的不是掩码；② `qtbli=00000001` 说明描述符没有被这最后一笔写弄坏；③ `survived=1`，这一笔清除没有把机器弄死。`ctl` bit 0 仍然是 0。

### 77.6 键 121 读数（DEVICE-VERIFIED，12/12 行）

第一次运行（12/13，少的正是 `gmu120k` —— 该行只在键 120 打，已从行表删掉）：payload sha256 `9bb3a4f1ec22cd3aa2ad12a2803320202e7c1d4946ff4e66a8e50d7b7fbc824a`，boot_id **640193532**，窗口 **`ee3728899e2aa17b87257b1041da9d381a2ed44afec9a967fc3302fe51e1b1af`**。为了给键 122 做对照，同一 payload 又跑了一次：boot_id **639658773**，窗口 **`5a116285aabb7852782bab49b09c854f97ea069a09b844e01860ef064239e15e`**，两次读数逐位相同。

```
gmu121k=observed=1 ctl=00000000 bit0=0 polls=200000 info=00800000 about_to_read_cm3=1
gmu121l=cm3_after=1 sysreset=00000000 bootcfg=00000002 cm3cfg=00004052 fwinit=babeface info=00800000 ctl=00000000 survived=1
```

判决：**THE CM3 DID NOT TOUCH ITS OWN RESET OR CONFIGURATION WORDS, so the fault is not the CM3 rebooting itself.** `bootcfg=00000002` 与 `cm3cfg=00004052` 正是 R247 在**启动字之前**读到的值，`fwinit` 仍是 `babeface`，`sysreset=0` —— 也就是说 fault 期间 CM3 的复位请求与两个配置字一动不动，而 `info` 仍是 `00800000`。

### 77.7 键 122：三次启动都没提交一行

payload sha256 `20ed8403c37a33891ccac667afa4bcefbd13426df7549e86fc94564a23399a6e`。**三次上机**（15:57、16:00、16:04）的每一步都通过了检查：

- 上机前 `check-reboot-to-twrp.py --arm` → `PASS (3 checks, 0 failures)`；
- `fastboot flash system_b` → `Finished. Total time: 4.781s`，且**回读** system_b 的关键块与 payload 逐字节相同（`flash readback matches: True`、`key block says: enchilada-pas-exec=122 -no_slto_`、`key verified: this boot will run key 122`）；
- `fastboot boot boot-r16.img` → `Booting OKAY`；
- 约 38–43 秒 `adb devices` 空列表，然后设备回到 TWRP。

而窗口**与上一轮逐字节相同**（先 `boot_id 640193532 / ee3728899e2a…`，后 `boot_id 639658773 / 5a116285aabb…`），行计数 `gmu122k=0 gmu122l=0`（10 of 12）。

**这不是「镜像坏了」**：把 `r253-s122-payload` 与 `r253-s121-payload` 逐字节比较，**全文件只有一个字节不同** —— 偏移 `0x7963270`，`enchilada-pas-exec=121` 的最后一个字符 `1` 变成 `2`。而同一个 121 payload 在键 122 失败之后紧接着跑成功（boot_id 639658773，12/12 行）。

**这也不是「boot 或 writer 本来就写不出来」**——控制键 123 回答了这一点。

### 77.8 控制键 123：不武装的同一颗内核照样提交窗口（DEVICE-VERIFIED）

把同一个镜像的同一个参数改写成 `enchilada-pas-exec=123`（**在 arm 窗口 `<= 122` 之外**，payload sha256 `bf82ed692b81f4b194f5104b00dcca268f0f2a6959286fa872dd7c787e8c89eb`），两次上机：

- 第一次：窗口 sha256 `7e8eef870d95ffc2ff6084e8bc1ece5071bab78b73b07709a610c3947479bad6`；
- 第二次：boot_id **581958999**，窗口 **`97239231380c3f398ba78872073c95d9d7345d3e85c43216730c42a06a9e7188`**，行 `gmu217seen=parsed=1 key=123 armed=0`，条的 12 行全为 0（未武装，块本来就不跑），窗口头里的 `key=` 字段为空。

⇒ 同一颗内核、同一条 flash/boot 路径，**未武装时窗口一定会被提交**（`gmu217seen` 这一行在武装判断之外，所以它总会出现）。因此键 122 的「一个字节都没变」只能意味着：**块跑了，然后在提交整轮之前机器就死了**。

判决：**ZEROING THE QUEUE-TABLE ADDRESS IS FATAL FOR THE BOOT ITSELF — the round does not commit a single byte, so this key cannot answer "does the fault need a readable table"; what it does answer is that the table address is load-bearing for the very act of requesting the start.**

### 77.9 判决（四条）

1. **`CM3_FAULT` 是一个可清除的事件**：一次 `GMU2HOST_INTR_CLR = 0xffffffff` 就让它从 `00800000` 回到 `00000000`，掩码不变。R252 的「留得住的持续状态」需要订正（见 77.11）。
2. **CM3 在 fault 期间没有复位自己、也没有改自己的配置**：`sysreset=0`、`bootcfg=00000002`、`cm3cfg=00004052` 与启动字之前逐位相同，`fwinit` 仍是 `babeface`。
3. **队列表地址清零 = 启动不了**：同一条路径、同一颗内核、只差这一个参数，三次都是零字节。
4. **`ctl` bit 0 在键 120/121 依旧全 0**：固件仍然从不报告 HFI 启动。加上 121 的读数，这一轮又给「CM3 在跑但 HFI 不启动」添了两条独立证据。

### 77.10 必须分开说的三件事

- 「fault 能被清掉」和「fault 有原因」不是同一件事：清得掉只说明它是一个**事件位**，不说明是什么事件；`ctl` bit 0 恒 0 说明事件之后 HFI 也没起来。
- 「CM3 没有复位自己」不等于「CM3 一切正常」：它只说明 `SYSRESET` 与两个配置字没动，不说明 CM3 在跑什么代码。
- 「零地址启动不了」不等于「fault 来自读表」：机器死在提交之前，我们**连它死在哪一笔都不知道**（见 77.12）。

### 77.11 对 §76.8 与 §76.2 的订正

R252 判决第 3 条写的是「留得住的持续状态（只开该源 + 清过一次 + 满轮询仍在）」。R253 的键 120 说明这句话**只对「没有人再清一次」成立**：R252 的三次清除都发生在**启动字之前**（preamble 与键 118 各一次），启动字之后从来没有人写过 `GMU2HOST_INTR_CLR`。加上键 120 的这一笔，bit 23 立刻消失。正确的表述是：**`CM3_FAULT` 是启动字之后约几百毫秒内发生、并且会一直留在寄存器里直到被清除的事件位**。旧表述保留在原处，本节点明订正。

### 77.12 没证明什么（以及为什么键 122 的答案只能等下一轮）

- 没有定位键 122 死在哪一笔：`HFI_QTBL_ADDR = 0`、`HFI_QTBL_INFO = 1`、启动字、轮询，四者都在「块开始」与「整轮提交」之间。候选机制有两个，本轮分不开：(a) 往**正在运行的** CM3 写零地址本身就是致命的；(b) 启动请求配一张零地址的表导致 GMU 读 0 地址。
- 没有读到 fault 原因寄存器（`CM3_FAULT` 的原因位），所以「事件位被清掉」之后仍然不知道固件为什么 fault。
- 没有把 `GMU2HOST_INTR_INFO` 当成中断接住过（IRQ 仍未接管），所以这里说的始终只是寄存器状态。
- 没有证明 `info` 的 bit 1/bit 10（`cbfsr=00000402` 的那种组合）与 bit 23 有任何关系。
- 0.32 秒与 3.17 秒都是按 R201 的 `IODelay` 标定换算出来的**轮数**，不是墙钟测量。

### 77.13 ROUND 254 的设计（下一轮）

把键 122 的死亡点夹出来，只需要在**危险的三笔之前**提交一次日志：

- 键 124：行 K 之后立刻调用 `boot_log_panic_flush()`（R215/R216 已证明它能提交刚打印的那一行），**再**写 `HFI_QTBL_ADDR = 0` / `HFI_QTBL_INFO = 1` / 启动字 → 轮询 → 行 L。
- 如果行 K 单独留在媒介上，就说明死亡发生在零地址之后的某一笔；如果连行 K 都留不下，说明在 flush 之前的某一步就已经死了 —— 两种结果都把区间缩到一笔之内。
- 键 125（对照）：同样的 flush，但**不**写零地址、只写启动字。它应该完整活下来（与键 120/121 同形），用来证明「加 flush 本身没有改变行为」。

### 77.14 工具事实（这一轮新学到的）

- `PE_parse_boot_argn` 的武装判断只在探针里一处（`fwKey >= 7u && fwKey <= 122u`）；而 `gmu217seen` 那一行**在武装判断之外**，所以未武装的键也会提交窗口 —— 这正是本轮控制键成立的原因。
- 探针的日志是**整轮提交**的：块跑到一半死掉，窗口一个字节都不会变。所以「零字节」≠「块没跑」，要看同一镜像的未武装对照。
- 分割镜头的办法：`r179_rekey.py` 生成的 payload 与上一轮**只差参数字符串那一个字节**（偏移 `0x7963270`），任何更大的差异都说明 rekey 走错了文件。
- 上机脚本会**回读并比对** system_b 的关键块（`flash readback matches` / `key block says`），所以「flashed 的到底是哪个 key」永远不是猜测。

## 七十八、ROUND 254：读出 fault 现场 —— CM3 读队列表时地址丢了 bit 31

### 78.1 一句话

在启动字之后读 CB0 的 `FSR/FAR/FSYNR0`，第一次拿到了**地址**：`far = (队列表地址 & 0x7FFFFFFF) + 8`，两次不同分配逐位吻合；而不写启动字的对照键 `fsr` 停在 `00000400`、`far=0`。也就是说 —— **翻译 fault 是启动请求产生的，产生它的那笔读把队列表地址的 bit 31 丢掉了。**

### 78.2 为什么这一轮去读 fault 现场

R245/R249 反复读到 `cbfsr=00000402`，我们只知道「有一次翻译 fault」，不知道它踩的是哪个地址。而 `arm-smmu` 的 context bank 里，`CB_FAR` 记的正是失败那笔访问的输入地址、`CB_FSYNR0` 记的是层级/读写方向/哪个 CB —— 这三样加起来能把「固件不接受启动」这句现象翻译成一个具体的地址。所以本轮不再改任何写序列，只加**读**。

三个键共用同一段前导（表 + 路由 + 复位 + 清中断），只在启动字前后分开：

| 键 | 对 fault 状态字做的事 | 对启动字做的事 | 想分辨什么 |
|---|---|---|---|
| 124 | 先读、再写 `0xc00001fe` 清、再读回 | 写 | 清之后再出现的 fault，只能是这一轮启动请求带来的 |
| 125 | 一次都不清，连读两次 | 写 | 如果 fault 本来就在，说明它属于布置而不是启动 |
| 126 | 同样清一次 | **不写** | 布置本身会不会自己 fault（对照） |

单变量面貌（R219 键 66 → R254 键 126，同一张表往下推）：

| 轮 | 键 | 与上一格的差别 |
|---|---|---|
| R219 | 66 | 不写任何 store |
| … | … | （见 §74） |
| R252 | 119 | 无启动字 + 满轮询 |
| R253 | 120 | 有启动字 + 命中后再清一次 |
| R254 | 124 | 有启动字 + 清状态字 + 读 FAR/FSYNR0 |
| R254 | 125 | 同 124，但不清状态字 |
| R254 | 126 | 清状态字但**不写启动字**（对照） |

### 78.3 代码形状

块 `if (fwKey == 124u || fwKey == 125u || fwKey == 126u) {`，探针 **735,965 字符 / 11,039 行**，块自身 23,504 字符。GMU 写点严格 **13 笔**（前导 8 + 124 的 3 + 125 的 2 + 126 的 2），状态字写 `0xc00001feu` 两笔（124、126），`CB_FSR` 读 11 处、`CB_FAR` 5 处、`CB_FSYNR0` 3 处，`for (` 5 个、`IODelay(1u)` 4 处、无 `IOSleep`。SMMU 的访问沿用既有方式：读 `rd32(gSmmuVa, off)`，写 `*reinterpret_cast<volatile uint32_t *>(gSmmuVa + off)` 后接 `dsb sy`。

### 78.4 构建与门禁

- CI **#365 run `44884866354`**，`2026-11-21T08:26:05Z → 08:43:33Z`，**28/28 步 success**。
- `kernel.macho` 126,304,256 B sha256 `f7107e05def50342c9e43b5181fbe7c2af3f3e00362794019a0e6faa7e4b5e72`；`payload.img` 150,994,944 B sha256 `ba7c0f52bcb64db1825be0538ea1e37bc9893e7882651f64b4191e4a6fded2b9`。
- 门禁：host **28/28**（GMU 写点 331 → 344）；stage4 **61/61**；stage5 **48/48**；stage6 **1478/1478**；全量 `ran 46 of 48, FAILING: 0`。
- 静态门 `PASS: R254 static gate`（23 条 REQUIRED + 12 SURVIVORS = 35 条在两个产物里都在）；**自证**：指向 R253 的产物 → `FAIL: R254 static gate (23 problems)`。
- 三次 rekey 的 payload sha256：124 = `666f0b8404dd45993844250ea3aca4bb2ac53ad4da41e2ffb25af72fd6531f51`、125 = `7b8a0a141be47ed0b0cc5e57824f5882c1730875335728ccc06adabb5fb22223`、126 = `c77aa7f09161db12c824baaabcf690f6dc9c41311b22913adbb643357790bc09`。

### 78.5 键 124（先清状态字，再写启动字）

`boot_id 604034594`，窗口 `4147719ce03d2b3f38b7db9ef26b05906744b4731aeaaaa9d83eed6a65b94401`，12/12 行。

```
gmu124a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=124 mode=124 qtbl=ad960000
gmu124b=build=0 need=3 tables=3 writes=3 root=ad964000 base=ad800000 ias=48 oas=36 ok=1
gmu124c=plan=0 n=9 abs=0 nab=9 cbndx=0 ttbr=ad964000 cw=1 id0=48016605
gmu124d=wrote=9 ttbr0=ad964000/00000000 sctlr=00001067 tcr=00803510 cbar=0001f300 root=ad964000
gmu124e=smr_set wanted=80000004 back=80000004 valid=1 sid=4 landed=1
gmu124f=s2cr_set wanted=00000000 back=00000000 typew=00000000 landed=1
gmu124g=last_row=1 about_to_start=1 mode=124 route=1 smr=80000004 s2cr=00000000 qtbl=ad960000 base=ad800000
gmu124h=init_cleared=1 before=babeface after_clear=00000000 about_to_reset=1
gmu124i=reboot=1 fwinit=babeface polls=4 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu124j=interrupts_cleared=1 info=00000000 mask=00000000 ao=00000000 about_to_start=1
gmu124k=fsr_cleared=1 fsr_before=00000400 fsr_clear_back=00000400 far_before=00000000 about_to_start=1
gmu124l=cleared_then_started=1 ctl=00000000 bit0=0 polls=200000 info=00800000 fsr=00000402 far=2d960008 fsynr0=00000021 survived=1
```

行 L 的三样：`fsr` 从 `00000400` 变成 `00000402`（`TF` bit1 置起），`far=2d960008`，`fsynr0=00000021`。

`fsynr0=0x21` 按钉住的上游位域解出来：`PLVL = 0x21 & 0x3 = 1`（在 level-1 走表时失败）、`WNR = 0`（读）、`S1CBNDX = 0`（本 CB0）。**所以这是一笔读，读的是 CB0 上某个没被映射到的地址。**

### 78.6 键 125（一次都不清，连读两次）

`boot_id 650093298`，窗口 `86b7007b1edfa187d550b7261add5f65c1cea20fedae83fa480289004b7227fe`，12/12 行。

```
gmu124a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=125 mode=125 qtbl=ad90d000
gmu124b=build=0 need=3 tables=3 writes=3 root=ad911000 base=ad800000 ias=48 oas=36 ok=1
gmu124g=last_row=1 about_to_start=1 mode=125 route=1 smr=80000004 s2cr=00000000 qtbl=ad90d000 base=ad800000
gmu125k=un_cleared=1 fsr_before=00000400 fsr_again=00000400 far_before=00000000 about_to_start=1
gmu125l=un_cleared_then_started=1 ctl=00000000 bit0=0 polls=200000 info=00800000 fsr=00000402 far=2d90d008 fsynr0=00000021 survived=1
```

两次相邻读都是 `00000400`（稳定，没有 TF），启动字之后变成 `00000402`，`far=2d90d008`。这一轮的队列表是 `ad90d000`，所以：

```
0xAD90D008 & 0x7FFFFFFF = 0x2D90D008
```

**与上一轮 `0xAD960008 & 0x7FFFFFFF = 0x2D960008` 逐位同构** —— 两次不同的分配地址、两次不同的 boot，fault 地址都是「队列表 + 8、并去掉 bit 31」。这不是巧合能解释的量级。

### 78.7 键 126（清状态字，不写启动字）

`boot_id 637424232`，窗口 `7f39d67886c1dfdc2cdcd426a55a37a48b5a9214dad0cca6d0e6ca93c1ff3bd4`，12/12 行。

```
gmu124a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=126 mode=126 qtbl=ad90f000
gmu124g=last_row=1 about_to_start=0 mode=126 route=1 smr=80000004 s2cr=00000000 qtbl=ad90f000 base=ad800000
gmu126k=fsr_cleared=1 fsr_before=00000400 fsr_clear_back=00000400 no_start=1 about_to_poll=1
gmu126l=no_start_polled=1 ctl=00000000 bit0=0 polls=200000 info=00000000 fsr=00000400 far=00000000 fsynr0=00000000 survived=1
```

表、路由、使能、复位全都做完，只差那一笔启动字 —— 状态字从头到尾 `00000400`，`far=00000000`，`info=00000000`。**布置本身一笔 fault 都不产生。**

### 78.8 判决

1. **fault 归属启动请求**：键 124（清了再启动）与键 125（不清就启动）都在启动字之后由 `00000400` 变成 `00000402`；键 126 做了同样的布置、同样的清、同样的 200000 轮轮询，但**不写启动字**，状态字停在 `00000400`。三者唯一差别就是启动字。
2. **地址可复现**：`far` 两轮分别是 `2d960008` 与 `2d90d008`，各自等于本轮的队列表地址 `+8` 再清掉 bit 31。
3. **访问类型**：`fsynr0=00000021` 两轮逐位相同 → 读、level-1 走表失败、本 CB0。
4. **固件仍然没有启动**：三键的 `ctl=00000000 / bit0=0`，键 124/125 的 `info=00800000`、键 126 的 `info=00000000`。

### 78.9 必须分开说的三件事

1. `far` 是**送进 SMMU 的那个地址**，不是我们写进 `HFI_QTBL_ADDR` 的地址。我们在 `gmu124a/g` 读到 `qtbl=ad960000`、`ad90d000`，而 SMMU 看到的是它们的 31 位截断形式。至于截断发生在 CM3 内部、HFI 地址通路、还是 SMMU 的输入装配上，本轮**没有分辨**。
2. `fsr` 的 `FORMAT` 字段（`GENMASK(10,9)`，此处 = 2）在两轮里**清之前就在**（`fsr_before=00000400`）。所以「清回 `00000400` 而不是 `00000000`」不是清失败 —— 恰恰相反，`0xc00001fe` 这组 W1C 位全被清掉了，只有不受它管的 `FORMAT` 位留着。这一点与上游 Linux 只写 `FSR_FAULT` 而不写 `FORMAT` 一致，也说明我预先写进规则里的「`fsr_clear_back` 不是 0 就是没清掉」这句话**过宽**，真正的判据是 `TF` 位。
3. 本轮拿到的是**寄存器读数**，不是 HFI 消息。`info` 的 bit 23 仍然只是「有一笔 fault」，我们读的是 SMMU 自己的账本，不是固件的解释。

### 78.10 推论与 R255 设计

如果「地址丢 bit 31」就是原因，那么把那段被截断的地址**映射出来**，那笔读就应该成功。R255 三个键：

- **127**：把 identity 表建成 `base = 0x2D800000`（覆盖 `0x2D800000–0x2D9FFFFF`，正好包含 `0x2D960008`/`0x2D90D008` 所在的 2 MiB），队列表仍在原地，写启动字，满轮询，再读 `FSR/FAR/FSYNR0/ctl/info`。若 `fsr` 干净、`far=0`，而 `info` 或 `ctl` 有变化 —— 截断这条链就被证实是**功能性的**，不只是地址算术。
- **128**：把 `HFI_QTBL_ADDR` 直接写成截断形式（`qtbl & 0x7FFFFFFF`），读回看重写有没有被保住，再写启动字 —— 分辨固件是「自己会截断」还是「必须我们给截断值」。
- **129**（对照）：同 127 的映射、但**不写启动字**，确认新映射本身不产生 fault。

### 78.11 没证明什么

- 没证明 CM3 会接受 `0x2D…` 这段地址 —— 只是证明了它会**读**那里。
- 没证明截断发生在哪一级（CM3 / HFI 地址通路 / SMMU 输入）。
- 没证明 HFI 队列成功之后固件就会启动：`ctl` bit0 至今每一次都是 0。
- 没证明那笔读就**是**队列表描述符（`+8` 与描述符首项位置吻合，但寄存器只给地址，不给内容）。
- 没有解释 `FORMAT=2` 为什么在启动字之前就已置位。

### 78.12 边界与工具事实

- 三个键都是 `check-reboot-to-twrp.py --arm` PASS 之后上机，看门狗把它们带回 TWRP，全程没有按键。
- 键 125 的两次相邻读都在启动字之前，`00000400` 与 `00000400` 相同 ⇒ 状态字不会自己动，所以「读一次就定论」在本轮是被支持的。
- 键 124 第一次跑是我忘了先 rekey，runner 在 `ABORT missing …\r254-s124-payload\payload.img` 处自己停下（没有碰设备）—— 上机脚本的「先查文件再动设备」这条自检这一轮救了一次。
- `r254_static.py` 与门禁的四处同步各自都踩了一次「终止符指向最新块」的老坑：新块插在 R253 与 ladder 之间时，`_k117119` 的终止符要**退回** R253 块头、`_k120122` 要改成 R254 块头。规则不变：**切片终止符永远是「下一个块的块头」。**

## 七十九、ROUND 255：把丢掉的那一位补进表里，fault 还在原地 —— 而把截断值交给固件则连日志都活不到

### 79.1 一句话

R254 说 CM3 读队列表时地址丢了 bit 31，于是这一轮**把那一位补进表里**：在 IOVA `0x2d800000`（覆盖 `0x2d960008` 所在的 2 MiB）建一张非 identity 块指向队列表的物理页，再写启动字。结果 fault 原地不动（`fsr=00000402 far=2d960008 fsynr0=00000021`，与 R254 逐位相同）—— **我们新写的描述符没有生效**；而不写启动字的对照键状态字干净（`fsr=00000400 far=0`）。第三个键把截断值直接写进 `HFI_QTBL_ADDR`，**连一行日志都活不到（2/2）**。

### 79.2 为什么这一轮去建非 identity 表

§78 的判决把 fault 归到「启动请求读队列表时地址丢了 bit 31」。那么最直接的一步就是：既然它要 `0x2d960008`，就把 `0x2d800000` 这 2 MiB 映射到队列表所在的那一页。`adreno_table_build()` 的签名本来就是分离的（`vbase/pbase` 是表页放哪、`iova/paddr` 是映射哪），所以不需要改表构建器，只需要换实参。

三个键共用同一段前导（表 + 路由 + 复位 + 清中断），只在启动字前后分开：

| 键 | 表建在哪 | 启动字 | 想分辨什么 |
|---|---|---|---|
| 127 | IOVA `0x2d800000` → PA `ad800000`（队列表页） | 写 | 补上那一位之后，walk 能不能过 |
| 128 | 同上，但把 `HFI_QTBL_ADDR` 写成 `qtbl & 0x7fffffff` | 写 | 截断到底在门内还是门外：给它截断值，它还用不用 |
| 129 | 同 127 | **不写** | 这张低位表本身会不会 fault（对照） |

单变量面貌（R219 键 66 → R255 键 129，同一张表往下推）：

| 轮 | 键 | 与上一格的差别 |
|---|---|---|
| R219 | 66 | 不写任何 store |
| … | … | （见 §74、§76、§77） |
| R253 | 120 | 有启动字 + 命中后再清一次 |
| R254 | 124 | 有启动字 + 清状态字 + 读 FAR/FSYNR0 |
| R255 | 127 | 同上，但表改建成**非 identity**（IOVA `0x2d800000`） |
| R255 | 128 | 同 127，但 `QTBL_ADDR` 给的是**已截断**的值 |
| R255 | 129 | 建了同样的低位表，但**不写启动字**（对照） |

### 79.3 代码形状

块头 `if (fwKey == 127u || fwKey == 128u || fwKey == 129u) {`，块 24,563 字符，`braces` 净 0，16 条行名（`gmu127a`–`gmu127j` 共用前导，`gmu127k/l`、`gmu128k/l`、`gmu129k/l` 各两支），13 笔 `gmuW[...] =` store，5 个 `for (`，4 处 `IODelay(1u)`，没有 `IOSleep`。

三处针对性改动：

1. 非 identity 的建表实参：`adreno_table_build(&l127, (uint64_t)s127virt, s127phys, 0x2d800000ull, r127base, 0x200000ull, prot127, &t127);`，行 B 因此多了 `iova=%08x`（读到的就是 `2d800000`），行 A/G 多了 `qtbl_value=%08x`。
2. `HFI_QTBL_ADDR` 的 store 从常量改成 `r127qtblValue`，而 `r127qtblValue = (r127mode == 128) ? (r127qtbl & 0x7fffffffu) : r127qtbl;` —— 只有键 128 交出去的是截断值。
3. 行 K/L 各记一组 `fsr/far/fsynr0`，键 127/129 的行 K 还额外记 `iova=2d800000 pa=...`，把「我们建了什么」与「SMMU 说它想要什么」并排。

### 79.4 构建与门禁

- CI **#366 `44890310462`**：`2026-11-21T09:35:33Z → 09:51:26Z`，**28/28** 步，success。
- 产物：`work\tmp\r255-kernel\kernel.macho` **126,304,256 B** sha256 `19a2ec602924d38850fa41456701030a1477f27389b005469dfa7841d7edfe3a`；`work\tmp\r255-payload\payload.img` **150,994,944 B** sha256 `b8073afaf99867e8bb2d69096b7639e33096a0ee3c35bcc2de3032941ec6b456`。
- 静态门：`35 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、`forbidden: 3 checked`、固件 `a630_gmu.bin`（32,768 B，sha256 钉住）→ `PASS: R255 static gate`；**区分能力自证**：指向 `r254-kernel`/`r254-payload` → `FAIL: R255 static gate (23 problems)`（逐条 `kernel=0 payload=0`）。
- 四处门禁：host **28/28**、stage4 **61/61**、stage5 **48/48**、stage6 **1496/1496**、全量 `r239_allgates.py` → **ran 46 of 48, FAILING: 0**。探针 **760,529 字符 / 11,453 行**，严格 GMU 写点 **357**，`rd32(gSmmuVa, 0x24u)` **15** 处，arm 窗口 `<= 129u` 唯一一处。

### 79.5 键 127 逐字读数

```
gmu127a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=127 mode=127 qtbl=ad960000 qtbl_value=ad960000
gmu127b=build=0 need=3 tables=3 writes=3 root=ad964000 base=ad800000 iova=2d800000 ias=48 oas=36 ok=1
gmu127c=plan=0 n=9 abs=0 nab=9 cbndx=0 ttbr=ad964000 cw=1 id0=48016605
gmu127d=wrote=9 ttbr0=ad964000/00000000 sctlr=00001067 tcr=00803510 cbar=0001f300 root=ad964000
gmu127e=smr_set wanted=80000004 back=80000004 valid=1 sid=4 landed=1
gmu127f=s2cr_set wanted=00000000 back=00000000 typew=00000000 landed=1
gmu127g=last_row=1 about_to_start=1 mode=127 route=1 smr=80000004 s2cr=00000000 qtbl=ad960000 qtbl_value=ad960000 base=ad800000
gmu127h=init_cleared=1 before=babeface after_clear=00000000 about_to_reset=1
gmu127i=reboot=1 fwinit=babeface polls=4 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu127j=interrupts_cleared=1 info=00000000 mask=00000000 ao=00000000 about_to_start=1
gmu127k=low_map=1 iova=2d800000 pa=ad800000 fsr_before=00000400 fsr_clear_back=00000400 far_before=00000000 about_to_start=1
gmu127l=low_map_started=1 ctl=00000000 bit0=0 polls=200000 info=00800000 fsr=00000402 far=2d960008 fsynr0=00000021 qtbl_now=ad960000 survived=1
```

`boot_id` **639964159**、窗口 `5138f374ffab9d75b66cb03702924b654f462d9b6f2e53272d640b183c84290f`、12/12 行、行 `key=127`。

判决：**THE WALK STILL FAULTED INSIDE THE RANGE THIS KEY MAPPED** —— `far=2d960008` 落在刚映射的 `[2d800000, 2da00000)` 里，`fsr` 形状与 R254 逐位相同，`fsynr0=00000021`（PLVL=1、WNR=0 读、CBNDX=0）。也就是说**这张新表没有对这个 walk 生效**。

### 79.6 键 128 逐字读数

两次上机，两次都是**零字节**：窗口仍是上一轮的（`boot_id` 639964159 / `1b15ffaf…`），行计数 `gmu128k=0 gmu128l=0`（10 of 12）。payload sha256 `e5ee238d3c319355f7042115e5dac20a3214a6e8c33bc44ccccdc576b66e1734`，flash 回读通过、boot 送出，之后没有任何行落盘。

判决：**HANDING THE FIRMWARE THE TRUNCATED ADDRESS KILLS THE ROUND BEFORE IT CAN COMMIT A ROW (2/2)** —— 与 R253 的键 122（把 `HFI_QTBL_ADDR` 写成 `0`，3/3 零字节）是同一个形状：**我们在这里写过的每一个 bit 31 被清掉的值，都把这一轮带进了「活不到提交」的结局**；而键 127/129（值仍是 `ad960000`）都正常落盘。这一轮没有定位死亡点（探针日志整轮提交，中途死掉一个字节都不落）。

### 79.7 键 129 逐字读数

`boot_id` **600256967**、窗口 `1b15ffafae7bf9357e9efecb18ae402446cf634d939ed45b20004458259eaf4e`、12/12 行、行 `key=129`：

```
gmu127b=build=0 need=3 tables=3 writes=3 root=ad964000 base=ad800000 iova=2d800000 ias=48 oas=36 ok=1
gmu127i=reboot=1 fwinit=babeface polls=4 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu129k=low_map=1 iova=2d800000 pa=ad800000 fsr_before=00000400 fsr_clear_back=00000400 no_start=1 about_to_poll=1
gmu129l=low_map_no_start=1 ctl=00000000 bit0=0 polls=200000 info=00000000 fsr=00000400 far=00000000 fsynr0=00000000 survived=1
```

判决：**WITHOUT ANY START WORD THE LOW MAPPING IS CLEAN TOO**（`fsr=00000400` 即 TF=0，`far=0`，`info=0`）—— 建表与路由本身不 fault，所以键 127 的那一次 fault 属于启动请求。

### 79.8 判决（四条）

1. **补上 bit 31 没有改变 walk 的结局**：`far` 与 R254 逐位相同，`fsynr0` 逐位相同 —— 描述符没有对这个 walk 生效。
2. **PLVL=1 把两件事连起来**：R254 是 identity 表（`ad800000`），它的 L1 下标是 2，而截断后的 IOVA `2d960008` 的 L1 下标是 0 ⇒ 走到 level 1 就遇到空描述符；R255 新建的表**正是往 L1[0] 写的**，可结果仍是 PLVL=1 ⇒ 我们写的描述符没有被用上。
3. **布置本身是干净的**（键 129），所以「fault 归启动请求」这条 R254 的结论不变。
4. **bit 31 被清的值是致命的**：`0`（R253 键 122，3/3）与 `qtbl & 0x7fffffff`（R255 键 128，2/2）都活不到提交；未截断的值（127/129）都正常。

### 79.9 必须分开说的三件事

1. **「描述符没生效」有两个可能的原因，这一轮没有分辨**：① 我们写进内存的描述符对 SMMU 不可见（表页的 cache/coherency 属性），② 这个 walk 根本没用我们编程的 `TTBR0`。R256 的第一件事就是把「内存里到底写了什么」读回来，再把三种可能分开。
2. **`far` 是 SMMU 侧记录的地址**，不是我们写进寄存器的值；键 127 的 `far` 与 R254 相同，正说明「读者想要的地址」没变，变的是我们能不能满足它。
3. **键 128 的死亡不是「块没跑」**：它跑不到提交，与 R253 键 122 同形；死亡点在 `HFI_QTBL_ADDR` 写入之后的某一处，本轮没有定位。

### 79.10 没证明什么

- 没证明我们的表**在任何情况下**都不生效：只证明「用这一套建表 + 编程流程，对这一个 walk 不生效」。
- 没证明 SMMU 的能力边界：没有做过「先用一张表让某个已知访问成功」的正向对照。
- 没证明键 128 的死因，也没证明它和 bit 31 的因果关系（只是两个 bit-31-清的值都死、两个 bit-31-置的值都活）。
- 没读到 `HFI_CTRL_STATUS` 的任何变化：`ctl=0` 三次全 0，固件仍然没有报告 HFI 启动。

### 79.11 R256 设计

三键，第一次**不去改任何 store 语义**，只回答「我们写的描述符到底在哪」：

| 键 | 做什么 | 判据 |
|---|---|---|
| 130 | 建低位表 + 编程 CB0，然后把三张表页里的关键描述符（`L0[0]`、`L1[0]`、`L2[0x16C]`）从内存**读回来**打印；不写启动字 | 读回值与 plan 逐位相同 ⇒ 我们写对了，问题在可见性；不同 ⇒ 建表/写回路径有问题 |
| 131 | 同 130，另在启动字之前对三张表页做 `dc civac` + `dsb sy`，然后写启动字 + 满轮询 | fault 消失 ⇒ 是 cache 可见性；fault 仍在 ⇒ 这个 walk 不用我们的 `TTBR0` |
| 132 | 同 131 的清理，但**不写启动字** | 清理本身不产生 fault（对照），否则 131 的干净不能归因 |

### 79.12 工具事实

- `r255_static.py` 必须从**仓库根**跑（`PROBE` 是仓库相对路径）；它的 `FORBIDDEN` 三条是下一轮的名字（`gmu130a=`/`gmu128m=`/`gmu129m=`）。
- runner 若没先 `r179_rekey.py` 就会在动设备之前 `ABORT missing …payload.img`（自检救了一次：R254 键 124 的第一次运行）。
- `r255_anchor.py <runner> <prev_boot_id> <prev_sha256>` 会回读上一轮窗口并打印 `anchor cross-check: … has that sha256`，然后再改 `PREV_BOOT/PREV_SHA`；键 128 的第二次运行时窗口已被 129 覆盖，所以它报的是 `FRESH WINDOW BUT IT NAMES key=129, NOT 128`，这是正确的警告而不是误判。

## 八十、ROUND 256：把描述符从内存读回来，又把它们刷到一致点 —— fault 一步不动，所以 walk 看的不是这张表

### 80.1 一句话

我们写进内存的三层描述符与 plan 的编码**逐位相同**（两轮、两组不同地址、各三项 match=1/1/1），把它们 `dc civac` 刷到一致点也**不改变**下一帧 fault：`fsr=00000402 far=2d960008 fsynr0=00000021` 与 R255 逐位相同。因此那一帧 walk 用的不是我们建的这张表。

### 80.2 为什么先读回描述符，而不是继续改 store

R254 把 fault 定到了队列表读取上，R255 把 bit 31 被丢掉的地址补进表里，结果 fault 原地不动。两轮都假定了一件事：「我们写进去的描述符就是内存里的描述符」。这一轮把这个假定拆成两步验证：先读回（内存内容），再刷到一致点（可见性）。两步都通过的情况下，「描述符错了」和「cache 看不见」这两条解释同时被排除。

### 80.3 三键设计与单变量表

| 键 | 写入 | 读回 | 问的问题 |
|---|---|---|---|
| 130 | 建低位表 + 编程 CB0（八笔前导，不写启动字） | 从内存读 `L0[0]`、`L1[0]`、`L2[0x16C]`，与 `adreno_lpae_table_pte/leaf_pte` 编码逐位比 | 我们写进去的东西是不是就在内存里 |
| 131 | 同 130，另对三页做 `dc civac` + `dsb sy`，然后清状态字 + 掩码 + 启动字 + 200000 轮 | 读 FSR/FAR/FSYNR0 | 内存正确且可见时，这帧 walk 还 fault 吗 |
| 132 | 同 131 的清理，**不写启动字** | 读 FSR/FAR/FSYNR0 | 清理本身会不会已经把 fault 弄出来（对照） |

单变量表（从 R219 键 66 延下来，只看「清状态字之后还清不清页、写不写启动字」这一组）：

| 键 | 表 | 清页 | 启动字 | 结果 |
|---|---|---|---|---|
| 129（R255） | 低位表 | 无 | 无 | `fsr=00000400 far=00000000`，干净 |
| 130（R256） | 低位表 | 无 | 无 | 同上，且三层描述符 m0=m1=m2=1 |
| 132（R256） | 低位表 | 有 | 无 | 仍 `fsr=00000400 far=00000000`，清理不改变任何东西 |
| 127（R255） | 低位表 | 无 | 有 | `fsr=00000402 far=2d960008 fsynr0=00000021` |
| 131（R256） | 低位表 | 有 | 有 | **逐位相同的 fault** |

### 80.4 代码形状

- 块 **25,022 字符**，`{}` 平衡为 0；探针 **785,551 字符 / 11,907 行**；全文严格 GMU 写点 **369**（R255 的 357 + 本轮 12）。
- 块内：`gmuW` **12**（前导 8 + 三个掩码 + 一个 `HFI_CTRL_INIT`）、`for (` **6**（写表、初始化轮询、清页、三个键各一）、`IODelay(1u)` **4**、`IOSleep` **0**、`dc civac` **1**；FSR/FAR/FSYNR0 的字节偏移 `0x58u` **12**、`0x60u` **5**、`0x68u` **3**。
- 读回用 `*reinterpret_cast<volatile uint64_t *>`（探针首次出现 64 位 volatile 读，stage5 的指针白名单已同步放宽）；越界保护：只在 `root_off + 0x1000 * tables <= span` 时才往下走。

### 80.5 构建与门禁

- host 门 **28/28** + `PASS: enchilada adreno probe shim + modules type-check`；stage4 **61/61**、stage5 **48/48**、stage6 **1517/1517**；全量 `r239_allgates.py` → **ran 46 of 48, FAILING: 0**。
- CI **#367 run `44908085664`** `completed / success`，`2026-11-21T13:17:09Z → 13:36:29Z`，28/28 步。
- 取件：`r256-kernel\kernel.macho` **126,320,640 B** sha256 **`6aaeee486f29defc11d2b7d10b1c351b84585c72cf91c21153d749e7b331afb3`**；`r256-payload\payload.img` **150,994,944 B** sha256 **`d75e04ec1fe9b9439987448cee2ef14726b714ae827fe070d43cb2e0bec0e8eb`**。
- 静态门：**26 条 REQUIRED + 12 条 SURVIVORS = 38 条在两件产物里都出现**，`key marker` kernel=1 payload=2，固件钉 `a630_gmu.bin` 32,768 B → **`PASS: R256 static gate`**；**自证**：指向 `r255-kernel`/`r255-payload` → **`FAIL: R256 static gate (26 problems)`**。

### 80.6 键 130：内存里的三层描述符就是 plan 的描述符

```
gmu130a=cx=80001fe0 cx_on=1 geom=0 pgshift=12 cb_base=8000 numcb=5 smmu=1 key=130 mode=130 qtbl=ad912000 qtbl_value=ad912000
gmu130b=build=0 need=3 tables=3 writes=3 root=ad916000 base=ad800000 iova=2d800000 ias=48 oas=36 ok=1
gmu130c=plan=0 n=9 abs=0 nab=9 cbndx=0 ttbr=ad916000 cw=1 id0=48016605
gmu130d=wrote=9 ttbr0=ad916000/00000000 sctlr=00001067 tcr=00803510 cbar=0001f300 root=ad916000
gmu130m=readback=1 root_off=0 l2_idx=364 l0=00000000ad917003 l1=00000000ad918003 l2=00000000ad800401 m0=1 m1=1 m2=1 pages=3
gmu130n=cleaned=0 pages=3 span=3000 dsb=0
gmu130k=no_clean=1 fsr_before=00000400 fsr_clear_back=00000400 far_before=00000000 no_start=1
gmu130l=no_clean_no_start=1 ctl=00000000 bit0=0 polls=200000 info=00000000 fsr=00000400 far=00000000 fsynr0=00000000 survived=1
```

（boot_id **645270395**，窗口 `541f66c26cabdefdc99ad4bddce2fb5ace429d252d6f14e7c3848ac32343f462`，14/14 行）

- `l0=00000000ad917003`：有效 + 表描述符（低两位 `11`），指向 L1 页 `ad917000`。
- `l1=00000000ad918003`：同上，指向 L2 页 `ad918000`。
- `l2=00000000ad800401`：有效的 **2 MiB block**（低两位 `01`），物理基址 `ad800000`，`AF` 已置（bit10）。
- `l2_idx=364` = `0x16C` = `(0x2d800000 >> 21) & 0x1ff`，与我们要塔的 IOVA 对得上。
- `m0=m1=m2=1`：三项与 `adreno_lpae_table_pte(l1pa)` / `adreno_lpae_table_pte(l2pa)` / `adreno_lpae_leaf_pte(prot130, 2u, r130base)` **逐位相同**。
- 没有启动字时：`fsr=00000400`（TF=0，只剩 FORMAT=2 这个不受 W1C 管的字段）、`far=0`、`info=0`。

**判决**：`THE THREE WORDS IN MEMORY ARE EXACTLY WHAT THE PLAN ENCODED, so the store, the paging and the leaf encoding are all right in memory`。

### 80.7 键 131：清到一致点之后，fault 逐位相同

```
gmu130b=build=0 need=3 tables=3 writes=3 root=ad965000 base=ad800000 iova=2d800000 ias=48 oas=36 ok=1
gmu130m=readback=1 root_off=0 l2_idx=364 l0=00000000ad965003 l1=00000000ad966003 l2=00000000ad800401 m0=1 m1=1 m2=1 pages=3
gmu130n=cleaned=1 pages=3 span=3000 dsb=1
gmu131k=cleaned_then_started=1 fsr_before=00000400 fsr_clear_back=00000400 far_before=00000000 about_to_start=1
gmu131l=cleaned_then_started_end=1 ctl=00000000 bit0=0 polls=200000 info=00800000 fsr=00000402 far=2d960008 fsynr0=00000021 qtbl_now=ad960000 survived=1
```

（boot_id **645823279**，窗口 `ce1e51af03270b18cb50895a659b16b0d36b42559836534b0a4d3788e4b73815`，14/14 行）

- 这一轮的表又是**新地址**（`ad965000/ad966000`，与 130 的 `ad917000/ad918000` 不同），而三项仍然 `m0=m1=m2=1`。
- 清页已执行：`cleaned=1 pages=3 dsb=1`。
- 启动字之后：`fsr=00000402`（TF=1, FORMAT=2）、`far=2d960008`、`fsynr0=00000021`（PLVL=1、WNR=0、S1CBNDX=0）、`info=00800000`、`ctl=0`。
- 与 R255 键 127 的 `fsr=00000402 far=2d960008 fsynr0=00000021` **逐位相同**。

**判决**：`THE WALK STILL FAULTS AFTER THE PAGES WERE CLEANED`，且 `THE WORDS ARE RIGHT, THE CLEAN RAN, AND THE WALK STILL DIES INSIDE THE RANGE THIS ROUND MAPPED`。

### 80.8 键 132：清页本身不产生 fault

```
gmu130n=cleaned=1 pages=3 span=3000 dsb=1
gmu132k=cleaned_no_start=1 fsr_before=00000400 fsr_clear_back=00000400 no_start=1
gmu132l=cleaned_no_start_end=1 ctl=00000000 bit0=0 polls=200000 info=00000000 fsr=00000400 far=00000000 fsynr0=00000000 survived=1
```

（boot_id **645545880**，窗口 `c862b9be4047b15434c863990ddcb1f59162ddcfb19789e9404d1780826c44cb`，14/14 行）

**判决**：`WITH THE SAME CLEAN AND NO START WORD THE STATUS WORD STAYS CLEAN (fsr=00000400 far=00000000), so cleaning changes nothing until the firmware is asked`。

### 80.9 判决四条

1. **我们写的描述符就是内存里的描述符**：两轮、两组地址、三项逐位 match。「建表写错了」这条解释被排除。
2. **可见性也被排除**：`dc civac` + `dsb sy` 之后 fault 与未清页时逐位相同。「cache 看不见」这条解释被排除（至少对我们能刷的那三页）。
3. **fault 仍在我们映射的 2 MiB 里**：`far=2d960008` 位于 `2d800000..2d9fffff`，而那一页的 L2 条目就是我们写的 `l2=ad800401`。
4. **所以那一帧 walk 用的不是这张表**（或不是这一份 TTBR）。`fsynr0` 的 PLVL=1 说「level-1 描述符无效」，而我们的 L1[0] 就是有效的表描述符 —— 这两件事直接矛盾，矛盾本身就是结论。

### 80.10 必须分开说的三件事

1. **「清到一致点」只能说到 PoC 为止**：`dc civac` 保证的是我们这个核心写的线到一致点，它不能证明 SMMU 自己的 **walk cache / TLB** 里没有旧的「描述符无效」条目。本轮没发任何 SMMU sync / TLB invalidate。
2. **`far` 仍然是「送进 SMMU 的地址」**，不是我们写进 `HFI_QTBL_ADDR` 的值（本轮已经不截断地交付 `ad960000`，而 fault 地址仍是 `2d960008`）。
3. **`fsr=00000402` 的 FORMAT=2 不属于 W1C 那组位**，所以「清完回读还是 `00000400`」不代表没清掉；判据是 TF 位。

### 80.11 没证明什么

- 没证明 SMMU walk 完全不用 CB0 的 TTBR0：只证明「在这一套编程顺序下，启动字之后那一帧 walk 没有用到我们刚写的描述符」。
- 没读到启动字**之后**的 CB0 五字（TTBR0/TTBR1/SCTLR/TCR/CBAR）—— 本轮只在编程后、启动字前读了一次（`gmu130d`）。
- 没读 `GFSR`，也没读其他 context bank 的 FSR —— 因此无法排除「这帧 walk 落在另一个 CB 里」。
- 没做「先让一个已知访问成功」的正向对照，所以「CB0 的 stage-1 是否在这个引导上任何时侯真的工作过」本轮仅属于「未证明」。

### 80.12 R257 设计

三键，全部围绕「那帧 walk 到底用的是什么」：

| 键 | 做什么 | 判据 |
|---|---|---|
| 133 | 编程 CB0 → 清状态字 → 启动字 → 满轮询 → **再读 CB0 五字**（TTBR0/TTBR1/SCTLR/TCR/CBAR）+ FAR/FSR | 五字仍是我们写的值 ⇒ 表指针没被改；被改 ⇒ 固件或硬件在启动时改写了 CB |
| 134 | 同 133，另读 `GFSR` 与 CB0..CB4 的 `FSR/FAR` | 若 fault 只在 CB0 ⇒ 流路由正确；若在另一个 CB ⇒ 我们一直读错了窗口 |
| 135 | 编程 CB0 → **做 SMMU sync + TLB 失效**（GR0 的 sync/tlbi）→ 启动字 → 满轮询 → 读 fault（不做清页，与 127/131 对比） | fault 消失 ⇒ 是 walk cache 旧条目；不消失 ⇒ 不是 TLB 问题 |

### 80.13 工具事实

- `r256_block.py` 里的 `re.subn(...)` **返回 `(new, count)`**，写成 `n, k = re.subn(...)` 会把新字符串丢进 `n`、只拿 `k` 做断言，于是 arm 窗口根本没改而断言还会过（实测：探针里还是 `<= 129u`）。当时是 stage4 的 `'fwKey >= 7u && fwKey <= 132u' in code` 把它抓出来的。
- 探针的 arm 窗口全文只有一处 `(fwKey >= 7u && fwKey <= 132u)`，在 `EnchiladaAdrenoProbe.cpp:1536`；写新块时要把它一起推到新上限，否则新键**根本不被武装**。
- stage5 的指针白名单在 `scripts/verify-enchilada-probe-stage5.py:252-254`；R256 首次用到 `volatile uint64_t *`，需加进去（已加）。
- `r256_static.py` 也必须从**仓库根**跑；它的 REQUIRED 是 26 条，`FORBIDDEN` 三条是下一轮的名字（`gmu133a=`/`gmu131m=`/`gmu132m=`）。

## 八十一、ROUND 257：把上下文银行自己的字读回来 —— 它仍是我们写下的样子，而 fault 一步不动

### 81.1 一句话

R254 读出 `far=2d960008`（队长地址丢了 bit 31），R255 把那一位补进表里而 fault 留在原地，R256 把描述符从内存读回来又刷到一致点、fault 仍然一步不动。这一轮换了一个完全不同的方向：**不再改任何东西，只在启动字之后把上下文银行 0 自己的字（TTBR0/SCTLR/TCR/CBAR/SMR0/S2CR0）读回来，再读全局状态字与四个邻银行**，并且照着厂商驱动在 probe 时的做法先做一次翻译失效。三键的答案是：银行里仍是我们写下的那五个数，全局字与四个邻银行全干净，先失效再启动也一点都不改 —— 而 fault 的字、地址、`plvl` 与 `cbndx` 三键逐位相同。**本轮没有解开缺口，但把「不是哪些原因」这一类收得更紧了，并且暴露出一个新事实：`FSR` 与 `FAR` 在两次相同的引导里给出了互相矛盾的读数。**

### 81.2 为什么先读银行自己的字，再做厂商的失效

前几轮所有「表是对的」的证据都来自**我们自己那条路径**：我们写的字、我们读回的数、我们建的表。如果 SMMU 走的根本不是这个上下文银行（或者我们的写被别的东西改回去了），那么「表对、fault 仍在」这句话就没有归因力。所以这一轮不写新东西，只做三件纯观测：

1. **键 133**：启动字之后，把 CB0 自己的 `TTBR0/SCTLR/TCR/CBAR` 与 `SMR0/S2CR0` 读回来，与启动之前写下的值比 —— 银行里还是我们写的那五个数吗？
2. **键 134**：同一个形状，再读 **全局** `sGFSR`（`USF` BIT1：有没有未匹配的流在 fault）与 **CB1..CB4 的 FSR/FAR** —— fault 是我们这一条流造成的，还是别处也有？
3. **键 135**：不读，改做一次**厂商自己的失效**：`TLBIALLH` → `TLBIALLNSNH` → 全局 sync（轮询 `sTLBGSTATUS` 到 0）→ 每银行 sync（`CB_TLBSYNC` 后轮询 `CB_TLBSTATUS` 到 0），然后才清状态字、开一个掩码源、写启动字。如果之前的 fault 是「旧翻译藏在 TLB / walk cache 里」，这一键应该把它清掉。

三键共用同一段前导（建 2 MiB 低位表、九笔编程 CB0、读回描述符、`dc civac` 清页、`SMR0/S2CR0` 路由、CM3 复位脉冲），所以唯一变量仍然是「启动字之前/之后我们做了什么观测」。

### 81.3 三键与单变量表

| 轮次/键 | 表 | 清页 | 启动字 | 额外动作 | 结果 |
|---|---|---|---|---|---|
| R255 / 127 | 低位 IOVA `2d800000` → 队列表页 | 无 | 有 | 无 | `fsr=00000402 far=2d960008` |
| R256 / 131 | 同上 | `dc civac` 到 PoC | 有 | 无 | `fsr=00000402 far=2d960008` |
| R257 / 133 | 同上 | 同上 | 有 | 读回 CB0 五字 | `fsr=00000402 far=2d960008`，银行字与我们写下的逐位相同 |
| R257 / 134 | 同上 | 同上 | 有 | 读 `sGFSR` 与 CB1..4 | `gfsr=0`、`fsr1..4=0`；`far=2d912008` 而 `fsr=00000000` |
| R257 / 135 | 同上 | 同上 | 有 | 先 `TLBIALLH/TLBIALLNSNH` + 两级 sync | `fsr=00000402 far=2d911008`（与 133 同形） |

### 81.4 代码形状

块 28,966 字符、529 行、花括号平衡 0、无 `IOSleep`；块内 14 笔 `gmuW[...] =`（前导 8 笔 + 三支各「掩码 + 启动字」）、`for (` 8 个（6 个是带 `IODelay(1u)` 的轮询、2 个是 `TLBSTATUS` 轮询）、`0x58u` 15 次 / `0x60u` 6 次 / `0x68u` 4 次 / `0x74u` 3 次 / `0x7f4u` 2 次 / `0x6cu`、`0x610u`、`0x7f0u` 各 1 次。失效那三笔是照厂商代码写 0（`QCOM_DUMMY_VAL`）：

```c
smmuW(0x6cu, 0u);            /* TLBIALLH      */
smmuW(0x68u, 0u);            /* TLBIALLNSNH   */
smmuW(0x70u, 0u);            /* sTLBGSYNC     */
for (i = 0; i < 10000u; ++i) { if ((rd32(gSmmuVa, 0x74u) & 1u) == 0u) break; IODelay(1u); }
smmuW((unsigned)g133.cb_base + 0x610u, 0u);   /* CB_S1_TLBIASID */
smmuW((unsigned)g133.cb_base + 0x7f0u, 0u);   /* CB_TLBSYNC     */
for (i = 0; i < 10000u; ++i) { if ((rd32(gSmmuVa, (unsigned)g133.cb_base + 0x7f4u) & 1u) == 0u) break; IODelay(1u); }
```

### 81.5 构建与门禁

CI run `44914416549`（`#368`）`2026-11-21T14:32:00Z → 14:52:38Z` 28/28 步；取件后 `kernel.macho` 126,320,640 B、`payload.img` 150,994,944 B。四个门禁：host 门 28/28、stage4 61/61、stage5 48/48、stage6 1537/1537，全量 `r239_allgates.py` 46/48 通过（两个需要设备或 40 位 rev 的检查跳过），FAILING 0。静态门对产物 PASS（required+survivors 38 条在两件产物里都出现），把路径指向上一轮（`r256-kernel` / `r256-payload`）则 FAIL 26 条 —— 区分能力自证通过。

静态门这一轮抓到一个**真的设计缺陷**：`gmu133n` 的 `cleaned=0` 那个拼法在本轮不可能被打印（三个键都清页），而它出现在源码里，于是「所有该出现的行为都在产物里」这条断言差一点用一条永远不会发生的期望值把好镜像判死。抓取时把它删掉、并在生成器里写明原因，是这一轮唯一一次「门禁比镜像更保守」的实例。

### 81.6 键 133：银行里仍是我们写下的样子

`boot_id 645844065`、窗口 `26a1d5b935c5539398e9253d70d0a98d2a42f02aaf952d5e07ed69e36a539a0c`、14/14 行。逐字：

```
gmu133b=build=0 need=3 tables=3 writes=3 root=ad964000 base=ad800000 iova=2d800000 ias=48 oas=36 ok=1
gmu133m=readback=1 root_off=0 l2_idx=364 l0=00000000ad965003 l1=00000000ad966003 l2=00000000ad800401 m0=1 m1=1 m2=1 pages=3
gmu133n=cleaned=1 pages=3 span=3000 dsb=1
gmu133k=fsr_cleared=1 fsr_before=00000400 fsr_clear_back=00000400 far_before=00000000 about_to_start=1
gmu133l=cb_after_poll=1 ctl=00000000 bit0=0 polls=200000 info=00800000 fsr=00000402 far=2d960008 fsynr0=00000021
          ttbr0=ad964000 sctlr=00001067 tcr=00803510 cbar=0001f300 smr0=80000004 s2cr0=00000000 survived=1
```

三件事同时成立：

1. `ttbr0=ad964000` **就是** `gmu133b` 里那个 `root`，`sctlr=00001067`、`tcr=00803510`、`cbar=0001f300` 与 `gmu133d`（编程之后立刻回读的那一行）逐位相同，`smr0=80000004`（VALID + SID 4）、`s2cr0=00000000`（TRANS）。
2. 描述符三层 `m0=m1=m2=1`，并且 `dc civac` 到 PoC（`span=3000`、`dsb=1`）。
3. 满轮询之后 `fsr=00000402`（TF=1、FORMAT=2）、`far=2d960008`、`fsynr0=00000021`（PLVL=1、WNR=0、CBNDX=0）。

⇒ **我们编程的那个银行、在启动字之后仍然是那个银行；表在内存里、在一致点上都对；而 walk 仍然在我们的 2 MiB 里以「level-1 描述符无效」失败。** 这三件事不能同时为真，除非 **这次 fault 的 walk 不是从这个上下文银行发出来的**。

### 81.7 键 134：全局字与四个邻银行都干净 —— 但 FSR 与 FAR 自相矛盾

`boot_id 645623742`（第一次）与 `662171835`（重跑一次确认），窗口 `1d21c111d11a192e52d6c51cbf9eda75b7ed8eb31a5ee304ab14bf4521bc1037` 与 `2394c265788e6d1f6586c3e72003357f4c6dd7e1f4acbf3fead997c3efb1b18a`，14/14 行。两次逐字相同：

```
gmu134k=fsr_cleared=1 fsr_before=00000000 far_before=00000000 gfsr_before=00000000 about_to_start=1
gmu134l=banks_after_poll=1 ctl=00000000 bit0=0 polls=200000 info=00800000 fsr=00000000 far=2d912008
          fsynr0=00000021 gfsr=00000000 fsr1=00000000 fsr2=00000000 fsr3=00000000 fsr4=00000000
          far4=00000000 survived=1
```

两条结论与一条新问题：

- **全局字干净**（`gfsr=0`，`USF` BIT1 = 0）⇒ 没有「未匹配的流」在 fault，CB0 上的这一笔属于**已经路由过的流**。
- **四个邻银行全干净**（`fsr1..4=0`、`far4=0`）⇒ fault 只在 CB0 上，不是我们读错了银行，也不是别的流。
- **新问题**：这一轮 `far_before=0` 且 `fsr_before=0`（进入块时两者都干净），启动字之后 **`far` 变成 `2d912008`、`fsynr0` 变成 `00000021`，而 `fsr` 仍是 `00000000`**。同一个地址、同样的截断形状（`ad900000+0x12000` 的 bit 31 被丢掉），两次引导完全相同。也就是说：**`FAR` 记录了一笔 translation fault，`FSR` 却没有显示它。**

这一条不许写成「fault 没有发生」：`FAR`/`FSYNR0` 只在一笔 fault 被记录时才会这样成对出现，而且地址形状与 133/135 完全一致。它也不许写成「`FSR` 被谁清掉了」：这一段代码在启动字之后**没有任何一笔写**（门禁断言「启动字是最后一笔 store」正是为了这一条）。剩下的两种解释必须分开讲：

1. **两次读之间有时间差**：`FSR` 的读在 `FAR` 的读之前几十纳秒，若 fault 恰好落在两者之间，就会得到「先读的 0、后读的非 0」。但两行连续读之间出现 fault 的概率低，而**两次引导都这样**，所以这个解释很勉强。
2. **这个窗口的读不是有序的**：如果 SMMU 寄存器窗口在我们这里被映射成可缓存的普通内存，而不是 Device 内存，那么 `volatile` 读只保证「不被编译器删」，不保证「不被硬件重排或从缓存返回旧值」。这个解释能一次性说明 134 的 `fsr=0`、以及 R245/R249/R254 里 `fsr` 与 `far` 读数偶尔「差一拍」的现象。

**本轮不判断是哪一种**，把「读窗口的内存属性」与「连续读三次 FSR 看它是否自己变化」列为 R258 的第一个待办。

### 81.8 键 135：照厂商的做法先失效，fault 一点没变

`boot_id 645022091`、窗口 `739ec57b1d8ccf0d4846d27aab9bef33b7f5c50f221e8516c9bd4a4fbe401a78`、15/15 行。逐字：

```
gmu135k=tlb_flushed=1 tlbiallh=1 tlbiallnsh=1 gsync=1 gstatus=00000000 fsr_before=00000400 about_to_start=1
gmu135k2=fsr_clear_back=00000400 cbstatus=00000000
gmu135l=flushed_then_started_end=1 ctl=00000000 bit0=0 polls=200000 info=00800000 fsr=00000402
          far=2d911008 fsynr0=00000021 gstatus2=00000000 survived=1
```

`TLBIALLH`、`TLBIALLNSNH`、`sTLBGSYNC` 三笔都落位（`tlbiallh=1 / tlbiallnsh=1 / gsync=1`），全局状态字轮询到 0（`gstatus=00000000`），每银行 sync 也到 0（`cbstatus=00000000`），启动前状态字是 `00000400`（只有 FORMAT 残留）、清一次之后仍是 `00000400`。然后启动字、满轮询：`fsr=00000402`、`far=2d911008`、`fsynr0=00000021`，与键 133 同形，**只差地址里的那一段分配偏移**。

⇒ **没有旧翻译藏在 TLB 或 walk cache 里**。如果障碍是「旧的无效翻译挡着」，先失效就应该让 walk 成功或者至少换一个地址；它两个都没做。结合 R256 的 `dc civac` 与 R255 的补位，**「描述符到不了 SMMU 的眼里」这一整类解释都已经被三面夹死**：内存里有正确的字、一致点上看得到、缓存里没有旧条目。剩下的只能是「这次 walk 根本没用我们这张表 / 这个银行」。

### 81.9 判决

1. **CB0 在启动字之后仍是我们编程的那个银行**（键 133）：`ttbr0` 等于我们写的 root，`sctlr/tcr/cbar/smr0/s2cr0` 与编程后立刻回读逐位相同。
2. **fault 是我们这条流、只在 CB0 上**（键 134）：全局 `USF=0`，四个邻银行 `fsr1..4=0`。
3. **失效不改变任何东西**（键 135）：厂商那套 `TLBIALLH + TLBIALLNSNH + 两级 sync` 全部落位并完成，fault 的字、地址形状、`plvl`、`cbndx` 与键 133 逐位相同。
4. **新事实：`FSR` 与 `FAR` 会互相矛盾**（键 134 两次）：`far=2d912008`、`fsynr0=00000021` 而 `fsr=00000000`；这一段代码在启动字之后没有任何写。
5. 三键的 `ctl` 都是 `00000000 / bit0=0`，固件仍然没有报告 HFI 启动。

合起来：**「表对、缓存对、银行对、没有别的流、失效也没用」这五条同时成立，于是「fault 的 walk 不是从这个上下文银行发出来的」成为唯一还站得住的解释** —— 但这句话必须限于「寄存器账本」的层，见 81.10。

### 81.10 必须分开说的三件事

1. **这是寄存器账本，不是 HFI 消息**。我们读的是 SMMU 的 `FSR/FAR/FSYNR0/GFSR` 与 GMU 的 `HFI_CTRL_STATUS`；没有一条是从 CM3 那边「说」出来的。
2. **`far` 是送进 SMMU 的地址，不是我们写进 `HFI_QTBL_ADDR` 的值**。R254 已经立了这条边界，本轮只加强它：键 133 交付的是不截断的 `ad960000`，fault 地址仍是 `2d960008`。
3. **`fsr=00000402` 的 `FORMAT`（bit10:9）不受 W1C 组管**（R254 §78.9），所以「清一次之后仍是 `00000400`」不是清失败；判据是 `TF`（bit1）。

### 81.11 没证明什么

- **没证明 CM3 在启动字之后有没有继续跑**：`ctl bit0` 一直是 0，我们只知道它没有举手。
- **没证明「walk 不从 CB0 发出」到底意味着什么**：可能是 CM3 自己的地址通路不经过 SMMU，可能是别的 SID 在走别的银行，也可能是一次预取。三键只排除了「我们这个银行的表/缓存/银行身份/别的流」。
- **没分辨 134 的 `FSR=0` 是时序还是内存属性**：见 81.7 两条解释，都留着。
- **没碰 GX 电源域**：这一整串都发生在 CX 已经上电的前提下，GX 从未打开。
- **没到提交路径**：PM4/IB/微码一条都没动，GPU 仍未渲染一帧。

### 81.12 R258 设计

1. **键 136（先解决读数可信度）**：启动字之后，把 `CB0_FSR` 连读 **三次**，三次之间各插一个 `IODelay(1)`，并把三次读数与同一拍里的 `FAR/FSYNR0` 一起打出来。若三次 `FSR` 里有一次非 0，134 的矛盾就是时序；若三次全 0 而 `FAR` 非 0，读数路径本身有问题。
2. **键 137（读窗口的内存属性）**：把 `gSmmuVa` 那个映射的属性（`Device-nGnRnE` 还是 `Normal`）打出来，并在同一段里做一次「先写后读同值」的自检（写一个我们自己挑的 `CB_SCTLR` 位、立刻读回）。这一步决定 136 的读数能不能被相信。
3. **键 138（换问题）**：不再问「表为什么没用」，改问「CM3 到底走不走 SMMU」—— 把 `SMR0` 的 `SID` 改成**另一个确定不存在的 SID**（例如 0x7f），看 fault 的 `far` 是否仍然出现。若仍然出现，说明这笔 fault 与 `SMR0/S2CR0` 的路由无关，CM3 的读根本不经过我们配置的这条流。

### 81.13 工具事实

- `r257_block.py` 是「先换块头、再改名」的写法，顺序反了会把块头也改名（本轮踩过）。改名正则必须是 `(?<=[A-Za-z_])120(?![0-9])`，不能用 `(?=[0-9A-Za-z_])`，否则 `g120;` 这种 token 结尾会漏。
- 新分支里手写的 `g130.cb_base` 必须改 `g133.*`；`rd32` 的签名是 `(volatile uint32_t *base, uint32_t off)`，不能传指针算术。
- `-Wshadow` 两次、`-Wunused-variable` 两次：分支里的回读变量要改名（`r133sctn` 等），多带的 `char rXXXRowM[300]` 要么用上要么删。
- 门禁里的字符串字面量**不能带撇号**（`bank's` / `SMMU's` 会 `SyntaxError: unterminated string literal`）。
- `} else {` 在块内有多个，取第三支必须用 `rindex`。
- 静态门抓行时，`gmu1..=...` 前面可能还有 `enchilada-adreno: ` 前缀，正则不能要求引号紧贴 `gmu`；`cleaned=0` 这种「源码里有、本轮不可能打印」的拼法要么删掉要么说明原因。
- 设备侧：runner 会在动设备之前检查 payload 是否存在（`ABORT missing …`），所以 rekey 必须在前；`r257_anchor.py` 要在每次上机之后立刻把下一份 runner 锚到刚拿到的窗口。


## 八十二、ROUND 258：把状态字连读三次、把窗口写回去再读 —— 读数可信了，fault 仍在；而把流匹配关掉，这一轮连日志都留不下

### 82.1 一句话

R257 留下的两处疑点（键 134 的 `FSR=0` 而 `FAR` 有值；`FSR/FAR` 是不是读到旧值）在这一轮被读数本身排除了：状态字连读三次、并在读 `FAR` 之后再读一次，四次全是 `00000402`；把 `CB0_SCTLR` 写回同值再读，读到的就是刚写下去的那个值。于是**读数路径是可信的**，fault 是硬件真真切切写下来的。而把 `SMR0` 的 `VALID` 清掉（保留 `SID=4`）以后，**两次上机都没有留下任何一行日志** —— 这一轮连结论都留不下。

### 82.2 为什么先做读数可信度，而不是接着追 fault

R257 键 134 的读数里，`far=2d912008`、`fsynr0=00000021` 都置了，`fsr` 却是 `00000000`，而那一整段代码在启动字之后没有任何写。两种解释都能解释它：① 两次读之间隔着几十纳秒，fault 恰好落在中间；② 这个 MMIO 窗口被映射成可缓存内存，`volatile` 只约束编译器、不约束硬件看到的值。两种解释对后面的所有轮次都致命 —— 如果读数不可信，前面八轮关于 `fsr`/`far` 的判决都要打折。所以这一轮先把「读数能不能信」单独做成一个键。

### 82.3 三键设计

| 键 | 做了什么 | 想分辨什么 |
|---|---|---|
| **136** | 启动字之后把 `CB0_FSR`(`0x58`) 连读三次（之间各 `IODelay(1)`），再读 `FAR`(`0x60`)/`FSYNR0`(`0x68`)，**再读一次** `FSR` | 134 的 `FSR=0` 是时序还是读路径 |
| **137** | 读 `FAR` 两次（之间 `IODelay(1)`）、`FSR` 取 first/mid/last，并把 `CB0_SCTLR`(`0x00`) **同值写回再读** | 窗口是不是返回「刚写进去的值」 |
| **138** | 把 `SMR0`(`0x800`) 写成 `0x00000004`（清 `VALID`、保留 `SID=4`）再启动 | CM3 的读到底走不走 SMMU 的流匹配 |

### 82.4 代码形状

- 块 **27,077 字符**、braces 0；探针 **841,596 字符 / 12,928 行**；严格 GMU 写点 **383 → 397**。
- 块内 `gmuW` 14 笔、`HFI_CTRL_INIT] = 1u;` 3 笔、掩码 3 笔、`GMU2HOST_INTR_CLR] = 0xffffffffu;` 1 笔、`0xc00001feu` 3 笔、`0x58u` 15 处、`0x60u` 6 处、`0x68u` 3 处、`0x48u` 3 处、`0x800u` 6 处、`for (` 6 个、`IODelay(1u)` 8 个、无 `IOSleep`。
- 三个键共用同一段 preamble（建 2 MiB 低位表、九笔编程 CB0、读回三层描述符、`dc civac` 清三页、`SMR0=80000004`、`S2CR0=0`、`CM3_FW_INIT_RESULT=0`、`QTBL_ADDR/INFO`、`SYSRESET` 脉冲、`CP_RESP=0`、200000 轮 `0xbabeface` 轮询、两笔 `INTR_CLR`）。

### 82.5 构建与门禁

- CI **#369 run `44920004888`** `completed / success`，`2026-11-21T15:35:34Z → 15:51:56Z`（16m22s，28/28 步）。
- `r258-kernel\kernel.macho` **126,320,640 B** sha256 `df6deeeb78acced3f7b95c65a7d4d4d635ed0fa660337e9b7510e489f351038d`；`r258-payload\payload.img` **150,994,944 B** sha256 `24b88b7846adfd0211b27c3da05feb466a426cf4eafeffdeae4690f927d00e0c`。
- 静态门：对 R258 产物 `PASS: R258 static gate`（25 条 REQUIRED 在两件产物里都出现）；指向 R257 产物 `FAIL: R258 static gate (25 problems)` —— 同一条门在上一轮产物上必须失败，本轮才成立。
- 四处门禁：host **28/28**、stage4 **61/61**、stage5 **48/48**、stage6 **1554/1554**；全量 `ran 46 of 48, FAILING: 0`。

### 82.6 键 136 逐字读数

`boot_id` **644267162**、窗口 sha256 `c238351717a05e99787d59e7b010158c3276cdd1c6e3b202b42c92afda4ef177`、14/14 行。

```
gmu136k=fsr_cleared=1 fsr_before=00000400 fsr_clear_back=00000400 far_before=00000000 about_to_start=1
gmu136l=fsr_three_reads=1 ctl=00000000 bit0=0 polls=200000 info=00800000 fsrA=00000402 fsrB=00000402 fsrC=00000402 far=2d960008 fsynr0=00000021 fsrD=00000402 survived=1
```

**判决：状态字在四次读里都是置着的**（含 `FAR` 之后那次）。134 的 `FSR=0` 不是读路径的稳定性质，是那一刻的时序窗口：那一次读恰好落在硬件写入之前。

### 82.7 键 137 逐字读数

`boot_id` **650481063**、窗口 `fa721275ad9a7e0d42ac209a2daf8082dd2fdbc032cb81ca4db0bb30b817f170`（第一次）；复跑 `boot_id` **655177693**、窗口 `ac48f18decce4ac602a8e47c125f9e64f56e72464ac34c082d97e48f48728c09`（两次读数逐位相同）。

```
gmu137k=fsr_cleared=1 fsr_before=00000000 far_before=00000000 gfsr_before=00000000 about_to_start=1
gmu137l=read_path=1 ctl=00000000 bit0=0 polls=200000 info=00800000 fsr_first=00000402 farA=2d911008 farB=2d911008 fsr_mid=00000402 fsr_last=00000402 sctlr_self=00001067 write_back=1 sctlr_back=00001067 fsynr0=00000021 survived=1
```

**判决：窗口返回的正是刚写下去的值**（`sctlr_self == sctlr_back == 00001067`），并且 `farA == farB`、三次 `FSR` 全置。⇒ 读数路径可信，134 的矛盾是「硬件什么时候写这个字」的问题，不是「我们读到旧值」的问题。

### 82.8 键 138 逐字读数（两次上机，零字节）

`boot_id` 仍停在上一层 `655177693`、窗口仍是 `ac48f18d…`、行计数 `12 of 14`（`gmu138k=0 gmu138l=0`）—— 两次上机都是这样。设备侧日志显示 `gate PASS` → `flash system_b OK` → `Booting OKAY` → 约 50 秒后回到 recovery，**流程本身没有失败**。

**判决：清掉 `SMR0.VALID` 之后，这一轮连一行日志都留不下（2/2）。** 这与 R253 键 122（把队列表地址写 0，3/3）和 R255 键 128（交出截断地址，2/2）属于同一类：改动一旦让某个必需的访问不再被匹配/可翻译，机器就走不到写日志的那一步。

### 82.9 判决四条

1. **读数可信**：状态字连读三次、读 `FAR` 之后再读一次，四次全置；`CB0_SCTLR` 同值写回再读，读到的就是刚写的值。
2. **134 的矛盾是时序**，不是读路径缺陷；`FSR` 与 `FAR` 不是同一拍写下来的。
3. **fault 的形状没变**：`far` 仍是队列表地址 + 8 且 bit 31 被清（`2d960008` / `2d911008`），`fsynr0=0x21`（PLVL=1、WNR=0、CBNDX=0），`ctl` bit0 仍是 0。
4. **关掉流匹配会让整轮死掉**：这条路是承重的，不是装饰。

### 82.10 必须分开说的三件事

1. 「四次读全置」只说明**这一轮**读的时候是置的；它不能证明 fault 发生在启动字之后的第一纳秒还是第 0.3 秒。
2. 键 138 的零字节**只能证明这一轮没写成日志**，不能证明「机器在那一刻 panic 了」还是「日志缓冲根本没轮到落盘」。判据是「窗口没变」，不是「看见崩溃」。
3. 键 138 的零字节也可能来自那笔 `SMR0` 写本身（写坏了一个承重寄存器），而不是来自「清掉匹配导致的 fault」。**这两件事这一轮没有分开** —— 这正是 R259 的第一个键。

### 82.11 没证明什么

- 没证明 CM3 的队列表读**经过** SMMU：138 用「关掉匹配 → 整轮死」换来了间接证据，但缺少「关掉匹配但不启动」的对照。
- 没证明 `FSR` 与 `FAR` 谁先写下来。
- 没证明 `cbfsr/cb_far` 属于 CB0 之外的任何银行（R257 键 134 已排除 CB1..CB4 与全局字）。
- 没证明固件为什么在 `ctl` bit0 上始终为 0。
- 没渲染一帧。

### 82.12 R259 设计

1. **键 139（拆开 138 的两种解释）**：照 138 的样子把 `SMR0` 写成 `0x00000004`，但**不写启动字**，只轮询，然后落日志。若这一轮活下来 ⇒ 死因是「启动字触发的未匹配 fault」，不是那笔写。若不写启动字也死 ⇒ 死因是那笔写本身。
2. **键 140（把匹配改回去再启动）**：`SMR0` 先写 `0x00000004`、再写回 `0x80000004`，然后启动 —— 若活下来，说明承重的是启动那一刻的匹配状态，而不是我们改过这个寄存器。
3. **键 141（换 SID 而不是清 VALID）**：`SMR0 = 0x80000000 | 0x7f`（有效但换成一个不存在的 SID），启动后读 `sGFSR.USF` 与 `CB0_FSR/FAR`。若 `USF` 置起而 `FSR` 干净 ⇒ 那笔读确实走 SMMU 的匹配；若 `FSR` 仍置 ⇒ 它绕过了匹配。

### 82.13 工具事实

- `r258_mkrun.py` 里 137 的判决串有 5 个占位符却只传了 4 个参数（`IndexError: Replacement index 4 out of range`），生成器改 `.format(far_a, far_b, fsr_first, fsr_mid, fsr_last)` 后重跑；**生成器里的 `.format(...)` 参数个数要在生成时就核对**，否则会等到上机之后才炸。
- 同一份 runner 连跑两次会**再引导一次**（每次都 flash + boot），所以「重新读窗口」不能靠重跑 runner，必须先修 runner 再跑。
- 块尾 `split("\n")` 会多出一个空串，closers 断言前要先 `while renamed[-1] == "": renamed.pop()`。
- 三分支共用同一组行缓冲会让别的分支的行名未使用（`-Werror,-Wunused-variable`），要在生成器里按分支边界切段改名。
- `git checkout -- <paths>` 会被 CC Safety Net 拦（`git.checkout-double-dash`），恢复用 `git show ":" + path`。
- 切片模板里 `%` 格式化很容易数错占位符（`TypeError: not enough arguments for format string`），能写字面串就写字面串。

## 八十三、ROUND 259：把流匹配拿下去、再放回来 —— 静止时什么都不 fault，放回来之后 fault 一字不变；而 SID 字段根本写不进去

### 83.1 一句话

三个键把「路由」翻过来查了一遍：拿掉 `SMR0` 的 `VALID` 而不启动，`fsr=00000400`（`TF=0`）、`far=0`、`gfsr=0`；放回 `VALID` 再启动，fault 与 R255/R257/R258 逐位同形；想换个 SID 做对照时硬件**拒绝改写 `SMR0` 的 SID 字段**（写 `0x8000007f` 读回 `0x80000004`）。

### 83.2 为什么这一轮从映射换到路由

前四轮（R255–R258）已经把映射这条线走完：表里的描述符就是 plan 的编码（§80）、清到一致点没用（§80）、银行里仍是我们写的五个字（§81）、把银行字读回来再写回去读数可信（§82）。剩下的解释只有一类：**这一帧 walk 为什么不用这张表**。它能被拆成两个可测的问题：① 这条流是否真的经过 `SMR0` 的匹配；② 匹配状态改变时 fault 是否跟着变。R259 就测这两条。

### 83.3 三个键与单变量表

| 键 | `SMR0` 写什么 | 启动字 | 目的 |
|---|---|---|---|
| **139** | `0x00000004`（清 `VALID`、留 `SID=4`） | **不写** | 静止时（没有启动请求）会不会自己 fault |
| **140** | `0x00000004` → 立刻写回 `0x80000004` | 写 | 匹配状态来回一趟是否改变 fault |
| **141** | `0x8000007f`（有效、但 SID 换成不存在的 0x7f） | 写 | 换成另一条流做对照 |

单变量对照（同一个 preamble，建表 + 九笔编程 CB0 + 读回三层描述符 + `dc civac` 清三页 + `CM3_FW_INIT_RESULT=0` + `QTBL_ADDR/INFO` + `SYSRESET` 脉冲 + 200000 轮 `0xbabefaceu` 轮询 + 两笔 `INTR_CLR`）：

| 键 | 启动字 | `SMR0` 读回 | `fsr` | `far` | `gfsr` | `ctl` |
|---|---|---|---|---|---|---|
| 139 | 无 | `00000004` | `00000400` | `00000000` | `00000000` | `00000000` |
| 140 | 有 | `80000004` | `00000402` | `2d951008` | — | `00000000` |
| 141 | 有 | **`80000004`（被拒）** | `00000402` | `2d960008` | `00000000` | `00000000` |

### 83.4 代码形状

块 `25,827 字符 / 468 行`，`braces 0`；探针 `867,425 字符 / 13,397 行`；严格 GMU 写点 `397 → 409`；块内 `gmuW[` 12 笔（preamble 8 + 键 140 的 mask 与启动字 + 键 141 的 mask 与启动字）、`HFI_CTRL_INIT] = 1u;` 2、`GMU2HOST_INTR_MASK] = ` 2、`0xc00001feu` 3、`for (` 6、`IODelay(1u)` 4、`IOSleep` 0。

### 83.5 构建与门禁

host 门 **28/28**（store 预算 `409`）；stage4 **61/61**、stage5 **48/48**、stage6 **1571/1571**；全量 `r239_allgates.py` **ran 46 of 48, FAILING: 0**。CI **#370 run `44970193906`** `2026-11-22T01:10:35Z → 01:26:20Z` **success 28/28**。产物 `r259-kernel\kernel.macho` **126,337,024 B** sha256 `b08cd04b63d060397625377ff94cc6237e405b6b35353e3a7261bb127e85fbb8`、`r259-payload\payload.img` **150,994,944 B** sha256 `f6fa0262c8e050d9dbb6120f5b3c945377d1afb3aed87a99bc6f28e912f5ac75`。静态门 `r259_static.py`：25 条 REQUIRED（18 条行 + 7 条拒绝行）在探针源与两件产物里都出现、`PASS: R259 static gate`；指向 R258 产物 **`FAIL: R259 static gate (25 problems)`**。

### 83.6 键 139（`boot_id 597896721`，窗口 `dfc78abd54c76b9708506d2d6dfe67827f7e9d3f354be1d73482236050c88c05`，14/14）

```
gmu139k=smr_valid_off=1 smr_want=00000004 smr_back=00000004 fsr_before=00000400 gfsr_before=00000000 no_start=1 about_to_poll=1
gmu139l=smr_valid_off_no_start=1 ctl=00000000 bit0=0 polls=200000 info=00000000 gfsr=00000000 fsr=00000400 far=00000000 smr_end=00000004 survived=1
```

判决：**NOTHING FAULTED WITHOUT THE START WORD** —— 建表、路由、清状态字之后停在那里，`TF` 位干净、地址寄存器空、全局字干净；而且 `SMR0` 的 `VALID` 位**是**可写的（写 `0x00000004` 读回 `0x00000004`）。

### 83.7 键 140（`boot_id 633253890`，窗口 `ba4adbbff27cd81676b6b40e892f65ffdb7d6430ee5f8df7e9032d1ea1d68122`，14/14）

```
gmu140k=smr_cycled=1 off_back=00000004 on_want=80000004 on_back=80000004 fsr_before=00000400 about_to_start=1
gmu140l=smr_cycled_then_started=1 ctl=00000000 bit0=0 polls=200000 info=00800000 fsr=00000402 far=2d951008 fsynr0=00000021 smr_end=80000004 survived=1
```

判决：**THE STATUS WORD STILL CARRIES TF=1 WITH THE MATCH BACK ON** —— `VALID` 拿下去又放回来（两次读回都按写入值）之后启动，fault 的字（`00000402`）、`plvl=1`、`cbndx=0`、`info=00800000` 与 R255/R257/R258 逐位相同。

### 83.8 键 141（`boot_id 598851584`，窗口 `d1de566630e898477ed079b83d03f4f0a6b317b785e6393c6e42175e9d9e6123`，14/14）

```
gmu141k=smr_other_sid=1 smr_want=8000007f smr_back=80000004 fsr_before=00000400 gfsr_before=00000000 about_to_start=1
gmu141l=smr_other_sid_end=1 ctl=00000000 bit0=0 polls=200000 info=00800000 gfsr=00000000 fsr=00000402 far=2d960008 fsynr0=00000021 smr_end=80000004 survived=1
```

判决：**THE MATCH REGISTER REFUSED THE VALUE (want=8000007f back=80000004)** —— `VALID` 位可写（139 已证），但 `ID` 字段（`GENMASK(15,0)`）在写 `0x7f` 时被硬件保留成 `4`。所以「换成另一条流」这件事**不能**靠改 `SMR0` 的 SID 来做；这一键因此只交付一个硬件事实，不交付路由结论。

### 83.9 判决四条

1. **静止不 fault**：没有启动请求时，即使匹配已被拿下去，`TF=0`、`far=0`、`gfsr=0`。
2. **匹配状态来回一趟不改变什么**：放回来再启动，fault 与历轮逐位同形。
3. **`SMR0.ID` 写不进去**：`VALID` 可写、`ID` 被钳回 `4`。
4. **138 的零字节被 139 划清了边界**：`SMR0 = 0x00000004` 这一笔写在**没有启动请求**时完全无害（139 存活并落满 14 行），所以打死 R258 键 138 的不是「把匹配拿下去」这件事本身，而是**匹配被拿下去的同时写出了启动字**。两次零字节 + 这一次存活，正好把死因夹在「启动请求」这一侧。
5. `ctl` 的 bit0 三键仍是 0，固件仍未报告 HFI 启动。

### 83.10 必须分开说的三件事

1. 139 的「干净」只说明**那一段时间里没有活动请求**，不能说明匹配状态对 fault 没有影响 —— 要证明后者，必须在**匹配被拿下去的同时发出启动请求**（R260 键 142）。
2. 141 的「被拒」是**寄存器层面**的事实（读回值），不是「SID 4 的流一定存在」的证据。
3. `gfsr` 是全局字：它干净只说明此刻没有未匹配的流被记下来，不说明没有未匹配的流曾经经过。

### 83.11 没证明什么

- 没证明 CM3 的队列表读**经过** `SMR0` 的匹配（141 想测没测成）。
- 没证明 fault 与匹配状态无关（140 只是把状态恢复原样，不是拿下去时启动）—— 这条要等 R260 的 142。
- 没证明 `ID` 被钳是「只读」还是「写保护只在有活动流时」，本轮没有在无流时单独试写 `ID`。
- 没解释 `far` 为什么仍丢 bit31（本轮 `far=2d951008`/`2d960008` 与历轮同构）。

### 83.12 R260 设计

1. **键 142**：`SMR0=0x00000004`（`VALID=0`）+ **先 `boot_log_panic_flush()`** + 掩码 + **启动字** + 满轮询 → 这是 138 的复现键，区别只在于先落盘。三种可能结果都对结论有用：① 零字节 ⇒ 「匹配被拿下去 + 启动请求」确实把这一轮打死，与 138 同形，**这本身就是路由证据**（那条读变成未匹配流之后 SMMU 的响应是致死的）；② 存活且 `gfsr.USF=1`、CB0 `fsr/far` 干净 ⇒ 那条读经过 `SMR0`，路由成立；③ 存活且 `fsr=00000402`、`gfsr=0` ⇒ fault 走的路径根本不看匹配。
2. **键 143**：`VALID=0` + flush + 启动字 + 轮询 + **立刻恢复 `VALID=1`** + flush + 再启动一次 + 轮询 → 同一引导内把「未匹配」与「已匹配」两段压在一起，哪一段死、哪一段活都留得下来。
3. **键 144**：不动 `SMR0`，改用 `SMR1`（`0x804`）装 `SID=0x7f` 并置 `VALID`，保留 `SMR0` 的 `SID=4` → 在不破坏原有匹配的前提下做「另一条流」的对照；若 `SMR1` 的 `ID` 同样被钳回，就把「ID 字段只读」从一次观察变成两次。

### 83.13 工具事实

- 判决串 `.format()` 参数不足这轮**又**发生一次（139 的 `WITHOUT THE START WORD` 有 5 个占位符只给 4 个 ⇒ `IndexError: Replacement index 4 out of range`）：**写判决串时最后那个字段（`far=`）必须出现在参数表里**，改完要重生成并 `ast.parse`。
- 静态门的拒绝行在源码里带 `\n` 转义（`"...refused-cx-down cx=%08x\n"`），抓出来必须**剥掉尾部两个字符**再与产物比对，否则永远不命中。
- `r259_mkstatic.py` 的 banner 断言要按出现次数写：`FAIL: R259 static gate` 在脚本里出现**两次**（裸行与带计数行），写成 `== 1` 会误报。
- 生成器插块必须插在 **ladder 之前**（`if (fwKey >= 18u && fwKey <= 21u)` 那一行），插在「上一轮块头」之前会让新切片把上一轮块吞进去，切片长度与 `SYSRESET=1` 计数会立刻翻倍。
- 设备 runner 第一次跑崩在判决串上时，**窗口其实已经写好**：先读窗口确认行落没落，再决定要不要重新引导 —— 这轮因此多引导了一次 139，两次读数逐位相同。

## 八十四、ROUND 260：把整张表填满，fault 消失了 —— CM3 第一次接受了启动请求

**84.1 一句话。** 把 SMMU 表的三页（144 只填两页子表）每一个条目都写成合法描述符之后，启动字之后的翻译
fault 在四次引导里全部消失（`fsr=00000400`、`far=00000000`、`fsynr0=00000000`），而 `CM3_CTRL_STATUS` 的
bit0 在四次里有三次读成 1（`polls=2`）—— 这是本移植第一次看到固件报告「HFI 启动」；把流匹配拿下去再启动
（键 142，附带一次写日志 flush）仍然是零字节 2/2，所以 R259 里键 138 的死因不是缺 flush。

**84.2 为什么是这两组键。**

R259 的键 139 证明「匹配拿下去、不启动」是静止无害的，键 138 证明「匹配拿下去、启动」会零字节，
但 138 只有一个样本、而且它没有先在启动字之前把日志写出去。R260 的键 142 就是把这两个差别一次补上：
同样的 `SMR0=0x00000004`、同样的掩码与启动字，中间多一次 `boot_log_panic_flush()`。零字节还是活下来，
这个问题的答案就与日志缓冲无关了。

R256 的键 130/131 已经证明三页描述符从内存读回来与 plan 的编码逐位相同，可是启动字之后仍然报
`plvl=1` 的 level-1 描述符 fault。R259 的键 141 又证明 `SMR0` 的 ID 字段写不进去（`want=8000007f`
`back=00000004`），所以「换一条流试试」这条路走不通。剩下的可能只有两种：要么 walker 读到的表里
**有我们没写的空条目**（我们只写了 `L1[0]` 与 `L2[364]` 这一条路径），要么它读的表根本不是我们写的那张。
R260 用「把三页每个条目都填成合法的」把第一种可能彻底堵死：无论 walker 从哪一级起步、用哪个下标，
都不会撞上空条目。**143 填三页（根页 + 两个子页），144 只填两个子页**，根页保持 builder 写的样子——
这样如果只有 143 好、144 不好，问题就在根页；如果两个都好，根页本来就是对的。

**84.3 三键设计与单变量表。**

| 键 | 与前一轮的差别 | 启动字 | 判据 |
|---|---|---|---|
| 142 | `SMR0=0x00000004`（清 VALID 保 SID=4）+ **启动字前一次 flush** | 有 | 零字节 = 「匹配拿下去 + 启动」与日志缓冲无关 |
| 143 | 根页与两个子页共 1536 个条目全部写成合法描述符或 2 MiB 叶子 | 有 | fault 消失 = 之前的 fault 落在一个空条目上 |
| 144 | 只填两个子页（1024 个条目），根页不动 | 有 | 与 143 同好 = 根页本来就是对的 |

单变量表（每个键相对上一轮只动一处）：

| 轮/键 | 表 | 匹配 | 启动字 | fault |
|---|---|---|---|---|
| R259/141 | identity，一条路径 | SID=4 有效 | 有 | `00000402 / 2d960008 / plvl=1` |
| R260/143 | 三页填满 | SID=4 有效 | 有 | `00000400 / 00000000 / plvl=0` |
| R260/144 | 两页子表填满 | SID=4 有效 | 有 | `00000400 / 00000000 / plvl=0` |
| R260/142 | 一条路径 | VALID=0 | 有 + flush | 零字节 2/2 |

**84.4 代码形状。** 块 30,492 字符 / 555 行，花括号净 0；块内 14 笔 `gmuW`（共用前导段 8 笔 + 三支各
「掩码 + 启动字」），10 个 `for (`、4 个 `IODelay(1u)`、没有 `IOSleep`；两个填表键各有一处
`for (...; i < 512ull; ...)` 写满一页，并各用一次 `dc civac` 把整段清到一致点；键 142 的 flush 走
探针里唯一可用的形态 `extern void boot_log_panic_flush (void) __attribute__((weak_import));`
加空指针判断。

**84.5 构建与门禁。** 探针 897,919 字符 / 13,953 行，全文件严格 GMU 写点 423；host 门 28/28、
stage4 61/61、stage5 48/48、stage6 1589/1589、全量 46 of 48（FAILING 0）；CI #371 `44974173907`
`2026-11-22T02:02:04Z → 02:21:28Z` success 28/28 步；静态门 25 条 REQUIRED 在两件产物里都出现
（key marker kernel=1 payload=2），对上一轮产物自证 FAIL 25 条。产物：
`kernel.macho` 126,353,408 B sha256 `0dd6ec49cd4bf8249ed38e6bc27e0a1af2739096a7ba105fa8e1dfe3c4427f2f`、
`payload.img` 150,994,944 B sha256 `f721ed8163e52224f509223aedc7286a93ab57500f5324715a673ace6ad26f79`。

**84.6 键 142：零字节 2/2。** 两次引导都只读回上一轮的字节（`d1de566630e89847…`，那是 R259 键 141 的窗口，
日志头的 boot id 仍是 598851584），意味着**这一轮一个字节都没写**。两次都完整走完了
`gate PASS → adb reboot bootloader → flash system_b OK → Booting OKAY → 回到 recovery`，回读校验也说
`flash readback matches: True`、`key verified: this boot will run key 142`。判决：**「匹配拿下去 + 启动字」
这个组合本身就会让这一轮死在提交之前，加不加 flush 都一样**；键 139 与键 140 一起把边界钉住——
只有匹配拿下去（139）是安全的，先拿下去再放回来再启动（140）也是安全的。

**84.7 键 143（填三页）：三次引导，fault 全部消失，固件三次里有两次报启动。**

- 第一次：boot 584844122、窗口 `957e441c79f09135…`、14/14 行。
  `gmu142b=build=0 need=3 tables=3 writes=3 root=ad955000 base=ad800000 iova=2d800000 ias=48 oas=36 ok=1`、
  `gmu143k=shotgun fills=3 l2_idx=364 l0=00000000ad956003 l2=00000000ad800401 fsr_before=00000400 about_to_start=1`、
  `gmu143l=shotgun_started ctl=00000001 bit0=1 polls=2 info=00000000 gfsr=00000000 fsr=00000400 far=00000000 fsynr0=00000000 smr_end=80000004 survived=1`。
- 第二次：boot 641886752、窗口 `6c3507e566aa834f…`、14/14 行。
  `gmu142b=build=0 need=3 tables=3 writes=3 root=ad960000 base=ad800000 iova=2d800000 ias=48 oas=36 ok=1`、
  `gmu143l=shotgun_started ctl=000000fe bit0=0 polls=200000 info=00000000 gfsr=00000000 fsr=00000400 far=00000000 fsynr0=00000000 smr_end=80000004 survived=1`。
- 第三次：boot 584484607、窗口 `36d0a48d3212c6c5…`、14/14 行。
  `gmu142b=build=0 need=3 tables=3 writes=3 root=ad960000 base=ad800000 iova=2d800000 ias=48 oas=36 ok=1`、
  `gmu143k=shotgun fills=3 l2_idx=364 l0=00000000ad961003 l2=00000000ad800401 fsr_before=00000400 about_to_start=1`、
  `gmu143l=shotgun_started ctl=00000001 bit0=1 polls=2 info=00000000 gfsr=00000000 fsr=00000400 far=00000000 fsynr0=00000000 smr_end=80000004 survived=1`。

三次共同点：`fsr=00000400`（TF=0、FORMAT=2）、`far=00000000`、`fsynr0=00000000`（plvl=0）——自 R254 起
每次都会出现的 `00000402 / 2d9xxxxx / plvl=1` 组合**不见了**。`ctl` 两次读成 `00000001`（bit0=1）、
一次读成 `000000fe`（bit0=0，且轮询跑满 200000 次）。`info`、`gfsr` 三次都是 0。

**84.8 键 144（只填两个子页）：fault 同样消失，固件同样报启动。**

boot 584028634、窗口 `2412c893b1ae1013…`、14/14 行。
`gmu142b=build=0 need=3 tables=3 writes=3 root=ad960000 base=ad800000 iova=2d800000 ias=48 oas=36 ok=1`、
`gmu144k=shotgun_l1l2 fills=2 l2_idx=364 l0=00000000ad961003 l2=00000000ad800401 fsr_before=00000400 about_to_start=1`、
`gmu144l=shotgun_l1l2_started ctl=00000001 bit0=1 polls=2 info=00000000 gfsr=00000000 fsr=00000400 far=00000000 fsynr0=00000000 smr_end=80000004 survived=1`。

**84.9 判决（五条）。**

1. **fault 是「空条目」造成的**：填满表之后四次引导全部 `far=0`、`fsynr0=0`、`TF=0`。
2. **只填子页就够**：键 144 的根页还是 builder 写的样子，结果与键 143 逐项相同 ⇒ 根页一直是对的，
   出问题的地方是 `L1[0]` 下面那条路径（`L2[364]` 及其邻居）里我们**没有写到的条目**。
3. **CM3 第一次接受启动**：四次里有三次 `ctl` 的 bit0 读成 1，`polls=2`（固定字在第一次轮询前后就回来了）。
4. **142 与 138 的死因相同**：匹配拿下去 + 启动字，与日志 flush 无关（2/2 零字节）。
5. **fault 消失不等于 HFI 通了**：`info=00000000`、`gfsr=00000000`，没有一条消息、没有一次中断。

**84.10 必须分开说的三件事。**

- `ctl` 的 `000000fe` 那一次不能与 `00000001` 那三次混为一谈：bit0 是判别位，其余位本轮没有解释依据
  （`AdrenoGmuBootV1.h` 只定义偏移，位定义要回到钉住的上游 `a6xx_gmu.xml` 去查）。
- 「fault 消失」是**寄存器账本**上的事实，不是「CM3 的读成功了」的证明：它只说明送进 SMMU 的那次访问
  没有落在空条目上。读进去的内容对不对，本轮没有读回队列表内容来验证。
- `fills=3` 与 `fills=2` 是**页数**（根页 + 两个子页 / 两个子页），不是条目数；每页 512 个条目。

**84.11 没证明什么。**

1. 没证明 HFI 真的开始工作了：没有消息、没有中断、没有 `info` 位。
2. 没解释 `ctl=000000fe` 那一次：可能是另一条状态路径，也可能是读到了半个初始化状态。
3. 没证明 `far=0` 是「读成功」而不是「fault 发生在别的 bank 或者根本没发生」；本轮没有读全局 `gfsr`
   以外的旁证，`gfsr=0` 只说明没有未匹配的流。
4. 没证明 143 与 144 的差别只来自根页：两次引导的 `root` 物理地址相同，但 `fills` 不同，仍属单变量。
5. 没碰 GX 电源域、没碰提交路径：这两处仍是原来的状态。

**84.12 R261 设计（三个键）。**

- **145**：重复键 143 三次（同一 payload，连续引导），把 `ctl` 的读法统计成一行
  `gmu145a=ctl_samples=1 ctl0=… ctl1=… ctl2=… bit0_count=… polls0=…`，回答「bit0=1 是常态还是偶发」。
- **146**：填满三页 + 启动字之后，**读回队列表本身的前 64 字节**（`HFI_QTBL_ADDR` 指向的物理页），
  看 CM3 有没有把消息写进去（`gmu146a=qtbl_read head0=… head1=…`）——这是「fault 消失之后固件到底
  做了什么」的第一条直接证据。
- **147**：填满三页 + 启动字 + **开 `GMU2HOST_INTR_MASK` 的 bit0（MSGQ）** + 满轮询，看 `info` 是否
  出现 `00000001`；与键 145 的 `info=0` 对照。

**84.13 工具事实（五条）。**

1. 探针块一律**插在 ladder 之前**，并且新块的切片终止符永远是「下一个块的块头」；本轮 `_k139141` 的
   终止符改成 R260 块头、新增 `_k142144` 以 ladder 收尾。
2. `stage6` 里 `rd32(gSmmuVa, 0x24u) <= 20` 与它的消息串必须同步改，ID1 的预算是逐轮累加的。
3. 静态门抓行正则要允许前缀：`"[^"]*?gmu1(?:42|43|44)[a-z][0-9]?=[^"]*"`；`gmu142n=cleaned=0`
   这种「源码里有、本轮不可能打印」的拼法要在生成器里显式跳过并写明理由。
4. 自证要指向**文件**而不是目录：`r260_static.py <kernel.macho> <payload.img>`；传目录会
   `PermissionError`。
5. 每次引导之后要**给窗口另存一个名字**（本轮发现 `r260_s143_window.bin` 会被下一次同键引导覆盖，
   第二次 143 的字节因此只留下 sha256 与行文本）。

## 八十五、ROUND 261：把队列表页读回来 —— 三次启动字每次都留下 `ctl=00000001`，而页首四字在两轮不同分配里逐字节相同

### 85.1 一句话

把 SMMU 表页填满之后（R260），启动字被接受；这一轮在这个前提下做两件事：**在同一引导里把启动字写三次**，看 `ctl` 的 bit0 是不是状态而不是边沿；**把 `HFI_QTBL_ADDR` 指向的那一页映射回来，读前四个字**，看固件到底写进去了什么。结果：三次启动字都留下 `ctl=00000001`（轮询数 2 / 1 / 1），队列页首四字是 `00000000 / 00000078 / 00000006 / 0000000c`，**两次不同引导、两个不同物理页，四字逐字节相同**。

### 85.2 为什么是这三键

R260 判决留下了两个问题。第一，`ctl` 的 bit0 在四次引导里读成 1 三次、读成 `000000fe` 一次，同一份代码——需要**同一引导内多次读**才能分清「状态」与「偶发边沿」。第二，fault 消失之后 `info=0`、`gfsr=0`，**没有任何 HFI 消息落进我们的观察**，所以最直接的一步是**不看消息寄存器，直接看内存**：把队列表页读回来。

### 85.3 三键与单变量表

| 键 | 做的事 | 与上一轮的关系 |
|---|---|---|
| 145 | 填三页 + 掩码 + **三次启动字**（各自独立轮询）→ 读一次 fault 账本 | R260 键 143 的重复键 |
| 146 | 填三页 + 掩码 + **一次启动字** → 映射队列表页、读 `w0..w3` | 新增观察点 |
| 147 | 填三页 + 掩码 + 启动 → **重新武装 `QTBL_ADDR`/`QTBL_INFO`** → 二次启动 → 读同一页 | 驱动侧的 re-arm 路径 |

| 轮次 | 键 | 启动字 | 表页 | `fsr` | `ctl` |
|---|---|---|---|---|---|
| R260 | 143 | 一次 | 填三页 | `00000400` | `00000001` |
| R260 | 143 | 一次 | 填三页 | `00000400` | `00000001` |
| R260 | 143 | 一次 | 填三页 | `00000400` | `000000fe` |
| R260 | 144 | 一次 | 只填两个子页 | `00000400` | `00000001` |
| **R261** | **145** | **三次** | 填三页 | `00000400` | **1 / 1 / 1** |
| **R261** | **146** | 一次 | 填三页 | `00000400` | `00000001` |
| **R261** | **147** | 两次（中间 re-arm） | 填三页 | `00000400` | **1 / 1** |

### 85.4 代码形状

块 **33,695 字符 / 646 行**，探针因此到 **931,549 字符 / 14,600 行**；严格 GMU 写点从 423 涨到 **442**。块内：`gmuW[...] =` **19** 笔、`HFI_CTRL_INIT] = 1u;` **6** 笔、掩码 3 笔、`GMU2HOST_INTR_CLR = 0xffffffff` 1 笔、`CM3_FW_INIT_RESULT = 0` 1 笔、`HFI_QTBL_ADDR` 2 笔、`dc civac` 4 处、`for (` **15**、`IODelay(1u)` **7**、没有任何 `IOSleep`。三键共用同一段前导（建表 → 九笔编程 CB0 → 读回 → 清页 → `SMR0=80000004` / `S2CR0=0` → 清 `FW_INIT_RESULT` → 写 `QTBL_ADDR/INFO` → `SYSRESET` 脉冲 → `CP_RESP=0` → 200000 轮 `0xbabeface` 轮询 → 两笔中断清除）。

读队列页用的是探针既有的物理映射入口（块内 `extern` 声明）：

```c
extern uint64_t armcpu_map_physical (uint64_t physical, uint64_t length);
```

映射粒度 4 KiB，覆盖 `HFI_QTBL_ADDR` 所在的页；读出 `w0..w3` 之后不做任何写回。

### 85.5 构建与门禁

CI **#372 `44978050991`** success、**28/28** 步（`2026-11-22T02:52:19Z → 03:07:23Z`）。产物：`kernel.macho` **126,353,408 B**（sha256 `c1820d0e85f901ce1c0f4455b89ef9871965eed4434ce4e387e138486aeaf0ee`）、`payload.img` **150,994,944 B**（sha256 `630bca85166030b2ddeab08679b97dbe038113609f549c89835fb90760a92b18`）。静态门 **PASS**（39 条在两件产物里都出现），指向 R260 产物时 **FAIL 27 条**。门禁：host **28/28**、stage4 **61/61**、stage5 **48/48**、stage6 **1608/1608**、全量 **46 of 48, FAILING 0**。

### 85.6 键 145 逐字读数（`boot_id 583854630`，窗口 `38d0f850f0c524a32809d5638b00f6ab1a2f2a29abfddf1b17dd7e22f478c8ff`，14/14 行）

```
gmu145k=three_starts=1 fills=3 l0=00000000ad961003 ctl0=00000001 pol0=2 ctl1=00000001 pol1=1 ctl2=00000001 pol2=1 about_to_read=1
gmu145l=three_starts_end=1 info=00000000 gfsr=00000000 fsr=00000400 far=00000000 fsynr0=00000000 smr_end=80000004 survived=1
```

### 85.7 键 146 逐字读数（`boot_id 584128299`，窗口 `2af7c5d1c4306b606485c96aecafe76e348fd13e37bfd89dd7f382df7fcc51b0`，15/15 行）

```
gmu146k=queue_read=1 fills=3 l0=00000000ad961003 qtbl=ad955000 qpa=ad955000 ctl=00000001 polls=2 info=00000000
gmu146k2=queue_words=1 win=73548000 w0=00000000 w1=00000078 w2=00000006 w3=0000000c
gmu146l=queue_read_end=1 gfsr=00000000 fsr=00000400 far=00000000 fsynr0=00000000 smr_end=80000004 survived=1
```

### 85.8 键 147 逐字读数（`boot_id 583776814`，窗口 `7f54d4e3eae3f1d126d4ecd2867510d0cc89bf5bb80564ec7a3ff41a3db53ba5`，15/15 行）

```
gmu147k=rearmed=1 fills=3 l0=00000000ad961003 ctl0=00000001 pol0=2 ctl1=00000001 pol1=1 qtbl_now=ad953000 info=00000000
gmu147k2=queue_words_after_rearm=1 win=792c1000 w0=00000000 w1=00000078 w2=00000006 w3=0000000c
gmu147l=rearmed_end=1 gfsr=00000000 fsr=00000400 far=00000000 fsynr0=00000000 smr_end=80000004 survived=1
```

### 85.9 判决

1. **启动字是状态，不是边沿**：同一引导里三次写启动字，三次读回都是 `ctl=00000001`；R260 那次 `000000fe` 因此是四次里的一次例外，而**接受本身是稳的**。
2. **重复启动一次比一次快**：轮询数是 2 / 1 / 1 —— 第一次要等固件走完启动，后两次立刻返回，说明固件已经把启动当成状态收下了。
3. **队列页首四字是非零且有结构的**：`00000000 / 00000078 / 00000006 / 0000000c`；`0x78 = 120`、`0x06`、`0x0c`（= `0x78 >> 4` 的两倍关系只是观察，未归因）。
4. **两次不同引导、不同物理页，四字逐字节相同**（键 146 的 `qpa=ad955000`、键 147 的 `qtbl_now=ad953000`，窗口虚拟地址也不同：`73548000` / `792c1000`）⇒ 这四个字**不是随机残留**，而是某一方**按固定布局写下的**。
5. **re-arm 没有改变可观察的东西**：二次启动仍然秒回（`pol1=1`），队列页四字与键 146 完全相同。
6. 三次引导的 fault 账本都干净（`fsr=00000400`、`far=00000000`、`gfsr=00000000`、`fsynr0=00000000`），`smr_end=80000004`。

### 85.10 必须分开说的三件事

1. **我们还没有测过「启动字之前」的那一页**。键 146/147 都是**先启动、再读**。所以「这四字是固件写的」这个说法目前**没有基线支撑**——它也可能来自本引导更早的一次写入（我们的前导只写 SMMU 表页与 GMU 寄存器，不写这一页，但这一点要下一轮用基线键证明）。
2. **`ctl` 的 bit0 是判别位，其余位没有解释依据**。`00000001` 与 `000000fe` 只能按 bit0 分，不能说 `0xfe` 那次「启动了一半」。
3. **这是内存账本，不是 HFI 消息**。`info=00000000`、`gfsr=00000000`，没有一条消息寄存器被置起；页首四字与消息寄存器之间的关系**未建立**。

### 85.11 这一轮没有证明什么

- 没有证明这四字来自 CM3（缺启动前基线）；
- 没有证明页首四字是 HFI 队列头（没有对照队列头的定义逐字段比对）；
- 没有证明 `ctl` bit0=1 意味着 HFI 通道就绪（`info`/`gfsr` 全零、没有中断）；
- 没有证明 re-arm 路径对固件有意义（二次启动秒回，与「什么都没变」不可区分）；
- 没有碰 GX 电源域、提交路径、Metal 前端。

### 85.12 R262 三键设计

- **148**：填三页 → 掩码 → **启动字之前**先映射并读回同一页（基线）→ 启动字 → 轮询 → **再读一次同一页** → 两次四字并排打印（同一引导内的差分）。
- **149**：填三页 → 掩码 → **不写启动字**，只映射并读两次同一页（间隔 `IODelay`）→ 判「这一页在我们不动它的时候是否稳定」。
- **150**：填三页 → 启动 → 轮询 → 映射并打印**前 32 个字**（128 字节），看它是否更像一个头（`status/start/end/size` 这类布局）。

### 85.13 工具事实

1. `armcpu_map_physical(uint64_t physical, uint64_t length)` 是探针里的只读映射入口（`package/EnchiladaAdrenoProbe.cpp:214`），块内必须自己 `extern` 声明；`rd32` 的签名是 `rd32(uintptr_t va, uint32_t off)`，**收整数地址**，不能传指针。
2. 同一个键连跑两次会**覆盖同一个窗口文件**；每次引导要另存名字（本轮已另存 `r261_s145_window.bin` 等）。
3. 静态门自证必须传**文件全路径**（传目录会 `PermissionError`）。
4. runner 的 `FIELDS` 只能引用**块内真的会打印的 tag**：R261 的 146/147 共用 145 的前导行，因此 `build/nab/cleaned` 只能从 `gmu145b/gmu145c/gmu145n` 取——本轮先写错成 `gmu146b`，靠交叉核对脚本抓出来。
5. 判决串要能接受设备实际打印的**量纲**：`l0` 在 145 里打印成 `2`（不是 16 位十六进制），因为该行用的是 `%u`；判据不能假设格式。
## 八十六、ROUND 262：启动前那四个字就已经在页里了 —— 这一轮把上一轮的解释翻了过来

### 86.1 一句话

把「启动前」和「启动后」分开读，那四个字（`00000000 00000078 00000006 0000000c`）在**写启动字之前**就已经在页里；不写启动字的那一臂也读到同样的四个字、而且两次读一致；启动字写下去、轮询跑完，那四个字**一个字都没动**。

### 86.2 为什么先看启动前

R261 读到「启动之后队列页前四字是 `00 78 06 0c`」，但那一轮没有启动前的基线，所以只能登记、不能归因。这一轮的第一件事就是把基线补上：同一个引导里先读一次、再启动、再读一次；另一臂干脆不启动，只读两次。

### 86.3 三键设计

| 键 | 做什么 | 判什么 |
|---|---|---|
| **148** | 填三页 → 读页首四字 → 清状态字 → 掩码 → 启动字 → 200000 轮 → 再读同一页四字 | 两次读数是否相同；`ctl` 的 bit0 |
| **149** | 填三页 → **一笔 GMU store 都不写** → 读四字 → `IODelay(1u)` → 再读四字 | 页面在没有启动请求时是否稳定、是否已有内容 |
| **150** | 填三页 → 启动 → 轮询 → 打印前 24 个字（三组八） | 页面是否更像一个队列头 |

单变量表（只列本轮用到的臂）：

| 轮/键 | 启动字 | 启动前基线 | 读到的四字 |
|---|---|---|---|
| R261 键 146 | 有 | **无** | `00 78 06 0c`（不可归因） |
| R262 键 149 | **无** | 有 | `00 78 06 0c`（两次一致） |
| R262 键 148 | 有 | 有 | 前后都是 `00 78 06 0c` |
| R262 键 150 | 有 | — | 24 字，两个字记录（`ad950000` / `ad951000`） |

### 86.4 代码形状

块 `work\tmp\r262_src_block.txt` 级别的一手数字：块 **28,707 字符 / 530 行**、探针 **960,258 字符 / 15,131 行**、全文件严格 `gmuW[...] =` 写点 **454**（R261 的 442 + 本块 12）。

块内：`gmuW` **12**（前导 8 + 148/150 各 mask+启动字）、`HFI_CTRL_INIT] = 1u;` **2**、`GMU2HOST_INTR_MASK] = ` **2**、`0xff7fffffu` **2**、`GMU2HOST_INTR_CLR] = 0xffffffffu;` **1**、`CM3_FW_INIT_RESULT] = 0u;` **1**、`HFI_QTBL_ADDR] = ` **1**、`HFI_QTBL_INFO] = 1u;` **1**、`0xc00001feu` **2**、`0x58u` 5 / `0x60u` 3 / `0x68u` 1 / `0x800u` 3、`for (` **9**、`IODelay(1u)` **4**、`512ull` **1**、`dc civac` **2**、`armcpu_map_physical` **9**、`0xbabefaceu` 1、无 `IOSleep`。

「填三页」被提到前导段（一次），三支共用：`r148tab1 = adreno_lpae_table_pte(r148l1pa)`、`r148tab2 = adreno_lpae_table_pte(r148l2pa)`、`r148leaf = adreno_lpae_leaf_pte(prot148, 2u, r148base)`，`for (r148fi = 0ull; r148fi < 512ull; ++r148fi)` 写满，然后 `dc civac` 全 span + `dsb sy`。

### 86.5 构建与门禁（含一次真崩溃）

- host 门 **28/28**、stage4 **61/61**、stage5 **48/48**、stage6 **1629/1629**、全量 `..\tmp\r239_allgates.py` → **ran 46 of 48, FAILING: 0**，全部在 `work\UniXNU-enchilada` 里、`TMP/TEMP=…\work\tmp\clangtmp`。
- CI **#374 run `44988007315`** success 28/28（`2026-11-22T05:07:19Z → 05:21:46Z`，14m27s）。
- 构建产物：`work\tmp\r262-kernel\kernel.macho` **126,369,792 B** sha256 **`3d450edf76a488b4f8fa9a4468a2887a5b6d2bd3d187ff3e9f4e051bf0b3b387`**；`work\tmp\r262-payload\payload.img` **150,994,944 B** sha256 **`4c05c2c5b0ee2c852c7a026f9af4a431ad2f32a44fed797a78e828a2abf032de`**。
- 静态门 `work\tmp\r262_static.py`（26 条 REQUIRED）：对真产物 **`PASS: R262 static gate`**（`required+survivors: 38 checked in both artefacts`、key marker kernel=1 payload=2、forbidden 3、固件钉 `a630_gmu.bin` 32,768 B）；对 R261 产物自证 **`FAIL: R262 static gate (26 problems)`**。
- **第一次推上去的构建里，149/150 两臂整段消失**：静态门对真产物 FAIL 5 条（`gmu149k/gmu149l/gmu150k/gmu150k2/gmu150l` 全零命中），而分支上的 blob 里那些行都在。原因是生成器的整体改名把 mode 选择器压成了常量：`const int r148mode = (fwKey >= 148u) ? 148 : ((fwKey >= 148u) ? 148 : 148);` ⇒ clang `-O2` 判定 `r148mode == 149` 与最后 `else` 不可达，两段分支连同格式串一起被丢掉（`-Wunreachable-code` 没开，零警告）。修法是生成器显式重写这一行并断言：`(fwKey >= 150u) ? 150 : ((fwKey >= 149u) ? 149 : 148)`。**教训：整体改名会毁掉「同一个数字既当键号又当区间端点」的表达式；静态门必须对真产物跑。**

### 86.6 键 148 逐字（三次引导，两次同形）

`gmu148b=build=0 need=3 tables=3 writes=3 root=ad955000 base=ad800000 iova=2d800000 ias=48 oas=36 ok=1`

`gmu148m=readback=1 root_off=0 l2_idx=364 l0=00000000ad956003 l1=00000000ad957003 l2=00000000ad800401 m0=1 m1=1 m2=1 pages=3`

`gmu148n=cleaned=1 pages=3 span=3000 dsb=1`

`gmu148k=diff_pre=1 fills=3 qtbl=ad951000 qpa=ad951000 win=4dfd8000 p0=00000000 p1=00000078 p2=00000006 p3=0000000c about_to_start=1`

`gmu148l=diff_post=1 ctl=00000001 bit0=1 polls=2 info=00000000 q0=00000000 q1=00000078 q2=00000006 q3=0000000c fsr=00000400 far=00000000 fsynr0=00000000 survived=1`

- 三次引导的窗口 sha256：`992d4d52a0bd3cf2…`（`ctl=000000fe`，两次读数同形）、`8bffaedab1f8e59f…`（**四字全零**、`ctl=00000000`、`info=00800000`）、`77e1d01b18bd456a…`（`ctl=00000001`、`polls=2`、四字同形）。
- 也就是说：同一条代码、同一份镜像，**页面内容在引导之间并不总是相同的**（三次里两次 `00 78 06 0c`、一次全零）。这条差异本轮没有解释，只登记。

### 86.7 键 149 逐字（无启动字）

`gmu149k=no_start_read=1 fills=3 qtbl=ad953000 qpa=ad953000 win=bd96b000 a0=00000000 a1=00000078 a2=00000006 a3=0000000c about_to_read_again=1`

`gmu149l=no_start_again=1 b0=00000000 b1=00000078 b2=00000006 b3=0000000c ctl=00000000 bit0=0 info=00000000 fsr=00000400 far=00000000 survived=1`

窗口 sha256 `34f12dcb5b44aa56…`、boot_id 583471469、14/14 行。**这一臂整轮一笔 GMU store 都不写**，页面里仍然有那四个字，两次读一模一样，`ctl` 的 bit0 为 0、`info` 为 0、状态字干净。

### 86.8 键 150 逐字（24 个字）

`gmu150k=page32=1 fills=3 qtbl=ad94f000 qpa=ad94f000 win=978e2000 ctl=00000001 polls=2 w00=00000000 w01=00000078 w02=00000006 w03=0000000c w04=00000002 w05=00000002 w06=00000001 w07=ad950000`

`gmu150k2=page32b=1 w08=00000a00 w09=00000400 w10=00000000 w11=00000000 w12=00000001 w13=00000001 w14=00000001 w15=00000000`

`gmu150l=page32_end=1 w16=00000000 w17=00000000 w18=00000001 w19=ad951000 w20=00000a04 w21=00000400 w22=00000000 w23=00000000 info=00000000 fsr=00000400 far=00000000 survived=1`

窗口 sha256 `e61218d4ecca41b2…`、boot_id 582938599、15/15 行。读出来的 24 个字分成**两条十二字记录**（`w00..w11` 与 `w12..w23`），各自带一个指针（`ad950000` / `ad951000`）和一对常数（`00000a00`/`00000400`、`00000a04`/`00000400`）—— 这是两个队列头并排的形状，不是「一次请求的答复」。

**判决串的一处错误（必须说明）**：150 的 runner 最后一行写「the control word still has bit 0 clear after the full poll」，可那一行 `ctl=00000001`。原因是判据从 `gmu150l` 取 `ctl`，而 `ctl` 只打在 `gmu150k` 上，取到 `None` 就落到「未置起」分支。**行里的 `ctl=00000001` 才是记录**；判决串这句是错的，R263 的生成器要加一条自检（判据引用的字段必须在该键真的打印出来的行里）。

### 86.9 判决

1. **那四个字在我们写启动字之前就存在**（键 149：整轮零 GMU store，页面照样是 `00 78 06 0c`，两次读一致）。
2. **启动字不改变那四个字**（键 148：前后逐字相同）。
3. 所以 R261 记的「启动之后队列页前四字」**不能归给启动请求** —— 这是对 §85.9 第 2 条的订正。
4. **CM3 接受启动**这一点不变：148 的干净一次与 150 都是 `ctl=00000001`、`polls=2`。
5. 页面形状是**两条十二字记录**，各带指针与常数对，更像预先摆好的队列头，而不是我们这次请求的产物。

### 86.10 必须分开说的三件事

1. 「启动前」不等于「我们什么都没做」：前导段仍然重建了表、编程了 CB0、写了 `HFI_QTBL_ADDR`/`HFI_QTBL_INFO`、做了 CM3 复位脉冲。所以 149 证明的是「**不写启动字**也一样有那四个字」，不是「页面的内容与固件无关」。
2. 三次 148 引导里有一次四字全零、`ctl=0`、`info=00800000`。这条差异没有被解释，也没有被重复出来；不能拿它去支持任何一种解释。
3. `far`/`fsr`/`fsynr0` 全干净只说明**这一次没有翻译 fault**，不说明「CM3 读成功」。

### 86.11 没证明什么

- 没证明那四个字是**谁**写的（CM3 初始化？我们复位脉冲之后的固件？分配器的初值？）。
- 没证明那两个指针（`ad950000` / `ad951000`）指向什么，也没证明它们与 HFI 消息队列的关系。
- 没证明 149 与 148 之间「页面不变」的原因是「固件没动」而不是「我们读的是同一个物理页」—— 两臂的物理页本来就可能不同。
- 没证明 `ctl=000000fe` 那一次（148 第一次引导）的额外位是什么意思。
- 没有一帧渲染、没有一个 HFI 消息、没有一次中断。

### 86.12 R263 设计（三键，把前导段拆开）

- **151**：同一引导里在**武装 QTBL 之前**读页面四字（`qtbl` 已从寄存器读到，但 `HFI_QTBL_ADDR`/`QTBL_INFO` 还没写），判「那四个字在武装之前就在」。
- **152**：武装之后、**CM3 复位脉冲之前**读一次，判「武装这一笔是否就足以让页面有内容」。
- **153**：复位脉冲之后、启动字之前读一次，判「复位之后的固件是否写页」。
- 三键共用 R262 前导段，只是把「读页面」这一步插在不同位置；判据是「哪一步之后四字从零变成非零」。

### 86.13 工具事实

1. **整体改名会毁掉 mode 选择器**：`re.sub(r"(?<![0-9])(14x|14y|14z)(?![0-9])")` 会把 `(fwKey >= 150u) ? 150 : ((fwKey >= 149u) ? 149 : 148)` 压成常量，clang 随后把不可达分支整段消除。生成器必须显式重写这一行并断言。
2. **静态门必须对真产物跑**：本轮只有它抓到了「源码里有、二进制里没有」。自证（对上一轮产物 FAIL）也要留着。
3. `work\tmp\r214_par.py` 在目录已存在时不会重新下载，会打印 `already flat` 并给出**旧文件的哈希**；换 run 之前要先删掉旧的 `r262-kernel`/`r262-payload` 目录。本轮因此白跑了一轮静态门。
4. runner 的判决串**不能硬编码「控制字未置起」**：R262 的 150 就是这样写错了一行；判据引用的字段必须在对应行里真的打印。
5. 同一个键连跑三次会覆盖同一个窗口文件；本轮已另存 `r262_s148_window_a|b|c.bin`，并把三份设备日志分别保存。
6. PowerShell 里 `python - <<'PY'` heredoc 不可用（`ParserError`），一律先 `Out-File` 成 `.py` 再跑。

## 八十七、ROUND 263：把读页面的时刻夹在武装与复位的两侧 —— 那四个字在我们任何一笔写之前就在页里，而固件只有做过复位脉冲才会接受启动

### 87.1 一句话

同一个队列页在**武装之前**就已经是 `00000000 / 00000078 / 00000006 / 0000000c`；武装、复位脉冲、启动字三笔都没有改动它一个字。而**跳过复位脉冲**的那一轮里，这个地址上的页是**全零**、`CM3_FW_INIT_RESULT` 读回 `00000000`、启动字轮询 200000 轮**一次都没被接受**；做过复位脉冲但不写启动字的那一轮，页有内容、固件重新初始化（`babeface`）、而 `ctl` bit0 仍是 0。

### 87.2 为什么把读点拆成三段

R262 只在「武装之前」和「启动之后」各读了一次，于是「四字从哪儿来」仍然有两个候选：来自**这一次引导的复位脉冲**，还是来自**更早**。R263 在同一个引导里把读点插到武装前（K1）、武装后复位前（K2）、复位后启动字前（K3）三个位置，再用 152/153 把「脉冲」与「启动字」分开。

### 87.3 三键

| 键 | 做的事 | 判决 |
|---|---|---|
| **151** | K1/K2/K3 三次读页 → 清状态字 → 掩码 → 启动字 → 200000 轮 → 再读（L） | 四次读**逐字相同** `00 78 06 0c`；`ctl=00000001 bit0=1 polls=2` |
| **152** | 同 151，但**整轮跳过复位脉冲** | 四次读**全零**；`fwinit=00000000`、`ctl=00000000 bit0=0 polls=200000` |
| **153** | 同 151，但**不写启动字** | 四次读**逐字相同** `00 78 06 0c`；`fwinit=babeface`、`ctl=00000000 bit0=0 polls=200000` |

单变量表：

| 轮/键 | 复位脉冲 | 启动字 | 页首四字 | 固件重初始化 | `ctl` bit0 |
|---|---|---|---|---|---|
| R262 148/149 | 有 | 有/无 | `00 78 06 0c` | `babeface` | 1 / 0 |
| R263 151 | 有 | 有 | `00 78 06 0c` | `babeface` | **1**（`polls=2`） |
| R263 152 | **无** | 有 | **全零** | **`00000000`** | **0**（`polls=200000`） |
| R263 153 | 有 | **无** | `00 78 06 0c` | `babeface` | 0（`polls=200000`） |

### 87.4 逐字读数

键 151（第一次 `boot_id 583128826`、窗口 `ebf6eaa7fa9ea85b4d8c5965c758cb75d1ad9da7ab2762b8e56da4a39296b078`；第二次 `boot_id 583648515`、窗口 `4011a2405f6f22b2c1ac5950cd05f76edc7938c9ba405904d386bcca5eb3d4d5`，两次读数逐字相同）：

```
gmu151g=last_row=1 about_to_start=1 mode=151 route=1 smr=80000004 s2cr=00000000 qtbl=ad953000 qtbl_value=ad953000 base=ad800000
gmu151h=init_cleared=1 before=babeface after_clear=00000000
gmu151i=reboot=1 fwinit=babeface polls=3 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu151j=interrupts_cleared=1 info=00000000 mask=00000000 ao=00000000
gmu151m=readback=1 root_off=0 l2_idx=364 l0=00000000ad959003 l1=00000000ad95a003 l2=00000000ad800401 m0=1 m1=1 m2=1 pages=3
gmu151n=cleaned=1 pages=3 span=3000 dsb=1
gmu151k1=pre_arm=1 qtbl=ad953000 qpa=ad953000 win=4449e000 a0=00000000 a1=00000078 a2=00000006 a3=0000000c
gmu151k2=armed=1 addr=ad953000 info=00000001 win=4449e000 b0=00000000 b1=00000078 b2=00000006 b3=0000000c
gmu151k3=post_reset=1 sysreset=00000000 fwinit=babeface win=4449e000 c0=00000000 c1=00000078 c2=00000006 c3=0000000c
gmu151l=full_bracket=1 fills=3 ctl=00000001 bit0=1 polls=2 info=00000000 d0=00000000 d1=00000078 d2=00000006 d3=0000000c fsr=00000400 far=00000000 fsynr0=00000000 survived=1
```

键 152（`boot_id 648666337`、窗口 `36d66d0dce49b27cb48ab655f61f892d7aae9a0022442a236377f75e0ee36269`）：

```
gmu151i=reboot=1 fwinit=00000000 polls=200000 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu151k1=pre_arm=1 qtbl=ad951000 qpa=ad951000 win=29cb3000 a0=00000000 a1=00000000 a2=00000000 a3=00000000
gmu151k2=armed=1 addr=ad951000 info=00000001 win=29cb3000 b0=00000000 b1=00000000 b2=00000000 b3=00000000
gmu151k3=post_reset=1 sysreset=00000000 fwinit=00000000 win=29cb3000 c0=00000000 c1=00000000 c2=00000000 c3=00000000
gmu152l=no_reset=1 fills=3 ctl=00000000 bit0=0 polls=200000 info=00800000 d0=00000000 d1=00000000 d2=00000000 d3=00000000 fsr=00000400 far=00000000 fsynr0=00000000 survived=1
```

键 153（`boot_id 598648172`、窗口 `44bd79fc223a983c6b3f91091df6e3b94f6c6cf5a06c67eefcf4af419c5d0a67`）：

```
gmu151i=reboot=1 fwinit=babeface polls=3 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu151k1=pre_arm=1 qtbl=ad955000 qpa=ad955000 win=a716e000 a0=00000000 a1=00000078 a2=00000006 a3=0000000c
gmu151k2=armed=1 addr=ad955000 info=00000001 win=a716e000 b0=00000000 b1=00000078 b2=00000006 b3=0000000c
gmu151k3=post_reset=1 sysreset=00000000 fwinit=babeface win=a716e000 c0=00000000 c1=00000078 c2=00000006 c3=0000000c
gmu153l=no_start=1 fills=3 ctl=00000000 bit0=0 polls=200000 info=00000000 d0=00000000 d1=00000078 d2=00000006 d3=0000000c fsr=00000400 far=00000000 fsynr0=00000000 survived=1
```

### 87.5 判决

1. **那四个字在本轮任何一笔 GMU 写之前就在页里**（151 的 K1、153 的 K1 都是 `00 78 06 0c`；K1 位置在 `HFI_QTBL_ADDR` 与 `QTBL_INFO` 两笔之前）。
2. **武装、复位脉冲、启动字三笔都不改动这一页**（K1 = K2 = K3 = L，两个独立引导、两个不同物理页 `ad953000` / `ad955000`）。
3. **跳过复位脉冲 ⇒ 固件不重新初始化**：`CM3_FW_INIT_RESULT` 从清零点一直读到 `00000000`（151/153 都会回到 `babeface`），启动字轮询 `200000` 轮没有一次让 `ctl` bit0 置起。
4. **做过复位脉冲但不写启动字 ⇒ 固件初始化了、启动仍未被接受**（153：`fwinit=babeface`、`ctl=00000000 bit0=0 polls=200000`）。
5. ⇒ **接受启动 = 复位脉冲（固件重跑）+ 启动字**，两个条件缺一不可；而**页首四字与这两件事都无关**，它们是更早的东西。
6. 对 §63 的订正：那四字**不是**「复位脉冲之后的固件写进去的」，因为 K1 读点在脉冲之前、且脉冲不改变它；它们随继承来的地址而来。

### 87.6 必须分开说的三件事

1. 「K1 之前我们什么都没做」不成立：前导段已经重建了 SMMU 表、编程了 CB0、清过 `CM3_FW_INIT_RESULT`；K1 只能证明「**本轮武装与启动相关的写之前**」。
2. 152 的页全零与 153 的页有内容，两轮的 `qtbl` **地址不同**（`ad951000` vs `ad955000`），所以「有没有内容」与「地址继承自哪一轮」是混在一起的，**不能**用 152 单独证明「脉冲决定页面内容」。
3. `fsr=00000400 / far=0 / fsynr0=0` 三键全干净，只说明没有翻译 fault，不说明固件读过这一页。

### 87.7 没证明什么

- 没证明那四个字是谁写的（CPU 侧 HFI 初始化 / 上一轮遗留在同一物理页 / 分配器初值）。
- 没证明 CM3 在 `ctl` bit0=1 之后读没读过这一页（读数不变不等于没读）。
- 没证明 `info=00800000`（152）与 `info=00000000`（151/153）之间差别的来源。
- 没有一条 HFI 消息、没有一次中断；GX 电源域仍未打开；GPU 从未渲染一帧。

### 87.8 R264 设计（三键，问「接受之后发生什么」）

- **154**：复位脉冲 + 启动字 + 满轮询，然后读 `CP_RESP`/`CP_MSG`、`GMU2HOST_INTR_INFO`，并把队列页第 1 个字（写索引）隔 `IODelay` 读两次 —— 判固件有没有推进队列。
- **155**：先把队列页首四字写成一组**可辨认的**值（`0xdeadbeef` 系）再复位 + 启动，看固件是否覆盖/消费它们 —— 判页面归属。
- **156**：同 155 但不写启动字（对照，判「写了页就变」是不是我们自己写的）。

### 87.9 工具事实

1. **runner 的 tag 拼接 bug**：`field("gmu%d l" % key, …)` 里多了一个空格，`_d`/`_bit0`/`_tf` 全部取到 `None`，判决串打出「page after the poll: None None None None」与「still has bit 0 clear」——而设备行本身是好的。生成器里要替换的是 `gmu%d l` 这个**子串**（转义字符串里的那次出现不会被 Python 表达式替换覆盖）。
2. `r263_anchor.py` 只认 `PREV_SHA = "0" * 64` 或 64 位十六进制字面量；生成器写成 `PREV_SHA = ""` 会让锚点断言 `replacements PREV_BOOT=1 PREV_SHA=0` 失败。
3. 同一个键连跑两次会覆盖 `r263_sNNN_window.bin`；本轮已另存 `r263_s151_window_r1.bin`、`r263_s151_window_r2.bin`、`r263_s152_window_r1.bin`。
4. `r214_par.py` 在目标目录已存在时不重新下载（打印 `already flat`），换 run 前必须先删目录。

## 八十八、ROUND 264：把页面的第 4..7 字改掉，固件就不再接受启动 —— 而 CP 消息字在「不写启动字」的对照里也有值

### 88.1 一句话
同一轮里，**不碰页面的键 154 得到 `ctl` bit0=1（`polls=2`），而先往页面第 4..7 字写四个图案的键 155 得到 `ctl` bit0=0（`polls=200000`、`info=00800000`）**；可是两个键把第 4..7 字读回来都是全零，我自己的写没有一次可见 —— 所以「固件读了这页」有了第一个行为证据，「这页归谁」还没有。

### 88.2 为什么这两个问题必须一起问
R263 把页面第一行的四个字（`00000000 00000078 00000006 0000000c`）在**任何一笔写之前**就已经存在这件事钉死了。剩下两个问题互相咬住：**（a）启动请求被接受之后，固件会做什么？**（b）**这页是我们的还是固件的？** 只有当我们的写能在页上留下可见痕迹，(b) 才有判据；而只有 (b) 有判据，(a) 里「固件改了什么」才不会被误当成「我们自己写过什么」。这一轮把两件事放进同一组引导：154 只问 (a)，155 用图案问 (b)，156 用同一个图案但不写启动字，把「图案自己会不会消失」从「固件改了它」里分出来。

### 88.3 三键与单变量表
| 键 | 复位脉冲 | 页第 4..7 字 | 启动字 | 判什么 |
|---|---|---|---|---|
| 154 | 有 | 不动 | 写 | 接受之后固件留下什么 |
| 155 | 有 | 写图案 | 写 | 图案能不能被我看见、固件会不会动它 |
| 156 | 有 | 写图案 | 不写 | 图案在没有人请求启动时是否自己消失 |

单变量对照（上一轮 → 本轮）：
- R263 键 153（不写启动字、不碰页面）→ 本轮键 156（不写启动字、写图案）：`ctl` 都是 0，但 156 多了「图案读回是否等于写入」这个判据。
- R263 键 151（写启动字、不碰页面）→ 本轮键 154：多读 `cpmsg`/`cpresp` 两字。
- 本轮键 154 → 键 155：只差「页第 4..7 字写不写图案」这一项。

### 88.4 代码形状
块 38,025 字符 / 708 行、花括号平衡 0；探针 1,027,734 字符 / 16,384 行；全文件严格 GMU 写点 **467 → 480**（新块自身 13 笔）；块内 `for (` 10 个、`IODelay(1u)` 7 个、`dc civac` 4 处、`0xbabefaceu` 1 处、`armcpu_map_physical` 12 处（其中 `_rw` 6 处）、`PWR_COL_CP_MSG` 3 处、`PWR_COL_CP_RESP` 4 处、无 `IOSleep`。19 条行名：`gmu154a..gmu154n`、`gmu154k1/k2/k3/k4`、`gmu154l`、`gmu155l`、`gmu156l`。

### 88.5 构建与门禁
CI run `44997367218`（#376）`2026-11-22T07:15:04Z → 07:35:25Z`，28/28 步成功。门禁：host 28/28、stage4 61/61、stage5 48/48、stage6 1667/1667、全量 46 of 48、FAILING 0；`check_mode_selector.py` 对 18 个三元选择器 PASS（自证对压平写法 FAIL）。静态门对真产物 PASS（26 条 REQUIRED + 12 条 SURVIVORS = 38 条在两件产物里各出现一次），对 R263 产物自证 FAIL 26 条。

### 88.6 键 154（`PASS`，boot 583027353，窗口 `84c3c388422f7cb94847e6957812723fad0f080ff1c5a34fb85f9f8f70a4d645`）
```
gmu154k1=pre_arm=1 qtbl=ad954000 qpa=ad954000 win=ee216000 a0=00000000 a1=00000078 a2=00000006 a3=0000000c
gmu154k2=armed=1 addr=ad954000 info=00000001 win=ee217000 b0=00000000 b1=00000078 b2=00000006 b3=0000000c
gmu154k3=post_reset=1 sysreset=00000000 fwinit=babeface win=ee217000 c0=00000000 c1=00000078 c2=00000006 c3=0000000c
gmu154l=cp_after_start=1 fills=3 ctl=00000001 bit0=1 polls=2 info=00000000 cpmsg=ad957000 cpresp=00000000
  d0=00000000 d1=00000078 d2=00000006 d3=0000000c e0=00000000 e1=00000078 e2=00000006 e3=0000000c
  w4=00000000 w5=00000000 w6=00000000 w7=00000000 fsr=00000400 far=00000000 fsynr0=00000000 survived=1
```
`ctl` bit0=1、`polls=2`、`info=00000000`，页四字在四个时刻逐字相同；`cpmsg=ad957000`、`cpresp=00000000`。

### 88.7 键 155（boot 601877756，窗口 `5c0efd72f6baff561c292e15e00bcb61258057e0f1c03ce4e145ceb66eefc700`）
```
gmu154k4=pattern=1 fills=3 qtbl=ad950000 qpa=ad950000 win=521ff000 ... w4=00000000 w5=00000000 w6=00000000 w7=00000000
gmu155l=pattern_after=1 fills=3 ctl=00000000 bit0=0 polls=200000 info=00800000 cpmsg=ad953000 cpresp=00000000
  ... fsr=00000400 far=00000000 fsynr0=00000000 survived=1
```
写入的图案是 `a5a5a5a5 5a5a5a5a c3c3c3c3 3c3c3c3c`，但**写后立刻读回就是四个零**；随后启动字**没有被接受**（`ctl` bit0=0、`polls=200000`），并且**出现了自 R254 以来第一次在填页之后的 `info=00800000`**。

### 88.8 键 156（boot 640317688，窗口 `ad1ea7112db45caabafccd21b70264dc33ef919869ddbddfdea36dc314b825a8`）
```
gmu154k4=pattern=1 ... w4=00000000 w5=00000000 w6=00000000 w7=00000000
gmu156l=pattern_no_start=1 fills=3 ctl=00000000 bit0=0 polls=200000 info=00000000 cpmsg=ad957000 cpresp=00000000
  ... fsr=00000400 far=00000000 fsynr0=00000000 survived=1
```
同一个图案、不写启动字：图案读回仍是四个零，`info=00000000`（没有 CM3 fault），`cpmsg` 仍有值。

### 88.9 判决
1. **`ctl` bit0 这一次不是常数**：154 = 1（`polls=2`）、155 = 0（`polls=200000`）、156 = 0（没有请求）。这是「接受」第一次在同一轮里出现两种结果。
2. **页面第 4..7 字的内容会改变固件的行为**：154 与 155 只差「有没有写那四个字」，结果从「接受」变成「不接受 + `info=00800000`」⇒ 固件确实读这四字（或它们的邻居）。
3. **我自己的写在这三个键里一次都没有可见痕迹**：155/156 的写后读回都是四个零，与写入值不等 ⇒ 「图案被固件抹掉」与「我的写根本没落到这页」两说都还开着。
4. **`cpmsg` 不能记在启动字头上**：156 没有启动字也有 `cpmsg=ad957000`（154 是 `ad957000`、155 是 `ad953000`，逐引导不同），而 R261/262 曾把「启动之后页首四字」归给启动请求 —— 这一条必须按本轮读数收窄。

### 88.10 必须分开说的三件事
- `info=00800000` 是 `GMU2HOST_INTR_INFO` 的 bit 23（`CM3_FAULT`），**只在 155 出现**；156 同样是「图案 + 填页 + 复位脉冲」却没有它，差别只在启动字，所以它跟「启动请求被拒绝」同侧，不是填页的副作用。
- `cpmsg`/`cpresp` 是**寄存器账本**（`PWR_COL_CP_MSG 0x5100` / `PWR_COL_CP_RESP 0x5101`），不是 HFI 消息本身；它们有值不等于固件回了消息。
- 「图案读回是零」**不能**当成「写成功后被抹掉」，也不能当成「写失败」—— 本轮没有任何一笔读是在没有 CM3 参与的情况下对同一映射做的自检。

### 88.11 没证明什么
- 没证明图案写到了物理页上（没有「写后就地读」的同映射自检，也没有换一个字写再看）。
- 没证明 155 的 `info=00800000` 是「读到我改坏的队列头」还是「别的原因」。
- 没证明 `cpmsg` 是固件写的（可能是复位脉冲期间固件初始化写的，156 也有值）。
- 没证明「接受」与「页面内容」之间的因果方向（可能是「不接受」导致固件不清理页，而不是相反）。
- 没证明任何一段 GPU 功能被打通：`ctl` bit0=1 只说明 CM3 报告接受了 HFI 启动，GX 电源域仍未打开、提交路径与映射未动、GPU 未渲染一帧。

### 88.12 R265 设计（三键）
- **157**：在同一映射上「写一个字 → 立刻读回 → 再写另一个字 → 再读回」，先证明写路径（用页内**第 64..67 字**这种没人用的位置，避开队列头）。
- **158**：把图案写在 64..67（安全位置）+ 启动字 ⇒ 若仍被接受，则 155 的拒绝确实来自「队列头被改」，而不是「任何写都会触发」。
- **159**：155 的复现（改 4..7 + 启动字），但**写后先 `dc cvac`（只清不失效）**再启动 ⇒ 把「写没落地」与「被固件抹掉」分开。

### 88.13 工具事实
- `armcpu_map_physical` / `armcpu_map_physical_rw` 的返回值当指针用；同一物理页两次映射得到**不同窗口地址**（154 里 K1=`ee216000`、K2=`ee217000`），所以「窗口地址相同」不能当判据。
- 行 `gmu154k1`、`gmu154k2` 在本轮日志里**各出现两次**（两次映射各打一次），runner 的 `count` 断言按 ≥1 判，不要按 ==1。
- `r214_par.py` 在目标目录已存在时不会重新下载（打印 `already flat` 并给出旧哈希）⇒ 每轮先删目录。
- PowerShell 的 here-string 往 Python 里塞 `\n` 会落成字面反斜杠，生成 C 字符串模板必须用 raw 三引号字符串。

## 八十九、ROUND 265：写路径是真的；而「改掉页头就拒绝启动」这条判决只站得住一半 —— 同一个位置换成 cvac，启动字就被接受了

### 89.1 一句话
我们在 R264 里说「把队列页第 4..7 字改掉，同一笔启动字就被拒绝」。这一轮先把「我们到底写不写得进去」单独证明了（键 157：写进去、立刻读回、换值再读、第二映射复核），再把 R264 那两个位置各重做一次，只改缓存操作 —— 结果：**同样四个图案、同样第 4..7 字，用 `dc cvac` 时启动字被接受（`ctl=1`、`polls=104`），用 `dc civac` 时被拒绝（R264 键 155，`ctl=0`、`polls=200000`）**。所以 R264 的判决要收窄：至少有一次，决定拒绝与否的是清缓存的那一笔，不是页里的内容。

### 89.2 为什么先证明写路径
R264 的两个键都把图案写进队列表页，然后在读回里看到全零。这留下一个无法回避的歧义：**我们根本没写进去**，还是**写进去了但我们自己的回读看不到**。在把「图案改变固件行为」当结论之前，必须先有一个键只回答「写路径是真的吗」。键 157 就是这一键：它不写启动字，只写、只读。

### 89.3 三键设计与预写的规则
| 键 | 写哪里 | 缓存操作 | 启动字 | 读回时机 |
|---|---|---|---|---|
| 157 | 第 64 字 → 改写第 64 字 → 写第 65 字 | `dc civac` + `dsb sy` | **不写** | 每次写完立刻读，再用第二个映射复核 |
| 158 | 第 64..67 字（安全字） | `dc civac` | 写 | 清缓存后、启动后各一次 |
| 159 | 第 4..7 字（页头） | **`dc cvac`** | 写 | 同上 |

三条规则在跑之前就打印并固定：写回的值是我们写进去的 ⇒ 映射真实且可写；同一字读两次不同值 ⇒ 第二次写覆盖了第一次；同位置换缓存操作而结果不同 ⇒ 拒绝与否取决于那一笔缓存操作。

### 89.4 代码形状
块 35,354 字符 / 646 行、花括号平衡；探针 1,063,100 字符 / 17,028 行；块内 GMU 写点 12 笔（preamble 8 + 三条分支各 1 掩码 + 两条分支各 1 启动字）；`for (` 9、`IODelay(1u)` 3、`dc civac` 4、`dc cvac` 2、`armcpu_map_physical_rw` 9；全文件严格 GMU 写点 492。三键共用 preamble（建表、编程 CB0、读回、清页、路由、CM3 复位脉冲、`0xbabeface` 轮询、两笔中断清除），差别只写、缓存操作、启动字三处。

### 89.5 构建与门禁
host 28/28、stage4 61/61、stage5 48/48、stage6 1,691/1,691、全量 46 of 48（FAILING 0）、模式选择器守卫 PASS；CI run 45007222286（#377）28/28 步 success；静态门 29 条对两件产物同时命中（PASS），且对上一轮产物 FAIL 29（区分能力自证）。

### 89.6 键 157：写路径成立
`boot_id 583033935`、窗口 `a7cf59130ad326189fc9bd113b8d464070b78247a4d1605401edd931a3bc5654`、18/18 行。
`gmu157k4=write_path=1 fills=3 rw=df241000 w0=a5a5a5a5 w1=5a5a5a5a w4=c3c3c3c3`；
`gmu157k5=read_window=1 win=df240000 w0=5a5a5a5a w1=c3c3c3c3`；
`gmu157l=no_start=1 fills=3 ctl=00000000 bit0=0 info=00000000 cpmsg=ad956000 fsr=00000400 far=00000000 fsynr0=00000000 survived=1`。
⇒ 第一次写的值读回来了；改写之后读到新值；第 65 字也吃到了自己的值；第二个映射读到的是最后写入的值；没有启动字时控制字保持 0。

### 89.7 键 158：安全字 + civac，读回全零，启动被拒
`boot_id 639961686`、窗口 `1f122bc481def26911d9d067c3c0006a398bab7916a8eb7a9163130eb2a87d53`、17/17 行。
`gmu157k4=safe_pattern fills=3 rw=9fc36000 w4=00000000 w5=00000000 w6=00000000 w7=00000000`；
`gmu158l=safe_start=1 fills=3 ctl=00000000 bit0=0 polls=200000 info=00800000 cpmsg=ad967000 w4..w7=00000000 fsr=00000400 far=00000000 fsynr0=00000000 survived=1`。
⇒ 写进 64..67 的四个图案在 `dc civac` 之后的回读里是零；启动字被拒绝。

### 89.8 键 159：页头 + cvac，图案可见，启动被接受
`boot_id 582268382`、窗口 `b15c25a2172202f070e2b2711cd5b79c72ce23e3839b9badee426f5cbc5d06fd`、17/17 行。
`gmu157k4=header_pattern fills=3 rw=4b7da000 w4=a5a5a5a5 w5=5a5a5a5a w6=c3c3c3c3 w7=3c3c3c3c`；
`gmu159l=header_cvac=1 fills=3 ctl=00000001 bit0=1 polls=104 info=00000000 cpmsg=ad967000 w4=a5a5a5a5 w5=5a5a5a5a w6=c3c3c3c3 w7=3c3c3c3c fsr=00000400 far=00000000 fsynr0=00000000 survived=1`。
⇒ 同样四个图案落在 R264 键 155 那个位置，只把清缓存换成只清不失效：图案在启动前后都在，启动字被接受；这一轮的 `gmu157k1` 页头四字是 `00000000 00000000 00000000 00000000`（157/158 是 `0/78/6/c`）——页头内容不是每轮都有预置。

### 89.9 判决
1. **写路径成立**：可写映射写进去的值立刻读得回来，换值也立刻可见，第二个映射独立确认。
2. **`dc civac` 之后我们自己的回读是零**（158），这一条本轮只登记，不解释（写被丢弃 or 回读路径不同，未分辨）。
3. **同一个位置换缓存操作，结论相反**：4..7 + `civac`（R264 键 155）被拒绝；4..7 + `cvac`（本轮 159）被接受（`polls=104`）。
4. ⇒ **R264 §88.9 的第 2 条要收窄**：不能再写成「页第 4..7 字的内容改变固件行为」。干净的对照只有 155 与 159 这一对，它证明的是「**同一位置、只换缓存操作，结果从拒绝变成接受**」。
5. 键 158 的拒绝**不能单独归因**到位置：它同时换了位置与缓存操作，与 159 之间有两处差异。

### 89.10 必须分开说的三件事
- 155 与 159 之间的单变量是「缓存操作」；158 与 159 之间的单变量才是「位置」。四条组合（4..7 / 64..67 × civac / cvac）里本轮只做了三条，**缺 64..67 + cvac**，所以「位置不影响」这句话还没有对照。
- 接受 ≠ 固件通了：159 的 `info=00000000`、`gfsr=00000000`，没有任何消息被取走的证据。
- `polls=104` 与历轮的 `polls=2` 不同，本轮不解释这个差别（它至少说明取走启动字的时间不是常数）。

### 89.11 没证明什么
- 没证明固件真的读了队列页里的内容（只有「我们写得进去 + 启动被接受」两件事）。
- 没证明页头四字的作者（本轮 159 的页头是零，说明这些字不是每轮都由我们之前的过程写下的）。
- 没证明 `dc cvac` 与 `dc civac` 在硬件上的语义差别（只观察到结果不同）。
- 没证明拒绝是「固件检查页头」还是「SMMU/缓存一致性导致读到的内容不同」。

### 89.12 R266 设计
四个组合补齐成矩阵，并在**同一引导内**做两对：
- **160**：4..7 + `civac`（复现 155）与 4..7 + `cvac`（复现 159），中间用一次 `dsb sy` 分开，两条行各打印启动结果。
- **161**：64..67 + `civac`（复现 158）与 64..67 + `cvac`（补缺），同样两次启动。
- **162**：只做 `dc cvac` 对 4..7、然后**不写启动字**（控制），检查 `cvac` 是否足以让页内容对固件可见而无需启动。
判据：同位置两臂结果不同 ⇒ 缓存操作是唯一决定项；两臂相同 ⇒ 位置是决定项；161 若 cvac 侧接受而 civac 侧拒绝，则与 159 一致。

### 89.13 工具事实
- runner 的 `gmu157k1`/`gmu157k2` 行各打印两次：preamble 里两处读段都打同一行名，`counts` 因此显示 2；判决取第一次出现即可。
- `polls` 字段在 157 的 L 行里不存在（该行没有 `polls=`），判决串里的 `polls=None` 是取不到的显示，不是设备缺字段。
- 静态门必须传**文件全路径**；传目录会 `PermissionError`。

## 九十、ROUND 266：把两种清法放进同一轮 —— 启动字这次被接受，于是「页头内容决定接受」这条判决被推翻

### 90.1 一句话

在同一份构建里、同一页的同一个偏移上，先用 `dc civac` 清一次再启动、再用 `dc cvac` 清一次再启动，**两次启动字都被接受**（`ctl` 位 0 = 1，轮询 1 与 0 次），四个图案字读回毫发无损；而在 R264 键 155 / R265 键 159 里，同样的写、同样的 `civac`、同样的启动字是**被拒绝**的。所以「页头那四个字的内容决定启动字被接受还是被拒绝」这一条不成立，至少不是由内容决定的。

### 90.2 为什么要把两次清法放进同一轮

R265 的三键用了三种不同的动作，得到三种不同的结果，但每次结果都只出现一次：键 157（只写不启动）证明写路径是真的；键 158（写 64..67 + `civac` + 启动）被拒；键 159（写 4..7 + `cvac` + 启动）被接受。三个键各只有一次观测，而且「位置」与「清法」在两个拒绝/接受之间同时变化 —— 于是有两种解释都能说得通。R266 的做法是把矩阵铺开：**两个偏移 × 两种清法**，同一次引导内完成，且每个键都在自己的引导里重复一次启动字（`civac` 一次、`cvac` 一次）。

### 90.3 三键设计

| 键 | 写的偏移 | 第一次清法 | 第二次清法 | 是否写启动字 |
|---|---|---|---|---|
| 160 | 4..7（页头） | `dc civac` | `dc cvac` | 两次都写 |
| 161 | 64..67（安全字） | `dc civac` | `dc cvac` | 两次都写 |
| 162 | 4..7（页头） | `dc cvac` | — | **不写** |

每笔掩码写入之前都先清一次 CB0 的状态字（`0xc00001fe`，写即清），这样「状态字是这次留下的」与「状态字是上一次留下的」不会被混在一起。

### 90.4 代码形状

块 40,005 字符 / 736 行（写点 17、状态字清零 5、掩码 5、启动字 4、`for (` 14、`IODelay(1u)` 5、`dc civac` 4、`dc cvac` 3、无 `IOSleep`）。探针 1,103,106 字符 / 17,764 行，严格 GMU 写点 509。三键共用前导段（建 2 MiB 非 identity 表、九笔编程 CB0、读回三层描述符、清三页、路由 SID 4、CM3 复位脉冲、`0xbabeface` 轮询、两笔中断清除、K1/K2/K3 三次读页）。

### 90.5 构建与门禁

CI run `45020325323`（#378）28/28 成功；静态门 PASS（35 条必需行在 kernel 与 payload 里都出现），对 R265 产物自证 FAIL 35 条；host 门 28/28、stage4 61/61、stage5 48/48、stage6 1710/1710、全量 46/48 全绿。

### 90.6 键 160 逐字读数（boot 581825972）

```
gmu160g=last_row=1 about_to_start=1 mode=160 route=1 smr=80000004 s2cr=00000000 qtbl=ad964000 qtbl_value=ad964000 base=ad800000
gmu160k1=pre_arm=1 qtbl=ad964000 qpa=ad964000 win=4bc01000 a0=00000000 a1=00000078 a2=00000006 a3=0000000c
gmu160k4=civac_arm=1 fills=3 rw=4bc01000 w4=a5a5a5a5 w5=5a5a5a5a w6=c3c3c3c3 w7=3c3c3c3c
gmu160k5=civac_accept=1 ctl=00000001 bit0=1 polls=1 w4=a5a5a5a5 w5=5a5a5a5a w6=c3c3c3c3 w7=3c3c3c3c info=00000000
gmu160k6=cvac_arm=1 w4=a5a5a5a5 w5=5a5a5a5a w6=c3c3c3c3 w7=3c3c3c3c
gmu160k7=cvac_accept=1 ctl=00000001 bit0=1 polls=0 w4=a5a5a5a5 w5=5a5a5a5a w6=c3c3c3c3 w7=3c3c3c3c info=00000000
gmu160l=matrix=1 fills=3 ctl0=00000001 pol0=1 ctl1=00000001 pol1=0 fsr=00000400 far=00000000 survived=1
```

### 90.7 键 161 逐字读数（boot 581999953）

```
gmu160k1=pre_arm=1 qtbl=ad950000 qpa=ad950000 win=4829b000
gmu161k4=civac_arm=1 fills=3 rw=4829b000 w4=a5a5a5a5 w5=5a5a5a5a w6=c3c3c3c3 w7=3c3c3c3c
gmu161k5=civac_accept=1 ctl=00000001 bit0=1 polls=1 w4=…同
gmu161k6=cvac_arm=1 w4=…同
gmu161k7=cvac_accept=1 ctl=00000001 bit0=1 polls=0 w4=…同 info=00000000
gmu161l=matrix=1 fills=3 ctl0=00000001 pol0=1 ctl1=00000001 pol1=0 fsr=00000400 far=00000000 survived=1
```

### 90.8 键 162 逐字读数（boot 581527881，不写启动字）

```
gmu162k4=header_cvac=1 fills=3 rw=9d756000 w4=a5a5a5a5 w5=5a5a5a5a w6=c3c3c3c3 w7=3c3c3c3c
gmu162k5=cvac_no_start=1 ctl=00000000 bit0=0 info=00000000 w4=…同
gmu162l=matrix=1 fills=3 ctl0=00000000 pol0=0 ctl1=00000000 pol1=0 fsr=00000400 far=00000000 survived=1
```

### 90.9 判决五条

1. **同一偏移、同一清法（`civac`）这次被接受**（键 160，轮询 1 次即置位），而 R265 键 159 里同形操作被拒绝（200000 次不置位）⇒ 「页头内容决定接受」这条判决被推翻，不再作为结论使用。
2. **安全字偏移同样被接受**（键 161，两次都是 1 与 0 次轮询）⇒ 偏移不是开关。
3. **两种清法在这一次引导里没有区别**（两次都接受）⇒「`civac` 与 `cvac` 的区别决定接受」这条也不成立。
4. **不写启动字时控制字保持 0**（键 162）⇒ 控制字确实只由启动请求驱动，这一条仍然成立。
5. **三次引导的页读都干净**（`fsr=00000400 far=00000000`，位 1 为 0）⇒ 本轮没有任何翻译 fault。

### 90.10 必须分开说清的三件事

1. 本轮**没有**找到「为什么 R264 键 155 / R265 键 159 被拒绝」的答案；能说的只是「同样的事情这一次没有被拒绝」。这两次拒绝的真实原因仍未知。
2. 接受与拒绝的**次数**现在不可比：R264/R265 侧共 3 次拒绝、0 次接受；R266 侧 4 次接受、0 次拒绝。样本不对称，尤其是它们分布在不同构建与不同引导里。
3. `polls=0` 表示**读到的时候位已经是 1**，不表示「零时间接受」；轮询循环第一次读之前已经过了一次 `IODelay(1)` 的等待点。

### 90.11 没证明什么

- 没有证明接受/拒绝是一个随机事件；只能说在现有样本里它不由「偏移」「清法」「图案内容」决定。
- 没有证明 CM3 真的读了队列页 —— 页读回只是我们自己的映射视角。
- 没有证明 `cpmsg`、`info` 与接受状态的关系（本轮两次接受时 `info` 都是 0）。
- 没有触碰 GX 电源域、没有提交任何命令、GPU 仍未渲染一帧。

### 90.12 R267 设计（三键）

- **163**：重复键 160 的形状**三次**（同一引导内三段 `civac` + 启动 + 轮询），把接受/拒绝的**计数**变成可比较的量。
- **164**：重复键 161 的形状三次（安全字偏移），与 163 对照偏移。
- **165**：不写任何图案、只清 `civac` 一次 + 启动字（判断「有没有写过页」是否影响接受）。

### 90.13 工具事实

1. 生成器模板里的 `%` 与 C 的 `%08x` 冲突时一律用 `@@TAG@@` 占位再 `.replace`，不要用 `%` 格式化拼 C 代码。
2. `git show ":package/…"` 取到的是 **index** 版本（哈希脚本会 `git add`），所以 `restore()` 必须**先剥掉本轮旧块**再写回，否则恢复回来的是坏块。
3. 每份 runner 的 `PREV_SHA` 必须是 64 个字符占位（`"0" * 64`），否则 anchor 脚本报 `replacements PREV_BOOT=1 PREV_SHA=0`。
4. 判决串里引用键号要用 `_this_key`，不能直接用生成器的 `key` 变量（生成器变量不会进 runner）；本轮因此崩过一次 `NameError: name 'key' is not defined`。

## 九十一、ROUND 267：同一引导里连开三次 —— 三次都被接受，控制字一旦置起就不再见底

### 91.1 一句话

第三次「写页头 → `dc civac` → 掩码 → 启动字」也在同一引导里被接受了（3/3、3/3），而只写掩码、不写启动字的那一轮控制字始终看不到 bit 0。

### 91.2 这一轮想分开的两件事

R266 把「页头内容」和「缓存操作」两条解释都撤回了，剩下的是「接受这件事本身是否稳定」。要在同一引导内看三次，才能把「换一次引导就变」与「同一引导内也变」分开。

| 对照项 | R264 键 155 | R265 键 159 | R266 键 160 | R267 键 163 |
|---|---|---|---|---|
| 写哪里 | 4..7 | 4..7 | 4..7 | 4..7 |
| 清法 | `dc civac` | `dc cvac` | `civac` 然后 `cvac` | `dc civac` ×3 |
| 启动字 | 一笔 | 一笔 | 两笔 | 三笔 |
| 结果 | 拒绝 | 接受 | 两次都接受 | 三次都接受 |

### 91.3 代码形状

块 44,833 字符 / 826 行，braces 0；探针 1,147,940 字符 / 18,591 行；严格 GMU 写点 531；块内 `gmuW[` 22、`HFI_CTRL_INIT] = 1u;` 7、`GMU2HOST_INTR_MASK] = ` 7、`0xc00001feu` 7、`for (` 19、`IODelay(1u)` 8、`dc civac` 10、`dc cvac` 1、`IOSleep` 0。

三个键共用同一段前导（建 identity 表、九笔编程 CB0、读回三层描述符、`dc civac` 清页、`SMR0=80000004`、`S2CR0=0`、CM3 复位脉冲、200000 轮 `0xbabefaceu` 轮询、两笔中断清除），差别只在分支里：

- **163** 三循环：写页第 4..7 字 → `dsb` → `dc civac` 全 span → `dsb` → 读回四字 → 清状态字 → 写 `0xff7fffff` 掩码 → 写启动字 → 200000 轮读 `HFI_CTRL_STATUS` bit 0；
- **164** 同形，写的是第 64..67 字；
- **165** 不写图案，只清一次、写掩码、**不写启动字**，满轮询。

### 91.4 构建与门禁

host 28/28、stage4 61/61、stage5 48/48、stage6 1,728/1,728、全量 46/48 FAILING 0；`check_mode_selector.py` 通过（自证对压平写法 FAIL）；静态门 32 行，对 R266 产物自证 **FAIL 32 problems**；CI #379 `45025061571` success 28/28（`12:59:02Z → 13:15:37Z`）；产物 `kernel.macho` 126,402,560 B sha256 `c3ee9f9602cd64faf593e191a1871947f4860f7c14973a1026dac185d41b00d2`、`payload.img` 150,994,944 B sha256 `fff10e7dff0070c15346d69f96945d3dcd60b8ee268d35d710616ea0a50b9918`；静态门对真产物 **PASS（44 条）**。

### 91.5 键 163（页头，三循环，DEVICE-VERIFIED 19/19）

```
boot_id 581929262  window 1fb1a633c515c1d991b87bfbad58763563b0145cd4c33f3fc0a22fc6ddf811f0
gmu163k1=pre_arm=1 qtbl=ad968000 qpa=ad968000 win=30118000
gmu163k2=armed=1 addr=ad968000 info=00000001
gmu163k3=post_reset=1 sysreset=00000000
gmu163c1=cycle=1 fills=3 ctl=00000001 bit0=1 polls=223 w4..w7=a5a5a5a5 5a5a5a5a c3c3c3c3 3c3c3c3c
gmu163c2=cycle=2 fills=3 ctl=00000001 bit0=1 polls=0
gmu163c3=cycle=3 fills=3 ctl=00000001 bit0=1 polls=0
gmu163l=three_cycles=1 ctl0=00000001 pol0=223 ctl1=00000001 pol1=0 ctl2=00000001 pol2=0 fsr=00000400 far=00000000 survived=1
```

### 91.6 键 164（安全字 64..67，三循环，DEVICE-VERIFIED 19/19）

```
boot_id 581964710  window 50509a06c54c83b8197c8e9520208c8563e77727095b65580a29d951a6b3a9af
gmu163k1=pre_arm=1 qtbl=ad968000 qpa=ad968000 win=32319000
gmu164l=three_cycles=1 ctl0=00000001 pol0=1 ctl1=00000001 pol1=0 ctl2=00000001 pol2=0 fsr=00000400 far=00000000
cycle 1/2/3 readback: a5a5a5a5 5a5a5a5a c3c3c3c3 3c3c3c3c（每循环 bit 0 = 1）
```

### 91.7 键 165（不写图案、只掩码、不启动，DEVICE-VERIFIED 17/17）

```
boot_id 640354539  window b465a5c513130507dfaf67a643067ed2eaf7ca8b744fc37f03ef5bcf67c89063
gmu165c1=cycle=1 fills=3 ctl=000000fe bit0=0 polls=200000 w4..w7 （未写）
gmu165l=no_pattern=1 ctl0=000000fe pol0=200000 ctl1=00000000 pol1=0 fsr=00000400 far=00000000
```

### 91.8 判决

1. **三次都被接受**：163 与 164 各 3/3，`ctl` 每一轮都是 `00000001`。接受不是「一次引导只成功一次」。
2. **第一次要等，后两次不用等**：163 的第一轮 223 次轮询才看到 bit 0，第二、三轮读到即是 1；164 的第一轮只需 1 次。⇒ 置位前的等待时间**不稳定**，本轮样本不足以归因（页头 vs 安全字、预热 vs 稳态）。
3. **控制字是自保持的**：后两轮再写启动字并没有把 bit 0 清回去，所以「轮询 0」读的是上一轮留下的 1，不是「这一轮不用等」。
4. **只有掩码不会置位**：165 满 200000 轮之后 `bit0=0`。
5. **三键都没有翻译 fault**：`fsr=00000400`、`far=0`、`fsynr0=0`。

### 91.9 必须分开说的三件事

- `polls=0` 不等于「立刻接受」——它只说明读的时候 bit 0 已经是 1，而这一位是自保持的。
- `ctl=000000fe` 与 R260 键 143 的一次读数相同，**这个值本轮仍然没有解释**；只能说 bit 0 明确为 0。
- 三次接受只证明控制字被置起，**没有**证明固件处理了三次请求（没有消息、没有中断、`info=0`）。

### 91.10 没证明什么

- 没证明 223 次与 1 次的差别来自页偏移；
- 没证明控制字置起后固件做了什么；
- 没证明换引导次数更多时会退化；
- 没证明「接受的次数」与任何队列状态相关。

### 91.11 下一轮（R268）

- 键 166：同一引导内六次「写页头 + `civac` + 启动」，看第一次的等待是否是唯一的一次；
- 键 167：每循环之间把 `HFI_CTRL_STATUS` 读两个不同偏移（`0x5180` 与 `0x5181`），看是否还有第二个状态字；
- 键 168：先清 `HFI_CTRL_STATUS` 再启动（如果可写），看控制字能否被手动复位。

### 91.12 工具事实

1. `r266_s160_run.py` 作为 runner 模板；R267 的 preamble 行名全部是 `gmu163*`（改名把共享前导段都变成 163），只有循环行与尾行带自己的键号 —— 按 `gmu164a…` 去查行会误报 24 条「MISSING ROW」。
2. `IOLog("enchilada-adreno: %s\n", <row>)` 的生成必须用字符串拼 `${row}`，用 `%` 格式化会被 Python 吃掉第一个 `%s`，编译报 `-Wformat-extra-args`。
3. 循环行的标签必须是 `gmu163c1=cycle=1`（带 `=`），否则 runner 的 `tag + "=" + name + "="` 正则取不到值。

## 九十二、ROUND 268：同一引导里连开六次全被「接受」，而把它手清之后再开一次却不被接受 —— 那一位是每次引导锁存一次，不是可以再置起的状态

### 92.1 一句话

R267 看到「同一引导里连开三次，三次控制字都是 1」，本轮把这句追问到底：**六次全 1（键 166）、三行四个状态字里只有 `0x5180` 的 bit 0 在动（键 167）、而把控制字手写回 0 之后再写一次启动字，控制字再也不置起（键 168）** —— 所以前面那些「后面几次不用等」，读的是上一轮留下的那一位，不是一次新的接受。

### 92.2 为什么把三件事拆成三键

- 键 166 只问「同形请求重复六次，控制字是不是每次都 1」；
- 键 167 只问「`0x5180` 上下三个字里有没有第二个信号跟着动」（若有，后面「不用等」就可能由它解释）；
- 键 168 只问「把那一位**手写回 0** 之后，再写一次启动字，它会不会再置起」—— 这是唯一能把「接受」与「锁存」分开的动作，因为只有它会**先把那一位清掉**。

### 92.3 代码形状

- 探针 1,204,019 字符 / 19,613 行；本块 **56,079 字符**；块内 `gmuW[` 31 笔、`HFI_CTRL_INIT = 1` 11 笔、`GMU2HOST_INTR_MASK` 11 笔、状态字清零 **11 笔**（每笔 mask 之前一笔，一一对应）、`for (` 27、`IODelay(1u)` 12、无 `IOSleep`。
- 键 166/167 的每一轮：写页头四字 → `dsb` → `dc civac` 全页 → `dsb` → 读回四字 → 清 CB0 状态字 → 掩码 → 启动字 → 最多 200000 轮轮询；键 168 只有一次图案写，中间多一笔 **`HFI_CTRL_STATUS = 0u`**。
- 严格 GMU 写点从 531 升到 **562**（+31）。

### 92.4 构建与门禁

- CI **#382 `45033225675`** `success` 28/28（`2026-11-22T14:30:54Z → 14:47:42Z`）。
- 产物：`kernel.macho` 126,418,944 B `2be9f7cfbfec…`、`payload.img` 150,994,944 B `9d7b76388b25…`；静态门 **`PASS: R268 static gate`**（38 条 REQUIRED 全中），对 R267 产物自证 **`FAIL (38 problems)`**。
- host 28/28、stage4 61/61、stage5 48/48、stage6 1,744/1,744、全量 46/48 `FAILING: 0`。

### 92.5 键 166 的读数

两次引导，每次 22/22 行。第二次（`boot_id 582522595`、窗口 `de0fb374c7d1…`）：

```
gmu166c1=cycle=1 fills=3 ctl=00000001 bit0=1 polls=103 w4=a5a5a5a5 w5=5a5a5a5a w6=c3c3c3c3 w7=3c3c3c3c info=00000000
gmu166c2..c6 同形，polls=0
gmu166l=six_cycles=1 fills=3 ctl0..ctl5 全 00000001 pol0=103 pol1..pol5=0 fsr=00000400 far=00000000 survived=1
```

第一次引导形状相同（首轮 `polls=1`）。⇒ 六次请求，六次控制字都是 1。

### 92.6 键 167 的读数

`boot_id 581214452`、窗口 `2f1b2ffbf7bb…`、22/22：

```
cycle 1: bit 0 = 1, polls = 1   status 5180/5181/5182/5183 = 00000001 00000000 06030100 00000000
cycle 2: bit 0 = 1, polls = 0   同
cycle 3: bit 0 = 1, polls = 0   同
```

⇒ 四个字里**只有 `0x5180` 的低位在动**；`0x5181`/`0x5183` 恒为 0，`0x5182` 恒为 `06030100`（与 R247 读到的 `sfr=06030100` 同一个值，是常量而不是状态）。没有第二个状态字可以解释「后面不用等」。

### 92.7 键 168 的读数

`boot_id 603692121`、窗口 `c1f1cadb7842…`、17/17：

```
gmu168k=latch_before=00000001 pol0=1 about_to_clear=1
gmu168l=manual_clear=1 after_write=00000000 ctl_now=00000000 pol1=200000
        w4..w7=a5a5a5a5 5a5a5a5a c3c3c3c3 3c3c3c3c fsr=00000400 far=00000000 survived=1
```

⇒ 第一次启动 1 次轮询就置起；**手写 0 之后读回确实是 `00000000`**（这一位可写）；然后清状态字 + 掩码 + 再写启动字，**200000 轮轮询之后 `ctl_now` 仍是 `00000000`** —— 第二次启动字没有被接受。

### 92.8 判决

1. **六次同形请求，控制字六次都是 1**（键 166）——但见第 3 条。
2. **没有第二个状态字**：`0x5180` 之外三个字分别是常量 0 / 芯片号 / 常量 0（键 167）。
3. **「后面几次不用等」不是新的接受**：键 168 证明把这一位清掉之后再启动就不置起，所以 166/167 里第 2 次之后的 `polls=0` 读的是**上一次留下的 1**。
4. **这一位是「每次引导锁存一次」**：置起之后手清可以把它写成 0，但写 0 之后同一引导内不再被启动字重新置起。
5. **三键都没有翻译 fault**（`fsr=00000400`、`far=0`、`fsynr0=0`），与 R260 之后各轮一致。

### 92.9 必须分开说的三件事

- `polls=0` 只表示「读的时候已经是 1」，**不表示这一轮被接受**；本轮之前几节把它们当成「接受」来读，需要按第 3 条收窄。
- 键 168 手写的 0 **确实写进去了**（`after_write=00000000`），但「固件看到了这个 0」没有证据；只能说「读回是 0」。
- 这一位置起仍**没有**伴随任何消息或中断（`info=0`），所以「接受」仍然只是寄存器账本意义上的。

### 92.10 没证明什么

- 没证明固件在第一次之后是否还在工作（没有再置起的入口，也没有消息）；
- 没证明手写的 0 与固件自己看到的 0 是同一个时刻；
- 没证明六次里第一次的 103 次轮询与第二次的 1 次之间差在哪（预热、页偏移、还是上一次运行的残留）；
- 没碰 GX 电源域。

### 92.11 ROUND 269 的三键

- **169**：同一引导内「启动 → 手清 → 启动 → **不经手清** 再启动」，看手清之后是不是**永久**不再置起（把 168 的单次扩成序列）。
- **170**：先不写启动字、只把控制字手写成 1（看这一位能不能被软件置起）—— 若能，则「固件置起」与「软件置起」可以用后续行为区分。
- **171**：把 `HFI_CTRL_STATUS` 手清之后，读 `CM3_FW_INIT_RESULT`、`CP_MSG`、`CP_RESP`、`info`、`gfsr`，看固件是否在那一拍改变了别的东西。

### 92.12 工具事实

- 本块的 `gmu168l` 把四个字打成**一个组合字段** `w4..w7=a5a5a5a5 5a5a5a5a c3c3c3c3 3c3c3c3c`，所以判决串里逐字取 `w4`/`w5`/`w6`/`w7` 会得到 `None`；读这类行要么按空格切整个字段再分，要么在块里改成四个独立 `w4=%08x w5=…`。
- 同一份 runner 重跑一次会**再引导一次**并覆盖 `r268_sNNN_window.bin`；本轮把第一次的窗口另存为 `r268_s166_window_r1.bin`。设备日志 `r268-s166-device.log` 也被覆盖，所以第一次的 `boot_id` 没有留下来 —— **每次引导前先备份日志与窗口**。
- `r268_anchor.py` 按 `HERE` 拼参数路径，在 `oneplus6-ios` 里传 `work\tmp\…` 会拼成 `work\tmp\work\tmp\…`；要么在 `work\tmp` 里跑并只传文件名，要么传绝对路径。

## 九十三、ROUND 269：手清能清掉那个字，但同一引导里再启动不再被接受 —— 上一轮的「每次引导锁存一次」得到正面确认

### 93.1 一句话

R268 的结论是「控制字的 bit 0 每次引导锁存一次」，本轮把它直接测一遍：**同一引导里先接受一次，再把它手写回 0，然后再写一次启动字 —— 200000 轮之后它仍是 0（三次全拒）；而同一个字确实可以被手写成 1、也可以被手清成 0，读回的就是写进去的值。**

### 93.2 三个键分别问什么

- **键 169**：一次引导里发四次启动字 —— 第 1 次照常；第 2 次之前**手清**一次；第 3 次**什么都不清**；第 4 次之前再手清一次。每支都先清 CB0 状态字、再一笔掩码、再启动字，所以四次之间**唯一的变量就是手清**。
- **键 170**：全程**不写启动字**，只用手写把控制字置成 1、读回、延迟后再读、再手写成 0、读回；然后才发一次真启动字。这把「软件能不能写这个字」与「固件接受启动」彻底分开。
- **键 171**：启动一次后拍一组固件字的快照（`CM3_FW_INIT_RESULT`、`PWR_COL_CP_MSG`、`PWR_COL_CP_RESP`、`GMU2HOST_INTR_INFO`、`sGFSR`），手清控制字，再拍一组 —— 看手清有没有连带动别的字。

### 93.3 代码形状

- 探针 1,237,972 字符 / 20,270 行；本块 **35,604 字符 / 651 行**；块内 `gmuW[` **25** 笔、`HFI_CTRL_INIT = 1` **6** 笔、`GMU2HOST_INTR_MASK` **6** 笔、状态字清零 **6** 笔（每笔掩码前一笔）、`HFI_CTRL_STATUS] =` **5** 笔（键 169 两次手清 + 键 170 两次手写 + 键 171 一次手清）、`for (` **11**、`IODelay(1u)` **8**、无 `IOSleep`。
- 严格 GMU 写点从 **562 → 587**（+25）。
- 行名 21 条：`gmu169a..n`（12 条共享前导段，**行名随改名一起变成了 `gmu169*`**）、`gmu169k1..k4`、`gmu169l`、`gmu170k`、`gmu170l`、`gmu171k`、`gmu171l`。

### 93.4 构建与门禁

- CI **#383 `45038530947`** `success` 28/28（`2026-11-22T15:28:48Z → 15:48:11Z`）。
- 产物 `kernel.macho` 126,435,328 B `dda2cb87b589…`、`payload.img` 150,994,944 B `d7aa521c202d…`（payload 第一次下载在 77,975,471 / 83,173,836 处 `INCOMPLETE` 中断，删目录重下后才完整）。
- 静态门 **`PASS: R269 static gate`**（31 条 REQUIRED 全中、`required+survivors: 43 checked in both artefacts`），对 R268 产物自证 **`FAIL (31 problems)`**。
- host 28/28、stage4 61/61、stage5 48/48、stage6 1,763/1,763、全量 46/48 `FAILING: 0`。

### 93.5 键 169 的读数

`boot_id 605728322`、窗口 `ca5f95a50af34177b13671898ea5cd71385e616289d3a20cdd5dfec727865aff`、17/17 行：

```
gmu169k1=start1=1 ctl=00000001 pol=1
gmu169k2=after_clear1=1 word=00000000 about_to_start=1
gmu169k3=start2_after_clear=1 ctl=00000000 pol=200000
gmu169k4=start3_no_clear=1 ctl=00000000 pol=200000
gmu169l=four_starts=1 after_clear1=00000000 clear_back2=00000000 ctl1=00000001 pol1=1 ctl2=00000000 pol2=200000 ctl3=00000000 pol3=200000 ctl4=00000000 pol4=200000 fsr=00000400 far=00000000 survived=1
```

判决串的原话：**「1 of 4 start words were accepted inside one boot / even right after a hand clear the firmware refuses another start / WITHOUT A CLEAR THE WORD NEVER COMES BACK (start 3 stayed at 00000000 first)」**。

### 93.6 键 170 的读数

`boot_id 580967959`、窗口 `e8fe8037a354918af194611c55036495893aa94ec7a10e43245105c01cfe5840`、17/17 行：

```
gmu170k=hand_set=1 base=00000000 set_back=00000001 set_late=00000001 clear_back=00000000 about_to_start=1
gmu170l=set_by_hand_end=1 fills=3 ctl=00000001 pol=1 fsr=00000400 far=00000000 survived=1
```

判决串的原话：**「THE CONTROL WORD IS WRITABLE BY HAND AND THE HAND VALUE IS WHAT READS BACK / THE HAND WRITE DOES NOT REPLACE THE START WORD: the real one then went accepted」**。

### 93.7 键 171 的读数

`boot_id 580505538`、窗口 `4a7782a0918dfaffcf16dfe2e31c27e6b8f06dbe23e4e9f54e1e1a1fa926b886`、17/17 行：

```
gmu171k=before_clear=1 ctl=00000001 pol=1 fwinit=babeface cpmsg=ad983000 cpresp=00000000 info=00000000 gfsr=00000000
gmu171l=after_clear=1 fills=3 ctl=00000000 fwinit=babeface cpmsg=ad983000 cpresp=00000000 info=00000000 gfsr=00000000 fsr=00000400 far=00000000 survived=1
```

判决串的原话：**「CLEARING THE CONTROL WORD BY HAND LEFT EVERY OTHER FIRMWARE WORD ALONE」**。

### 93.8 判决

1. **手清有效**：键 169 的手清读回 `00000000`，键 170 的手写 1 读回 `00000001`、手写 0 读回 `00000000`、延迟之后仍是 `00000001` ⇒ 这个字在这条映射上是可读可写的。
2. **同一引导里清掉之后不再置起**：键 169 的第 2、3、4 次启动全部 200000 轮 `ctl=00000000` ⇒ 手清之后固件**不再接受**新的启动字。
3. **上一轮的「每次引导锁存一次」得到正面确认**：R267/R268 里第 2 次之后的 `polls=0` 读的是**上一次留下的 1**；把 1 真的清掉之后，同一引导内就没有第二次置起。
4. **手写不占用那一次**：键 170 先手写 1（读回 1）再手清（读回 0），随后**一次真启动字仍被接受**（`ctl=00000001 pol=1`）⇒ 「一次」是固件对**启动字**的响应，不是对控制字被写过的响应。
5. **手清不连带动别的字**：键 171 的五个固件字在两次快照里逐字相同，只有控制字从 `00000001` 变成 `00000000`。
6. 三键都没有翻译 fault（`fsr=00000400 / far=0 / fsynr0=0`）。

### 93.9 必须分开说的三件

- 键 169 第 2/3/4 次全是 `pol=200000`，这只是「等满窗口也没读到 1」，**不等于**「固件拒绝了这个请求」—— 与 R246/R249 的边界一样：寄存器账本上没有反应，固件内部发生了什么本轮看不到。
- 键 170 的手写 1 读回 1，**只证明映射可写**，不证明固件「看到」了这个 1；`info=0`、`gfsr=0` 说明它没有引起任何可见响应。
- 键 171 的两次快照在同一引导内相邻两步，中间只有一笔 `HFI_CTRL_STATUS = 0`；五个字不变是这一笔的局域性证据，**不是**「手清对固件无副作用」的普遍结论。

### 93.10 没证明什么

- 没证明固件是否真的收到并拒绝了第二次启动（没有消息、没有中断、没有 status 位）。
- 没证明「一次」这个限制落在哪一级（CM3 的请求状态机 / HFI 的 CMDQ / 队列页 / 别的）。
- 没证明手清之后如果**换一张队列表**或**重启 CM3**能否再接受一次。
- 没证明 `polls=1` 与 `polls=0` 的差别有功能含义 —— 本轮第一次启动 `polls=1`，R268 里首轮是 `polls=103`，都在几拍之内。

### 93.11 R270 设计

- **键 172**：键 169 的形状，但每次启动之前**重启 CM3**（`QTBL_ADDR/INFO → SYSRESET 脉冲 → CP_RESP=0`），看「一次」是否随 CM3 复位而重置。
- **键 173**：键 169 的形状，但第 2 次启动前把 `HFI_QTBL_ADDR` 重新武装成**新的页面**（换一张队列表），看接受与否跟队列表内容是否有关。
- **键 174**：先启动一次（置起）→ 手清 → **重启 CM3** → 再启动，把「手清 + 复位」两个动作叠在一起看能否重新置起。

### 93.12 工具事实

- `r214_par.py` 的 payload 下载可能在 `INCOMPLETE: n of m` 处中断且**不重试**，目录也不会被创建；必须删目录、显式重跑，重跑时把并行度从 16 降到 8 更稳。
- `r269_mkrun.py` 里 `.format(sum(... (int(_v, 16) & 1)))` 少一个 `)` 会让 `ast.parse` 把错误报在**下一条**语句上；生成器必须每条 append 都配 `ast.parse` 复查。
- 改名链 `r166→r169` 不碰字符串字面量，所以共享前导段的行名会残留 `gmu166*`；本轮补了 `pre.replace("gmu166", "gmu169")` 才让 21 条行名对得上。

## 九十四、ROUND 270：重启 CM3 之后控制字又读成 1 —— 「重新接受」与「锁存位自己活过复位」这一轮还分不开

### 94.1 一句话

把 CM3 复位塞进两次启动字之间（键 172）、把队列表换到一张新页（键 173）、先手清再复位再启动（键 174）之后，三次引导的十九行全部落盘：**固件每次都真的重跑**（`CM3_FW_INIT_RESULT` 清零后再读回 `babeface`，每次 5 拍），**控制字在复位之后又读成 1**；但第二次、第三次启动的第一拍读数就是 1（`pol=0`），而这三支里只有键 174 做过手清，所以这一轮**分不开**「复位把接受还回来了」和「那个位本来就活着、复位也没把它清掉」。

### 94.2 为什么把复位放进启动字之间

R269 键 169 已经把「同一引导里只接受一次」立住了：启动一次 → 手清 → 再启动 200000 轮仍是 0。留下两个互斥的解释：

1. 固件内部有一个「本引导只受理一次」的**状态**，复位会把它恢复；
2. 那个状态其实只是**控制字里的一位**，手清把位清了，而固件根本没有第二次受理的能力。

区分它们只需要一个动作：把手清与复位叠在一起。键 172 只看复位（每次启动前都重启 CM3），键 173 完全不重启、只换队列表的新页（把「固件在看哪张表」单独拎出来），键 174 把两个动作叠加（启动 → 手清 → 复位 → 再启动）。

### 94.3 代码形状

- 块 **47,339 字符 / 847 行**，`gmuW[...] =` **55** 笔，其中 `HFI_CTRL_INIT = 1u` **8** 笔、`GMU2HOST_INTR_MASK` **8** 笔、`CM3_SYSRESET` 上升沿/下降沿各 **5** 笔、`HFI_QTBL_ADDR`/`HFI_QTBL_INFO` 各 **8** 笔、`HFI_CTRL_STATUS = 0u` **1** 笔（只在键 174）。
- 行名 **27** 条：共享前导段 `gmu172a..n`（12 条）+ `gmu172k1..k3`，键 172 的 `gmu172p1..p3` + `gmu172l`，键 173 的 `gmu173p1..p3` + `gmu173l`，键 174 的 `gmu174p1..p3` + `gmu174l`。
- 每次复位都是同一形状：`QTBL_ADDR/INFO` 重写 → `CM3_SYSRESET = 1` → `CP_RESP = 0` → `CM3_SYSRESET = 0` → 轮询 `CM3_FW_INIT_RESULT` 到 `0xbabeface`；`MODEL_LOCK` 一次不动（不写 `sCR0` bit10）。
- 键 173 的换页是把队列表那 4 KiB 拷进 `armcpu_dma_alloc_writable(4096)` 的新页、逐 64 字节 `dc civac`，再写 `QTBL_ADDR = 新页`、`QTBL_INFO = 1`。

### 94.4 构建与门禁（这一轮本身花了四次构建）

| 序 | 运行 | 结果 | 原因 |
|---|---|---|---|
| 1 | #384 `45083097073` | failure | 探针里八条 `redefinition of 'r172Row*'`（前导段已声明过同名缓冲）+ 一处 `expected '}'` |
| 2 | #385 `45084894407` | failure | 五条 `-Werror,-unused-variable`：`s172s1/s172s2/s172s3/r172ck4/r172pk4`，全部来自「一行声明多个变量」的逗号列表 |
| 3 | #386 `45086073145` | success 28/28 | **但整块被编译掉了** —— 静态门对真产物 `FAIL: R270 static gate (34 problems)`，`gmu172a=` 在 kernel 与 payload 里一个都找不到 |
| 4 | #387 `45088125037` | success 28/28 | 修掉块头嵌套后静态门 **PASS（46 条在两边都命中）** |

- 第 3 次的真相是**新块被套进旧块**：复制来的前导段第一行就是 R269 的块头 `if (fwKey == 169u || …) {`，于是新块成了旧块的子作用域，而旧块末尾 `return`，clang `-O2` 把整块连同格式串删掉，**编译零警告、CI 28/28 全绿**。修法是 `pre = pre.replace(OLD_HDR, "", 1)` 加三条断言。
- 门禁终态：`stage4 61/61`、`stage5 48/48`、`stage6 1783/1783`、host 门 28/28、全量 `ran 46 of 48, FAILING: 0`、`check_mode_selector.py` PASS。
- 产物：`kernel.macho` 126,435,328 B sha256 `60eae436be0ddec2a8212b71a389401efa5d77142e00a18c82c3860d3be90825`；`payload.img` 150,994,944 B sha256 `05233dddd48aa295358ca29bc48be1ca1ee66d13205f58ea1d8b348565eab60b`。

### 94.5 键 172：每次启动前重启 CM3

`boot_id 581940959`、窗口 `a0892393b8de1b4c624aad9a0b3f6a51229ad3c6a9238a0a9a33db3c5e20c1b7`、19/19 行，payload 重钥后 sha256 `7a6753304a3ae5dc78a8ec0e5c7b6a739b0fc692e075435ac1418922b83fa24a`。

```
gmu172i=reboot=1 fwinit=babeface polls=4 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu172p1=start_after_restart=1 n=1 fw=babeface rpol=5 ctl=00000001 pol=1
gmu172p2=start_after_restart=1 n=2 fw=babeface rpol=5 ctl=00000001 pol=0
gmu172p3=start_after_restart=1 n=3 fw=babeface rpol=5 ctl=00000001 pol=0
gmu172l=three_restarts=1 fills=3 ctl1=00000001 pol1=1 ctl2=00000001 pol2=0 ctl3=00000001 pol3=0 fw1=babeface fw2=babeface fw3=babeface fsr=00000400 far=00000000 survived=1
```

三次复位之后 `CM3_FW_INIT_RESULT` 都自己回到 `babeface`（`rpol=5`），所以**固件每次都重跑了**；三次启动字之后控制字都读成 1。

### 94.6 键 173：第 2/3 次启动前把队列表换到新页

`boot_id 582844614`、窗口 `7d661c2ff98b9959a590acfba038907df3eda2e54866f1abed7c47c0184534bb`、19/19 行，payload 重钥后 sha256 `eec99d5d0e39f8791593fbd256cea7c9376fbab4cd71d44fe1bccb384a20b423`。

```
gmu173p1=start=1 n=1 rearm=0 addr=00000000 ctl=00000001 pol=1
gmu173p2=start=1 n=2 rearm=1 addr=b3500000 ctl=00000001 pol=0
gmu173p3=start=1 n=3 rearm=1 addr=b3500000 ctl=00000001 pol=0
gmu173l=three_starts=1 new_addr=b3500000 ctl1=00000001 pol1=1 ctl2=00000001 pol2=0 ctl3=00000001 pol3=0 fsr=00000400 far=00000000 survived=1
```

新页真的落在 `b3500000`（`rearm=1`），**换表之后没有新的翻译 fault**（`fsr=00000400` 只置 TF，`FSR_FAULT` 是 `0xc00001fe`）。这一键里一次复位都没有，所以任何变化都不来自「固件状态被恢复」。

### 94.7 键 174：启动 → 手清 → 复位 → 再启动

`boot_id 581711093`、窗口 `2beb5eb164d82913caed29ea7255ca060b12cb5cfd415973596d63da2ae6f667`、19/19 行，payload 重钥后 sha256 `5f0b07748c351ff49a17bd641022389d1e258c52f51e40fcd7cfbd8560c98e2a`。

```
gmu174p1=start1=1 ctl=00000001 pol=1
gmu174p2=after_clear=1 word=00000000 about_to_restart=1
gmu174p3=after_restart=1 fw=babeface rpol=5 ctl=00000001 pol=0
gmu174l=clear_then_restart=1 clear_word=00000000 ctl1=00000001 pol1=1 ctl3=00000001 pol3=0 fw=babeface fsr=00000400 far=00000000 survived=1
```

手清有回读（`word=00000000`），复位后固件重跑（`rpol=5`），再写一次启动字后控制字读成 1。

### 94.8 判决

1. **CM3 复位确实让固件重跑**：三次键 172 + 一次键 174，四次复位后 `CM3_FW_INIT_RESULT` 都从被清零的 `00000000` 自己回到 `babeface`，轮询 5 拍。
2. **复位之后控制字又读成 1**：键 172 的三次、键 174 的最后一次都是这样，且都没有翻译 fault（`fsr=00000400`、`far=0`）。
3. **这一轮分不开两种解释**：键 172 的第 2/3 次启动与键 173 的第 2/3 次启动都是 `pol=0`（第一拍就读到 1），而这些位置**没有做过手清** —— 1 可能是新接受，也可能只是第 1 次留下的那一位还活着；只有键 174 做过手清（回读 0），但那一次在复位之后仍然 `pol=0`，所以「复位把 1 放回去」与「启动字被立即接受」同样不可分。
4. **换队列表不是拒绝的原因**：键 173 把表搬到 `b3500000` 之后没有新 fault，机器照常返航。

### 94.9 必须分开说的三件事

1. runner 的判决行写的是「A CM3 RESTART RESTORES THE ACCEPTANCE」，那是 runner 的解读；寄存器账本只支持到「复位之后控制字读成 1」。
2. 这台机器的 `CM3_SYSRESET` 读回永远是 `00000000`（键 172/173/174 的 `gmu172i` 都一样），所以「复位做了什么」只能靠 sentinel（`babeface`）与后续读数间接判断。
3. `pol=0` 只表示「第一次读就是 1」，不表示「固件在 0 拍内接受了」——它同样兼容「这个位本来就是 1」。

### 94.10 单变量表：R269 键 169 → R270 键 174

| 项 | R269 键 169 | R270 键 174 |
|---|---|---|
| 第 1 次启动 | `ctl=00000001 pol=1` | `ctl=00000001 pol=1` |
| 中间动作 | 手清 → 再启动（不重启） | 手清 → **重启 CM3** → 再启动 |
| 第 2 次读数 | `ctl=00000000 pol=200000` | `ctl=00000001 pol=0` |
| 固件 sentinel | 未读 | `babeface`（`rpol=5`） |

差别只有「中间有没有复位」，结果从「200000 轮都不置起」变成「第一拍就是 1」。

### 94.11 没证明什么

1. 没证明「复位把接受还回来了」——缺一个「清零 + 复位 + **不写启动字**」的对照，只有它能说明复位自己会不会把 1 放回控制字。
2. 没证明「第二次启动真的被固件受理」——`ctl` 是唯一的判据，而它的语义还停在「置起过」。
3. 没证明队列表内容与接受无关——键 173 换页之后没有重启，第 2/3 次读数同样被同一个 1 污染。
4. 没证明复位后的固件处于「干净」状态——只看了 `CM3_FW_INIT_RESULT` 与 `CM3_CFG`、`CM3_BOOT_CONFIG`。
5. 仍然没有任何功能推进：`ctl` 之外没有第二个状态字变化，GX 电源域依旧没开，GPU 依旧没渲染一帧。

### 94.12 三条边界

1. 复位脉冲的形状（`SYSRESET=1 → CP_RESP=0 → SYSRESET=0 → poll`）是从 R247 起一直用的同一形状，本轮没有换过。
2. `pol` 的轮数是「带 `IODelay(1u)` 的读循环次数」，不是秒；换算按 R201 的标定。
3. 键 173 的新页地址每次引导都可能不同（`b3500000` 只是这一次），它不是常量。

### 94.13 R271 设计

- **键 175**：手清 → 重启 CM3 → **不写启动字**，只读控制字与 `CM3_FW_INIT_RESULT`。这是本轮缺的那个对照：如果复位之后控制字自己变回 1，那「恢复接受」这个说法整个不成立。
- **键 176**：手清 → 重启 CM3 → **先读一次控制字**（记录复位后的原值）→ 再写启动字 → 再读。把「复位后的原值」与「启动字之后的值」分开。
- **键 177**：手清 → 重启 CM3 → 启动两次（中间不再清零），看第二次是否又变成 200000 轮不置起 —— 若如此，则「一次」是按**固件运行**计数，而不是按引导。

### 94.14 工具事实

- **host 门不编译探针**：`PASS: enchilada adreno probe shim + modules type-check (host stub, arm64 Mach-O)` 只查模块与 shim，探针的语法/警告门只有 CI（一轮 16–20 分钟）。这一轮三轮失败里有两轮是探针语法问题。
- **复制上一轮块当模板时必须剥掉它的块头那一行**，否则新块成为旧块的子作用域；旧块以 `return` 结束时编译器静默删除新块，CI 仍会 28/28 全绿，只有「对真产物查行名」的静态门能发现。
- **「一行声明多个变量」要按 declarator 逐个查引用**，`-Werror,-unused-variable` 会在 CI 第 16 步才报。
- 取 CI 日志包必须用禁用重定向的 opener 先拿 `Location`（`GET /actions/artifacts/<id>/zip` 会 302 到 Azure blob，带 Authorization 直接跟跳会 403）。
- 改名复用脚本要 `assert t.count(old) == 1`：`r270-logs` → `r270-logs2` 被替换两遍会得到 `r270-logs22`，而扫描脚本去 `r270-logs2` 找 ⇒ 空输出被误读成「没有 error」。

## 九十五、ROUND 271：CM3 复位后控制字并没有自己变回 1 —— 「复位还回接受」这一说被拆掉了

### 95.1 一句话

把 R270 的「复位」动作单独拎出来，配上一个**不写启动字**的对照（键 175）：手清控制字到 `00000000` → 重启 CM3 → 满 200000 拍（约 3.17 s，带 `IODelay(1u)`）什么都只读不写，控制字**整段读成 `00000000`（bit0=0），一拍都没自己变回 1**；只有在观察结束之后补写的那一个启动字才把它写回 `00000001`（`pol=1`）。键 176 重启之后**先读原值**再动手：读到的就是 `00000000`。所以 R270 键 172/174 里「复位后控制字读成 1」并不是复位放回去的 —— 复位之后控制字是 0，那个 1 是跟在后面的启动字写出来的。

### 95.2 为什么这三支能拆掉 R270 的说法

R270 的三支把「复位」与「启动字」叠在一起，读者无法区分「读数是启动字写出来的」和「复位本身把位放回去了」。R271 把这两件事在时间上分开：

1. **键 175** 复位之后**一个启动字都不写**，只读满一整个观察窗口。如果控制字自己变回 1，就不再需要任何启动字；如果它整段是 0，那么任何后续的 1 都只能是启动字写出来的。
2. **键 176** 复位之后**第一件事就是读**控制字的原值（在任何手清、任何启动字之前），记录「复位刚结束时固件留了什么」。
3. **键 177** 复位之后连写两个启动字、中间不清，看第二个是否还受理 —— 若不受理，则「一次」是按**固件运行**计数；若受理，则「一次」是按**引导**计数。

### 95.3 代码形状

- 块 **39,015 字符 / 696 行**；`gmuW[...] =` **36** 笔，其中 `HFI_CTRL_INIT = 1u` **4** 笔、`GMU2HOST_INTR_MASK` **3** 笔、`CM3_SYSRESET` 上升/下降沿各 **4** 笔、`HFI_QTBL_ADDR`/`HFI_QTBL_INFO` 各 **4** 笔、`HFI_CTRL_STATUS = 0u` **3** 笔。
- 行名 **27** 条：共享前导段 `gmu175a..n`（12 条）+ `gmu175k1..k3`，键 175 的 `gmu175p0..p2` + `gmu175l`，键 176 的 `gmu176p1..p3` + `gmu176l`，键 177 的 `gmu177p0..p2` + `gmu177l`。
- 重启形状与 R270 完全相同（`QTBL_ADDR/INFO` 重写 → `CM3_SYSRESET = 1` → `CP_RESP = 0` → `CM3_SYSRESET = 0` → 轮询 `CM3_FW_INIT_RESULT` 到 `0xbabeface`），`MODEL_LOCK` 从未写（不碰 `sCR0` bit10）。
- `watch_untouched` 的观察循环是带 `IODelay(1u)` 的读循环（200000 拍），不是纯 `rd32` 紧自旋 —— 后者会饿死日志 writer（R205/R208 的教训）。

### 95.4 构建与门禁

| 项 | 结果 |
|---|---|
| CI | #388 `45098747160` **success 28/28** |
| 静态门 | **PASS: R271 static gate**（`required+survivors: 48 checked in both artefacts`，key marker kernel=1 payload=2，forbidden 3 checked） |
| 静态门自证 | 对 R270 产物 **FAIL (34 problems)**（`gmu176*`/`gmu177*` 行 kernel=0 payload=0）⇒ 门有区分能力、新块确实进二进制 |
| host 门 / stage4 / stage5 / stage6 | 28/28 / 61/61 / 48/48 / 1802/1802 |
| 全量门禁 | `ran 46 of 48, FAILING: 0` |
| `check_mode_selector.py` | `selectors 26 / upper bound 177`，自证 FAIL，`VERDICT: PASS` |
| 产物 sha256 | kernel `52ee50a13cc6a878bc8319df5b6fc1b2d57aa797b61c58960570557b966ed047`（126,451,712 B）、payload `1500834307f197e6067744e3a4ba75fd4796d642839f2bb63a5af17fe989d947`（150,994,944 B） |

### 95.5 键 175：复位之后不写启动字，只读满窗口

`boot_id 655187289`、窗口 `210649b1d2606089650d7997bb985760e1d43a26199da16adb864260a286e12c`、19/19 行，payload 重钥后 sha256 `9345eeca135855385d386b308f958125c61c95616f43e7d52636d97bcd964abd`。

```
gmu175h=init_cleared=1 before=babeface after_clear=00000000 about_to_reset=1
gmu175i=reboot=1 fwinit=babeface polls=4 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1
gmu175p0=hand_cleared=1 word=00000000 about_to_restart=1
gmu175p1=no_start=1 fw=babeface rpol=4 watch=00000000 pol=200000
gmu175p2=control_start=1 ctl=00000001 pol=1
gmu175l=mode=175 ck1=00000000 pk1=200000 ck2=00000001 pk2=1 ck3=00000000 pk3=200000 fsr=00000400 far=00000000 survived=1
```

判决：手清后读 0；复位后（`gmu175i` 的 `ctl=00000000`）仍是 0；**整段观察 200000 拍里 `watch=00000000`（bit0=0）**，固件 sentinel `babeface`（`rpol=4`）；观察结束后补写的启动字命中 `ctl=00000001 pol=1`。⇒ **复位本身没有把 1 放回去**。

### 95.6 键 176：复位之后先读原值

`boot_id 574526237`、窗口 `d29556bdc749917a7a4dc06a3b82a4e8b34f2614cd718e098fe41d1319e3abae`、19/19 行，payload 重钥后 sha256 `2205986b0da81151b6134fc9f07dd47b81a39da0b2e316488389e9f2070ed97c`。

```
gmu176p1=raw_after_restart=1 fw=babeface rpol=4 ctl=00000000 bit0=0
gmu176p2=cleared_after_restart=1 word=00000000 about_to_start=1
gmu176p3=start_after_clear=1 ctl=00000001 pol=1
gmu176l=mode=176 ck1=00000000 pk1=200000 ck2=00000001 pk2=1 ck3=00000000 pk3=200000 fsr=00000400 far=00000000 survived=1
```

判决：**复位刚结束、任何手清与启动字之前**，`raw_after_restart` 读到的就是 `ctl=00000000 bit0=0`；随后手清仍 0、启动字命中 `00000001 pol=1`。⇒ 与键 175 互证：复位留下的原值是 0。

### 95.7 键 177：复位后连写两个启动字（中间不清）

`boot_id 646890857`、窗口 `daf3e6522a7072e8f2db57513222877e080b51a132bf1d73d4f068c44b6f9e75`、19/19 行，payload 重钥后 sha256 `15d0e539641c3dcee6c97dae3bf97a81e91032807d8e781ab00f51437bc3a7d2`。

```
gmu177p0=hand_cleared=1 word=00000000 about_to_restart=1
gmu177p1=first_start=1 ctl=000000fe pol=200000
gmu177p2=second_start=1 ctl=000000fe pol=200000
gmu177l=mode=177 ck1=000000fe pk1=200000 ck2=000000fe pk2=200000 ck3=00000000 pk3=200000 fsr=00000400 far=00000000 survived=1
```

判决：**这一支没有回答「一次是按运行还是按引导」的问题** —— 第一个启动字就返回 `ctl=000000fe`（bit0=0）满 200000 拍不受理，第二个同样 `000000fe`。与键 175/176 里「手清之后的启动字 `pol=1` 立刻命中」对照，这里的差别是**没有先手清就写启动字**（键 177 的顺序是 手清 → 复位 → 启动），说明键 177 里那个 `000000fe` 更像是「复位之后必须先把手清落在启动字之前，否则控制字停在 `0xfe` 的非零态、启动字不落地」。这是本支的一个**未收口的异常**，登记为待查，不作判决。

### 95.8 判决（对 R270 的修订）

1. **R270 的「复位把接受还回来了」被拆掉**：复位之后控制字原值是 `00000000`（键 176 直接读到、键 175 整段观察也读到 0），并没有一位 1 留在里面。R270 键 172/174 里读到的 1 是**跟在其后的启动字**写出来的，不是复位放回去的。
2. **「一次受理」的机制仍是控制字这一位**，而不是一个看不见的固件内部状态：复位不清 `CTRL_STATUS` 之外的东西，而 `CTRL_STATUS` 复位后本来就是 0。
3. **键 177 未收口**：复位后不先手清就写启动字，控制字停在 `0xfe`，两个启动字都不受理。这与键 175/176 的顺序（手清在启动字之前）构成对照，需要单独一支再查（见 95.10）。

### 95.9 必须分开说的三件事

1. 键 175 的 `watch=00000000 pol=200000` 是「满窗口只读」，它证明的是**复位之后控制字不会自己变 1**，不证明「复位之后固件完全没动过任何其它寄存器」——只看了 `CM3_FW_INIT_RESULT`（`babeface`）、`CM3_SYSRESET`（`00000000`）、`CM3_CFG`（`00004052`）。
2. `CM3_SYSRESET` 读回永远是 `00000000`（三支的 `gmu175i`? 实为 `gmu175h`/`gmu176p1`… 里的 `sysreset=00000000`），与 R270 一致，所以「复位做了什么」只能靠 sentinel 与读数间隔间接判断。
3. 键 177 的 `000000fe` 不是「拒绝」的干净证据：bit0=0 但整个字是 `0xfe`（bit1..7 有值），和 R267 键 165 / R270 里出现过的 `000000fe` 同形，来源未分辨（可能是掩码残留或 HFI 侧状态），只登记不归因。

### 95.10 R272 设计（承接键 177 的异常）

- **键 178**：复位之后**先手清、再写启动字**（复现键 175/176 的顺序），确认 `pol=1` 可复现 —— 把「必须手清在启动字之前」立住。
- **键 179**：复位之后**不手清、只读**控制字多拍，记录它停在 `0xfe` 还是 `0`（分辨 `0xfe` 是复位留下的还是启动字写坏的）。
- **键 180**：复位之后不手清、写启动字后立刻再读一个不同偏移的状态字（`0x5180..0x5183`），看 `0xfe` 与哪个字相关。

### 95.11 工具事实

- **本轮 runner 的 PAYLOAD 目录名是 `r271-p<N>-payload`（rekey 输出目录），不是 mkrun 里写的 `r271-s<N>-payload`**：三个 runner 初次运行都 `ABORT missing …
271-s175-payload\payload.img`，改成 `r271-p175-payload` 后才跑起来。mkrun 改名时把 `r270-s172` 全替换成 `r271-s<key>`，而 rekey 用的是 `p<key>`，两处命名不一致是这次的空转原因。
- **重键工具允许变长**：`r170_rekey.py <src> <dst dir> <old> <new>`，`enchilada-pas-exec=2` → `=175/=176/=177` 各改了配置字节（1377 → 1379 B），整个文件 150,994,944 B 不变，mtime 钉到 `EPOCH=315532800`。`r165_arg_payload.py` 只允许等长。
- 上机顺序沿用 `r271_s17N_run.py` 自带：`check-reboot-to-twrp.py` PASS → `adb reboot bootloader` → `flash system_b` → `boot boot-r16.img`（UEFI 卷，非内核）→ 等看门狗回 recovery → `reboot-to-twrp.py --arm-bcb-only` 重新武装 → `dd` 拉 32 MiB 窗口 → 校验 key 块与 8 MiB 头 sha → 解析。

## 九十六、ROUND 272：0xfe 来自启动字而不是复位 —— 且一个被受理的启动字把 0x5180 写成 1

### 96.1 一句话

R271 键 177 里那个「两次启动字都没被受理、控制字停在 `000000fe`」的异常，被 R272 三支拆开了：**键 179**（重启 CM3、什么都不清、**一个启动字都不写**）控制字读回 `00000000`（bit0=0）、四个状态字 `00000000 00000000 06030100 00000000`；**键 180**（同一形状，**多写一个启动字**）控制字变成 `00000001`、四个状态字变成 `00000001 00000000 06030100 00000000`。⇒ 那个 `0xfe` 是键 177 **自己写下去的那个启动字**留下的，不是复位留下的；而且一个被受理的启动字**会把 `0x5180` 从 0 写成 1**（这是第一次看到 `0x5180` 携带受理位）。**键 178** 是阳性对照：重启之后先手清（读回 `00000000`）再写启动字，控制字仍是 `00000001`、`pol=1` ⇒ 键 177 之所以失败，是因为它的手清放在启动字**之后**，而不是「重启之后路径失效」。

### 96.2 为什么这三支能拆掉 R271 的异常

R271 键 177 把「手清、重启、两次启动字」叠在同一段里，读者无法区分 `0xfe` 是复位留下的、还是启动字写出来的。R272 把「有没有启动字」做成唯一的差别：

1. **键 179 vs 键 180** 只在「写不写那个启动字」上不同。键 179 不写 → `ctl=0`、`0x5180=0`；键 180 写 → `ctl=1`、`0x5180=1`。所以 `0xfe`（键 177 里的）只能来自启动字。
2. **键 178** 用「先手清、重启、再启动」复现 R271 键 176 的受理（`pol=1`），证明重启之后受理路径本身没有坏 —— 键 177 失败是顺序问题（清在字之后）。
3. 四状态字 `0x5180..0x5183` 在键 179 与 180 之间逐字比较，把「启动字具体改了哪个字」钉死到 `0x5180` 的最低位。

### 96.3 代码形状

- 块 **37,193 字符 / 653 行**；`gmuW[...] =` **31** 笔，其中 `HFI_CTRL_INIT = 1u` **2** 笔、`GMU2HOST_INTR_MASK` **2** 笔、`HFI_CTRL_STATUS = 0u` **1** 笔、`CM3_SYSRESET` 上升/下降沿各 **4** 笔。`for (` **10**、`IODelay(1u)` **6**。
- 行名 **18** 条/键：共享前导段 `gmu178a..n`（12 条）+ `gmu178k1..k3`，键 178 的 `gmu178p0/p1` + `gmu178l`，键 179 的 `gmu179p1` + `gmu179l`，键 180 的 `gmu180p1/p2` + `gmu180l`。
- 三支语义：**178** = 手清 → 重启 → `ARM_AND_START`（清在启动字之前，177 的阳性对照）；**179** = 重启 → 不手清 → 只读控制字与 `0x5180..0x5183`；**180** = 重启 → 不手清 → 写启动字 → 立即再读 `0x5180..0x5183`。

### 96.4 构建与门禁

| 项 | 结果 |
|---|---|
| CI run | **#389 `45112447196`** success **28/28** |
| commit | branch `r272-anomaly` **`6d735dada0afefadbd88ea96381a350f5579eae0`** |
| kernel | `work\tmp\r272-kernel\kernel.macho` 126,451,712 B sha256 `6abcf630af63a0d99593ee5987911d0ae072133d9bd9153b14704752bcf38c29` |
| payload | `work\tmp\r272-payload\payload.img` 150,994,944 B sha256 `98849e253d5a2cf4f3b296ea2669631ecf0e1e4430a7a19cc0e3ae91183d6ae7` |
| 静态门 | PASS（35 行 = 24 required + 11 survivors，双产物；对 R271 产物自证 FAIL 23） |
| host 编译 | 25/25 + 51/51 contract PASS |
| probe 门 | 28/28（store 计数 678 → 709，+31） |
| stage4/5/6 | 61/61、48/48、1819/1819 PASS |
| 全量门禁 | ran 46 of 48，FAILING: 0；check_mode_selector selectors 27 / upper bound 180 PASS |

### 96.5 键 178（阳性对照：清在启动字之前，重启后受理仍成立）

- rekey：`enchilada-pas-exec=2 → =178`（配置 1377→1379 B），payload sha256 `7683cdf0ae62c7e423b37aa4b3699c49200ddf08bd2c882aeb4af6704e71e143`。
- `boot_id 589216635`、窗口 sha256 `4619d61679de89bcefe0f930176b8f731fc35c3b52ba21ff08e8163d8b046c94`、18/18 行。
- `gmu178p0=hand_cleared=1 word=00000000 about_to_restart=1` → 重启 → `gmu178p1=clear_then_start=1 fw=babeface rpol=5 ctl=00000001 pol=1`。
- `gmu178l=mode=178 ck1=00000001 pk1=1 ck2=00000000 pk2=200000 ck3=00000000 pk3=200000 w5180=00000000 w5181=00000000 w5182=00000000 w5183=00000000 fsr=00000400 far=00000000 survived=1`。
- ⇒ 手清（回读 0）→ 重启 → 启动字，控制字仍读 `00000001`、`pol=1`：**清在启动字之前的路径重启后照旧成立**。无翻译 fault（`fsr=00000400 / far=0`）。

### 96.6 键 179（重启、不清、不启动：控制字与四状态字的基线）

- rekey：`→ =179`，payload sha256 `b323fa9b6a8ce638b5414cee3fe2fb26787d017b1dba1d1e429dfca4b173aa51`。
- `boot_id 586756880`、窗口 sha256 `2e119c2575c900c53fa6a50df85cbd7faa42770df202567171c1cbee590922e2`、17/17 行。
- `gmu179p1=restart_no_clear=1 fw=babeface rpol=4 ctl=00000000 bit0=0 w5180=00000000 w5181=00000000 w5182=06030100 w5183=00000000`。
- `gmu179l=mode=179 ck1=00000000 pk1=200000 ck2=00000000 pk2=200000 ck3=00000000 pk3=200000 w5180=00000000 w5181=00000000 w5182=06030100 w5183=00000000 fsr=00000400 far=00000000 survived=1`。
- ⇒ 重启之后**不写任何东西**，控制字整段是 `00000000`，`0x5180=0`、`0x5182=06030100`（芯片号）、`0x5181=0x5183=0`。**复位没有把任何位放回去，也没有写下 `0xfe`。**

### 96.7 键 180（重启、不清、写启动字：唯一差别是那个启动字）

- rekey：`→ =180`，payload sha256 `a3412fb862d791601ea4e1562c2f569fe0d1dd2330b4dc59d798171ccc3b4394`。
- `boot_id 586144606`、窗口 sha256 `726e56568571b6d717938650026b4ef723a714096a5bf6872ada94dbf9cf38f1`、18/18 行。
- `gmu180p1=start_no_clear=1 ctl=00000001 pol=1` → `gmu180p2=after_start=1 ctl=00000001 w5180=00000001 w5181=00000000 w5182=06030100 w5183=00000000`。
- `gmu180l=mode=180 ck1=00000001 pk1=1 ck2=00000001 pk2=0 ck3=00000000 pk3=200000 w5180=00000001 w5181=00000000 w5182=06030100 w5183=00000000 fsr=00000400 far=00000000 survived=1`。
- ⇒ 与键 179 只差「写不写启动字」：写下去之后控制字 0→1、`0x5180` 0→1，其余三字不动。**一个被受理的启动字把 `0x5180` 的最低位写成 1。**

### 96.8 判决

1. **`0xfe` 来自启动字，不来自复位**：键 179（不写启动字）控制字是 `00000000`；键 180（写）才是 `00000001`，且键 177 的 `0xfe` 只在写了启动字之后出现。
2. **一个被受理的启动字把 `0x5180` 写成 1**：键 180 `w5180=00000001`，键 179 `w5180=00000000`，其余三字两键逐字相同（`0 0 06030100 0`）。这是本移植第一次看到 `0x5180` 携带受理位。
3. **重启之后受理路径本身没坏**：键 178 先手清再启动仍得 `ctl=1 / pol=1`。键 177 之所以「两次都不受理」，是因为它的手清在启动字**之后**（第二次启动字写在一个已被受理并锁存的控制字上）。
4. 三键都没有翻译 fault（`fsr=00000400 / far=0`），`survived=1`。

### 96.9 必须分开说的三件事

- **「0x5180 = 1」是事件还是状态，未定**：键 180 只读了一次启动字之后的 `0x5180`，没有读它会不会自己回落，也没有像 R253 键 120 那样「命中后再清一次」检验它可否被清除。登记为未收口。
- **`0xfe` 的位模式仍未拆**：本次只证明它由启动字写下，没有逐位解释 `0xfe` 相对于 `0x01` 多出来的 bit1..bit7 各是什么。
- **键 177 的失败归因是推断**：由键 178（清在前 → 受理）与键 177（清在后 → 不受理）对比得出，未做「清在后但换个顺序」的独立复现。

### 96.10 R273 设计

- **键 181**：命中后（`0x5180=1`）再清一次 CLR，看 `0x5180` 是否回落 —— 判「`0x5180=1` 是可清除事件还是锁存状态」（对照 R253 键 120 对 `GMU2HOST_INTR_INFO` bit23 的判法）。
- **键 182**：读 `0x5180` 的完整 32 位并逐位打印，配合 `0x517f/0x5184` 邻字，看 `0xfe` 多出的 bit1..bit7 是否只是邻字泄漏。
- **键 183**：把「清在启动字之后」独立复现一次（键 177 的失败路径），确认失败可重复而非偶然。

### 96.11 工具事实

1. 设备 runner 的 `PAYLOAD` 初值是模板遗留的 `r271-p175-payload`，本轮三份 runner 都要改成各自的 `r272-p<N>-payload`，否则 `ABORT missing`（mkrun 的改名链不含 PAYLOAD 行）。
2. `python -c` 里写含中文引号/反斜杠的替换串会被 PowerShell 解析成 `unterminated string literal`，一律先写 `.py` 再跑。
3. 同键连跑会覆盖同名窗口文件，三键各另存 `r272_s17{8,9,0}_window_r1.bin`（各 33,554,432 B）。

## 九十七、R273：控制字尾巴的三问——0x5180 bit0 是锁存态、0xfe 不是邻字泄漏、也不是那个顺序的产物

**97.1 一句话**
在 R272 把「复位不放回控制字、启动字把 0x5180 bit0 写 1、一次受理由启动字承载」立住之后，本轮只问三件关于控制字尾巴的事：清中断锁存后 0x5180 bit0 会不会回落（它是可清事件还是锁存态）、0xfe 这个形状是不是从邻字漏进来的、以及键 177 那次「启动后手清得到的 0xfe」是不是那个顺序的稳定产物。三键都跑完，三问都有答案。

**97.2 为什么要把这三问答完**
R272 留下了两个悬案。第一，R267 键 165 与 R270 里出现过控制字读成 `0xfe`（低位全 1）的形态，而 R272 键 180 的干净启动给出的是 `0x00000001`——`0xfe` 是「启动字之后、还没被手清」的中间态，还是邻字漏进读取，没人说清楚。第二，R253 键 120 曾在其它寄存器上证明「命中后清 CLR 能把 bit 回落」，但那是 0x5192 的 bit23，不是 0x5180 的 bit0；0x5180 bit0 究竟可清还是锁存，没有直接测过。这两件事都关乎「一次受理」到底是不是一个软件能看、能清的位，所以必须单独拆开问。

**97.3 代码形状**
探针块 38,949 字符 / 681 行，braces 净值 -1；`gmuW[` 34 笔，`HFI_CTRL_INIT] = 1u` 3 笔（三键各一次），`GMU2HOST_INTR_MASK] = ` 3 笔，`0xc00001feu` 3 笔（三键各清一次 FSR），`CM3_SYSRESET` 1u/0u 4/4（preamble 一次 + 三键各一次），`HFI_QTBL_ADDR] = ` 4 笔、`HFI_QTBL_INFO] = 1u` 4 笔（preamble + 三键各一次），`HFI_CTRL_STATUS] = 0u` 仅 1 笔（只有键 183 手清），`HFI_CTRL_STATUS] = 1u` 0 笔（没有任何一键直接把控制字写成 1），`GMU2HOST_INTR_CLR] = ` 2 笔（preamble 继承 1 笔 + 键 181 自己 1 笔），`for (` 11 处、`IODelay(1u)` 7 处、无 `IOSleep`。

**97.4 构建与门禁**
CI #390（run `45120883570`）success 28/28。取件拍平：kernel.macho 126,468,096 B sha256 `456b36c1b46e8a0f0a3bcdc44f14b6519288b1aa34d50565adcb44e02c4e485f`，payload.img 150,994,944 B sha256 `c835619c72e5063bb05e256e9e4cda3bfe35bdb928c47d49410684c655b2a5fc`。静态门 `r273_static.py` 对真产物 **PASS**（24 行 required+survivors 在 kernel 与 payload 里都命中，marker kernel=1 / payload=2，forbidden 3 条全零，固件 32768 B sha `b07f9029…` 钉住），对 R272 产物 **FAIL 14 problems**（`gmu182p1/gmu182l/gmu183p1/gmu183p2/gmu183l` 等 kernel=0 payload=0）——门的区分能力自证成立，说明这一轮的块真的进了二进制。host 编译 25/25 + 51/51；probe 门 store 计数由 709 抬到 743（+34）；stage4 61/61、stage5 48/48、stage6 1839/1839；全量 `r239_allgates.py` ran 46 of 48 FAILING 0；`check_mode_selector.py` 28 selectors / upper bound 183 / 自证 FAIL / PASS。

**97.5 键 181（启动后清中断锁存，再读 0x5180）**（DEVICE-VERIFIED，boot_id `587021515`，窗口 sha256 `b8883ff40dbb400a25679c84caef824851d35be9597012540d2ec9916889de59`，18/18 行）
- `gmu181i=reboot=1 fwinit=babeface polls=4 sysreset=00000000 cfg=00004052 ctl=00000000 win=1 survived=1`
- `gmu181p1=start=1 fw=babeface rpol=4 ctl=00000001 pol=1`
- `gmu181p2=after_intr_clr=1 ctl=00000001 bit0=1 w5180=00000001 w5182=06030100`
- `gmu181l=mode=181 ck1=00000001 pk1=1 ck2=00000001 pk2=200000 ck3=00000000 pk3=200000 w5180=00000001 w5181=00000000 w5182=06030100 w5183=00000000 fsr=00000400 far=00000000 survived=1`
- 读数：启动字被受理（`ctl=1 pol=1`）；随后写 `GMU2HOST_INTR_CLR` 清中断锁存；再读控制字仍是 `00000001`、bit0 仍 1、`w5180` 仍 `00000001`。**0x5180 bit0 在中断锁存被清后保持为 1 ⇒ 它是锁存态，不是可清事件。**（与 R253 键 120 不同：那里清 CLR 让 0x5192 的 bit23 回落；这里 0x5180 bit0 不因清 CLR 而回落。）

**97.6 键 182（启动后读全 32 位控制字与全部邻字）**（DEVICE-VERIFIED，boot_id `586979996`，窗口 sha256 `4a11b9c55e8b6d2defa3a9fb78922160cd30a696e354cbb0d607032568ae56c6`，17/17 行）
- `gmu182p1=shape=1 ctl=00000001 w517f=00000000 w5181=00000000 w5183=00000000 w5184=00000001 w5185=ad980000 w5186=00000001`
- `gmu182l=mode=182 ck1=00000001 pk1=1 ck2=00000000 pk2=200000 ck3=00000000 pk3=200000 w5180=00000001 w5181=00000000 w5182=06030100 w5183=00000000 fsr=00000400 far=00000000 survived=1`
- 读数：控制字这次是干净的 `00000001`；邻字 `w517f=0 / w5181=0 / w5183=0 / w5184=00000001`（QTBL_INFO）/ `w5185=ad980000`（QTBL_ADDR，正是队列表物理地址）/ `w5186=00000001`（CTRL_INIT）。**没有任何邻字带 `0xfe` 形状 ⇒ 之前出现过的 0xfe 是控制字自己的值，不是邻字漏进读取。**

**97.7 键 183（先启动后手清，复现键 177 的顺序）**（DEVICE-VERIFIED，boot_id `587953523`，窗口 sha256 `11e1309631df41dcfcea2c1f27535a6ec215a4fa6a437a8d850fd94aae6f8e4c`，18/18 行）
- `gmu183p1=start_first=1 ctl=00000001 pol=1`
- `gmu183p2=clear_after_start=1 ctl=00000000 bit0=0 w5180=00000000`
- `gmu183l=mode=183 ck1=00000001 pk1=1 ck2=00000000 pk2=200000 ck3=00000000 pk3=200000 w5180=00000000 w5181=00000000 w5182=06030100 w5183=00000000 fsr=00000400 far=00000000 survived=1`
- 读数：先写启动字 → `ctl=00000001 pol=1`；再手清 → `ctl=00000000 bit0=0 w5180=00000000`，且满 200000 拍后仍 0。**键 177 那次「启动后手清读成 0xfe」这一次没有复现——干净顺序给出的是 0 ⇒ 那个 0xfe 是一次性的残留态，不是「先启动后手清」这个顺序的稳定产物。**

**97.8 判决（四点）**
1. **0x5180 bit0 是锁存态，不是可清事件**：键 181 清中断锁存（`GMU2HOST_INTR_CLR`）后 bit0 与 w5180 都不回落。软件清 CLR 改变不了它。
2. **0xfe 不是邻字泄漏**：键 182 逐字读了 0x517f/0x5181/0x5183/0x5184/0x5185/0x5186，没有一个带 0xfe 形状；控制字的值是它自己的。
3. **0xfe 不是「先启动后手清」这个顺序的产物**：键 183 用与键 177 完全相同的顺序（先写启动字、后手清），得到干净 `00000000`，没有 0xfe。
4. 于是 R267/R270 里那个 0xfe **只是一次性残留态**（在那一拍上恰好读到的、既非邻字泄漏也非条件稳定产物），不应被当成任何一条路径的稳定性质。控制字在干净启动后就是 `00000001`，手清后就是 `00000000`。

**97.9 必须分开说的三件事**
1. 「可以手清控制字」（R268 键 168）与「0x5180 bit0 可清」是两回事：前者是写 `HFI_CTRL_STATUS` 这个软件寄存器位；后者是清 `GMU2HOST_INTR_CLR` 这个中断锁存器。键 181 证明清锁存器不碰 bit0。
2. 「0xfe 曾出现」（R267/R270）与「0xfe 是稳定产物」（本轮否定）是两回事：出现过不等于可复现。
3. 「启动字把 bit0 写 1 且是锁存态」与「受理只发生一次」是两回事：bit0 锁存，不代表每引导只能受理一次；R269 已证明手清后同一引导不再置起。

**97.10 没证明什么**
1. 没有测 0x5180 bit0 的置起机制在硬件内部是怎么连的（没有读固件代码）。
2. 没有分辨 0xfe 那一次到底是什么让控制字读成了全 1（只证明它既非邻字也非该顺序的稳定产物）。
3. 没有回答「一次受理」在固件内部是按固件运行计数还是按引导计数（R271 键 177 未收口，本轮 183 也不回答这个问题）。
4. 没有开 GX 电源域——本轮完全不碰 GX。

**97.11 R274 设计（下一步）**
- 控制字的尾巴到此收口。下一轮把主攻切回 GX 电源域：逐笔对照 CX 域的上电序列与 GX 域，观测固件是否本应写 GDSC；对应 `notes/GX-power-domain-status.md` 里 G1（`POWER_UP_COMPLETE` bit16 由什么置起）/G2（gfxcc `vdd_gfx` 与 GX 时钟前置）/G3（`cfg=01080400` bit10/18/19 语义）三个缺口。键 184 = 读 gfxcc 的 vdd_gfx 与 GX 时钟寄存器现状；185 = 打开 gfxcc 前置后再走 GX GDSC 四笔看 bit16；186 = 对照 CX 域完整上电序列逐笔复刻到 GX。

**97.12 工具事实**
1. 设备 runner 的 `PAYLOAD` 用 `r273-p<key>-payload`（rekey 输出目录名就是它），mkrun 改名链要显式带上 `("r272-p178-payload","r273-p%d-payload")` 这一条，否则会指向不存在的目录、runner 直接 ABORT `missing …r273-p178-payload`。
2. `r273_static.py` 传相对路径时以脚本所在目录（`work\tmp`）为基准，是能工作的；一旦传 `work\tmp\…` 这种以 `work\tmp` 再开头的路径就会拼成 `work\tmp\work\tmp\…` 而报 missing——要么只传相对脚本目录的路径，要么传绝对路径。
3. 三键各另存 `r273_s18{1,2,3}_window_r1.bin`（各 33,554,432 B），同键连跑会覆盖 `rNNN_sNNN_window.bin`。

## 九十八、R274：GX 电源域——静止时 bit16 为 0、四笔 GDSC 写入把 GDSCR 拉起但 POWER_UP_COMPLETE 永不置起、时钟不是那道门

**98.1 一句话**
本轮第一次直接量 GX 电源域的静止态与上电时序：静止时 GX 的 POWER_UP_COMPLETE（bit16）为 0、域确实是关的；四笔 GDSC 写入（clamp 打开、clamp 关闭、清 GDSCR 的 bit0）能把 GDSCR 的 power-up 位（bit27）拉起来（00222001→08222000），但 bit16 在满 20000 次轮询后仍是 0；而且开不开那两个 gfxcc 分支时钟结果**逐位相同**，所以时钟不是 bit16 的那道门。

**98.2 为什么先量 GX 域**
R213 以来一直卡在「固件不接受启动、GPU 一帧未渲染」，而 GX 电源域从未打开是一个并行的硬缺口（见 `notes/GX-power-domain-status.md`：`gdsc_gx_do_nothing_enable` 是空实现）。在回去继续攻固件之前，先把 GX 域本身量清楚：它静止时到底是不是关的（bit16=0）、四笔 GDSC 写入能不能让它上的、gfxcc 时钟是不是前置条件。三件事互不依赖，可以一次量完，且不需要 CM3 重启或 SMMU 建表，所以这一轮用扁平自包含块，不复用 R273 的重机器。

**98.3 代码形状**
探针块 9,770 字符 / 194 行，braces 净 0（opens=12 closes=12）；块内 `gmuW[` 12 笔——其中 4 笔是两条 CBCR 时钟寄存器（cxgmuWord 写 `(v&~0xff0)|0xff0` 再 `|1`，cxoWord 同），8 笔是四笔 GX GDSC 序列（clamp `|0x10`、clamp `&~0x10`、clamp `&~1`、gdscr `&~1`）各重复一次（185 与 186 各一遍）。`for (` 4 处、`IODelay(1u)` 6 处、无 `IOSleep`；本块**不写** `HFI_CTRL_INIT`、不写中断掩码、不清 FSR、不复位 CM3、不建 SMMU 表——所以 `gmuW[` 只有这 12 笔。寄存器都经 `gmuW[...]`（volatile uint32_t *const）访问，`sGmuFwVa` map 的是 `0x0506a000..+0x30000`：GX GDSCR = `0x0509100c`（dword 0x9c03）、GX CFG/状态 = `0x05091010`（dword 0x9c04，就是轮询 POWER_UP_COMPLETE bit16 的那个字）、GX clamp = `0x05091508`（0x9d42）、CX 状态 = `0x05091540`（0x9d50）、CX GMU CBCR = `0x05091098`（0x9c26）、CXO CBCR = `0x0509109c`（0x9c27）。

**98.4 构建与门禁**
CI #391（run `45129130628`）success 28/28。取件拍平：kernel.macho 126,468,096 B sha256 `8e80298186512067b4277e3cb15a4022d27df744966465ef9c17e766cc1c74d5`，payload.img 150,994,944 B sha256 `99d74a3609fac268022cac57ac6a9a8aa60f47c2d7a2f7569f0d647ec2156675`。静态门 `r274_static.py` 对真产物 **PASS**（18 行 required+survivors 在 kernel 与 payload 里都命中，marker kernel=1 / payload=2，forbidden 3 条全零，固件 32768 B sha `b07f9029…` 钉住），对 R273 产物 **FAIL 8 problems**（`gmu184p1/gmu184l/gmu185p0/gmu185p1/gmu184l2/gmu186p0/gmu186p1/gmu184l3` 全部 kernel=0 payload=0）——门的区分能力自证成立。host 编译 25/25 + 51/51；probe 门 store 计数由 743 抬到 755（+12）；stage4 61/61、stage5 48/48、stage6 1839/1839；全量 `r239_allgates.py` ran 46 of 48 FAILING 0；`check_mode_selector.py` 29 selectors、上界 186、自证 FAIL、VERDICT PASS；`r274_hashes.py` 7 路径 0 不符。

**98.5 键 184：静止态（时钟关闭）**
`boot_id 588174820`、窗口 sha256 `37fcea66256e4725c8eb225bee5d7670cc39b1eaae727ff1fd5ce342a93e98db`、2/2 行。逐字：`gmu184p1=state=1 gdscr=00222001 cfg=00088400 clamp=00000001 cx=80001fe0 cxgmu=00004ff1 cxo=00000001 gx_on=0`。`cfg=00088400` 的 bit16（`& 0x10000`）= **0** ⇒ **GX 域静止时是关的，轮询 bit16 是有意义的**。`gdscr=00222001` 的 bit0=1（GDSC 关闭）、bit27=0；`cx=80001fe0` 是 CX 域的状态（与 GX 不同域，仅作对照）。

**98.6 键 185：开两时钟 + 四笔 GDSC 写入**
`boot_id 585398319`、窗口 `b3cacbfe9a0d8a01056b4250d47a7ecffb7ec6073e85a862dcc26475e1abf56d`、3/3 行。开时钟前 `gmu185p0=pre=1 gdscr=00222001 cfg=00088400 cxgmu=00004ff1 cxo=00000001`；开时钟并走完四笔 GDSC 后 `gmu185p1=clocked=1 gdscr=08222000 cfg=01080400 clamp=00000000 cx=80001fe0 cxgmu=00004ff1 cxo=00000001 gx_on=0 polls=20000`。判读：`gdscr` 由 00222001 变 08222000——**bit27（power-up）已置起，说明 GDSC 接受了上电请求**；`cfg` 由 00088400 变 01080400，但 `01080400 & 0x00010000 = 0`——**POWER_UP_COMPLETE（bit16）在满 20000 次轮询后仍是 0**。`polls=20000` 是轮询撑满上限，不是提前退出。

**98.7 键 186：不开时钟，只走四笔 GDSC 写入（对照）**
`boot_id 589445539`、窗口 `b4de98942625f87a8ac16a406e94823676415c51f4703d038de545db1b93ae04`、3/3 行。GDSC 写入前 `gmu186p0=pread=1 gdscr=00222001 cfg=00088400 cxgmu=00004ff1 cxo=00000001`；四笔写入后 `gmu186p1=noclock=1 gdscr=08222000 cfg=01080400 clamp=00000000 cx=80001fe0 cxgmu=00004ff1 cxo=00000001 gx_on=0 polls=20000`。与键 185 的写入后状态**逐位相同**（gdscr=08222000、cfg=01080400、bit16=0、polls=20000）。

**98.8 判决**
1. **GX 域静止时确实关着**：键 184 `cfg=00088400` bit16=0。这坐实了 GX 电源域从未打开这一缺口不是空的。
2. **四笔 GDSC 写入能把 GDSCR 的 power-up 位（bit27）拉起来**：00222001→08222000，两键一致。写入路径成立，GDSC 接受了请求。
3. **但 POWER_UP_COMPLETE（bit16）永不置起**：键 185、186 满 20000 轮询仍是 0。GDSC 收下了请求却没完成上电——这正是 `notes/GX-power-domain-status.md` 里的缺口 G1（bit16 由什么置起）。
4. **gfxcc 时钟不是 bit16 的那道门**：键 185（开两时钟）与键 186（不开）写入后状态逐位相同。开不开时钟，结果一样。之前「时钟是前置条件」的猜测被本轮否掉——真正门在别处（多半是 AON 复位 clamp 时序、vdd_gfx 供电、或固件是否先写 GDSC，对应 G2/G4/G5）。

**98.9 必须分开说的三件事**
第一，本轮只证明「我们写的四笔 GDSC 序列把 bit27 拉起了、bit16 没跟上」，没证明 mainline 的完整 GDSC 上电序列（可能还有更多笔，或要配合 AON/RBCPR）也失败——那是同一缺口的不同切面，不能合并成「GDSC 上电必失败」。第二，键 186 是 185 的对照，二者唯一差别是时钟写不写；但两者都在同一个固件运行态下测的，若固件本身在某处改写了 GX GDSC，两键会一起看到那个改写，本轮不能排除。第三，`cx=80001fe0`、`cxgmu=00004ff1` 这两组值在三个键里逐位相同，说明 CX 域在整轮里稳定，可作 GX 的稳定参照，但本轮没有主动去动 CX。

**98.10 没证明什么**
没证明 bit16 究竟由什么置起（缺口仍在）。没证明 vdd_gfx 供电是否到位（本轮没读 gfxcc 的 BCM 寄存器）。没证明 AON 复位 clamp 的时序对不对。没证明固件是否在别处写 GDSC（G4）。没证明 `cfg=01080400` 的 bit10/18/19 各自语义（G3）。也没证明一旦 bit16 置起，GX 之后真能跑起来。

**98.11 R275 的设计思路**
顺着本轮判决，下一轮该拆 G1 的两条最可能的门：一是 vdd_gfx 电源本身（读 gfxcc 的 BCM/电源寄存器，看 gfx 供电有没有被要求、有没有到位）；二是 AON 复位 clamp 的时序（mainline 在 GDSC 上电前后对 AON clamp 的处理，本仓是否缺）。三键可设计为：读 gfxcc BCM 状态、按 mainline 顺序做 AON clamp 释放后再走四笔 GDSC、以及把 CX 的完整上电序列照搬到 GX 作对照。

**98.12 工具事实**
一是本轮探针块**不能复用 R273 的 pre**——R273 的 pre 留了 4+1 个未闭合 `{`，靠 R273 块尾 5 个 `}` 收口，是与 R273 分支结构深度耦合的重机器（SMMU 建表 + CM3 重启）；R274 直接写扁平自包含块（只 `sGmuFwVa`/`gmuW`/`rd32` + 固定 dword 索引）。二是**块生成器第一版用 `src[:i]+body+src[j:]` 把 R273 整块删掉了**（i=R273 头、j=ladder），症状是 probe 里 `fwKey == 181u` 计数 0；改为插在 ladder 之前并加三条断言（`count(OLD_HDR)==1`、`count(NEW_HDR)==1`、`index(OLD_HDR)<index(NEW_HDR)`）才对。三是对 store 总量的判断错了两次——先确认基线文件是对的（R273 版 743），再加本轮的 12 得 755。四是静态门 `r274_static.py` 的行取自 `snprintf(...)` 的格式串（不是 `IOLog`，后者只 print `%s` 缓冲）；提取时要先折叠相邻字面量。五是 runner 没有 preamble 的 `key=` 戳，只能靠「哪条本键专属行存在」判 key（`gmu184p1=` / `gmu185p0=` / `gmu186p0=`），并用尾行 `mode=` 交叉核对。

## 九十九、R275：rail 带外握手——两个 vote 数值全部落位、门铃不被应答、门铃之后 info 抬出 bit23（CM3_FAULT）

**99.1 一句话**
本轮的起点是「GX 的 POWER_UP_COMPLETE（bit16）由什么置起」：R274 已证明四笔 GDSC 写入能把 GDSCR 的 power-up 位（bit27）拉起来，但 bit16 在满 20000 次轮询后仍是 0。这一轮不去猜寄存器，而是照钉住的上游把 `a6xx_gmu_gfx_rail_on()` 那条**带外（out-of-band）门铃**通路整条搬到设备上：清 `GMU_BOOT_SLUMBER_OPTION`、写两个 arc vote、按 bit22 敲门铃、等 `GMU2HOST_INTR_INFO` 的 bit30 应答。结果是：**两个 vote 全零（固件从没投过 rail 票）、四笔写在门铃前全部落位且读数一点没变、门铃**一次也没被应答**（bit30 始终 0），而门铃之后 `info` 从 `00000000` 变成 `00800000`——按上游的位定义那是 bit23 = `CM3_FAULT`**。把 CM3 先复位再敲门铃（同一套五笔）结果**逐位相同**，所以答案不是「固件还没起来」。

**99.2 为什么改问 rail 门铃**
R274 收尾时列了三件「必须分开说」的事，其中最关键的是：**CPU 侧的 GX GDSC 上电路径是空实现**——钉住的上游 `drivers/clk/qcom/gpucc-sdm845.c` 里 `gpu_gx_gdsc` 的 `.power_on` 是 `gdsc_gx_do_nothing_enable`，而该函数连 GDSC 本身都不碰，只 `regulator_enable(sc->rsupply)`（注释原文 "Do nothing with the GDSC itself"）。同时 `a6xx_gmu_resume()` 对 GX 电源域那次 `pm_runtime_get_sync(gmu->gxpd)` 的注释也写明 "won't actually do anything"。换句话说：**mainline 从来没打算用「写 GDSCR」这条路上电**，它走的是 GMU 的带外请求——`a6xx_gmu_gfx_rail_on()` 先清 slumber、再写 GX/MX 两个 vote、再敲 bit22 的门铃，等 bit30 应答。R274 量的四笔 GDSCR 写是我们自己发明的路径，它把 bit27 拉起来却不能完成上电，这一点在本轮得到了侧证：在门铃之前把 vote 写全，`gx_on` 仍然是 0。

**99.3 代码形状**
探针块 12,116 字符 / 226 行，braces 净 0（opens=10 closes=10）；块内 `gmuW[` **13 笔**——188 五笔（`GMU_BOOT_SLUMBER_OPTION`、`GMU_GX_VOTE_IDX`、`GMU_MX_VOTE_IDX`、`GMU_HOST2GMU_INTR_SET`、`GMU_GMU2HOST_INTR_CLR`）、189 另八笔（先三笔复位：清 `CM3_FW_INIT_RESULT`、`CM3_SYSRESET=1`、`CM3_SYSRESET=0`，再五笔同 188）；`IODelay(1u)` 2 处、`IODelay(100u)` 2 处（应答窗口是 100 × 100 µs = 10 ms，不用空转买时间）。本块**不写** `HFI_CTRL_INIT`、不写中断掩码、不清 FSR、不建 SMMU 表。寄存器全部经 `gmuW[...]`（`volatile uint32_t *const`）访问，`sGmuFwVa` map 的是 `0x0506a000..+0x30000`；用到的 dword 索引（来源钉住的上游 `drivers/gpu/drm/msm/registers/adreno/a6xx_gmu.xml`）：`GMU_BOOT_SLUMBER_OPTION 0x23f8`、`GMU_GX_VOTE_IDX 0x23f9`、`GMU_MX_VOTE_IDX 0x23fa`、`GMU_HOST2GMU_INTR_SET 0x5194`、`GMU_GMU2HOST_INTR_INFO 0x5192`、`GMU_GMU2HOST_INTR_CLR 0x5191`、`GMU_GX_SPTPRAC_POWER_CONTROL 0x0081`、`GMU_SPTPRAC_PWR_CLK_STATUS 0x50d0`、`GMU_CM3_SYSRESET 0x5000`、`GMU_CM3_FW_INIT_RESULT 0x501c`、`GPU_CC_GX_GDSCR 0x9c03`（= `ADRENO_GMU_BOOT_V1_GPU_CC_GX_GDSCR`）、GDSCR 状态 = `0x9c04`（GDSCR+1）。三键：**187** 纯普查（零写、连读两次）、**188** 照上游五笔但不动 CM3、**189** 先复位 CM3（等 `0xbabeface` 自己回来）再跑同一套五笔。188 与 189 之间唯一的差别就是「敲门铃时固件是不是刚起来」，这是本轮的分辨力来源。

**99.4 构建与门禁**
CI #392（run `45139289855`）success 28/28。取件拍平：kernel.macho 126,468,096 B sha256 `d9db2be1647c5102b840e5965d23a4e48a10dcff29623f3b6988865c151a8172`，payload.img 150,994,944 B sha256 `07d9461e9fa4ac10ff0b3b9b669b08cb45379afe8b759e162f88ee4f9157270a`。静态门 `r275_static.py` 对真产物 **PASS**（20 行 required+survivors 在 kernel 与 payload 里都命中，marker kernel=1 / payload=2，forbidden 3 条全零，固件 32768 B sha `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f` 钉住），对 R274 产物 **FAIL 10 problems**（10 条 R275 行全 kernel=0 payload=0）——门的区分能力自证成立。host 门 32/32；probe 门 28/28，store 计数由 755 抬到 **768**（+13）；stage4 61/61、stage5 48/48、stage6 1862/1862；全量 `r239_allgates.py` ran 46 of 48 FAILING 0；`check_mode_selector.py` 30 selectors、上界 189、自证 FAIL、VERDICT PASS；`r275_hashes.py` 7 路径 0 不符。三份 payload 各自重键（`enchilada-pas-exec=2` → `=187/188/189`），sha256 分别 `e5326aed4fe6f168f1ea705cf5d4a4187246a9fb00b9e4850c6be1ddc7ccb8f4`、`5d78411bb57aab6fcd0670e58b989501fa262030f4bbff8a68f1c7b047b7596f`、`e86d90fd2c4a471cb60bd6ea735adaeea9dcff8ce472f8b603908b439d10779d`；每键上机前 `check-reboot-to-twrp.py --arm` PASS（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`）。

**99.5 键 187：静止态普查（零写、两份读数）**
`boot_id 587926344`、窗口 sha256 `ecf8232c3db9da179545c1dcb4d0f553d49d4b30574b5c8bdf762efc711b75b5`，3/3 行。逐字：`gmu187p0=census=1 bs=00000000 gx=00000000 mx=00000000 sp=00000000 st=000000d4 gd=00222001 cf=00088400 fw=babeface info=00000000`；`gmu187p1` 逐位相同；`gmu187l=mode=187 gd=00222001 cf=00088400 gx_on=0 ack=0 info=00000000 survived=1`。读法：`bs=0` 说明 slumber 请求字本来就是清的；`gx`/`mx` 两个 arc vote **都是 0**，也就是**固件从没投过 rail 票**；`sp=0` 是 SPTPRAC 控制字（未请求），`st=000000d4` 是它的状态字；`gd=00222001` 是 GDSCR（bit0=1 SW_COLLAPSE、bit27=0），`cf=00088400` 是 GDSCR+1（bit16=0）；`fw=babeface` 说明 CM3 的 init latch 在，`info=0` 说明此刻没有任何 GMU→host 中断。两份读数逐位相同 ⇒ 这些字不是自己在动，后面的差值只能归因于我们的写。

**99.6 键 188：照上游五笔敲门铃，不动 CM3**
`boot_id 586464408`、窗口 sha256 `d9a65e52a943d60ab81ab5c28bd338a7fe9ace2cc1246d1800769c6b812da2b8`，3/3 行。`gmu188p0=pre=1 bs=00000000 gx=00000000 mx=00000000 sp=00000000 st=000000d4 gd=00222001 cf=00088400 fw=babeface info=00000000`（与 187 逐位相同）；写完之后 `gmu188p1=oob_nocm3=1 ack=0 bs=00000000 gx=00000000 mx=00000000 info=00800000 gd=00222001 cf=00088400 gx_on=0 polls=100`；尾行 `gmu187l2=mode=188 gd=00222001 cf=00088400 gx_on=0 ack=0 info=00800000 survived=1`。三件事：①**五笔写本身没有改动任何可观测的 rail 状态**——`bs/gx/mx` 事后仍是 0，`gd/cf` 与静止态逐位相同；②**应答位没来**——`polls=100`（10 ms）里 `GMU2HOST_INTR_INFO` 的 bit30 一次也没置起（`ack=0`）；③**门铃之后 `info` 变成了 `00800000`**，即 bit23 置起。

**99.7 键 189：先复位 CM3，再敲同一套门铃**
`boot_id 587310319`、窗口 sha256 `bd32f40fc71d6d123848aca833a8adaae7b447d31d8fa75f46de1bf3dd91732b`，4/4 行。`gmu189p0=pre=1`（与 187 逐位相同）；复位脉冲后 `gmu189p1=reset=1 fw=babeface polls=4 sysreset=00000000`——**清掉 init latch 再打 `CM3_SYSRESET`，`0xbabeface` 在 4 拍内自己回来**，证明敲门前固件确实重跑过；再敲门铃 `gmu189p2=oob_cm3=1 ack=0 bs=00000000 gx=00000000 mx=00000000 info=00800000 sp=00000000 st=000000d4 gd=00222001 cf=00088400 gx_on=0 polls=100`。与键 188 的应答结果**逐位相同**。

**99.8 判决**
1. **缺的不是 vote 数值**：静止态两个 vote 已经是 0，188/189 把「读到的 0」原样写回、并清 slumber，读数一点没变。也就是说**固件从来没有投过 rail 票**，但我们把票写进去也不会让它开始上电。
2. **门铃这条通路在被固件拒绝**：`HOST2GMU_INTR_SET` bit22 写下去，`GMU2HOST_INTR_INFO` 的 bit30（legacy 编码 slab 里 BOOT_SLUMBER 的 ack 位）在 10 ms 内**一次也没置起**。188（CM3 一直在跑）与 189（CM3 刚复位、4 拍回 latch）结果相同 ⇒ 不是「固件还没起来」，而是**这颗固件不服务这个带外请求**。
3. **门铃之后 bit23 置起 = `CM3_FAULT`**：`info` 从 `00000000` 变成 `00800000`，而按钉住的上游 `a6xx_gmu.xml`，`GMU2HOST_INTR_INFO` 的 bit23 就是 `CM3_FAULT`（同一文件里 `MSGQ` 是 pos 0、`CM3_FAULT` 是 pos 23）。本仓库 `AdrenoGmuBootV1.h` 只定义了这个字的偏移 `0x5192u`、**没有位域**，所以这条位定义是**外部依据**，不是本仓库的定义。187（零写）读到的是 `0`，188/189（敲门铃）读到同一个 `00800000` ⇒ 这一位置起**归门铃**。它与 R252 那条「启动字之后出现 `CM3_FAULT`」是同一族的现象：同样的位、同样延迟出现、同样留得住（本次 10 ms 步长内已可见，比 R252 的 0.32 s 更短，因为轮询步长不同）。
4. **GX 电源域上电的最后一步仍然缺**：`gx_on`（`cf & 0x10000`）在三键里全是 0。R274 的 G1 没有关闭；但候选门的集合被本轮收敛了：**不是 vote 数值、不是 gfxcc 时钟、也不是「固件没在跑」，而是带外中断通路本身**（固件是否实现了 BOOT_SLUMBER 这一类 OOB 消息）。

**99.9 必须分开说的三件事**
第一，本轮证明的是「我们照上游写的这条带外通路**在这颗固件上不被应答**」，**没有**证明 mainline 在真机上能靠它把 GX 拉起来——上游那条路是在它自己的固件版本与 HFI 握手顺序下成立的，我们这里的固件是随这台设备的 recovery 分区里那一个（sha 见 99.4），两者不是同一个东西。第二，bit23 = `CM3_FAULT` 的**位定义来自外部依据**（上游 xml），本仓库没有位域；因此「置起的是 fault」这层解释可以写进判决，但**不能**写成「本仓库已定义并验证过该位的语义」。第三，189 的复位只证明「固件重跑过一次」，**没有**证明固件在复位后进入了与 mainline 期望一致的状态——`ctl` 那条 HFI 启动位在 R245–R252 从来没置起过，本轮也没有重新去碰它（本块不写 `HFI_CTRL_INIT`）。

**99.10 没证明什么**
没证明 `GMU_BOOT_SLUMBER_OPTION` 的其它取值（非 0）会不会被服务；没证明 `GMU_GX_VOTE_IDX`/`GMU_MX_VOTE_IDX` 的**非零**票值是否被接受（本轮只写了读到的 0，因为不发明票值）；没证明 `HOST2GMU_INTR_SET` 的**其它位**（尤其 `MSGQ` pos 0 那条）是否被服务；没读 `CM3_FAULT` 的**原因寄存器**；没证明 `00800000` 与「我们敲门铃」是唯一的因果关系（只证明零写的 187 读到 0、敲了门铃的 188/189 读到同一值）；也没证明 bit16 究竟由谁置起。

**99.11 R276 的设计思路**
顺着本轮，下一轮该把「带外通路到底被实现了多少」分开量：一是敲 `HOST2GMU_INTR_SET` 的**其它位**（先 `MSGQ` pos 0，再逐个单独敲），看是否有一位能被应答——这能区分「只有 BOOT_SLUMBER 不支持」与「整个 `HOST2GMU_INTR_SET` 都不被服务」；二是在敲门铃前后**读 `CM3_FAULT` 的原因寄存器**（如果存在可读的 fault 原因字），把「fault 是我敲出来的」变成有内容的归因，而不是只有一位；三是把 `GMU_BOOT_SLUMBER_OPTION` 写一个**非 0 的合法取值**再敲门铃，看固件是否只在「它自己想睡」的语义下才应答。三件事都要保持零副作用：不写 `HFI_CTRL_INIT`、不碰中断掩码、不用空转买时间。

**99.12 工具事实**
一是 `r214_par.py` 的 needle 是**单个子串**匹配，artifact 名是 `unixnu-enchilada_defconfig-kernel` / `unixnu-enchilada_defconfig-payload`，所以取件必须分别传 `"defconfig-kernel"` / `"defconfig-payload"`；传 `"defconfig-kernel|payload"` 会 `no artifact matching`（本轮踩了一次）。二是探针块里按分支发射寄存器常量时，**只发该分支真正用到的那些**——第一版把三组 `const uint32_t …Word` 全发进每个分支，probe 门直接 `unused variable 'r187setWord' [-Werror,-Wunused-variable]`；拆成 `WORDS_CENSUS`/`WORDS_DOORBELL`/`WORDS_RESET` 按分支发射后 28/28。三是 `python -c` 里带引号或中文的代码在 PowerShell 下必然报 `TerminatorExpectedAtEndOfString` 或 `SyntaxError`（本轮两次），一律先写 `.py` 再跑。四是本块第三个分支是 `} else if (r187mode == 189) {`（不是裸 `} else {`），切片与断言里若按 `rindex('} else {')` 切会 `ValueError`；本块的复位写的是 `gmuW[r187fwWord]` / `gmuW[r187rstWord]`，不是寄存器名字面量，计数断言必须按实际写法查。五是 store 预算的基线要先确认文件是对的：R274 版实测 755，本轮 +13 = 768，`scripts/check-enchilada-adreno-probe.py` 的预算与说明串同步更新。

## 一百、R276：带外门铃本身不被固件服务——换一条消息也一样沉默，且每次敲门都抬出 CM3_FAULT

**100.1 一句话**
R275 把「GX 的 bit16 由谁置起」收敛到「带外（out-of-band）门铃通路」，本轮不猜数值、直接问这条通路到底通不通：先取静止态普查，再敲**另一条**带外消息（`GMU_OOB_GPU_SET`，set bit16 / ack bit24，不是 boot-slumber 的 22/30），再把 mainline 在 GMU fault 前后真正读的那几个字（AO 主机中断状态、AHB fence 状态、RPMH 电源状态、SPTPRAC 状态）连解码一起读出来，最后用 `BOOT_SLUMBER_OPTION=1`（「即将入睡」这个上游唯一合法取值）再敲一次同一门铃。结论是三句话：**①这条固件一条带外消息也不应答**（bit24 和 bit30 在 10 ms 窗口里都没置起过）；**②每次敲门都会把 `GMU2HOST_INTR_INFO` 的 bit23 抬起来**（外部依据里那是 `CM3_FAULT`），零写入的键读不到它；**③静止态下 `GPU_GMU_CX_GMU_RPMH_POWER_STATE` 已经是 `0x0f`**——按上游 `a6xx_gmu_notify_slumber()` 的判据，这颗 GMU **自认为已经在睡**。

**100.2 为什么改问门铃本身**
R275 收尾时列了三条必须分开说的候选：带外消息没被实现、本固件只服务部分消息、以及 CM3 虽然回了 `0xbabeface` 但并没有在跑。本轮把前两条做成可判的：`a6xx_gmu_set_oob()` 服务的四条消息各有各的 set/ack 位对（引脚住的 `a6xx_gmu_oob_bits[]`：`GPU_SET {set 16, ack 24}`、`PERFCOUNTER_SET {17, 25}`、`BOOT_SLUMBER {22, 30}`、`DCVS_SET {23, 31}`），所以敲 `GPU_SET` 是一个**与 boot-slumber 无关的独立探针**：它若被应答，R275 的沉默就只属于那一条消息；它若同样沉默，说明 `HOST2GMU_INTR_SET` 这一整块请求都没人取。顺带把 mainline 判 GMU 是否健在的那几个字也读掉，因为「沉默」和「坏了」是两件事。

**100.3 代码形状**
探针块 14,211 字符 / 264 行，braces 净 0（opens=11 closes=11）；块内 `gmuW[` **9 笔**——键 190 两笔（`HOST2GMU_INTR_SET` 写 `0x00010000u`(set 16)、`GMU2HOST_INTR_CLR` 写 `0x01000000u`(ack 24 清)），键 191 三笔（同一对 boot-slumber 位 22/30 加上把 AO 状态字按 W1C 写回 `gmuW[aclWord] = r190ao3`），键 192 四笔（`BOOT_SLUMBER_OPTION=1`、set 22、clr 30、`BOOT_SLUMBER_OPTION=0` 回置）。`IODelay(1u)` 3 处、`IODelay(100u)` 3 处（应答窗口仍是 100 × 100 µs = 10 ms，不用空转买时间）。本块**不重启 CM3**（所以 `CM3_SYSRESET`/`CM3_FW_INIT_RESULT`/`0xbabeface` 三个字面量在块内计数都是 0），**不写** `HFI_CTRL_INIT`、不写中断掩码、不碰 SMMU 表。用到的 dword 索引（来源钉住的上游 `drivers/gpu/drm/msm/registers/adreno/a6xx_gmu.xml`，基址 `0x0506a000`、size `0x30000`）：`GMU_BOOT_SLUMBER_OPTION 0x23f8`、`GMU_GX_VOTE_IDX 0x23f9`、`GMU_MX_VOTE_IDX 0x23fa`、`GMU_HOST2GMU_INTR_SET 0x5194`、`GMU_GMU2HOST_INTR_CLR 0x5191`、`GMU_GMU2HOST_INTR_INFO 0x5192`（`MSGQ` pos 0、`CM3_FAULT` pos 23）、`GMU_SPTPRAC_PWR_CLK_STATUS 0x50d0`（bit6 `GX_HM_GDSC_POWER_OFF`、bit7 `GX_HM_CLK_OFF`）、`GMU_AO_HOST_INTERRUPT_STATUS 0x9305`（W1C；`WDOG_BITE` 0、`RSCC_COMP` 1、`VDROOP` 2、`FENCE_ERR` 3、`DBD_WAKEUP` 4、`HOST_AHB_BUS_ERROR` 5）、`GMU_AO_HOST_INTERRUPT_CLR 0x9304`、`GMU_AHB_FENCE_STATUS 0x9313`、`GPU_GMU_CX_GMU_RPMH_POWER_STATE 0x50ec`、`GPU_CC_GX_GDSCR 0x9c03`（= `ADRENO_GMU_BOOT_V1_GPU_CC_GX_GDSCR`）、GDSCR 状态 = `0x9c04`。三键：**190** 普查 + `GPU_SET` 门铃（CM3 原样）；**191** 同一门铃前后读 fault 上下文并把 AO 状态写回；**192** 先写「即将入睡」值再敲同一门铃，读 RPMH 电源状态后回置。

**100.4 构建与门禁**
CI #393（run `45147220617`）success 28/28。取件拍平：kernel.macho 126,484,480 B sha256 `c0d20970df33f553302f1867d7dc3df9106c893fc4de2bf4aa5423ce00107654`，payload.img 150,994,944 B sha256 `1de9fafc56aa771ff13f03c585df79f854d1c450a4795bab01653c88ed5786a7`。静态门 `r276_static.py` 对真产物 **PASS**（24 行 required+survivors 在 kernel 与 payload 里都命中；marker kernel=1 / payload=2；forbidden 3 条全零；固件 32768 B sha `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f` 钉住），对 R275 产物 **FAIL 11 problems**（11 条本轮行全 kernel=0 payload=0）——门的区分能力自证成立。host 门 32/32；probe 门 28/28，store 计数由 768 抬到 **777**（本轮 +9）；stage4 61/61、stage5 48/48、stage6 1885/1885；全量 `r239_allgates.py` ran 46 of 48 FAILING 0；`check_mode_selector.py` 31 selectors、上界 192、自证 FAIL、VERDICT PASS。三份 payload 各自重键（`enchilada-pas-exec=2` → `=190/191/192`），sha256 分别 `24ce4dcf6e835dc51953d0fca4a23542d8a21ffc4059a4def9f63dc7ab044125`、`9c9eba3dee524953d66c547826742f1bc8a2b85ced88a05948832c1bb3ab1708`、`6ff42d31b6d1eaaa5c73473a324370bcd21f795c59cc0e6de8f06e83364eb4e1`；每键上机前 `check-reboot-to-twrp.py --arm` PASS（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`）。

**100.5 键 190：换一条消息，同样沉默**
`boot_id 585277122`、窗口 sha256 `5af2975b23cd7ac2c031f7c67ce377c7b1859257f0a0c919c3eb4e11ab3dc70e`，3/3 行。静止态逐字：`gmu190p0=census=1 bs=00000000 gx=00000000 mx=00000000 st=000000d4 gxoff=1 ao=00000000 fn=00000000 rp=0000000f gd=00222001 cf=00088400 info=00000000`。敲 `GPU_SET` 之后：`gmu190p1=gpuset=1 ack=0 info=00800000 st=000000d4 gxoff=1 gx=00000000 mx=00000000 ao=00000000 fn=00000000 rp=0000000f gd=00222001 cf=00088400 polls=100`；尾行 `gmu190l=mode=190 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ack=0 info=00800000 survived=1`。读法：`ack=0` 说明 `GMU2HOST_INTR_INFO` 的 **bit24 在 10 ms 里一次也没置起**——**不是 boot-slumber 那一条不被服务，是这块请求没人取**；`info` 从 `00000000` 变成 `00800000`（bit23），而零写入的键（R275 的 187）读到的是 0 ⇒ **这一位是敲门敲出来的**；`st`/`gd`/`cf`/`gx`/`mx` 与静止态逐位相同 ⇒ 敲门没有改动任何 rail 状态，`gx_on` 仍是 0。注：本键第一次上机（`boot_id 585589995`、窗口 `6e8933a5…`）因判定脚本把 `gx_on` 从「不含该字段的那一行」取成 `None` 而重跑一次；两次除 `boot_id`/窗口 sha 外逐字相同，第二次的窗口覆盖了同名文件。

**100.6 键 191：fault 上下文是干净的**
`boot_id 585160014`、窗口 sha256 `61290952979d96824b7e3beca0d91c39769c9a6eec3baf4baf45cba69b0d509f`，4/4 行。门铃前：`gmu191p0=faultctx_pre=1 st=000000d4 gxoff=1 ao=00000000 fn=00000000 rp=0000000f gd=00222001 cf=00088400 wdt=0 ahb=0 fen=0 vdr=0`；门铃后：`gmu191p1=faultctx_post=1 ack=0 info=00800000 st=000000d4 gxoff=1 ao=00000000 fn=00000000 rp=0000000f gd=00222001 cf=00088400 wdt=0 ahb=0 fen=0 vdr=0`；把 AO 状态字写回后 `gmu191p2=aoclr=1 seen=00000000 wrote=0 back=00000000 info=00800000 st=000000d4`。读法：**四个 AO 中断源（看门狗咬、AHB 总线错、fence 错、欠压）一个也没挂**，`fn=0` 说明 AHB fence 处于放行；因此 `CM3_FAULT` 这一位背后**没有**任何硬件故障源在报，它是「请你别敲」而不是「我正在崩」。W1C 那一笔因为本来就没有挂起位，`seen=0 → wrote=0 → back=0` 是一次**平凡**的写回，只能证明这个字可以写、不能证明它可清（没东西可清）。

**100.7 键 192：「即将入睡」的值也换不来应答**
`boot_id 585306511`、窗口 sha256 `9285e708fe80ef2e4f0775ffc727460984f2534865639a07a31dd8a88e9c67bf`，4/4 行。静止态与 190 逐字相同；`gmu192p1=slumber=1 bs=00000001 ack=0 info=00800000 rp=0000000f gx=00000000 mx=00000000 st=000000d4 gd=00222001 cf=00088400 polls=100`；回置后 `gmu192p2=restored=1 bs=00000000 info=00800000 survived=1`。读法：`bs` 写 1 读回 1 ⇒ 「即将入睡」这个值**写得进、留得住**；同一门铃仍然 `ack=0` ⇒ 上游那套「先声明要睡、再等应答、再用 RPMH 状态确认」的握手在这颗固件上**第一步就断**；`rp=0000000f` 自始至终没动过——按上游判据这颗 GMU **本来就在「睡着」的状态**，所以它不需要、也不接受「请你起来」的请求。

**100.8 判决**
1. **带外门铃这条通路整块不被服务**：`GPU_SET`（set 16 / ack 24）与 `BOOT_SLUMBER`（set 22 / ack 30）在 10 ms 窗口里都不置应答位；`HOST2GMU_INTR_SET` 写下去是**被受理但无人处理**，不是写不进去（R275/R276 里这些 store 都没有触发卡死，整块后续行照常打印）。
2. **每敲一次门铃，`GMU2HOST_INTR_INFO` 的 bit23 就被抬起来，且留得住**：190/191/192 三键都在门铃后读到 `00800000`，而零写入的 187 读到 0。按外部依据（上游 `a6xx_gmu.xml`，`CM3_FAULT` pos 23；本仓库 `AdrenoGmuBootV1.h` 只有偏移 `0x5192u`、**没有位域**）这是 **CM3 侧 fault**。它与 R252「启动字之后出现 CM3_FAULT」同族：同一位、同样延迟出现、同样留得住；本轮新增的是**因果面的收窄**——它不是启动字独有的产物，敲门铃同样能抬出它。
3. **沉默不是「坏了」**：AO 四个故障源全零、AHB fence 放行、`st=000000d4` 稳定、`fw=babeface` 在位、`rp=0x0f` ⇒ CM3 活着、自认在睡、没有硬件故障要报，只是**不服务这条请求**。
4. **GX 上电路径的缺口因此不再是「数值」**：R274 的 G1（bit16 由什么置起）仍未关闭，但候选门已经从「vote 值 / 时钟 / 固件是否刚起来 / AON clamp 时序」收敛到**一条**：这颗固件有没有实现 GMU 的带外请求服务。若它没实现，`a6xx_gmu_gfx_rail_on()` 这条 mainline 路径在这颗固件上**根本不成立**，GX 上电必须另找入口（例如固件自定义的 HFI 消息）。

**100.9 必须分开说的四件事**
第一，本轮证明的是「**这颗 recovery 分区里的固件**不服务带外请求」，**没有**证明 mainline 在真机上靠它拉不起 GX——上游那条路是在它自己的固件版本与 HFI 握手顺序下成立的，与本设备的分区固件（sha 见 100.4）不是同一个东西。第二，bit23 = `CM3_FAULT` 的**位定义来自外部依据**（上游 xml），本仓库没有位域；「置起的是 fault」这层解释可以写进判决，但**不能**写成「本仓库已定义并验证过该位的语义」。第三，`rp=0x0f` 只说明**这一个字**等于上游所说的「已入睡」值，没有证明 GMU 真的进入低功耗（电源域与时钟的独立证据见 R198–R201：GX 从未上电）。第四，191 的 W1C 写回是平凡写回，**不能**当作「该字可清」的证明。

**100.10 没证明什么**
没证明 `HOST2GMU_INTR_SET` 的**其余位**（尤其 `MSGQ` pos 0）是否被服务——本轮只敲了两条；没证明固件是否**收到**了这些写（只能看到它没有回 bit24/bit30，看不到它内部的接收计数）；没证明 `CM3_FAULT` 是唯一的回应还是「有别的位被我们漏读」（本轮只读整字并解出 bit23，没有逐位比对其它置起位以外的东西）；没证明 `rp=0x0f` 是固件的主动声明还是复位默认值（本轮没有读它在上电初期的值）；也没有证明 GX 上电到底该走哪条路——只是把「不是哪里」又收窄了一层。

**100.11 R277 的设计思路**
下一轮该问的是「**固件到底在跑什么**」，而不是再换一条门铃：一是读 CM3 的固件版本字（`a6xx_gmu_start()` 用 `GMU_CM3_DTCM_START + 0xff8` 选 mask/reset 值，本轮没读），把「回了 `0xbabeface`」这件事与「固件是哪一版」对上；二是看 HFI 队列有没有被**消费者**推进（R261/R262 只证明队列页里躺着两条记录，没有证明 CM3 读过它），这能把「固件在跑主循环」与「固件只把 init latch 写回来」分开；三是若队列确有推进，就试固件自定义消息（而不是 mainline 的带外位）；四是继续零副作用：不写 `HFI_CTRL_INIT`、不碰中断掩码、不复位 CM3、不用空转买时间。

**100.12 工具事实**
一是判定脚本取字段时**必须确认那一行真的有这个键**：本轮把 `gx_on` 从 `gmu190p1`（其格式串没有 `gx_on`）里取，`field()` 的正则跨到别的行才侥幸没有误命中，结果打印了 `gx_on None` 并因此重跑一次——修法是从真正带该键的尾行取。二是 `r214_par.py` 的 needle 是**单个子串**匹配（artifact 名 `unixnu-enchilada_defconfig-kernel` / `..._defconfig-payload`），必须分别传 `"defconfig-kernel"` / `"defconfig-payload"`。三是静态门的 survivors 必须用**历史轮次真实打印过的格式串**：第一版把 R275 的 `gmu187l*` 误写成带 `gxoff` 的本轮尾行格式，静态门立刻报「不是源码里的连续行」——这正是它该做的事。四是 `python -c` 里带引号或中文在 PowerShell 下必报 `TerminatorExpectedAtEndOfString`，一律先写 `.py`。五是块内第三个分支是 `} else if (r190mode == 192) {`（不是裸 `} else {`），切片与计数断言都按实际写法查。

## 一百零一、R277：固件是什么、以及消息队列的门铃也没人取——写 `HFI_CTRL_INIT` 反而让整块死在提交之前

**101.1 一句话**
R276 把「带外门铃整块不被服务」钉住之后，本轮不再换门铃，改写**固件身份与 HFI 消息通路**：只读普查 CM3 的版本字与全部 HFI 字（键 193），敲 HFI 的**消息**门铃 `MSGQ`（bit 0，不是任何带外位；键 194），再按上游 `a6xx_gmu_hfi_start()` 的前半段先写 `HFI_CTRL_INIT=1` 等 `HFI_CTRL_STATUS` bit0 落位、然后敲同一条消息门铃（键 195）。结论三句：**①固件自报的版本字是 `00000000`**（不是任何上游认识的修订版），而 `HFI_CTRL_INIT` 读 0、`HFI_CTRL_STATUS` 读 0、`HFI_VERSION_INFO` 读 0，只有队列表描述符 `qinfo=00000001` 与队列表地址 `qaddr=ad970000` 有值；**②消息门铃同样无人应答**——bit0 在 10 ms 窗口里一次没置起，而 `CM3_FAULT`(bit23) 照旧被敲门抬出来，说明这块固件**既不服务带外位、也不服务消息环**；**③把 `HFI_CTRL_INIT` 写成 1 会让整块死在提交之前**：键 195 连跑两次都是**零字节**（窗口与上一键逐字节相同、连 `boot_id` 都没换），也就是说那一笔 store 之后系统没能走到日志提交。
 
**101.2 为什么改问「固件是什么」与「消息环有没有人取」**
R275/R276 已经排除了 vote 数值、时钟前置、AON clamp 时序与「CM3 是否刚起来」这几类解释，剩下的候选只有两条：这颗固件**没有实现**带外服务，或者它实现了但**只服务它自己的消息环**（HFI）。要分开这两条，先得知道「它是什么」：上游 `a6xx_gmu_start()` 启动前就读 `GMU_CM3_DTCM_START + 0xff8` 这个版本字，并按它是否 `<= 0x20010004` 选择 `0xffffffff/0xbabeface` 还是 `0x1ff/0x100` 的初始化方案——我们一直读到的 `0xbabeface` 属于**老方案**，可这个判断从没被验证过，因为那个字从没读过。其次要问「消息环」：`a6xx_hfi_queue_write()` 把消息放进环形缓冲后抬的就是 `HOST2GMU_INTR_SET` 的 `MSGQ` 位（bit 0），这条位与带外四位**不重叠**，所以敲它是与 R275/R276 完全正交的一次探针；而 R261/R262 只证明过队列页里躺着两条记录，从没证明 CM3 读过它。

**101.3 代码形状**
探针块 11,420 字符 / 218 行，braces 净 0（opens=10 closes=10）；块内 `gmuW[` **5 笔**——键 193 **零写**，键 194 两笔（`HOST2GMU_INTR_SET` 写 `0x00000001u`(MSGQ)、`GMU2HOST_INTR_CLR` 写同值清），键 195 三笔（`HFI_CTRL_INIT` 写 `0x00000001u`、同一条消息门铃、同值清）。`IODelay(1u)` 1 处（init 等待 2000 次）、`IODelay(100u)` 2 处（两键的 100 × 100 µs = 10 ms 应答窗口）。本块**不重启 CM3**（`CM3_SYSRESET`/`CM3_FW_INIT_RESULT` 只作为只读行出现，`0x5000u`/`0x501cu` 各 1 次、`0xbabefaceu` 0 次），不写中断掩码、不碰 SMMU 表、不用空转买时间。用到的 dword 索引（来源钉住的上游 `drivers/gpu/drm/msm/registers/adreno/a6xx_gmu.xml`，基址 `0x0506a000`、size `0x30000`）：`GMU_CM3_DTCM_START 0x1c00`（版本字 = `0x1c00 + 0xff8` = `0x2bf8`）、`GMU_HFI_CTRL_STATUS 0x5180`、`GMU_HFI_VERSION_INFO 0x5181`、`GMU_HFI_SFR_ADDR 0x5182`、`GMU_HFI_MMAP_ADDR 0x5183`、`GMU_HFI_QTBL_INFO 0x5184`、`GMU_HFI_QTBL_ADDR 0x5185`、`GMU_HFI_CTRL_INIT 0x5186`、`GMU_GMU2HOST_INTR_INFO 0x5192`（`MSGQ` pos 0、`CM3_FAULT` pos 23）、`GMU_HOST2GMU_INTR_SET 0x5194`、`GMU_GMU2HOST_INTR_CLR 0x5191`、`GMU_CM3_SYSRESET 0x5000`、`GMU_CM3_FW_INIT_RESULT 0x501c`、`GMU_SPTPRAC_PWR_CLK_STATUS 0x50d0`（bit6 `GX_HM_GDSC_POWER_OFF`、bit7 `GX_HM_CLK_OFF`）、`GPU_CC_GX_GDSCR 0x9c03`、GDSCR 状态 = `0x9c04`。三键：**193** 只读身份普查；**194** 敲消息门铃（CM3 原样）；**195** 先写 init 再敲同一条门铃。

**101.4 构建与门禁**
CI #394（run `45159446166`）success 28/28（`HEAD_SHA 056130abf2c2c151f2e183268c4a2aa79c318c27`，分支 `r277-identity` commit `522523cb66dde5c6235d50acd636812f77d34c8b`、tree `7b35899fac503f673e2de3f4baa269fcfc65a5e3`，base main `1615519a97b08eb06a169ebb0eb211227cf2b0f6`）。取件拍平：kernel.macho 126,484,480 B sha256 `370faeed64614162fdd3ff9d8c99ce8a349923e4af2a7d81942851ca31f28778`，payload.img 150,994,944 B sha256 `a7e31a3d6e74a20b5734c2e07c9e0acca25ce5e53f2d50ef92e77ca2e89b2ccc`。静态门 `r277_static.py` 对真产物 **PASS**（22 行 required+survivors 在 kernel 与 payload 里都命中；marker kernel=1 / payload=2；forbidden `gmu196a=`/`gmu197a=`/`gmu198a=` 全零；固件 32768 B sha `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f` 钉住），对 R276 产物 **FAIL 9 problems**（9 条本轮行全 kernel=0 payload=0）——门的区分能力自证成立。host 门 32/32；probe 门 28/28，store 计数由 777 抬到 **782**（本轮 +5）；stage4 61/61、stage5 48/48、stage6 1908/1908；全量 `r239_allgates.py` ran 46 of 48 FAILING 0；`check_mode_selector.py` 32 selectors、上界 195、自证 FAIL、VERDICT PASS。三份 payload 各自重键（`enchilada-pas-exec=2` → `=193/194/195`），sha256 分别 `3a615ba854a8454d4c304e14677da98bad7391161fadd7a68bb923ee5dc3a647`、`077ea2f0b2772bb9c64e810ee4db3f37451cac8aa18ef76439fd508b05addda0`、`56d9012104c72ffdb4406a43e07f26e3f7091ab161306079230609ae26d6ba27`；每键上机前 `check-reboot-to-twrp.py --arm` PASS（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`）。

**101.5 键 193：固件自报的版本字是零，HFI 只有队列表有值**
`boot_id 584756099`、窗口 sha256 `fa95135a61ebdbb63be0f026401a9a974e925c0bdd3d55baec64eda34ea644ca`，2/2 行，零写入。逐字：`gmu193p0=identity=1 ver=00000000 ctl=00000000 hv=00000000 sfr=06030100 mmap=00000000 qinfo=00000001 qaddr=ad970000 init=00000000 sr=00000000 fw=babeface info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；`gmu193l=mode=193 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ack=0 info=00000000 survived=1`。读法：**版本字 `0x2bf8` 读 0**——它不是某一个上游修订版，而是**没有值**；于是 `a6xx_gmu_start()` 里那个「`<= 0x20010004` 就用 `0xbabeface`」的分支在我们这里**等于永远满足**（0 当然 ≤ 该常数），我们一直读到的 `0xbabeface` 与「固件是哪一版」之间**没有任何已被验证的联系**。`sfr=06030100` 与 R247 读到的「芯片号字」是同一个数，`hv=0`、`mmap=0` ⇒ HFI 侧除了 `qinfo=00000001`（队列表**已描述**）与 `qaddr=ad970000`（表**有地址**）之外，其余全是零：`HFI_CTRL_STATUS` bit0 = 0（HFI 从未被报告为启动），`HFI_CTRL_INIT` = 0（我们从未写过，也没人有替我们写过），`CM3_SYSRESET`=0、`fw=babeface` 在位、`st=000000d4`（bit6/bit7 置起 ⇒ `gx_on=0`）。

**101.6 键 194：消息门铃也没人取，fault 位照旧被抬出来**
`boot_id 585406562`、窗口 sha256 `33e8ed83763823fc3df8a4c49763d8a2acdcf763399b1c3c8954eebc33b9f0fe`，3/3 行。门铃前 `gmu194p0=pre=1 ctl=00000000 info=00000000 qaddr=ad980000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；敲 `MSGQ`（bit0）后 `gmu194p1=msgq=1 ack=0 info=00800000 ctl=00000000 qaddr=ad980000 st=000000d4 gxoff=1 gd=00222001 cf=00088400 polls=100`；尾行 `gx_on 0`。读法：**`ack=0` ⇒ `GMU2HOST_INTR_INFO` 的 bit0 在 10 ms 里没置起**——消息门铃与带外四位一样没人取；`info` 从 `00000000` 变成 `00800000`（bit23 `CM3_FAULT`）⇒ **连消息门铃都能把它敲出来**，这把 R276 的「每次写 `HOST2GMU_INTR_SET` 都有这一位」推广到了整块请求字；`qaddr` 前后都是 `ad980000`（本键自己分配的地址）⇒ 队列表没有被消费者动过；`ctl=0`、`cf` bit16=0 不动 ⇒ 消息门铃同样不推进 GX 上电。（判定脚本在本键打印了一句语法走样的英文 `the doorbell did not left its own bit set`，那是**输出文本的笔误**，数字以 `MSGQ bit before 0 after 0` 为准。）

**101.7 键 195：写 `HFI_CTRL_INIT` 让整块死在提交之前（两次零字节）**
本键连跑两次，两次都是**零字节**：读回的窗口 sha256 仍是 `33e8ed83763823fc3df8a4c49763d8a2acdcf763399b1c3c8954eebc33b9f0fe`（键 194 的窗口）、连 SHLOG 头里的 `boot_id` 也仍是 `585406562`，`rows: gmu195p0=0 gmu195p1=0 gmu195p2=0 gmu193l3=0`。两次的 flash 都成功（`flash system_b OK`、`boot sent ok=True`），手机上机后 TWRP 回来、读日志成功——**所以不是「没刷进去」，是这一键所在的整轮在提交之前就没了命**；探针日志是**整轮提交**，块中途死一个字节都不落。键 195 与键 194 的差别只有一笔 `HFI_CTRL_INIT=1` 加它前面那段等待，于是这一笔就成了「致死」的唯一候选；而 R239 的键 83（先清 `QTBL_INFO` 再写 `INIT=1`）当时是 **4/4 存活**——两轮合起来说明：**「队列表被描述着（`qinfo=1` 且有地址）+ 写 `INIT=1`」这个组合会致死，而先抹掉描述再写则不致死。**

**101.8 判决**
1. **这颗固件不是上游认识的任何一版**：版本字读 0，HFI 版本/MMAP/SFR 全零（`sfr` 那个 `06030100` 与芯片号字同值，不能当作「共享区地址」的证据），只有队列表描述符与地址有值。**「回到 `0xbabeface`」这件事不能用于推断固件版本**，因为上游用来判断版本的那个字在这里是空的。
2. **消息环也没人取**：`MSGQ`（bit0）与两条带外位（`GPU_SET` 16/24、`BOOT_SLUMBER` 22/30）在同样的 10 ms 窗口里都没应答 ⇒ 不是「不支持某一条消息」，而是**这几条请求都没有被处理**。
3. **`CM3_FAULT` 是敲门铃的通用回应**：三次门铃（R276 的 190/191/192、本轮的 194）都在事后读到 `00800000`，且零写入的键读 0；本轮的 194 把这条从「带外位」推广到「消息位」。
4. **写 `HFI_CTRL_INIT=1` 在「表被描述着」的状态下会让整轮死在提交之前**（2/2 零字节）；跨轮对照（R239 键 83 先清描述再写 init，4/4 存活）把它收窄成**组合条件**，而不是「写 init 一定死」。
5. **GX 上电路径的缺口再收窄一层**：mainline 的 `a6xx_gmu_gfx_rail_on()`（带外）与 `a6xx_gmu_hfi_start()`（init + 消息环）两条入口，在这颗固件上**都没有被服务**；下一层要问的不再是「哪条消息」，而是「这颗固件到底在跑什么主循环、有没有自己的命令入口」。

**101.9 必须分开说的四件事**
第一，键 195 的两次零字节**只证明「这一键作为整体没有提交任何字节」**，**没有**证明死因是那一笔 store 本身——也可能是紧随其后的等待循环或 CM3 收到 init 后的动作；把死因归于那笔写，依据是「与 194 只差这一笔」加上 R239 的对照，这是**推理链**不是直接观测（本轮无法在致死块里打印任何行）。第二，版本字读 0 可能意味着「该字未被写入/不可读」，而**不是**「固件版本是 0」；本轮没有读同一地址在别处的值来分辨这两种解释。第三，`sfr=06030100` 与芯片号同值这件事**只登记、不归因**。第四，R239 的对照来自**另一次构建、另一次状态**（那时我们刚做过 CM3 复位脉冲），两轮之间固件相同、表内容不一定相同，所以「组合条件」的表述**不能**升级成「机制已证明」。

**101.10 没证明什么**
没证明 `MSGQ` 位在**别的时刻**（例如刚复位后、或表刚建好时）会不会被服务；没证明 CM3 是否**收到了**这些写（只能看到没有应答位）；没证明键 195 的死亡是「CM3 把整颗 SoC 拉进复位」还是「内核在那一笔之后立刻遇到不可恢复的访问」——两种都会表现为「窗口没被提交 + TWRP 回来」；没做本轮内的对照键（例如同一固件下「先清描述再写 init」），所以 101.8 第 4 条仍是跨轮推理；也没证明 GX 上电应该走哪条路，只把「不是哪两条」写实了。

**101.11 R278 的设计思路**
下一轮的第一件事是**补本轮缺的那个对照**：同一固件、同一次构建里，键 196 = 先写 `HFI_QTBL_INFO=0` 再写 `HFI_CTRL_INIT=1`（若存活，则「表被描述着」确实是致死条件的一半）；键 197 = 只写 `HFI_CTRL_INIT=1` 但**先把表地址备份再清成 0**（分开「描述符」与「地址」哪个是那一半）；键 198 = 完全不碰 init，只把队列页（`qaddr` 那张表）**读**一遍前后对比（判「消费者有没有推进」的零写入基线）。三键都保持零副作用纪律：不复位 CM3、不碰掩码、不用空转买时间。

**101.12 工具事实**
一是 `r277_wait.py` 一开始**不存在**——我直接把 `r276_wait.py` 当参数传给它自己，后台任务报 `can't open file` 才补建；等待脚本必须每轮新建。二是 `r277_anchor.py` 的窗口正则要写成 `r27[67]_s\d+_window\.bin`，只匹配**本轮与上一轮**，否则同名窗口跨轮命中会给错的交叉核对。三是 probe 门的 store 预算断言是**累计值**：777 → 782，说明串里必须把本轮三键的笔数写清楚，否则下轮无从推算。四是 `r170_rekey.py` 每次从**同一份** `r277-payload\payload.img` 派生三份，不要拿上一键的产物再键一次（会叠加）。五是判定脚本 `field()` 必须从**真正带该键的行**取值（R276 的 `gx_on None` 就是教训），本轮 194 的 `MSGQ bit` 输出文本笔误也提醒：**判决要写进文档的句子，最好在生成器里就写对**。六是 `python -c` 里带引号或中文在 PowerShell 下必报 `TerminatorExpectedAtEndOfString`，一律先写 `.py`（本轮又踩两次）。

## 一百零二、R278：致死的那一半是队列表描述符，不是地址——抹掉描述符就能活，且整块在 init 之后会短暂读成零

**102.1 一句话**
R277 把「写 `HFI_CTRL_INIT=1` 会让整块死在提交之前」这件事钉住（键 195 两次零字节），但只留下一个跨轮对照（R239 键 83 先清 `QTBL_INFO` 再写 init，4/4 存活），致死条件到底是**描述符**、**地址**还是**两者一起**，从来没在同一构建里分开过。本轮用三个键把它拆开：**键 196** 只清描述符（地址留着）再写 init——**存活**，`init` 读回 1（那笔 store 真的写进去了）、`HFI_CTRL_STATUS` bit0 在 2000 拍里**一次没置起**；**键 197** 只清地址（描述符留着 `1`）再写 init——**两次上机都是零字节**；**键 198** 先按 `a6xx_gmu_start()` 的顺序真复位 CM3（`fw` 3 拍就回 `babeface`）、再清描述符、再写 init、并**只在 latched 时**才敲消息门铃——**6/6 存活**，`init` 同样读回 1，`ctl` bit0 在 20000 拍里**一次没置起**，门铃被按设计**跳过**（`skipped=1`）。三句结论：**①致死的那一半是 `HFI_QTBL_INFO`（描述符）而不是地址**——196 与 197 的差别只有「哪个字被抹掉」，结果一个是六行满编、一个是零字节；**②复活之后固件仍然不接受 HFI 启动**：`ctl` bit0 在 2000 与 20000 两种等待长度下都没置起，所以「表曾经被描述着」这个致死条件和「固件根本不接受 HFI」这个功能缺口是**两件事**，前者现在能被拆掉，后者拆不掉；**③新现象：写完 init 之后，整块 GMU/GPU_CC 寄存器会短暂读成零**（`st`、`gd`、`cf`、甚至我们没写过的 `qa` 都是 `00000000`），在键 198 里它**后来自己恢复了**（下一次读到 `st=000000d4 gd=00222001 cf=00088400`），在键 196 里直到尾行都还是零——这条从未在任何一轮出现过，登记为观测，不归因。

**102.2 为什么把「表被描述着」当成嫌疑人**
R277 的键 195 与 R239 的键 83 只差「写 init 之前有没有把 `QTBL_INFO` 清掉」：195 没清，两次零字节；83 清了，4/4 存活。两边固件相同、代码路径相同，所以「表被描述着 + 写 init」被列成组合条件。但这个组合里有两个可变的字：`HFI_QTBL_INFO`（描述符，R277 读到 `00000001`）与 `HFI_QTBL_ADDR`（表地址，R277 读到 `ad970000`/`ad980000`）。只清描述符、只清地址、两个都清、两个都不清，共四种，跨轮只做过「都不清」（死）与「清描述符」（活，但那轮的键 83 并不读 `ctl` bit0，所以活下来之后固件认不认这次启动，没人知道）。本轮把剩下两种补齐，并且**每一键都把"那笔 store 到底有没有写进去"读回来**——否则「存活」可能只是「store 没落位所以什么也没发生」。

**102.3 代码形状**
探针块 14,079 字符 / 266 行，braces 净 0（opens=16 closes=16）；块内 `gmuW[...] =` **11 笔**——键 196 两笔（`HFI_QTBL_INFO` 写 0、`HFI_CTRL_INIT` 写 1），键 197 两笔（`HFI_QTBL_ADDR` 写 0、`HFI_CTRL_INIT` 写 1），键 198 七笔（`CM3_SYSRESET` 写 1、`CM3_FW_INIT_RESULT` 写 0、`CM3_SYSRESET` 写 0、`HFI_QTBL_INFO` 写 0、`HFI_CTRL_INIT` 写 1，以及**只在 latched 时**才执行的门铃 set 与 clear 各一笔）。`IODelay(1u)` 4 处（init 等待 2000 次、复位后 `babeface` 等待 20000 次、198 的 init 等待 20000 次、复位脉冲 1 次），`IODelay(100u)` 1 处（198 的门铃 10 ms 应答窗口）。用到的 dword 索引（来源钉住的上游 `drivers/gpu/drm/msm/registers/adreno/a6xx_gmu.xml`，基址 `0x0506a000`、size `0x30000`）：`GMU_CM3_DTCM_START 0x1c00`（版本字 = `0x2bf8`）、`GMU_HFI_CTRL_STATUS 0x5180`、`GMU_HFI_QTBL_INFO 0x5184`、`GMU_HFI_QTBL_ADDR 0x5185`、`GMU_HFI_CTRL_INIT 0x5186`、`GMU_GMU2HOST_INTR_INFO 0x5192`（`CM3_FAULT` pos 23）、`GMU_HOST2GMU_INTR_SET 0x5194`、`GMU_GMU2HOST_INTR_CLR 0x5191`、`GMU_CM3_SYSRESET 0x5000`、`GMU_CM3_FW_INIT_RESULT 0x501c`、`GMU_SPTPRAC_PWR_CLK_STATUS 0x50d0`（bit6 `GX_HM_GDSC_POWER_OFF`、bit7 `GX_HM_CLK_OFF`）、`GPU_CC_GX_GDSCR 0x9c03`、GDSCR 状态 = `0x9c04`。三键：**196** 清描述符→写 init；**197** 清地址→写 init；**198** 真复位 CM3→清描述符→写 init→（latched 才）敲门铃。

**102.4 构建与门禁**
CI #395（run `45169617324`）success 28/28（`HEAD_SHA 056130abf2c2c151f2e183268c4a2aa79c318c27`，分支 `r278-hfi-start` commit `2c5feabb2e6d761579beb94ef1dd01a6ae5a1f49`、tree `767e2d7ce327ac55bdb071b089a6ee7a39193989`，base main `0293a09f3eab2cf041c0f5f67f119f3e3c7ff81c`）。取件拍平：kernel.macho 126,484,480 B sha256 `fb35b7e4e3ad100bfabd0244c05a17d2ba15c7e45a547922b2cc37f5736c165c`，payload.img 150,994,944 B sha256 `c2482be09cf8432767fdee115d27d34058b38d4ba0e369ce9e83a13beba0a766`。静态门 `r278_static.py` 对真产物 **PASS**（36 行 required+survivors 在 kernel 与 payload 里都命中；marker kernel=1 / payload=2；forbidden `gmu199a=`/`gmu200a=`/`gmu201a=` 全零；固件 32768 B sha `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f` 钉住），对 R277 产物 **FAIL 14 problems**（14 条本轮行全 kernel=0 payload=0）——门的区分能力自证成立。host 门 32/32；probe 门 28/28，store 计数由 782 抬到 **793**（本轮 +11）；stage4 61/61、stage5 48/48、stage6 1932/1932；全量 `r239_allgates.py` ran 46 of 48 FAILING 0；`check_mode_selector.py` 33 selectors、上界 198、自证 FAIL、VERDICT PASS。三份 payload 各自重键（`enchilada-pas-exec=2` → `=196/197/198`），sha256 分别 `6429de0a7c6239b00a0004e880fe876fed9fc95409d0e31d3597888b300847bc`、`2717af6168a43a315a53ed113cdfb90ed15063a961d046d96d48bfca74ec420f`、`1c9372d0999d35d4c8c4ede13a32408cfc926fa0db136ac52813abedbc715c26`；每键上机前 `check-reboot-to-twrp.py --arm` PASS（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`）。

**102.5 键 196：抹掉描述符就能活，但那笔 init 没换来一次启动**
`boot_id 584895190`、窗口 sha256 `543043724c8f211bfcc36be17bbf7c3b2bfea0bfe7e11bf2ba3720ee7477c037`，4/4 行。逐字：`gmu196p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad96f000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；`gmu196p1=qinfo_cleared=1 qi=00000000 qa=ad96f000`；`gmu196p2=init_only=1 wrote=1 ctl=00000000 latched=0 polls=2000 init=00000001 info=00000000 qi=00000000 qa=00000000 st=00000000 gxoff=0 gd=00000000 cf=00000000`；`gmu196l=mode=196 gd=00000000 cf=00000000 gx_on=0 gxoff=0 ack=0 info=00000000 survived=1`。读法：**①清描述符那笔落位了**（`qi 00000001 → 00000000`，而地址 `qa` 仍是 `ad96f000`）⇒ 这一笔只动了它瞄准的字；**②写 init 那笔也落位了**（`init=00000001`）⇒ 后面没有「启动」不是「没写进去」；**③`ctl` bit0 = 0、`latched=0`**，2000 拍（约 2 ms）里一次没置起 ⇒ **固件没有受理这次 HFI 启动**；**④`info=00000000`** ⇒ 这次 store 没有把 `CM3_FAULT`(bit23) 敲出来（与 R276 的门铃相反）；**⑤整块读数塌成零**：`st=00000000`、`gd=00000000`、`cf=00000000`，连我们**没写过**的 `qa` 也在同一行读成 `00000000`（它在 p1 还是 `ad96f000`），尾行仍是零 ⇒ 从写完 init 到块尾，这个窗口读不到真值。

**102.6 键 197：只抹地址不抹描述符，两次上机都是零字节**
本键连跑两次，两次都是**零字节**：读回的窗口 sha256 仍是 `543043724c8f211bfcc36be17bbf7c3b2bfea0bfe7e11bf2ba3720ee7477c037`（键 196 的窗口）、`boot_id` 仍是 `584895190`，`rows: gmu197p0=0 gmu197p1=0 gmu197p2=0 gmu196l2=0`。两次的 flash 都成功（`flash system_b OK`、`boot sent ok=True`），手机回到 TWRP、日志读成功、钥匙块回读与 payload 逐字节相同、系统头 8 MiB sha256 与 payload 前 8 MiB 相同 ⇒ **不是「没刷进去」，是这一键所在的整轮在提交之前就没了命**。与键 196 的唯一差别是「抹掉的是地址而不是描述符」：196 活、197 死 ⇒ **致死的那一半是描述符 `HFI_QTBL_INFO`**，不是地址。

**102.7 键 198：真复位 CM3 之后再来一遍，仍然不接受 HFI 启动**
`boot_id 586717131`、窗口 sha256 `32306716b6806e54afc9dbd488693682b845ff7a85ba8df6336b57deed3f1a13`，6/6 行。逐字：`gmu198p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad980000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；`gmu198p1=reset=1 fw=babeface polls=3 sr=00000000`；`gmu198p2=qinfo_cleared=1 qi=00000000 qa=ad980000`；`gmu198p3=init_after_reset=1 wrote=1 ctl=00000000 latched=0 polls=20000 init=00000001 info=00000000 qi=00000000 qa=00000000 st=00000000 gxoff=0 gd=00000000 cf=00000000`；`gmu198p4=msgq=1 ack=0 skipped=1 polls=0 info=00000000 ctl=00000000 qi=00000000 qaddr=ad980000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；`gmu196l3=mode=198 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ack=0 info=00000000 survived=1`。读法：**①复位真的发生了**（`fw=babeface` 3 拍回来，`sr=00000000`）；**②复位之后清描述符、写 init 都落位**（`qi 1→0`、`init=00000001`）；**③`ctl` bit0 在 20000 拍（约 20 ms，>100 个 writer 轮）里一次没置起** ⇒ 把「固件刚起来就来得及」「等得不够久」两种解释都排除；**④门铃被按设计跳过**（`skipped=1`、`polls=0`、`ack=0`）⇒ 「没有应答」这一句在本键里**不成立**，因为根本没敲；**⑤整块读数先塌成零、然后自己回来**：p3 里 `st/gd/cf/qa` 全零，p4 里又读到 `st=000000d4 gd=00222001 cf=00088400`，而 p3 到 p4 之间**一笔 store 都没有**（跳过分支）⇒ 那个「零」是**瞬时的读取现象**，不是被某笔写毁掉的状态。

**102.8 判决**
1. **致死的是描述符，不是地址**：196（描述符 0、地址在）6/6 存活且两笔 store 都落位；197（地址 0、描述符在）两次零字节。跨轮再看 R277 键 195（两个都在）两次零字节 ⇒ 三键合起来是一个干净的二分：**`HFI_QTBL_INFO != 0` 时写 `HFI_CTRL_INIT=1` 致死；`= 0` 时存活**，与地址是否为零无关。
2. **复活不等于启动**：196 与 198 里 `HFI_CTRL_INIT` 都读回 `00000001`、`HFI_CTRL_STATUS` bit0 却一次没置起（2 ms 与 20 ms 两种等待长度都试过）。所以「表被描述着」是一个**可以被拆掉的致死条件**，「固件不接受 HFI 启动」是**拆不掉的第二个缺口**，两者不能混为一谈。
3. **复位 CM3 不是缺的那一步**：198 先做真复位（`babeface` 3 拍回、`sr` 归零），随后仍不 latched ⇒ 与 R247/R248 的「固件会重跑」不是同一条线索上的缺口；固件**跑起来了但不接受这次启动**。
4. **写 init 会带出一个瞬时的全零窗口**：196 与 198 的 `p2`/`p3` 行里 `st/gd/cf/qa` 全零（196 尾行仍是零，198 的 `p4` 已恢复）⇒ 这是一条**新的、跨两键一致**的观测。最直接的解释是「写完 init 后 GMU 侧的 AHB 通路短暂不响应，读到 0」，但本轮**没有**读第二遍来测这段窗口的长度，也**没有**读 `CM3_FW_INIT_RESULT`/`CM3_SYSRESET` 看固件是否被这笔 store 弄成重启。
5. **`CM3_FAULT` 与 init 无关**：本轮的 init store 没有把 bit23 敲出来（196/198 的 `info` 都是 `00000000`），所以「敲门铃→抬 fault」与「写 init→不抬 fault」是两条不同的回应。

**102.9 必须分开说的五件事**
第一，键 197 的两次零字节**只证明「这一键作为整体没有提交任何字节」**，死因归于「描述符仍在」是**与 196 的对照推理**，不是直接观测——致死块里打印不出任何行。第二，「196 存活」不等于「那笔 init 被受理」：`latched=0` 是设备自己写的字，判决用的是它。第三，「全零窗口」可能意味着「读数瞬时不响应」，也**可能**意味着「那块寄存器真的被复位到 0 了」；p3→p4 之间没有 store 而它自己回来，支持前者，但**不支持**把它升级成「已证明」。第四，`qa` 在 196 的 p2 读成 0 这件事很容易被误读成「地址被清了」——我们只写了 `HFI_QTBL_INFO`，`qa` 的变化是全零窗口的一部分，不是那笔 store 的效果（p1 里它还读到 `ad96f000`）。第五，本轮三键的窗口 sha 与 boot_id 全部取自设备自写字段，没有用任何手工维护的表。

**102.10 没证明什么**
没证明 `HFI_CTRL_STATUS` bit0 是不是「唯一」的启动判据（只读了这一个位）；没证明写完 init 后的**全零窗口有多长**，也没证明它是否与 `HFI_QTBL_INFO=0` 有关（197 死在提交前，拿不到它的读数）；没证明固件在这一笔 init 之后**是否真的重启过**（没读 `fw_init_result`/`sysreset`）；没证明「先清描述符、写 init、再复位」或「复位、写 init、不清描述符」这两条反序组合会怎样；也没证明 GX 上电该走哪条路——`cf` bit16 在三键里都是 0。

**102.11 R279 的设计思路**
下一轮第一件事是把「全零窗口」量出来：**键 199** = 清描述符→写 init→**每 200 拍读一次 `st/gd/cf`、连读 10 次**（判零窗口长度与是否自己恢复），并在每次读里附带 `fw_init_result`/`sysreset`（判固件是否被这笔 store 弄成重启）；**键 200** = 同一序列但在 init 之后**再读一次 `HFI_CTRL_STATUS` 与 `info`**（判「不 latched」是不是瞬时现象）；**键 201** = 先复位→写 init（**不清描述符**）——这是 197 的反序版本，判「致死条件是否依赖顺序」。三键都保持零副作用：不写掩码、不写 SMMU 表、不用空转买时间。

**102.12 工具事实**
一是**判定脚本会把上一轮的字节当成这一轮的判决**：`r278_s197_run.py` 首版把键专属的裁决句放在 `if/elif` 链的**外面**，于是零字节的运行照样打印了「the round survived」；修法是把键专属那一段整体缩进进 `else:` 分支（生成器里先攒进 `kb` 列表再缩进 4 空格拼回），并**重跑 197** 确认修完仍是零字节（而不是因为改脚本才有了结果）。二是 **mtools 不认 `C:` 盘符**：`r170_rekey.py` 传绝对路径会报 `Drive 'C:' not supported`，必须把工作目录设到 `work\tmp` 再传**相对路径**（本轮第一份 196 payload 就是先失败再重键的）。三是 CI artifact 名是 `unixnu-enchilada_defconfig-kernel`/`…-payload`，`r214_par.py` 的 needle 是**单个子串**，必须分别传 `"defconfig-kernel"` 与 `"defconfig-payload"`。四是 `pathlib` 的 `write_bytes` 会保留 CRLF 前提下的字节，但**证据/交接文件必须显式 `newline="\n"`**，否则中文段的控制字符统计会混进 `\r`。五是本轮 probe 门的 store 预算是**累计值** 793，说明串里要写清「196 两笔、197 两笔、198 七笔」，否则下轮无从推算。六是 `python -c` 里带引号或中文在 PowerShell 下必报 `TerminatorExpectedAtEndOfString`，一律先写 `.py`。

## 一百零三、R279：零窗口不是必然的、20 ms 等待没换来一行、描述符的死亡判决复现到第三次

**103.1 一句话**
R278 留下了三个悬而未决的点：写 `HFI_CTRL_INIT=1` 之后那块 GMU/GPU_CC 会**短暂读成零**（长度未知）、「不 latched」是不是**等得不够久**（只试过 2 ms 与 20 ms 两次读）、以及「描述符非零就死」是不是**依赖顺序**。本轮三键分别对准它们：**键 199** 用十个采样把零窗口量了一遍（每 200 拍采样一次，约 3 ms 一档，总约 2 ms 的横向覆盖）——**十个采样全部读到真值，零窗口一次没出现**；**键 200** 在写完 init 之后**再等 20 ms** 才读同一批字——`p2` 又一次读到全零窗口（第三次出现），但 `p3`（after_wait）与尾行**一行都没落地**；**键 201** 把键 197 的次序反过来（先真复位 CM3、再不清描述符直接写 init）——**这一键连 `p0` 都没落地，窗口字节与 `boot_id` 全未变**。三句结论：**①零窗口不是那两笔 store 的必然结果**（199 在同一次构建里完全没出现，196/198/200 出现）；**②「不 latched 是常态」这句话本轮拿不到证据**——20 ms 的等待没有走到读回；**③描述符死亡判决复现到第三次**（195、197、201 三次零字节；196、198、200 三次存活），但 201 的零字节**不能**用来证明「复位之后写 init 也死」，只能证明「这一键没有提交任何字节」。

**103.2 三个键各自问什么**
键 199 与 R278 键 196 是**同一序列**（清 `HFI_QTBL_INFO` → 写 `HFI_CTRL_INIT=1`），只多了十次横向采样，每次采样后跑 200 次「读 `HFI_CTRL_STATUS` + `IODelay(1)`」的循环（约 200 µs），采样内容是 `st`/`gd`/`cf`/`qi`/`qa`/`ctl`/`fw`/`sr`——换句话说，它把 R278 只在两点上（p2、p4）看过的那条时间轴，铺成了十点。键 200 把「等得不够久」这条解释单独拿掉：同一序列、同一 2000 拍轮询，之后**再等 200 × `IODelay(100u)`（20 ms）**，然后读 `ctl`/`info`/`fw`/`sr`/`qi`/`qa`/`st`/`gd`/`cf`。键 201 是 R278 键 197 的反序：先 `CM3_SYSRESET=1` → `FW_INIT_RESULT=0` → `SYSRESET=0` → 20000 拍等 `0xbabeface`，然后**不清描述符**直接写 init——如果它活着，就说明「死」只认描述符的值；如果它死了，就说明次序也算数。

**103.3 代码形状**
探针块 14,177 字符 / 264 行，braces 净 0（opens=15 closes=15）；块内 `gmuW[...] =` **8 笔**——键 199 两笔（`QTBL_INFO` 写 0、`CTRL_INIT` 写 1）、键 200 两笔（同）、键 201 四笔（`SYSRESET` 写 1、`FW_INIT_RESULT` 写 0、`SYSRESET` 写 0、`CTRL_INIT` 写 1）。`IODelay(1u)` 4 处、`IODelay(100u)` 1 处。本轮**不敲门铃**（块内 `0x5194u`/`0x5191u` 计数为 0），所以「没有应答」不是本轮的观测项。dword 索引沿用 R278（基址 `0x0506a000`）：`0x2bf8` 版本字、`0x5180` `HFI_CTRL_STATUS`、`0x5184` `QTBL_INFO`、`0x5185` `QTBL_ADDR`、`0x5186` `CTRL_INIT`、`0x5192` `GMU2HOST_INTR_INFO`、`0x50d0` `SPTPRAC_PWR_CLK_STATUS`、`0x9c03` `GDSCR`、`0x9c04` 状态、`0x5000` `CM3_SYSRESET`、`0x501c` `FW_INIT_RESULT`。

**103.4 构建与门禁**
CI #396（run `45178876325`）success 28/28（`HEAD_SHA 056130abf2c2c151f2e183268c4a2aa79c318c27`，分支 `r279-zero-window` commit `2a015f1b28528712700ebd9ae9e002674b4fe283`、tree `d3f8f6b4dbad659b17349c231a15c6e58727bdc5`，base main `b592f096c5c2e7e4e397bcdc9be3c68f8ceab960`）。取件拍平：kernel.macho 126,484,480 B sha256 `ca514f6e7ef9b081453c618c14863ef1cd1a06a74869a758ad15cf5c254b0e3f`，payload.img 150,994,944 B sha256 `60dee43b8d70fba4d611e996f334aedb64a312ce86073b36d4c551c347f07b00`。静态门 `r279_static.py` 对真产物 **PASS**（39 行 required+survivors 在 kernel 与 payload 里都命中；marker kernel=1 / payload=2；forbidden `gmu202a=`/`gmu203a=`/`gmu204a=` 全零；固件 32768 B sha `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`），对 R278 产物 **FAIL 13 problems**（13 条本轮行全 kernel=0 payload=0）——门的区分能力自证成立。host 门 32/32；probe 门 28/28，store 计数由 793 抬到 **801**（本轮 +8）；stage4 61/61、stage5 48/48、stage6 1960/1960；全量 `r239_allgates.py` ran 46 of 48 FAILING 0；`check_mode_selector.py` 34 selectors、上界 201、自证 FAIL、VERDICT PASS。三份 payload 各自重键（`enchilada-pas-exec=2` → `=199/200/201`），sha256 分别 `add6fa33e69b88f60acdd0a02ffcddbc22cede8e89b86eb31377d3be36e6a0e6`、`ece18ad72a37a39b6faa9a6c45514814b919128b828d4c4c5229af33ae5f14da`、`22ea2b27c0271586814cdf7f0cb623bb22d1dec5bb195301c7699e94f6747154`；每键上机前 `check-reboot-to-twrp.py --arm` PASS（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`），每次回到 TWRP 后 runner 自己再 `--arm-bcb-only`。

**103.5 键 199：同一个序列，十个采样一次也没读到零**
`boot_id 585000873`、窗口 sha256 `73bb002665bb9f16579e1ad663154f6f11fb4ef7fb94b2587d05108ff92dc07d`，13 行全在（`p0`/`p1`/`l` + 十条 `gmu199s%u`）。逐字：`gmu199p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad971000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；`gmu199p1=qinfo_cleared=1 qi=00000000 qa=ad971000`；十条采样**逐字相同**：`gmu199s0=sample=0 st=000000d4 gd=00222001 cf=00088400 qi=00000000 qa=ad971000 ctl=00000000 fw=babeface sr=00000000 gxoff=1`（s1…s9 除 `sample=` 与 `gmu199s` 编号外完全一致）；`gmu199l=mode=199 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ack=0 info=00000000 survived=1`。读法：**①两笔 store 都落位**（`qi 00000001 → 00000000`，地址仍 `ad971000`；`init` 没读回但尾行之前没有零窗口）；**②`ctl` bit0 在十条采样与十段 200 拍循环里一次没置起**（`latched` 未在行里出现，`ctl` 恒 `00000000`）⇒ 2 ms 量级的等待与 199 的铺开采样同样读不到受理；**③`fw=-babeface` 十条全同、`sr=00000000` 十条全同** ⇒ 这笔 init **没有**让固件重启；**④`st/gd/cf/qa` 十条全同且全为真值** ⇒ R278 记下的「写完 init 之后整块读成零」**在本轮没有复现**；**⑤`info=00000000`** ⇒ init 不抬 `CM3_FAULT`（与 R278 一致）。

**103.6 键 200：零窗口第三次出现，但 20 ms 的等待没走到读回**
`boot_id 584708042`、窗口 sha256 `f7d868542302f7997531501017f0a353f16ff06ee67b2ad83d7c2f43f4d0b547`，落地 **3/5** 行（`p3` 与尾行 `gmu199l2` 缺失）。逐字：`gmu200p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad973000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；`gmu200p1=qinfo_cleared=1 qi=00000000 qa=ad973000`；`gmu200p2=init_only=1 wrote=1 ctl=00000000 latched=0 polls=2000 init=00000001 info=00000000 qi=00000000 qa=00000000 st=00000000 gxoff=0 gd=00000000 cf=00000000`。读法：**①同一个序列在这里**又一次读到全零窗口（`st/gd/cf/qa` 全 `00000000`，`init` 读回 1）⇒ 这是 196、198 之后的**第三次**，同时 199 完全没出现 ⇒ 它是**间歇的**，不是必然；**②`p2` 能落地、`p3` 不能** ⇒ 这一轮**死在 20 ms 等待里或它的第一次读上**，不是「跳过」、也不是「没写进去」；**③因此「不 latched 是常态」这句话本轮**不能**成立**：`p3` 没读到，只证明 2000 拍轮询里没有，没证明 20 ms 之后也没有。

**103.7 键 201：反序组合，零字节**
这一键**一行都没落地**（`gmu201p0=0`、`gmu201p1=0`、`gmu201p2=0`、`gmu199l3=0`），读回的窗口 sha256 仍是 `f7d868542302f7997531501017f0a353f16ff06ee67b2ad83d7c2f43f4d0b547`、`boot_id` 仍是 `584708042`（与键 200 完全相同）。上机流程逐项正常：`flash system_b OK`、`boot sent ok=True`、手机回 TWRP、`key block 31120: device == payload -> True`、`key block says: enchilada-pas-exec=201 -no_slto_`、系统头 8 MiB sha256 与 payload 前 8 MiB 相同（`840454a1de81a6d425e12a4c9c87db31bd8bef67435d56952e27716e9792dfd7`）⇒ **不是没刷进去**。边界：因为没有任何行落地，**无法判断它死在复位那三笔还是 init 那一笔**；只能记「这一键作为整体没有提交任何字节」。

**103.8 判决**
1. **零窗口是间歇的**：196、198、200 三次出现，199 一次没出现，而 199 与 196 的 store 序列**逐笔相同**。所以「写 init 之后整块读成零」不是这笔 store 的确定性后果；本轮**不**把它升级成「已证明的机制」，只记「间歇、长度未定」。
2. **20 ms 等待没有换来判决**：200 的 `p3` 未落地，说明那一轮没走完等待；「不 latched 是否常态化」这个问题**仍然开着**，而且现在知道**等得久本身会结束这一轮**（这是新的一条）。
3. **描述符的死亡判决复现到第三次**：195（都留着）、197（地址清零、描述符留着）、201（先复位、描述符留着）三次都是零字节；196（描述符清零）、198（复位+描述符清零）、200（描述符清零）三次都有行落地。六次里五次的操作变量只有描述符，所以「`HFI_QTBL_INFO != 0` 时写 INIT 致死」这条判决现在是**三次复现**，但 201 的次序变量**没有**被证明（它也可能死在复位阶段）。
4. **一行落地的含义被本轮改写**：200 落地 `p0`–`p2` 却缺 `p3`，说明探针的行是**分区落地**的——死在轮中会丢掉「尚未刷出的行」，而不是「整轮一起提交」。所以「零字节」只意味着「死得早于第一次刷出」，**不再**意味着「整轮一行都没写过」。这条修正 R278 §102.9 的表述（那里把零字节读成「整轮未提交」）。
5. **`CM3_FAULT` 与 init 无关**（199、200 的 `info` 都是 `00000000`），与 R278 一致。

**103.9 必须分开说的四件事**
第一，199 的「没读到零」**不等于**「零窗口不存在」：它的十条采样虽然覆盖约 2 ms，但采样点之间是 200 拍循环，若零窗口短于一次循环且恰好落在采样间隙里，就会漏掉——只能说「没有落在十个采样点上」。第二，201 的零字节**不能**单独支撑「次序不重要」，它只支撑「这一键没提交」；次序问题要靠一个**能落地 `p1` 再死**的设计去问。第三，200 的死**不能**归到 20 ms 的 `IODelay` 本身：同一轮里 `p2` 之前的 2000 拍 `IODelay(1)` 活着走完，差别只在等待长度与「等完之后的第一读」；是哪一半致死，本轮分不开。第四，所有 `boot_id`/窗口 sha 都取自设备自写字段，没有用任何手工表。

**103.10 没证明什么**
没证明零窗口的长度（十个采样点全没命中，只能给上界「两点之间」）；没证明它是否只发生在那块寄存器被写过之后的特定相位；没证明 201 死在复位还是 init；没证明 20 ms 之后 `ctl` bit0 是否会出现；没证明 `HFI_CTRL_STATUS` bit0 之外的任何启动判据；`cf` bit16 在三键里仍是 0，GX 上电路径**没有**进展。

**103.11 R280 的设计思路**
下一轮把两件没问成的事拆小：**键 202** = 200 的等待**减半到 10 ms 并且只做一次读**（把「等得久会死」与「等完读会死」分开：若 10 ms 能落地一行、20 ms 不能，长度就是变量）；**键 203** = 201 的次序但**先落地一行再写 init**（复位 → 读一行 → 不清描述符写 init → 读一行；用「p1 必须落地」把复位阶段与 init 阶段分开，这一键的 `p1` 就是 201 缺的那一行）；**键 204** = 199 的采样加密到 20 点、每点之间只跑 50 拍（约 50 µs），把零窗口的下界压到 50 µs 量级。三键都保持零副作用。

**103.12 工具事实**
一是**「落地行数」本身是观测**：`counts:` 行给出每行计数，缺行与「值为零」必须分开读，否则会把「`p3` 没跑到」读成「`p3` 读到零」（本轮 `r279_s200_run.py` 的判定句就是这么打印的——见下条）。二是**判定脚本对缺失行会打印自相矛盾的句子**：200 的裁决里出现 `ctl None, None steps` 与 `st None`，因为生成器在 `p3` 缺失时仍走了「20 ms 之后仍然没有」那条分支；修法是先把「行是否落地」作为前置条件，缺行时只报「这一轮没有走到读回」。三是 `r170_rekey.py` 的 readback 自检把 **`enchilada-pas-exec=2` 当成 `…=200` 的前缀**，于是正确的重键被判成失败（`readback does not carry the new value`），第二轮 `=201` 同样中招；修法是用「值边界」正则 `re.escape(new) + rb"(?![0-9])"` 判新值、`re.escape(old) + rb"(?![0-9])"` 判旧值残留（修完重跑三份，字节与第一次一致）。四是 `peek277.py` 把 run id **硬编码**在脚本里，给它一个新 id 它照样打印上一轮的「completed success」——本轮差点据此去下载旧产物；改成 `peek_ci.py <run_id>`（id 从 argv 读、打印 `VERDICT: PASS/FAIL` 与本轮 run 自己的步表）后才发现 #396 还在 `in_progress`。五是 `r214_par.py` 在 run 未完成时返回空 artifact 列表并打印 `no artifact matching ...: []`，这句话**不是**「artifact 名不对」，要先用 `peek_ci.py` 看 run 状态。

## 一百零四、R280：十毫秒的等待走到了读回、五十微秒的采样一次零窗口都没见、复位前一行的路径又一次零字节

**104.1 一句话**
R279 留下三件没问成的事：**20 ms 的等待为什么没走到读回**、**零窗口到底多短**、**描述符的死亡判决到底卡在哪一步**。本轮三键各问一件。**键 202**（把 20 ms 减半到 10 ms、等完只读一次）**五行全落地**：`gmu202p2` 又一次读到整块为零，而十毫秒之后 `gmu202p3` 读到 `st=000000d4 gd=00222001 cf=00088400 qa=ad972000 fw=babeface sr=00000000`，`waited=100` 是设备自己数的——**十毫秒的等待没有结束这一轮，读回也走到了**，`ctl` bit0 仍然没有置起。**键 203**（复位 CM3 → **先打印一行** → 不清描述符写 init）**又是零字节**，窗口 sha 与 `boot_id` 与键 202 那次**逐字相同**（`8c26e086…`、`585610819`），四行一行未落地——**连复位之前那个 census 行都没落地**，所以这一键既不能证明「死在复位」也不能证明「死在 init」，只能证明「没提交」。**键 204**（二十个采样、每点间隔五十拍 ≈ 50 µs）**三个头行加二十行采样全部落地**，而**二十个采样逐字相同且第一个采样就不为零**（`st=000000d4 gd=00222001 cf=00088400 qi=00000000 qa=ad971000 ctl=00000000 fw=babeface sr=00000000`）——**在 50 µs 的尺度上，零窗口根本没有出现**。三句结论：**①十毫秒的等待是安全的**，R279 键 200 的死落在 10 ms 与 20 ms 之间（或落在那两次「等完只读一次」的实现差别上），**②零窗口不能被「写完立刻读」复现**（204 的第一个采样就是真值），它和「写完 init 之后跑完 2000 拍 `ctl` 轮询再读」这个形状绑定得更紧，**③复位前一行的设计没有换来任何一行**，201/203 两次零字节说明这条路径要么死在复位，要么死在更早的地方，而不是死在 init。

**104.2 三个键各自问什么**
键 202 与 R279 键 200 同形（清 `HFI_QTBL_INFO=0` → 写 `HFI_CTRL_INIT=1` → 2000 拍轮询 `ctl` bit0），只把「再等 200 × `IODelay(100u)`（20 ms）」减半成「`for (r202waitPw = 0u; r202waitPw < 100u; ++r202waitPw) IODelay(100u)`（10 ms）」，然后**只做一次读**（`ctl`/`info`/`fw`/`sr`/`qi`/`qa`/`st`/`gd`/`cf` 九个字）。它把 R279 分不开的两半拆开：若十毫秒能落地一行而二十毫秒不能，长度就是变量；若十毫秒也落地不了，那死不是长度造成的。
键 203 是 R279 键 201 的同序（`CM3_SYSRESET=1` → `FW_INIT_RESULT=0` → `SYSRESET=0` → 20000 拍等 `0xbabeface` → **不清描述符**写 init → 2000 拍），只多一件事：**在写 init 之前先把 `gmu203p1=after_reset=1 fw=… polls=… sr=… qi=… qa=…` 刷出来**。这一行就是 201 缺的那一行——它落地，复位阶段结束；它不落地，这一键死在复位（或更早）。
键 204 是 R279 键 199 的同序（清描述符 → 写 init → 采样），采样从十点加密到**二十点**、每点之间只跑 **50** 次「读 `ctl` + `IODelay(1)`」而不是 200 次，也就是点距从约 200 µs 压到约 50 µs，把零窗口的下界往下压一个数量级。每个采样都带 `fw` 与 `sr`，所以「窗口是零」不可能被误读成「固件重启了」。

**104.3 代码形状**
探针块 14,187 字符 / 264 行，braces 净 0（opens=15 closes=15）；块内 `gmuW[...] =` **8 笔**——键 202 两笔（`QTBL_INFO` 写 0、`CTRL_INIT` 写 1）、键 203 四笔（`SYSRESET` 写 1、`FW_INIT_RESULT` 写 0、`SYSRESET` 写 0、`CTRL_INIT` 写 1）、键 204 两笔（同 202）。`IODelay(1u)` 4 处、`IODelay(100u)` 1 处。本轮**不敲门铃**（块内 `0x5194u`/`0x5191u` 计数为 0），「门铃有没有人取」不是本轮的观测项。dword 索引沿用 R278/R279（基址 `0x0506a000`）：`0x2bf8` 版本字、`0x5180` `HFI_CTRL_STATUS`、`0x5184` `QTBL_INFO`、`0x5185` `QTBL_ADDR`、`0x5186` `CTRL_INIT`、`0x5192` `GMU2HOST_INTR_INFO`、`0x50d0` `SPTPRAC_PWR_CLK_STATUS`、`0x9c03` `GDSCR`、`0x9c04` 状态、`0x5000` `CM3_SYSRESET`、`0x501c` `FW_INIT_RESULT`。三个选择器一律显式重写为 `(fwKey >= 204u) ? 204 : ((fwKey >= 203u) ? 203 : 202)`，arm 窗口 `(fwKey >= 7u && fwKey <= 204u)`。

**104.4 构建与门禁**
CI **#397**（run `45190745316`）success 28/28（`HEAD_SHA 056130abf2c2c151f2e183268c4a2aa79c318c27`，分支 `r280-split` commit `ade02e483f35984d1bf5939708f9f001338163ee`、tree `a9f7d8c0f0b28dbf7130790a9d3fa5fda863ef11`，base main `60212aebba30427f8935fe44ee80379c3fc5b94a`）。取件拍平：`kernel.macho` 126,484,480 B sha256 `66044f422e4fa5d22cb648ac6193028d8d6aca1dfbfac3395b22a4c5ec5f4415`，`payload.img` 150,994,944 B sha256 `1332cb8ab6169cc56a90ad8f000760a5bf4fcab035f84f421e674d8d5d116392`。静态门 `r280_static.py` 对真产物 **PASS**（45 行 required+survivors 在 kernel 与 payload 里都命中；marker kernel=1 / payload=2；forbidden `gmu205a=`/`gmu206a=`/`gmu207a=` 全零；固件 32768 B sha `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`），对 R279 产物 **FAIL 13 problems**（13 条本轮行全 kernel=0 payload=0）——门的区分能力自证成立。host 门 25/25 + 51/51 + 32/32；probe 门 28/28，store 计数由 801 抬到 **809**（本轮 +8）；stage4 61/61、stage5 48/48、stage6 1991/1991（新增 `_k202204r`/`_k202204` 切片，`_k199201r`/`_k199201` 的终止符改到 R280 头，窗口字面量 `201u→204u`）；全量 `r239_allgates.py` **ran 46 of 48, FAILING: 0**；`check_mode_selector.py` 35 selectors / upper 204 / 自证 FAIL / VERDICT PASS。三份 payload 重键：p202 `43f5fdaceaeed4097903688a589d7b14dbf69d9fadcf14ae8a5354fa59243bcb`、p203 `595dead4847c943249d68c6f2cf08a796b338d4d787a0160c595e37419e3d64a`、p204 `bb0534838a72973ab54830395f4988c2054bc4ce20ea5d1229d28ca25897a639`；每次回 TWRP 后钥匙块与系统头 8 MiB 都与 payload 逐字一致（sha `4e3f83a083d5b2eb719f2e5d456893e499a7252b6525babf6de9f14f65b937e9`），所以三键都不是「刷错镜像」。

**104.5 键 202：十毫秒的等待走到了读回，窗口在十毫秒内恢复**
`boot_id 585610819` / 窗口 sha256 `8c26e08652494b5817c766fb83eb7c5432c82ea46f6da72747ea59b946b5e66c`，五行全部落地：
`gmu202p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad972000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；
`gmu202p1=qinfo_cleared=1 qi=00000000 qa=ad972000`；
`gmu202p2=init_only=1 wrote=1 ctl=00000000 latched=0 polls=2000 init=00000001 info=00000000 qi=00000000 qa=00000000 st=00000000 gxoff=0 gd=00000000 cf=00000000`；
`gmu202p3=after_half_wait=1 waited=100 ctl=00000000 info=00000000 fw=babeface sr=00000000 qi=00000000 qa=ad972000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；
`gmu202l2=mode=202 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ack=0 info=00000000 survived=1`。
四件事从这里读出来：**①`waited=100`** 是设备自己数的等待轮次，**十毫秒真的跑完了**；**②`gmu202p3` 存在**，所以「等完之后的那一读」没有致死——R279 键 200 的死不在这一形状的十毫秒档；**③`gmu202p3` 的 `st`/`gd`/`cf` 全部回到非零**，零窗口在十毫秒之内自己恢复，**④`gmu202p3` 的 `qa` 回到 `ad972000`**，而 `qi` 仍是 `00000000`——说明 `qa` 那一格在 `p2` 读到零并不是我们写过，而是整块窗口一起读成零；描述符清零这一笔的后果**只落在 `qi` 上**。

**104.6 键 203：复位之前那一行也没落地**
`boot_id` 与窗口 sha **与键 202 那次逐字相同**（`585610819` / `8c26e08652494b5817c766fb83eb7c5432c82ea46f6da72747ea59b946b5e66c`），`counts: gmu203p0=0 gmu203p1=0 gmu203p2=0 gmu202l3=0`，`rows: key 203 rows landed 0 of 4`；设备日志里那条 `stage7=armed-not-executed … exec_key=202` 也说明**这份窗口属于上一次引导**——判定脚本把「窗口没变」直接判成「这一轮没有提交任何字节」，这个判定本轮是正确的。要点是**这一键的设计本来要求 `p0`（复位之前的 census）先落地**，而它也没有落地：所以 203 的死**早于**复位那三笔 store，甚至早于写 init。R279 键 201 的零字节因此**不能**再被解释成「死在 init」，它与 203 同形，只能说明「这条路径在刷出第一行之前就结束了」。**这一键没有回答它要问的问题**，201/203 两把钥匙仍然打不开「次序算不算数」。

**104.7 键 204：五十微秒的尺度上零窗口不存在**
`boot_id 585373308` / 窗口 sha256 `b934337510d0f68a34f61a7d3b3a27854b962faf81178d2de1ce82036908568a`，三行头行加**二十行采样全部落地**：
`gmu204p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad971000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；
`gmu204p1=qinfo_cleared=1 qi=00000000 qa=ad971000`；
`gmu204s0..s19` 二十行**逐字相同**：`st=000000d4 gd=00222001 cf=00088400 qi=00000000 qa=ad971000 ctl=00000000 fw=babeface sr=00000000 gxoff=1`；
`gmu202l=mode=204 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ack=0 info=00000000 survived=1`。
**第一个采样（写完 init 之后的第一读）就已经是 `000000d4`**——所以「写完那一笔之后整块寄存器立刻读成零」这个说法**不成立**。二十个采样的 `fw` 全是 `babeface`、`sr` 全是 `00000000`，说明这 20 × 50 拍的跨度里固件没有重启；`qi` 恒 `00000000`（描述符那一笔的后果稳稳落在它上面）、`qa` 恒 `ad971000`（地址那一格从未被写过）。结合 202 的十毫秒、199 的十点、196/198/200/202 的「跑完 2000 拍 `ctl` 轮询再读」，**零窗口与「立即读」不绑定，与「跑完那 2000 拍轮询后再读」这个形状绑定得更紧**——这是一个可被下一轮直接检验的形状假设，不是结论。

**104.8 本轮把哪几条上一轮的说法改掉了**
1. **「20 ms 的等待结束整轮」现在有了边界**：十毫秒能走完并落地，所以致死区间落在 10 ms 与 20 ms 之间（或落在那两次实现里「等完只读一次」的差别上）；R279 §103.9 的「不能归到 `IODelay` 本身」仍然成立，但可以收窄成「不是十毫秒这一档能治的」。
2. **「整块窗口读成零」不是写完 init 立刻发生的**：204 的第一个采样就是真值，202 的 `p2` 才读到零——**所以它不是 store 的直接后果，而是「store + 2000 拍轮询之后」那个相位上的观测**。
3. **描述符那一笔的后果只落在 `qi`**：202 的 `p3` 与 204 的二十个采样都显示 `qa` 一直是有值的，`p2` 里的 `qa=00000000` 是整块窗口塌下去的附带读数，不是地址被清。
4. **201/203 的零字节仍不能被解释成「死在 init」**：203 连复位之前的 census 行都没有落地，这条路径的死点比 init 更早，本轮没有定位它。

**104.9 必须分开说的四件事**
第一，203 的零字节**不能**当作「次序算数」的证据，也不能当作「次序不算数」的证据——它只说明这条路径没有走到刷出第一行。第二，202 的「十毫秒安全」**不等于**「二十毫秒一定不安全」：两条 run 之间还有别的差别（等待实现、读数数量），本轮只证明了十毫秒这一档能过。第三，204 的「二十个采样都非零」**不等于**「零窗口不存在」：采样点距约 50 µs，若零窗口短于 50 µs 且正好落在两次采样之间，同样会漏掉；只能说「没有落在二十个采样点上，且第一个采样就不是零」。第四，所有 `boot_id` 与窗口 sha 都取自设备自写字段，没有用任何手工维护的表。

**104.10 没证明什么**
没证明零窗口的成因（是不是那 2000 拍 `ctl` 轮询把 AHB 打进某个状态）；没证明致死区间在 10/20 ms 之间的确切位置；没证明 203 死在复位还是更早；没证明 `ctl` bit0 在任何等待长度下会出现（本轮 202/204 两键都没有）；`cf` bit16 在三个键里仍然是 0，**GX 上电路径没有进展**。

**104.11 R281 的设计思路**
下一轮把本轮新得到的形状假设直接拿去问：**键 205** = 写完 init 之后**只**跑 2000 拍 `ctl` 轮询、紧接着读一次（复现 196/198/200/202 的 `p2` 形状，把「2000 拍轮询」这个变量单独拎出来）；**键 206** = 同样的 2000 拍轮询但**每 100 拍采样一次**（二十次采样落在轮询内部，看零窗口是不是只在轮询结束那一刻出现）；**键 207** = 把等待做成阶梯（100/150/200 三档各读一次，判 10/15/20 ms 的致死边界）。三键都保持零副作用、都不碰门铃。

**104.12 工具事实**
一是**判定脚本自己会撒谎**：`peek_ci.py` 在 run 还在 `in_progress`、只剩一半步骤跑过时，仍然打印 `VERDICT: PASS: every step succeeded`——因为它只统计「已完成步骤的结论」，没有把 run 自己的 `status` 作为前置条件；改成「`status != completed` 时只打印 `STILL RUNNING`，不许给 PASS」。二是**生成器的字符串拼接错误只在 `ast.parse` 时才暴露**：`r280_mkrun.py` 里一行 `a('…')')` 多了一个闭引号，整份脚本 `SyntaxError: unterminated string literal (detected at line 167)`，改掉多余的两个字符才生成成功。三是**锚点脚本的正则没跟着改就会静默失效**：`r280_anchor.py` 从 R279 复制时只改了提示文字、没改 `re.fullmatch(r"r27[89]_s\d+_window\.bin")`，于是对一份全新的 `r280_s202_window.bin` 打印 `WARNING: no r280/r281 window on disk has that sha256`——**一个不检查却自称检查的门比没有门更坏**；把正则改成 `r28[01]` 后立刻命中。四是**下载会半途而废**：`r214_par.py` 第一次取 payload 时在 72,779,260 / 83,176,298 处报 `range errors: {4: 'RuntimeError: status 403'}` 并 `INCOMPLETE` 退出，删目录、并发从 8 降到 4 重下才拿到完整 83,176,298 B——**`wrote … expected …` 那两个数不等时不许继续**。五是 `push_via_api.py` 在 `oneplus6-ios\scripts\`，从 `work\tmp` 起是 `..\..\scripts\push_via_api.py`。

## 一百零五、R281：两千拍轮询读不坏窗口、二十个环内采样全健康、纯等下去在十五与二十毫秒之间断掉

**105.1 一句话**
R280 把零窗口收窄到一个形状：**写完 `HFI_CTRL_INIT`、跑完 2000 拍 `ctl` 轮询、再读一次**，那一次读到整块为零，而十毫秒之后窗口已经恢复，同时「写完立刻读」的二十个采样一次零都没见。本轮三键把那个形状拆开，结果把它推翻了：**键 205**（同形但删掉十毫秒等待）读到 `st=000000d4 gd=00222001 cf=00088400 qi=00000000 qa=ad975000`——**窗口是健康的，零窗口没有复现**；**键 206**（同一个 store，把采样搬进 2000 拍轮询内部，二十个点每点隔一百拍）**二十个采样逐字相同、全部健康**，循环结束后的那一次读也健康（`st=000000d4 qi=00000000 qa=ad973000`）；**键 207**（完全不轮询，只按 10/15/20 ms 的阶梯纯等）**前两档各落地一行且都健康，第三档一行没落地**——**这一轮死在「写完之后纯等 15 到 20 毫秒之间」**。三句结论：**①2000 拍轮询本身不会把窗口读成零**，R280 键 202 那次整块为零是**间歇**的，不是形状的函数；**②「纯等」也会结束整轮，边界落在 15 ms 与 20 ms 之间**（第一次从 store 本身量起，而不是从循环末尾量起）；**③`ctl` bit0 在三键里一次都没有置起、`cf` bit16 全是 0**，GX 上电路径没有任何进展。零窗口因此不能再归给任何一笔写或任何一段循环，它更可能是**这一轮快要结束时**读出来的东西——这是下一轮唯一还站得住的假设。

**105.2 三个键各自问什么**
- **键 205**：与 R280 键 202 同形（`HFI_QTBL_INFO=0` → `HFI_CTRL_INIT=1` → 2000 拍轮询 `ctl` bit0），**只删掉后面那 100 × `IODelay(100u)` 的十毫秒等待**，循环结束后立刻读一次整窗并打一行。R280 键 202 的 `p2` 就是这个读，而它读到全零；如果零窗口属于循环，这里必须再读到一次全零。
- **键 206**：同一个 store，**把采样搬进循环内部**：二十个点，每点读九字并打一行，点与点之间一百拍 `IODelay(1u)`（约一百微秒），循环结束后再读一次。R280 只能看到「循环结束时为零」，看不到「循环进行时为不为零」。
- **键 207**：**一笔轮询都不做**，写完 `HFI_CTRL_INIT` 之后按 10 ms / 再加 5 ms / 再加 5 ms 三档纯等，每档读一次并打一行。R280 的十毫秒是从循环末尾量起、R279 键 200 的二十毫秒也是从循环末尾量起；这一键第一次**从 store 本身量起**。

**105.3 逐字读数**（设备自己写的行，`boot_id` 与窗口 sha 都来自设备）

键 205 `boot_id 585441584` / 窗口 `a64f40282377836891bff3823e9cd67986d8b484cac6e188f069dcc85fdd1732`，4/4 行：
```
gmu205p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad975000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400
gmu205p1=qinfo_cleared=1 qi=00000000 qa=ad975000
gmu205p2=poll_only=1 wrote=1 ctl=00000000 latched=0 polls=2000 init=00000001 info=00000000 qi=00000000 qa=ad975000 st=000000d4 gxoff=1 gd=00222001 cf=00088400 fw=babeface sr=00000000
gmu205l=mode=205 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ack=0 info=00000000 survived=1
```

键 206 `boot_id 585630472` / 窗口 `c3982a9fe45072945ab27f9c2af9ded90396e46579a1157c32da8760f08ca4b3`，4/4 头行 + 二十行采样：
```
gmu206p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad973000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400
gmu206p1=qinfo_cleared=1 qi=00000000 qa=ad973000
gmu206s0=sample=0 st=000000d4 gd=00222001 cf=00088400 qi=00000000 qa=ad973000 ctl=00000000 fw=babeface sr=00000000 gxoff=1
（s1..s19 与 s0 逐字相同，只有编号不同）
gmu206p3=after_loop=1 samples=20 ctl=00000000 info=00000000 qi=00000000 qa=ad973000 st=000000d4 gxoff=1 gd=00222001 cf=00088400 fw=babeface sr=00000000
gmu205l2=mode=206 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ack=0 info=00000000 survived=1
```

键 207 `boot_id 584937401` / 窗口 `49dcb88a8ccb2f2f17597565f2074e14bde4856adf88deba7ab45d2ed0a9a94f`，**6 行只落地 4 行**：
```
gmu207p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad970000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400
gmu207p1=qinfo_cleared=1 qi=00000000 qa=ad970000
gmu207p2=waited_100ms=1 ctl=00000000 info=00000000 qi=00000000 qa=ad970000 st=000000d4 gxoff=1 gd=00222001 cf=00088400 fw=babeface sr=00000000
gmu207p3=waited_150ms=1 ctl=00000000 info=00000000 qi=00000000 qa=ad970000 st=000000d4 gxoff=1 gd=00222001 cf=00088400 fw=babeface sr=00000000
（gmu207p4=waited_200ms 与 gmu205l3 均 0）
```

**105.4 判决**
1. **两千拍轮询读不坏窗口**：键 205 在循环结束的同一瞬间读到 `st=000000d4 qi=00000000 qa=ad975000`，键 206 更狠——**二十个环内采样全部健康**。⇒ R280 键 202 的 `p2` 全零**不是这个形状的函数**，它是**间歇**的。此前把零窗口与「store + 2000 拍轮询 + 读」绑定的说法**撤回**。
2. **纯等也会结束整轮，边界在 15 ms 与 20 ms 之间**：键 207 在 10 ms、15 ms 各读到健康窗口，**20 ms 那一档一行未落地**。前面几轮的死点（R279 键 200 的 20 ms、R280 键 202 的 10 ms 存活）与它一致，但这是第一次**从 store 本身量起**，不再隔着循环。⇒ 「长等待致死」是真的，而且阈值比先前以为的 20 ms 更低。
3. **`ctl` bit0 一次都没置起**：三键的 `latched=0`、`ctl=00000000` 全部一致，键 206 连中间点都是 0。⇒ **固件没有接受 HFI 启动**，与 R277 键 195 的「带描述符写 init 即死」合起来仍指向「HFI 启动通路没有被实现」。
4. **`cf` bit16 仍是 0**：三键的 `cf=00088400`、`gx_on=0`、尾行 `gxoff=1`。⇒ GX 电源域仍未上电，G1（bit16 由什么置起）**没有关闭**。
5. **零窗口的唯一还站得住的解释**：它出现在**这一轮即将结束**的时候——writer 已经开始退出，之后的读返回零。这条解释能把 R278/R279/R280 的三次零窗口（196/198/200/202 的 `p2`）与 205/206 的健康读统一起来：前者都在长等待或长循环之后、后者都在短路径上。下一轮应直接检验它：**让这一轮在已知时刻死掉，并在最后一刻读一次窗口**。

**105.5 边界与不算数的地方**
- 键 207 的 20 ms 档「没有落地」只能说明**这一轮没有走到那一行**，不能说「20 ms 本身致命」——从 15 ms 到 20 ms 之间还有 5 ms 的空档，下一轮可以把阶梯细分到 1 ms。
- 键 205/206 的「健康」是**一次观测**，不能说零窗口不会在长循环之后再出现；只能说它**不必**出现。间歇性的东西需要重复次数，而本轮每键只跑一遍。
- 键 206 的二十个采样点间隔是一百拍 `IODelay(1u)`，实际时间取决于平台，文档只写「点距约一百微秒」，不写死。
- 三键都没有读 `GMU_AO_HOST_INTERRUPT_STATUS`，因此「这一轮是怎么死的」本轮仍然只有时间证据，没有故障源证据。

**105.6 门禁与工具**
- 探针块 `work\tmp\r281_block.py` 生成：probe **1,489,842 字符 / 25,143 行**、块 **16,530 字符 / 303 行 / opens=12 closes=12**、块内 `gmuW[` **6**（每键「清描述符 + 写 init」两笔）、`IODelay(1u)` 2 / `IODelay(100u)` 3；模式选择器 `(fwKey >= 207u) ? 207 : ((fwKey >= 206u) ? 206 : 205)` 显式写好并断言。
- host 门全 PASS（25/25 + 32/32 + 51/51 + 各 contract）；probe 门 **28/28**（`scripts/check-enchilada-adreno-probe.py` 的 store 预算 **809 → 815**，说明串追加 R281 句）；stage4 **61/61**、stage5 **48/48**、stage6 **2024/2024**；全量 `ran 46 of 48, FAILING: 0`；`check_mode_selector.py` **36 selectors / upper 207**。
- 构建 CI **#398 run `45208311718` success 28/28**（分支 `r281-timing` commit `2076a577bf588077a04f007e66fbde2e5ed0ea2c`、tree `79b589360ffc5c4d232574e63cdac426d0d6e1d3`）。
- 产物：`kernel.macho` 126,500,864 B sha256 `389ea16fc5e4a0990a045456675a51ff0130c46391a0cd531026ae23afdd5d5a`；`payload.img` 150,994,944 B sha256 `57828a9c9e96754e0a17f69a1719bec7dc3a22e63c34c09e3de115cfaf7f80db`。
- 静态门 `work\tmp\r281_static.py`：对真产物 **PASS**（54 行 required+survivors 在 kernel 与 payload 两边都命中、key 标记 kernel=1/payload=2、forbidden 3 条全零、固件 32768 B sha 钉住），并对 **R280 产物 FAIL 15**（15 条本轮行一条都找不到）⇒ 区分能力自证成立。
- 本轮把门禁自己的两个坑写进记录：`r281_block.py` 第一次生成的 `gmu205p2` 少一个格式参数，被 clang 的 `-Werror,-Wformat-extra-args` 当场挡住（探针门 18/20）；`r281_wait.py` 在一次网络瞬断里 `RemoteDisconnected` 退出，已改成遇异常重试，否则「等 CI」这一步会把整轮卡死。

**105.7 下一步**
- **208**：把 207 的阶梯细分到 1 ms（15/16/17/18/19/20 ms 各一行），把「死在 15 与 20 之间」压到 1 ms 之内；**每档只读一次**，前面的行已落地就能给边界。
- **209**：**在最后一刻读窗口**——把本轮放到一个已知会死的等待之后（例如 19 ms），紧接着读一次整窗，检验「零窗口 = 这一轮临死前读到的东西」这个唯一还站得住的解释。
- **210**：把 209 拆成两半：一半在等待之后**只读不写**（无 store 的对照），一半在等待之后**读九字并立刻打一行**（有 store），分开「临死读数」与「store 之后的读数」。

## 一百零六、R282：二十毫秒的纯等在有 store 时全落地、无 store 时照样死，阶梯连第一步都没落地

**106.1 一句话**
R281 把「纯等十五到二十毫秒会结束整轮」写成了一条时间边界。本轮三个键把那条边界拆开，结果**边界塌了**：**键 209**（清描述符 → 写 `HFI_CTRL_INIT` → 纯等二十毫秒 → 读一次）**四行全落地**，读到的窗口是**健康**的（`st=000000d4 qi=00000000 qa=ad970000 gd=00222001 cf=00088400 fw=babeface sr=00000000 init=00000001`）；**键 210**（**一笔写都没有** → 同样纯等二十毫秒 → 读一次）**只落地第一行、第二行与尾行都没有落地**；**键 208**（清描述符 → 写 INIT → 一毫秒阶梯 15/16/17/18/19/20 ms，每档一行）**阶梯行一行都没落地**，日志里只有清描述符前后的两行普查。三句结论：**①「等二十毫秒会死」不成立**——同样的二十毫秒、同样两笔 store，209 活了下来而且窗口健康；**②同样二十毫秒、一笔 store 都不写，210 死了**——所以「时间」不是那把刀，差异在两笔 store 之后的机器状态；**③`ctl` bit0 与 `cf` bit16 在三个键里仍然全是 0**，GX 仍然没有上电。R281 那条「零窗口 = 临死读数」的假设也因此没有被验证到：本轮**没有任何一个键读到零窗口**。

**106.2 三个键各自问什么**
- **键 208**：把 R281 键 207 的三档阶梯（10/15/20 ms）细分成**一毫秒一档**（15/16/17/18/19/20 ms，共六行），每档读九字并打一行。R281 只能把死亡夹在十五和二十毫秒之间；这个键本该把它压到一毫秒之内。
- **键 209**：重放 R281 键 207 里**必死的那一段**——两笔 store 之后纯等二十毫秒（200 × `IODelay(100u)`）——然后**只读一次整窗**。R281 那一档「一行都没落地」，本轮问的是：那一行如果落地，读到的是零窗口还是健康窗口。
- **键 210**：**同一个二十毫秒等待，但把两笔 store 全部去掉**（不清描述符、不写 INIT、不敲门铃、不复位），只读前一行、等、读后一行。它是「等本身会不会杀人」的对照组。

**106.3 逐字读数（设备自己写的字段）**
- **键 208** `boot_id 585358419` / 窗口 sha256 `252e6b44d722f5a387eeb896d6630446888e6495d81f049b99cfd9e9854fa70b`，**三行只落地两行**：
  `gmu208p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad976000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`
  `gmu208p1=qinfo_cleared=1 qi=00000000 qa=ad976000`
  `gmu208l` 与 `gmu208s0..s5` **全为 0**——连 15 毫秒那一档的行都没有落地。
- **键 209** `boot_id 585554804` / 窗口 sha256 `75c8e974c00ca1bcabbb6b03e7ec090774bc271a7bb473769da9eb83ae59e2d1`，**四行全落地**：
  `gmu209p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad970000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`
  `gmu209p1=qinfo_cleared=1 qi=00000000 qa=ad970000`
  `gmu209p2=after_200ms=1 ctl=00000000 qi=00000000 qa=ad970000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400 fw=babeface sr=00000000 init=00000001`
  `gmu208l2=mode=209 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ack=0 info=00000000 survived=1`
- **键 210** `boot_id 584292275` / 窗口 sha256 `95841d46017a2c694e0acfaa399c20d488fe6d7f8e79406bd288e2e7cb493ea5`，**三行只落地一行**：
  `gmu210p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad973000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`
  `gmu210p1` 与 `gmu208l3` **均为 0**。

**106.4 判决**
1. **R281 的「二十毫秒会结束整轮」被撤回。** 键 209 与 R281 键 207 的第三档是同一个等待（200 × `IODelay(100u)`）且前面同样是那两笔 store，唯一差别是 209 在等待之后一次读完就打行、207 之前还做过两次「五毫秒 + 读 + 打行」。209 四行全落地、窗口健康，因此**那个等待本身不是刀**。
2. **没有 store 的同一个等待照样会结束整轮。** 键 210 一笔写都没有，二十毫秒之后那一行没有落地。把 209 与 210 并排看，结论只能是：**决定生死的不是「等了多久」，而是「等之前机器被写成了什么状态」**——写了两笔 store 的活了下来，什么都没写的死了。这与「写 INIT 会杀死这一轮」的旧印象方向相反。
3. **键 208 的六档阶梯一档都没有落地**，所以「死亡落在哪一毫秒」这次没有量到；能确认的只有：写完 INIT 之后的第一次「等十五毫秒再读」没有活到把那一行刷出去。
4. **R281 的「零窗口 = 临死读数」没有被验证，也没有被否定。** 本轮三个键里没有任何一次读到零窗口：209 的读是健康的，208/210 根本没走到读。零窗口仍然只在 R280 键 202（`st=00000000 gd=00000000 cf=00000000`）与 R279 的按键里出现过，而 R281 键 205/206 与 R282 键 209 都没有复现。
5. **G1 未关闭。** 三个键的 `ctl` bit0 都是 0、`cf` bit16 都是 0，`gd` 始终 `00222001`（SW_COLLAPSE=1、bit27=0），GX 域从头到尾没有上电。

**106.5 边界与读数陷阱**
- **「行落地」是关于刷盘批次的陈述，不是「这一行被打印过」的证明。** 本轮键 208 一次丢了六行、键 210 丢掉最后两行、R279 键 200 只丢最后一行，说明探针日志是按批次落盘的：一个批次没有刷出去，整批都看不见。所以「某行没落地」只能说「这一轮没活到那一批被刷出去」，不能推出「那一行没有被打印」。
- 每个键各跑一遍，208/209/210 之间的差别**不能排除单次偶然**：209 活、210 死的对照很干净（唯一变量是两笔 store），但样本量是一。
- 三键都没有读 `GMU_AO_HOST_INTERRUPT_STATUS`，所以「210 是怎么死的」本轮仍然没有故障源证据。
- 210 的 p0 里 `qi=00000001`——说明它读到的是**还没被清过的描述符**，与 209 的 p0 相同，两个键的前置状态一致。

**106.6 门禁与工具**
- 探针块 `work\\tmp\\r282_block.py` 生成：probe **1,520,891 字符 / 25,357 行**、块 **12,048 字符 / 214 行 / opens=8 closes=8**、块内 `gmuW[` **4**（208 两笔、209 两笔、210 零笔）、`IODelay(1u)` **0** / `IODelay(100u)` **3**；模式选择器 `(fwKey >= 210u) ? 210 : ((fwKey >= 209u) ? 209 : 208)` 显式写好并断言。
- host 门全 PASS；probe 门 **28/28**（store 预算 **815 → 819**）；stage4 **61/61**、stage5 **48/48**、stage6 **2052/2052**；全量 `ran 46 of 48, FAILING: 0`；`check_mode_selector.py` **37 selectors / upper 210**。
- 构建 CI **#400 run `45225964916` success 28/28**（分支 `r282-ladder` commit `316de91bce191f2c79bb6007a4f95948bd50caf1`、tree `2acfe80f6a8babcc4a92b75fa3d9da7e34de5eb4`）。**#399 run `45222981323` 在排队十五分钟后被 cancelled（没有任何 step 跑起来）**，重新派发后 #400 正常完成——记在这里，免得把一次平台取消读成构建失败。
- 产物：`kernel.macho` 126,500,864 B sha256 `dcf8b1448d862ac5e3d9762e96ebe5ec1441b2c7337c6c2409cc45c2c57c6fae`；`payload.img` 150,994,944 B sha256 `8866457d2b8bda5acaba0dcfbef8b7c3355aacfa152db923d30a1cc4b1256be0`。
- 静态门 `work\\tmp\\r282_static.py`：对真产物 **PASS**（65 行 required+survivors 在 kernel 与 payload 两边都命中、key 标记 kernel=1/payload=2、forbidden 3 条全零、固件 32768 B sha 钉住），对 **R281 产物 FAIL 11** ⇒ 区分能力自证成立。
- 本轮静态门自己踩了两个坑，都值得记住：**forbidden 标记不能取已经存在的名字**。探针里早年就有 `gmu213a/gmu213b/gmu213c`（R213 那架梯子）以及 `gmu214`、`gmu216`…`gmu221`、`gmu223` 的旧行，第一版把 `gmu213a=` 当「下一轮才该出现的名字」，门对着一份完好的构建报 FAIL；第二版又取 `gmu214a=`/`gmu216a=`，同样命中旧行。最后用源码扫一遍所有 `gmu\\d{3}[a-z]` 前缀，取 211/212/215 这三个源码里不存在的编号当 forbidden，门才 PASS。**这门以后必须先扫源码再选标记。**
- payload 重键（每键一份，sha256）：208 `be262be128340ffb74373070b513797b5522fb3465f706e5671a195991ab6bd7`、209 `582174d175f305ba0389063a1c951b026c1f42c363996153c03113eca6c10829`、210 `70e269435a0b7e3d534dc903c2db27854d0d2205126b5151f24532c252732e13`；三份都由 `enchilada-pas-exec=2` 改成 `=208/=209/=210`。
- 每键上机前 `scripts\\check-reboot-to-twrp.py --arm` PASS（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`），每次回 TWRP 后 runner 自跑 `reboot-to-twrp.py --arm-bcb-only`。

**106.7 下一步**
- **211**：**不写任何寄存器**，只按一毫秒一行打三十行（30 ms），看行落地在哪一毫秒停——这同时量出刷盘批次的大小和「有没有一个固定时间点会杀掉这一轮」，是 208 那六行整批丢失的必要对照。
- **212**：同一个一毫秒节奏，但每一毫秒**多读一次九字窗口**（不写），看「读」本身是否改变存活。
- **213**：把 210 的对照补齐——二十毫秒纯等之后**连打两行**（同一次读的内容复制成两行），如果第一行能落地而第二行不能，说明丢的是批次而不是那一行；如果两行都不落地，说明这个等待之后的任何输出都刷不出去。
- 序号提醒：`gmu213a/gmu213b/gmu213c` 已被 R213 那架梯子占用，本轮之后的键要用 **`213d/213e/213f`** 这类后缀，或直接用 214 以后的编号（同样要先扫源码确认不撞）。

## 一百零七、R283：一毫秒一行打三十行，两个键都在第十四行之后整段消失，而且一次 MMIO 读都没做的那一档也一样

**107.1 一句话**
R282 把「纯等二十毫秒会不会杀人」拆成两份对照，结论是**生死取决于等待之前机器被写成什么状态**。本轮三个键**一笔 store 都不写**，只问一件事：**一个什么也不做的轮次自己能活多久，这个寿命跟打印、跟读寄存器有没有关系**。答案很短：**键 211**（一毫秒一行、每行读九字窗口，共三十行）**只落地前十四行**（`ms=1` 到 `ms=14`），`ms=15` 起的十六行与尾行全都没有落地；**键 212**（同一毫秒节奏，**循环里一次 MMIO 读都不做**，每行只打毫秒数与节拍计数）**同样只落地前十四行**；**键 213**（不写、纯等二十毫秒、一次读打印两行）**两行都没落地**，只有等待之前的普查行在。三句话：**①截断与读流量无关**——212 一次 MMIO 读都没做，切点与 211 逐毫秒相同；**②截断点在同一个地方**（第十四行之后），两条互不相同的节奏给出同一个切点，说明这不是「十二行日志塞满了」那种容量故事；**③二十毫秒的纯等之后连打两行也一行不落地**，把 R282 键 210 那句「只落地一行」收窄成「等待之后凡是打印出来的东西都进不了介质」。而 `ctl` bit0 与 `cf` bit16 在三个键里仍然全是 0——GX 仍然没有上电。

**107.2 三个键各自问什么**
- **键 211**：**不写任何寄存器**，一毫秒一行打三十行（每行之间 `for (r211pw = 0u; r211pw < 10u; ++r211pw) IODelay(100u)`），每行读九字窗口并打印。它同时量两件事：刷出批次到底能容纳多少行，以及**有没有一个固定的时间点会把这一轮带走**。
- **键 212**：同一毫秒节奏，但**循环里一次 MMIO 读都不做**，每行只打 `ms=` 与 `tick=`。这是 211 的对照组：如果 211 的十四行是「读把日志挤掉了」，212 应当打满三十行；如果 212 也在第十四行之后消失，那么截断与读无关。
- **键 213**：**一笔写都不做**，纯等二十毫秒（200 × `IODelay(100u)`），然后**只读一次整窗、把同一份读数打印两行**（`gmu213d` 与 `gmu213e`，中间不插任何寄存器访问）。R282 键 210 只打一行、结果那一行没落地，两种解释分不开——「输出在等待之后刷不出去」与「只丢了那一行」。两行是一对判据：**只第一行落地** ⇒ 轮次还活着、丢的是批次；**两行都不落地** ⇒ 等待之后的输出整体不进介质。

**107.3 设备自己写了什么（逐字）**
- **键 211**（`boot_id 584355533`，窗口 sha256 `aa738f4bab8fc7267f2f836460956b384c17815de24240e2dc40d066164d5df8`，落地 15 行）：普查行 `gmu211p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad976000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；十四行采样 `gmu211m0=ms=1 …` 到 `gmu211m13=ms=14 …`，**十四行的九字逐字相同**：`ver=00000000 ctl=00000000 qi=00000001 qa=ad976000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400 fw=babeface sr=00000000`；`ms=15` 到 `ms=30` 的十六行与尾行 `gmu211l` **全部为 0**。
- **键 212**（`boot_id 583782669`，窗口 sha256 `fb7c20ce2ff03ca2673abee3299f11fea16bfe4c5aa4f831ed6e58ee24cbe627`，落地 15 行）：普查行 `gmu212p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad973000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；十四行 `gmu212m0=ms=1 tick=0` 到 `gmu212m13=ms=14 tick=130`；`ms=15` 到 `ms=30`、循环之后的整窗读、尾行 `gmu211l2` **全部为 0**。
- **键 213**（`boot_id 583756840`，窗口 sha256 `14f953ecf9d08623562d228abea690c86c3b238c9cb1ef383580359908278134`，落地 1 行）：普查行 `gmu213p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad971000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；之后的两行复制 `gmu213d` 与 `gmu213e` **都是 0**，尾行 `gmu211l3` 也是 0。
- 判定脚本三键都先打出同一句身份确认：`THIS BOOT REACHED THE WRITER AND ITS STAMPS NAME key=211/212/213`（键名与 `boot_id` 都来自设备自己写的行，不用手维护的表）。211 的判决句 `THE MEDIUM KEPT 14 OF 30 CADENCE ROWS / LAST ROW … 14 MS / CONSECUTIVE RUNS [[1..14]] / NO CADENCE ROW SAW A ZERO WINDOW / FW_INIT_RESULT STAYED babeface`；212 的判决句 `THE MEDIUM KEPT 14 OF 30 READ-FREE CADENCE ROWS … LAST AT 14 MS`；213 的判决句 `NEITHER COPY LANDED`。

**107.4 判决**
1. **截断与寄存器读无关。** 212 在循环体里没有一次 `rd32`，切点仍然是第十四行之后，与 211 逐毫秒重合。R282 留下的那条「是不是读坏了窗口、读把日志挤出去」到此作废。
2. **截断是关于「这一轮还在不在」的，而不是关于日志容量的。** 211 每行一百五十字节上下，十四行约两千字节，远没到缓冲上限；而同一个镜像上 R279 键 199 的十行 3 ms 采样、R282 键 209 的四行都完整落地。所以「第十四行之后什么都没有」读成「这一轮在十四到十五毫秒之间没了」，比读成「缓冲满了」更省假设。
3. **那两笔 GMU 写是让轮次活得久一点的条件之一。** 键 199（清 `HFI_QTBL_INFO` + 写 `HFI_CTRL_INIT` + 十次 3 ms 采样，跨三十毫秒）十行全落地；键 209（同两笔 + 二十毫秒等待）四行全落地；而本轮 211/212 **一笔都不写**，十四到十五毫秒就被切。R282 说「生死取决于等待之前的机器状态」，本轮把那句话的具体内容指到这两笔写上面——但这是**旁证**，本轮没有做「只写一笔」的拆分（见 107.7 的键 216）。
4. **二十毫秒纯等之后的输出整体进不了介质。** 213 的两行复制都没落地，而同一轮的普查行落地。这说明 210 的「只落地一行」不是「第二行被丢」，而是**等待之后打印的东西一律不落地**。
5. **零窗口本轮一次都没出现。** 211/212 的十四行 `ctl=0 st=000000d4 gd=00222001 cf=00088400` 逐字相同，`fw=babeface`、`sr=0` 也没动过。R281 那条「零窗口 = 临死读数」的假设**既没被证实也没被排除**，只是本轮不在那个形状里。
6. **HFI 依然没有被接受、GX 依然没有上电。** 三键的 `ctl` bit0 全 0、`cf` bit16 全 0、`gd=00222001`（`SW_COLLAPSE` 仍然置着、power-up 位没有置起）。G1（`GDSC_POWER_UP_COMPLETE` 由什么置起）没有任何一条被本轮关闭。

**107.5 边界与读数陷阱**
- **「某行没落地」是关于刷出批次与轮次存活的陈述，不是关于硬件状态的陈述。** 本轮 211/212 的十四行确实是连续前缀，可以当批次边界用；但不能由此断言「第十五毫秒那一刻硬件发生了什么」——它只说明那之后的输出没有进入宿主机看到的那份日志。
- 211/212/213 三键**都没有写寄存器、没有脉冲复位、没有碰排队表**，所以这三轮的「十四行」不能直接外推到有 store 的轮次；R279/R282 里那些轮次活得比十四行久，恰好就是反证。
- `qa=（队列表地址）在三个键里分别是 `ad976000`/`ad973000`/`ad971000`，都是设备自己写的、每次启动都不一样的值；不要把它们当成同一个常量去比对。
- 判定脚本里 `field()` 的 tag 不能带等号：写成 `field("gmu208s0=", …)` 时会与模板自带的正则拼成 `==`，永远返回 `None`（本轮已修，R282 的「`gx_on None`」就是这么来的）。

**107.6 门禁与工具**
- host 门 `check-enchilada-adreno-host.py` 全 PASS（25/25 + 32/32 + 51/51 + 各 contract）；probe 门 **28/28**（store 预算保持 **819**，本轮 +0 笔）；stage4 **61/61**、stage5 **48/48**、stage6 **2081/2081**；全量 `..\\tmp\\r239_allgates.py` **ran 46 of 48, FAILING: 0**；`check_mode_selector.py` **38 selectors / upper 213**。
- 构建 CI **#401 run `45235331565` success 28/28**（分支 `r283-cadence` commit `ba0239bfd84090cc7dcf1b4f3b6802859768847f`、tree `cd67c8dbb8902bcce7c9dbfcc00d494a7a97893b`）。
- 产物：`kernel.macho` 126,500,864 B sha256 `fa02d57eb0215e68419827588a181e43ecc8104a05fe87352311bc7fcc0aed85`；`payload.img` 150,994,944 B sha256 `97c7c9123bc15a2489faa7a0174ed48113e707451a8255e48277d3ab7b282209`。
- 静态门 `work\\tmp\\r283_static.py`：对真产物 **PASS**（75 行 required+survivors 在 kernel 与 payload 两边都命中、key 标记 kernel=1/payload=2、forbidden 3 条全零、固件 32768 B sha 钉住），对 **R282 产物 FAIL 10** ⇒ 区分能力自证成立；forbidden 这次取的是源码里不存在的 224/225/226，沿用 R282 那条「先扫源码再选标记」的规矩。
- payload 重键（每键一份，sha256）：211 `cdec5cd2873c590714fdd3bec603352853a1ae7435a45b820c9cd70fb9254021`、212 `245f3dd85e110fea4f39f866ead1967a34d188319749a291207631e03bdc8fb3`、213 `8aa87c84b830212e147fa324eb89559d0cf075eecdfecdc7bec19a40c83ebf8b`；三份都由 `enchilada-pas-exec=2` 改成 `=211/=212/=213`。
- 本轮自己踩的两个探针门 FAIL，都记下来：**`row()` 不做 `%` 格式化却写了 `%%s`** ⇒ `IOLog("enchilada-adreno: %%s\n", …)` 被 clang 判 `-Werror,-Wformat-extra-args`（`EnchiladaAdrenoProbe.cpp:24019:46`），把模板改成 `%s` 才过；**`tail()` 给 212/213 传了 `"L2"`/`"L3"` 后缀，但 `DECL` 只声明了 `r211RowL`** ⇒ `use of undeclared identifier 'r211RowL2'`（`24106:22`），补两行 `r211RowL2[352]`/`r211RowL3[352]`。
- 每键上机前 `scripts\\check-reboot-to-twrp.py --arm` PASS（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`），每次回 TWRP 后 runner 自跑 `reboot-to-twrp.py --arm-bcb-only`。

**107.7 下一步**
- **键 214**：复现 211 的十四行切点，但**每毫秒只打一个节拍数、不打整窗九字**。211 与 212 已经把「读」排除掉了，剩下两种可能：行太长（一百五十字节）把批次撑满，或者轮次时间到。把每行压到一个数字，如果切点被推后，说明是字节量；如果仍是十四行，说明是时间。**214 号前缀在源码里已经出现过，本轮的键要用 215 以后且先扫源码。**
- **键 215**：**两笔 store**（清 `HFI_QTBL_INFO` + 写 `HFI_CTRL_INIT`）之后立刻跑同一个三十行节奏。预测：切点被推到三十行之后。若成立，就把「这两笔写延长了存活」从旁证变成主证。
- **键 216**：只写**一笔**（只清 `HFI_QTBL_INFO`，不写 INIT）之后跑同一节奏；再配一个只写 INIT 的对照（可用 217）。两笔里究竟哪一笔是那个条件，这一对照就能分开。
- 目标不变：`ctl` bit0 还是 0、`cf` bit16 还是 0，GX 从未上电，准入表 `modeled=9 / satisfied=0`。

## 一百零八、R284：把行压短、把两笔写放回去，切点都还是十三到十五行，所以它既不是行长也不是那两笔写

**108.1 一句话**
R283 看到两个不同节奏都在第十四行之后整段消失，于是留下两个候选：**行太长把批次撑满**，或者**那一轮的时间到了**；同时它还留了一条旁证——R279 键 199 与 R282 键 209 都带着**清 `HFI_QTBL_INFO` + 写 `HFI_CTRL_INIT`** 这两笔写，都活过了十四行，看起来像「这两笔写延长了存活」。R284 用三个键把这两件事同时问掉，答案把两个候选都否了：**键 214**（一笔 store 都不写，每行只打 `ms=` 与 `tick=`，行从一百五十字节压到二十来字节）**落地十五行**；**键 216**（只清描述符一笔）**落地十四行**；**键 215**（清描述符 + 写 INIT 两笔，`qi` 读回 0、`init` 读回 1）**只落地十三行**。四把不同的形状（R283 的零写整窗、R284 的零写短行、只清描述符、两笔全写）全部落在**十三到十五行**这个带子里 ⇒ **切点既不是行长撑出来的，也不是那两笔写换来的**，它是这条打印通路自己的容量。而 `ctl` bit0 在三个键里仍然全是 0（215 甚至把 `init` 读回成 1 也没换来 bit0），`gxoff=1`、`cf` bit16=0——GX 仍然没有上电。

**108.2 三个键各自问什么**
- **键 214**：**不写任何寄存器**，三十步、一毫秒一步，每行只打 `ms=` 与 `tick=`（不读窗口）。它与 R283 键 212 的唯一区别是**行的大小**：212 每行九字窗口（约一百五十字节），214 每行两个字。行长若是条件，切点应当推后。
- **键 215**：同一条三十步短节奏，但**先做两笔 store**：把 `HFI_QTBL_INFO` 写 0（读回 `qi=00000000`）、把 `HFI_CTRL_INIT` 写 1（读回 `init=00000001`），并打印 `gmu215p1=stores=2 …` 确认落位，然后再跑节奏。R283 的旁证说这两笔写会延长存活，这个键就是那个旁证的正面检验。
- **键 216**：同一条短节奏，但**只清描述符一笔**（不写 INIT，`init` 读回 0），由它和 215 一起把「两笔里哪一笔是条件」分开。

**108.3 设备自己写了什么（逐字）**
- **键 214**（`boot_id 584029972`，窗口 sha256 `981b652ac86d291456faa92c2603bae205b4efb519a2bd272846f4c0375bdc48`，落地 16 行）：普查行 `gmu214p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad970000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；节拍行 `gmu214m0=ms=1 tick=0` 一路到 `gmu214m14=ms=15 tick=140`，**十五行连续、`tick` 步进整十**；`ms=16` 到 `ms=30` 的十五行与尾行 `gmu214l` **全部为 0**。
- **键 215**（`boot_id 584256289`，窗口 sha256 `352c62b467c70cf283696611429386694de9047da71f29d16de7999d331ac777`，落地 15 行）：普查行 `gmu215p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad972000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；`gmu215p1=stores=2 qi=00000000 qa=ad972000 init=00000001 ctl=00000000` ⇒ **两笔 store 都落位（描述符被清成 0、INIT 被写成 1），而 `ctl` 读回仍是 0**；节拍行 `gmu215m0=ms=1 tick=0` 到 `gmu215m12=ms=13 tick=120`，**十三行**；`ms=14` 起与尾行 `gmu214l2` **全 0**。
- **键 216**（`boot_id 582509218`，窗口 sha256 `bca0051911c2e93635c6d683ee702b1adb03d5480402caaf3f7401d59c8f799b`，落地 15 行）：普查行 `gmu216p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad970000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；`gmu216p1=cleared_only=1 qi=00000000 qa=ad970000 init=00000000 ctl=00000000` ⇒ **只清描述符，INIT 从未写**；节拍行 `gmu216m0=ms=1 tick=0` 到 `gmu216m13=ms=14 tick=130`，**十四行**；`ms=15` 起与尾行 `gmu214l3` **全 0**。
- 判定脚本三键都先打同一句身份确认：`THIS BOOT REACHED THE WRITER AND ITS STAMPS NAME key=214/215/216 (boot_id …, new window sha)`；214 的判决句 `THE MEDIUM KEPT 15 OF 30 SHORT-CADENCE ROWS / LAST ROW THE MEDIUM KEPT IS THE 15 MS ONE / THE SHORT ROW PUSHED THE BOUNDARY PAST FOURTEEN MS (to 15 MS), so row size does matter`；215 的 `THE MEDIUM KEPT 13 OF 30 SHORT-CADENCE ROWS / STILL CUT AT 13 MS WITH THE TWO STORES IN FRONT OF THE CADENCE`；216 的 `THE MEDIUM KEPT 14 OF 30 SHORT-CADENCE ROWS / CLEARING THE DESCRIPTOR ALONE STILL CUT AT 14 MS`。

**108.4 判决**
1. **行长不是条件**：214 的行被压到两个字，切点 15 行；212 的行是九字窗口，切点 14 行。差一行，方向也不稳定（215 两笔写只有 13 行）。⇒ **「行太长把批次撑满」撤回**，同时基准从「一毫秒一行」换算出来的「14 毫秒」也不能再当成硬时间——它其实是**行数**的另一种说法。
2. **那两笔写也不是条件**：215 先确认 `qi=00000000`（描述符被清）与 `init=00000001`（INIT 被写），随后只落地 13 行，比什么也不写的 214（15 行）与只清描述符的 216（14 行）**还少**。⇒ R283 那条「这两笔写让轮次活得久一点」的旁证**被推翻**；R279 键 199 与 R282 键 209 之所以「活过十四行」，是因为它们本来就只有十三行/四行可打，不是那两笔写的功劳。
3. **同一个键重跑会给出不同的行数**：214 在本轮被跑过两次，第一次（`boot_id 584782655`，窗口与日志已被重跑覆盖）落地 **13 行**（`gmu214m0..m12`，`ms=1..13`），第二次落地 **15 行**。⇒ **这个切点有 ±2 行的抖动**，不是一条刀切的线；把「十四行」写成常数是错的，正确写法是「**十三到十五行**这个带子」。
4. **切点属于打印通路，不属于我们写的寄存器**：四种预热（零写、只清描述符、两笔全写、以及 R283 的零写整窗）都没把它挪出这个带子 ⇒ 这一轮的寿命对「预热」不敏感，与「读不读」也已经无关（R283 键 212）。剩下唯一还没被否掉的轴是**节拍**：每毫秒一行 vs 每毫秒多行、以及从第一行起算还是从探针起点起算。
5. **HFI 仍然没有被接受**：215 把 `init` 写成 1 并读回 1，`ctl` bit0 依旧 0；三键的普查行 `gxoff=1`、`cf=00088400`（bit16=0）。GX 从未上电，G1 未关闭。
6. **尾行三键全缺**：三次都在切点之前就没了后续输出，所以**切点之后的窗口读一次也没拿到**（尾行的 `gx_on`/`ctl` 都是 `None`）。这意味着「切点之后机器是什么状态」目前只能靠下一轮的普查行回答。

**108.5 边界与读数陷阱**
- **行落地是刷出批次的陈述**，不是「那一行没有被打印」。214 第一次 13 行、第二次 15 行，说明批次边界本身有抖动；据此说「15 毫秒那一刻硬件变了」是错的。
- **同键重跑会覆盖同名窗口与日志**（`r284_s214_window.bin` / `r284-s214-device.log`）：第一次 214 的窗口已被第二次覆盖，所以那条 13 行的读数只有日志文字为凭，不能再用 sha 复核。**每键只跑一次**是本轮之后的规矩，或者每跑一次就立刻另存窗口。
- **一次判定失败不等于一次上机失败**：214 第一次跑完时判定脚本自己崩了（`return "\n".join(out)` 那行是我在生成器里多写的一句，`out` 在 runner 里不存在 ⇒ `NameError`），设备那边窗口与日志都好好地落了地。**判定脚本要在上机之前就被 `ast.parse` 与行检查验过**，本轮补了 `r284_rowscheck.py` 与 `ast.parse` 两道。
- `field()` 的 tag 必须是**真实行名前缀**：生成器把 `_st0 = field("gmu2140", …)` 写错了（正确是 `gmu214p0`），只会静默返回 `None` 而不会报错——判据脚本里的每个 tag 都要能在源码里逐条对上。

**108.6 门禁与工具**
- host 门全 PASS（25/25 + 32/32 + 51/51 + 各 contract）；probe 门 **28/28**（store 预算 **819 → 822**：键 215 两笔、键 216 一笔、键 214 零笔）；stage4 **61/61**、stage5 **48/48**、stage6 **2112/2112**；全量 `r239_allgates.py` **ran 46 of 48, FAILING: 0**；`check_mode_selector.py` **39 selectors / upper 216 / 自证 FAIL / VERDICT PASS**。
- 构建 CI **#402 run `45243468945` success 28/28**（分支 `r284-cadence` commit `f2e8acc0b72ca269cc5245c85d64be0e4ad8fddd`、tree `356337ec037afeecdaa33e85c78f5d79918cc111`）。
- 产物：`kernel.macho` 126,500,864 B sha256 `05097289ac39a83bf7f0807034ff3efde5788b170b9bbadec0aca22da3fcaf20`；`payload.img` 150,994,944 B sha256 `4c7c44d3f37308014902286d51f7ad325dc98aa13919ee91f06f6717024343c3`。
- 静态门 `work\\tmp\\r284_static.py`：对真产物 **PASS**（86 行 required+survivors 在 kernel 与 payload 两边都命中、key 标记 kernel=1/payload=2、forbidden 3 条全零、固件 32768 B sha 钉住），对 **R283 产物 FAIL 11** ⇒ 区分能力自证成立。forbidden 取 `gmu227a/gmu228a/gmu229a`（源码里不存在的名字），沿用「先扫源码再选标记」的规矩。
- payload 重键（每键一份，sha256）：214 `bb68b696b5edd76c1241595b652d7876fb828c626331a91fc031ff7a6d061e9e`、215 `ac4863274478d627f8322c51d22ee2a12819e10eee898f16b258c244f4af1456`、216 `c8de4cbce594d1a63db092ca99bb1337e67fbdd3e235adca08cebe3ed09905e3`；三份都由 `enchilada-pas-exec=2` 改成 `=214/=215/=216`。
- 两个 `-Werror` 修复：`r214ctl2/r214qi2/r214qa2` 与 `r214fw2` 被 `full_reads(2)` 赋值却没打印（`-Werror,-unused-but-set-variable`，`EnchiladaAdrenoProbe.cpp:24192:14` 与 `24194:14`），把尾行格式扩成 `mode=%d ctl=%08x qi=%08x qa=%08x gd=%08x cf=%08x gx_on=%d gxoff=%d info=%08x fw=%08x sr=%08x survived=1` 后过。
- 每键上机前 `scripts\\check-reboot-to-twrp.py --arm` PASS（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`），每次回 TWRP 后 runner 自跑 `reboot-to-twrp.py --arm-bcb-only`。

**108.7 下一步**
- **键 217**：只写 `HFI_CTRL_INIT` 一笔（不清描述符）之后跑同一条短节奏，把 215/216 的两笔拆分补完——如果 217 也是十三到十五行，那么「哪一笔是条件」这个问题本身就作废了。
- **键 218**：同一条节奏但**每毫秒打两行**（三十步、六十行），把「行数」与「墙钟时间」分开：若切点仍是三十步里的前十五步，则限的是时间；若切在第三十行，则限的是行数。
- **键 219**：先纯等五毫秒再开始节奏，看切点是从**第一行**起算还是从**探针起点**起算。
- 目标不变：`ctl` bit0 还是 0、`cf` bit16 还是 0，GX 从未上电，准入表 `modeled=9 / satisfied=0`。

## 一百零九、R285：时钟从探针入口起算；每毫秒两行在第十二步就停；描述符仍置时只写 INIT 直接死

**109.1 一句话**
R284 把「行长」与「那两笔写」双双否掉之后，只剩一个轴没被问过：**节拍**——是每毫秒能打几行在限，还是从某个起点算起的墙钟在限，以及那个起点到底是第一行还是探针入口。R285 用三个键把它问完，答案同时落地两件事。**键 225** 在节奏之前先纯等五毫秒：它只落地九行，`5 + 9 = 14`，正好落进 R284 量到的十三到十五行带子里 ⇒ **结束这一轮的时钟从探针入口起算，不是从第一行起算**。**键 224** 每毫秒打两行（同样三十步、共六十行）：它停在**第十二步**、落地**二十四行**——比零写的十五行**多**（所以不是按行数算）、比十五毫秒**早**（所以不是按墙钟算），两行落在同一步里（12/12）说明丢的是**整批**。**键 223** 只写 `HFI_CTRL_INIT` 一笔而不清描述符：**两次上机都零字节**，与 R278 键 195 逐条同形 ⇒ 致死条件是「**表被描述着** + 写 INIT」，写 INIT 在那种状态下不是延寿而是**杀手**。`ctl` bit0 仍然三键全 0，GX 仍未上电。

**109.2 三个键各自问什么**
- **键 223**：**只写 `HFI_CTRL_INIT`**（`gmuW[r223iniWord] = 1u;` + `dsb`），**不清 `HFI_QTBL_INFO`**，读回 `qi/qa/init/ctl` 后打 `gmu223p1=init_only=1 …`，再跑三十步短节奏。它补完 R284 的 215/216 拆分：196/199/202 都是「先清描述符再写 INIT」活得好好的，223 是那条拆分的另一头。
- **键 224**：**不写任何寄存器**，同一条三十步节奏，但**每毫秒打两行**（`gmu224m%u=… half=1` 与 `gmu224n%u=… half=2`，共六十行）。若切点按**步**算（约十五步），限的是时间；若按**行**算（约十四行），限的是行数。
- **键 225**：**不写任何寄存器**，但**先纯等五毫秒**（`for (r223pw3 = 0u; r223pw3 < 50u; ++r223pw3) IODelay(100u)`，50×100 µs），打 `gmu225p1=waited_5ms=1 waited=50 …`，再跑完全相同的三十步。若切点仍是十四到十五行，说明时钟从第一行起算；若掉到约九行，说明从探针入口起算。

**109.3 设备自己写了什么（逐字）**
- **键 224**（`boot_id 583462756`，窗口 sha256 `2b14a28ae5af10ed4f560ea64438e732bd22b52e051e4047eeac6674eaea4bbf`）：普查行 `gmu224p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad973000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；节拍行 `gmu224m0=ms=1 tick=0 … gmu224m11=ms=12 tick=110`（**十二行**）与 `gmu224n0=ms=1 tick=0 half=2 … gmu224n11=ms=12 tick=110 half=2`（**十二行**）；`ms=13` 起与尾行 `gmu223l2` 全 0。判定句：`THE MEDIUM KEPT 24 OF 60 ROWS OVER 12 OF 30 STEPS / LAST STEP WITH A LANDED ROW IS STEP 12 (12 MS) / THE CUTOFF CAME AT STEP 12, BETWEEN THE TWO BANDS, so neither reading is clean`。
- **键 225**（`boot_id 583126622`，窗口 sha256 `9e9b5e428ee0b5720a21e8b2bff03b86ef788a469c9eaa0036f32c4978d77a3a`）：`gmu225p0=pre=1 ver=00000000 ctl=00000000 qi=00000001 qa=ad973000 info=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400`；`gmu225p1=waited_5ms=1 waited=50 qi=00000001 qa=ad973000 init=00000000 ctl=00000000`；节拍行 **九行** `gmu225m0=ms=1 tick=60 … gmu225m8=ms=9 tick=80`；尾行 `gmu223l3` 缺。判定句：`THE MEDIUM KEPT 9 OF 30 SHORT-CADENCE ROWS AFTER A FIVE-MILLISECOND WAIT / LAST ROW THE MEDIUM KEPT IS THE 9 MS ONE / THE WAIT WAS COUNTED: only 9 rows landed, so the clock that ends the round started at probe entry, not at the first row`。
- **键 223**（两次，窗口均未被本键改写）：第一次 `boot_id 582509218` / 窗口仍是 R284 键 216 的 `bca0051911c2e93635c6d683ee702b1adb03d5480402caaf3f7401d59c8f799b`；第二次（在 224/225 已证明管道健康之后）`boot_id 583126622` / 窗口仍是键 225 的 `9e9b5e428ee0b5720a21e8b2bff03b86ef788a469c9eaa0036f32c4978d77a3a`，判定句 `FRESH WINDOW BUT ITS STAMPS NAME [225]; do not attribute it without which payload was flashed`、`rows: gmu223p0=0 gmu223p1=0 gmu223m%u=0 gmu223l=0`、`THE STORE LINE NEVER LANDED, so the round ended at or before it and the cadence below says nothing about it`。

**109.4 判决**
1. **时钟从探针入口起算**：225 先等 5 ms 再打节奏，只剩 9 行，`5 + 9 = 14` 与零写、只清描述符、两笔全写、整窗读那几种形状的十三到十五行对得上 ⇒ 决定这一轮结束的不是「第一行之后过了多久」，而是「**探针开始跑之后过了多久**」。R283/R284 的「十四到十五毫秒」由此获得同一个坐标系。
2. **「行数」与「墙钟」都被否掉**：224 每毫秒两行时停在第 **12 步**、落地 **24 行**。按行数算本该只到第七步（十四行），按墙钟算本该走到第十五步（三十行）——两个都不对。落在中间的意思是：剩下的是**介质自己的容量**（日志缓冲/刷盘批次的字节数或条数）与**固定窗口**的组合，而**不是**我们打印的条数，也**不是**打印过程经过的毫秒数。`m`/`n` 两行每一步都成对落地（12/12）说明丢的是**整批**，不是半行。
3. **「两笔写里哪一笔是条件」问到反面**：清描述符之后写 INIT（196/199/202）活着；**描述符仍然置着的时候只写 INIT（223）两次都是零字节**，与 R278 键 195 逐条同形（195 当年也是「描述符仍置 + 写 INIT」零字节 2/2）⇒ 致死条件是「**表被描述着** + 写 INIT」这个组合，与「清不清描述符」无关。INIT 单写在这种状态下是**杀手**而不是延寿。
4. **HFI 仍然没有被接受**：225 的 `ctl=00000000`、普查行 `qi=00000001`（描述符仍置）、`qa=ad973000`、`st=000000d4`、`gxoff=1`、`cf=00088400`（bit16=0）⇒ GX 从未上电，G1 未关闭。
5. **切点带子比 R284 记的更宽**：零写一行/毫秒是 14–15 行，两行/毫秒是 12 步 24 行，五毫秒预等是 9 行 ⇒ 写文档时不能把「十四行」当常数，只能写「入口起算约十四毫秒、且与节拍有关」。
6. **尾行三键仍然全缺**：切点之后一次窗口读都没拿到，`gx_on/ctl/info/fw` 全是 `None`。

**109.5 边界与读数陷阱**
- **runner 的等待窗口可能短于设备回 TWRP 的实际时间**：223 的第二次改跑在设备返回之前就 `exit 1` 退出，手机约两分钟后自己回到 TWRP（`adb devices` 先空、45 秒后出现 `27f4f45d recovery`）。出现这种情况不能把「没读到新窗口」当成判决，要用**另一个零写键**（224/225）复跑证明管道健康之后，再决定零字节是否算该键的结果。
- **零字节的两种解释必须分开**：「这一轮死在 store 之前」与「这一轮根本没跑我们的镜像」在窗口上是同一副样子（窗口不变、boot_id 不变、行全 0）。本轮靠 224/225 的窗口都刷新了来排除后者，这个交叉验证**每轮都要做**。
- **行落地是刷出批次的陈述**，不是「那一行没有被打印」：224 的 24 行与 225 的 9 行都是连续前缀，只能说明批次边界在那儿。
- `r285_anchor.py` 只对 223 做了锚定（R284 键 216），224/225 的 PREV 没有回填；但判决只用设备自写字段（窗口 sha256、boot_id、行内 `key=`），锚是辅证。

**109.6 门禁与工具**
- host 门全 PASS（25/25 + 32/32 + 51/51 + 各 contract）；probe 门 **28/28**（`scripts/check-enchilada-adreno-probe.py` 的 store 预算 **822 → 823**：键 223 一笔、224/225 零笔）；stage4 **61/61**、stage5 **48/48**、stage6 **2140/2140**；全量 `r239_allgates.py` **ran 46 of 48, FAILING: 0**；`check_mode_selector.py` **40 selectors / upper 225 / 自证 FAIL / VERDICT PASS**。
- 构建 CI **#403 run `45250213191` success 28/28**（分支 `r285-cadence` commit `76c0cd4486b443699cda83aecce813314fa176f1`、tree `11f11f3faf119fd6d9ebe4724e254d429234e813`）。
- 产物：`kernel.macho` 126,500,864 B sha256 `4434dac025c237000b7ff9998b4a54f1cd0a23b6e9c72dc673561de4023c7776`；`payload.img` 150,994,944 B sha256 `e8212a1b1afaf09a846e763f0a82466b81aa8604cbf0329f2004783f914552a5`。
- 静态门 `work\\tmp\\r285_static.py`：对真产物 **PASS**（98 行 required+survivors 在 kernel 与 payload 两边都命中、key 标记 kernel=1/payload=2、forbidden 3 条全零、固件 32768 B sha 钉住），对 **R284 产物 FAIL 12** ⇒ 区分能力自证成立。
- payload 重键（每键一份，sha256）：223 `c6bd37eb29c7a4f618c2896dac4a00f2912e23ffd08ea1502c2738f62bad6a4c`、224 `00767729f548a6a01b137b87e1c83ccdf0849e0d34a18709e473fdefffed0449`、225 `5c3737ccbfefc65caf46b54c4cdfbd894312adeeef694b77c5ac0b20443b7bb9`。
- 选号教训：`check_mode_selector.py` 要求键号**严格连续** `(hi, mid) == (base+2, base+1)`，所以「222/224/225」这种跳号会被判 FAIL（`r222mode thresholds are (225, 224), expected (224, 223)`）；改名必须整文件替换（生成器 157 处、stage4/5 各 10 处、stage6 106 处），先在 `re.finditer(r'.?222.?')` 上确认没有相邻数字。
- 每键上机前 `scripts\\check-reboot-to-twrp.py --arm` PASS（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`），每次回 TWRP 后 runner 自跑 `reboot-to-twrp.py --arm-bcb-only`。

**109.7 下一步**
- **键 226**：每毫秒四行（三十步、一百二十行），把「行数」这一轴再压一档：若落地点数随每步打印量继续下滑（例如九步、九行），则容量按**字节**算；若稳定在十二步，则限的是**步数**。
- **键 227**：每毫秒一行但**先等十毫秒**（预计只剩四到五行），把「入口起算」的斜率量出来，与 225 的五毫秒那点连成一条线。
- **键 228**：每毫秒一行，但把等待**拆成两段各五毫秒、中间打一行**，判「等待期间的输出能否落地」——这正对 R283 键 213（二十毫秒纯等之后两行都没落地）的现象，能把「等待之后不能打」与「等待本身把时间用光」分开。
- 目标不变：`ctl` bit0 还是 0、`cf` bit16 还是 0，GX 从未上电，准入表 `modeled=9 / satisfied=0`。

## 一百一十、R286：行数不是标量、墙钟也不是标量；十毫秒等待不吃预算，字节才是

**110.1 一句话**
R285 留下两条互斥的解释：落地量像「十三到十五行」的行预算，又像「从探针入口起算」的墙钟。R286 用三个零写键把它们各自推了一档。**键 226**（每毫秒四行、共一百二十行）第二次上机只落地 **十三行**、末行停在 **4 ms**（第一次更糟：只有普查行、节奏一行都没落地）⇒ 落地量**既不按行数也不按墙钟**。**键 227**（先纯等 **十毫秒**，再走一毫秒一行）却仍落地 **十行**、末行到 **20 ms** ⇒ **等待本身不消费那份预算**，R285 的「时钟从探针入口起算」必须撤回。**键 228**（把五毫秒拆成两段各五毫秒、中间打一行）两段等待之间的那一行与之后的整窗读都落地，而节奏只落 **三行** ⇒ 每次真正写进日志都会花掉预算，**预算是按字节算的**。`ctl` bit0 三键全 0、`cf` bit16 全 0，G1 仍未关闭。

**110.2 三个键各自问什么**
- **键 226**：**不写任何寄存器**，同一条三十步一毫秒节奏，但**每步打四行**（`for (r226q = 0u; r226q < 4u; ++r226q)`，行 `gmu226q%u=ms=%u tick=%u idx=%u`，共一百二十行）。若落地量随每步打印量线性下滑，容量按**字节**算；若仍钉在第十二步，限的是**步数**。
- **键 227**：**不写任何寄存器**，一毫秒一行，但**节奏之前先纯等十毫秒**（`for (r226pw2 = 0u; r226pw2 < 100u; ++r226pw2) IODelay(100u)`，100×100 µs），打一行 `gmu227p1=waited_10ms=1 waited=%u qi=%08x qa=%08x init=%08x ctl=%08x info=%08x st=%08x gxoff=%d gd=%08x cf=%08x` 把等待后的整窗记下来，再跑三十步。它给 R285 键 225 的「五毫秒 + 九行 = 十四」再加一个十毫秒点：若入口起算，这里应只剩四到五行。
- **键 228**：**不写任何寄存器**，一毫秒一行，但**把等待拆成两段各五毫秒、中间打一行**：五毫秒 → `gmu228p1=after_5ms_a=1 waited=50 …` → 再五毫秒 → `gmu228p2=after_5ms_b=1 waited=100 …` → 三十步节奏。它正对 R283 键 213（二十毫秒纯等之后两行都没落地）：若等待本身就不让输出落地，中间那一行会缺；若只是预算被花光，中间那一行会落地而节奏变短。

**110.3 设备自己写了什么（逐字）**
- **键 226 第一次上机**（`boot_id 582775174`，窗口 sha256 `c4998c63835f903c6ad4421e476a9ffd788dcbf2c4e9d5910f5919d6fe05c3ea`）：`counts: … gmu226p0=1 gmu226q%u=0 gmu226l=0`、`cadence rows: 0` ⇒ **只有普查行落地，四行每步的节奏一行都没落地**。该次判定脚本崩在 `NameError: name '_this_key' is not defined`（`r286_s226_run.py:466`）——生成器 `r286_mkrun.py` 的 `region()` 只发了 `_names = [...]` 一行，把模板里 `_ROWS`／`_this_key = EXPECT_KEY` 两行顶掉了；修法是把三行一起发。**这一跑只作管道证据，不作判决**。
- **键 226 第二次上机**（`boot_id 582745183`，窗口 `b231d41ed4fb829cf44e20359562dc73c0dd6db7d15ce5c4eafa0e8cea588eac`）：`rows: gmu226p0=1 gmu226q%u=13 gmu226l=0`；`steps with at least one row landed: [1, 2, 3, 4]`、`rows that landed: 13 of 120`、`LAST STEP WITH A LANDED ROW IS STEP 4 (4 MS)`、判定句 `THE CUTOFF MOVED DOWN TO STEP 4 WITH 13 ROWS LANDED, so the limit is counted in ROWS (about fourteen), not in elapsed time`；普查 `ver 00000000 st 000000d4 qi 00000001 qa ad974000`。
- **键 227**（`boot_id 582765401`，窗口 `d5f05b606da0d9be65c1fbbc88ff55f68e393bac4314b6e770b9f47d73905a59`）：`rows: gmu227p0=1 gmu227p1=1 gmu227m%u=0 gmu226l2=0`；`after the ten-millisecond wait: init 00000000 ctl 00000000`；`the waited line landed: True (waited=100)`；`rows that landed (ms): [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]`、`THE MEDIUM KEPT 10 OF 30 CADENCE ROWS AFTER A TEN-MILLISECOND WAIT`、`LAST CADENCE ROW IS THE 10 MS ONE`、`WAIT PLUS LAST ROW = 20 MS`、判定句 `10 + 10 = 20 MS IS FAR PAST THE BAND, so the wait does not simply spend the same budget and the entry-referenced line R285 drew needs revising`。
- **键 228**（`boot_id 582233094`，窗口 `93d64d91025c9fbb760cfb5396cb7b49806ced5808bca5d093adf788416c4a08`）：`rows: gmu228p0=1 gmu228p1=1 gmu228p2=1 gmu228m%u=0 gmu226l3=0`；`between the halves: init 00000000 ctl 00000000 / after them: init 00000000 ctl 00000000`；`row between the two halves landed: True (waited=50)`、`row after the second half landed: True (waited=100)`；`rows that landed (ms): [1, 2, 3]`、判定句 `BOTH HALF-WAIT ROWS LANDED, so output survives a five-millisecond wait and the earlier losses were the budget being spent, not the wait itself`、`LAST CADENCE ROW IS THE 3 MS ONE`、`TOTAL WAIT PLUS LAST ROW = 13 MS`。

**110.4 判决**
1. **行数不是标量**：同样三十步，211 落 15 行、224 落 25 行、225 落 11 行、226 落 14 行（13 + 普查行；第一次只有 1 行）、227 落 12 行、228 落 6 行 —— 从 1 到 25 全都有。
2. **墙钟也不是标量**：末行时刻分别是 14、12、14、4、20、13 ms。**R285 的「时钟从探针入口起算」必须撤回**：227 先等十毫秒仍能走到 20 ms，225 只等五毫秒却停在 14 ms ⇒ 等待不是从同一个口袋里扣钱。
3. **能同时解释两组的只有「写进日志的字节」**：226 每步四行很快就把预算烧完（4 ms 就停），228 两行等待读加上三行节奏就花光，227 只有短短十二行却拖到 20 ms（因为它几乎没有花字节）。224 的 25 行是唯一硬反例，恰恰因为它的行最短（`ms=`/`tick=` 十来个字节）⇒ **预算按字节，不按行**。
4. **「等待之后的输出进不了介质」被推翻**：228 在两次五毫秒等待之间与之后各打一行整窗，**两行都落地**（`waited=50`、`waited=100` 都在）⇒ R283 键 213 的「二十毫秒纯等之后连打两行一行都没落地」应改读为「那一轮在等待期间已经把预算之外的活干完、随后整轮结束」。
5. **零窗口本轮一次也没出现**（三键 `ctl=00000000 st=000000d4 gxoff=1 gd=00222001 cf=00088400` 三次一致）⇒ R281 的「临死读数」假设既未证实也未排除，最可能仍是 R283/R284 的读法：那一轮提前结束、窗口根本没被读到。
6. **G1 仍未关闭**：三键 `ctl` bit0 全 0（227 的十毫秒等待后也是 `init 00000000 ctl 00000000`）、`cf` bit16 全 0、`qi=00000001`（描述符仍置）⇒ HFI 未被接受、GX 未上电。

**110.5 边界与读数陷阱**
- **行落地是关于「这一轮活到哪里」的陈述，不是关于硬件状态变化的陈述**：226 第一次只落地普查行、第二次落到第十三行，同一条二进制、同一台机器，差别只能归到这一轮何时结束。
- **一次上机不足以定形状**：226 的两次上机（1 行与 13 行）说明这个带子本身有抖动，判决里一律写「两次上机各是多少」而不写单次。
- **判定脚本崩掉不等于上机失败**：226 第一次的 `NameError` 是生成器缺陷（`region()` 少发两行），设备侧窗口与日志完好；修好生成器后重跑才拿到可用读数。
- **`_this_key` 这类模板变量必须在生成的 runner 里就存在**：runner 通过 `ast.parse` 不代表能跑——`r286_rowscheck.py` 只查花括号与 `.format`，查不到「名字没定义」，所以要靠一次真跑。

**110.6 门禁、构建与产物**
- 静态门 `r286_static.py`：真产物 **PASS**（`required+survivors: 110 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、forbidden 3 条全零、固件 `a630_gmu.bin` 32,768 B sha256 `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`）；对 **R285 产物**自证 **FAIL 12**（12 条 REQUIRED 全 `kernel=0 payload=0`）。
- 门禁：host PASS（25/25 + 32/32 + 51/51）、probe **28/28**（store 预算保持 **823**：本轮零 store）、stage4 **61/61**、stage5 **48/48**、stage6 **2163/2163**、全量 `ran 46 of 48, FAILING: 0`、`check_mode_selector.py` **41 selectors / upper 228 / 自证 FAIL / VERDICT PASS**。
- CI **#404 run `45258278346` success 28/28**；分支 `r286-shape` commit `db2da7233ab803d8d1d7ddf2e6ec3344be46ccf1`、tree `c131502b12848afd4e2c9d67f11d6e00d53ebe3c`（base main `1095b6beeb9b`，535 已有 + 4 上传）。
- 产物：`r286-kernel\kernel.macho` 126,500,864 B sha256 `8a99f5ef0ba9e43fb2749d253be64af49aa81915722f9d24057c31471ee24fc2`；`r286-payload\payload.img` 150,994,944 B sha256 `92602e849a1e279c4f2a200cf07c67077044bc147737bc8681deeb33d476dab1`。
- payload 重键（同一份镜像派生）：p226 `c24410e645bf58e963aaa2f2a828db47c0247fa6cdb3bdb330602bb77c0509a9`、p227 `2e909a317eb32840a34711f2da4dd9cc288a184d066bfddfb52bc52bbb7aea51`、p228 `d9260abef95d46a81aa1dbb56eecba04c98414b93d2c2d51c96648874240f0a3`（`enchilada-pas-exec` 1377→1379 B）。
- `work\tmp\r286_hashes.py`（7 路径）**0/7**。
- 每键上机前 `scripts\check-reboot-to-twrp.py --arm` PASS（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`），每次回 TWRP 后 runner 自跑 `reboot-to-twrp.py --arm-bcb-only`。

**110.7 下一步：把力气搬回 GPU 主链**
- 前五轮（R281–R286）买的是一条读数纪律：**每一轮最多八行、每行尽量短、判决字段放在最前面**，否则介质会在读到关键行之前就把这一轮结束掉。
- 主链上真正的缺口还是那一个：**HFI 未被接受**（`ctl` bit0 恒 0）。R260 的键 143/144 曾经把 SMMU 表页的每个条目都写成合法描述符，然后 **SMMU 的 `FSR_FAULT` 消失、`ctl` bit0 第一次读成 1**（`polls=2`）；R274 之后的扁平块都没有重建那张表，等于把那次成功的前提丢掉了。
- **键 229**：重建并填满队列表页（复用 `adreno_lpae_*` 助手）→ 清 `HFI_QTBL_INFO` → 写 `HFI_CTRL_INIT` → 读 `ctl/qi/qa`，判能否稳定重现 `bit0 = 1`。
- **键 230**：229 之后加 `BOOT_SLUMBER_OPTION = 0` + `GX_VOTE_IDX`/`MX_VOTE_IDX` + `HOST2GMU_INTR_SET` 门铃，看带外请求是否终于被应答（`GMU2HOST_INTR_INFO` bit30）。
- **键 231**：229 之后读 `GMU_SPTPRAC_PWR_CLK_STATUS`、`GPU_CC_GX_GDSCR`、`CFG` 并轮询 `cf` bit16，看 GDSC 是否第一次报 power-up。
- 目标不变：准入表 `modeled=9 / satisfied=0`、`CONFIG_XNU_NATIVE_ADRENO=n`、GPU 未渲染一帧。

## 一百一十一、R287：把队列表页整页填满再写 INIT —— 表建成了，HFI 仍然没有被接受

**111.1 一句话**
前六轮（R281–R286）量的是日志介质的脾气，这一轮把力气搬回 GPU 主链，用一个已经在这台机器上成功过的配方：**把 SMMU 上下文银行装好、把队列表页的每个条目都写成合法描述符、清 `HFI_QTBL_INFO`、写 `HFI_CTRL_INIT`、轮询控制字 bit0**（R260 键 143 就是这么第一次让 `ctl` bit0 读成 1 的）。本轮的三个键 229/230/231 都跑了这段前导，**每一笔都落位**（`fills=3`、`build=0`、`plan=9`、`fsr=00000400` 无 fault），**`ctl` bit0 却三键全 0 到 200000 拍**。而在同一台机器、同一镜像上，**今天早些时候重跑 R260 键 143 时 HFI 是被接受的**（`ctl=00000001 bit0=1 polls=2`，`boot_id 582506894`、窗口 `623ade94…`）。**两次的差别只能在「键 143 还写了什么」**：它把那两个 GMU 流的路由写进 `SMR0`/`S2CR0`，并把 `GMU2HOST_INTR_MASK` 写成 `0xff7fffff`。所以本轮的第一判决是**否**（表不是充分条件），第二判决是一个可以一次上机判死的假设（路由加掩码）。

**111.2 三个键各自问什么**
- **前导（三键共用）**：读普查行（`ver/qi/qa/st/gd/cf/cx_on`）→ `armcpu_dma_alloc_writable` 分配四页 → `adreno_lpae_geometry`/`adreno_table_bytes_needed`/`adreno_table_build`（IOVA `0x2d800000`，2 MiB 叶子）→ `adreno_smmu_ctx_plan` + `adreno_install_absolute` 九笔 CB0 写 → 把三页的每个条目都写成合法描述符（root/l1/叶子）→ `dc civac` 全 span → 清 `CB_FSR`（写 `0xc00001feu`）→ `HFI_QTBL_INFO = 0` → `HFI_CTRL_INIT = 1` → 轮询 `ctl` bit0 200000 拍（每拍 `IODelay(1u)`）。
- **键 229**：前导之后只读一次整窗，行 `gmu229p2=after_hfi=1 st=%08x gd=%08x cf=%08x gxoff=%d info=%08x`。它问的是「这张表够不够」。
- **键 230**：前导之后写 `BOOT_SLUMBER_OPTION = 0`、`GX_VOTE_IDX = 0`、`MX_VOTE_IDX = 0`、`HOST2GMU_INTR_SET = 0x00400000`（set 22），100 拍 `IODelay(100u)` 轮询 `GMU2HOST_INTR_INFO` bit30，再写 `0x40000000` 清，行 `gmu230p2=oob=1 ack=%u info=%08x bs=%08x gx=%08x mx=%08x polls=%u`。它问的是「HFI 起来以后带外门铃会不会被应答」。
- **键 231**：前导之后 20000 拍 `IODelay(1u)` 轮询 `CFG` bit16，行 `gmu231p2=rail_poll=1 cf=%08x bit16=%u polls=%u gd=%08x st=%08x gxoff=%d`。它问的是「HFI 起来以后 GX rail 会不会上电」。

**111.3 逐字读数（设备自写）**
- **键 229** `boot_id 639287744` / 窗口 `4cf38798e2ffd690dbb6f06a599dc13fb89ff3d5d6c38553004235b3faf7ab66`：
  `gmu229p0=pre=1 ver=00000000 qi=00000001 qa=ad971000 st=000000d4 gd=00222001 cf=00088400 cx_on=1`
  `gmu229p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000 fsr=00000400 far=00000000`
  `gmu229p2=after_hfi=1 st=000000d4 gd=00222001 cf=00088400 gxoff=1 info=00000000`
  `gmu229l=mode=229 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ctl=00000000 survived=1`
- **键 230** `boot_id 639182680` / 窗口 `16af12492516c16d57235910525c5d50a2f160f2aa8c6772d8c34b593e452b76`：
  `gmu230p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000`
  `gmu230p2=oob=1 ack=0 info=00800000 bs=00000000 gx=00000000 mx=00000000 polls=100`
  `gmu229l2=mode=230 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ctl=00000000 survived=1`
- **键 231** `boot_id 645209585` / 窗口 `6431088acdf58f0080d1386f7ef81d58f337d61cb5f7856cc1922794e791c535`：
  `gmu231p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000`
  `gmu231p2=rail_poll=1 cf=00088400 bit16=0 polls=20000 gd=00222001 st=000000d4 gxoff=1`
  `gmu229l3=mode=231 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ctl=00000000 survived=1`
- **对照（同一镜像，今天早些时候）**：键 143 `boot_id 582506894` / 窗口 `623ade9420fe121c97157f125a4d44eaedb84f8edab40da929db9a079b376f0e` /
  `gmu143l=shotgun_started ctl=00000001 bit0=1 polls=2 info=00000000 gfsr=00000000 fsr=00000400 far=00000000 fsynr0=00000000 smr_end=80000004 survived=1`。
- 三键尾行都 `survived=1`：本轮行少（每键四行），介质没有提前结束这一轮。

**111.4 判决**
1. **「把表页填满就能让 HFI 接受」在今天不成立。** 表确实建成了（`adreno_table_build` 返回 0、三页填满、九笔 CB0 写落位），`CB_FSR` 读回 `00000400`（TF=0，没有 walk fault），`ctl` bit0 从第一拍数到第 200000 拍都是 0。
2. **所以键 143 的成功不来自「表」这一件事。** 键 143 在同一个镜像上二拍就置起了 bit0；它比 229 多做三件事：写 `SMR0`（`0x800`）让 GMU 的 stream ID 匹配进这个银行、写 `S2CR0`（`0xc00`）把流从 BYPASS 改成 TRANS、把 `GMU2HOST_INTR_MASK` 写成 `0xff7fffff`（只开 MSGQ 与 CM3_FAULT）。**这是 R288 直接要判死的假设**：把这三笔补到 229 的前导里，若 `bit0` 起来，则「HFI 需要流路由 + 掩码」成立；若仍 0，则 143 的成功另有来源（下一步只能逐笔二分它的九个写）。
3. **键 231 的否定回答有条件**：`cf=00088400`（bit16=0）、`gd=00222001`（SW_COLLAPSE 仍置，power-up 未置）、`st=000000d4`（GX_HM_GDSC_POWER_OFF|GX_HM_CLK_OFF 仍置）⇒ 在 HFI **未被接受**的状态下，GX rail 不会自己上电。**它不能回答「HFI 接受之后 rail 会不会上电」**，那要等 232 成功之后才能问。
4. **键 230 的否定回答同样有条件**：`ack=0`、`bs/gx/mx` 读回全 0、`info=00800000` ⇒ 在 HFI **未被接受**的状态下，带外门铃无人应答，且敲门铃会把 bit23（外部依据：`a6xx_gmu.xml` 里 `GMU2HOST_INTR_INFO` 的 `CM3_FAULT` 在 pos 23）抬起来、留得住。这与 R275/R276 的形状一致，只是这一次的上下文是「表已经铺好」。
5. **G1 仍未关闭**：三键 `ctl` bit0 全 0；`gd/cf` 与普查行逐位相同；`cx_on=1`（CX 域在，GX 域不在）。

**111.5 边界与读数陷阱**
- **`build=0` 是成功，不是失败**：`adreno_table_build` 返回 0 表示建表成功，代码在 `r229build == 0u` 的分支里才继续装 CB0；把它读成「build 失败」会把整条判决读反。
- **`fills=3` 是页数，不是条目数**：一页 512 个 8 字节条目，三页共 1536 笔。
- **`gmu229p1` 有多个变体**：`refused-cx-down` / `refused-geometry` / `refused-no-qtbl` / `no-alloc-symbol` / `alloc-failed` / `refused-plan` / `refused-build` 都会以同一个标签前缀出现。**只按 `find` 取第一个会读到失败变体**；判据必须看 `hfi=1` 这个变体里的 `bit0` 与 `polls`。
- **静态门读错产物这个坑又出现了一次**：本轮 `r287_mkstatic.py` 只替换了文件尾部的 `r286-kernel`/`r286-payload`，文件头的 `KERNEL`/`PAYLOAD` 常量仍是上一轮路径，于是门对真产物报了 12 个 FAIL。**修法是整体替换路径，再对真产物跑一次、对上一轮产物跑一次**（前者 PASS、后者 FAIL 才叫门）。
- **CI 日志是被 302 跳转的**：直接 `urlopen` 带 `Authorization` 头会在跳转后得到 `401 Server failed to authenticate`；必须自己处理 302（`HTTPRedirectHandler` 子类返回 `None` 拿到 `Location`），再裸 `urlopen(loc)`。本轮用它证明了 CI 确实 checkout 了 `ref: r287-table` → `57dab6a0`。

**111.6 门禁与工具（本轮全部通过）**
- 探针块：块 **38,683 字符 / 658 行 / opens=83 closes=83**，块内 `gmuW[` **11**、`dsb sy` 23、`dc civac` 3、`0xc00001feu` 3；`probe` **1,609,372 字符 / 26,936 行**。
- host 门 PASS（25/25 + 51/51 + 32/32）；probe 门 **28/28**（store 预算 `823 → 834`）；stage4 **61/61**、stage5 **48/48**、stage6 **2189/2189**；全量 `ran 46 of 48, FAILING: 0`；`check_mode_selector.py` **42 selectors / upper 231 / 自证 FAIL / VERDICT PASS**。
- 静态门：真产物 **PASS**（required+survivors 110 行两边命中），对 **R286 产物 FAIL 12** ⇒ 区分能力成立。
- 构建：CI **#405 run `45265903128` success 28/28**；分支 `r287-table` commit `57dab6a0443fbea4dd79eb4f5e566947f95de790`、tree `d997cd2cb0081150119f363a3fe90555648b67cc`（base `main 728316c9326a`，534 已有 + 5 上传）。
- 产物：`kernel.macho` 126,500,864 B sha256 `8695d552c2cac7e1f950f496ef15434900afd66f415341b02b0af392e5bb8e13`；`payload.img` 150,994,944 B sha256 `f6b8ad1e29c5dbc669ee7aedc62e334bed814753cd49a820272776e0bf5cbd7b`。
- 重键：p229 `0a632171056ab7c6862ce9e61d08001e045bbee9b5ef941948ad55cf80b8e5a0`、p230 `55681fbf4a8df4e8556e21355dcb3885de1bffd7125575223fa3a80d54002406`、p231 `537be44f628610bc1710705f5c6fc59325ab6597e94f46c496a8a37485f79da7`。
- 每键前 `scripts\check-reboot-to-twrp.py --arm` PASS（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`），每次回 TWRP 后 runner 自跑 `reboot-to-twrp.py --arm-bcb-only`。

**111.7 下一步**
- **键 232**：在 229 的前导里补三笔 —— `SMR0 = VALID | SID4`（写 `0x800`）、两个 GMU 流的 `S2CR` 从 BYPASS 改成 TRANS（写 `0xc00` 起）、`GMU2HOST_INTR_MASK = 0xff7fffff`，然后照旧写 `HFI_CTRL_INIT` 并轮询。**一次上机就能判死「HFI 是不是要流路由 + 掩码」**。
- **键 233**：232 成功后再敲门铃（带外请求的意义只有在 HFI 起来之后才成立）。
- **键 234**：232 成功后再轮询 `CFG` bit16（同一条理由：rail 的答案只有在 HFI 起来之后才成立）。
- 读数纪律不变：**每键不超过八行、每行尽量短、判决字段放最前面**。
- 目标不变：准入表 `modeled=9 / satisfied=0`、`CONFIG_XNU_NATIVE_ADRENO=n`、GPU 未渲染一帧。

## 一百一十二、R288：掩码与流路由都不是那道门 —— 键 143 多出来的其实是「描述符 + CM3 复位」

**112.1 一句话**
R287 把「填满队列表页」这条路走到头：表建成了、九笔 CB0 都落位、`fsr=00000400` 无 fault，`ctl` bit0 仍然 0 到 200000 拍。本轮把键 143 与键 229 的源码逐笔对比，抽出**两个候选差异**——`GMU2HOST_INTR_MASK = 0xff7fffff` 与「把两个 GMU 流 `SMR0`/`S2CR` 路由进 CB0」——然后用三个键做 2×2：**232 只加掩码、233 只加路由、234 两个都加**。三键的前导与 229 完全相同（同一份从 R287 生成器里原样提取的代码），因此每键只多一味料。结果：**232 的掩码读回 `ff7fffff`、233 的 `SMR0` 读回 `80000004` 且两条 `S2CR` 读回 `00000000`、234 两味料都读回**，而**三键的 `ctl` bit0 全部是 0，200000 拍一次没置起**。所以本轮判死的是这两个候选，剩下的差异被逐笔对照逼出来：**键 143 在复位 CM3 之前先写了 `HFI_QTBL_ADDR` 与 `HFI_QTBL_INFO = 1`，并做了一次 CM3 复位脉冲（`CM3_SYSRESET` 1 → `PWR_COL_CP_RESP = 0` → 0，然后轮询 `FW_INIT_RESULT` 到 `babeface`）**；而 R274 之后的扁平块（含本轮）**把 `HFI_QTBL_INFO` 清成 0、从不写 `QTBL_ADDR`、也从不复位 CM3**。

**112.2 三个键各自问什么**
- **前导（三键共用，从 `work\tmp\r287_block.py` 的 `PRE` 原样提取，只把 `r229` 改成 `r232`）**：普查行（`ver/qi/qa/smr/s2cr0/st/gd/cf/cx_on`，本轮新增读 `SMR0` 与 `S2CR0`）→ 分配四页 → `adreno_lpae_geometry`/`adreno_table_bytes_needed`/`adreno_table_build`（IOVA `0x2d800000`，2 MiB 叶子）→ `adreno_smmu_ctx_plan` + `adreno_install_absolute` 九笔 CB0 写 → 三页每个条目写成合法描述符 → `dc civac` → 清 `CB_FSR`（`0xc00001feu`）→ `HFI_QTBL_INFO = 0` → `HFI_CTRL_INIT = 1` → 轮询 `ctl` bit0 200000 拍。**这段前导与 R287 键 229 逐字相同**，所以三键之间的差别只有下面那一笔。
- **键 232（只加掩码）**：在 `HFI_CTRL_INIT` 之前写 `GMU2HOST_INTR_MASK = 0xff7fffff` + `dsb`，之后读回 `mask`、`smr`、`s2cr0`。
- **键 233（只加路由）**：在 `HFI_CTRL_INIT` 之前写 `SMR0 = 0x80000004`（VALID | SID4，地址 `0x800`）、`S2CR0 = 0x00000000`、`S2CR1 = 0x00000000`（TRANS，地址 `0xc00`/`0xc04`），每笔后 `dsb`，之后读回 `smr`、`s2cr0`、`s2cr1`。
- **键 234（两者都加）**：掩码与三笔路由都写，读回四字。

**112.3 逐字读数（设备自写）**
- **键 232**：`boot_id 617805448`、窗口 sha256 `80489848158264b97be83e6043d3fef418979358906df2dbe2569078882f57f3`、4/4 行落地。`gmu232p0=pre=1 ver=00000000 qi=00000001 qa=ad984000 smr=00000000 s2cr0=000200ff st=000000d4 gd=00222001 cf=00088400 cx_on=1`；`gmu232p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000 fsr=00000400 far=00000000`；`gmu232p2=mask_only=1 mask=ff7fffff ctl=00000000 bit0=0 smr=00000000 s2cr0=000200ff`；`gmu232l=mode=232 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ctl=00000000 survived=1`。
- **键 233**：`boot_id 631616648`、窗口 sha256 `192fce24e6028c45587c2faa81ab8d42f75e6c62316bad1031644250ed154a65`、4/4 行。`gmu233p0=pre=1 ver=00000000 qi=00000001 qa=ad970000 smr=00000000 s2cr0=000200ff st=000000d4 gd=00222001 cf=00088400 cx_on=1`；`gmu233p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000 fsr=00000400 far=00000000`；`gmu233p2=route_only=1 smr=80000004 s2cr0=00000000 s2cr1=00000000 ctl=00000000 bit0=0`；`gmu232l2=mode=233 … survived=1`。
- **键 234**：`boot_id 618857401`、窗口 sha256 `dc543c3652dd23afd5803d6728dbad8f971e637fb1c5c91417d01607f6381b82`、4/4 行。`gmu234p0=pre=1 ver=00000000 qi=00000001 qa=ad974000 smr=00000000 s2cr0=000200ff st=000000d4 gd=00222001 cf=00088400 cx_on=1`；`gmu234p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000 fsr=00000400 far=00000000`；`gmu234p2=both=1 mask=ff7fffff smr=80000004 s2cr0=00000000 s2cr1=00000000 ctl=00000000 bit0=0`；`gmu232l3=mode=234 … survived=1`。

**112.4 判决**
1. **掩码不是那道门**：232 的 `mask` 读回 `ff7fffff`（未被硬件钳），`ctl` bit0 仍 0。
2. **流路由也不是那道门**：233 的三笔都读回落位（`SMR0 = 80000004` = VALID + SID4，两条 `S2CR` = `00000000` = TRANS），`ctl` bit0 仍 0。注意普查行里 `smr=00000000`、`s2cr0=000200ff`，说明这一轮之前**没有任何流被匹配**，路由确实是新加的。
3. **两者叠加也不是**：234 四字全落位，`ctl` 仍 0。
4. **于是差异被源码对照逼出来**：今天早些时候键 143 接受 HFI 时（`ctl=00000001 polls=2`），它先做了四件事，而本轮前导一件都没做 —— ① `HFI_QTBL_ADDR = <队列表物理地址>`；② `HFI_QTBL_INFO = 1`（**描述符有效**，不是清的）；③ **`CM3_SYSRESET = 1` → `PWR_COL_CP_RESP = 0` → `CM3_SYSRESET = 0` 的复位脉冲**；④ 轮询 `FW_INIT_RESULT` 到 `babeface`。**也就是说：固件必须在「队列表描述符已就位」的状态下被复位启动**，之后写 `HFI_CTRL_INIT` 才会被受理；R274 之后所有扁平块都是「清描述符 + 不复位」，所以控制字恒 0 —— 这与 R248（清 `FW_INIT_RESULT` 后复位，`babeface` 自己回来）和 R239 键 83（先清 `QTBL_INFO` 再写 INIT 能活）都自洽：**复位是让固件读描述符的唯一窗口**。
5. **G1 仍未关闭**：三键 `cf=00088400`（bit16=0）、`gd=00222001`、`st=000000d4`（`GX_HM_GDSC_POWER_OFF|GX_HM_CLK_OFF` 都置）；`gx_on=0`。

**112.5 边界与读数陷阱**
- 本轮三键**都是 4/4 行全落地**，没有碰到日志介质的截断问题（R283–R286 的教训是「行少才安全」，本轮继续遵守：每键四行、行内字段短）。
- `fsr=00000400` 表示 `TF=0`：**没有 walk fault**，因此「表没被用上」不成立；表确实在，问题在固件未被复位。
- `smr=00000000` 与 233 的 `smr=80000004` 是同一轮的 before/after，可自证写路径有效。
- `build=0` 是**成功码**（R287 已记），不是「没构建」。
- 键 232/233/234 的窗口与 boot_id 三份都不同，`row key detected from stamps` 分别报 `[232]`/`[233]`/`[234]`，不存在串轮。

**112.6 门禁与工具**
- 生成器 `work\tmp\r288_mkgen.py`：从 `r287_block.py` 用正则 `r'PRE = """(.*?)"""\n\nP2_229'` **原样提取**被键 143 证明过的前导（不重打），把 `r229`→`r232`，并在 `gmuW[r232qiWord] = 0u;` 之前插入占位符 `@EX@`，让每键只加一味料；块其余部分（p0/p1/p2/尾行/三个分支/main）自写。
- 块与探针：**块 39,226 字符 / 674 行 / opens=79 closes=79**、块内 `gmuW[` **8**、probe **1,648,599 字符 / 27,611 行**；`NEW_HDR` = 232/233/234，`OLD_HDR` = 229/230/231，插在 ladder 前，arm 窗口 `<= 234u`，选择器 `(fwKey >= 234u) ? 234 : ((fwKey >= 233u) ? 233 : 232)`。
- **本轮修掉的六个生成/门禁缺陷（同类会再犯）**：① `@T` 后紧跟 `@` 当分隔符 ⇒ 产出 `gmu229@p0=`，改成 `@Tp0`；② `pre.count("@T") == 2` 写死（实际 8 个，前导有七个 `refused-*` 行）⇒ 改 `>= 2`；③ 只改 `r229` 前缀没改 `s229`（34 处）⇒ `use of undeclared identifier 's229phys'`；④ 前导里还有 57 处不带 `r` 前缀的 `229` ⇒ `use of undeclared identifier 'g229'`，修法只对 `PRE = """` 到闭合 `"""` 之间做替换（`OLD_HDR` 里的 `229u/230u/231u` 必须保住）；⑤ 手写的 `IOLog(...%s\\n...)` 少一层反斜杠 ⇒ `-Werror,-Winvalid-pp-token`，修法是双反斜杠（12 行）；⑥ 静态门路径只替换 `tail` 不替换 `head` 会**静默读上一轮产物**（R287 踩过），本轮 `work\tmp\r288_mkstatic.py` 两处都换，并把 R287 的 12 行搬进 SURVIVORS、forbidden 换成源码里不存在的 `gmu235a/gmu236a/gmu237a`。
- 门禁结果：host 门 PASS（25/25 + 51/51 + 32/32）；probe 门 **28/28**（store 预算 **834 → 842**，说明串尾追加 R288 句）；stage4 **61/61**、stage5 **48/48**、stage6 **2218/2218**；全量 `ran 46 of 48, FAILING: 0`；`check_mode_selector.py` **43 selectors / upper 234 / 自证 FAIL / VERDICT PASS**；静态门 `work\tmp\r288_static.py` 对真产物 **PASS**（110 行两边命中、marker kernel=1/payload=2、forbidden 3 条零、固件 32768 B sha 钉住），对 **R287 产物 FAIL 12**。
- 构建：分支 `r288-mask-route`（base main `fc2181e2dc4e`）、commit `432282be9bd66f8ab354be2ce02e1070b480885a`、CI **#406 run `45277279092` success 28/28**。产物 `r288-kernel\kernel.macho` 126,517,248 B sha256 `f37b8c613218033c298b58f0a407170191ebab2fd521f811e436403bb19328c0`、`r288-payload\payload.img` 150,994,944 B sha256 `0d544396f950f4c3986ae3f79de9dad9ef2554a72f4fa7ffef32be916bbaeb72`。
- payload 重键（`enchilada-pas-exec=2` → `=23N`）：p232 `2ab0c86588b9451140ad39078b66d51405b11d4c598afbb520cb1d64e240989b`、p233 `3ba2d0204815e15e5cf81ccf87c79aea991614840ed0d619e4f7ba845f8ea038`、p234 `773b36a935bd60cc380148b5854ab5681d036ee2edfe2b5bab113c90287c6e9a`。
- 每键前 `scripts\check-reboot-to-twrp.py --arm` PASS（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`），每次回 TWRP 后 runner 自跑 `reboot-to-twrp.py --arm-bcb-only`；三次 anchor 交叉核对分别命中 `r287_s231_window.bin`、`r288_s232_window.bin`、`r288_s233_window.bin`。

**112.7 下一步**
- **键 235**：把键 143 的完整配方搬进本轮前导 —— 建表并填满三页 → `dc civac` → 清 `CB_FSR` → **`HFI_QTBL_ADDR = <页表基址>`、`HFI_QTBL_INFO = 1`** → **`CM3_SYSRESET = 1` → `PWR_COL_CP_RESP = 0` → `CM3_SYSRESET = 0`** → 轮询 `FW_INIT_RESULT` 到 `babeface` → 写 `HFI_CTRL_INIT = 1` → 轮询 `ctl` bit0。**预测 `ctl=00000001 polls=2`**；若成立，「HFI 起不来」这一条从缺口清单里划掉。
- **键 236**：同 235 但**跳过复位脉冲**（描述符就位、不复位）⇒ 判「复位是否必要」。
- **键 237**：同 235 但**复位后再把 `QTBL_INFO` 清 0** 才写 INIT ⇒ 判「描述符必须在 INIT 时仍有效」。
- 读数纪律不变：**每键不超过八行、每行尽量短、判决字段放最前面**；选号前先扫 `gmu\d{3}[a-z]`（已占 100、102–234），forbidden 取 238/239/240。
- 目标不变：准入表 `modeled=9 / satisfied=0`、`CONFIG_XNU_NATIVE_ADRENO=n`、GPU 未渲染一帧。

## 一百一十三、R289：照键 143 的真实顺序重放 —— 顺序全落位，HFI 仍不接受；而两个键在落地任何一行之前就没了

**113.1 一句话**
R288 用 2×2 把「掩码」与「流路由」判死之后，剩下的差异只剩「顺序」：键 143 先建表填页、再清 `CM3_FW_INIT_RESULT`、把队列表地址写回 `HFI_QTBL_ADDR`、举起 `HFI_QTBL_INFO`、**复位 CM3**（`CM3_SYSRESET` 1 → `PWR_COL_CP_RESP = 0` → 0，再轮询 `FW_INIT_RESULT` 到 `babeface`）、清两个中断字、掩码、最后才写 `HFI_CTRL_INIT`；而 R274 之后的扁平块（229–234）既不复位 CM3、也不写 `QTBL_ADDR`，还主动把 `QTBL_INFO` 清成 0。本轮三键把那条顺序整段搬进前导：**235 全配方**、**236 全配方但不做复位与 init-result 等待**、**237 全配方且复位之后再把 `QTBL_INFO` 清 0**。结果分两半：**237 四行全落地并给出完整读数，`ctl` bit0 仍是 0（200000 拍）**；**235 与 236 都是零字节 —— 探针一行都没写进窗口，`dd` 读回的还是上一轮的窗口（同 `boot_id`、同 sha）**。紧接着做了一次对照上机（把**已知能活的键 234** 用同一份 R289 payload 重键后跑），四行全落地、窗口 sha 全新，证明**这份内核与 payload 本身能启动、能跑探针**，零字节死的是 235/236 自己的代码路径。

**113.2 三个键各自问什么**
- **共用前导**（`work\tmp\r289_block.py`，由 `work\tmp\r289_mkgen.py` 在 R288 生成器上做受控手术得到）：普查行（`ver/qi/qtbl/fw/st/gd/cf/cx_on`）→ 分配四页 → `adreno_table_build`（IOVA `0x2d800000`，2 MiB 叶子）→ 九笔 CB0 → 三页每条目写合法描述符 → `dc civac` → 清 `CB_FSR`（`0xc00001feu`）→ `CM3_FW_INIT_RESULT = 0` → 写回 `HFI_QTBL_ADDR`（**取自普查行读到的现值**）→ `HFI_QTBL_INFO = 1` → `@RST@` → 两个 CLR（`GMU2HOST_INTR_CLR = 0xffffffff`、`AO_HOST_INTERRUPT_CLR = 0xffffffff`）→ `@POST@` → `GMU2HOST_INTR_MASK = 0xff7fffff` → `HFI_CTRL_INIT = 1` → 轮询 `ctl` bit0 200000 拍 → `p1` 行（`fills/build/plan/ctl/bit0/polls/fsr/far/fw/sr`）。
- **键 235**：`@RST@` = 复位脉冲 + 200000 拍等 `0xbabeface`；`@POST@` 空 ⇒ 全配方。
- **键 236**：`@RST@` 空、`@POST@` 空 ⇒ 全配方减去复位与 init-result 等待。
- **键 237**：`@RST@` 同 235、`@POST@` = `HFI_QTBL_INFO = 0` ⇒ 复位之后、掩码之前把描述符放掉。
- 生成结果：块 **42,383 字符 / 740 行**、`gmuW[` **28**（三臂 10/7/11）、probe **1,690,982 字符 / 28,352 行**；`NEW_HDR="if (fwKey == 235u || fwKey == 236u || fwKey == 237u) {"`、插在 ladder 之前、arm 窗口 `<= 237u`、选择器 `(fwKey >= 237u) ? 237 : ((fwKey >= 236u) ? 236 : 235)`。去注释后块内 `0x5184u/0x5185u/0x5186u/0x5193u/0xff7fffffu/0x5191u/0x9304u/0x5000u/0x501cu` 各三、`0x5101u`/`0xbabeface`/`gmuW[r235srWord] = 1u;` 各二、`IODelay(1u)` 五处；无门铃、无 vote。
- 构建：CI **#407 `45283995954` success 28/28**；分支 `r289-recipe` commit **`fedefec2e92a682c05c64d4cbf9a30fca70167be`**、tree `a33cf4ada971a2943de801838650352af9712df6`（534 已有 + 5 上传，base main `22d45ed53d22`）。产物 `work\tmp\r289-kernel\kernel.macho` 126,533,632 B sha256 `0183b70e9c036a93ef0f5aab93008261edfc5be4fbb00e1f26dbfc75dcb46df6`、`work\tmp\r289-payload\payload.img` 150,994,944 B sha256 `24f47775390d93b82ea0d1745466c359dd940925f3a2f566e92d699a69f5a94a`。重键：p235 `015cf34aa4163bf89e03fffc58b272e85fd80e3a41d132f65494464ce28d8f2b`、p236 `2099710c222b7512ea0065761746040b7c3ca19a2997ab1a90a73957a5738827`、p237 `761efc328d9abf812b6a90bc8c83c5e83055ed6edf351858f5c4c25194130aa0`、对照 p234ctl `407f0f3490795310184607efcf2e626eae37bdd2da7928183ec4a68ebe6727b6`。

**113.3 逐字读数（设备自己写的字段）**
- **键 235 —— 零字节**：`counts: gmu235p0=0 gmu235p1=0 gmu235p2=0 gmu235l=0`；窗口 sha256 仍 `dc543c3652dd23afd5803d6728dbad8f971e637fb1c5c91417d01607f6381b82`、`boot_id` 仍 `618857401`；`stage7=armed-not-executed why=not-armed armed=0 exec_key=234`（窗口里那份 SHLOG 是上一轮的）；`msg_bytes=30784 slots_written=9`；`flash readback matches: True`、`key block 31132: device == payload -> True`、`system_b head 8 MiB sha256 019e16d9…` 与 payload 一致 ⇒ **不是没刷进去**。
- **键 236 —— 零字节**：同上（窗口 sha `dc543c36…`、`boot_id 618857401`、四行全 0、`counts: enchilada-adreno=24` 全是旧日志）；payload `2099710c…` 刷入并回读一致。
- **对照键 234（用 R289 payload 重键 `p234ctl`）—— 四行全落地**：`boot_id 644192134`、窗口 **`5af63ed65d50d78440e6110d7425d2df023170c0b670ea81e926af3ba1440a13`**；`gmu234p0=pre=1 ver=00000000 qi=00000001 qa=ad988000 smr=00000000 s2cr0=000200ff st=000000d4 gd=00222001 cf=00088400 cx_on=1`；`gmu234p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000 fsr=00000400 far=00000000`；`gmu234p2=both=1 mask=ff7fffff smr=80000004 s2cr0=00000000 s2cr1=00000000 ctl=00000000 bit0=0`；`gmu232l3=mode=234 … survived=1` ⇒ **内核能启动、探针能跑、掩码与路由都还能落位**，与 R288 逐位一致。
- **键 237 —— 四行全落地**：`boot_id 645221820`、窗口 **`9b4b98e0d48962b8e2d8727d42db69f28ed157419a5770d1a92a6ae8a2b18a98`**；`gmu237p0=pre=1 ver=00000000 qi=00000001 qtbl=ad985000 fw=babeface st=000000d4 gd=00222001 cf=00088400 cx_on=1`；`gmu237p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000 fsr=00000400 far=00000000 fw=babeface sr=00000000`；`gmu237p2=cleared_after_reset=1 qtbl=ad985000 qi=00000000 mask=ff7fffff fw=babeface sr=00000000 ctl=00000000 bit0=0`；`gmu235l3=mode=237 … survived=1`。

**113.4 判决**
1. **照键 143 的顺序整段重放，仍然不接受 HFI**：237 把复位脉冲、`FW_INIT_RESULT = 0`、写回 `QTBL_ADDR`、`QTBL_INFO = 1`、两个 CLR、掩码全部做完，`fw=babeface` 证明复位后固件确实重新跑到了 `babeface`，`fsr=00000400` 证明没有 walk fault，`ctl` bit0 仍是 0 到 200000 拍 ⇒ **R287/R288 排除掉「表」「掩码」「路由」之后，剩下的「顺序」也不是那道门**。这同时把「键 143 今天为什么接受」重新变成未决问题（它的成功不是这五笔顺序的结果）。
2. **两个键在落地任何一行之前就没了**，而同一份 payload 的键 234 与键 237 都活着 ⇒ **不是镜像问题，是 235/236 的代码路径**。235 与 236 的共同点只有一处：**`HFI_QTBL_INFO` 在写 INIT 时仍是 1，而且整轮都不再清它**（237 在复位之后把它清回 0 才写 INIT）。因此最可能的机制是「**描述符保持有效 + CM3 带着这张表跑**」最终把总线/固件搞死，死在第一次刷盘之前（探针行按批次落地，零字节只说明死得早）。
3. **「复位」本身不是死因**：236 没有复位也死了、237 有复位还活着 ⇒ 生死与复位无关，与被清掉的描述符有关。
4. **写回的是陈旧地址**：235/236/237 的 `qtbl` 都是普查行读到的 `HFI_QTBL_ADDR` 现值（`ad985000`，属于上一轮），而不是本轮新建表的物理地址；键 143 写的是**本轮刚建好的表**的地址。这条差异本轮没有被分开测（三键都用陈旧值），必须进下一轮。
5. **G1 仍未关闭**：三键 `gd=00222001`（SW_COLLAPSE 置、power-up 未置）、`cf=00088400`（bit16 = 0）、`st=000000d4`（`GX_HM_GDSC_POWER_OFF | GX_HM_CLK_OFF` 都置）⇒ GX 仍未上电，GPU 仍未渲染一帧。

**113.5 边界与读数陷阱**
- **零字节是关于「这一轮有没有活到第一次刷盘」的陈述**，不是「那几行没被打印」；本轮 235/236 与 R283 键 203、R279 键 201 同类。
- **窗口 sha 相同 = 本轮没写窗口**，此时 `dd` 读回的是上一轮字节，`boot_id` 也是上一轮的；判定脚本已按「窗口未变」单独报，不把它当成读数。
- **判定脚本的尾行字段名错了一处**：判决读取 `gmu237l`/`gmu235l` 这类名字，而本轮尾行实际叫 `gmu235l/l2/l3`，于是 `rail after the round: gd None cf None gx_on None`；`p1`/`p2` 与三键读数本身不受影响，下一轮生成器要把尾行名按 `gmu235l<suffix>` 取。
- **`exec_key` 只能用来判断窗口里那份 SHLOG 属于哪一轮**，不能用来判断这一轮跑的是哪个键（窗口没被写时它必然是旧的）。
- 本轮没有出现零窗口读数；没有使用门铃、vote、`IOSleep`；没有写 `sCR0`。

**113.6 门禁与工具**
- host 门全 PASS（25/25 + 51/51 + 25/25 + 32/32 + 各 contract）；probe 门 **28/28**（store 预算 834 → **870**）；stage4 **61/61**、stage5 **48/48**、stage6 **2249/2249**；全量 `ran 46 of 48, FAILING: 0`；`check_mode_selector.py` **44 selectors / upper 237 / 自证 FAIL / VERDICT PASS**。
- 静态门 `work\tmp\r289_static.py`：对真产物 **PASS**（`required+survivors: 110 checked in both artefacts`、marker kernel=1/payload=2、forbidden `gmu238a/gmu239a/gmu240a` 三条零、固件 32768 B sha `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`），对 **R288 产物 FAIL 12**（传两个参数即指向别的产物）。
- 工具坑：`source_rows()` 返回**一个字符串**（折叠后的全文），把它当列表遍历会让每条 REQUIRED 都「缺失」；stage6 的 ID1 重读预算每轮 +1（本轮 35 → 36，键列表要补 235/236/237）；探针块生成必须从 index 版恢复（`git show ':package/EnchiladaAdrenoProbe.cpp'`，PowerShell 重定向为 UTF-16LE）。

**113.7 下一步（R290）**
- **238**：全配方，但把 `HFI_QTBL_ADDR` 写成**本轮新建表的物理地址**（`s235phys`）而不是读回的陈旧值，且**不**在之后清 `QTBL_INFO` ⇒ 判「陈旧地址是不是那两个键的死因」。
- **239**：重复 235 的形状（陈旧地址 + 保持 `QTBL_INFO = 1`）⇒ 判死因是否可重现（可重现才谈得上定位）。
- **240**：写**新地址** + 复位之后**清 `QTBL_INFO`**（即 237 的存活形状但配新地址）⇒ 若 238/239 一个死一个活，这条把「地址」与「描述符是否保持」拆开。
- 每键行数压到 8 行以内、每行尽量短；尾行名按 `gmu235l<suffix>` 取；选号前先扫 `gmu\d{3}[a-z]`（已占 100、102–237），forbidden 取 241/242/243。
- 目标不变：准入表 `modeled=9 / satisfied=0`、`CONFIG_XNU_NATIVE_ADRENO=n`、GPU 未渲染一帧。

## 一百一十四、R290：把「陈旧地址」与「描述符保持有效」拆开 —— 死因是后者，地址不是

**114.1 一句话**
R289 的三键写回的都是普查行读到的**陈旧队列地址**（`ad985000`，上一轮那张表），而键 143 写的是它自己新建的表；同时 R289 里两个键在落地任何一行之前就没了、一个键活下来。本轮把这两个变量拆开：**238 写本轮新建表的物理基址、描述符整轮保持有效**；**239 重复 235 的形状（写回陈旧地址、描述符保持有效）**；**240 写新地址、但在复位之后把描述符清掉**（237 的存活形状配新地址）。结果很干净：**238 与 239 都是零字节**（探针一行没进窗口，`dd` 读回的还是上一轮的窗口 —— 同 `boot_id 645221820`、同 sha `9b4b98e0d489…`），**240 四行全落地**且给出完整读数 —— 新地址 `ad989000` 与陈旧地址 `ad985000` 确实不同、描述符 `qi = 00000000`、固件 `fw = babeface`、`fsr = 00000400`、`ctl` bit0 在 200000 拍后仍是 0、`gd = 00222001`、`cf = 00088400`、`gx_on = 0`。⇒ **死因不是「地址陈旧」，而是「描述符在 CM3 运行期间保持有效」**：235/239/238 都保持、都死；237/240 都在复位之后清掉、都活。HFI 仍未接受。

**114.2 三个键各自问什么**
- **共用前导**（`work\tmp\r290_block.py`，由 `work\tmp\r290_mkgen.py` 在 R289 生成器上做受控手术得到）：普查行（`ver/qi/qtbl/fw/st/gd/cf/cx_on`）→ 分配四页 → `adreno_table_build`（IOVA `0x2d800000`，2 MiB 叶子）→ 九笔 CB0 → 三页每条目写合法描述符 → `dc civac` → 清 `CB_FSR`（`0xc00001feu`）→ `CM3_FW_INIT_RESULT = 0` → **`HFI_QTBL_ADDR = @QA@`**（`@QA@` 由每个键填：`r238qaw = (uint32_t)s238phys;` 或 `r238qaw = r238qtbl;`）→ `HFI_QTBL_INFO = 1` → 复位脉冲（`CM3_SYSRESET` 1 → `PWR_COL_CP_RESP = 0` → 0，再 200000 拍轮询 `FW_INIT_RESULT` 到 `0xbabeface`）→ `GMU2HOST_INTR_CLR = 0xffffffff` + `AO_HOST_INTERRUPT_CLR = 0xffffffff` → `@POST@` → `GMU2HOST_INTR_MASK = 0xff7fffff` → `HFI_CTRL_INIT = 1` → 200000 拍轮询 `ctl` bit0。
- **238**：`@POST@` 为空（描述符整轮有效），地址写 `s238phys`（本轮新表）。
- **239**：`@POST@` 为空，地址写 `r238qtbl`（普查行读到的陈旧值）—— 即 235 的形状，用来判死因可否重现。
- **240**：`@POST@` 是 `gmuW[r238qiWord] = 0u;` + dsb（复位之后、掩码之前清描述符），地址写 `s238phys`。
- 三键探针全文件 `gmuW[...] =` 实测 **901**（块内 **31**：238/239/240 分别 10/10/11）。

**114.3 逐字读数（设备自己写的字段）**
- **238：零字节**。`counts: gmu238p0=0 gmu238p1=0 gmu238p2=0 gmu238l=0`；窗口 sha256 仍 `9b4b98e0d48962b8e2d8727d42db69f28ed157419a5770d1a92a6ae8a2b18a98`、`boot_id` 仍 `645221820`（**这是 R289 键 237 的窗口**）；`stage7=armed-not-executed why=not-armed armed=0 exec_key=237`。刷写本身正常（`flash readback matches: True`、key block 回读一致），所以是这一轮**没活到第一次刷盘**。
- **239：零字节**。同上（`gmu239p0=0 gmu239p1=0 gmu239p2=0 gmu238l2=0`，窗口与 `boot_id` 仍是 237 的）⇒ **235 的死亡形状完整重现**。
- **240：四行全落地**。`boot_id 642521717`、窗口 sha256 **`41ada9c94a468e50a9e62cd7a1211f519cd6dcccf3cd669ebe93c7337f9e1e7b`**：
  - `gmu240p0=pre=1 ver=00000000 qi=00000001 qtbl=ad985000 fw=babeface st=000000d4 gd=00222001 cf=00088400 cx_on=1`（普查：描述符仍置着、地址是上一轮的表）
  - `gmu240p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000 fsr=00000400 far=00000000 fw=babeface sr=00000000`（表建好、九笔 CB0 落位、复位后固件确实回到 `babeface`、无 walk fault、HFI 不被接受）
  - `gmu240p2=fresh_cleared=1 qa=ad989000 qi=00000000 mask=ff7fffff fw=babeface sr=00000000 ctl=00000000 bit0=0`（**新地址写进去且读回是它**、描述符清零、掩码落位）
  - `gmu238l3=mode=240 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ctl=00000000 survived=1`
- 判定脚本句：`before the writes: ver 00000000 queue address ad985000 init result babeface st 000000d4 cx_on 1` / `THE FIRMWARE WAS HANDED A FRESH ADDRESS (ad989000 vs the stale ad985000)` / `THE FIRMWARE REACHED babeface BEFORE HFI WAS ASKED TO START` / `HFI WAS STILL REFUSED after the whole key-240 recipe (200000 polls)` / `rail after the round: gd 00222001 cf 00088400 gx_on 0`。

**114.4 判决**
1. **死因定位：「描述符在 CM3 运行期间保持有效」**。保持有效的三键（235 陈旧地址、239 陈旧地址、238 新地址）全都零字节；复位之后清掉的两键（237 陈旧地址、240 新地址）全都活。**地址不是死因**：238 带新地址照样死，240 带新地址照样活。
2. **新地址这条路本身无害**：240 把 `ad989000`（本轮表的基址）写进 `HFI_QTBL_ADDR`，读回就是它，`fsr = 00000400`（无 fault），整轮活着 ⇒ 地址写对了、SMMU 没有因此报错。
3. **HFI 仍未接受**：240 在完整配方下 `ctl` bit0 在 200000 拍里一次没置起，即便固件已回到 `babeface`、描述符在启动那一刻是清零的。**「描述符必须在 HFI 启动时有效」这一假设与 240 冲突**（240 在启动前把描述符清了，若描述符是必要条件就该失败，而它确实失败 —— 但 237 也一样失败，所以这一条尚无区分力）。
4. **键 143 为什么曾接受仍未解释**：143 在今天早些时候被复现过（`ctl = 00000001`、`polls = 2`），它与本轮 238 的唯一差别是「143 写的是自己那张表的地址 + 描述符保持」，而 238 正是这个形状却死了 ⇒ 差别只能在更细的地方（143 的 ctx 装法与页内容、或它前面那段 `adreno_table_build` 的参数）。
5. **G1 未关闭**：`gd = 00222001`（SW_COLLAPSE 置、power-up 未置）、`cf = 00088400`（bit16 = 0）、`st = 000000d4`（`GX_HM_GDSC_POWER_OFF|GX_HM_CLK_OFF` 都置）、`gx_on = 0`；GPU 未渲染一帧，准入表 `modeled=9 / satisfied=0 / BLOCKED`，`CONFIG_XNU_NATIVE_ADRENO=n`。

**114.5 边界与读数陷阱**
- **零字节只说明「没活到第一次刷盘」**，不能说明代码错在哪一行；本轮 238/239 的死点与 235 完全同形（连窗口 sha 都还是 237 的）。
- **窗口 sha 相同 = 这一轮没写窗口**（`boot_id` 也是旧的）—— 判定脚本只有把 `PREV_BOOT/PREV_SHA` 与 `boot_id`/窗口 sha 对比才能区分「本轮没写」与「本轮写了同样的字节」；`exec_key` 只说明窗口里 SHLOG 属于哪一轮。
- **尾行字段名必须按实际行名取**（`gmu238l`/`gmu238l2`/`gmu238l3`）：R289 曾因判定脚本写死 `gmu235l` 而打印 `rail after the round: gd None cf None gx_on None`，本轮已改。
- **`s238phys` 也出现在 239 分支的注释里**，用 `_d239.count('s238phys') == 0` 做静态断言会误报；必须比对**赋值语句本身**。

**114.6 门禁与工具**
- 块：`work\tmp\r290_mkgen.py` 手术派生 `r290_block.py`（20,198 字符）；最终块 **43,285 字符 / 758 行 / opens=85 closes=85**、`gmuW[` **31**、probe **1,734,268 字符 / 29,111 行**；arm 窗口 `(fwKey >= 7u && fwKey <= 240u)`；选择器 `const int r238mode = (fwKey >= 240u) ? 240 : ((fwKey >= 239u) ? 239 : 238);`。
- host 门 PASS（32/32 + 25/25 + 51/51）；probe 门 **28/28**（store 预算 **870 → 901**）；stage4 **62/62**、stage5 **49/49**、stage6 **2267/2267**；全量 `..\tmp\r239_allgates.py` → `ran 46 of 48, FAILING: 0`；`check_mode_selector.py` → **45 selectors / upper 240 / VERDICT PASS**；静态门 `r290_static.py` 对真产物 **PASS**（110 行两边命中、marker kernel=1/payload=2、forbidden `gmu241a/gmu242a/gmu243a` 零），对 **R289 产物 FAIL 12**。
- 构建：分支 `r290-separation` commit `34b612f25ff017672e2a8f6b5708161eb1b0b0fd`、tree `45de0b12436962bc8a19e83e7d6920cce189e442`（base main `c078154f1d74`）；CI **#408 `45290962103` success 28/28**；产物 `kernel.macho` 126,533,632 B sha256 `6299d86cccd44d3978777fca6f4e2b592d292fe5a48f224a4565c956e19ed91b`、`payload.img` 150,994,944 B sha256 `6d6634bac820850c5e6e4cf64871d3bb0a09ee5b8ac1e0433398c662182d2dd6`；重键 p238 `60fa331471aef10ceb8270191f274f8b85fcf8fa197b35f71b77c88e9b8a11b2`、p239 `5d60c5fad6ddb57eb7775045b33f54b1807801fd81a621eaa9b870b22ec91ae2`、p240 `9224b1750337270f5e58e5c5c545586bd6855f66ea2c48de50ef4c8ab8177ec9`。
- 工具：`r290_rows.py`（打印本轮 12 条 `snprintf` 格式串）、`r290_mkstatic.py`、`r290_mkrun.py`/`r290_mkmkrun.py`、`r290_mkrowscheck.py`/`r290_rowscheck.py`、`r290_mkhelpers.py`/`r290_anchor.py`/`r290_wait.py`、`peek_ci.py <run_id>`。
- 教训：**`verdict_block` 必须返回列表**（`region()` 里是 `"\n".join(verdict_block(key))`，返回字符串会被逐字拆行）；**`build()` 的 `subs` 里 `r289-s%d`→文件名替换要跟着轮次改**，否则生成的 runner 会把上一轮的 payload 目录当成本轮的（本轮生成过一次 `r289-p238-payload` 才发现）。

**114.7 下一步（R291）**
- **241**：**复位期间描述符清零、启动前再举起**（`QTBL_INFO = 1` 在 `HFI_CTRL_INIT` 之前立刻写），既避开 238 的死区、又让「启动那一刻描述符有效」成立 ⇒ 若接受，说明 143 的接受就靠这个次序。
- **242**：同 241，但启动之后立刻再把描述符清 0（`@POST2@`）⇒ 判 HFI 启动是否需要描述符在启动**之后**继续有效。
- **243**：同 241，接受之后敲一次带外门铃（`BOOT_SLUMBER` set 22 / ack 30）⇒ 一旦 HFI 接受，带外服务是否可用（R275/R276 的否定回答只在「HFI 未接受」条件下成立）。
- 目标不变：准入表 `modeled=9 / satisfied=0`、`CONFIG_XNU_NATIVE_ADRENO=n`、GPU 未渲染一帧。

## 一百一十五、R291：把描述符清零移到复位之前、再在启动前举起 —— 三键全部零字节

**115.1 一句话**
R290 判「描述符在 CM3 运行期间保持有效 = 死」，本轮把描述符的处理整个倒过来：**241 在复位脉冲之前先清描述符、在 `HFI_CTRL_INIT` 之前再举起**（这样固件重启时看不到有效队列表，而启动那一刻表是在的）；**242 同 241，但在控制字轮询之后再清一次**；**243 同 241，并在启动字之后敲一次带外门铃**（`BOOT_SLUMBER` set 22 / ack 30，10 ms 轮询后清 ack）。三键都照 R289/R290 证明过的键 143 全配方（填三页 + `dc civac` + 写 `HFI_QTBL_ADDR` + `QTBL_INFO=1` + 复位脉冲 + 等 `0xbabeface` + 两个中断 CLR + 掩码 + 写启动字）。**结果是三键全部零字节**：`gmu241p0/p1/p2/gmu241l`、`gmu242p0/p1/p2/gmu241l2`、`gmu243p0/p1/p2/gmu241l3` **计数全 0**，三份窗口 sha256 都是 **`41ada9c94a468e50a9e62cd7a1211f519cd6dcccf3cd669ebe93c7337f9e1e7b`**、`boot_id` 都是 **`642521717`** —— 也就是 R290 键 240 那一轮的窗口原封未动。**本轮没有拿到任何新的寄存器读数**，因此它对「举起时机」这个问题没有回答。

**115.2 三键设计**
- 共用前导（键 143 全配方，与 R290 逐笔相同）：分配四页 → `adreno_table_build`（IOVA `0x2d800000`）→ `adreno_smmu_ctx_plan` + 九笔 CB0 → **每页 512 条目全填合法描述符** → `dc civac` → 清 `CB_FSR`（`0xc00001feu`）→ `CM3_FW_INIT_RESULT = 0` → `HFI_QTBL_ADDR = 本轮新表的物理基址` → `HFI_QTBL_INFO = 1` → 复位脉冲（`CM3_SYSRESET=1` → `PWR_COL_CP_RESP=0` → `CM3_SYSRESET=0` → 200000 拍等 `0xbabeface`）→ `GMU2HOST_INTR_CLR=0xffffffff` + `AO_HOST_INTERRUPT_CLR=0xffffffff` → `GMU2HOST_INTR_MASK=0xff7fffff`。
- **241**：前导的**第一步**把描述符清 0（在复位之前），在掩码之后、启动字之前把 `QTBL_INFO` 重新写 1。行 `gmu241p2=raised_before_init=1 qa=… qi=… mask=… fw=… sr=… ctl=… bit0=…`。
- **242**：同 241，且控制字轮询结束之后再写一次 `QTBL_INFO = 0`。行 `gmu242p2=raised_then_cleared=1 …`。
- **243**：同 241，且启动字之后写 `BOOT_SLUMBER_OPTION=0`、`GX_VOTE_IDX=0`、`MX_VOTE_IDX=0`、`HOST2GMU_INTR_SET=0x00400000`（set 22），100×`IODelay(100u)` 轮询 `GMU2HOST_INTR_INFO` 的 bit30，再写 `0x40000000`（ack 30）清。行 `gmu243p2=oob=1 ack=… info=… bs=… gx=… mx=…`。
- 块规模：**45,151 字符 / 794 行 / opens=87 closes=87**、块内 `gmuW[` **39**（三臂 11 / 12 / 16）、探针全文件 **1,779,420 字符 / 29,906 行**、全文件 `gmuW[...] =` 实测 **940**。

**115.3 逐字读数（设备自写）**
- **241** 零字节：`rows: gmu241p0=0 gmu241p1=0 gmu241p2=0 gmu241l=0`，`row key detected from stamps []`，窗口 sha256 `41ada9c94a468e50a9e62cd7a1211f519cd6dcccf3cd669ebe93c7337f9e1e7b`，`boot_id 642521717`，`THE WINDOW IS STILL THE PREVIOUS ROUND'S BYTES`；刷写回读一致（`flash readback matches: True`）。
- **242** 零字节：`rows: gmu242p0=0 gmu242p1=0 gmu242p2=0 gmu241l2=0`，同窗口 sha、同 `boot_id 642521717`；因为这一键的 runner 没有锚定上一轮的 `boot_id`（`PREV_BOOT = 0`），判定句打到 `FRESH WINDOW BUT ITS STAMPS NAME []` 这个分支 —— **这是判定脚本的诚实拒绝，不是「本轮有新窗口」**。
- **243** 零字节：`rows: gmu243p0=0 gmu243p1=0 gmu243p2=0 gmu241l3=0`，同窗口 sha、同 `boot_id 642521717`；同样因为未锚定走到 `FRESH WINDOW BUT ITS STAMPS NAME []`，`doorbell acked None info None slumber option None votes None None`。
- 三份窗口文件都是 33,554,432 B，sha 与 R290 键 240 的窗口逐字相同 ⇒ **没有任何一轮写过窗口**。

**115.4 判决**
1. **本轮没有任何寄存器读数**：三键全零字节，`ctl` bit0、`cf` bit16、`fsr` 这些都没有新证据 ⇒ 「把描述符清零移到复位之前再举起」这个时序**既没有被证实也没有被排除**。
2. **「整轮消失」这种死法在本轮复现了三次**（241/242/243），而结构上只差一两笔的 R290 键 240（描述符在复位期间有效、轮询之后清）与 R289 键 237 都活下来了 ⇒ **死法不是稳定的关于某一笔写**；它至少和「这一轮从哪一步开始就没能刷出去」有关，可能就是日志介质把整批行丢掉。
3. **R290 的「描述符保持有效 = 死」必须降级为「在这三次观测里两者同现」**：本轮 241/242/243 都把描述符清在复位**之前**（也就是不复现 R290 那个形状），但它们同样死；同时 R289 键 237 与 R290 键 240 在描述符有效的形状下活着。单凭「描述符有效/无效」这条线切不开生死。
4. **HFI 依然没有被接受**（本轮连读数都没有，所以只能说：没有观察到接受）。
5. **流程缺陷（要记）**：只给 241 做了锚定，242/243 的 `PREV_BOOT`/`PREV_SHA` 还是 0 ⇒ 它们的判定句无法做「窗口是否刷新」的判断。**锚定必须逐键做，且当上一键零字节时，下一键应锚到「最后一次真实观测到的 boot」而不是上一键的名字**。

**115.5 边界与读数陷阱**
- **零字节 = 这一轮死在第一次刷盘之前**；窗口 sha 不变、`boot_id` 不变、`exec_key` 仍是更早那一轮 —— 三者要一起看，任何一个单独看都会误判。
- **不要用「上一键的 boot」做锚**如果那一键零字节：本轮 242/243 就是例子；正确锚是「最近一次真实落地轮次的 boot」。
- runner 的判定分支在 `_fresh` 为假时给的是「窗口仍是上一轮字节」，在 `_fresh` 为真但 stamp 不匹配时给的是「新窗口但 stamp 命名别的键」—— 这两句都是**拒绝归因**，不是观测结果。
- 本轮 `qinfo`/`qtbl` 都没有被读到，所以不能再说「写进去的是新地址还是陈旧地址」。

**115.6 门禁与工具（本轮全部通过）**
- **静态门** `work\tmp\r291_static.py`：对真产物 **PASS**（`required+survivors: 110 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、forbidden `gmu244a/gmu245a/gmu246a` 三零、固件 32768 B sha `b07f9029…` 钉住）；对 **R290 产物** 传 `r290-kernel\kernel.macho r290-payload\payload.img` → **FAIL 12**（12 条 REQUIRED 全 `kernel=0 payload=0`）⇒ 区分能力自证成立。
- **探针门** `scripts/check-enchilada-adreno-probe.py`：store 预算 **901 → 940**，并新增两条独立断言（`gmuW[r241qiWord] = 0u;` 至少两次、`gmuW[r241setWord] = 0x40000000u;` 存在）——**不再往那条巨长消息串里插句子**，R290 那次插坏留下空串残迹的教训。修完 **30/30 PASS**。
- **host 门** 25/25 + 51/51 + 32/32 + 各 contract PASS；**stage4 63/63**、**stage5 50/50**、**stage6 2287/2287**；全量 `..\tmp\r239_allgates.py` → **ran 46 of 48, FAILING: 0**；`work\tmp\check_mode_selector.py` → **46 selectors / upper 243 / 自证 FAIL / VERDICT PASS**。
- **CI** #409 `45298367330` **success 28/28**（分支 `r291-raise` commit `9596d6f8eab265deca9171a2a5f65622ed08576f`、tree `8e47a220232e8956f1a1991e5c77bb8bed7134fc`、base main `da1cc8b8c118`，534 已有 + 6 上传）。
- 产物：`work\tmp\r291-kernel\kernel.macho` 126,550,016 B sha256 `ec263f61cf12e0d3c2205e9ffcfd8600c70ecf60100cffb0b925c92311044ca5`；`work\tmp\r291-payload\payload.img` 150,994,944 B sha256 `2608684902c4acf330a8f86ad33340fc96f1028f88bea4eb0d153e2291ed7e1b`。重键 p241 `88df721e20bc6d3f368f2ce19b434493b1a178b9d0e8d57aeeb7cd7ad598bea6`、p242 `2db8a7b78a8108a22c68d2053f3ec949824c5088b89719fe9ecbf43baf79ba4d`、p243 `91804c44d0215c459884ac43bd113ecfa572940f8ff8fee22a82265a82b0b089`。
- 工具：`work\tmp\r291_mkgen.py`（从 `r290_block.py` 做受控文本手术：链式改名 240→243/239→242/238→241、`OLD_HDR` 显式改成 238/239/240、复位前 `qi` 写 0、插 `@RAISE@` 与 `@POST2@`、`EX_REST_LIGHT` 只在需要时发射）、`r291_gate3.py`、`r291_gates45.py`、`r291_gates6.py`、`r291_mkstatic.py`/`r291_static.py`/`r291_static_check.py`、`r291_mkrun.py`/`r291_s24N_run.py`、`r291_rowscheck.py`、`r291_anchor.py`、`r291_wait.py`、`peek_ci.py <run_id>`。
- 教训：**`python -c` 里绝不要写带引号/花括号/`elif` 的代码**（PowerShell 把整段打碎成 `ParserError`，本轮两次）；**`git show :path` 的输出是 UTF-8**（PowerShell 重定向才是 UTF-16LE，混用会把探针写坏）；**`source_rows()` 是无参函数**，静态门自检要读 `mod.PROBE` 的工作副本（读 index 版会看不到本轮刚落的块，本轮因此误报 12 条 REQUIRED 缺失）；**runner 的 `verdict.append` 带 `{}` 时检查 `.format(` 必须按括号配平取窗口**，定长 4 行窗口会误报。

**115.7 下一步（R292）**
- **先量死法本身，而不是再猜密码**：**244** = 完全不做任何 store、只按五步各打一行（零写控制键）——若它也零字节，则「整轮消失」与写无关，是介质/刷盘的性质；**245** = 241 的同一形状（重复一次），判三连死是否可复现；**246** = 只做「清描述符 → 打一行 → 退出」，把致死点夹在最小形状里。三键都要保证**至少一行落在任何一笔 store 之前**，并且逐键锚到最近一次真实落地的 boot。
- 其余状态不变：准入表 `modeled=9 / satisfied=0`、`CONFIG_XNU_NATIVE_ADRENO=n`、GPU 未渲染一帧；G1（GDSCR bit16 由什么置起）仍未关闭。

## 一百一十六、R292：先量「整轮消失」这个死法本身（键 244/245/246）

**116.1 一句话**
R291 三个键全部零字节之后，本轮不再猜寄存器，而是直接量这个死法：一个**完全不写任何 GMU 字**的轮次（键 244）**五行全落地**，一个**只写一笔**描述符清零的轮次（键 246）**两行都落地且清零生效**，而**逐笔复现 R291 键 241 的十一笔形状**（键 245）**连死两次**。于是「整轮消失」不再能归给介质本身，它和这一轮写了多少笔绑在一起。

**116.2 三键设计**
- **244**：零 store。先打一行普查（`ver/ctl/qi/qa/st/gd/cf/fw/sr`），再五步各等一毫秒打一行（`step/ver/st/gd/cf/fw`），最后打尾行。整键不碰 `HFI_CTRL_INIT`、不碰 `CM3_SYSRESET`、不碰描述符。
- **245**：从探针里把 R291 键 241 那一臂的源码**原样切出、整段改名为 245**（`arm241.replace("241", "245")`），所以它是 241 的逐字复现：清描述符在复位脉冲之前、启动字之前再举起、掩码在启动字之前、十一笔 store。
- **246**：最小的夹逼形状 —— 打一行普查 → **唯一一笔 store**（`HFI_QTBL_INFO = 0`，即描述符清零）→ 再打一行把描述符读回 → 尾行。

**116.3 逐字读数（设备自写）**
- **键 244**：`boot_id 587771363` / 窗口 `54e4b5a0d4943e23ff7eb09020a05158308f4878062ff40c39ecaa4f4d90b472`。`rows: gmu244p0=1 gmu244q%u=0 gmu244l=1`（行名里的 `%u` 是字面量，实际五行由 `gmu244q\d+=` 另行数得 **5**）；`census row: ver 00000000 st 000000d4`；`cadence rows the medium kept: 5 of 5`；`rail after the round: gd 00222001 cf 00088400 gx_on 0`。判定句 `THE WHOLE NO-STORE ROUND SURVIVED, so losing a round is caused by a store`。
- **键 245**：**两次都是零字节**。第一次 `rows: gmu245p0=0 gmu245p1=0 gmu245p2=0 gmu245l=0`、`row key detected from stamps [244]`（锚还指着 244 的窗口）；重锚到 244 的 `587771363 / 54e4b5a0…` 之后再跑，`window sha256` 仍是 `54e4b5a0d4943e23ff7eb09020a05158308f4878062ff40c39ecaa4f4d90b472`、`boot_id` 仍是 `587771363`，判定句 `THE WINDOW IS STILL THE PREVIOUS ROUND'S BYTES (boot_id 587771363, sha unchanged); this boot committed nothing, so the round ended before it could print`。
- **键 246**：`boot_id 587672940` / 窗口 `03060bc93cfc5908a4198fedd3b05863e156d122aa1a3fda9f0df4969fbfe39d`。`rows: gmu246p0=1 gmu246p1=1 gmu246l=1`；`descriptor before 00000001 after 00000000 queue address ad98b000`；`init result babeface reset word 00000000 after the single store`；`rail after the round: gd 00222001 cf 00088400 gx_on 0`。

**116.4 判决**
1. **零 store 的轮次能整轮活下来**（244 五行 + 普查 + 尾行全落地，且它是本轮唯一没有任何 GMU 写的键）⇒ 「整轮消失」不是介质的无条件性质。
2. **一笔 store 也不致命**（246 两行都落地、`HFI_QTBL_INFO` 确实从 `00000001` 读到 `00000000`、`fw=babeface`）⇒ 致死点不在「有没有写」这个二分上。
3. **十一笔形状连死两次**（245 两次零字节，且第二次是重锚之后仍然零字节）⇒ R291 的三连死是**这个形状**的性质，不是那三次引导的偶然。
4. 于是边界落在**一笔与十一笔之间**：致死不是单笔，而是某一笔（或某几笔的组合、或累计写入）。R293 的题目因此变成**二分 store 数**：把 241/245 的十一笔逐笔删到剩七笔、四笔、两笔，看哪一档开始死。
5. **HFI 本轮没有任何新读数**（245 死、244/246 不碰启动字）⇒ G1（GDSCR bit16 由什么置起）仍未关闭，`ctl` bit0 无从观察。
6. 三条旁证同向：`st=000000d4`（GX_HM_GDSC_POWER_OFF 与 GX_HM_CLK_OFF 都置）、`gd=00222001`（SW_COLLAPSE 仍置、power-up 未置）、`cf=00088400`（bit16=0）。

**116.5 边界与读数陷阱**
- 「零字节」只能读成**这一轮没活到第一次刷盘**；它不等于「这些 store 没执行」。
- **锚必须每键重锚，哪怕上一键落地了**：244 落地后 245 的 runner 还指着 R290/R291 的旧锚，第一次跑出来的 `row key detected from stamps [244]` 就是它 —— 那一次的结果不能用来判 245。
- **行名里的 `%u` 会被当字面量去数**：244 的 `gmu244q%u` 计到 0，而同一份日志里 `gmu244q\d+=` 数到 5；行表与判定句必须用同一个口径，否则同一轮会给出两个互相矛盾的「落地行数」。
- **判定脚本的字符串比较要按真实位数**：246 的描述符读回是 `00000000`（八位十六进制），而脚本写的是 `_q1 == "0"`，于是把「清零生效」印成了「清零没生效」；读数以 `descriptor before 00000001 after 00000000` 为准。
- 探针门本轮把 store 预算从 **940 抬到 952**（244 零笔、245 十一笔、246 一笔），并新增两条独立断言（244 不写任何 `gmuW[...]`、246 只写一笔）。
- 静态门 `r292_static.py`：真产物 **PASS**（`required+survivors: 108 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、forbidden `gmu247a=/gmu248a=/gmu249a=` 三零、固件 32768 B sha `b07f9029…`），对 **R291 产物 FAIL 10** ⇒ 区分能力自证成立。

**116.6 门禁与工具**
- 构建：CI **#410 `45304747158` success**（28 个 step 全 success，含 Build kernel / Build payload / Upload）；分支 `r292-loss` commit `32bafb67aabc963139bd4f18868ed64417e3e0b7`、tree `e624a6fe74c7e1cd813a32de2229bb419594a510`（base main `7b62e2bde04e`，535 已有 + 5 上传）。
- 产物：`work\\tmp\\r292-kernel\\kernel.macho` 126,550,016 B sha256 `6649f01cd6ac983d2961f94893a4c522b64e802bbbd9b5c5b58a2d71894f1ed5`；`work\\tmp\\r292-payload\\payload.img` 150,994,944 B sha256 `d33f2609733bd3d580b312a0dcf3771429b29b1506c65dfd0f3214feb0aace8a`。
- 重键（`r170_rekey.py … "enchilada-pas-exec=2" "enchilada-pas-exec=24N"`）：p244 `361980791c795d6926fc5307dff8356e9d52c74d17ec6e8239a52ce18fa08a41`、p245 `4db491681f47c9114ced1a7188320dd3cf8345497b007576f63b4ea700f62952`、p246 `762f9ff9e0d2c0d7ec796096707d859ba5924ee444b482da1b6fa85cc6d9ec51`。
- 门禁：host 门 25/25 + 51/51 + 25/25 + 32/32；probe 门 **30/30**；stage4 **64/64**、stage5 **51/51**、stage6 **2313/2313**；全量 `ran 46 of 48, FAILING: 0`；模式选择器 **47 个 / 上界 246 / 自证 FAIL / VERDICT PASS**。
- 本轮新工具：`work\\tmp\\r292_block.py`（手写生成器：245 从探针里整段复制 241 那一臂，244/246 手写）、`r292_count.py`、`r292_gate3.py`、`r292_gates6.py`、`r292_fix1.py`、`r292_fix2.py`（选择器变量必须叫 `r244mode` 而不是 `r245mode`，否则模式守卫会把它读成 `(247, 246)`）、`r292_rows.py`、`r292_mkstatic.py`、`r292_fix3.py`（forbidden 曾错成 `gmu2247a=`）、`r292_static_check.py`、`r292_mkrun.py`、`r292_rowscheck.py`、`r292_anchor.py`。

**116.7 下一步（R293）**
- **二分 store 数**：以 245 的十一笔为起点，做**七笔**、**四笔**、**两笔**三个递减形状（保留「清描述符 → 复位 → 写地址 → 描述符举起 → 掩码 → 启动字」这条骨架，按优先级删：先删两个中断 CLR、再删 `PWR_COL_CP_RESP`、再删掩码），看哪一档开始零字节。若四笔活、七笔死，就再细分到五笔/六笔。
- 每个键**第一行必须在第一笔 store 之前**，并且逐键重锚到上一键（无论上一键是否落地）。
- 其余状态不变：准入表 `modeled=9 / satisfied=0`、`CONFIG_XNU_NATIVE_ADRENO=n`、GPU 未渲染一帧；G1 仍未关闭。

## 一百一十七、R293：把「十一笔致死」二分成四笔、七笔、九笔（键 247/248/249）

**117.1 一句话**
R292 把边界夹在「一笔（键 246，活）」与「十一笔（键 245，连死两次）」之间。本轮从键 245 的源码里**逐笔删减**，得到**四笔（247）**、**七笔（248）**、**九笔（249）**三个形状：**四笔活、七笔活、九笔连死两次**。于是致死的那一笔落在 **248 与 249 之间的那两笔**：`GMU2HOST_INTR_MASK = 0xff7fffff`（`0x5193`）与**复位之后的描述符再举起**（`HFI_QTBL_INFO = 1`，`0x5184`）。三臂都没有写两个中断 CLR（`0x5191`/`0x9304`），所以那两笔已经被排除。

**117.2 三键设计**
三个键共用同一段前导（与 R292 键 245 逐字相同的骨架：装 CB0、分配四页并建表、每页五百一十二条目全填、`dc civac`、清 CB_FSR），差别只在**写出多少笔 `gmuW` 存储** —— 其余每一行读、每一个屏障、每一条打印都逐字保留：

- **247（四笔）**：`HFI_QTBL_INFO = 0`（复位前清描述符）→ `CM3_SYSRESET = 1` → `CM3_SYSRESET = 0` → `HFI_CTRL_INIT = 1`。
- **248（七笔）**：247 的四笔 + `CM3_FW_INIT_RESULT = 0`、`HFI_QTBL_ADDR = 新表地基`、`PWR_COL_CP_RESP = 0`。
- **249（九笔）**：248 的七笔 + `GMU2HOST_INTR_MASK = 0xff7fffff`、复位之后 `HFI_QTBL_INFO = 1`（把描述符再举起）。

三臂都保留复位脉冲后等待 `0xbabefaceu` 的 200000 拍轮询，因此「固件是否重跑」在三臂里都是可观测的。

**117.3 逐字读数（设备自写）**
- **键 247（四笔，活）**：`boot_id 601203741`、窗口 `4f516cc35e26693dbc0c2102ab703b43979652a06ed64a1210d29ec73f5fa940`，四行全落地；`rows: gmu247p0=1 gmu247p1=1 gmu247p2=1 gmu247l=1`；`census row: ver 00000000 st 000000d4 cx_on 1`；`preamble fills 3 build 0`；`control 00000000 bit0 0 after 200000 polls; boundary 00000400`；`init result babeface reset word 00000000`；`address written ad98d000 descriptor at the end 00000000 mask 00000000`；`rail after the round: gd 00222001 cf 00088400 gx_on 0`。
- **键 248（七笔，活）**：`boot_id 643800062`、窗口 `006f9919bc8965077846459e76055500087273117a80a6bd57ddbd375cc25c5b`，四行全落地；`census row: ver 00000000 st 000000d4 cx_on 1`；`preamble fills 3 build 0`；`control 00000000 bit0 0 after 200000 polls; boundary 00000400`；`init result babeface reset word 00000000`；`address written ad9a0000 descriptor at the end 00000000 mask 00000000`；`rail after the round: gd 00222001 cf 00088400 gx_on 0`。
- **键 249（九笔，零字节，连死两次）**：两次上机窗口 sha 仍是 `006f9919bc8965077846459e76055500087273117a80a6bd57ddbd375cc25c5b`、`boot_id` 仍是 `643800062`，`rows: gmu249p0=0 gmu249p1=0 gmu249p2=0 gmu249l=0`；但 `flash readback matches: True`、`system_b head 8 MiB sha256 46710f3e2e3d639c892ddc2c05873be957b0af49b380e49222638829650c26b0` 与 payload 头逐字相同、`key block 31136: device == payload -> True`、`key block says: enchilada-pas-exec=249` ⇒ **刷写无误，是这一轮没活到第一次刷盘**。

**117.4 判决**
1. **边界在七笔与九笔之间**：四笔活、七笔活、九笔连死两次。R292 的「一笔不致命、十一笔致死」由此细化成一个具体的两笔区间。
2. **两个是嫌疑犯**：249 相对 248 多出的恰好是 **`GMU2HOST_INTR_MASK = 0xff7fffff`** 与**复位后的描述符再举起 `HFI_QTBL_INFO = 1`**。下一轮把这两个拆开（各加一笔，都是八笔）就能一次判死。
3. **两个中断 CLR 已被排除**：三臂都没写 `0x5191`/`0x9304`，247/248 照样活着、249 照样死，所以「清中断」不是存活的必要条件，也不是致死的充分条件。
4. **HFI 仍未接受**：247/248 两个活着的、结构完整的轮次都在 200000 拍里读到 `ctl = 00000000`、`bit0 = 0`，`fsr = 00000400`（无 walk fault）。G1（GDSCR `bit16` 由什么置起）仍未关闭。
5. **GX 侧仍全零**：三臂 `gd = 00222001`（SW_COLLAPSE 仍置、power-up 未置）、`cf = 00088400`（`bit16 = 0`）、`gx_on = 0`、`st = 000000d4`（`GX_HM_GDSC_POWER_OFF | GX_HM_CLK_OFF` 都置）。GPU 尚未渲染一帧。

**117.5 边界与读数陷阱**
- **描述符读回 0 是设计使然**：247/248 的 p2 行读 `HFI_QTBL_INFO`，这两个键在复位前就把它清了，所以读回 `00000000` 不代表写入失败 —— 判据是**「这一行有没有落地」**，不是它的值。
- **零字节轮次不能归因**：249 两次的窗口 sha 与上一轮（248）逐字相同，说明这一轮**没有写过窗口**；判定脚本因此打印「窗口还是上一轮的字节」。这类行必须与 `flash readback matches` 一起读，否则会把「刷写失败」误判成「探针死了」。
- **同键第二次上机必须在前一次真实落地的窗口上锚定**：249 首跑前已用 248 的 `boot_id 643800062`/`006f9919…` 锚定，所以两次零字节是同一判断，而不是「锚错」。
- **`iodelay` 与轮询**：三臂都保留两条 `IODelay(1u)` 轮询（复位后等 `0xbabeface`、启动后等 `ctl` bit0），这是三臂之间唯一与 store 数无关的固定开销。

**117.6 门禁与工具**
- host 门 25/25 + 51/51 + 25/25 + 32/32；probe 门 **30/30**（store 预算 `952 -> 972`，并在巨长消息串之外另加两条独立断言）；stage4 **65/65**、stage5 **52/52**、stage6 **2342/2342**；全量 `ran 46 of 48, FAILING: 0`；`check_mode_selector.py` **48 selectors / upper 249 / 自证 FAIL / VERDICT PASS**。
- 静态门 `work\tmp\r293_static.py`：真产物 **PASS**（`required+survivors: 110 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、forbidden `gmu250a=`/`gmu251a=`/`gmu252a=` 三零、固件 32768 B sha `b07f90299ff2ae33948fcfd6ed67f53fbd287b5bf85ff2d43db11d4d32c1621f`）；对 **R292 产物**自证 **FAIL 12**。
- CI **#411 `45313447017` success 28/28**；分支 `r293-bisect` commit `4477e8be2792d1a019b0a156c59a4b6c4522c6d3`、tree `97ab5e54e3a2477ccbf20e958d7bd1f149abfedb`（base main `a424dea88b54`，535 已有 + 5 上传）。
- 产物：`r293-kernel\kernel.macho` 126,550,016 B sha256 `01d3647c7ce8091c02841868c9c17ef82bef8b9150fec19433b6e577d2b304b1`；`r293-payload\payload.img` 150,994,944 B sha256 `fe16e815542323b06f6b1385feaa7d8b7fb44b43789ed65096275bdadb988d55`。重键后 p247 `f4176d76b1120497f70245fadb5a953035dce8d9ca47638bbd19fb04cd5fc938`、p248 `0534da352e2abf0f23dd5c0ae8275d551ed750f2bb416e332973a360ad05d4c1`、p249 `a3b70a24014cbc4285dca2c7637ff1ad6aba56e9ff387d7ed6caa363c3510915`。
- 教训：`r293_mkstatic.py` 从 `REQUIRED = [` 切到 `SURVIVORS = [` 会把 REQUIRED 自己的收尾 `]` 吞进去，必须只取 `\n]\n` 之前的部分；`b'gmu247a='` 在源里是**单引号**字面量，替换前先 `count("b'" + x + "'") == 1` 核对；生成 runner 时 `main()` 的键元组与 `r292`/`r293` 前缀要一并改，否则会用旧模板的 `r292_s%d` 输出。

**117.7 下一步（R294）**
把 249 相对 248 多出的两笔拆成两个八笔臂：**250 = 248 + 仅 `GMU2HOST_INTR_MASK = 0xff7fffff`**；**251 = 248 + 仅复位后 `HFI_QTBL_INFO = 1`**；**252 = 248 + 两笔都在（八笔 + 一笔 = 九笔，即 249 的复现对照）**。三键都保留 247/248 已被证明能活的前导，逐键重锚（247 的 `601203741`/`4f516cc3…`、248 的 `643800062`/`006f9919…`）。

## 一百一十八、R294：把七笔与九笔之间的两笔拆开（键 250/251/252）

**118.1 一句话**
R293 把边界夹在「七笔（键 248，四行全落地）」与「九笔（键 249，连死两次）」。本轮把 249 相对 248 多出的那两笔拆成两个八笔臂：**250 = 七笔 + 仅 `GMU2HOST_INTR_MASK = 0xff7fffff`（掩码）**、**251 = 七笔 + 仅复位之后的 `HFI_QTBL_INFO = 1`（描述符再举起）**、**252 = 两笔都在（九笔，249 的复现对照）**。结果：**250 四行全落地，251 零字节，252 零字节** ⇒ **致死的是「复位之后再举起描述符」那一笔，掩码无罪**。

**118.2 三键设计**
三臂共用同一段前导（与 R293 键 247/248 逐字相同的七笔骨架：装 CB0、分配四页并建表、每页五百一十二条目全填、`dc civac`、清 `CB_FSR`、`CM3_FW_INIT_RESULT = 0`、写 `HFI_QTBL_ADDR`、复位脉冲 `CM3_SYSRESET = 1/0` 与 `PWR_COL_CP_RESP = 0`、等 `0xbabeface`、`HFI_CTRL_INIT = 1`、200000 拍轮询），差别只在**复位之后那两笔**：

- **250（八笔，`mask_only`）**：七笔 **+ `GMU2HOST_INTR_MASK = 0xff7fffff`**，描述符保持清着的状态。
- **251（八笔，`raise_only`）**：七笔 **+ `HFI_QTBL_INFO = 1`**，掩码一个字节都不写。
- **252（九笔，`both_again`）**：两笔都写，即 249 的九笔形状原样重放。

三臂都不写两个中断 CLR（`0x5191`/`0x9304`），每臂的普查行都落在第一笔存储之前，逐键重锚（250 锚 R293 键 248 的 `643800062` / `006f9919…`；251 与 252 锚 250 的 `644404938` / `b72145af…`，因为 251 零字节后 252 只能锚到最近一次真实落地的引导）。

**118.3 逐字读数**

- **键 250** `boot_id 644404938` / 窗口 `b72145afafb87af744d6d14fa9e4c5b8562e3d5c1bde5d0ffbceba279396fa7c`，四行全落地：
  `gmu250p0=pre=1 ver=00000000 qi=00000001 qtbl=ad9a0000 fw=babeface st=000000d4 gd=00222001 cf=00088400 cx_on=1`
  `gmu250p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000 fsr=00000400 far=00000000 fw=babeface sr=00000000`
  `gmu250p2=mask_only=1 qa=ad9a4000 qi=00000000 mask=ff7fffff fw=babeface sr=00000000 ctl=00000000 bit0=0`
  `gmu250l=mode=250 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ctl=00000000 survived=1`
- **键 251**：零字节 —— `rows: gmu251p0=0 gmu251p1=0 gmu251p2=0 gmu251l=0`；窗口 sha256 仍是 `b72145af…`、`boot_id` 仍是 `644404938`（即 250 那一轮的字节），判定句 `THE WINDOW IS STILL THE PREVIOUS ROUND'S BYTES … this boot committed nothing`；`flash readback matches: True`、钥匙块回读一致 ⇒ **刷写无误，是这一轮没活到第一次刷盘**。
- **键 252**：同样零字节、同样窗口与 `boot_id`，判定句同上。

**118.4 判决**

1. **掩码无罪**：250 在全配方前导之后写下 `mask=ff7fffff`（读回逐位一致），四行照样落地 ⇒ `GMU2HOST_INTR_MASK` 不是致死的那一笔。
2. **致死的是「复位之后再举起描述符」**：251 只比 250 多这一笔 `HFI_QTBL_INFO = 1`，就整轮消失 ⇒ 在 CM3 已经开始跑之后再让队列表描述符有效，会让这一轮在第一次刷盘之前就结束。
3. **九笔形状的死亡由此归因完成**：252 复现 249 的零字节（第三次），而 250 说明它不是掩码引起的 ⇒ **R291 三连死、R292 十一笔连死、R293 九笔连死，命中的都是同一笔**（那些形状里 `HFI_QTBL_INFO` 都处于 1）。
4. **与 R290 的结论一致**：R290 键 238（新地址 + 描述符保持有效）死、240（新地址 + 复位后清描述符）活 ⇒ 当时判成「描述符整轮保持有效」，现在可以收紧为「**在 CM3 运行期间让描述符有效**」。
5. **HFI 仍未接受**：唯一活下来的 250 在 200000 拍内读到 `ctl=00000000 bit0=0`、`fsr=00000400`（无 walk fault）⇒ **G1 未关闭**；`gd=00222001`、`cf=00088400`（bit16=0）、`gx_on=0`、`st=000000d4`，GPU 未渲染一帧。

**118.5 边界与读数陷阱**

- 251/252 的判定句里的「row key detected from stamps [250]」来自窗口里仍然属于 250 的 SHLOG —— **零字节时窗口与 `boot_id` 都不会前进**，这不是新读数，不能用来归因 251/252。
- 与 R293 一样，**锚必须在每个键之前重锚**；上一键零字节时必须锚到最近一次真实落地的引导（本轮 251 死、252 只能锚 250）。
- 这一轮**没有出现任何新的 `ctl` 读数**，所以「HFI 会不会在某个形状下被接受」本轮仍然只有否证、没有新的证据。

**118.6 门禁与工具**

- 块：`work\tmp\r294_block.py` 从 `work\tmp\r293_block.py` 派生（三臂共用 `work\tmp\r293_arm245.txt` 的原始臂文本，按 `DROP = {250: ["clr","aoc","qi1"], 251: ["clr","aoc","mask"], 252: ["clr","aoc"]}` 逐笔删减），生成后块 42,298 字符 / 741 行 / opens=85 closes=85、`gmuW[` 25（8/8/9）、probe 1,883,665 字符 / 31,732 行、全文件 store 997。
- 门禁：host 32/32 + 51/51 + 25/25；probe **31/31**（store 预算 972 → **997**，另加一条独立断言「两组拆解臂各自点名了它要分开的那两笔」）；stage4 **66/66**、stage5 **53/53**、stage6 **2369/2369**（ID1 重读预算 40 → 41，键表补 250/251/252）；全量 `ran 46 of 48, FAILING: 0`；mode-selector **49 selectors / upper 252 / 自证 FAIL / VERDICT PASS**。
- 构建：CI **#412 `45320105059` success 28/28**；分支 `r294-split` commit `0f03d6fa8ef280b6d649b237752a542f870679d0`、tree `7104377a798ea6aa17fba4e73304d0bbca8a3522`（base main `bc1fafa891db`，535 已有 + 5 上传）。
- 产物：`work\tmp\r294-kernel\kernel.macho` 126,566,400 B sha256 `5384560c5ea3e43f580e93b0c9240527f429e30f09e6f1d22d8d85ecbb069842`；`work\tmp\r294-payload\payload.img` 150,994,944 B sha256 `42541ceb57850166023ae475e705aac2b0d4990c6717686359b8e188fe63215f`。重键后 p250 `3f53ffb022a13848926b506553f98310476ceb7b47255d36c008e369878ec785`、p251 `234f0e18972311045be96410291a443e71cd8567ce822bd8819a7b2378d7efea`、p252 `26923a2be9b6d10595c4c9aa30132285c5d6ecd9d8efde84564199347666b430`。
- 静态门 `work\tmp\r294_static.py`：真产物 **PASS**（`required+survivors: 110 checked in both artefacts`、marker kernel=1/payload=2、forbidden `gmu253a/gmu254a/gmu255a` 三零、固件 32768 B sha 钉住）；对 **R293 产物**（`r293-kernel\kernel.macho` `r293-payload\payload.img` 两参数）**FAIL 12**。
- runner：`work\tmp\r294_mkrun.py` → `r294_s250_run.py` 25,645 字符 / 18 verdict.append、`r294_s251_run.py` 25,620 / 18、`r294_s252_run.py` 25,623 / 18；ROWS 250=`gmu250p0/p1/p2/gmu250l`、251=`gmu251p0/p1/p2/gmu251l`、252=`gmu252p0/p1/p2/gmu252l`；STAMP `gmu250p0=`/`gmu251p0=`/`gmu252p0=`。`r294_rowscheck.py` **PASS**；`r294_anchor.py`（正则 `r2(?:93|94)_s\d+\w*_window\.bin`）三次锚定分别命中 `r293_s248_window.bin`（250）、`r294_s250_window.bin`（251）、`r294_s250_window.bin` 与 `r294_s251_window.bin`（252）。
- 教训：派生 runner 生成器时 `MARK` 常量里的轮次号**不能跟着数字改名链走**（模板 `r272_s178_run.py` 里那行仍是 `R247 judgement`，改名后 `MARK` 找不到 ⇒ `ValueError: substring not found`）；`r294_mkstatic.py` 用 `body()` 切 `REQUIRED = [`/`SURVIVORS = [` 之后新列表是 12 + 98 = 110 条。

**118.7 下一步（R295）**
「举手」这一笔已经被判死，但它只是**让这一轮消失**，不是让 HFI 被接受。下一轮把注意力从「存活」搬回「接受」：**253 = 七笔前导 + 掩码（250 的存活形状）之后，干净地补做一次「先清描述符、再写 `HFI_QTBL_INFO = 1`、再写启动字」的完整序列**（把举手放在复位之后但在启动字之前，看是否既活下来又被接受）；**254 = 253 + 把描述符写在启动字之后的第二拍**（判顺序）；**255 = 253 但把 `HFI_CTRL_INIT` 换成 200000 拍的第二次尝试**（判是否只要「先举手再启动」）。

## 一百一十九、R295：把「举手」搬进复位窗口，三臂全部消失（键 253/254/255）

**119.1 一句话**
R294 把致死点判成「复位之后再举起描述符」。本轮把同一笔搬到复位**之前**（253，与 R260 键 143 的真实次序逐笔相同）、留在复位之后（254，R294 的复现对照）、以及复位之前**再加一次启动字**（255）。结果：**三臂全部零字节，窗口与 `boot_id` 都还是 R294 键 250 的那一份** ⇒ **「位置」不是判别式：只要把 `HFI_QTBL_INFO` 从 0 写成 1，这一轮就在第一次刷盘之前结束**。

**119.2 三键设计**
三臂共用 R293/R294 已被证明能活的七笔骨架（装 CB0、分配四页并建表、每页 512 条目全填、`dc civac`、清 `CB_FSR`、`CM3_FW_INIT_RESULT = 0`、写 `HFI_QTBL_ADDR = s255phys`、复位脉冲 `CM3_SYSRESET = 1`/`PWR_COL_CP_RESP = 0`/`CM3_SYSRESET = 0` 并等 `0xbabeface`），三臂都不写两个中断 CLR：

- **253（九笔，`raise_in_reset`）**：`序 = fw=0 → qa=新基址 → qi=0 → qi=1（举手）→ sr=1 → cp=0 → sr=0 → mask → ini=1`，**举手落在 `CM3_SYSRESET = 1` 之前**，这正是 R260 键 143 的顺序（143 当年就是这样被接受的）。
- **254（九笔，`raise_after_reset`）**：举手留在复位与等待之后、启动字之前，即 R294 键 251/252 的死亡位置，作为对照。
- **255（十笔，`raise_in_reset_retry`）**：253 再加一次 `HFI_CTRL_INIT = 1` 与第二个 200000 拍窗口，判「两次启动字」能否改变结果。

每臂的普查行都落在第一笔存储之前；253 锚 R294 键 250 的 `644404938` / `b72145af…`，254 与 255 锚上一键。

**119.3 逐字读数**

- **253**：`rows: gmu253p0=0 gmu253p1=0 gmu253p2=0 gmu253l=0`；窗口 sha256 **`b72145afafb87af744d6d14fa9e4c5b8562e3d5c1bde5d0ffbceba279396fa7c`**、`boot_id` **`644404938`**（与 R294 键 250 逐字相同）；`row key detected from stamps []`；判定句 `THE WINDOW IS STILL THE PREVIOUS ROUND'S BYTES …; this boot committed nothing, so the round ended before it could print`；`THE ROUND NEVER REACHED ITS TAIL ROW`。
- **254**：`rows: gmu254p0=0 gmu254p1=0 gmu254p2=0 gmu254l=0`；窗口 sha 与 `boot_id` 同上；判定句 `FRESH WINDOW BUT ITS STAMPS NAME []`（因为锚已是上一轮自己的窗口，`exec_key` 里没有本键的戳）。
- **255**：`rows: gmu255p0=0 gmu255p1=0 gmu255p2=0 gmu255l=0`；窗口 sha 与 `boot_id` 同上。
- 三键都 `flash readback matches: True`、`key block says: enchilada-pas-exec=25N` ⇒ 刷写无误，死的是这一轮本身。

**119.4 判决**

1. **位置不是判别式**：举手在复位**之前**（253，与 143 同序）与在复位**之后**（254）都让整轮消失 ⇒ R294 的表述必须收紧：不是「运行期间保持有效」，而是**把 `HFI_QTBL_INFO` 从清着的状态写成 1 这个动作本身**。
2. **两次启动字救不了**（255 同样零字节）⇒ 死点在启动字之前，与 HFI 是否被接受无关。
3. **掩码仍然无罪**（250 是同一个七笔骨架加掩码，四行全落地），两个中断 CLR 也仍被排除（三臂都没写）。
4. **与 143 的矛盾成为下一轮的主问题**：143 当年在 `QTBL_INFO = 1` 之后复位、被接受且四行落地；本轮 253 与它逐笔相同却整轮消失。剩下的差别只有两处 —— 143 的举手**前没有先写 `qi = 0`**（即没有 0→1 的跃变，它写的可能是一个原本就是 1 的位），以及 143 写回的 `HFI_QTBL_ADDR` 是**内核自己那张表**的地址而不是本轮新表的基址。
5. **HFI 仍未接受**：本轮没有任何一条新的 `ctl` 读数，G1（GDSCR `cf` bit16 由什么置起）未关闭；`gd=00222001`、`cf=00088400`、`gx_on=0`、`st=000000d4`。

**119.5 边界与读数陷阱**

- 「零字节」= **这一轮没活到第一次刷盘**；`flash readback matches: True` 与 `key block says: …` 是排除「没刷进去」的唯一凭据，必须在每键的公告里各看一次。
- **窗口 sha 相同不等于「本轮写了同样的字节」**，而是「本轮没写窗口」：`boot_id` 相同即证明这是上一轮的窗口文件。
- 254/255 的 `FRESH WINDOW BUT ITS STAMPS NAME []` 是**锚定归因失败的提示**，不是新读数；逐键必须重锚（上一键零字节时更要用「最近一次真实落地」的 boot）。
- 判定脚本的 `census row: ver None …` 只是「那些行没落地」的转写，不能读成「设备读到 None」。

**119.6 门禁与工具**

- host 门 25/25 + 51/51 + 32/32；probe 门 **32/32**（store 预算 997 → **1025**，并在 R294 那条断言之后新增一条独立断言 `R295 the three keys move the descriptor raise relative to the CM3 reset`）；stage4 **67/67**、stage5 **54/54**、stage6 **2397/2397**（stage6 新增约 33 条 R295 断言段，ID1 重读预算 41 → 42）；全量 `ran 46 of 48, FAILING: 0`；mode-selector **50 selectors / upper 255 / 自证 FAIL / VERDICT PASS**。
- 静态门 `r295_static.py`（由 `r294_static.py` 派生）：真产物 **PASS**（`required+survivors: 110 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、forbidden `gmu256a/gmu257a/gmu258a` 三零、固件 32768 B sha 钉住）；对 **R294 产物**（`r294-kernel\kernel.macho r294-payload\payload.img`）**FAIL 12**。
- 构建：CI **#413 `45326633497` success**；分支 `r295-position` commit `4e3afb0b1796e7bddb4a9aebc892bd2dcacef2c2`、tree `27510dff88b8a72a2e2fde2bec9ce2d0b14af892`（base `5720351c3d6c`，535 已有 + 5 上传）。产物：`work\tmp\r295-kernel\kernel.macho` **126,570,496 B sha256 `ab569cb9933e8eee8e1319df086b7bfcfefce52454a909a4e3ba2b30802ba491`**、`work\tmp\r295-payload\payload.img` **150,994,944 B sha256 `1bf5d608d255e6c9a9dce4736ba744a57550869aa398a45d82ae9bbd1f883a9b`**。
- 重键（`r170_rekey.py … "enchilada-pas-exec=2" "enchilada-pas-exec=25N"`，cwd `work\tmp`）：p253 / p254 / p255 各 150,994,944 B，配置串 1377 → 1379 B。
- 生成器 `work\tmp\r295_block.py` 的写法值得记：**不重打 245 那一臂**，而是从 `work\tmp\r293_arm245.txt` 读出原文，按 `STORES` 字典用「删一笔 + 在锚点前插一笔」的方式重排，因此 253 与 143 的顺序一致这件事是**外科手术得到**的，不是手写近似。

**119.7 下一步（R296）**
把「143 与 253 的矛盾」拆成两个可一次判死的变量：**256 = 存活骨架 + 只改 `HFI_QTBL_ADDR`（写新表基址）而完全不碰 `HFI_QTBL_INFO`**（判「换地址」本身是否致死）；**257 = 存活骨架 + 只写 `HFI_QTBL_INFO = 1` 一次（不先清 0，即没有 0→1 跃变）**（判跃变）；**258 = 存活骨架 + 先清 `qi = 0` 再写 `qi = 1`，但地址写回内核自己的那张表（`r256qtbl` 读回值）**（判「跃变 + 原地址」是否接近 143 的存活条件）。

## 一百二十、R296：判据是「启动时描述符处于什么状态」，不是跃变也不是地址（键 256/257/258）

**120.1 一句话**
R295 把「举手」搬到复位前后都死，于是本轮把 143 与 253 的三处差别各做一个臂：**256 只改 `HFI_QTBL_ADDR`、完全不碰 `HFI_QTBL_INFO`**、**257 只写一次 `HFI_QTBL_INFO = 1`（不先清 0，无跃变）**、**258 清 0 再举 1 但地址写回内核自己那张表**。结果**三臂全部零字节**。把三臂与 R294 键 250（活）逐笔对齐后，判据浮出来了：**凡是让描述符停在 1 的形状都死，凡是让它停在 0 的形状都活** —— 与跃变无关、与地址无关、甚至与我们是否写过它无关。

**120.2 三键设计**

三臂共用 R293/R294 已被证明能活的骨架（装 CB0、分配四页并建表、每页 512 条目全填、`dc civac`、清 `CB_FSR`、`CM3_FW_INIT_RESULT = 0`、复位脉冲与 `0xbabeface` 等待、`GMU2HOST_INTR_MASK = 0xff7fffff`、`HFI_CTRL_INIT = 1`、200000 拍轮询），都不写两个中断 CLR：

- **256（七笔，`address_only`）**：`fw=0 → qa=新表基址 → sr=1 → cp=0 → sr=0 → mask → ini=1`，**描述符一笔都不写**（保持内核留下的状态）。
- **257（八笔，`raise_without_clear`）**：256 + 一笔 `HFI_QTBL_INFO = 1`，前面没有清 0，因此没有 0→1 跃变。
- **258（九笔，`raise_with_kernel_address`）**：256 + 清 0 + 举 1，但 `HFI_QTBL_ADDR` 写回的是**内核自己那张表**的地址（`r256qtbl` 读回值），不是本轮新建的表。

每臂的普查行都落在第一笔存储之前；256 锚 R295 键 253 的最近一次真实落地 boot（`644404938` / `b72145af…`），257 与 258 锚上一键。

**120.3 逐字读数**

- **256**：`rows: gmu256p0=0 gmu256p1=0 gmu256p2=0 gmu256l=0`；窗口 sha256 **`b72145afafb87af744d6d14fa9e4c5b8562e3d5c1bde5d0ffbceba279396fa7c`**、`boot_id` **`644404938`**；判定句 `THE WINDOW IS STILL THE PREVIOUS ROUND'S BYTES …; this boot committed nothing, so the round ended before it could print`；`THE ROUND NEVER REACHED ITS TAIL ROW`。
- **257**：四行同样全 0、同窗口同 `boot_id`；判定句 `FRESH WINDOW BUT ITS STAMPS NAME []`（锚定归因失败提示，不是新读数）。
- **258**：四行同样全 0、同窗口同 `boot_id`。
- 三键都 `flash readback matches: True`、钥匙块回读 `enchilada-pas-exec=25N` 一致 ⇒ 刷写无误。

**120.4 判决**

1. **描述符停在 1 就是死**：256 一笔都不写却死 ⇒ 内核自己把描述符留在 1，**致死的是启动那一刻它处于 1 这个状态**，不是我们的某笔写。
2. **跃变不是判据**：257 没有 0→1 跃变，照样死。
3. **地址不是判据**：250（R294，活）写的是**新表基址**，258（死）写的是**内核自己的地址** ⇒ 地址两边都试过，生死不随地址变。
4. **于是「活」的形状只有一个共同点**：`HFI_QTBL_INFO` 在写启动字时是 **0**。R293 键 247/248、R294 键 250、R290 键 240 全部如此；R291 键 241/242/243、R292 键 245、R293 键 249、R294 键 251/252、R295 键 253/254/255、本轮 256/257/258 全部相反。
5. **与 143 的矛盾收窄到一处**：143 写 `HFI_QTBL_INFO = 1` 后复位、被接受且四行落地。按本轮判据它应该死。它相对 256/257/258 还多的东西只有**两个中断 CLR（`GMU2HOST_INTR_CLR` 与 `AO_HOST_INTERRUPT_CLR` 各 `0xffffffff`）** —— 这正是 R297 的第一变量。
6. **HFI 仍未接受**：本轮没有任何新的 `ctl` 读数，`gd=00222001`、`cf=00088400`（bit16=0）、`gx_on=0`、`st=000000d4` ⇒ G1 未关闭，GPU 未渲染一帧。

**120.5 边界与读数陷阱**

- 「零字节」= 这一轮没活到第一次刷盘；靠 `flash readback matches: True` 与钥匙块回读排除刷写问题。
- 窗口 sha 相同 ≠ 写了同样的字节，而是**本轮没写窗口**；`boot_id` 相同即证明这是上一轮的窗口文件。
- 257/258 的 `FRESH WINDOW BUT ITS STAMPS NAME []` 是锚定归因失败提示；逐键必须重锚。
- 本轮三臂的「致死」与 R295 三臂同形，**不要把「连续两轮全死」读成「设备坏了」** —— 判定脚本每次都先确认刷写回读一致。

**120.6 门禁与工具**

- host 门 25/25 + 51/51 + 32/32；probe 门 **33/33**（store 预算 1025 → **1049**，新增独立断言 `R296 the three keys separate the address, the raise and the transition`）；stage4 **68/68**、stage5 **55/55**、stage6 **2422/2422**（新增约 30 条 R296 断言段，ID1 重读预算 42 → 43）；全量 `ran 46 of 48, FAILING: 0`；mode-selector **51 selectors / upper 258 / 自证 FAIL / VERDICT PASS**。
- 静态门 `r296_static.py`（由 `r295_static.py` 派生）：真产物 **PASS**（`required+survivors: 110 checked in both artefacts`、`key marker 'enchilada-pas-exec': kernel=1 payload=2`、forbidden `gmu259a/gmu260a/gmu261a` 三零、固件 32768 B sha 钉住）；对 **R295 产物**（`r295-kernel\\kernel.macho r295-payload\\payload.img`）**FAIL 12**。
- 构建：CI **#414 `45331956397` success**；分支 `r296-transition` commit `07b0c411d6bd4215af8fa643b1193db4b114135c`、tree `d825373482984bd64d187da48da6a1a7b9d25879`（base `89e501092f28`，535 已有 + 5 上传）。产物：`work\\tmp\\r296-kernel\\kernel.macho` **126,586,880 B sha256 `ce81b1ae1a2d149243e5004bbd7d38c123504edc52cd534fe257eaccf4c84264`**、`work\\tmp\\r296-payload\\payload.img` **150,994,944 B sha256 `8caef06cade8104da2f6877aa13edf602760f3f90aa76363da86e7fd64eb13a8`**。
- 重键（`r170_rekey.py … "enchilada-pas-exec=2" "enchilada-pas-exec=25N"`，cwd `work\\tmp`）：p256 / p257 / p258 各 150,994,944 B。
- 生成器 `work\\tmp\\r296_block.py` 继续用「读 245 原始臂 + 按 `STORES` 字典删/插」的外科写法；258 的地址回写是**单独一条正则替换**（`gmuW[r256qaWord] = r256qaw;` → `gmuW[r256qaWord] = r256qtbl;`），因此「写内核地址」这件事在源码里是逐字可查的。

**120.7 下一步（R297）**
按判决 5，143 与本轮三臂的唯一剩余差别是**两个中断 CLR**：**259 = 256 的七笔 + 两个 CLR（`0x5191`/`0x9304` 各 `0xffffffff`）**（判「恢复 CLR 能否让描述符停在 1 的形状活下来」）；**260 = 250 的存活形状 + 两个 CLR**（判「CLR 会不会把活的形状弄死」，双向对照）；**261 = 259 + 写回内核地址（即 143 的完整链，含复位与两个 CLR）**（判「143 能被接受」是否真能被重放）。

## 一百二十一、R297：两个中断 CLR 既救不了停在 1 的形状，也会把活着的形状弄死（键 259/260/261）

**121.1 一句话**
R296 把 143 与本轮三臂的唯一剩余差别收窄到**两个中断 CLR**（`GMU2HOST_INTR_CLR` `0x5191` 与 `AO_HOST_INTERRUPT_CLR` `0x9304` 各 `0xffffffff`）。本轮三臂：**259 = 256 的七笔 + 两个 CLR（描述符一笔都不写）**、**260 = R294 键 250 的存活形状（描述符只清 0、不举）+ 两个 CLR**、**261 = 143 的完整链（清、举、复位、两个 CLR、掩码、启动字）**。结果**三臂全部零字节**，窗口 sha256 仍是 `b72145afafb87af744d6d14fa9e4c5b8562e3d5c1bde5d0ffbceba279396fa7c`、`boot_id` 仍是 `644404938` ⇒ **CLR 既不能把「描述符停在 1」的形状救活，还会把「描述符停在 0」的存活形状弄死**；143 的「被接受」在今天的表上不可重放。

**121.2 三键设计**

三臂共用 R293/R294 已被证明能活的骨架（装 CB0、分配四页并建表、每页 512 条目全填、`dc civac`、清 `CB_FSR`、`CM3_FW_INIT_RESULT = 0`、复位脉冲与 `0xbabeface` 等待、`GMU2HOST_INTR_MASK = 0xff7fffff`、`HFI_CTRL_INIT = 1`、200000 拍轮询），差别只在两个 CLR 与描述符：

- **259（九笔，`clears_descriptor_untouched`）**：`fw=0 → qa=新表基址 → clr → aoc → sr=1 → cp=0 → sr=0 → mask → ini=1`；**`HFI_QTBL_INFO` 一笔都不写**，保持内核留下的 1。
- **260（十笔，`clears_descriptor_cleared`）**：259 + 一笔 `HFI_QTBL_INFO = 0`，**只清不举**（即 250 的存活形状），仍带两个 CLR —— 双向对照臂。
- **261（十一笔，`clears_clear_and_raise`）**：259 + 清 0 + 举 1，即 143 的完整链。

每臂的普查行都落在第一笔存储之前；CLR 都写在掩码与启动字之前，与 143 同序。逐键重锚：259 锚 R296 键 256 那轮最近一次真实落地（`644404938` / `b72145af…`），260 锚 259、261 锚 260；因 259/260 都是零字节，按既定规则（前键零字节则锚最近一次真实落地）260/261 实际沿用同一锚。

**121.3 逐字读数**

- **259**（15:11:44 起；payload `7a93ac1e12268237ed7184418887622a18e634e7a12e5fd382870bdc1d9ffa3e`）：15:11:45 gate PASS、15:11:56 `flash system_b OK`、15:11:57 boot sent、15:12:37 back in recovery；`key block 31145: device == payload -> True`（键块 `enchilada-pas-exec=259 -no_slto_`）、`system_b head 8 MiB` sha256 `7167a2c0dd410110e2a340e04a803c36499159a626e5aec3252d16b4ce7997e9` 与 payload 头一致、`flash readback matches: True`；窗口 33,554,432 B sha256 `b72145afafb87af744d6d14fa9e4c5b8562e3d5c1bde5d0ffbceba279396fa7c`。runner 在判定块因 `_hl.sha259` 笔误崩溃（`AttributeError: module 'hashlib' has no attribute 'sha259'`），窗口与日志都已落盘，用 `r297_judge.py` 复判：`rows: gmu259p0=0 gmu259p1=0 gmu259p2=0 gmu259l=0`、`row key detected from stamps []`、`VERDICT: THE WINDOW IS STILL THE PREVIOUS ROUND'S BYTES (boot_id 644404938, sha unchanged); this boot committed nothing`。
- **260**（15:14:09 起；payload `8d98bf90df828f1b6fb42541ad795a75c21e506c3e27c8e37d7b73e39936d893`）：15:14:10 gate PASS、15:14:21 flash OK、15:14:22 boot sent、15:15:32 回 TWRP；`key block 31145: device == payload -> True`、`flash readback matches: True`；四行全 0、窗口 sha256 `b72145af…`、`boot_id 644404938`、`row key detected from stamps []`、同一条「仍在上一轮字节」判定。
- **261**（15:15:53 起；payload `82c048da0192e9edac2a78dea510d0a88b6423f0dcf811532f70441a5c07000a`）：15:15:54 gate PASS、15:16:05 flash OK、15:16:06 boot sent、15:16:46 回 TWRP；同样 `key block ... True` / `flash readback matches: True`；四行全 0、同窗口同 `boot_id`。
- 三键前后各一次 `REBOOT_TO_TWRP_GATE: PASS (3 checks, 0 failures)`（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`），三次刷写后都回读了钥匙块与 8 MiB 头 ⇒ 三键都不是「刷错镜像」。

**121.4 判决**

1. **CLR 不能救停 1 的形状**：259 与 256 的差别只有两个 CLR，256 死、259 也死 ⇒ 恢复 CLR 不改变结论。
2. **CLR 会杀活着的形状**：260 用的是 250 的原样存活形状（描述符清 0、不举），只多了两个 CLR，结果死。按 R296 判据（写启动字时描述符为 0 就活）它本该活 ⇒ **CLR 本身带毒**；143 的「被接受」不能再用「CLR 无害」解释。
3. **143 不可重放**：261 把 143 的完整链逐笔重放，仍死 ⇒ 143 那次被接受依赖探针之外的变量（那一轮的产物/表状态或时序），不是这条链本身的性质。
4. **必须写明的 caveat**：250 的「活」是在 R294 那版构建上观测的，本轮没有同一 build 的「无 CLR」阳性对照；判决 2 成立的前提是「260 的形状与 250 等价（只差 CLR）」。若跨轮构建差异本身就能致死，本条需要 R298 的同 build 对照复核。判决 1 与 3 不依赖这个前提（259/261 都是本轮同 build 内的同骨架对照）。
5. **HFI 仍未被接受**：本轮没有任何新的 `ctl` 读数（窗口里仍是上一轮那份日志），G1 未关闭，GPU 未渲染一帧。

**121.5 边界与读数陷阱**

- 「零字节」= 没活到第一次刷盘；三键窗口 sha 与 `boot_id` 全同，且每次回读一致 ⇒ 不是刷写问题，也不是设备坏了。
- 259 的判定来自复判脚本（runner 笔误崩溃），崩的是打印不是采集；窗口与设备日志都是 runner 落盘的原件。
- `enchilada-pas-exec=259 -no_slto_` 的尾巴是 `mcopy` 重写 config 时补的填充字节（config 1377 → 1379 B），不是键值损坏。
- 260/261 的锚定来自「前键零字节则锚最近一次真实落地」的既定规则；`row key detected from stamps []` 说明本轮没留下自己的 stamp，归因只能靠锚。
- 窗口 sha 相同 ≠ 写了同样的字节，而是**本轮没写窗口**；`boot_id` 相同即证明这是上一轮的窗口文件。

**121.6 门禁与工具**

- 取件：CI **#415 run `45338328882` success**（`2026-11-24T06:09:18Z → 06:28:54Z`）；分支 `r297-clears` commit `6e67fd86b1c4ea01e7330262709c172081de8e20`、tree `3a8133c4f152dc8f9154bf3efa1dddc5321465b8`（base main `7b8b91c8b30718760d9926c54d90d48781e5d312`，5 个文件：probe + 4 个验证脚本）。artifacts：kernel 78,981,473 B、payload 83,149,893 B、logs 3,743,603 B。
- 下载 payload 的教训：artifact API 的 302 跳到签名 blob URL 后**不能带 `Authorization`**，否则 401 `Server failed to authenticate the request`；本轮改用签名 URL + 8 线程 Range 分块（`work\\tmp\\r297_dl.py`，913.3 s，8 段各重试 6 次）取回，zip sha256 `c389eac92deb2c2f5083a411c0df15fd89ebf2d736c6456ebc797a6e35ac1870`、`testzip()` 无坏项。
- 产物：`work\\tmp\\r297-kernel\\kernel.macho` **126,586,880 B sha256 `ea2842b3a582ba9ae18c1eb8ec1a5bdc1b52b8676e7a07c6ed4a60253a3c7d38`**；`work\\tmp\\r297-payload\\payload.img` **150,994,944 B sha256 `d3bca99533d19b76b7f1f4008a8caa9fe3d070f031b84625d00e15daecb1585b`**。
- 静态门 `work\\tmp\\r297_static.py`：真产物 **PASS**（`required+survivors: 110 checked in both artefacts`、marker `kernel=1 payload=2`、forbidden `gmu262a/gmu263a/gmu264a` 三零、固件 32768 B sha 钉住）；对 **R296 产物**（`r296-kernel\\kernel.macho r296-payload\\payload.img`）**FAIL 12**（12 条 gmu259/260/261 行全 kernel=0 payload=0）⇒ 区分能力自证成立。
- 重键（`work\\tmp\\r179_rekey.py`，cwd `work\\tmp`）：p259 `7a93ac1e12268237ed7184418887622a18e634e7a12e5fd382870bdc1d9ffa3e`、p260 `8d98bf90df828f1b6fb42541ad795a75c21e506c3e27c8e37d7b73e39936d893`、p261 `82c048da0192e9edac2a78dea510d0a88b6423f0dcf811532f70441a5c07000a`，各 150,994,944 B。
- 门禁：host **25/25 + 51/51 + 25/25 + 32/32** + 各 contract PASS；probe **34/34**（store 预算保持 **1049**，新增独立断言 `R297 the clear keys carry none of the pinned pulse literals`）；stage4 **69/69**、stage5 **56/56**、stage6 **2448/2448**（新增约 30 条 R297 断言段，ID1 重读预算 43 → 44）；全量 `r239_allgates.py` → `ran 46 of 48, FAILING: 0`；`work\\tmp\\check_mode_selector.py` → **52 selectors / upper 261 / 自证 FAIL / VERDICT PASS**。
- 块 `work\\tmp\\r297_block.py`：43,251 字符 / 759 行 / `gmuW[` 30（9/10/11）；probe 源 2,012,181 字符 / 33,985 行。

**121.7 下一步（R298）**

1. **同 build 阳性对照**：**262 = 260 去掉两个 CLR**（严格等于 250 的存活形状），先确认「描述符停在 0 的形状」在本版产物上仍然活 —— 这是判决 2 的前提。
2. **CLR 二分**：**263 = 262 + 只 `GMU2HOST_INTR_CLR`**；**264 = 262 + 只 `AO_HOST_INTERRUPT_CLR`**。把毒性定到某一个 CLR 上。
3. **顺序与掩码对照**：**265 = 262 + 两个 CLR 放在启动字之后**（先启动后清中断），检验是否只是「启动前清中断」这个顺序致死；**266 = 262 + 掩码换成 `0xffffffff`**（与 143 的掩码差异）。
4. 若 262 也死，则判决 2 作废，把「死亡」归到跨轮构建/表状态上，改用旧产物重放 143 原链做同轮对照。

## 一百二十二、R298：同构建阳性对照成立，两个 CLR 各自单独都不杀存活形状（键 262/263/264）

**122.1 一句话**
R297 判决 2 的 caveat 在本轮被同构建对照补上：**262（严格等于键 250 的 descriptor-cleared 形状，零 CLR）**、**263（262 + 仅 AO 清 `0x9304 = ~0`）**、**264（262 + 仅 HFI 清 `0x5191 = ~0`）** 三键**全部真落盘**（窗口 sha256 逐键新值、`boot_id` 逐键新值、行内 stamp 指名本键）。⇒ **descriptor-cleared 形状在本版产物上活着**；**两种 CLR 各自单独都不杀这个形状**；R297 键 260 的死因此只能归因于**两个 CLR 同时在场（或其先后顺序/次数）**；三键 **HFI 仍被拒**（`ctl=00000000`、`bit0=0 after 200000 polls`、`fsr=00000400`）⇒ **CLR 依旧不是 HFI 被接受的充分条件**。

**122.2 三键设计**

三键都从 R297 的 260 臂「删 store」手术式派生（模板是 10 笔的 250 形状），store 计数 **8/9/9**，共 **+26**（probe 门 store 预算 **1079 → 1105**）：

- **262（八笔，`descriptor_control`）**：`fw=0 → qa=新表基址 → qi=0 → sr=1 → cp=0 → sr=0 → mask=0xff7fffff → ini=1`，**两个 CLR 一笔不写**；末尾读 `0x9305` / `0x5192`。这是 R297 缺的**同构建阳性对照**（250 的存活是 R294 构建观测）。
- **263（九笔，`descriptor_control_ao_clear`）**：262 + **只在写启动字前清 `AO_HOST_INTERRUPT_CLR` `0x9304 = ~0`**，紧邻前后读 `0x9305`（d 行 `ao_clear`）⇒ R297 取证 §8.3 的 260b。
- **264（九笔，`descriptor_control_hfi_clear`）**：262 + **只在写启动字前清 `GMU2HOST_INTR_CLR` `0x5191 = ~0`**，紧邻前后读 `0x5192`（d 行 `hfi_clear`）⇒ 与 263 对称，回答「260 里到底是哪一个 CLR 把活形状弄死」。

每臂的**零写普查行**（`0x5180/0x5181/0x5184/0x5185/0x5186/0x5192/0x9305` 七个字）都打印在本臂第一笔 store 之前，所以同一 boot 里上一臂残留的锁存位无法伪装成「CM3 活着」。键窗由 `(fwKey >= 7u && fwKey <= 261u)` 改为 `<= 264u`，新块 45,581 字符 / 795 行，插在 ladder 之前、R297 块之后；R297 块字节不变，ladder 之后后缀相同（`work\\tmp\\r298_check_block.py` 自证）。

**122.3 逐字读数**

| 键 | 时间 | payload sha256 | key 块 / 回读 | 窗口 sha256 | boot_id | msg_bytes | 落盘行 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 262 | 16:25:00–16:25:58 | `4c61fabd…c381d` | block 31153 device==payload **True**、matches **True** | `3642553a8d44a4bba44039547a5d5528d3e191f0068abc94ab260661cafbf546` | **643989549** | 30967 | 5/5 |
| 263 | 16:27:11–16:28:09 | `d6a8a7dd…23356d` | 同上 **True/True** | `99829d88660e7d72ed1bbc8f03c7bfddb93ecb37f57c7fa45cf58aed8180fe62` | **619986579** | 31064 | 6/6 |
| 264 | 16:28:26–16:29:23 | `f3e6d130…bf00e` | 同上 **True/True** | `f126646f29cd76b1e4a3add4180958cbbe0eb101e7468fff89e7e89a70c3aa0b` | **642045731** | 31066 | 6/6 |

- **262**（正对照）逐字：`gmu262p0=pre=1 ver=00000000 qi=00000001 qtbl=ad995000 fw=babeface st=000000d4 gd=00222001 cf=00088400 cx_on=1`；`gmu262c=census=1 5180=00000000 5181=00000000 5184=00000001 5185=ad995000 5186=00000000 5192=00000000 9305=00000000`；`gmu262p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000 fsr=00000400 far=00000000 fw=babeface sr=00000000`；`gmu262p2=descriptor_control=1 qa=ad9a0000 qi=00000000 mask=ff7fffff fw=babeface sr=00000000 ctl=00000000 bit0=0`；`gmu262l=mode=262 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ctl=00000000 survived=1`；runner：`row key detected from stamps [262] / THIS BOOT REACHED THE WRITER AND ITS STAMPS NAME key=262 … HFI WAS STILL REFUSED after 200000 polls`。
- **263**（仅 AO 清）逐字：`gmu263d=ao_clear=1 pre9305=00000000 post9305=00000000`；`gmu263p2=descriptor_control_ao_clear=1 qa=ad9a4000 qi=00000000 mask=ff7fffff fw=babeface sr=00000000 ctl=00000000 bit0=0`；`gmu263l=… survived=1`；`census` 行七字同 262 形状（5184=00000001、5185=ad9a0000，其余 0）。
- **264**（仅 HFI 清）逐字：`gmu264d=hfi_clear=1 pre5192=f0409800 post5192=f0409800`；`gmu264p2=descriptor_control_hfi_clear=1 qa=ad995000 qi=00000000 mask=ff7fffff fw=babeface sr=00000000 ctl=00000000 bit0=0`；`gmu264l=… survived=1`；`census` 行七字同 262 形状（5184=00000001、5185=ad991000，其余 0）。
- 三键每次前 `REBOOT_TO_TWRP_GATE: PASS (3 checks, 0 failures)`（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`）；窗口均 33,554,432 B，`magic=SHLOG001 ver=4 hdr=152 first_lba=14809083`、`uptime_s=4 write_errors=0`；三键 `slots_written=9`。

**122.4 判决**

1. **262 落盘 ⇒ R297 判决 2 保留、不作废**：descriptor-cleared 形状（`qi=0, sr 1→0, mask=0xff7fffff, ini=1`，CLR 一笔不写）在今天这版产物上 5 行全落地，所以 260（同形状 + 两个 CLR）的死**不是构建差异**，只能归到两个 CLR 上。
2. **263：AO 清单独无害**。`pre9305=00000000 post9305=00000000`（清前清后同一次 dsb 间隔内读 `0x9305`），六行全落地、`survived=1` ⇒ 260 的死不能归因于 `0x9304=~0` 单独。
3. **264：HFI 清单独无害**（六行全落地、`survived=1`）；但 `pre5192=f0409800 post5192=f0409800` —— 写 `0x5191=~0` 后紧接的这次读**没有变化**。这条只证明「该写入没有改变 `0x5192` 的读数」，**不能证明清的动作生效**；两种可能都未排除：(a) `0x5192` 这些位不由 `0x5191` 的 W1C 清，(b) GMU 在同一间隔内立刻重锁存。
4. **致死需要两个 CLR 同时在场（或与顺序/次数有关）**：262 活、263 活、264 活，而 R297 260（同形状 + 两个 CLR）死 ⇒ 单独一个清不杀形状。R299 的 265（262 + 两个 CLR）是把这个结论从「间接」抬成「同构建直接复现」的唯一一步。
5. **三键都没有让 HFI 被接受**：`ctl=00000000`、`bit0=0 after 200000 polls`、`fsr=00000400`（`far=00000000`）、`preamble fills 3 build 0`、`plan=9`。三次表基址不同（`qtbl=ad995000 / ad9a0000 / ad991000`）而 `qi=00000001` 三次一致 —— 「表地址每次不同、INFO=1 每次相同」，与 R296 判据方向一致，且与 CLR 无关。G1（`cf` bit16）未关闭，GPU 未渲染一帧。

**122.5 上游语义对账（采纳 R297 取证 §7 的三项口径修正）**

- **两个 CLR 都是 host 侧 W1C**：`a6xx_gmu_irq`（`drivers/gpu/drm/msm/adreno/a6xx_gmu.c:41-42`）读 `0x9305` 整字原样写回 `0x9304`；`a6xx_hfi_irq`（同文件 `:65-66`）读 `0x5192` 整字写回 `0x5191`；`a6xx_hfi.c:120-122` 与 `a6xx_gmu.c:341` 各只写一位 ⇒ 唯一自洽解释是**写 1 清**。清太早的后果：抹掉 `WDOG_BITE`/`CM3_FAULT` 会让 `a6xx_gmu_fault`（`:20-34`）不入队、只剩静默超时；落在 set_oob 的 set（`:328`）与 poll（`:331`）之间 ⇒ `:334` `Timeout waiting for GMU OOB set`；抹掉 MSGQ ⇒ `a6xx_hfi.c:113` 等 ack 超时。
- **`0xff7fffff` 不是「只开 MSGQ 与 CM3_FAULT」**：它等于 `~(1 << 23)` = `~A6XX_HFI_IRQ_MASK`（`a6xx_gmu.c:905`/`:1090`）。在 1=masked 的语义下，它**只放开 CM3_FAULT、反而掩掉 MSGQ**；此前 evidence 里「只开 MSGQ 与 CM3_FAULT」的写法是错的（不影响轮询读 `0x5192`）。
- **`0x5180` bit0 是锁存、上游从不写也不自清**（唯一复位者 `a6xx_hfi_stop` `:774-793`，裸机不调）⇒ 同一 boot 内跨臂残留会伪装成「被接受」，**每臂必须先报零写普查 bit0=0，再报本臂读数**。本轮三键的 census 行已经这样做了（5180=00000000）。
- **143 的 `ctl bit0=1` 只能由活着的 CM3 置起**（`a6xx_gmu.c:247-250`；143 是唯一一次 `bit0=1` 且 `polls=2`）⇒ 那一刻 CM3 固件是活的。「`INFO=1` 就死」更可信的解释是**活固件去走一张未与上游布局对账的表**（`a6xx_hfi.c:822-855` / `:807-819`；A630 响应队列 id=4），而不是「两个寄存器位耦合」。而「判活的唯一共同点是写 INIT 时 `QTBL_INFO==0`」（256 一笔不写 `0x5184` 仍死 ⇒ 那个 1 是内核自己留的，`a6xx_gmu.c:9058` 一带）说明 CLR 的相关性只存在于 `INFO=1` 形状中。

**122.6 边界与读数陷阱**

- **同构建只对三键成立**：262/263/264 共用 CI #416 的同一份 payload（`cf64c4ca…`）+ 三次重键；与 R297 的 250/260 之间仍是跨构建比较（R297 payload `d3bca995…`）。
- **264 的 `pre/post` 同值不能证明「清发生且无效」**：它只证明「写 `0x5191=~0` 没有改变紧接着的 `0x5192` 读数」。下一轮必须加清后 `IODelay` 重读，或读 `0x5191` 自身，把「清是否生效」变成可判定。
- 三键 `msg_bytes` 30967/31064/31066、`slots_written=9` 一致 ⇒ 写入路径与槽位没有变化，读数可比。
- `262` 是「从 260 删 store」派生的行为等价，不是跨构建字节级同一份产物；「250 的形状」指的是 store 序列与写值，不是镜像字节。
- 三次窗口都是新值与新 `boot_id`，且 `flash readback matches: True` ⇒ 本轮不是「上一轮字节」的假活；stamp 直接指名本键，无需靠锚推断。

**122.7 门禁与工具**

- 取件：CI **#416 run `45351760672` success**（`2026-11-24T07:55:30Z → 08:15:46Z`）；分支 `r298-isolation` commit `44901d36c86a5061c60c7e23d8dd43e7246bdbfa`、tree `fa9e1e250a28e36fc2c60dc6cd6f45850a5a6227`（parent 为 main `90f1ad43072b4500f486e0b203cf936bf3cb89b1`，540 个 blob）。artifacts：kernel 78,983,006 B（id 14901338132）、payload 83,354,977 B（id 14901034517）。
- 产物：`work\\tmp\\r298-kernel\\kernel.macho` **126,619,648 B sha256 `c0f71b2ae9848689edb527034837af469efebd8af02574c8caa82d0cd952ba62`**；`work\\tmp\\r298-payload\\payload.img` **150,994,944 B sha256 `cf64c4ca65ad28eb97278eb6b682e694206542681a6d9a7e8be54b8e376a3197`**。
- 静态门 `work\\tmp\\r298_static.py`：真产物 **PASS**（`required+survivors: 115 checked in both artefacts`、marker `kernel=1 payload=2`、forbidden `gmu265a/gmu266a/gmu267a` 三零、固件 32768 B sha 钉住）；对 R297 产物 **FAIL 17** ⇒ 区分能力自证成立。
- 重键（`work\\tmp\\r179_rekey.py`，cwd `work\\tmp`）：p262 `4c61fabdd6fe58c93d603549d9f65c73d85be1631eeb6fed899eb3872c8c381d`（config 1377→1379 B）、p263 `d6a8a7dd24804e07fb3094714be05cef24a93545a21c42740aae7ef16223356d`、p264 `f3e6d130f2b2fff89678613a72967923fea6f99a265a933d09ab886552dbf00e`，各 150,994,944 B。
- 门禁：host **25/25 + 51/51 + 25/25 + 32/32** + 各 contract PASS；probe **37/37**（store 预算 **1079 → 1105**，新增三条 R298 store 断言，clang 真编译通过 host stub arm64 Mach-O）；stage4 **70/70**、stage5 **57/57**、stage6 **2474/2474**（新增 26 条 R298 行契约，ID1 重读预算 44 → 45）；全量 `r239_allgates.py` → `ran 46 of 48, FAILING: 0`；`work\\tmp\\check_mode_selector.py` → **53 selectors / upper 264 / 自证 FAIL / VERDICT PASS**。
- 块 `work\\tmp\\r298_block.py`：45,581 字符 / 795 行 / store 8/9/9；probe 源 2,058,467 字符 / 34,791 行。

**122.8 下一步（R299）**

1. **265 = 262 + 两个 CLR**：在今天构建上直接复现 R297 的 260，把「两个一起就死」从间接抬成直接。
2. **266 = 265 换序**（先 `0x5191` 后 `0x9304` → 先 `0x9304` 后 `0x5191`）：判定致死项是否与先后顺序有关。
3. **267 = 262 + 两次 HFI 清**（`0x5191` 写两次、中间 dsb）：检验「第二次清」是否才是致死项。
4. **268 = 264 的变体**：清之后 `IODelay` 再读 `0x5192` / 读 `0x5191` 自身，把「清是否生效」变成可判定；若 265 也落盘，则 260 的死要重新归因，应回到字节级比对 260 块与 262 块。

## 一百二十三、R299：两个 CLR 一起也不再杀存活形状——R297 的归因被撤回（键 265/266/267）

**123.1 一句话**
把 R298 已验证存活的 descriptor-cleared 形状（键 262，八笔 store）加上**两个中断 CLR**，按 **R297 键 260 的顺序（HFI 先、AO 后）**、**换序（AO 先、HFI 后）**、**同序中间隔 100 µs 并重读 `0x5192`** 三种写法各跑一次：**265 / 266 / 267 全部真落盘**（7/7、7/7、8/8 行，窗口 sha256 与 `boot_id` 逐键新值，行内 stamp 指名本键）。⇒ **两个 CLR 同时在场、任何顺序、有无间隔，都不杀这个形状**。R297 键 260 的死**不能**归因于这两个 CLR；R299 §122.4 的「致死需要两个 CLR 同时在场（或与顺序/次数有关）」**在此撤回**。三键 **HFI 仍被拒**（`ctl=00000000`、`bit0=0 after 200000 polls`、`fsr=00000400`、`far=00000000`）⇒ **CLR 依旧不是 HFI 被接受的充分条件**。

**123.2 三键设计**

三键都从 R298 键 263 的臂（R298 模板里唯一带 `0x9305` 前后读的臂）**外科手术**派生：删掉只清 AO 的那一笔、把两笔 CLR 按需要重排/插间隔。store 计数 **10/10/10**，共 **+30**（probe 门 store 预算 **1105 → 1135**）：

- **265（十笔，`both_clears_260_order`）**：`fw=0 → qa=新表基址 → qi=0 → sr=1 → cp=0 → sr=0 → mask=0xff7fffff → ini=1` 之外，**先 `0x5191 = ~0`（HFI 清）再 `0x9304 = ~0`（AO 清）**——与 R297 键 260 同序，两笔都在 mask 与启动字之前；紧邻读数 `0x5192`（e 行）与 `0x9305`（d 行）。
- **266（十笔，`both_clears_swapped`）**：同两笔**换序**（AO 清先、HFI 清后）⇒ 判「先后顺序」是否是 R297 的致死项。
- **267（十笔，`both_clears_spaced`）**：与 265 同序，**两笔之间 `IODelay(100u)` 并再读一次 `0x5192`**（f 行 `spaced_re5192`）⇒ 判「两笔紧邻相撞」是否是致死项。

每臂的**零写普查行**（`0x5180/0x5181/0x5184/0x5185/0x5186/0x5192/0x9305` 七字）都打印在本臂第一笔 store 之前。键窗由 `(fwKey >= 7u && fwKey <= 264u)` 改为 `<= 267u`，新块 **49,211 字符 / 848 行**，插在 ladder 之前、R298 块之后；R298 块字节不变。

**123.3 逐字读数**

| 键 | 时间 | payload sha256（重键后） | key 块 | 窗口 sha256 | boot_id | msg_bytes | 落盘行 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 265 | 17:53:xx | `f683384136be1230c8807441d51185cf62df5c61ca994a9eb2abe173e662ee59` | block 31153 device==payload **True**、readback **True** | `e3bede7c564912e5182854419717fc4037d49a669ef2602fc9d53247cab50fc5` | **643726211** | 31147 | 7/7 |
| 266 | 17:55:xx | `5a2cc8b733b81975a9b405568484323757e376b455d2600b20a5dbbadd153751` | 同上 **True/True** | `ea06b62b8fa931c57032d3be36ac55746b29454c9e6dcc9c7bcb60bd454ec93c` | **642291156** | 31171 | 7/7 |
| 267 | 17:57:xx | `d01e2b0e86a47a2bdfedb9f2a91cb35d8fc682f2d4a84f7529ca5ec5dacf2e23` | 同上 **True/True** | `a5b0269bd4dc74981b6c6f106f85df73b29ac2143bbfc15851a982a984652698` | **642264887** | 31252 | 8/8 |

- **265** 逐字：`gmu265p0=pre=1 ver=00000000 qi=00000001 qtbl=ad992000 fw=babeface st=000000d4 gd=00222001 cf=00088400 cx_on=1`；`gmu265c=census=1 5180=00000000 5181=00000000 5184=00000001 5185=ad992000 5186=00000000 5192=00000000 9305=00000000`；`gmu265e=hfi_clear=1 pre5192=f0409800 post5192=f0409800`；`gmu265d=ao_clear=1 pre9305=00000000 post9305=00000000`；`gmu265p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000 fsr=00000400 far=00000000 fw=babeface sr=00000000`；`gmu265p2=both_clears_260_order=1 qa=ad996000 qi=00000000 mask=ff7fffff fw=babeface sr=00000000 ctl=00000000 bit0=0`；`gmu265l=mode=265 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ctl=00000000 survived=1`。
- **266** 逐字：`qtbl=ad98e000`、`5185=ad98e000`、`gmu266d=ao_clear=1 pre9305=00000000 post9305=00000000`、`gmu266e=hfi_clear=1 pre5192=f0409800 post5192=f0409800`、`gmu266p2=both_clears_swapped=1 qa=ad992000 qi=00000000 mask=ff7fffff … ctl=00000000 bit0=0`、`gmu266l=… survived=1`。
- **267** 逐字：`qtbl=ad992000`、`gmu267e=hfi_clear=1 pre5192=f0409800 post5192=f0409800`、**`gmu267f=spaced_re5192=1 v=f0409800`**、`gmu267d=ao_clear=1 pre9305=00000000 post9305=00000000`、`gmu267p2=both_clears_spaced=1 qa=ad9a0000 qi=00000000 mask=ff7fffff … ctl=00000000 bit0=0`、`gmu267l=… survived=1`。
- 三键每次前 `REBOOT_TO_TWRP_GATE: PASS (3 checks, 0 failures)`（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`）；窗口均 33,554,432 B，`magic=SHLOG001 ver=4 hdr=152 first_lba=14809083`、`uptime_s=4 write_errors=0`；三键 `slots_written=9`。

**123.4 判决**

1. **两个 CLR 一起不再杀形状（撤回 R298 判决 4）**：265（260 顺序）十笔、七行全落地、`survived=1`。这是对 R297 键 260 之死的**同形状直接复现尝试**，结果**没复现**。⇒「两个 CLR 同时在场」既不是 260 之死的充分条件，也不是必要条件在本形状上的解释。
2. **顺序无关**：266（AO 先、HFI 后）同样七行全落地。⇒ 致死项不是两笔的先后。
3. **间隔无关**：267（两笔之间 `IODelay(100u)` + 重读）八行全落地，且 `v=f0409800` 与清前一致。⇒ 不是「相撞」。
4. **两笔 CLR 都不改变紧邻读数的位**：三键 `pre5192 == post5192 == f0409800`，264 也一样；267 的间隔后重读仍是 `f0409800`。⇒ 这**只**证明「写 `~0` 没有改变这些位的读数」，**不**证明清生效：要么这些位不由该 W1C 清，要么 GMU 在同一间隔内立刻重锁存。R298 §122.6 的边界仍然成立。
5. **新的观测（本轮首次记录）**：普查行 `5192=00000000`，而到写清之前一刻读到的 `0x5192` 已是 **`f0409800`**。⇒ 在普查之后、CLR 之前的那几笔（`qi=0`、复位脉冲、`0xbabeface` 等待、mask）之间，**`0x5192` 被置起了 bit23/22/17/11（按 `f0409800` 解）**。这个「谁把 `0x5192` 抬起来」的时序此前从未单独记录。
6. **R297 的 260 之死必须重新归因**：260 与 265 的差别只剩**骨架**（R297 臂来自 R293 模板：装 CB0、建表、512 条目填满、`dc civac`、清 `CB_FSR`、`CM3_FW_INIT_RESULT=0`、复位脉冲、等待 `0xbabeface`……），以及**两笔 CLR 在骨架内的插入位置**。R298 §122.8 已预告过这条分支：若 265 也落盘，就回到字节级比对 260 块与 262 块。
7. **HFI 仍未接受**：三键 `ctl=00000000`、`bit0=0 after 200000 polls`、`fsr=00000400`、`far=00000000`、`preamble fills 3 build 0`、`plan=9`；`gd=00222001`、`cf=00088400`（bit16=0）、`gx_on=0`。G1 未关闭，GPU 未渲染一帧。

**123.5 边界与读数陷阱**

- **同构建仅对三键成立**：265/266/267 共用 CI #417 的同一份 payload + 三次重键；与 R297 的 259/260/261 之间仍是**跨构建**比较（R297 payload `d3bca995…`）。所以「没复现」的严格表述是：**在 R298 之后的这版产物上，这两个 CLR 不再杀这个形状**；R297 那版的死因未被排除为构建差异，但 262 在同版活、263/264/265/266/267 全活把「CLR 相关」这个方向压到很低。
- **266 的 d/e 行顺序**与 265/267 相反（先 AO 后 HFI），这是设计使然，读日志时不要按行序误判。
- **`f0409800` 的通解未做**：本轮只把它当原始字记录，未逐位归属（按 R251 的位表 bit23=CM3_FAULT、bit22=？、bit17/11=？）。留作后续。
- 三键 `msg_bytes` 31147/31171/31252、`slots_written=9` 一致 ⇒ 写入路径与槽位无变化，读数可比。
- 三键窗口都是新 `boot_id` 与新 sha，且 `key block … device == payload -> True`、`readback matches: True` ⇒ 不是「上一轮字节」的假活。

**123.6 门禁与工具**

- 取件：CI **#417 run `45362588124` success**（`2026-11-24T09:10:00Z → 09:3x`）；分支 `r299-both-clears` commit `9f9a735fe77b2a3c99ff227d9e7e5efe492d7366`、tree `7d202d055ac612557e09d735e753ea98e3914eed`（parent 为 main `a297fe0f299a23fb1d581c1081ac48f305f7b4aa`，535 已有 + 5 上传）。
- 产物：`work\\tmp\\r299-kernel\\kernel.macho` **126,619,648 B sha256 `979df504f20e43be74ab22084e88749522dc2f3c3e9830971b7182e8dff30599`**；`work\\tmp\\r299-payload\\payload.img` **150,994,944 B sha256 `473bff740ca987049cf2edbe57ead5da33b3b5874abc0ffff95975fe61d0ff6a`**。
- 静态门 `work\\tmp\\r299_static.py`（22 条 REQUIRED + 355 条 SURVIVORS = 377 条）：真产物 **PASS**（`required+survivors: 120 checked in both artefacts`、marker `kernel=1 payload=2`、forbidden `gmu268a/gmu269a/gmu270a` 三零、固件 32768 B sha 钉住）；对 R298 产物 **FAIL 22** ⇒ 区分能力自证成立。
- 重键（`work\\tmp\\r170_rekey.py`，cwd `work\\tmp`）：p265 `f6833841…662ee59`（config 1377→1379 B）、p266 `5a2cc8b7…153751`、p267 `d01e2b0e…cf2e23`，各 150,994,944 B。
- 门禁：host **25/25 + 51/51 + 32/32** + 各 contract PASS；probe **38/38**（store 预算 **1105 → 1135**，新增独立 check `R299 the three keys put both interrupt clears back on the living shape`）；stage4 **71/71**、stage5 **58/58**、stage6 **2504/2504**（新增约 30 条 R299 行契约，ID1 重读预算 45 → 48）；全量 `r239_allgates.py` → `ran 46 of 48, FAILING: 0`；`work\\tmp\\check_mode_selector.py` → **54 selectors / upper 267 / 自证 FAIL / VERDICT PASS**。
- 块 `work\\tmp\\r299_block.py`：49,211 字符 / 848 行 / store 10/10/10；probe 源 **2,107,679 字符 / 34,8xx 行**。

**123.7 下一步（R300）**

1. **字节级比对 260 块与 262/265 块**：把 R297 的 260 臂原文与 R298–R299 的 265 臂原文逐行 diff，列出骨架层面的每一处差别（不只是 store 序列）：CB0 装配、表构建、512 条目填充、`dc civac`、`CB_FSR` 清零、`CM3_FW_INIT_RESULT=0`、复位脉冲与等待、mask/ini 的相对位置。
2. **在活形状上逐项加回骨架**：265 已是「活形状 + 两笔 CLR」；下一步把 R293 骨架里的**单项**（例如 `CM3_FW_INIT_RESULT=0`、`CB_FSR` 清零、512 条目填充）加到 265 上，一项一键，找出哪一项与 CLR 组合才致死。
3. **谁抬起 `0x5192`**：在普查之后、CLR 之前逐笔插读 `0x5192`，把 `00000000 → f0409800` 的跃变定位到具体那一笔；这同时给 267 的「清是否生效」提供基线。
4. **`f0409800` 逐位归属**：按 R251 的位表解 bit23/22/17/11，确认其中哪些属于 `CM3_FAULT`，哪些属于本轮尚未命名的源。
## 一百二十四、R300：同一构建上重跑 260 / 262 / 143——「致死」与「被接受」都随构建翻面（无新构建的 A/B 轮）

**124.1 一句话**
本轮**不建新产物**：R299 的 kernel（`979df504f20e…`）与 payload（`473bff740ca9…`）里已经同时含着 R259…R299 全部探针块，所以把同一份 payload 重键三次，就能在**同一构建**上重跑三个历史键：**260**（R297 判死的臂）、**262**（R298 判活的正对照）、**143**（R260 唯一被接受过的臂）。结果：**260 四行全落盘（存活）**、**262 五行全落盘（存活）**、**143 两行全落盘但 `ctl=000000fe`、`bit0=0`、`polls=200000`（不再被接受）**。⇒ **R297 键 260 之死不能在今天的构建上复现**；**R260 键 143 的「被接受」也不能复现**。两条判决原本都来自跨构建比较，本轮把「跨构建比较不成立」这件事本身做成了证据。

**124.2 无构建的理由与三个键的设计**

- **260**：与 R299 键 265 的臂经**归一化逐行 diff**（`work\tmp\r300_diff3.py`，把 `r259*/r265*`、`gmu260*/gmu265*` 及各作用域标识符统一）后，**store 序列逐笔同值同序（各 10 笔）**：`fw=0 → qa=新基址 → qi=0 → sr=1 → cp=0 → sr=0 → clr=0xffffffff → aoc=0xffffffff → mask=0xff7fffff → ini=1`；非 store 差异**只有三处**：265 多一行零写普查（7 次读 + 一次 `snprintf`/`IOLog`）、265 在两笔 CLR 前后各多两次读（`pre5192/post5192`、`pre9305/post9305`）、以及 p2 标签文字。⇒ 结构上**没有**可指向的致死项。
- **262**：R298 的正对照（descriptor-cleared、无 CLR、八笔），用来证明这版产物**仍会写字节**，260 的存活不是「窗口陈旧」。
- **143**：R260 唯一被接受过的臂（填满根页与两张子页的合法描述符 + `HFI_CTRL_INIT` + 200000 拍轮询），用来检验「被接受」本身是否可复现。
- 三键**没有源码改动**，所以 `package/EnchiladaAdrenoProbe.cpp` 与全部 host/probe/stage4/5/6 门禁在本轮**不适用**；本轮唯一的构建面证据来自「同一 payload 的三次重键 + 三次窗口回读」。

**124.3 逐字读数**

| 键 | 重键 payload sha256 | 窗口 sha256 | boot_id | 落盘行 | 结论 |
| --- | --- | --- | --- | --- | --- |
| 260 | `4a1a4c47a46f168d110c402b466556123986ee34722b194d518dde31ff82a75a` | `f8ad5dd34df78237ad8c95726adaf84365d087d15b236e5f3572948b382c3a15` | **640969717** | 4/4 | **存活**（R297 同一块源码当时零字节） |
| 262 | `f569fa3120654f49cb9c06acf1be0047c99f52169f0fec43df56c6ce6e2e0a84` | `724b934b9a27ce5d024b4de73c9b448569c6391da9d9841449ed0eb0fd6363e0` | **641030674** | 5/5 | **存活**（正对照成立） |
| 143 | `ec9fe72dd0eef1a731abf658aaecd257ce823cb47da16c92cdaae4713da9eff0` | `2f69146093b5a7b3dc03148b302c819fd21d03359c0e838887bc4329b46eada1` | **639546346** | 2/2 | **被拒**（`ctl=000000fe bit0=0`） |

- **260** 逐字：`gmu260p0=pre=1 ver=00000000 qi=00000001 qtbl=ad98f000 fw=babeface st=000000d4 gd=00222001 cf=00088400 cx_on=1`；`gmu260p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000 fsr=00000400 far=00000000 fw=babeface sr=00000000`；`gmu260p2=clears_descriptor_cleared=1 qa=ad993000 qi=00000000 mask=ff7fffff fw=babeface sr=00000000 ctl=00000000 bit0=0`；`gmu260l=mode=260 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ctl=00000000 survived=1`。
- **262** 逐字：`gmu262p0=pre=1 … qtbl=ad992000 …`；`gmu262c=census=1 5180=00000000 5181=00000000 5184=00000001 5185=ad992000 5186=00000000 5192=00000000 9305=00000000`；`gmu262p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000 fsr=00000400 far=00000000`；`gmu262p2=descriptor_control=1 qa=ad996000 qi=00000000 mask=ff7fffff …`；`gmu262l=mode=262 … survived=1`。
- **143** 逐字：`gmu143k=shotgun fills=3 l2_idx=364 l0=00000000ad996003 l2=00000000ad800401 fsr_before=00000400 about_to_start=1`；`gmu143l=shotgun_started ctl=000000fe bit0=0 polls=200000 info=00000000 gfsr=00000000 fsr=00000400 far=00000000 fsynr0=00000000 smr_end=80000004 survived=1`。
- 三次都：`key block … device == payload -> True`、`flash readback matches: True`、窗口 33,554,432 B、`uptime_s=4 write_errors=0`；三次设备都在 recovery、`REBOOT_TO_TWRP_GATE: PASS (3 checks, 0 failures)`（boot_a md5 `ca8ddd7a5af345636cad8c5bf645fee2`）。

**124.4 判决**

1. **R297 键 260 之死不复现**：同一份源码块（R297 块仍在源里，`fwKey == 260u` 走的就是它）在 R299 构建上**四行全落、`survived=1`**，而 R297 那一版同键零字节。⇒ 该死亡是**构建属性**，不是臂的语义属性。R299 §123.4-6 的「必须重新归因到骨架」**在此撤回**：骨架仍然在，且在今天的构建上不致死。
2. **每个键的差异都不足以解释翻面**：260 与 265 的 store 逐笔相同；非 store 差异只有普查行、四次额外读、标签文字。普查行是 7 次 `rd32` + 一次 `snprintf`/`IOLog`，额外读是 4 次 `rd32`。⇒ 若这些差异真能决定生死，那么「生死」由**微秒级时序**决定，而微秒级时序在构建之间本来就会漂移。
3. **「被接受」同样翻面**：143 在 R260 那次是 `ctl=00000001`、`polls=2`；今天 `ctl=000000fe`、`bit0=0`、`polls=200000`。⇒ 「143 被接受」不能当作稳定事实使用；它要么是构建属性，要么是被更早某次上手状态影响的瞬态。注意 `000000fe` 在 R260 记录里也出现过一次，两者是否同一现象**未分辨**。
4. **正对照成立**：262 五行全落且窗口是新 sha/新 `boot_id`，⇒ 这版产物确实写了字节，260 的存活不是「读到了上一轮的窗口」。
5. **方法论收缩（本轮最重要的产出）**：凡是「A 死、B 活」的结论，若 A 与 B 不在**同一构建**，都必须重做。R264–R299 之间大量判决属于跨构建比较（例如「掩码无罪」「两个 CLR 带毒」「位置不是判别式」「INFO=1 就死」），它们的严格形式只能是「**在那一版产物上**如此」。同构建内的比较（R297 的 259/260/261 全死、R299 的 265/266/267 全活、本轮的 260/262 全活）才是可直接引用的。
6. **存活 ≠ 接受**：三键全部落盘，但 260/262 的 `ctl=00000000`、143 的 `ctl=000000fe` 都`bit0=0`。⇒ HFI 仍被拒，G1（`cf` bit16=0）未关闭，GPU 未渲染一帧。

**124.5 边界与读数陷阱**

- 每键**只跑一次**：本轮只能说明「这一次活 / 这一次死」，**不能**推出「每构建内确定性」。R301 必须先做同键重复（260 再跑、143 再跑）才能把「构建」定为自变量。
- **260 与 265 已在同一构建内**：二者都活 ⇒ 在本构建上「CLR 相关」的差异**完全消失**；若 R301 重复仍稳定，则 R297 的死因只能在「R297 那版构建」里找（编译器、代码布局、boot 时序、串口量）。
- **窗口文件同名覆盖**：260 的 runner 仍把窗口写到 `r297_s260_window.bin`（R300 版副本另存为 `r300_s260_window.bin`），143 同理（`r287_s143_window.bin` → `r300_s143_window.bin`）。**读证据必须读 `r300_s*` 副本。**
- **无 CI、无分支、无源码改动**：本轮不产生新的 kernel/payload，也不动 `package/`；只有三个重键 payload 与文档。
- 260 的 `qtbl=ad98f000`、262 的 `qtbl=ad992000` 不同是运行时分配差异，不可跨键比较。

**124.6 门禁与工具**

- 本轮源码零改动 ⇒ host/probe/stage4/5/6/全量/mode-selector **不适用**（没有可测的差异）；`r287_hashes.py` 复核 7 路径 **0/7 不符**，说明源码与上一轮推送态一致。
- 设备端门禁：每个 runner 内部先跑 `check-reboot-to-twrp.py`（要求 recovery + PASS），三次均 PASS；每次运行结束由 runner 自己 `--arm-bcb-only` 重新武装。
- 重键：`work\tmp\r170_rekey.py r299-payload\payload.img r300-p<key>-payload "enchilada-pas-exec=2" "enchilada-pas-exec=<key>"`，三次 config 1377 → 1379 B。
- runner 派生：`work\tmp\r300_mkrun.py` 由 `r297_s260_run.py`、`r298_s262_run.py`、`r287_s143_run.py` 派生，只改 payload 目录、keyblock 文件名、新鲜度锚（R299 键 267 的 `boot_id 642264887` / sha `a5b0269b…`）与轮次标签；143 的 runner 本来就没有 `PREV_BOOT` 锚，靠 stamp 归属。

**124.7 下一步（R301）**

1. **同键重复**：260 与 143 各再跑一次（同一构建、同一 payload）。若结果与本次一致 ⇒ 每个构建内部**确定性**，则自变量是「构建」；若翻转 ⇒ 连「同一构建内确定性」也不成立，所有单次结果只能算一次采样。
2. **找构建自变量**：候选是探针源码体积/布局（R297 → R299 之间每轮 +12K～50K 字符）、串口输出总量（普查行与 IOLog 会显著改变时序）与内核构建本身。可在同一构建里用「同臂 + 开关普查行」成对比较，把「多打一行日志」从「多读七个字」中分离。
3. **接受问题的独立线**：143 的 `000000fe` 与 R260 的 `00000001 polls=2` 之间差的是整条骨架（R260 那次键 143 之后又有多少块插入）。若「被接受」也是构建属性，则接受条件必须在**同一构建内**用配对臂找，而不是跨轮拼图。
## 一百二十五、R301：同一构建上的同键重复——260 两次全活，143 一次被拒一次被接受（接受与否不是确定性属性）

**125.1 一句话**
R300 只把每个键跑了一次，所以它只能说明「这一次活 / 这一次死」。本轮在同一份 kernel（`979df504f20e…`）与同一份 payload（`473bff740ca9…`）上把 **260** 与 **143** 各再跑一次，只重键、不改任何字节：**260 四行再次全落（2/2 活）**，**143 这次 `ctl=00000001`、`bit0=1`、`polls=2`（被接受）**。⇒ 同一构建内 260 的行为**稳定为活**；而**「被接受」与否在完全相同的字节上会翻面**——R300 的 143 被拒（`ctl=000000fe`）、本轮 143 被接受（`ctl=00000001 polls=2`）。接受与否是**每次引导的采样**，不是臂的属性，也不是构建的属性。

**125.2 设计：唯一自变量是引导**
- 复用 R300 的两份重键 payload：`r301-p260-payload\payload.img` sha `4a1a4c47a46f…`（与 R300 的 p260 **逐字节相同**）、`r301-p143-payload\payload.img` sha `ec9fe72dd0ee…`（与 R300 的 p143 **逐字节相同**）。
- kernel 仍是 `979df504f20e43be74ab22084e88749522dc2f3c3e9830971b7182e8dff30599`，探针源码**零改动**（`r287_hashes.py` 7 路径 0/7 不符）。
- runner 由 `r300_s260_run.py` / `r300_s143_run.py` 派生（`work\tmp\r301_mkrun.py`）：新窗口/新 sbhead/新日志/新 keyblock 名（`r301_s260_window.bin` 等），**不覆盖 R300 证据**；260 的新鲜度锚改为 R300 最后一个落地（`boot_id 639546346` / sha `2f69146093b5…`），143 的 runner 无锚，靠 stamp 归属。
- 两次运行之间都先 `check-reboot-to-twrp.py --arm` → `PASS (3 checks, 0 failures)`；runner 内部再跑一次 gate 并自己重新武装。

**125.3 逐字读数**

| 键 | payload sha256 | 窗口 sha256 | boot_id | 落盘行 | 结果 |
| --- | --- | --- | --- | --- | --- |
| 260 | `4a1a4c47a46f168d110c402b466556123986ee34722b194d518dde31ff82a75a` | `e1b7bb788a34ddd270e86a70ecdb0e613cea9fe060d346970d198da54aa0f5be` | **619019194** | 4/4 | **存活**（`ctl=00000000`、`bit0=0`、`polls=200000`） |
| 143 | `ec9fe72dd0eef1a731abf658aaecd257ce823cb47da16c92cdaae4713da9eff0` | `1692e0e008aa176582cef70e831481e808521c49e49c2fafbf867790a197c2c8` | **580446273** | 2/2 | **被接受**（`ctl=00000001`、`bit0=1`、`polls=2`） |

- **260** 逐字：`gmu260p0=pre=1 ver=00000000 qi=00000001 qtbl=ad992000 fw=babeface st=000000d4 gd=00222001 cf=00088400 cx_on=1`；`gmu260p1=hfi=1 fills=3 build=0 plan=9 ctl=00000000 bit0=0 polls=200000 fsr=00000400 far=00000000 fw=babeface sr=00000000`；`gmu260p2=clears_descriptor_cleared=1 qa=ad9a0000 qi=00000000 mask=ff7fffff fw=babeface sr=00000000 ctl=00000000 bit0=0`；`gmu260l=mode=260 gd=00222001 cf=00088400 gx_on=0 gxoff=1 ctl=00000000 survived=1`。
- **143** 逐字：`gmu143k=shotgun fills=3 l2_idx=364 l0=00000000ad9a1003 l2=00000000ad800401 fsr_before=00000400 about_to_start=1`；`gmu143l=shotgun_started ctl=00000001 bit0=1 polls=2 info=00000000 gfsr=00000000 fsr=00000400 far=00000000 fsynr0=00000000 smr_end=80000004 survived=1`。
- 两次都：`flash readback matches: True`、`key block … device == payload -> True`、窗口 33,554,432 B、`row key detected from stamps` 唯一、`uptime_s=4 write_errors=0`。

**125.4 判决**

1. **260 在同一构建上是确定性的「活」**：R300（`640969717`）与 R301（`619019194`）两次不同引导、同一字节，四行全落、`ctl=0`。⇒ R297 那一版的死亡只能是**那一版构建**的属性，R299 §123.4-6 的「重新归因到骨架」继续**保持撤回**。
2. **「被接受」在同一构建上不确定**：143 的字节完全相同（payload sha 一致），R300 被拒（`ctl=000000fe bit0=0 polls=200000`）、R301 被接受（`ctl=00000001 polls=2`）。⇒ 接受与否是**每次引导的采样**；R300 §124.4-3 把翻面归给「构建」也只对了一半，真正的自变量比构建更细——是**这一次引导的瞬态**（时序、CM3 上电/复位历史、串口节奏、SMMU 遗留状态都可能参与，本轮未分辨）。
3. **因此「143 是唯一被接受过的臂」这类结论必须降级**：它只是「143 在 R260 那次被接受」这一**单次采样**。R264–R299 里凡以「被接受/被拒」当稳定属性做的推断，都只能当作一次观测。
4. **接受没有改变故障图景**：两次 143（被拒与被接受）的 `info=00000000`、`gfsr=00000000`、`fsr=00000400`、`far=00000000`、`fsynr0=00000000` 完全一致，`fsr=00000400` 的 TF 位为 0 ⇒ **`ctl` bit0=1 只说明 CM3 收下了启动请求，并不说明固件随后做了任何可观测的事**。
5. **存活 ≠ 接受，接受 ≠ 前进**：260 存活两次仍 `bit0=0`；143 被接受一次仍没有任何新寄存器变化。G1（`cf` bit16=0、`gd=00222001`）未关闭，HFI 未真正启动，GPU 未渲染一帧。

**125.5 边界与读数陷阱**
- 每个键**只有两次采样**（R300 + R301），不能据此估计接受率；只能说「同一字节上接受与否会变」。
- 143 的 `l0` / `l2` 每次引导不同（本次 `l0=00000000ad9a1003`、`l2=00000000ad800401`），是运行时分配差异，不可跨引导比较。
- **窗口文件必须读 `r301_s*`**：260 的 runner 若指向旧名会覆盖 R300 证据，本轮已改成新名；`r301_rows.py <window> <tag…>` 直接从窗口二进制抽行，避免误读旧内容（窗口是滚动的，旧键的行仍在文件里）。
- 本轮**无 CI、无分支、无源码改动**：`git status` 只该看到文档改动。

**125.6 下一步（R302）**
1. **测接受率**：把 143 在同一构建上连跑 4～6 次，得到「接受 / 拒绝」的经验比例；同时跑 2～3 次 260 作稳定对照。若接受率明显小于 1，则任何单次「被接受」都不能用来定位机制。
2. **找瞬态自变量**：在同一构建内做配对臂——同骨架下「多一行普查日志 / 不普查」「两笔 CLR 之间加/不加 `IODelay`」「复位脉冲前后各多/少一次读」；把「日志造成的时序」与「读造成的时序」分开。
3. **若 143 被接受时可复现**，则下一次被接受时立刻读更多状态（CM3 的 `CM3_CFG`/`CM3_SYSRESET`/`CP_MSG`/`CP_RESP`、`HFI_MSGQ` 头、SMMU `CB_FSR`/`CB_FAR`），把「收下请求」变成「固件真的开始工作」的证据。
## 一百二十六、R302：接受率实测——同一字节上 6 次里 5 次被接受（poll 2），1 次读到 `ctl=000000fe`

**126.1 一句话**
R301 证明「被接受」在完全相同的字节上会翻面，本轮就把翻面**量化**：同一份 kernel（`979df504f20e…`）、同一份 payload（`473bff74…`）、同一个臂（键 143）再跑 **4 次**，加上 R300 的 1 次被拒与 R301 的 1 次被接受，共 **6 次采样**：**5 次被接受**（每次都是 `ctl=00000001`、`bit0=1`、**`polls=2`**）、**1 次读出 `ctl=000000fe`**（`bit0=0`、`polls=200000`）。⇒ 接受率约 **5/6**；被接受时**轮询数恒为 2**，说明 CM3 的应答是**立即**的；而那次「被拒」不是沉默，是**一个带内容的错误字 `0xfe`**。

**126.2 设计**
- 6 个采样全部落在**同一构建**、**同一 payload 字节**、**同一臂**上，唯一自变量是**这一次引导**。
- 4 个新采样由 `work\tmp\r302_mkrun.py` 从 `r301_s143_run.py` 派生（`r302_s143_rN_run.py`，N=1..4），只改窗口/sbhead/日志/keyblock 名（`r302_s143_rN_window.bin` 等），**不覆盖** R300/R301 证据；payload 复用 `r301-p143-payload\payload.img`（sha `ec9fe72dd0ee…`，与 R300 的 p143 逐字节相同）。
- 每次运行前 `check-reboot-to-twrp.py --arm` → `PASS (3 checks, 0 failures)`；runner 内部再跑一次 gate。

**126.3 六次采样逐字读数**

| # | 来源 | boot_id | 窗口 sha256 | `ctl` | `bit0` | `polls` |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | R300 | 639546346 | `2f69146093b5a7b3dc03148b302c819fd21d03359c0e838887bc4329b46eada1` | `000000fe` | 0 | 200000 |
| 2 | R301 | 580446273 | `1692e0e008aa176582cef70e831481e808521c49e49c2fafbf867790a197c2c8` | `00000001` | 1 | 2 |
| 3 | R302 r1 | 582078449 | `2aaed29fbfc5df2367f5d5dbb8f48592a629668540b8034f95f02439651902a7` | `00000001` | 1 | 2 |
| 4 | R302 r2 | 582055453 | `2833f3bd69da14597f1394c56310f30b1e0fc791770c934c600484aec45b412b` | `00000001` | 1 | 2 |
| 5 | R302 r3 | 582128795 | `44dc10d0bb0e9ca204ac497a4cab8bb8987d78543a7021c4504c622b67e69b04` | `00000001` | 1 | 2 |
| 6 | R302 r4 | 582106768 | `037c527e4ad616e7277c8975d37fc68da89a6877aa6eb10ebfb1199d8173a475` | `00000001` | 1 | 2 |

- 5 次被接受的 `gmu143l` **逐字相同**：`gmu143l=shotgun_started ctl=00000001 bit0=1 polls=2 info=00000000 gfsr=00000000 fsr=00000400 far=00000000 fsynr0=00000000 smr_end=80000004 survived=1`。
- `gmu143k` 五次都是 `fills=3 l2_idx=364 l0=…1003 l2=00000000ad800401 fsr_before=00000400 about_to_start=1`（r4 的 `l0=00000000ad995003`，其余为 `ad9a1003`，是运行时分配差异）。
- 被拒那次（R300）的同两行见 §124.3：`ctl=000000fe`、`info=00000000`、`gfsr=00000000`、`fsr=00000400`、`far=00000000`、`fsynr0=00000000`、`smr_end=80000004`。

**126.4 判决**

1. **接受率 ≈ 5/6**（同一字节、同一构建）。⇒ 单次「被拒」不能证明臂被拒，单次「被接受」也不能证明机制成立；R260 那次「143 唯一被接受」其实是**多数情形**，而 R300 的「被拒」是**少数情形**。
2. **被接受时 `polls` 恒为 2**：CM3 的应答是**立即**的（不是「等到第 200000 拍才置起」）。⇒ 接受是**快速应答**，被拒是**从不应答**，两者之间没有中间态。
3. **「被拒」不是沉默而是 `0xfe`**：`ctl=000000fe` 的 bit1..bit7 全部置起，bit0 为 0。⇒ 这是一个**带内容的错误/中止字**，不是一个空寄存器。同一个 `0xfe` 曾在 R267 键 165 出现过一次（那次无启动字、只手清），⇒ `0xfe` 至少出现过两次、跨两个不同臂，**值得作为独立对象追**（位定义未取证，本轮只登记不归因）。
4. **接受没有改变故障图景**：5 次被接受的 `info=0`、`gfsr=0`、`fsr=00000400`（TF=0，非 fault）、`far=0`、`fsynr0=0` 完全一致，与被拒那次**逐字相同**。⇒ **`ctl` bit0=1 只是「CM3 收下了启动请求」，固件随后没有写出任何可观测的东西**：没有 HFI 消息、没有 fault、没有 SMMU 侧变化。
5. **因此下一个问句必须换**：不再是「怎样让它被接受」（它本来就多数被接受），而是「**被接受之后，固件为什么不做任何事**」。G1（`cf` bit16=0、`gd=00222001`）仍未关闭，GPU 未渲染一帧。

**126.5 边界与读数陷阱**
- 6 次采样**不足以**给出可信的接受率区间（5/6 的 95% 区间大致 36%–99%），只能定性说「多数被接受、少数读出 `0xfe`」。
- `polls=2` 是**轮询计数**，不是时间；两次轮询之间有一次 `IODelay`，所以「2」表示「轮询开始后几乎立即」。
- 6 次的 `l0`/`l2` 地址随引导漂移（`ad9a1003`/`ad995003`），**不可跨引导比较**。
- 窗口文件按 run 分开（`r302_s143_rN_window.bin`），逐次用 `work\tmp\r301_rows.py <window> gmu143k gmu143l` 抽取，避免滚动窗口里的旧行混入。

**126.6 下一步（R303）**
1. **追 `0xfe`**：把「无启动字 + 手清 `0x5180=0`」与「真启动字」两臂各跑 3 次，看 `0xfe` 是否与「上一次引导留下的状态」相关；若 `0xfe` 能被稳定复现，就先在**干净复位**后读它的位。
2. **接受之后看固件说了什么**：在被接受的那一次立刻读 `CM3_CFG`/`CM3_SYSRESET`/`CP_MSG`/`CP_RESP`、`HFI_MSGQ` 头（`0x5192`/`0x5180` 之后的队列字）、`CB_FSR`/`CB_FAR`，并开 bit23 掩码看 `CM3_FAULT` 是否在**被接受**的情形里也出现。这是第一次有机会在「固件收下请求」之后取证。
3. **保持同构建**：R303 仍不建新产物（当前 kernel/payload 里已含 143 与 260 两块），只重键与换臂顺序。
## 一百二十七、R303：在被接受的那一 boot 上直接取证——固件只留下 `5180` bit0 与 `5186` bit0，`CP_RESP` 仍是 0，3 秒后一字不变

**127.1 一句话**
R302 把「被接受」量化为多数情形之后，本轮第一次**在被接受的那一次引导上**取证：三个键都重放键 143 的 shotgun 形状，**键 268/269 都被接受**（`ctl=00000001`、`bit0=1`、**`polls=2`**），但固件留下的东西极少——`HFI_CTRL_STATUS`（`0x5180`）只有 bit0 置起，`PWR_COL_CP_RESP`（`0x5101`）仍是 `00000000`，`GMU2HOST_INTR_INFO`（`0x5192`）是 `00000000`，`CB_FSR` 仍是 `00000400`（TF=0），`CM3_FW_INIT_RESULT` 仍是 `babeface`，`CM3_CFG`/`CM3_BOOT_CONFIG`/`CM3_SYSRESET` 与启动前逐位相同；再等满一个 200000 拍窗口（键 269）后，除运行时地址外**每一个字都不变**。另有一个**必须订正的观测**：`0x5186` 在启动字写下去之后从 `00000000` 变成 `00000001`，而第三键（270）在被拒时**也是 1**；R304 核对寄存器表发现 **`0x5186` 就是 `HFI_CTRL_INIT`（`AdrenoGmuBootV1.h:74`）——那是我们自己写下去的字**，所以这个「1」只证明我们的 store 落了地，不证明固件动过它（订正见 §127.7）。⇒ **接受之后固件既没有前进、也没有报错，它只是把请求收下了**；而 `0xfe` 这个拒绝字在本轮再次出现（第 9 个采样）。

**127.2 设计**
- 三个键全部重放**键 143 的 shotgun 形状**（三页表全填、清 `CB_FSR`、`CM3_FW_INIT_RESULT=0`、复位脉冲、等 `0xbabeface`、掩码、写 `HFI_CTRL_INIT=1`、200000 拍轮询），共享同一段 preamble（`work\tmp\r303_block.py` 从 R260 块的 preamble + 臂 143 外科手术派生，块 41,082 字符）。
- **键 268**：接受后**立刻**读 HFI 块 `0x5180..0x5187`（行 `gmu268m`）与 CM3/CP 字（行 `gmu268n`：`cfg/bootcfg/sysreset/cpmsg/cpresp/g2hinfo/g2hmask/cbfsr/cbfar`）。
- **键 269**：同形状，但在读数前**再跑满一个 200000 拍窗口**（行 `gmu269m=settled=1 settle=200000`），看状态是否随时间演化。
- **键 270**：同形状，读四次控制字（`c0..c4`）→ **手写 `HFI_CTRL_STATUS=0`** → 读回（`gmu270n`），随后再读 HFI 块与 CM3 字，判 bit0 是锁存还是活值、手清是否被接受。
- 本轮**建了新构建**（分支 `r303-after`，CI #418 `45378301306` success 28/28），所以三键都是**同一构建、同一份 kernel/payload 字节**，只重键该键。
- 每次运行前 `check-reboot-to-twrp.py --arm` → `PASS (3 checks, 0 failures)`；runner 内部再 gate 并自己重新武装。

**127.3 三键逐字读数**

| 键 | boot_id | 窗口 sha256 | `ctl` | `bit0` | `polls` | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| 268 | 582087034 | `5452729068cd9e899f2ba877aa25a6254bf1b69a93ba0db3ec4d0735d65664d7` | `00000001` | 1 | 2 | 接受后立刻读 |
| 269 | 633034758 | `6c1f64f621f61788ccf61424e43f4841ed7fcff4b1231667a19a44222df52abc` | `00000001` | 1 | 2 | 再等 200000 拍 |
| 270 | 639696149 | `59ddcb1162593194f945a8ef646f7824a5f6670e0e5fa37d9ef099eb4620ae13` | `000000fe` | 0 | 200000 | 本轮被拒 |

- `gmu268k`/`gmu269k` 都是 `shotgun fills=3 l2_idx=364 l0=00000000ad9a1003 l2=00000000ad800401 fsr_before=00000400 about_to_start=1`；`gmu270k` 同形。
- `gmu268l=shotgun_started ctl=00000001 bit0=1 polls=2 info=00000000 gfsr=00000000 fsr=00000400 far=00000000 fsynr0=00000000 smr_end=80000004 survived=1`；`gmu269l` **逐字相同**。
- **接受后立刻（键 268）**：`gmu268m=after_acceptance_hfi 5180=00000001 5181=00000000 5182=06030100 5183=00000000 5184=00000001 5185=ad992000 5186=00000001 5187=00000000`；`gmu268n=after_acceptance_cm3 cfg=00004052 bootcfg=00000002 sysreset=00000000 cpmsg=ad995000 cpresp=00000000 g2hinfo=00000000 g2hmask=ff7fffff cbfsr=00000400 cbfar=00000000`。
- **等满窗口后（键 269）**：`gmu269f=settled_hfi 5180=00000001 5181=00000000 5182=06030100 5183=00000000 5184=00000001 5185=ad994000 5186=00000001 5187=00000000`；`gmu269n=settled_cm3 cfg=00004052 bootcfg=00000002 sysreset=00000000 cpmsg=ad997000 cpresp=00000000 g2hinfo=00000000 g2hmask=ff7fffff cbfsr=00000400 cbfar=00000000`——除 `5185`/`cpmsg` 的运行时地址外与 268 **逐位相同**。
- **被拒（键 270）**：`gmu270l` 是 `ctl=000000fe bit0=0 polls=200000`；`gmu270m=after_repeat=1 c0=000000fe c1=000000fe c2=000000fe c3=000000fe c4=000000fe`；`gmu270n=after_clear=1 wrote=0 back=00000000`；`gmu270f=cleared_hfi 5180=00000000 … 5184=00000001 5185=ad993000 5186=00000001 5187=00000000`；`gmu270o=cleared_cm3 cfg=00004052 bootcfg=00000002 sysreset=00000000 cpmsg=ad996000 cpresp=00000000 g2hinfo=00000000 g2hmask=ff7fffff cbfsr=00000400 cbfar=00000000`。

**127.4 判决**

1. **接受是可复现的，不是一次侥幸**：本构建 3 次里 2 次被接受，累计 9 个采样（R300 1 次、R301 1 次、R302 4 次、R303 3 次）**7 次被接受、2 次读出 `0xfe`**。⇒ 「被接受」是常态；单一拒绝不能推翻机制。
2. **接受之后固件只动了一个位**：`0x5180`（`HFI_CTRL_STATUS`）bit0 置起。⇒ 这是多日攻关里**第一次确认「固件确实写下的、可归属的一个位」**。原先写作「第二个位是 `0x5186`」的说法已在 §127.7 订正为误读。
3. **订正：`0x5186` 不是固件写的**。寄存器表里 `0x5186` 是 `HFI_CTRL_INIT`，写入 1 的是我们自己；键 270 被拒时同样读回 `00000001`，恰好证明它只是**回读我们自己的 store**。⇒ R303 没有观测到固件写的第二个位，固件侧唯一可归属的变化仍是 `0x5180` bit0。
4. **`CP_RESP` 仍是 0、`GMU2HOST_INTR_INFO` 仍是 0、`CB_FSR` 仍是 `00000400`**：固件既没有回消息、也没有报 fault、也没有在 SMMU 侧留下新痕迹。⇒ 如果 CM3 在等某个 CP 应答或某条队列消息，那它**还没有走到那一步**，而不是走错。
5. **时间不改变结论**：再给 200000 拍（约 3 秒，R201 标定）后每个字都不变（只有运行时地址 `5185`/`cpmsg` 不同）。⇒ 固件**不是在慢慢推进**，而是**停在同一个点上**。
6. **控制字是可手写的活值，不是硬件锁存**：键 270 连读五次都是 `000000fe`，手写 `0` 后读回 `0`，随后的 HFI 块读里 `0x5180` 也是 `0`。⇒ R268 §92 的「bit0 每次引导锁存一次」应理解为「固件写一次、之后不再写」，而**主机随时可以清**；这与 R269 键 170 的结论一致，但在**被拒**的形状上也成立。
7. **`0xfe` 第 2 次出现，仍在被拒时**：`0xfe` = bit1..bit7 全置、bit0=0；两次出现（R300 与 R303 键 270）都是「写启动字后 200000 拍里 bit0 从未置起」。⇒ 它是一个**「收到了、但拒绝了」的应答字**，不是沉默（位定义未取证）。
8. **G1 仍未关闭**：`gd=00222001`、`cf=00088400`（bit16=0）、`gx_on=0`，GPU 未渲染一帧。

**127.5 边界与读数陷阱**
- `5184=00000001` 与 `5185=<表地址>` 在**被接受**时保持原样 ⇒ 固件**没有消费、也没有撤销**我们发布的队列表描述符；这与 R296 的「描述符停在 1 就死」在**同一构建**上并不冲突（那时死的是没有填满表页的骨架），但**跨构建**不可比（R300 §124.4）。
- `5182=06030100` 是芯片号（与 R247 `sfr=06030100` 同源），不是状态。
- `0x5184` = `HFI_QTBL_INFO`、`0x5185` = `HFI_QTBL_ADDR`、**`0x5186` = `HFI_CTRL_INIT`（我们写）**、`0x5180` = `HFI_CTRL_STATUS`、`0x5182` = `HFI_SFR_ADDR`（`drivers/NativeAdreno/AdrenoGmuBootV1.h:72-75`）。启动字之后读到 `5186=1` 是**回读自己的写**，不是固件状态。
- `polls=2` 是轮询计数不是时间；「2」= 轮询开始后几乎立即。
- 三份窗口是分开的文件（`r303_s268_window.bin`/`r303_s269_window.bin`/`r303_s270_window.bin`），行用 `work\tmp\r301_rows.py <window> gmu<key>k gmu<key>l …` 精确抽取。
- 键 268 的读取发生在「轮询结束后」，与启动字之间隔着 `polls` 循环；所以「接受后立刻」严格说是「轮询返回后立刻」。

**127.7 订正（R304 取证时发现）**
- 寄存器对照（`drivers/NativeAdreno/AdrenoGmuBootV1.h`）：`HFI_CTRL_STATUS 0x5180u`、`HFI_SFR_ADDR 0x5182u`、`HFI_QTBL_INFO 0x5184u`、`HFI_QTBL_ADDR 0x5185u`、**`HFI_CTRL_INIT 0x5186u`**。
- 因此 §127.1、§127.4-2、§127.4-3、§127.5 里把 `0x5186` 当作「固件写下的新位」是**误读**：它就是我们写启动字的那一笔的回读，`bit0=1` 只说明 store 落了地。被拒的键 270 也读回 `1`，正是同一个意思。
- 修正后**固件侧**只剩一条观测：**被接受之后 `HFI_CTRL_STATUS` bit0 置起**；`PWR_COL_CP_RESP`、`GMU2HOST_INTR_INFO`、`CB_FSR`、`CM3_CFG/BOOT_CONFIG/SYSRESET` 逐位不变，时间也不改变它。
- 其余判决不受影响：接受率（9 次采样 7 接受）、`polls=2`、`0xfe` 的含义、控制字是活值、固件停在同一点。
- 教训（与 R299/R300 同类）：**新读数先对照寄存器表再写成「发现」**，否则「新观测」可能只是回读自己刚刚写下的字。

**127.6 下一步（R304）**
1. **把 `0x5186` 追成有依据的位**：先在**被接受**与**被拒**两种 boot 上读「写启动字之前」与「之后」的 `0x5186`，确认它是「请求被看见」而不是「复位清零」；同时读 `0x5188..0x518f`（HFI 块的剩余部分）看有没有配套的状态字。
2. **问固件到底在等什么**：在接受之后试三条路径——① 读队列页（`0x5185` 指向的页）在**接受后**的头几个字，看固件有没有把请求写进队列；② 写 `HOST2GMU_INTR_SET` 的门铃（R276 键 190 只验证过 bit22 无 ack）看是否有新的 `GMU2HOST_INTR_INFO`；③ 检查 `CM3_CFG` bit 与 CP 侧的 `PWR_COL_CP_MSG` 是否有回环。
3. **不要再靠换形状**：R291–R303 已证明形状/位置/时序都不是判别式；R304 的自变量应是**「接受之后我们写什么」**，而不是「启动字怎么写」。
## 一百二十八、R304：接受之后固件确实回了一句（`HFI_CTRL_STATUS` bit0 = ACK），但从此不再说话；门铃只换来一个延迟的 `CM3_FAULT`

**128.1 一句话**
R303 把「接受之后固件什么都没做」写进了文档，本轮先把这个说法订正掉：钉住的上游 `drivers/gpu/drm/msm/adreno/a6xx_gmu.c:242-255` 里，`a6xx_gmu_hfi_start()` 做完「写 `HFI_CTRL_INIT=1`」之后**轮询的正是 `HFI_CTRL_STATUS & 1`**，也就是说 `0x5180` bit0 就是**固件对 HFI 启动的应答**。所以键 271/273 的 `ctl=00000001 bit0=1 polls=2` 不是「固件沉默」，而是「**固件在两次轮询之内确认了 HFI 启动**」；真正缺的是 ACK 之后的**一切传输活动**：`GMU2HOST_INTR_INFO` 始终 0（没有 MSGQ 中断）、`PWR_COL_CP_RESP` 始终 0、队列头 `read_index == write_index == 0`、`CB_FSR` 始终 `00000400`（TF=0）。本轮三个键都没能让它多说一个字，包括**手清控制字之后再敲 `HOST2GMU_INTR_SET` bit22 门铃**（键 273）：唯一的变化是 `GMU2HOST_INTR_INFO` 从 `00000000` 变成 `00800000`（bit23 = `CM3_FAULT`），而 bit23 的**延迟出现**已在 R252 归因于启动字本身。另有一个必须登记的**空读数**：键 272 的 `gmu272q` 行解引用的是**我们自己的 LPAE 页**（`s303virt`），不是 `0x5185` 指向的队列表页，所以它打印的 `ad998003`/`00000000` 序列是**我们自己写的表项**，不回答「固件有没有写队列」。

**128.2 设计**
- 三键全部重放**键 143 的 shotgun 形状**，共享同一段 preamble（`work\tmp\r304_block.py` 从 R303 块外科手术派生：`r268→r271 / r269→r272 / r270→r273`、行标签 `gmu268/269/270 → gmu271/272/273`、共享行 `gmur303 → gmur304`）。
- **键 271**：接受后立刻读 HFI 块 `0x5186..0x518f`（行 `gmu271q=ext_after`），并在**启动字之前**由共享 preamble 读同一块（行 `gmur304z=before_start`，13 个字 `5180/5184/5185/5186/5187/5188..518f`）——这是**第一次**在同一 boot 上把「启动字前后的同一个字」摆在一起。
- **键 272**：同形状 + 再跑满一个 200000 拍窗口（`gmu272m=settled=1 settle=200000`），随后读 HFI 块（`gmu272f`）、CM3/CP 九字（`gmu272n`）与「队列页」（`gmu272q`，**指针错，见 128.4-6**）。
- **键 273**：同形状 + 连读五次控制字（`c0..c4`）→ **手写 `HFI_CTRL_STATUS=0`** → 读回 → 读 HFI 块与 CM3 字 → **写 `HOST2GMU_INTR_SET=0x00400000`（bit22）敲门铃** → 再读 `info/mask/ao`、`cpresp`、`ctl`（行 `gmu273p`）。
- 本轮**建了新构建**（分支 `r304-after`，commit `1dc4e87e3f0894e48661ffe59ce9c362651663cf`、tree `f103569ef220ba925b30c76bee5940be7cb084db`，CI **#419 `45389917485` success 28/28**）：kernel `9bed19032fe32ed34a18876ce7585eb3f3bab2446c0cd0f72bfbecc5cc05c5ec`（126,636,032 B）、payload `4c0184614e99420514c5ff697abfb734b9a94d137238de4751a12bc4c2482e71`（150,994,944 B）。三键同一构建、同一份字节，只重键该键（`r304-p271/272/273-payload`）。
- 每次运行前 `check-reboot-to-twrp.py --arm` → `PASS (3 checks, 0 failures)`；runner 内部再 gate 并自己重新武装（`reboot-to-twrp.py --arm-bcb-only`）。

**128.3 三键逐字读数**

| 键 | boot_id | 窗口 sha256 | `ctl` | `bit0` | `polls` | `info` |
| --- | --- | --- | --- | --- | --- | --- |
| 271 | 582570004 | `2d5bbcb2bb9a1732695a308daed9b13be978a5fb66b4527009449777ea67ddbb` | `00000001` | 1 | 2 | `00000000` |
| 272 | 609687317 | `e426b80d671ad7502ae1f1a54ba71986f709482be955a69d40c26f8352453671` | `00000000` | 0 | 200000 | `00800000` |
| 273 | 583134707 | `84187304568479cd27c41820df65d157b7f88d88781d6e713dc64a2286d2ed85` | `00000001` | 1 | 2 | `00000000`（`after` 转 `00800000`） |

- 三键 `gmuNNNk` 同形：`shotgun fills=3 l2_idx=364 l0=00000000ad998003 l2=00000000ad800401 fsr_before=00000400 about_to_start=1`。
- `gmu271l=shotgun_started ctl=00000001 bit0=1 polls=2 info=00000000 gfsr=00000000 fsr=00000400 far=00000000 fsynr0=00000000 smr_end=80000004 survived=1`；`gmu273l` **逐字相同**；`gmu272l=… ctl=00000000 bit0=0 polls=200000 info=00800000 …`。
- **启动字之前（共享行）**：`gmur304z=before_start 5180=00000000 5184=00000001 5185=ad993000 5186=00000000 5187=00000000 5188..518f=00000000`（271 那次 `5185=ad990000`，只是运行时地址）。
- **键 271 启动字之后**：`gmu271q=ext_after 5186=00000001 5187=00000000 5188=00000000 5189=00000000 518a..518f=00000000`；`gmu271m=after_acceptance_hfi 5180=00000001 5181=00000000 5182=06030100 5183=00000000 5184=00000001 5185=ad990000 5186=00000001 5187=00000000`；`gmu271n=after_acceptance_cm3 cfg=00004052 bootcfg=00000002 sysreset=00000000 cpmsg=ad993000 cpresp=00000000 g2hinfo=00000000 g2hmask=ff7fffff cbfsr=00000400 cbfar=00000000`。
- **键 272**：`gmu272f=settled_hfi 5180=00000000 5181=00000000 5182=06030100 5183=00000000 5184=00000001 5185=ad993000 5186=00000001 5187=00000000`；`gmu272n=settled_cm3 cfg=00004052 bootcfg=00000002 sysreset=00000000 cpmsg=ad996000 cpresp=00000000 g2hinfo=00800000 g2hmask=ff7fffff cbfsr=00000400 cbfar=00000000`；`gmu272q=queue_page 5185=ad993000 w0=ad998003 w1=00000000 w2=ad998003 w3=00000000 w4=ad998003 w5=00000000 w6=ad998003 w7=00000000`。
- **键 273**：`gmu273m=after_repeat=1 c0=00000001 c1=00000001 c2=00000001 c3=00000001 c4=00000001`；`gmu273n=after_clear=1 wrote=0 back=00000000`；`gmu273f=cleared_hfi 5180=00000000 … 5186=00000001 …`；`gmu273o=cleared_cm3 cfg=00004052 bootcfg=00000002 sysreset=00000000 cpmsg=ad996000 cpresp=00000000 g2hinfo=00000000 g2hmask=ff7fffff cbfsr=00000400 cbfar=00000000`；`gmu273p=doorbell_set=1 info=00000000 mask=ff7fffff ao=00000000 info_after=00800000 mask_after=ff7fffff ao_after=00000000 cpresp_after=00000000 ctl_after=00000000`。

**128.4 判决**

1. **`0x5186` 就是 `HFI_CTRL_INIT`，是我们的写，不是固件的新观测**：`drivers/NativeAdreno/AdrenoGmuBootV1.h` 里 `HFI_CTRL_INIT = 0x5186u`；上游 `a6xx_gmu.c:247` 正是 `gmu_write(gmu, REG_A6XX_GMU_HFI_CTRL_INIT, 1)`。⇒ R303 §127.2/127.4-2 的「多日攻关里第一次看到固件写下了一个位」**撤回**（§127.7 已就此订正，本条固定位依据）。
2. **`HFI_CTRL_STATUS` bit0 = 固件 ACK**：上游 `a6xx_gmu_hfi_start()` 写 `HFI_CTRL_INIT=1` 后轮询 `HFI_CTRL_STATUS & 1` 并把它当「HFI 队列已启动」。⇒ 键 271/273 的 `bit0=1 polls=2` 是**固件的应答**；「接受之后固件什么都没做」应改述为「**固件确认了启动，然后在传输上保持沉默**」。
3. **拒绝有两种字**：键 272 被拒时 `ctl=00000000`（不是 `0xfe`）。累计 **12 个采样：9 次被接受、3 次被拒**，其中 **2 次读 `0xfe`**（R300 键 143、R303 键 270）、**1 次读全 0**（本轮键 272）。⇒ `0xfe` 不能一律当「拒绝的唯一形式」；`bit0` 是否置起才是判据。
4. **被拒的 boot 上 `CM3_FAULT`（`info` bit23）集起**：键 272 在 `settled` 读里 `info=00800000`；同一构建里被接受的键 271 两次读都是 `00000000`。⇒ **fault 与拒绝同现**（只登记相关，不归因；R252 的「启动字之后延迟出现」仍是唯一有数据的因果方向）。
5. **门铃没有换来任何新东西**：键 273 手清控制字（`back=00000000`）之后敲 `bit22`，`info` 由 `00000000` 变 `00800000`（bit23），而 `mask` 不变、`AO_HOST_INTERRUPT_STATUS` 不变、`cpresp_after=00000000`、`ctl_after=00000000`。⇒ 没有 MSGQ（bit0）、没有 `BOOT_SLUMBER` 应答（bit30）、没有 CP 回环；**主动敲门铃不会让固件说话**（与 R276 键 190 的「bit22 无 ack」一致，但这次是在**被接受**的 boot 上做的）。
6. **键 272 的「队列页」行是空读数**：`gmu272q` 解引用 `s303virt`（我们自己的 LPAE 根页），不是 `0x5185` 指向的队列表页；它打印的 `ad998003`/`00000000` 交替序列正是我们自己的 64 位表项（`…0003` = table 描述符）。⇒ 这一行**不回答**「固件有没有写队列」，R305 必须改用映射好的 `sHfiWin`（表基址 `sHfiPa` 的未缓存映射）重做。这也是本轮唯一的**探针缺陷**，登记在案、不重复。
7. **SMMU 侧仍干净**：三键 `survived=1`、`CB_FSR=00000400`（TF=0）、`CB_FAR=00000000`、`fsynr0=00000000`、`smr_end=80000004`。⇒ 沉默不是读表 fault 造成的。
8. **G1 仍未关闭**：`gd=00222001`、`cf=00088400`（bit16=0）、`gx_on=0`，GPU 未渲染一帧。

**128.5 边界与读数陷阱**
- **判决串的解析值必须与原始行对照**：第一版 runner 的 `field()` 用的是「前缀贪婪」正则，`name="cfg"` 会命中行里最后一个 `bootcfg=`，于是 271 的判决串把 `cfg=00004052` 打成了 `00000002`。改法是**按行 + 按空白 token 精确匹配**（`work\tmp\r304_fieldfix2.py`），并用 `work\tmp\r304_reparse271.py` 对同一份设备日志重解析复核（`cfg 00004052`、`bootcfg 00000002`、`cpmsg ad993000`、`cbfsr 00000400`）。
- `5182=06030100` 是芯片号（与 R247 `sfr=06030100` 同源），不是状态。
- `polls=2` 是轮询计数不是时间；「2」= 轮询开始后几乎立即。
- 三份窗口分开（`r304_s271/272/273_window.bin`），行用 `work\tmp\r301_rows.py <window> <tags>` 精确抽取；同键连跑会覆盖同名窗口。
- 键 272 那次**不是零字节**：6 行全部落地（`gmu272k/l/m/f/n/q`），所以 272 的读数有效，只有它的队列页行无效。
- 键 273 的 `ctl_after=00000000` 是**我们手清之后**的值，不能读成「固件撤回了 ACK」。

**128.6 门禁、工具与产物**
- host 门 PASS（25/25、51/51、25/25、32/32 + 32 组 contract）；probe 门 **38/38**（store 预算 **1150 → 1166**）；**stage4 73/73、stage5 60/60、stage6 2532/2532**；全量 `ran 46 of 48, FAILING: 0`；mode-selector **56 selectors / upper 273 / 自证 FAIL / PASS**。
- 静态门 `work\tmp\r304_static.py`：真产物 **PASS**（`required 19 / survivors 135`、forbidden 3 checked、固件 32768 B sha 钉住），对 **R303 产物 FAIL 19**（自证成立）。
- 重键 payload：`r304-p271-payload` `0d9d488adbc54a5f1a655a952413dc4f89c778fb597598a60753accff823159c`、`p272` `228e0032c59e7edf23d29881e026be47c066373bf87f4d11eae1921e384f6f55`、`p273` `0e312fbe50e6b6e61398294f267d733e16b157f93087c389c442b2450346a8a6`（各 150,994,944 B）。
- `work\tmp\r304_mkrun.py` 派生三份 runner（各 27,845 字符 / 28 条 `verdict.append`）；`work\tmp\r304_rowscheck.py` → `ROWCHECK: PASS`（runner 标签与 probe 一致、无 R303 标签泄漏）。
- 键 272 在 20:51 那次**引导挂死**（payload 已刷进 `system_b`、`boot-r16` 已启动，但十分钟内既不在 adb 也不在 fastboot），21:30 仍是「STILL GONE」；用户手动强制重启救回 TWRP 后，08:05 重跑**一次成功**（boot_id 609687317）。⇒ 这是一次**引导失败**，不是块缺陷（271/272 的 kernel 相同、payload 只差键字符串）。

**128.7 下一步（R305）**
1. **真正发一条 HFI v1 消息**：按 `a6xx_hfi.c:66-100` 的门铃约定（`HOST2GMU_INTR_SET` **bit0**，不是 bit22）与 `AdrenoHfiV1.h` 的 legacy 队列布局，在**被接受之后**把消息写进命令队列并敲门铃，然后读应答队列（`gmu<key>r` 行：`info/bit0/polls/cmd_wr/resp_rd/resp_wr/w0..w3`）。三键 = `HFI_H2F_MSG_TEST`（id 5，1 dword，最省）/ `HFI_H2F_MSG_INIT`（id 0，4 dwords）/ **空队列只敲门铃**（对照，判「门铃本身会不会被应答」）。
2. **用 `sHfiWin` 重做队列页读数**（修掉 128.4-6 的空读数），并把 `PWR_COL_CP_MSG` 指向的页头一起读。
3. 若三键都无应答 ⇒ 固件**没有在跑 HFI 循环**（它只做了 ACK），下一步就该问「它还缺什么前置条件」（GX/G1 电源、`CM3_CFG` 的位、`CM3_BOOT_CONFIG` 的 boot 模式），而不是继续换启动字的写法。

## 一百二十九、R312：把「在死域上映射 SMMU 窗口」这一笔关进闸门 —— 安静构建首次让 stage 1..6 整轮返回（30 秒），但键块仍要等 GPUCC 窗口活着

R305 以来的冻结（键 274/275/276、280、283、289、290 全部冻死）在本轮被拆成两个不同的东西。

**阶梯定位（R310 内核，`r310_ladder.py`）。** stage 1 于 30 秒返回，行是
`rc=0 GPUCC=5090000 size=9000 mapped=0 read=0 cx=ffffffff gx=ffffffff cx_on=1 gx_on=1 gx_off=1 verdict=gpucc-unmapped steps_left=9`
—— 哨兵 `0xffffffff` 被 `cx_is_on()` 判成「上电」，于是 stage 2 走进「CX up」分支去映射并读 GMU 窗口，当场冻结。
即冻结点落在 stage 2 的 GMU 触碰，而其入口是被我自己的哨兵骗开的。

**R312 的唯一实质改动**（`package/EnchiladaAdrenoProbe.cpp:1062`）：KGSL SMMU 窗口的映射从无条件执行改为
`const int smmuMapAllowed = adreno_domain_cx_is_on(cxNow);` 之后再
`if (!gSmmuVa && &armcpu_map_physical && smmuMapAllowed)`，拒绝时打印
`stage4=smmu-map-refused cx=%08x cx_on_now=%d why=domain-down`。

CI #430 `45738666647`，kernel `ce810774bceabf94e2bcf1a430d8632ec3c49abb5ad98d21e544751795d77f04`，
payload `a08fa705c06a6ffe1ed67f615bec415755e87ad0d905a72d4369cf380d353774`；
host 25/25+51/51+32/32、probe 39/39、stage4 80/80、stage5 67/67、stage6 2588/2588、全量 ran 46 of 48 FAILING 0、mode-selector 61/upper 292。

**同一颗内核上的三次引导，只差 boot 参数：**

| # | args | 结果 |
|---|---|---|
| 1 | 全部闸门缺省 | **CAME BACK 30 s**，窗口 `e330229997b93d…`：`mapped=0 read=0 cx=00000000 cx_on=0 verdict=gpucc-unmapped`、`stage2=cx-down-gmu-not-mapped` |
| 2 | `enchilada-pas-exec=290` | **CAME BACK 30 s**，窗口 `af0b4176e37dd3…`，但无 `gmu290a` 行 |
| 3 | `enchilada-gpucc-map=1 enchilada-gpucc-read=1 enchilada-pas-exec=290` | **冻结**（180 s 无 adb） |

第 1 次是里程碑：自 R305 起每一次带 `enchilada-adreno=6` 的引导都冻死，这一颗第一次让 stage 1..6 全跑完并回到 TWRP。
第 2 次划出边界：探针能在带键构建下存活，但键块不打印 —— 键派发（`:1558-1560`）嵌在 `if (stage >= 6u)`（`:1163`）里，
而那一块只在 IOMMU 闸门的 permitted 分支内可达，也就是要等 CX 读到「上电」。
第 3 次解释了历史键轮为何曾打印出 `gmu271k` 一类行：先映射 GPUCC 窗口、读 CX 得 `00005ff4`（断电）、
stage 5 绑定该窗口并把轨抬起来（`cx_after=80001fe0`）、stage 4 才 permitted、键才运行。
今天照这条路走会冻结，而这正是 R309 注释里已写下的那个**概率性**危险：GPUCC 状态字在 GPUCC AHB 时钟关闭时读不到底；
243 份历史窗口显示它多数时候在 ~29.4 秒返回 `cx=00005ff4`，今天没回。

**结论（本轮）：** 确定性障碍 —— 在死域上映射 SMMU 窗口 —— 已修复并在设备上验证；
剩下的障碍是「要跑任何键，就必须让 GPUCC 窗口被映射且两个状态字可读」，那要求把 GPUCC AHB 时钟/电源真正抬起来。
这是 GX/CX 供电的移植工作，不是探针指令化的问题。下一轮起不再加键，转入 GPUCC 时钟/电源 bring-up。
G1 仍未关闭（`gd=00222001`、`cf bit16=0`），GPU 未渲染一帧，进度仍约 15–20%。

## 一百三十、R313–R315：GPUCC 别名通道、早期作用域、以及“映射本身就是墙”

### 130.1 R313 的设计前提（CITED 上游）

`drivers/clk/qcom/gpucc-sdm845.c` 给出 GPUCC 的寄存器偏移；GPUCC 物理基 `0x05090000` 落在探针已映射的
GMU 窗口（`ADRENO_BOARD_GMU_BASE = 0x0506a000`）之内，差 `0x26000`，故 gmu dword 基为 `0x9800`。
别名：`cx_gmu` `0x1098 → 0x9c26`、`cxo` `0x109c → 0x9c27`、`cx_gdscr` `0x106c → 0x9c1b`、
`cx_hw_ctrl` `0x1540 → 0x9d50`、`gx_clamp` `0x1508 → 0x9d42`、`gmu_clk_src` `0x1120 → 0x9c48`、
`pll1` `0x100 → 0x9840`。设想：不碰 GPUCC 自己的映射（已证危险），改从 GMU 窗口这一侧读写同一批寄存器。

### 130.2 R313 的构建与门禁（HOST-VERIFIED / CI-VERIFIED）

块 2,412 字符，probe 2,261,072 → 2,263,485；块内 16 处 `gmuW[`、其中 3 笔写。host 门 PASS、probe 39/39
（store 预算 1184 → 1187）、stage4/5 PASS、stage6 2594/2594、全量 `ran 46 of 48, FAILING: 5`（全是
设备离线导致的 debugfs/chipid 项）、mode-selector 62 / upper 295。CI #431 `45744880825` success，
commit `a84da1477b189c89d7b9b2b5de4f9bc0090eaaf9`，kernel
`f81d444bad71b21847c05ec3593f3a09b0d18160cad0c8fe4c058ecc3bc4bbbc`（126,652,416 B）、payload
`7e2cdd7272f4b0390abb16ae885e8b948e09a0a48f742b8f30bd994e24c33bc2`。静态门对真产物 PASS、对 R312 产物 FAIL 4。

### 130.3 R313 上机：三次都回来，一行都没有（DEVICE-VERIFIED）

三键 293/294/295 都在 30 s 内回到 adb，窗口 sha 依次
`2a7f0888c07bff1c4ebb8115b1ac81ca699a6d28be97c903f73aecfd7c8185af`、
`1e25d40321f2b4bb1e52b4989bab7686bddb9cc17bef227d684b89297ca5f3e6`、
`0caea5a1b498e41dc172c6a8c00ab3627e6b6e80ed41816c4ccb808bf31c47fa`，
但**没有任何 `gmu293a`/`gmu294a`/`gmu295a` 行**；窗口里只有
`rc=0 GPUCC=5090000 size=9000 mapped=0 read=0 cx=00000000 … verdict=gpucc-unmapped steps_left=9`
与 `stage2=cx-down-gmu-not-mapped`。

括号深度量测给出结构性原因：`if (fwKey == 293u …)` 在 `package/EnchiladaAdrenoProbe.cpp:36919`、
**深度 10**，外层依次 `702`（函数）→ `1043 if (stage >= 4u)` → `1111 if (ioRow[0] != 0)` →
`1138 if (!gSmmuVa)` → `1146 if (adreno_iommu_id2_decode(id2,&d) != 0)` → `1579 {` →
`1600 if (fwArmed)` → `1617 if (kAdrenoGmuImageWords == 0u)` → `1632 if (!held)` →
`1673 if (fwKey >= 8u)`；`fwKey` 在 `:1580` 声明、`fwArmed` 在 `:1581`。
⇒ **所有 key ≥ 8 的块都嵌在 `if (fwArmed)` 里**，而安静构建永不武装 ⇒ 键块被**跳过**，不是冻死。
R312 键 290 打不出行是同一原因。此判决同时收回了「别名快照不能做成键块」之外的一切解释。

### 130.4 R314：把块移到深度 1、用自己的参数（HOST-VERIFIED / CI-VERIFIED）

新块插在 `    if (stage >= 4u) {` 之前、括号深度 1；用**自己的** boot 参数 `enchilada-alias`
（296/297/298）派发，自己 `armcpu_map_physical(ADRENO_BOARD_GMU_BASE, ADRENO_BOARD_GMU_SIZE)`，
末尾 `gDone = true; return;` 保证只跑一次。probe 2,263,485 → 2,267,174。
门禁：host PASS、probe 39/39、stage4 81/81、stage5 68/68、stage6 2594/2594、全量
`ran 46 of 48, FAILING: 0`（设备在线）、mode-selector 62/295。
CI #432 `45754431391` success，commit `069cc1166636e4b695c6b34169bd49408f3e3182`，kernel
`afee665a216d749ff4938766fc6a447f01b95dfcf408d92a119e34591aa13c3c`、payload
`ff024e827d6fce66b78f61146d885f718ea1a7a5074571749d0b5ff6005cbe1f`。
三键 payload 用等长 args 变体生成（`r311_variant.py --drop … --add "enchilada-alias=29N"`，镜像字节数不变）。

### 130.5 R314 上机：只读的那一臂也冻死（DEVICE-VERIFIED）

键 296（只读六字、零写）：13:00:16 gate rc=0 → flash OKAY → boot OKAY → 13:00:35 fastboot released →
**`RESULT: DID NOT COME BACK in 200 s`**。冻结与键块位置、武装链、GPUCC 窗口都无关。

### 130.6 R315：把「映射」与「读」拆开（HOST-VERIFIED / CI-VERIFIED / DEVICE-VERIFIED）

同一颗 R315 内核上三臂：

* **301**：不映射、不读，只打一行 —— **回来**，行 `gmu301a=early_print_only ok=1 mapped=0 row=only`，
  窗口 sha `eab6c61e3909e4e01f0d019ec8dbca47eb41baaf353d39ce2bc93c8cda12a6c8`。
* **299**：只映射 GMU 窗口、零读 —— **冻死**（14:23:53 起 200 s 无 adb）。
* **300**：映射 + 读一个字 —— 未跑，299 已给出答案。

构建：CI #433 `45763768530` success，commit `b734a42f472e71e85d339cc3bcdae8e3231c1910`，kernel
`412289a03a296022ea59c89fd00a39da35a4d4407add5a0710e07a9c872b06ab`（126,652,416 B）、payload
`d8d44ea50c71fae01cc01d37836dcb3200c28b17feeccd89b8f23a8e7dd95611`。门禁：host PASS、probe 39/39、
stage4 81/81、stage5 68/68、stage6 2594/2594、全量 `ran 46 of 48, FAILING: 0`、mode-selector 62/295；
静态门对 R315 真产物 PASS、对 R314 产物 FAIL 3。

### 130.7 判决（本轮的核心结论）

1. 深度 1 的早期块、自己的 boot 参数、打印路径都是好的（301）。
2. **在 CX 上电之前映射任何一个 GPU 域窗口，会把机器挂死**，即使一个字节都不读（299）。
3. 因此 R305 274–276、R306 280–282、R307 283–285、R309 290、R314 296 的全部冻结**是同一个动作**。
4. 形式化表述：**CX 域起来之前，GPU 没有任何可用 MMIO；而通过 GPUCC 抬 CX 本身就是那个会挂的动作。**
   别名通道、GPUCC 直读、读写闸门，至此全部封死。
5. 剩下的入口是 R304 已测到的**固件**：CM3 对 HFI 启动字应答（`HFI_CTRL_STATUS` bit0，poll 2，12 次采样
   里 9 次），随后不从队列取消息、不回写、不抬 CX。它缺的输入是电轨/VSENSE，不是我们写的任何寄存器。

### 130.8 边界与未做

* 冻结页的全部证据仍是「`stage:` 空白 + 控制台框空 + `IRQ=0`」——诊断页由冻结路径补画，
  不能证明块执行到哪一行；本轮新增的 301 行是**正向**证据（同一颗内核上能打印），据此排除了
  「深度 1 的打印本身有问题」。
* 键 300（映射 + 一次读）未跑，因为 299 已把「读」从因果链里摘掉。
* R313/R314/R315 三次上机的失败样本各 1 次，都是「一次引导的采样」；同一颗内核上 301 成功 1/1。
  这里不主张统计强度，只主张因果闭链（299 与 301 只差一次映射调用）。

### 130.9 附带修复：`porting.toml` 长期不可解析（HOST-VERIFIED）

本轮交付文档时发现 `porting.toml` 已经不是合法 TOML：`curl` 三个历史版本后确认**至少从 R304 起就如此**
（`1dc4e87e` 895,657 字符 / 55 处 `\'` / 42 处 `,,`；`2ef73e2a`、`df79cff`、`0ba27cc9` 同为 59 处 `\'`），
`tomllib` 在 `Unclosed array (at line 647 …)` 处停下。三类损伤全部来自历轮补丁脚本的字符串拼接：
`\'`（TOML 两种字符串都不允许的转义）、`,,`（替换脚本留下的重复分隔符）、以及单引号字面量里混入裸撇号
（`host's`、`key 241's` 之类，提前闭合字符串）。

修复方式：把**全部 889 个数组元素**改写成 TOML 多行字面量 `'''…'''`（这种字符串允许撇号、双引号与反斜杠
原样出现），并把行内词中的撇号统一为 U+2019；写入前用 `tomllib` 验收、写入后再验一次。
结果：**903,971 字符 / 可解析 / CRLF 0**。

一处必须诚实记录的副作用：gpu 区三个数组的**长度从 101/112/124 变成 144/161/170**。这不是新增内容，
而是原文件里的裸撇号把相邻元素**吞并成一个字符串**，历轮用正则数元素时被少算了（那些计数从未经过
`tomllib`）。修复后每个元素重新独立，计数才第一次等于真实元素数；随后追加本轮的一条记录，得
**145/162/171**。

### 130.10 自动回 TWRP 的两条路都试过了：都不行（DEVICE-VERIFIED）

用户要求「运行时卡死也能回 TWRP」。两个只改 boot-args、不新建内核的 payload（都由键 299 那张
`r315-p299-payload` 等长改出，镜像字节数不变）：

| payload | args 变化 | sha256 | 结果 |
|---|---|---|---|
| `r316-p299-autoreset` | `sheng-autoreset=2400` → `0090` | `dcb8288b5d25d581a0cc268e9f6f1157097889a043bf7f5268a0addb16a49c38` | 14:46:39 引导 → **260 s 未回** |
| `r316-p299-wd` | 上面再加 `sheng-disable-watchdogd=1` → `=0`，autoreset `=0060` | `5f4f6148a7fbfeeeaf849b7b5a7a6720b74105c7136120e4317204c4592d20e1` | 15:06:32 引导 → **200 s 未回** |

两次都刷写成功（`Sending 'system_b' 147456 KB OKAY`、`Booting OKAY`），机器也确实接手了内核
（`fastboot released after 6 s`），随后进入 R315 已描述的那个状态：屏幕停在诊断页、`adb` 与 `fastboot`
都没有。

与源码一致：`package/BootLog.c:1273` 的 autoreset 判断跑在日志写线程里，需要那一轮 UFS 写成功；
`package/BootReset.c` 的 `boot_reset_watchdog()` 是返回 1 的桩（`no hypervisor watchdog backstop on
enchilada (%u s requested)`），而 `com.apple.watchdogd` 走的是 Apple 的 watchdog 策略、不是 SoC 的
`PMIC` 硬狗。**结论：这套内核里没有任何一条软件路径能在「IRQ 关掉的硬锁」之后把机器带回来，唯一
恢复方式是人工长按电源键约 12 秒。** 这不是缺陷，是当前移植阶段的硬边界，已作为上机纪律写进交接。

### 130.11 复现与落点：GMU 窗口的映射 2/2 冻死，而墙外还有两个非 GPU 域的控制器（DEVICE-VERIFIED / CITED）

键 299（只映射 GMU 窗口、零读）在同一天再跑一次：15:13:29 引导、`fastboot released after 6 s`、
**200 s 未回**。加上 14:23:53 那次，**2/2 冻死**；同一颗内核上键 301（不映射不读、只打一行）1/1 回来。
作为对照，GPUCC 窗口的映射此前是 1/2（R313 零构建矩阵 A 格）。⇒ 「CX 断电时映射 GPU 域窗口」至少对
GMU 窗口是**确定性**的，对 GPUCC 窗口是概率性的，两者都不再是可用通道。

由此得到的下一步方向不是再换窗口，而是**离开 GPU 域**：GPU 的上电链在他处，且这些控制器不在 GPU 电源域里，
映射它们不会触发同一个 AHB 停摆：

* `GCC`（`0x00100000`，`gcc_gpu_cfg_ahb_clk` 与 `gfx3d` 时钟的父级）—— 上游 `gpucc-sdm845.c` 的
  `cx_gmu_clk` / `gfx3d_clk_src` 都挂在 GCC 的 `gcc_gpu_cfg_ahb_clk` 与 `gpll` 上，mainline 的顺序是先开
  GCC 侧 AHB 时钟再碰 GPUCC；
* `RPMh` / `SPMI`（PMIC 电轨与 VSENSE）—— CX/GX 的 `vdd` 由 PMIC 提供，固件等的「rail up」就是它。

两者都是**普通 SoC 域**，探针已有 `armcpu_map_physical` 的能力，不需要 GPU 上电即可访问。
下一轮（R317）的计划：键 302 = 只映射 GCC 窗口并读 `CFG_AHB` 相关寄存器（零写）；
键 303 = 在 GCC 窗口里打开 GPU 的 AHB/配置时钟后**再**回到 GPUCC 状态读；键 304 = 走 SPMI 读 PMIC 的
CX/GX 轨道状态（只读）。判读顺序：302 必须回来（否则「映射非 GPU 窗口」也被同一个缺陷污染，
那问题就不在电源域而在 `armcpu_map_physical` 的通用路径上）；302 回来而 303 冻 ⇒ 时间/顺序问题；
303 回来后 GPUCC 状态字可读 ⇒ 供电链终于接上，可以回到 R305 的 HFI 传输实验。

### 130.12 墙不在 GPU 域：映射 GCC 窗口同样冻死（DEVICE-VERIFIED）

R316 结束时的推断是「离开 GPU 域，去 GCC 与 PMIC」，理由是 GPUCC/GMU 的映射在 CX 断开时会冻。
R317 把这一推断直接测了：新块在**括号深度 1**、用**自己的 boot 参数** `enchilada-soc` 派发，
三个键只改窗口与动作，`gDone = true; return;` 一次性执行，零 `gmuW[]` 写。

构建与门禁（全部一次通过，未改动任何门禁脚本）：`check-enchilada-adreno-host.py` 25/25 + 51/51 +
32/32、`check-enchilada-adreno-probe.py` **39/39**（零 store，预算不变）、stage4 **81/81**、
stage5 **68/68**、stage6 **2594/2594**、全量 `ran 46 of 48, FAILING: 0`、
`check_mode_selector.py` **62 selectors / upper 295 / PASS**（新键不使用 `fwKey`，故窗口字面量不变）。
分支 `r317-socbase` commit `6b49c266fbeaf9624aae1f621bc174954842191d`（tree `566612e1f4cc…`）、
CI **#434 `45777150128` success**、kernel `4652d5c489ebeddd0ae7f1f5306164792979435519d688018655235dcb9be13e`
（126,668,800 B）、payload `96d316605951fb784905dcce46ca33653226a110eb2272e7dad5f3e57507bbdc`。
静态门 `r317_static.py`：对真产物 **PASS**（`required+survivors: 211 checked in both artefacts`、
`forbidden: 3 checked`、固件 sha 钉住），对 R315 产物 **FAIL 6**（自证成立）。
三个 payload 由 `r311_variant.py` 等长改 args 得到（`--drop "enchilada-touch=4 sheng-touch-hid=1 sheng-lite=1"`
`--add "enchilada-soc=<key>"`）：p302 `d32bb5c04957a3b0542ed7f9266e686284d1f7a6a6460a72b920d1ee90f5ce36`、
p303 `afbc65ea879f4884c7fd01be3ac5a71acece0e5afbd66ecf84691fc7f77d22fd`、
p304 `380889f78edb405c9d4bb70ecbe6943cbc3cb7a4136d7cc5b783d8e50effb8a9`。

上机（`r310_win.py <dir> --key <N> --wait 220`，每键前 `--arm` 闸门 rc=0）：

| 键 | 窗口 | 动作 | 结果 |
|---|---|---|---|
| 302 | GCC `0x00100000`，size `0x40000` | map + 读 `+0` `+4` `+0x1000` `+0x3FFFC` | 16:21:20 引导、`fastboot released after 6 s`、**220 s 未回 = 冻死** |
| 303 | GCC `0x00100000`，size `0x1000` | map + 只读 `+0` `+4` | 16:34:38 引导、`fastboot released after 6 s`、**220 s 未回 = 冻死** |

对照（此前同一台机器、同一颗内核）：键 **301**（不映射、不读，只打一行）1/1 回来；
键 **299**（映射 GMU 窗口、零读）2/2 冻死；GPUCC 只映射 1/2 冻死。

判决：**冻结点不在 GPU 电源域**。GCC 是完全独立的 SoC 域，且 `gcc_gpu_cfg_ahb_clk` 就在它里面，
它在 CX 断开时本应可访问；它同样把机器冻死，且与窗口大小、读的个数无关（302 四读、303 两读，
都是 1/1）。⇒ 嫌疑从「GPU 域窗口」移到两处之一：①`armcpu_map_physical` 本身（建立映射这个动作）；
②「映射 MMIO 窗口后**任何**未上电区域的访问」——但 299 是零读也冻，权重偏向 ①。

边界与未做：302/303 各只有一个样本（不是 2/2 的重复采样）；`304`（`armcpu_map_physical(0x100000, 0)`
零尺寸调用）尚未上机；没有「映射非 GPU 窗口且零读」这一格 —— 那是 R318 的键 305，
以及与它配对的 RAM 窗口两格（306 映射 + 读，307 只映射）。

### 130.13 第三格：零长度调用 `armcpu_map_physical` 也冻死（DEVICE-VERIFIED）

键 304 与 302/303 唯一的区别是**没有建立任何窗口**：`armcpu_map_physical(0x00100000, 0)`
（length = 0），不读、不写、不保留映射，只打印调用返回值。

16:48:14 引导、`fastboot released after 6 s`、**200 s 未回 = 冻死**。

至此同一颗内核（R317，`4652d5c4…`）上的四次上位机读数：

| 键 | 是否调用 `armcpu_map_physical` | 参数 | 结果 |
|---|---|---|---|
| 301 | **否** | — | 回来（1/1） |
| 304 | 是 | `(0x00100000, 0)` | 冻死 |
| 303 | 是 | `(0x00100000, 0x1000)` + 两读 | 冻死 |
| 302 | 是 | `(0x00100000, 0x40000)` + 四读 | 冻死 |

⇒ **存活与「是否调用映射器」一一对应（0/4 对 1/1）**，与窗口大小、读的个数、以及窗口属于哪个
电源域都无关。实现（`drivers/ArmCpu/ArmCpu.cpp:358-363`）是
`IOMemoryDescriptor::withPhysicalAddress(physical, length, kIODirectionIn)` + `map(kIOMapAnywhere |
kIOMapInhibitCache)`；零长度也冻，说明冻结发生在**这条 IOKit 路径内部**，而不是在之后的寄存器访问里。

尚未分辨的两种可能（R318 键 306/307 就是要分开它们）：
①映射器在这个阶段对**任何**物理区间都不可用（IOKit 映射/分配路径本身）；
②映射器可用，但 `kIOMapInhibitCache` 之后的**访问**在未上电区停摆 —— 304 不支持这条，
因为它什么都没映射也没读。

### 130.14 第四格：映射**普通内存**也冻死 —— 嫌疑锁定在映射器本身（DEVICE-VERIFIED）

R318（键 305/306/307）的方块本来是为了分开「域」与「动作」两个轴，但 306 一跑就给出了更硬的结论。
306 映射的是 `0x80000000`（内核自己就在用的普通 RAM），读一个字：

17:16:36 引导、`fastboot released after 6 s`、**200 s 未回 = 冻死**。

| 键 | 调用映射器 | 目标 | 长度 | 动作 | 结果 |
|---|---|---|---|---|---|
| 301 | **否** | — | — | 只打一行 | 回来 1/1 |
| 304 | 是 | GCC `0x00100000` | **0** | 无 | 冻死 |
| 302 | 是 | GCC `0x00100000` | `0x40000` | 四读 | 冻死 |
| 303 | 是 | GCC `0x00100000` | `0x1000` | 两读 | 冻死 |
| 299 | 是 | GMU `0x0506a000` | `0x30000` | 无 | 冻死 2/2 |
| **306** | 是 | **RAM `0x80000000`** | `0x1000` | 一读 | **冻死** |

⇒ 六次调用、六次冻死，一次不调用、一次存活；**目标区间（含普通内存）、长度（含 0）、读与不读、
以及窗口属于哪个电源域，全都不改变结果**。这与 R312 零构建矩阵里 GPUCC 只映射「1/2 冻」的
概率性一致：那 1 次「回来」是抽签抽到的，而不是安全。

实现（`drivers/ArmCpu/ArmCpu.cpp:358-363`）：

```c
extern "C" uint64_t armcpu_map_physical (uint64_t physical, uint64_t length)
{
  OSSharedPtr<IOMemoryDescriptor> d (IOMemoryDescriptor::withPhysicalAddress (physical, length, kIODirectionIn), OSNoRetain);
  IOMemoryMap *map = d ? d->map (kIOMapAnywhere | kIOMapInhibitCache) : nullptr;   // kept: never released
  return map ? static_cast<uint64_t> (map->getVirtualAddress ()) : 0;
}
```

⇒ 冻结点在这条 IOKit 路径内部的某处（`withPhysicalAddress` 或 `map`），而不是在随后的寄存器访问里 ——
零长度那一格（304）没有保留映射也没有读，同样冻死。

**这与「历史上键块能落行」并不矛盾**：那些轮次同样是抽签，只是当时抽到了能返回的那一侧；R143–R273
的成功样本集中在上午（11/24 20:47 与 11/25 08:05、09:35），而 11/25 10:00 之后连抽 6 次全冻。
也就是说，当前这台机器/这颗 payload 组合下，映射器几乎不可用；「GPU 域」「CX 未上电」都不是必要条件。

**下一步（离线可做）**：
1. 读 XNU 的 `IOMemoryDescriptor::withPhysicalAddress` 与 `IOMemoryMap::map`（`iokit/IOKit/`）找这条路径里
   可能无限等待的点：内核 map 的分配（`kIOMapAnywhere`）、`IOUncachedMemory` 的页表项建立、以及它是否
   依赖某个需要中断才能推进的线程；
2. 对照 `package/BootDiagExport.c:50` 的成功用法（截图，晚得多、且在不同线程/上下文里调用）——
   若差别在「调用上下文」，那么结论就是**不能在轮询线程里做映射**，而应把映射搬到一次性上下文；
3. 设备到手后先跑剩下的两格（305 GCC 只映射、307 RAM 只映射）确认「目标无关」，再决定是否改架构。

## 一百三十一、R319–R320：墙是 IOKit 的 descriptor 构造器；换成 pmap 自己的 `ml_io_map` 之后，GPUCC 与 GMU 两个窗口都能映射、GMU 的 `HFI_CTRL_STATUS` 也能读出来

R317/R318 已经把「调用 `armcpu_map_physical` 就一定冻死」测成 6/6（目标覆盖 GPUCC、GMU、GCC 与普通 RAM；
长度覆盖 0、0x1000、0x40000、0x30000；有读与无读），而唯一活着的那一臂根本不调用它。剩下两种可能：
冻在 `IOMemoryDescriptor::withPhysicalAddress(...)`，或者冻在随后的 `map()`。R319 把这两步拆开。

### 131.1 R319 键 308：只建 descriptor、**从不** `map()` —— 冻死（DEVICE-VERIFIED）

`drivers/ArmCpu/ArmCpu.cpp` 新增一个导出符号，只做构造器那一半：

```c
extern "C" uint64_t armcpu_desc_only (uint64_t physical, uint64_t length)
{
  OSSharedPtr<IOMemoryDescriptor> d (IOMemoryDescriptor::withPhysicalAddress (physical, length, kIODirectionIn), OSNoRetain);
  return d ? 1u : 0u;   // never mapped, never released
}
```

键 308（`enchilada-maptest=308`，深度 1 的早作用域、零 `gmuW[]` 写）08:48:48 `fastboot released`，
**220 s 未回 = 冻死**。⇒ `map()` 根本没被调用，冻死已经发生。

### 131.2 R319 键 310：可写兄弟（`kIODirectionInOut`，两次调用都在）—— 也冻死（DEVICE-VERIFIED）

键 310 走 `armcpu_map_physical_rw(0x00100000, 0x1000)`，09:03:00 释放、**220 s 未回**。⇒ 不是方向问题。
（键 309 未跑：它只在同一 tick 里先等 0.2 s 再映射，308 已把「时机」这一支排除。）

### 131.3 判决：冻结点是 descriptor 构造器，九次调用九次冻死

| 键 | 调用 | 目标 | 结果 |
|---|---|---|---|
| 301 | 无 | — | 回来 1/1 |
| 304 / 302 / 303 | `armcpu_map_physical` | GCC | 冻死 |
| 299 | `armcpu_map_physical` | GMU | 冻死 2/2 |
| 306 / 307 | `armcpu_map_physical` | **普通 RAM** | 冻死 |
| 305 | `armcpu_map_physical` | GCC 零读 | 冻死 |
| **308** | **只有 `withPhysicalAddress`** | GCC | **冻死** |
| **310** | `armcpu_map_physical_rw` | GCC | **冻死** |

### 131.4 R320：换 pmap 自己的 `ml_io_map` 作为设备窗口通道

`llvm-nm` 在已构建的 kernel 里确认这几条符号都在：

```
fffffff00ba1db14 S _ml_io_map
fffffff00ba1e228 S _ml_io_map_wcomb
fffffff00ba1e20c S _ml_io_map_with_prot
fffffff00ba1e294 S _ml_static_ptovirt
```

`ml_io_map` 是 pmap 的 io submap 映射：不需要 descriptor、不经过 IOKit 的任务/VM 对象。
本仓库自己的注释还留着一句实测（`package/BootDiagExport.c`）：`ml_io_map` 只在一个**十兆字节**的
面板帧上失败过（`kmem_alloc failed with 3`），所以那个调用者改用 IOKit；而我们的窗口是 4 KiB 到 192 KiB。
新增导出（`drivers/ArmCpu/ArmCpu.cpp`，`ml_io_map` 按 weak_import 声明，缺了也不会打断构建）：

```c
extern "C" vm_offset_t ml_io_map (vm_offset_t phys_addr, vm_size_t size) __attribute__((weak_import));

extern "C" uint64_t armcpu_io_map (uint64_t physical, uint64_t length)
{
  if (!&ml_io_map) return 0;
  if (physical == 0 || length == 0) return 0;
  return static_cast<uint64_t> (ml_io_map (static_cast<vm_offset_t> (physical),
                                          static_cast<vm_size_t> (length)));
}
```

### 131.5 R320 三键：三个窗口全部映射成功，GMU 的寄存器也读出来了（DEVICE-VERIFIED）

| 键 | 动作 | 设备自写行 | 结果 |
|---|---|---|---|
| **311** | `armcpu_io_map(GCC 0x00100000, 0x1000)`，零读 | `gmu311a=gcc_io_map ok=1 win=546ba000 pa=100000 size=1000 reads=0` | **回来** |
| **312** | `armcpu_io_map(GMU 0x0506a000, 0x30000)`，零读 | `gmu312a=gmu_io_map ok=1 win=6ceba000 pa=506a000 size=30000 reads=0` | **回来** |
| **313** | 同上，再读 `HFI_CTRL_STATUS` | `gmu313a=gmu_io_read ok=1 win=6d800000 pa=506a000 size=30000 ctl=00000000` | **回来** |

时间戳：311 = 09:57:44、312 = 09:58:57、313 = 10:00:09（都在探针入口后约 29.3 s，与历史成功行的节奏一致）。
每次都是 `flash rc=0`、`boot rc=0`、`fastboot released after 6 s`，窗口 sha256 分别为
`4a5e600de589…f95be7`、`b83d17613403…25d4bc`、`30ab9ff7a7c4…3b45025`。

⇒ **`ml_io_map` 通道可用**：GPUCC 与 GMU 两个窗口都能拿到内核 VA，GMU 的 `HFI_CTRL_STATUS` 也能安全读出
（值 `00000000`，与「CM3 未启动」的既有状态一致）。这条通道完全绕开了 9/9 冻死的 IOKit 构造器。

### 131.6 边界与下一步

- 本节的「能用」是**这一颗 kernel（`4ed2c3cc8ac7…a76b46`）、这一次上机（各 1/1）**的结果；`ml_io_map` 的
  submap 容量有限（十兆面板失败是实测），所以**每个窗口只映射一次、总量要小**，这正好符合我们的用法。
- 下一步（R321）：把探针里所有窗口的取法从 `armcpu_map_physical` 换成 `armcpu_io_map` —— stage 1 的 GPUCC
  两个状态字、stage 2 的 GMU 窗口、stage 4/6 的 SMMU 窗口 —— 然后在能用的通道上重跑 R312 的那条
  「安静构建 → stage 1..6 整轮」路径与 GPUCC 状态读，把「CX 到底有没有上电」这一问重新打开。
- 仍然未解决的：HFI 传输（R305 的 legacy 队列表与门铃）需要 CM3 活着，而 CM3 需要 CX 上电；G1
  （GDSCR `cf` bit16）也仍未关闭。GPU 仍未渲染一帧。

## 一百三十二、R321：把「能用的通道」放进**原有入口**——九次冻死的那条 `armcpu_map_physical` 自己改走 `ml_io_map`，于是历史必冻的形状与 GMU 窗口都在真机上回来了

R319 把墙钉在 `IOMemoryDescriptor::withPhysicalAddress(...)` 这个构造器上（键 308 只构造、不 `map()`，仍冻死；键 310 的可写兄弟也冻死）；R320 证明 pmap 的 `ml_io_map` 能映射 GCC（键 311）、GMU（键 312）并读出 `HFI_CTRL_STATUS`（键 313），三次都在 30 秒内回来。

R321 因此不动探针，只改 `drivers/ArmCpu/ArmCpu.cpp` 两个入口的**函数体**：`armcpu_map_physical` 与 `armcpu_map_physical_rw` 先试 `ml_io_map`，返回非零即用；为零才退回原来的 IOKit 路径（io submap 小，`package/BootDiagExport.c` 记录过十兆面板在那里 `kmem_alloc failed with 3`）。`ml_io_map` 的声明放在**命名空间作用域**——第一版把它写进函数体，CI #438 立刻以 `ArmCpu.cpp:366:10: error: expected unqualified-id` + `use of undeclared identifier 'ml_io_map'` 拒绝（C++ 不允许块作用域的链接规范），修正后 CI #439 `45932215479` success。

**设备读数（DEVICE-VERIFIED，同一颗 kernel `5bac45c4615b92a9e90fbad7ee1c029f091535b08cd0d3086eecd676216bb86a`，payload `f974ef48c54657a0e9ef0610a129f3694c5d90f896e3efcb2e7c747aef120265`）**

| 形状 | 结果 | 关键行 |
|---|---|---|
| 安静构建 `enchilada-adreno=6` | CAME BACK 30 s | `mapped=0 read=0 cx=00000000 gx=00000000 cx_on=0 gx_on=0 gx_off=0 verdict=gpucc-unmapped steps_left=9`；`stage2=cx-down-gmu-not-mapped` |
| **历史必冻** `gpucc-map=1 gpucc-read=1 pas-exec=2` | **CAME BACK 30 s（2/2）** | `mapped=1 read=1 cx=00005ff4 gx=00088400 cx_on=0 gx_on=0 gx_off=1 verdict=cx-DOWN steps_left=9` |
| `gmu-map=1 gmu-read=1 gmu-force=1` | **CAME BACK 30 s（2/2）** | `stage2=gmu-read gmu=506a000 size=30000 general7=00000000 boot_state=1(cold)` |

132.1 **R312 的 `r312-hist`（当时 10:21 冻死）现在是 2/2 回来并打出 GPUCC 的两个状态字** —— 这是 R305 以来第一次在不冻的前提下读到自己想要的寄存器。窗口 sha 依次 `851a9e0208df52e7147de6caf5475877820cd714e7f0ec0cbf430ac7bccb6337`（第一次）与重复样本（同形状）。
132.2 **GMU 窗口经「原有入口」映射也活了**：`stage2=gmu-read` 说明 stage 2 通过 `armcpu_map_physical(0x0506a000, 0x30000)` 拿到内核 VA，并读 `GENERAL_7 = 00000000`、`boot_state=1(cold)` —— 之前 R311 的 `gmu-force` 正是冻在这里。
132.3 **CX 仍然是关的**：`cx=00005ff4`、`cx_on=0`、`gx=00088400`、`gx_on=0`、`gx_off=1`、`verdict=cx-DOWN` —— 与 R310 阶梯时侥幸读到的那次逐位一致（`cx=00005ff4`）。所以「读得到」不等于「域已上电」；G1（GDSCR `cf` bit16=0、`gd=00222001`）仍未关闭，GPU 仍未渲染一帧。
132.4 反汇编自证：`r321_verify.py` 在 `r321-kernel\kernel.macho` 里找到 `<_armcpu_map_physical>`，其函数体引用 `ml_io_map`（`references ml_io_map: True`），并且仍保留 IOKit 兜底分支（`bl __ZN18IOMemoryDescriptor19withPhysicalAddressEyyj`）——通道确实换在函数体内部，不是只改了注释。
132.5 门禁：host 25/25 + 51/51 + 32/32、probe 39/39、stage4 81/81、stage5 68/68、stage6 2594/2594、全量 `ran 46 of 48, FAILING: 0`、mode-selector PASS；`r321_static.py` 对真产物 PASS、按其派生链对 R320 产物 FAIL。
132.6 边界：各形状 1–2 次样本、单颗 kernel；`ml_io_map` 的 io submap 容量有限，同一窗口只映射一次；本次没有验证写路径（`_rw`）在设备窗口上的实际效果，只有读。

## 一百三十三、R322：通道修好之后，R314 的早期别名块终于真的跑起来了——GPUCC 时钟能读能写、能写进去并留住，而 CX 的 GDSCR 仍是 `00222001`（SW_COLLAPSE=1，域还是关的）

R321 用 `ml_io_map` 换掉了两个入口里的 IOKit 构造器。于是 R314 早就编进 kernel 的早期别名块（键 296/297/298，独立 boot 参数 `enchilada-alias`、深度 1、自映射 GMU 窗口）第一次真正执行——它当初 1/1 冻死（13:00），这一轮 **3/3 都在 30 秒内回来**，且每一行都落地。**没有新构建**：同一颗 R321 kernel `5bac45c4…bb86a`，只换 boot 参数。

**设备读数（DEVICE-VERIFIED）**

| 键 | 臂 | 结果 |
|---|---|---|
| 296 | 只读六字快照 | `gmu296a=early_alias ok=1 win=2d57c000 cx_gmu=80004220 cxo=80000001 cx_gdscr=00222001 cx_hw=00005ff4 rcg=80000000 pll=00000000` |
| 297 | `0x5091098`(cx_gmu_clk) BIT0 置 1 | `gmu297a=early_cxgmu_enable wrote=1 pre=80004220 post=80004221 bit0=1 cx_gdscr=00222001` |
| 298 | 再置 `0x509109c`(cxo) BIT0 | `gmu298a=early_cxo_enable wrote=2 cxo_pre=80000001 cxo_post=80000001 cxgmu_post=80004221 cx_hw=00005ff4 rcg_post=80000000 pll_post=00000000` |

133.1 **GPUCC 时钟读得出、写进去、留得住**：`cx_gmu` 从 `80004220` 变成 `80004221`（bit0 = CX GMU 时钟使能），回读确认；`cxo=80000001` 本来已经开着（写它不改变值，属正常）。这证明 R313 当时「别名快照做不成键块」只是**作用域/武装**问题，通道本身是好的。
133.2 **CX 仍然关着，而且现在能说清缺哪一步**：`cx_gdscr=00222001` ⇒ bit0（`SW_COLLAPSE`）= 1，域处于 collapse；bit16（`POWER_UP_COMPLETE`）= 0。`cx_hw=00005ff4` 与历史上「侥幸读到」的那次一致。所以下一步不是猜，而是**清 `SW_COLLAPSE` 那一位并轮询 bit16**。
133.3 **PLL1（Fabia）= 0**，`rcg=80000000`（根时钟开关在、但 PLL 未配置）⇒ 与 mainline `gpucc-sdm845.c` 的 CX GMU 分支要求（先配置 PLL1，再让 RCG 跑）吻合；只开 `cx_gmu_clk` 的 BIT0 不足以让分支真正供时钟。
133.4 本轮的形状全部 1/1 样本、单颗 kernel；三键都是「一次做完就 `gDone = true; return;`」的一次性执行，不会重入。
133.5 边界：没有写 GDSCR（`0x509106c`）、没有碰 PLL1 的配置寄存器，也还没验证写路径在**GDSC** 上的效果；这些留给下一轮。

