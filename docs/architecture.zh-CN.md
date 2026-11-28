[English](architecture.md) | 简体中文

# 架构与代码导航

## 控制面与数据面

`software/main/app_main.c` 初始化板级资源，然后进入当前 `bench_bridge_run()`。名称中保留了 bench 历史，但这里实际承担当前固件的主控制流程。

- **控制 owner**：`bench_bridge.c` 统一协调 DSP、功放、Web / TCP 请求、音源以及停止 / 故障状态。
- **共享数据面**：`chime_render.c` 拥有唯一连续 I2S 写入任务；`chime_usb.c` 负责 UAC2/HID，`chime_mixer.c` 混合独立控制的 USB、蓝牙通道。单路重启或欠载不会停止另一通道。
- **蓝牙**：`chime_bluetooth.c` 管理连接和 A2DP / AVRCP，`chime_bt_decoder.c` 管理解码 worker，`chime_bt_pcm.c` 管理 PCM 水位与缓冲，`chime_bt_delay.c` 管理接收端延迟估计。
- **DSP / 功放**：`chime_dsp_boot.c`、`bench_dsp.c`、`bench_audio.c`、`bench_sound.c` 配合 `board_support` 和 `dsp_control` 执行受控访问。
- **交互**：`chime_input.c` / `chime_controls.c` 处理编码器与按键；`chime_led.c` / `chime_indicator.c` 处理 LED。
- **网络**：`chime_network.c`、配网 guard、`bench_web.c` / `bench_status.c` 提供配网、状态与手动控制。

两路输入分别使用 epoch；generation 用于空闲播放准入，蓝牙连接不再抢占 USB。本地静音、待机、Web STOP、外部 DSP 所有权和硬故障不能被普通音量操作擅自清除。上述设计意图不代表所有并发和异常组合均已实板验收。

## Codec 边界

| 路径 | 责任 |
| --- | --- |
| Espressif `esp_audio_codec` | 当前外部 SBC、AAC-LC、Opus API |
| `software/components/chime_freeaptx/` | aptX / aptX HD 后端；LGPL 义务需单独履行 |
| `software/components/chime_ldac/` | 当前 LDAC 薄封装 |
| `software/components/chime_lhdc/` | 当前 LHDC V5 薄封装 |
| `software/components/chime_codec_reference/` | 唯一 reference LDAC / LHDC backend、上游来源 pin 和生成器 |

vendor 原始源、构建目录的 port 生成源、两层薄封装是同一套后端的不同层次，不是多套可并行选择的旧解码器。旧 `chime_ldac/src` / `chime_lhdc/src` 不再保留。

## DSP 数据流

`dsp/esp32-bindings.json` 按模块名绑定输入和输出增益，`scripts/freeze-dsp-image.py` 将编译数据转为 `software/main/generated/` 下的冻结镜像、参数头和来源 manifest。构建时再次校验；输出模式切换保留启用支路的编译增益，不强制写 unity。

当前允许板级 L / R / Sub 路由，但不能从输出模式名称推断所有滤波、限幅、超低频或热保护已经满足产品要求。

## 硬件映射

GPIO / I²C 地址的代码权威入口是 `software/components/board_support/include/board_support.h`，使用前须与具体 PCB 核对。

| 功能 | 当前代码配置 |
| --- | --- |
| I²C SDA / SCL | IO0 / IO1 |
| ADAU1451 / LR 功放 / Sub 功放地址 | `0x38` / `0x4c` / `0x4d` |
| TDM DOUT / BCLK / FS | IO9 / IO10 / IO11 |
| 编码器 A / B / 按键 | IO42 / IO40 / IO43，按键低有效 |
| LR / Sub PDN | IO15 / IO13 |

这不是通用 ESP32 开发板固件；改硬件后必须审查启动状态、时钟、USB、故障和电源控制，而不只是改几个 GPIO。

## 双路混音与操作

- USB 仍为 48 kHz 双声道 PCM16，保留异步 FIFO 反馈；USB 静音时也继续消费。蓝牙独立缓冲并修正时钟漂移。
- 内部 PCM 为 Q5.27、增益为 Q2.30，使用 64 位乘加，最终输出时统一舍入到 24 位有效数据并装入 32 位 I2S 时隙。直接相加，不设固定衰减、归一化、闪避或单路自动补偿。两路较响时可能削波；最终饱和防止整数回绕，`MIX FLOW` 提供削波计数与 Q27 峰值，但不是扬声器功率保护。
- 音量、静音和缺流尾部采用 480 帧斜坡。短按只切换本地全局静音，不发送 HID Mute；静音期间两路继续推进。长按仍为待机，STOP、硬故障和外部 DSP 所有权独立生效。
- 旋转只发送有界的 HID 音量增减按下/释放报告，不写 AVRCP，也不在设备内重复修改 UAC 音量。HID 通常影响 PC 默认输出，不能保证定向控制任意端点；请将 Chime 设为默认输出。每格步长由操作系统定义。挂起/断连丢弃待发送操作，恢复先发送中立报告。
- 正常播放采用混音；手动授权的外部 SigmaStudio 工程仍仅允许 USB 试听。手动正弦波保留现有受保护控制序列。

