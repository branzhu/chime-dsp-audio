[English](README.md) | 简体中文

# Chime · DSP Audio

基于 **ESP32-S31 + ADAU1451 + TAS5825M** 的桌面 DSP 音箱研发项目，包含固件、SigmaStudio 工程、PCB 工程导出文件和外壳交换模型。

USB / 蓝牙负责音频输入，ADAU1451 承担音频处理，ESP32 负责音源、控制与状态管理。项目优先维持单一音频输出链路和受控的 DSP / 功放操作，而不是在不同功能中各建一套播放逻辑。

> **当前是研发 / bench 版本，不是经过量产、安全或声学认证的成品方案。** 部分功能只有主机回归与目标构建证据。自有材料已采用分层许可，但引用的 LDAC 与部分参考适配代码仍有授权待确认事项；当前内容是公开发布准备稿，不能据此认为所有材料已获开源或商业分发授权。详见[许可与第三方材料](docs/licenses.md)。

## 能做什么

- **USB Audio**：48 kHz、双声道 PCM16 输入，共享 PCM renderer 输出到 DSP。
- **蓝牙 A2DP**：代码支持 SBC、AAC-LC、aptX、aptX HD、Android Opus、LDAC、LHDC V5；后五项属于实验扩展。LDAC / LHDC 目前只暴露 44.1 / 48 kHz 双声道 PCM16，LHDC 不支持 V2 / V3。
- **音源选择**：A2DP 连接时选择蓝牙，暂停仍占源，断开后返回 USB；不混音。
- **DSP 工程同步**：按工程中的模块名称绑定参数，校验 SigmaStudio 工程、编译产物和固件冻结镜像的一致性。
- **本地操作**：旋钮调整所选音源音量，短按本地静音，长按软件待机 / 唤醒；RGB LED 指示状态。
- **调试与控制**：Web 配网 / 状态 / 手动控制，以及用于可信局域网调试的 SigmaStudio TCP bridge。
- **主机测试**：覆盖协议、音源切换、PCM、codec、输入、LED、Web 和冻结 DSP 数据。

蓝牙 codec 表示当前源码与配置中存在的能力，**不代表所有手机、码率、射频环境或长播场景已通过验证，也不代表官方认证**。后端来源和限制见[架构](docs/architecture.md)与[许可说明](docs/licenses.md)。

## 系统结构

```text
USB Audio ───────┐
                ├─ ESP32-S31：音源选择 / 解码 / 共享 PCM renderer
Bluetooth A2DP ─┘                    │ I2S / TDM
                                     ▼
                               ADAU1451 DSP
                                     │
                               TAS5825M × 2
                                     │
                              L / R / Sub 输出

旋钮 / LED / Web ── 控制与状态 ── DSP / 功放单一 owner
```

Sub 使用两个独立 BTL 输出通道；**禁止在外部直接并联功放输出**。软件保护不等同于硬件安全保证，试听前请阅读[安全与已知限制](docs/safety.md)。

## 目录

| 路径 | 内容 |
| --- | --- |
| [`software/`](software/) | ESP-IDF 固件、组件及主机测试 |
| [`dsp/`](dsp/) | SigmaStudio 工程、编译输出与 ESP32 参数绑定 |
| [`hardware/`](hardware/) | PCB 工程导出文件；当前为 `audio-dsp.epro2`，不是 Gerber 制造包 |
| [`mechanical/`](mechanical/) | 对外保留 STEP / 3MF 交换模型，不发布 Creo 原生源文件 |
| [`scripts/`](scripts/) | 构建辅助、DSP 同步、测试与公开快照导出工具 |
| [`tools/wireshark/`](tools/wireshark/) | SigmaDSP 协议分析工具 |
| [`docs/`](docs/README.md) | 面向使用者的精简文档 |
| [`assets/branding/`](assets/branding/) | 项目标识素材 |

研发仓库可能包含更多内部记录；公开候选快照只按白名单收录上述必要内容，不携带原仓库的 Git 历史。

## 构建与测试

使用当前已验证的 **ESP-IDF 6.1 / `esp32s31`** 工具链，并先进入 ESP-IDF 的环境：

```sh
idf.py --preview -C software build
```

首次构建会下载锁定版本的 Espressif 组件。完整命令、主机编译器与测试分类见[构建与测试](docs/build.md)。

