English | [简体中文](README.zh-CN.md)

# Chime · DSP Audio

An experimental desktop DSP speaker platform based on **ESP32-S31 + ADAU1451 + TAS5825M**, including firmware, a SigmaStudio project, a PCB project export, and enclosure exchange models.

USB and Bluetooth provide audio input, the ADAU1451 handles signal processing, and the ESP32 manages sources, controls, and system state. The design uses a single shared audio output path and coordinated DSP / amplifier control rather than separate playback pipelines for each feature.

> **This is a development / bench project, not a production-ready or safety- or acoustically-certified product.** Some features have only host regression and target-build evidence. Project-owned materials use separate licenses, but permissions for the imported LDAC implementation and some reference adapter code remain unresolved. Availability in this repository does not mean all materials are cleared for open-source or commercial redistribution. See [Licenses and third-party materials](docs/licenses.md).

## Features

- **USB Audio**: 48 kHz, stereo PCM16 input, routed to the DSP through a shared PCM renderer.
- **Bluetooth A2DP**: the code supports SBC, AAC-LC, aptX, aptX HD, Android Opus, LDAC, and LHDC V5. The last five are experimental extensions. LDAC / LHDC currently expose only 44.1 / 48 kHz stereo PCM16 output; LHDC V2 / V3 are not supported.
- **Source selection**: an A2DP connection selects Bluetooth, pausing retains that source, and disconnecting returns to USB. Sources are not mixed.
- **DSP project synchronization**: parameters are bound by module names, with consistency checks across the SigmaStudio project, compiler outputs, and the frozen DSP image embedded in the firmware.
- **Local controls**: the rotary encoder adjusts the selected source volume; a short press toggles local mute, and a long press enters or exits software standby. An RGB LED indicates state.
- **Debugging and control**: a web interface for Wi-Fi provisioning, status, and manual control, plus a SigmaStudio TCP bridge for debugging on a trusted LAN.
- **Host tests**: coverage for protocols, source switching, PCM, codecs, inputs, LEDs, the web interface, and frozen DSP data.

Codec support describes capabilities present in the current source and configuration. It **does not imply validation on every phone, bitrate, RF environment, or long-running playback scenario, nor official codec certification**. See [Architecture](docs/architecture.md) and [Licensing](docs/licenses.md) for backend provenance and limitations.

## System overview

```text
USB Audio ───────┐
                ├─ ESP32-S31: source selection / decoding / shared PCM renderer
Bluetooth A2DP ─┘                    │ I2S / TDM
                                     ▼
                               ADAU1451 DSP
                                     │
                               TAS5825M × 2
                                     │
                              L / R / Sub outputs

Encoder / LED / Web ── control and status ── single DSP / amplifier owner
```

The subwoofer path uses two independent BTL output channels. **Never wire amplifier outputs directly in parallel.** Software protection is not a substitute for hardware safety. Read [Safety and known limitations](docs/safety.md) before playback.

## Repository layout

| Path | Contents |
| --- | --- |
| [`software/`](software/) | ESP-IDF firmware, components, and host tests |
| [`dsp/`](dsp/) | SigmaStudio project, compiler outputs, and ESP32 parameter bindings |
| [`hardware/`](hardware/) | PCB project export: currently `audio-dsp.epro2`, not a Gerber manufacturing package |
| [`mechanical/`](mechanical/) | Published STEP / 3MF exchange models; native Creo source files are not included |
| [`scripts/`](scripts/) | Build helpers, DSP synchronization, tests, and public snapshot export tools |
| [`tools/wireshark/`](tools/wireshark/) | SigmaDSP protocol analysis tools |
| [`docs/`](docs/README.md) | Concise user-facing documentation, currently in Simplified Chinese |
| [`assets/branding/`](assets/branding/) | Project branding assets; excluded from the software / documentation license grants |

The private development repository may contain additional internal records. Public snapshots include only allowlisted materials and do not carry the private repository's Git history.

## Build and test

Use the currently validated **ESP-IDF 6.1 / `esp32s31`** toolchain. Activate the ESP-IDF environment, then run from the repository root:

```sh
idf.py --preview -C software build
```

The first build downloads the pinned Espressif components. See [Build and host tests](docs/build.md) for full commands, host compiler requirements, and test categories.

