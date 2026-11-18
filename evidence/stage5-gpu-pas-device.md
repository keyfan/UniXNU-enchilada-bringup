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
- `sctlr=00200626`：按 ARM SMMU-500 的 sCR0 布局，**bit0 SMMUEN=0**（全局关闭）、bit1 CLIENTPD=1（客户端口禁用）。其余置位（0x600、0x200000）在没查 sCR0 字段表之前不命名，不做过度断言。语义上这与「CB0 从未被本驱动上电编程、没有任何页表」自洽。
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
3. GX 起来之后才读 SECVID（`svt`），再谈 CB0/TTBR（`SMMUEN` 置位后才有意义）。
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

**`sctlr = 0x00200626`** → 置位 bits **1, 2, 5, 9, 10, 21**：按 ARM SMMU-500 的 sCR0 布局，bit0 `SMMUEN` = **0**（全局关闭）、bit1 `CLIENTPD` = 1（客户端口禁用）、bit2 `PRIVCFG` = 1。其余位（0x626 里的 0x620 与 0x200000）同样不在此仓库的钉住范围内，不命名。

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
external abort）；读到 `sCR0=0x00200626` 说明 `SMMUEN=0`，于是 HFI 队列表可以**直接用
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
- `sCR0=0x00200626`：bit0 `SMMUEN=0`、bit1 `CLIENTPD=1` ⇒ SMMU 未使能，GMU 流量
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
