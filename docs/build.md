English | [简体中文](build.zh-CN.md)

# Build and host tests

## Toolchain

- **ESP-IDF 6.1**, targeting **ESP32-S31 / `esp32s31`**. Use `--preview` for this preview target.
- Use the Python, CMake, and Ninja versions provided by the ESP-IDF environment.
- Host tests require Node.js (the currently validated environment uses 26.12.1), Python 3, and a C compiler. Linux can use GCC; on Windows, use the Visual Studio 2022 MSVC x64 environment and set `CC=cl`.
- Wireshark integration tests also require TShark with Lua support. Set `TSHARK` to its executable if needed.
- Editing the DSP project requires a matching SigmaStudio version. Building the existing frozen snapshot does not require launching SigmaStudio.

## Firmware build

Activate the ESP-IDF environment, then run from the repository root:

```sh
idf.py --preview -C software build
```

`software/dependencies.lock` pins component versions; the first build needs network access to download them. Project scripts check upstream Bluetooth / TinyUSB file hashes and generate the required adaptations into the build directory. Do not edit the global SDK or managed components by hand. If SDK hashes differ, review and port the changes instead of bypassing verification.

The tracked `software/sdkconfig` is the board build configuration; `sdkconfig.defaults` supplies defaults. Build outputs and downloaded components are not part of the public source repository.

Experimental vendor codecs are enabled by default. In **Chime Bluetooth** under `menuconfig`, disabling vendor codecs returns to SBC / AAC; disabling external codecs as well returns to SDK SBC. These options change what is linked: **they do not remove third-party sources from the repository or resolve source redistribution permissions**. See [Licensing](licenses.md).

## Host regressions

Run from the repository root; no board connection is required:

```sh
node scripts/test-input.mjs
python scripts/generate-bt-src.py --check
node scripts/test-mixing.mjs
node scripts/test-bench-audio.mjs
node scripts/test-bench-web.mjs
node scripts/test-chime-page.mjs
node scripts/test-frozen-dsp.mjs
node scripts/test-sigma-bridge.mjs
node scripts/test-bluetooth.mjs
python scripts/test-reference-codec-port.py
```

- On Windows, run in an x64 Native Tools environment, set `CC=cl`, and point `PYTHON` to a real Python executable rather than a Windows app alias.
- `BT_LDAC_ASAN=1` / `BT_LHDC_ASAN=1` enable the corresponding host AddressSanitizer paths.
- With `IDF_PATH` and `PYTHON` configured, Bluetooth tests also check the actual SDK delay / AVDTP hooks. Missing prerequisites produce explicit skips; a skipped check is not a pass.
- `node scripts/test-sigma-wireshark.mjs` requires TShark.
- `node --test scripts/check-bench-bridge.test.mjs scripts/sigmastudio-tcp-probe.test.mjs` checks the helper tools.

Small protocol fixtures, register facts, and historical calibration fixtures live in `software/tests/fixtures/`. Regressions do not require private development documents or vendor PDF copies. Historical calibration values do not establish safe output levels for the current DSP gain configuration.

## Changing the DSP project

The project entry point is `dsp/ADAU1451.ssprj`; bindings are defined in `dsp/esp32-bindings.json`.

```sh
python scripts/freeze-dsp-image.py --update
python scripts/freeze-dsp-image.py
node scripts/test-frozen-dsp.mjs
```

Compile the project in the matching SigmaStudio version before updating frozen data. The generator checks the project, parameter addresses, compiled data, and gains. Fix inconsistencies rather than editing generated headers or weakening checks. Git must preserve the bytes specified in `.gitattributes`, or source hashes will change.

## Before flashing

The board configuration uses **16 MiB flash** and a **3 MiB factory application partition**. Do not flash only an application over an older, smaller partition layout. Check the complete build outputs and partition table first. Do not assume the USB audio and debugging interfaces are interchangeable.

Read the warnings in `scripts/flash-firmware.ps1` and choose a flashing method appropriate to your wiring and debugger. Do not automatically erase NVS, write EEPROM, or burn security eFuses. With loads disconnected, check power, wiring, and amplifier state; begin playback validation at a very low host / phone volume. See [Safety](safety.md).