Building is not flashing. **Do not flash only a new application over an older partition table, erase NVS casually to recover connectivity, or ignore volume-change warnings.** This project does not provide an unvalidated one-click power-on-and-play procedure.

## Hardware and enclosure files

- [PCB notes](hardware/README.md): file format, current deliverables, and checks required before manufacturing.
- [Enclosure notes](mechanical/README.md): STEP / 3MF usage and the boundary around unpublished native Creo files.
- These files describe the current development revision; they do not constitute manufacturing, assembly, electrical, or acoustic acceptance.

## Documentation

The linked guides below are currently in Simplified Chinese. The [Chinese README](README.zh-CN.md) provides the corresponding project overview.

- [Build and host tests](docs/build.md)
- [Architecture and code map](docs/architecture.md)
- [Safety, validation boundaries, and known issues](docs/safety.md)
- [Licenses and third-party materials](docs/licenses.md)
- [Public release workflow](docs/publishing.md)

## Upstream projects and acknowledgements

Chime builds on work shared by **WillyBilly06** and other upstream contributors:

- **[ESP32-A2DP-SINK-WITH-CODECS-UPDATED](https://github.com/WillyBilly06/ESP32-A2DP-SINK-WITH-CODECS-UPDATED)**: the source of the selected LDAC decoder implementation, LDAC / LHDC V5 adapter code, and codec lifecycle helpers integrated into Chime. These imports are pinned to [commit `45281c5`](https://github.com/WillyBilly06/ESP32-A2DP-SINK-WITH-CODECS-UPDATED/tree/45281c5212cf5e22eb1d804e75439d2ac051f7c0); the [import manifest](software/components/chime_codec_reference/UPSTREAM.json) records the individual files and hashes.
- **[LHDC-V5-Decoder](https://github.com/WillyBilly06/LHDC-V5-Decoder)**: the upstream portable LHDC V5 decoder used by the imported snapshot. Its comparison revision is [commit `cf857ce`](https://github.com/WillyBilly06/LHDC-V5-Decoder/tree/cf857ce1a1429965e161e54adbc3f293fd9ce273). The [snapshot record](software/components/chime_codec_reference/vendor/native_bt/codecs/lhdcv5/source_snapshot/SOURCE.json) distinguishes matching files from the two working-change files.
- **freeaptx**: the aptX / aptX HD implementation is imported through the same ESP32 reference repository at the older [commit `0d85a97`](https://github.com/WillyBilly06/ESP32-A2DP-SINK-WITH-CODECS-UPDATED/tree/0d85a975ec079d954f1bdd3e94d2fed70848c252). See its [provenance manifest](software/components/chime_freeaptx/UPSTREAM.json) and original LGPL-2.1-or-later notices for the original authors and terms.

Only selected codec-related parts of the reference project are integrated; its custom Bluetooth host, codec worker, and renderer are not linked as replacements for Chime's existing paths. Thank you to the upstream authors and contributors for sharing their work.

These acknowledgements identify provenance, not a blanket license grant. Component-specific terms and unresolved permissions are documented in [Third-party notices](THIRD_PARTY_NOTICES.md).

## Licensing

- **Project-owned software and original DSP project configuration contributions**: [Apache-2.0](LICENSE).
- **Original PCB design contributions**: [CERN-OHL-P-2.0](LICENSES/CERN-OHL-P-2.0.txt).
- **Project-authored public documentation and the four STEP / 3MF models**: [CC-BY-4.0](LICENSES/CC-BY-4.0.txt). Unpublished native Creo files are not included.
- **Third-party materials**: retain their original terms or unresolved-permission status; see [Third-party notices](THIRD_PARTY_NOTICES.md). Logos, names, and trademarks are not included in the above grants.

This project is a downstream user of the LDAC implementation and reference adapter code and has not established their complete licensing chain. Users must assess upstream information and the rights required for their own use. This statement does not replace authorization or supply missing permission. See [License scope](LICENSES/README.md) for the full boundaries.

## Feedback and contributions

When reporting an issue, include the hardware revision, firmware commit, ESP-IDF version, source / phone and negotiated codec, reproduction steps, and sanitized logs. Do not include Wi-Fi credentials, private recordings, or a complete personal development environment dump.

Contributions must follow the license scope of the relevant materials, retain provenance, and be yours to contribute. Third-party code, hardware files, mechanical models, and trademarks do not acquire a single common license merely by appearing in the same repository.
