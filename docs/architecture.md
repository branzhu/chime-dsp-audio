# 架构与代码导航

## 控制面与数据面

`software/main/app_main.c` 初始化板级资源，然后进入当前 `bench_bridge_run()`。名称中保留了 bench 历史，但这里实际承担当前固件的主控制流程。

- **控制 owner**：`bench_bridge.c` 统一协调 DSP、功放、Web / TCP 请求、音源以及停止 / 故障状态。
- **共享数据面**：`chime_usb.c` 包含当前 USB / BT 共享 PCM worker 与唯一 I2S TX 路径；音源选择不是额外创建一个蓝牙输出任务。
- **蓝牙**：`chime_bluetooth.c` 管理连接和 A2DP / AVRCP，`chime_bt_decoder.c` 管理解码 worker，`chime_bt_pcm.c` 管理 PCM 水位与缓冲，`chime_bt_delay.c` 管理接收端延迟估计。
- **DSP / 功放**：`chime_dsp_boot.c`、`bench_dsp.c`、`bench_audio.c`、`bench_sound.c` 配合 `board_support` 和 `dsp_control` 执行受控访问。
- **交互**：`chime_input.c` / `chime_controls.c` 处理编码器与按键；`chime_led.c` / `chime_indicator.c` 处理 LED。
- **网络**：`chime_network.c`、配网 guard、`bench_web.c` / `bench_status.c` 提供配网、状态与手动控制。

源切换使用 epoch / generation 让旧 PCM 和排队操作失效。本地静音、待机、Web STOP、外部 DSP 所有权和硬故障不能被普通音量操作擅自清除。上述设计意图不代表所有并发和异常组合均已实板验收。

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