## 蓝牙 SRC

`chime_bt_pcm.c` 在蓝牙锁内复制有界输入窗口，`chime_bt_src.c` 在锁外执行 FIR。采用 192 taps、256 分数相位和系数插值、Q2.30 系数、32 位小数相位；支持 16/32/44.1/48 kHz 输入并输出 48 kHz。三组按采样率区分的系数通过 `scripts/generate-bt-src.py` 可重复生成，运行时不分配系数或计算三角函数。队列反馈限制在 +/-2000 ppm，每 128 帧最多变化 1 ppm。

48 kHz 内核将当前系数相位行缓存在任务栈上的 1536 字节 RAM 中，热点代码放入 IRAM。分数漂移仍经过完整 FIR：仅在整除条件检查能证明与原向零截断完全等价时，才用一次高位乘法省去每 tap 的插值余数处理；特殊分数位置保留原有符号除法。步进恰好为 1 时只是在块内复用插值后的系数，不是旁路滤波。仅 SRC 使用 `-O3`。系数、64 位累加、Q27 输出舍入以及队列/漂移策略均不变，不以降低滤波质量换性能。主机逐位参考测试覆盖全部相位行、插值整除边界、完整音频块与 +/-2000 ppm；实板耗时仍需复测。

FIR 需要 192 帧输入上下文，队列消费前已核算这一需求；重置和重缓冲不复用旧滤波历史。本次保留原有自适应缓冲目标，不同时压缩延迟。蓝牙延迟报告仍是未标定估计，不代表 USB 与蓝牙同步播放。

`node scripts/test-mixing.mjs` 使用主机桩执行实际 mixer、renderer、SRC，覆盖 16k/3kHz、32k/12kHz、44.1k/20kHz 信号及指定镜像、声道对称、相位端点、DC 精度。每个输入采样率还有 +/-300 ppm、各十分钟的时钟模拟，但不模拟无线和 CPU 调度。这不是完整频谱认证或 24 小时实板验收。实板需要检查 `MIX FLOW compute_us` 的最大值 相对 2667us 块周期的余量，并测试 USB、各 codec 解码与 Wi-Fi 并发。

### 核绑定与 ESP32-S31 PIE 实验

正常播放在 **CPU0** 上运行逐位一致的标量 FIR，双核配置下将解码任务固定在 **CPU1**。渲染任务优先级保持 6、解码保持 5，渲染栈为 8 KiB。启动日志打印实际运行的核以及 `src=scalar pie_probe=disabled`，不在 PCM 任务中执行 PIE 自检。

S31 的 PIE **仅在 CPU1 可用**：任务在 CPU0 执行 PIE 指令时，IDF RISC-V 移植层会覆盖原来的亲和性，将任务迁移到 CPU1；协处理器的延迟上下文管理也会将任务绑核。PIE 测速后选择回退标量，不会撤销迁移。因此，在渲染任务中探测 PIE 会使它与优先级更低的解码任务共享 CPU1，即使最后没有启用 SIMD。实板故障表现也从 PCM 队列持续增长，变为原始包积压、PCM 队列接近耗尽和缺块。同一次启动对比测得每 128 帧标量 2074us、PIE 2215us，该候选实现并未提速；这些墙钟时间不是通用 CPU 时间基准。

原有 PIE 候选代码仅在显式指定编译宏 `CHIME_SRC_PIE_BENCHMARK=1` 或运行主机数学模型时编译，正常播放路径不包含该候选。独立探测必须由专用 CPU1 任务运行，在 CPU0 上调用会在执行 PIE 前直接拒绝。不应仅为了使用该候选就将正常渲染移到 CPU1，而不重新设计、验证解码与渲染的调度分工。

