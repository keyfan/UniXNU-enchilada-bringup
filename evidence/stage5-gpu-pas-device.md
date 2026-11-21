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

