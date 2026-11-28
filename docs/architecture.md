English | [简体中文](architecture.zh-CN.md)

# Architecture and code map

## Control and data planes

`software/main/app_main.c` initializes board resources and enters `bench_bridge_run()`. Despite its historical bench name, this is the current firmware's main control flow.

- **Control owner**: `bench_bridge.c` coordinates the DSP, amplifiers, web / TCP requests, audio sources, and stop / fault state.
- **Shared data plane**: `chime_render.c` owns the only continuous I2S writer. `chime_usb.c` handles UAC2/HID transport; `chime_mixer.c` combines independently controlled USB and Bluetooth lanes. A source restart or underrun does not stop the other lane.
- **Bluetooth**: `chime_bluetooth.c` manages connections and A2DP / AVRCP; `chime_bt_decoder.c` manages the decoder worker; `chime_bt_pcm.c` handles PCM buffering and watermarks; `chime_bt_delay.c` estimates sink delay.
- **DSP / amplifiers**: `chime_dsp_boot.c`, `bench_dsp.c`, `bench_audio.c`, and `bench_sound.c` perform coordinated access through `board_support` and `dsp_control`.
- **Local interaction**: `chime_input.c` / `chime_controls.c` handle the encoder and button; `chime_led.c` / `chime_indicator.c` handle the LED.
- **Networking**: `chime_network.c`, the provisioning guard, and `bench_web.c` / `bench_status.c` provide provisioning, status, and manual control.

Each input uses its own epoch; generations gate idle admission without making a Bluetooth connection preempt USB. Ordinary volume changes must not clear local mute, standby, web STOP, external DSP ownership, or hard faults. These are design intentions, not proof that every concurrency and failure combination has passed board-level validation.

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

## Simultaneous audio and controls

- USB remains 48 kHz stereo PCM16 with asynchronous FIFO feedback. A muted USB stream is still consumed. Bluetooth retains independent adaptive buffering and clock-drift correction.
- Internal samples are Q5.27, gains Q2.30, with 64-bit products/sums. Quantization to meaningful PCM24 happens once at the final 32-bit I2S slot boundary. Summing is unity: no fixed attenuation, automatic normalization, ducking or hidden single-source boost. Two loud inputs can clip; final saturation prevents wraparound and `MIX FLOW` reports clip counts and peak Q27 level. This is not a speaker-power limiter.
- The renderer applies a 480-frame gain ramp for level/mute changes and missing-input fade tails. Short press is local all-input mute, not a HID Mute key. Both inputs continue advancing while locally muted. Long press remains standby. STOP, protection faults and external DSP ownership remain separate.
- Rotation sends bounded HID Consumer Volume Increment/Decrement press-release reports. It never writes AVRCP or locally duplicates a UAC gain adjustment. HID volume keys generally affect the PC default output, not a guaranteed device-specific endpoint; select Chime as default. Per-detent loudness is OS-defined. Pending keys are discarded on USB suspend/disconnect; reconnect sends a neutral report before any new keys.
- Normal playback is a mix; explicitly armed external SigmaStudio audition remains USB-only. Manual sine output still runs through the existing protected control sequence.

## Bluetooth SRC

`chime_bt_pcm.c` prepares a bounded immutable input window under the Bluetooth lock; `chime_bt_src.c` performs FIR work **outside** that critical section. The SRC has 192 taps, 256 fractional phases with coefficient interpolation, Q2.30 coefficients and a 32-bit fractional phase. It supports 16/32/44.1/48 kHz input and 48 kHz output. Three rate-specific banks are generated reproducibly with `scripts/generate-bt-src.py`; no runtime coefficient allocation or trigonometry is needed. Queue feedback is limited to +/-2000 ppm and changes by at most 1 ppm per 128-frame block.

The 48 kHz kernel stages the current coefficient row in a task-local 1536-byte RAM cache and runs from IRAM. Fractional drift still uses the full FIR: a signed-high multiply replaces per-tap interpolation remainder work only when an exact divisibility test proves equivalence to the original truncation. Exceptional blends retain the original signed division. An exactly unity step reuses the interpolated coefficients within the block; it does not bypass the filter. The SRC alone builds at `-O3`. These changes preserve all coefficients, 64-bit accumulation, Q27 output rounding and the queue/drift policy; they are not a reduction in filter quality. Host reference tests cover every phase row, interpolation divisibility boundaries, full blocks and +/-2000 ppm; hardware timing must still be measured.

The FIR requires 192 input frames of context; queue preparation accounts for this before consuming samples. At reset/rebuffer no old filter history is reused. Existing adaptive buffer targets are retained rather than reducing latency at the same time. Bluetooth delay reporting remains an uncalibrated estimate, not a USB/Bluetooth synchronization guarantee.

`node scripts/test-mixing.mjs` executes production mixer/renderer/SRC code with host stubs. Numerical probes cover 16k/3kHz, 32k/12kHz and 44.1k/20kHz tones and selected images, stereo symmetry, phase endpoints and DC precision. Clock simulations cover ten minutes for each supported rate at +/-300 ppm **without** simulating radio or CPU scheduling. These tests are not exhaustive spectral certification or board-level timing/24-hour acceptance. Before normal use, check the maximum `MIX FLOW compute_us` against the 2667us block period, and stress USB + codec decode + Wi-Fi on hardware.

### Core affinity and the ESP32-S31 PIE experiment

Production playback uses the bit-exact scalar FIR on **CPU0**, with the codec
worker pinned to **CPU1** in dual-core builds. The renderer retains priority 6,
the decoder priority 5, and the renderer stack is 8 KiB. Its startup log prints
the actual running core and `src=scalar pie_probe=disabled`. No PIE probe is
executed on the PCM task.

