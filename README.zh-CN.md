[English](README.md)

# Chime

Chime 是我折腾的一套桌面 2.1 音箱，起点是一台在架子上吃灰的 Creative Stage SE。它在我习惯的小音量下听起来不太对劲，但箱体和低频单元还有利用价值。于是我保留了 Stage SE 的箱体和喇叭，让它负责低频，另外做了一对 3D 打印的小卫星箱，再用自己的 PCB 替换掉原装控制板。

主控是 ESP32-S31，负责 USB、蓝牙音频和系统控制；ADAU1451 负责 DSP；两颗 TAS5825M 负责功率放大。我想要的是一套能自己测、自己调、想改就改的桌面音响。

仓库里有固件、SigmaStudio 工程、PCB 设计导出文件和箱体模型。目前还是个人研发项目，不是一套拿到文件就能直接照着量产的成品方案。

## 硬件与音频链路

```text
USB Audio ───────┐
                 ├── ESP32-S31 ── I2S / TDM ── ADAU1451 DSP ── TAS5825M × 2 ── L / R / Sub
Bluetooth A2DP ─┘       │
                        └── 编码器 / LED / Wi-Fi Web UI
```

两只卫星箱用的是 GP52-463 两英寸单元、3D 打印密闭箱；改装后的 Stage SE 负责低频。它的两个喇叭分别由独立的 BTL 功放通道驱动，**不能把功放输出端直接并联**。通电前请先看[安全说明和已知问题](docs/safety.md)。

## 目前能做什么

- **USB Audio**：48 kHz、双声道、16-bit PCM。
- **蓝牙 A2DP**：SBC、AAC-LC，以及实验性的 aptX、aptX HD、Android Opus、LDAC 和 LHDC V5 路径。源码里有不等于各种手机、码率和长时间播放都验证过。LDAC/LHDC 目前输出 44.1/48 kHz 双声道 PCM16；不支持 LHDC V2/V3。
- **音源切换**：蓝牙连接后切到蓝牙；暂停不切回 USB；断开蓝牙后回到 USB。目前不做 USB/蓝牙混音。
- **DSP 控制**：SigmaStudio 工程、编译数据和固件里的 DSP 参数会做一致性检查。
- **本机操作**：编码器调音量，短按静音，长按待机/唤醒；RGB LED 显示状态。Web 页面提供 Wi-Fi 配网和手动控制。
- **调试**：可信局域网内可通过 TCP bridge 连接 SigmaStudio；另外还有覆盖音频、控件、codec、DSP 数据等的主机测试。

DSP 现在主要干的还是分频、PEQ、Gain 这些基础工作。当初想玩的 Dynamic Loudness、Virtual Bass 等高级算法，还在待办列表里。

## 编译固件

目前使用 **ESP-IDF 6.1**，目标芯片 `esp32s31`。进入 ESP-IDF 环境后，在仓库根目录运行：

```sh
idf.py --preview -C software build
```

首次编译会下载锁定版本的 Espressif 组件。其他构建命令和主机测试方法见[构建文档](docs/build.md)。

暂时没有整理成一键烧录流程。尤其不要把新应用直接刷到旧分区表上，也别为了恢复 Wi-Fi 就随手擦掉 NVS；相关注意事项见[安全与已知问题](docs/safety.md)。

## 仓库里有什么

| 目录 | 内容 |
| --- | --- |
| [`software/`](software/) | ESP-IDF 固件、组件及主机测试 |
| [`dsp/`](dsp/) | SigmaStudio 工程、生成数据、ESP32 参数绑定 |
| [`hardware/`](hardware/) | EasyEDA PCB 导出文件 `audio-dsp.epro2`，**不是** Gerber 制造包 |
| [`mechanical/`](mechanical/) | STEP / 3MF 箱体模型；未公开 Creo 原生文件 |
| [`scripts/`](scripts/) | 构建、DSP 同步、测试及发布脚本 |
| [`tools/wireshark/`](tools/wireshark/) | SigmaDSP 协议分析工具 |
| [`docs/`](docs/README.md) | 技术文档 |

公开仓库是从开发仓库整理出来的快照，不包含完整开发历史。要加工 PCB 或箱体的话，先看 [PCB 说明](hardware/README.md)和[箱体文件说明](mechanical/README.md)。

## 文档

- [架构与代码导航](docs/architecture.md)
- [构建与测试](docs/build.md)
- [安全与已知问题](docs/safety.md)
- [许可与第三方代码](docs/licenses.md)
- [公开发布流程](docs/publishing.md)

## 致谢与许可

蓝牙 codec 部分引用了 [ESP32-A2DP-SINK-WITH-CODECS-UPDATED](https://github.com/WillyBilly06/ESP32-A2DP-SINK-WITH-CODECS-UPDATED)、[LHDC-V5-Decoder](https://github.com/WillyBilly06/LHDC-V5-Decoder) 和 **freeaptx** 的工作。感谢 WillyBilly06 和其他上游作者。具体导入的版本和文件都记录在 [codec 引入清单](software/components/chime_codec_reference/UPSTREAM.json)、[LHDC 快照记录](software/components/chime_codec_reference/vendor/native_bt/codecs/lhdcv5/source_snapshot/SOURCE.json)和 [freeaptx 清单](software/components/chime_freeaptx/UPSTREAM.json)里。我只拿了需要的 codec 相关部分，没有替换 Chime 自己的蓝牙 host 或音频 renderer。

仓库不同部分使用不同的许可证：

- 自己写的固件和原创 DSP 工程配置：[Apache-2.0](LICENSE)。
- 自己设计的 PCB：[CERN-OHL-P-2.0](LICENSES/CERN-OHL-P-2.0.txt)。
- 自有文档和四份公开的 STEP / 3MF 模型：[CC BY 4.0](LICENSES/CC-BY-4.0.txt)。

**部分引入的 LDAC 和适配代码，转发/再分发权限仍未完全确认。** 上面这些许可证不覆盖它们、其他第三方材料和项目标识。使用或重新分发前请看[第三方声明](THIRD_PARTY_NOTICES.md)与[许可范围](LICENSES/README.md)。

## 反馈

欢迎提 Issue 或建议。报问题时最好带上硬件版本、固件 commit、ESP-IDF 版本、音源/手机、协商的 codec（如果有）和能复现问题的日志。Wi-Fi 密码等私人信息记得删掉。
