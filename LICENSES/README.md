# 许可证适用范围

Copyright 2026 Chime contributors。以下许可只授予项目贡献者有权授予的权利；第三方代码、库、模型、图片、工具内容及其衍生部分不因目录位置而改用本项目许可。

| 范围 | 许可证 | 边界 |
| --- | --- | --- |
| `software/`、`scripts/`、`tools/` 中的自有软件、测试生成程序及自有测试数据；`dsp/` 中原创工程配置与设计贡献 | [Apache-2.0](../LICENSE) | 排除引入的第三方代码、第三方测试流、厂商算法 / 库及其生成内容；不得以此覆盖第三方权利 |
| `hardware/audio-dsp.epro2`、`schematic.pdf`、`pcb.pdf`、`bom.xlsx` 中的原创 PCB / 原理图设计贡献 | [CERN-OHL-P-2.0](CERN-OHL-P-2.0.txt) | 仅覆盖有权授权的设计贡献；第三方封装、模型、图片等维持原条款。相关 Notices 包括根 NOTICE、hardware/README.md 和 THIRD_PARTY_NOTICES.md |
| `mechanical/` 下 `satellite/front`、`satellite/back`、`back-panel/back-panel`、`feet/feet` 对应的 `.stp` / `.3mf` 文件中的自有模型 | [CC-BY-4.0](CC-BY-4.0.txt) | 只授权列出的交换文件；不包含板卡装配参考、Creo 参数化源文件及历史 |
| 项目自撰的根 README、公开 `docs/` 文档、各目录 README 和许可说明文字 | [CC-BY-4.0](CC-BY-4.0.txt) | 不覆盖第三方引文、上游 README、标准许可证全文或私人研发资料 |
| `assets/branding/` 中的 Logo / 标识素材、项目名称及商标 | 不纳入上述许可 | 除法律允许或另行授权外不提供素材使用授权；不授予商标、官方背书或认证权利 |
| 其他第三方材料 | 各自原许可或待确认 | 见 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)；授权未确认不等于可任意使用 |

未列出的私人资料、Creo 原生文件、原仓库历史以及内部文档，不因本许可方案而对外授权。README 不覆盖源文件原有的许可声明；发生范围疑问时请联系维护者，不应直接将根 LICENSE 套用到所有文件。

## 署名与修改

- CC BY 4.0 内容可署名为 **Chime contributors / Chime DSP Audio**，附许可证链接，保留已有来源说明并标明修改。传播文件时应保留能识别项目来源的信息，不得暗示维护者认可修改版；不使用尚未确定的 GitHub 仓库地址作为唯一署名来源。
- PCB 使用 CERN-OHL-P-2.0（Permissive，非 W / S 互惠版本）。保留适用 Notices；修改设计时按许可注明修改日期和简述；交付产品时确保接收者能够访问适用 Notices。此许可不是可直接生产、安全或完整设计交付的保证。
- Apache-2.0 内容按根 LICENSE 保留适用版权、许可和 NOTICE，并标注修改。第三方内容不重新许可；修改第三方代码也不能因此获得超出原授权的权利。
- 新贡献应在上述对应范围的许可下提交，并保留来源与第三方权利说明；不要提交无权授权的材料。

## 本目录的许可文本

标准许可证全文保留其原有条款，不受本项目的 CC 文档许可改写。CC-BY-4.0 与 CERN-OHL-P-2.0 文本来自 SPDX license-list-data 的对应标准文本；Apache-2.0 来自当前 ESP-IDF 的标准 LICENSE。第三方 MIT、LGPL 和乐鑫修改版 MIT 文本保留实际依赖版本中的声明。

这些文本和免责声明不证明第三方授权已完备，也不提供 Sony / Savitech 等第三方专利、商标或认证许可。发行前检查见 [docs/licenses.md](../docs/licenses.md)。
