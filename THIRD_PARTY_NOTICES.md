# Third-party notices / 第三方材料说明

本清单记录当前选定来源与已核实的声明，不是完整 SDK SBOM、法律意见或第三方权利保证。项目自有许可仅按 [LICENSES/README.md](LICENSES/README.md) 的范围适用；第三方材料保留原条款。来源公开、保留哈希或引用链接均不等于已取得全部再分发权利。

## 当前蓝牙 codec 来源（2026-11-28）

现有七种外部解码后端统一引用
[WillyBilly06/ESP32-S31-A2DP-Codecs](https://github.com/WillyBilly06/ESP32-S31-A2DP-Codecs)，
提交 `71071a783811799d2977e4e87f733acdc2220c29`。
540 个导入文件及逐文件 SHA-256 见
`software/components/chime_codec_reference/UPSTREAM.json`；实际源码位于其
`vendor/s31/`，构建时的适配见 `prepare.py` 与组件 README。
未导入上游应用、蓝牙 host 或 renderer；没有整体替换安装的 ESP-IDF。

| 后端 | 路径（相对于 vendor/s31/） | 声明与限制 |
| --- | --- | --- |
| SBC / OI | external/sbc/decoder | 源文件保留 Android / Open Interface North America 版权、Apache-2.0 条款 |
| AAC / Helix | external/libaac-lc/libhelix_aac | RealNetworks RPSL / 可适用的 RCSL；保留源文件许可块，不是 MIT，也不属于项目 Apache-2.0 重新授权 |
| aptX / aptX HD | external/libfreeaptx/main | 源码 LGPL-2.1-or-later；COPYING 原文保留。父目录 CC0 LICENSE **不能覆盖**具体源文件 LGPL 声明 |
| Opus | external/opus | 保留 COPYING 的 BSD-style 条款及 LICENSE_PLEASE_READ.txt / IETF IPR 信息；专利声明与著作权许可分别核对 |
| LDAC | external/libldac-dec | Sony 声明等按源文件保留；尚未核实完整独立解码实现的授权链和再分发许可，不能套用 AOSP 编码器许可 |
| LHDC V5 | external/lhdc-v5 | 选定 S31 仓库未提供可确认覆盖该修改版本的统一许可；不能仅凭另一仓库历史版本 Apache 声明覆盖本次快照 |
| LATM parser | stack/a2dp/latm_parser.c,h | 源文件 Apache-2.0 声明保留 |

**LDAC、LHDC 及来源不完整部分的授权链、再分发和 codec 专利 / 产品认证仍待核实。**
本项目也是引用者；固定哈希、来源公开、用户指定来源、保留版权并不补足缺失许可，
也不代表维护者可以据此合法再分发。上述第三方材料不被本项目重新统一授予
Apache-2.0、MIT 或 CC。公开导出继续 fail-closed，不能把固件构建成功当成法律清关。

freeaptx 的版权包括 Aurelien Jacobs、Pali Rohár、Hunter Wardlaw。当前固件静态链接
所选 S31 仓库中的该库；分发需按采用的 LGPL 版本满足源码、修改、许可告知、
重新链接与必要调试逆向工程等义务，不能只提供 `.bin` 或上游链接。
完整 LGPL 文本另见 [LGPL-2.1.txt](LICENSES/LGPL-2.1.txt)。

旧 `software/components/chime_freeaptx/` 及其清单只作历史保留，其 CMake 不再编译源码。
旧 native_bt / LHDC snapshot 已退出当前组件；历史
[LHDC-V5-NOTICE.md](LICENSES/LHDC-V5-NOTICE.md) 不是当前 S31 快照的完整授权证明。

## ESP-IDF 和 managed components

当前版本见 `software/dependencies.lock`，实际下载版本的文件头、LICENSE 和 NOTICE 优先。ESP-IDF 内部含多种第三方许可；下表不是对所有传递依赖的统一授权。

| 组件 | 当前版本 | 已检查许可 / 要求 |
| --- | --- | --- |
| ESP-IDF | 6.1.0 | 根 LICENSE 为 Apache-2.0；内部第三方源代码、控制器及无线库继续按各自许可和 NOTICE |
| espressif/mdns | 1.8.0 | Apache-2.0；保留适用声明 |
| espressif/esp_tinyusb | 2.4.0 | Apache-2.0；保留适用声明 |
| espressif/tinyusb | 0.21.0~2 | MIT；Copyright (c) 2012-2026, hathach (tinyusb.org)；全文见 [TinyUSB-MIT.txt](LICENSES/TinyUSB-MIT.txt) |
| espressif/cjson | 1.7.19 | MIT；Copyright (c) 2009-2017 Dave Gamble and cJSON contributors；全文见 [cJSON-MIT.txt](LICENSES/cJSON-MIT.txt) |
| espressif/esp_audio_codec（历史，已移除） | 2.6.2 | LicenseRef-Espressif-Modified-MIT；Copyright (c) 2025 Espressif Systems (Shanghai) CO., LTD；**仅限结合乐鑫产品使用，禁止为非乐鑫产品用途再分发**；全文见 [许可原文](LICENSES/LicenseRef-Espressif-Modified-MIT.txt) |

当前构建已从 manifest / lock 移除 `esp_audio_codec`。其历史许可证不是标准 MIT，也不是通用、无硬件限制的开源许可。包内其他第三方 codec 还应按实际声明核查；其 README 指向 [Espressif Copyrights and Licenses](https://docs.espressif.com/projects/esp-adf/en/latest/COPYRIGHT.html)。本项目不重新授权这些依赖，不以自有 Apache-2.0 许可解除其限制。

## USB 描述符

`software/main/include/chime_usb_descriptors.h` 含 HiFiPhile 的 MIT 许可声明：Copyright (c) 2023 HiFiPhile。原文件头保持不变，另提供 [USB-descriptors-MIT.txt](LICENSES/USB-descriptors-MIT.txt)。

## DSP、测试数据、PCB 和机械模型

- SigmaStudio 工程的原创配置与设计贡献按项目范围许可；Analog Devices 工具、算法模块 / 库和生成物中第三方内容仍需按其条款确认分发权利。不打包安装器或驱动 DLL，不把整个生成程序宣称为项目独立原创。
- Codec 测试流与生成器按各自 `PROVENANCE.json` 保留出处；编码器许可与测试流的权利分别判断。原始事实、测量和协议夹具不构成对厂商手册或驱动的授权。不打包私人录音或厂商手册副本。
- PCB / 机械文件的项目许可只覆盖有权授权的原创部分，不覆盖嵌入的第三方封装、3D 模型、图片、品牌或其他素材。发布前需核对实际文件内容与权利。

许可文本、来源说明、专利与商标许可、安全合规是不同事项。未经许可不得宣称获得 Sony、Savitech、Espressif、Analog Devices 或其他厂商的官方认证或背书。
