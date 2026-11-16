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

### 12.9 诚实边界（R162 时点，与 §九 并存）

- 准入表 **`modeled=9 / satisfied=0 / BLOCKED` 未变**；`CONFIG_XNU_NATIVE_ADRENO=n` 未变；**GPU 从未渲染**；
  `gputate=` 一次都没拿到。
- 我们的内核**从未成功认证 zap**；`PAS_INIT_IMAGE` 仍被拒（`0xFFCFFFxx` 族），**成因未解释**。
- `SHM_BRIDGE_CREATE` 的 `0x57` / `0x5b` 两个状态码**未解释**（不在 pin 的 8 个错误码里）；句柄复用**未解释**。
- 「早期 SCM 路径的改动会杀死启动」有**四次真机读数支持**（3 次静默 + 1 次能写日志的对照），但
  **具体哪一处仍未定位**；R162 是「退回设备已证的形状」，**不是**「找到了 bug」。
- **R162 之后 `boot_id` 是否恢复变化、`gputate=` 是否真的出现，都还没测**；在拿到这两项之前，
  不得把 R162 当成「已解决」。
- **不得**写「PAS 通过了」「GPU 能工作了」「Windows 能做到所以我们也能做到」——
  用户证实的是 **Windows 在这一台上真的驱动过**，不是我们做到了。