This separation is essential on S31: PIE is available **only on CPU1**. The
IDF RISC-V port migrates a task that executes PIE on CPU0 to CPU1, overriding
its previous affinity; lazy coprocessor ownership also pins the task there.
Choosing scalar after a PIE benchmark does **not** undo that migration. A
probe on the renderer therefore caused it to share CPU1 with the lower-priority
decoder, even when PIE was rejected. The observed failure changed from rising
PCM occupancy to raw-packet backlog, a near-empty PCM queue and starvation gaps.
A board startup comparison also measured scalar 2074us versus PIE 2215us per
128 frames, so that particular SIMD implementation did not provide a speedup.
These wall-time measurements are not general CPU-time benchmarks.

The original experimental PIE candidate remains available only with an explicit
`CHIME_SRC_PIE_BENCHMARK=1` compiler definition, or in the host mathematical
model. It is compiled out of the normal playback path. Its standalone probe
requires a dedicated CPU1 task and refuses to execute on CPU0. Normal rendering
must not be moved to CPU1 simply to use this candidate without first redesigning
and verifying the codec/render scheduling topology.

The candidate exactly decomposes each interpolated Q2.30 coefficient as
`c = hi * 65536 + lo`, with signed 16-bit halves. Four 192-tap vector dot
products are reconstructed in 64 bits before the unchanged Q27 rounding; it
does not quantize coefficients to Q15. The generated `abs(c) <= 2^30` bound
makes both halves representable, and `192 * 32768^2` fits a signed 40-bit XACC.
All vector buffers are 16-byte aligned local scratch; CFG/SAR and hardware-loop
state are untouched. The standalone probe checks raw extreme-value dot products
and 6415 SRC jobs, and compares seven-run median timings. Host tests verify the
coefficient split and benchmark-selection policy, but are **not** PIE instruction
emulation or board-level concurrency acceptance.

### Render timing and PCM overflow diagnostics

`MIX FLOW` prints `compute_us`, `src_us`, `write_us` and `period_us` as
`last/max` microseconds, plus `deadline_misses`. `src_us` includes queue
preparation and FIR; `write_us` includes normal I2S DMA pacing. All times are
wall times and may include task preemption. A transient maximum alone does
not prove sustained overload: correlate deadline growth, block progress,
queue occupancy and decoder timing. The renderer does not add a sleep to every
compute deadline miss; I2S writes provide pacing. If writes remain shorter
than 100us for 20ms, a one-tick fairness yield (`unpaced_yields`) prevents
sustained unpaced overload from monopolizing a CPU and starving the control
owner. Write wall time is only a blocking heuristic. STOP is requested before
the owner waits for the render mutex.

A `pcm-overflow` fault means the fixed 8192-frame input PCM queue could not
accept a chunk. In this path `error=257` (`ESP_ERR_NO_MEM`) labels a full
queue, not evidence of exhausted heap. The fault preserves pushed, consumed,
rendered and discarded frame counters before invalidating the stream. Capture
these counters, the surrounding `MIX FLOW` and Bluetooth decoder/queue logs
when testing LDAC alone and concurrently with USB and Wi-Fi. This diagnostic
does not automatically drop audio or expand the queue. The RV32 interpolation
optimization is checked bit-for-bit against the original 64-bit arithmetic at
all phase rows and supported rates; filter taps, coefficients and gain are
unchanged. Host tests still do not establish board-level real-time performance.

### Distinguishing producer bursts from slow consumption

A full queue does **not** establish a slow FIR. LDAC packets are fully
layout-validated before publication; every decoded LDAC frame must produce
512 bytes (128 stereo PCM16 frames). The compressed-packet queue holds up to
12 packets and currently has no capacity feedback from the PCM queue. At
48kHz, a 4608-frame startup target leaves 3584 frames (74.7ms) free in the
8192-frame PCM queue. A sufficiently large admitted burst can fill that
space even without slow rendering. The host capacity counterexample models
this possibility only; it does not identify the cause of any phone trace.

On a latched fault, snapshots are taken before diagnostic UART output:

- `fault MIX`: actual ready SRC block count/average wall time, completed
  compute/SRC/write/period times, and an in-progress SRC flag/elapsed time.
  Prefill calls are excluded from the ready-SRC average. A snapshot taken
  during FIR can be one prepared/consumed block ahead of completed SRC blocks.
- `fault DECODE`: raw-queue wait/depth including the current dequeued packet,
  validated LDAC frame count, and decoded PCM frame count updated **before**
  the PCM admission callback. An overflow may admit only part of that frame,
  so this count is not interchangeable with queue `pushed`. Completed packet
  processing times still exclude the current packet. Raw timestamp deltas
  and same-timestamp/CRC observations are diagnostic, never a deduplication
  or rejection rule; fragments, silence and CRC collisions require care.
- `fault START`: first PCM arrival, first successful prepare, and queue/target
  at that first prepare, relative to output admission. `UINT32_MAX` means
  unseen, not zero. This separates source/open delay from buffer fill time.
- `fault FLOW`: up to sixteen 20ms wall-time bins of successfully pushed and
  consumed input frames, accepted RX packet counts and queue watermarks.
  These are fixed bins, not exact sliding-window maxima; edge bins can be
  partial, and the trace records preparation rather than completed I2S output.

Sustained low consumption plus rising queues differs from a single high
production bin. Correlate both with raw-queue waits and actual SRC timing;
neither one maximum nor the queue accounting identity proves a root cause.
No additional attenuation, automatic dropping, queue expansion or decoder
backpressure is introduced by this instrumentation. The recorder is fixed
storage, reset per output admission, updated under the existing BT lock and
printed only on failure. No runtime allocation or successful-block UART.