候选将插值后的 Q2.30 系数精确拆成 `c = hi * 65536 + lo`，高低两部分均为有符号 16 位数；四次 192-tap 向量点积在 64 位整数中重组，再使用原有 Q27 舍入，不量化成 Q15。生成器验证 `abs(c) <= 2^30`，保证两部分均可表示；最坏累加值 `192 * 32768^2` 在有符号 40 位 XACC 范围内。缓冲均为 16 字节对齐的局部工作区，不改变 CFG/SAR 或硬件循环状态。独立探测验证满幅极值点积及 6415 个 SRC 作业，并对比七次计时的中位数。主机测试验证系数拆分和探测选择策略，不是 PIE 指令模拟，也不代表实板并发验收。

### 渲染耗时与 PCM 溢出诊断

`MIX FLOW` 的 `compute_us`、`src_us`、`write_us`、`period_us` 按
`最近值/最大值` 输出微秒数，另有 `deadline_misses`。`src_us` 包含队列准备与
FIR，`write_us` 包含正常的 I2S DMA 等待。所有时间都是包含任务抢占的墙钟时间，
不能仅凭一次最大值就认定持续算力不足；应结合超时增长、块数推进、队列占用和
解码耗时判断。不再每次计算超时都额外休眠，由 I2S 写入提供节拍。
若写入耗时持续小于 100us 达 20ms，保留一次 tick 的调度让步（`unpaced_yields`），
避免持续无阻塞过载占满 CPU、饿死控制 owner；写入墙钟时间只是阻塞的启发式判断。
STOP 在 owner 等待渲染互斥锁之前提出。

`pcm-overflow` 表示固定 8192 帧的输入 PCM 队列无法容纳新的数据块。
此路径的 `error=257`（`ESP_ERR_NO_MEM`）标记队列已满，并不是堆内存耗尽的证据。
故障日志在失效清空前保存 pushed、consumed、rendered、discarded 计数。
分别测试 LDAC 单独播放、与 USB/Wi-Fi 并发，并收集这些计数、周边 `MIX FLOW`
和蓝牙队列/解码日志。诊断不通过自动丢帧或扩大队列掩盖溢出。
RV32 插值优化在全部相位行和支持的采样率上，与原 64 位算法逐位对照；
滤波长度、系数和增益不变。主机测试仍不代表实板实时性能已经达标。

### 区分生产端突发与消费不足

队列满**不能单独证明 FIR 太慢**。LDAC 包在发布 PCM 前会完整校验包内帧布局，
每个解码帧必须输出 512 字节，即 128 帧双声道 PCM16。压缩包队列最多保留
12 包，目前没有 PCM 队列剩余容量的反馈。48kHz 下，4608 帧启动目标给
8192 帧队列留下 3584 帧（74.7ms）空间；足够大的集中推入即使没有慢 FIR
也可能填满余量。主机容量反例只证明这种可能性，不代表已复现某部手机的故障。

故障锁存后，在串口输出之前抓取以下快照：

- `fault MIX`：实际就绪的 SRC 块数、平均墙钟耗时、已完成块的各阶段耗时，
  以及正在执行 SRC 的标志和经过时间。平均值排除预缓冲空调用。
  若恰好在 FIR 中抓取快照，队列的 prepare/consumed 可能领先已完成 SRC 一个块。
- `fault DECODE`：包含当前已取出包的原始队列等待/深度，已校验的 LDAC 帧数，
  以及在 PCM 入队回调**之前**更新的已解码 PCM 帧数。溢出时当前帧可能只入队一部分，
  因而该计数不能等同于队列 `pushed`。完整包处理耗时仍不包含正在执行的包。
  时间戳增量是 SDK 原始单位，相同时间戳/CRC 仅作为观察，不据此去重或拒绝；
  合法分片、静音和 CRC 碰撞都需要注意。
- `fault START`：相对输出准入的首次 PCM 到达、首次 prepare 成功时刻，及当时队列/目标。
  `UINT32_MAX` 表示未发生，不是零，用来区分输入/打开解码器等待和填充时间。
- `fault FLOW`：最近最多 16 个 20ms 墙钟分段，记录成功推入/消费的输入帧数、
  接受的 RX 包数和队列水位。它是固定时间桶，不是精确滑动窗口峰值；首尾桶可能
  不完整，消费计数反映 prepare，而不是 I2S 已完成输出。

连续多个桶消费偏低、队列持续上涨，与某个桶生产量突增是不同现象，需要结合
原始包等待和实际 SRC 耗时判断；单个最大值和队列记账恒等式都不能证明根因。
此次诊断不新增衰减、自动丢帧、扩队列或解码背压。记录器使用固定存储、每次输出
准入重置，在已有蓝牙锁内更新，仅故障时打印；无运行时分配和逐成功块串口输出。
