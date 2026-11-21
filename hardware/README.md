# PCB 资料

当前提供 `audio-dsp.epro2`：PCB 设计工程的导出容器，可使用兼容 `.epro2` 格式的嘉立创 EDA 专业版 / EasyEDA Pro 工具尝试导入；具体兼容版本需实际确认。

这是项目级导出文件，**不是 Gerber / NC Drill 制造包，也不是已确认可直接下单的生产资料**。当前公开资料没有另外提供经核对的 BOM、贴片坐标、板厂叠层或装配说明。

核心器件：ESP32-S31、ADAU1451、TAS5825M × 2。固件引脚和地址以 `software/components/board_support/include/board_support.h` 为入口，必须与实际 PCB 逐项核对。

制造 / 装配前自行完成 ERC / DRC、电源与额定值、阻抗与时钟、USB、散热、BTL 负载及故障控制检查。工程导出中可能含图片、库与模型；正式发布前核对来源、授权和个人元数据。

## 许可与 Notices

Copyright 2026 Chime contributors。`audio-dsp.epro2` 中有权授权的原创 PCB / 原理图设计贡献采用 **CERN-OHL-P-2.0**（SPDX-License-Identifier: CERN-OHL-P-2.0），不是 W / S 互惠版本。许可全文见 [CERN-OHL-P-2.0](../LICENSES/CERN-OHL-P-2.0.txt)。

本 README、根 [NOTICE](../NOTICE) 和 [第三方声明](../THIRD_PARTY_NOTICES.md) 是相关 Notices；复制与修改时保留适用声明，修改时记录日期及简述，交付产品时确保接收者可访问相关 Notices。第三方封装、模型和图片不被重新许可；不授予第三方专利、商标或认证权利。许可授予不等于该导出文件已通过可编辑性、完整性或可制造性验证。本 README 的自撰说明文字采用 CC-BY-4.0。