构建不是烧录。**不要把新应用单独刷入旧分区表，不要为恢复联网随意擦除 NVS，也不要忽略音量变化警告。** 本项目不提供未经板级确认的一键上电发声流程。

## 硬件与机械资料

- [PCB 文件说明](hardware/README.md)：打开格式、当前交付范围及制造前检查。
- [外壳文件说明](mechanical/README.md)：STEP / 3MF 的用途及 Creo 源文件边界。
- 这些文件仅描述当前研发版本，不构成加工、装配、电气或声学验收结论。

## 文档

- [构建与测试](docs/build.md)
- [架构与代码导航](docs/architecture.md)
- [安全、验证边界与已知问题](docs/safety.md)
- [许可与第三方材料](docs/licenses.md)
- [公开发布流程](docs/publishing.md)

## 上游项目与致谢

Chime 引用了 **WillyBilly06** 及其他上游贡献者的工作：

- **[ESP32-A2DP-SINK-WITH-CODECS-UPDATED](https://github.com/WillyBilly06/ESP32-A2DP-SINK-WITH-CODECS-UPDATED)**：本项目集成的部分 LDAC 解码实现、LDAC / LHDC V5 适配代码及 codec 生命周期辅助代码来源于该项目。当前引入版本固定为 [提交 `45281c5`](https://github.com/WillyBilly06/ESP32-A2DP-SINK-WITH-CODECS-UPDATED/tree/45281c5212cf5e22eb1d804e75439d2ac051f7c0)，具体文件与哈希见 [引入清单](software/components/chime_codec_reference/UPSTREAM.json)。
- **[LHDC-V5-Decoder](https://github.com/WillyBilly06/LHDC-V5-Decoder)**：引入快照中便携式 LHDC V5 解码器的上游项目，对照版本为 [提交 `cf857ce`](https://github.com/WillyBilly06/LHDC-V5-Decoder/tree/cf857ce1a1429965e161e54adbc3f293fd9ce273)。[快照记录](software/components/chime_codec_reference/vendor/native_bt/codecs/lhdcv5/source_snapshot/SOURCE.json) 区分了匹配文件与两份带工作修改的文件。
- **freeaptx**：aptX / aptX HD 实现通过同一 ESP32 参考仓库的较早 [提交 `0d85a97`](https://github.com/WillyBilly06/ESP32-A2DP-SINK-WITH-CODECS-UPDATED/tree/0d85a975ec079d954f1bdd3e94d2fed70848c252) 引入；原作者与 LGPL-2.1-or-later 条款按源文件及 COPYING 保留，来源见 [溯源清单](software/components/chime_freeaptx/UPSTREAM.json)。

本项目只集成参考项目中选定的 codec 相关部分，没有链接其自定义蓝牙 host、codec worker 或 renderer 来替代 Chime 现有链路。感谢上游作者与贡献者公开分享相关工作。

致谢用于说明来源，不代表全部上游内容适用同一许可证；逐项许可及授权待确认事项见 [第三方声明](THIRD_PARTY_NOTICES.md)。

## 许可证

- **自有软件与原创 DSP 工程配置贡献**：[Apache-2.0](LICENSE)。
- **原创 PCB 设计贡献**：[CERN-OHL-P-2.0](LICENSES/CERN-OHL-P-2.0.txt)。
- **自有公开文档与四份 STEP / 3MF 模型**：[CC-BY-4.0](LICENSES/CC-BY-4.0.txt)；不包含未发布 Creo 源文件。
- **第三方内容**：按原许可或授权待确认状态处理，见 [第三方声明](THIRD_PARTY_NOTICES.md)。Logo / 名称 / 商标不纳入上述许可。

本项目也是 LDAC / 参考适配代码的引用者，尚未确认完整授权情况；使用者应结合上游资料和自身用途自行评估。该说明不替代授权，也不补足缺失许可。完整范围见 [LICENSES/README.md](LICENSES/README.md)。

## 反馈与贡献

提交问题时请附上硬件版本、固件提交、ESP-IDF 版本、音源 / 手机及协商 codec、复现步骤和经过脱敏的日志。不要附带 Wi-Fi 凭据、私人录音或整份个人开发环境信息。

贡献请遵循对应内容的许可范围，保留来源并确认有权提交；第三方代码、硬件文件、机械交换模型及商标不因出现在同一个仓库就自动适用同一种许可证。
