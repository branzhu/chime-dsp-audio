# Third-party notices / 第三方材料说明

本清单记录当前选定来源与已核实的声明，不是完整 SDK SBOM、法律意见或第三方权利保证。项目自有许可仅按 [LICENSES/README.md](LICENSES/README.md) 的范围适用；第三方材料保留原条款。来源公开、保留哈希或引用链接均不等于已取得全部再分发权利。

## LDAC 与参考适配代码：授权待确认

相关代码引用自 [WillyBilly06/ESP32-A2DP-SINK-WITH-CODECS-UPDATED](https://github.com/WillyBilly06/ESP32-A2DP-SINK-WITH-CODECS-UPDATED/tree/45281c5212cf5e22eb1d804e75439d2ac051f7c0)，固定提交为 `45281c5212cf5e22eb1d804e75439d2ac051f7c0`。

本项目也是相关代码的引用与集成者。维护者尚未确认以下代码完整的著作权授权链、适用许可证和再分发条件，无法保证其适用于每个使用场景。**使用者应结合上游资料、适用法律和自身用途，自行评估使用、修改及再分发所需权利；必要时联系权利人或专业顾问。**

- LDAC：`software/components/chime_codec_reference/vendor/esp-idf/components/bt/host/bluedroid/external/libldac-dec/`。原 `inc/ldacBT.h` 标有 `Copyright (C) 2013 - 2017 Sony Corporation`；检查的解码实现目录没有明确的独立许可证，所选参考仓库根目录也未找到统一授权声明。不能套用 AOSP 编码器许可或已删除旧解码器的 MIT 许可。
- 参考适配：上述组件中引入的 `vendor/native_bt/` 适配代码（单独声明许可的 LHDC decoder snapshot 除外）。尚未核实覆盖这些适配实现的明确许可。引用仓库同一目录内可能混有不同来源，不能将 LHDC 的 Apache 声明扩展到所有适配文件。
- 实际选取文件、来源提交与 SHA-256 由组件的 `UPSTREAM.json` 记录。Chime 的生成 / 适配修改见组件 README、`prepare.py` 及生成记录；这些记录不补足上游授权。

**上述代码不被本项目重新授予 Apache-2.0、MIT、CC 或其他统一许可。本说明及“自行评估”不是免责或授权文件，不补足缺失的第三方许可，也不代表维护者可据此合法再分发。** 本地候选导出仍标注授权待确认；正式发布前应补齐足够授权证据，或制作实际移除相应源码 / 依赖且通过构建验证的版本。

## LHDC V5 decoder snapshot

上游：[WillyBilly06/LHDC-V5-Decoder](https://github.com/WillyBilly06/LHDC-V5-Decoder/tree/cf857ce1a1429965e161e54adbc3f293fd9ce273)。该固定提交 README 的 License 段明确声明解码器实现采用 **Apache-2.0**，虽然根目录没有独立 LICENSE 文件。原文出处与许可摘录保存在 [LHDC-V5-NOTICE.md](LICENSES/LHDC-V5-NOTICE.md)。

本地 snapshot 的 `SOURCE.json` 记录十四份匹配文件及两份带工作修改的 IMDCT 文件；应保留修改来源，不能只根据哈希推定修改授权。该实现声明与 LHDC codec 专利、商标、产品认证及 Savitech 的其他权利是不同事项，不捆绑任何专有编码器二进制。

## freeaptx — LGPL-2.1-or-later

路径：`software/components/chime_freeaptx/`。来源提交 `0d85a975ec079d954f1bdd3e94d2fed70848c252`，文件与哈希见该目录 `UPSTREAM.json`；权利按源文件及 COPYING，而非引用仓库可能出现的根许可推定。

- Copyright (C) 2017 Aurelien Jacobs
- Copyright (C) 2018-2020 Pali Rohár
- Copyright (C) 2021 Hunter Wardlaw

完整条款见原 `COPYING` 和 [LGPL-2.1.txt](LICENSES/LGPL-2.1.txt)。源文件声明允许 LGPL 2.1 或后续版本；提供 2.1 文本不将授权缩减为 2.1-only。保留版权、许可与实际修改记录。

当前固件静态链接该库。分发固件时应按所采用 LGPL 版本满足对应源码、修改、许可告知、重新链接及必要调试逆向工程等义务。不能仅提供 `.bin` 或一个上游链接就推定合规；应准备并验证能修改库后重新链接的源码 / 目标文件、构建脚本及工具链材料。此清单本身不是已完成的固件合规材料包。

## ESP-IDF 和 managed components

当前版本见 `software/dependencies.lock`，实际下载版本的文件头、LICENSE 和 NOTICE 优先。ESP-IDF 内部含多种第三方许可；下表不是对所有传递依赖的统一授权。

| 组件 | 当前版本 | 已检查许可 / 要求 |
| --- | --- | --- |
| ESP-IDF | 6.1.0 | 根 LICENSE 为 Apache-2.0；内部第三方源代码、控制器及无线库继续按各自许可和 NOTICE |
| espressif/mdns | 1.8.0 | Apache-2.0；保留适用声明 |
| espressif/esp_tinyusb | 2.4.0 | Apache-2.0；保留适用声明 |
| espressif/tinyusb | 0.21.0~2 | MIT；Copyright (c) 2012-2026, hathach (tinyusb.org)；全文见 [TinyUSB-MIT.txt](LICENSES/TinyUSB-MIT.txt) |
| espressif/cjson | 1.7.19 | MIT；Copyright (c) 2009-2017 Dave Gamble and cJSON contributors；全文见 [cJSON-MIT.txt](LICENSES/cJSON-MIT.txt) |
| espressif/esp_audio_codec | 2.6.2 | LicenseRef-Espressif-Modified-MIT；Copyright (c) 2025 Espressif Systems (Shanghai) CO., LTD；**仅限结合乐鑫产品使用，禁止为非乐鑫产品用途再分发**；全文见 [许可原文](LICENSES/LicenseRef-Espressif-Modified-MIT.txt) |

`esp_audio_codec` 的许可证不是标准 MIT，也不是通用、无硬件限制的开源许可。包内其他第三方 codec 还应按实际声明核查；其 README 指向 [Espressif Copyrights and Licenses](https://docs.espressif.com/projects/esp-adf/en/latest/COPYRIGHT.html)。本项目不重新授权这些依赖，不以自有 Apache-2.0 许可解除其限制。

## USB 描述符

`software/main/include/chime_usb_descriptors.h` 含 HiFiPhile 的 MIT 许可声明：Copyright (c) 2023 HiFiPhile。原文件头保持不变，另提供 [USB-descriptors-MIT.txt](LICENSES/USB-descriptors-MIT.txt)。

## DSP、测试数据、PCB 和机械模型

- SigmaStudio 工程的原创配置与设计贡献按项目范围许可；Analog Devices 工具、算法模块 / 库和生成物中第三方内容仍需按其条款确认分发权利。不打包安装器或驱动 DLL，不把整个生成程序宣称为项目独立原创。
- Codec 测试流与生成器按各自 `PROVENANCE.json` 保留出处；编码器许可与测试流的权利分别判断。原始事实、测量和协议夹具不构成对厂商手册或驱动的授权。不打包私人录音或厂商手册副本。
- PCB / 机械文件的项目许可只覆盖有权授权的原创部分，不覆盖嵌入的第三方封装、3D 模型、图片、品牌或其他素材。发布前需核对实际文件内容与权利。

许可文本、来源说明、专利与商标许可、安全合规是不同事项。未经许可不得宣称获得 Sony、Savitech、Espressif、Analog Devices 或其他厂商的官方认证或背书。
