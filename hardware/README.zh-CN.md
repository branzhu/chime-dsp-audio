[English](README.md) | 简体中文

# PCB 设计

[`audio-dsp.epro2`](audio-dsp.epro2) 是 PCB 工程导出文件。可尝试使用兼容格式的 **嘉立创 EDA 专业版 / EasyEDA Pro** 导入；具体版本的兼容性尚未验证。

核心器件为 **ESP32-S31、ADAU1451 和两颗 TAS5825M**。使用前应将 `software/components/board_support/include/board_support.h` 中的固件引脚与 I²C 地址逐项对照实际 PCB。

## 制造与装配前

这是工程导出，**不是 Gerber / NC Drill 制造包，也不是经过量产验证的设计**。没有另行提供经核对的 BOM、贴片坐标、板厂叠层和装配说明。

检查 ERC / DRC、电源与额定值、阻抗与时钟、USB、散热、BTL 负载和故障处理。嵌入的库、模型与图片应按各自条款核对。上电前请阅读[安全说明](../docs/safety.zh-CN.md)。

## 许可与声明

Copyright 2026 Chime contributors。`audio-dsp.epro2` 中的原创 PCB / 原理图设计贡献采用 **CERN-OHL-P-2.0**（SPDX-License-Identifier: CERN-OHL-P-2.0），见[许可全文](../LICENSES/CERN-OHL-P-2.0.txt)。

本 README、根 [NOTICE](../NOTICE) 和[第三方声明](../THIRD_PARTY_NOTICES.md) 包含适用 Notices。保留适用声明，修改时记录日期及简述，交付产品时确保接收者可访问相关声明。第三方封装、模型和图片不被重新许可，不授予第三方专利、商标或认证权利。许可不保证可编辑性、完整性或可制造性。本 README 的自撰说明文字采用 CC-BY-4.0。
