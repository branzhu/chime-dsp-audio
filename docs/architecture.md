English | [简体中文](architecture.zh-CN.md)

# Architecture and code map

## Control and data planes

`software/main/app_main.c` initializes board resources and enters `bench_bridge_run()`. Despite its historical bench name, this is the current firmware's main control flow.

- **Control owner**: `bench_bridge.c` coordinates the DSP, amplifiers, web / TCP requests, audio sources, and stop / fault state.
- **Shared data plane**: `chime_usb.c` contains the USB / Bluetooth shared PCM worker and the sole I2S TX path. Source selection does not create a separate Bluetooth output task.
- **Bluetooth**: `chime_bluetooth.c` manages connections and A2DP / AVRCP; `chime_bt_decoder.c` manages the decoder worker; `chime_bt_pcm.c` handles PCM buffering and watermarks; `chime_bt_delay.c` estimates sink delay.
- **DSP / amplifiers**: `chime_dsp_boot.c`, `bench_dsp.c`, `bench_audio.c`, and `bench_sound.c` perform coordinated access through `board_support` and `dsp_control`.
- **Local interaction**: `chime_input.c` / `chime_controls.c` handle the encoder and button; `chime_led.c` / `chime_indicator.c` handle the LED.
- **Networking**: `chime_network.c`, the provisioning guard, and `bench_web.c` / `bench_status.c` provide provisioning, status, and manual control.

Source changes use epochs / generations to invalidate old PCM and queued operations. Ordinary volume changes must not clear local mute, standby, web STOP, external DSP ownership, or hard faults. These are design intentions, not proof that every concurrency and failure combination has passed board-level validation.

## Codec boundaries

| Path / dependency | Responsibility |
| --- | --- |
| Espressif `esp_audio_codec` | Current external SBC, AAC-LC, and Opus APIs |
| `software/components/chime_freeaptx/` | aptX / aptX HD backend; separate LGPL obligations apply |
| `software/components/chime_ldac/` | Current LDAC thin wrapper |
| `software/components/chime_lhdc/` | Current LHDC V5 thin wrapper |
| `software/components/chime_codec_reference/` | The single reference LDAC / LHDC backend, pinned upstream sources, and generators |

The original vendor sources, generated port sources in the build directory, and thin wrappers are layers of the same backend, not alternative legacy decoders. The old `chime_ldac/src` / `chime_lhdc/src` trees are no longer retained. Upstream provenance and unresolved permissions are described in [Third-party notices](../THIRD_PARTY_NOTICES.md) and the [licensing guide](licenses.md).

## DSP data flow

`dsp/esp32-bindings.json` binds input and output gains by module name. `scripts/freeze-dsp-image.py` converts compiler outputs into the frozen image, parameter headers, and provenance manifest under `software/main/generated/`. The build verifies these again. Output-mode changes preserve the compiled gain of enabled branches rather than forcing unity gain.

The current board supports L / R / Sub routing. Output-mode names do not establish that filtering, limiting, subsonic protection, or thermal protection meet product requirements.

## Board mapping

The authoritative code entry point for GPIOs and I²C addresses is `software/components/board_support/include/board_support.h`. Check it against your actual PCB before use.

| Function | Current code configuration |
| --- | --- |
| I²C SDA / SCL | IO0 / IO1 |
| ADAU1451 / LR amplifier / Sub amplifier addresses | `0x38` / `0x4c` / `0x4d` |
| TDM DOUT / BCLK / FS | IO9 / IO10 / IO11 |
| Encoder A / B / button | IO42 / IO40 / IO43; button active low |
| LR / Sub PDN | IO15 / IO13 |

This is not generic ESP32 development-board firmware. A hardware change requires reviewing startup states, clocks, USB, faults, and power control, not just changing GPIO numbers.
