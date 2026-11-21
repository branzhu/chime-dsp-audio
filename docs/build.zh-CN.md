[English](build.md) | 简体中文

# 构建与测试

## 工具链

- ESP-IDF **6.1**，目标 **ESP32-S31 / `esp32s31`**。预览目标使用 `--preview`。
- Python 与 CMake / Ninja 使用 ESP-IDF 环境提供的版本。
- 主机测试使用 Node.js（当前验证环境为 26.12.1）、Python 3 和 C 编译器；Linux 可使用 GCC，Windows 使用 Visual Studio 2022 的 MSVC x64 环境并设置 `CC=cl`。
- Wireshark 集成测试另需带 Lua 支持的 TShark；用 `TSHARK` 指定可执行文件。
- 编辑 DSP 工程还需要匹配工程版本的 SigmaStudio；仅构建现有冻结快照不需要启动 SigmaStudio。

## 固件构建

先加载 ESP-IDF 环境，在仓库根目录执行：

```sh
idf.py --preview -C software build
```

`software/dependencies.lock` 固定组件版本；首次构建需要联网下载。Bluetooth / TinyUSB 的适配由项目脚本校验上游文件哈希后生成到构建目录，不应手改全局 SDK 或 managed components。SDK 哈希不匹配时，应审查并移植，而不是删掉校验。

当前跟踪的 `software/sdkconfig` 是板级构建配置，`sdkconfig.defaults` 是默认项。构建输出和下载组件不纳入公开仓库。

默认启用实验性 vendor codec；可以在 `menuconfig` 的 **Chime Bluetooth** 中关闭 vendor codec 回到 SBC / AAC，或再关闭 external codec 回到 SDK SBC。关闭选项只改变链接结果，**不会从仓库中移除第三方源码，也不会消除源码再分发问题**。

## 主机回归

在仓库根目录执行，无需连接板卡：

```sh
node scripts/test-input.mjs
node scripts/test-bench-audio.mjs
node scripts/test-bench-web.mjs
node scripts/test-chime-page.mjs
node scripts/test-frozen-dsp.mjs
node scripts/test-sigma-bridge.mjs
node scripts/test-bluetooth.mjs
python scripts/test-reference-codec-port.py
```

- Windows：在 x64 Native Tools 环境运行，设置 `CC=cl`，并将 `PYTHON` 指向真实 Python 可执行文件；避免依赖 Windows 应用别名。
- `BT_LDAC_ASAN=1` / `BT_LHDC_ASAN=1` 启用对应主机 ASan 路径。
- `IDF_PATH` 和 `PYTHON` 配好后，蓝牙测试还会验证实际 SDK delay / AVDTP hook；缺少时会明确跳过，不能将跳过算作通过。
- `node scripts/test-sigma-wireshark.mjs` 需要 TShark。
- `node --test scripts/check-bench-bridge.test.mjs scripts/sigmastudio-tcp-probe.test.mjs` 验证辅助工具。

小型协议、寄存器事实及历史标定夹具存于 `software/tests/fixtures/`；回归不依赖私有开发文档或厂商 PDF 副本。历史标定值不代表当前 DSP 增益配置的安全输出能力。

## 修改 DSP

工程入口是 `dsp/ADAU1451.ssprj`，绑定入口是 `dsp/esp32-bindings.json`。

```sh
python scripts/freeze-dsp-image.py --update
python scripts/freeze-dsp-image.py
node scripts/test-frozen-dsp.mjs
```

先用匹配版本的 SigmaStudio 编译工程，再更新冻结数据。生成器会核对工程、参数地址、编译数据及增益；失败时先修复不一致，不要直接改生成头文件或降低校验要求。Git 必须保留 `.gitattributes` 中声明的原始字节，否则来源哈希会变化。

## 烧录前

本项目使用 16 MiB Flash 配置和 **3 MiB factory 应用分区**。不要只刷 app 覆盖旧版更小分区；先检查完整构建产物和分区表。USB 音乐接口与调试接口不能想当然混用。

阅读 `scripts/flash-firmware.ps1` 的警告，并根据自己的接线和调试器选择烧录方式。不要自动擦除 NVS、写 EEPROM 或烧安全 eFuse。断开负载检查供电、接线和功放状态后，再以很低的主机 / 手机音量开始验证。
