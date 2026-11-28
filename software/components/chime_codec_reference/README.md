# ESP32-S31 codec-only import

All seven external Bluetooth decoders now come from
[WillyBilly06/ESP32-S31-A2DP-Codecs](https://github.com/WillyBilly06/ESP32-S31-A2DP-Codecs),
commit `71071a783811799d2977e4e87f733acdc2220c29`.
`UPSTREAM.json` pins every imported byte. Re-import with
`python scripts/import-s31-codecs.py <local-upstream-checkout>`.

| Product codec | Selected upstream library |
| --- | --- |
| SBC | `external/sbc/decoder` (OI) |
| AAC-LC | `external/libaac-lc/libhelix_aac` + `stack/a2dp/latm_parser` |
| aptX / aptX HD | `external/libfreeaptx/main` |
| Android Opus | `external/opus` + `external/opus_config` (fixed-point) |
| LDAC | `external/libldac-dec` |
| LHDC V5 | `external/lhdc-v5` |

Only libraries are linked, not the upstream fork's Bluetooth host, AVDTP,
worker, I2S, controls or app. `chime_s31_codec.c` is the explicit, single-worker
lifecycle/PCM16 boundary. LDAC/LHDC's old public frame adapters remain thin
frontends. There is no fallback to `esp_audio_codec`, the old native_bt port,
or the legacy standalone `chime_freeaptx` component.

Product capabilities remain unchanged: SBC 16/32/44.1/48k mono/stereo,
AAC 44.1/48k mono/stereo, vendor codecs 44.1/48k stereo (Opus 48k/20ms only).
No new aptX LL or LC3plus SEP is advertised. Turning vendor codecs off still
uses these S31 SBC/AAC sources; external-codec off intentionally retains the
stock IDF internal SBC recovery mode. Queue depth, epochs, STOP cancellation,
CPU1 priority-5 decoder ownership, CPU0 mixer and sole I2S writer are retained.
Vendor worker stack is 8192 bytes, matching the upstream worker's budget;
actual high-water/real-time acceptance still requires board measurement.

## Auditable local adaptations

`prepare.py` checks all pins BEFORE writing output, then copies sources into
the build directory. `CHIME_PORT.json` records actual generated-byte hashes.
Imported bytes are never edited in place. Local changes are:

- Isolate SBC's compile inclusion gate from private Bluedroid headers.
- Use malloc/free for Opus instead of private Bluedroid allocator names;
  retain fixed-point arithmetic and the upstream C99 VLA scratch strategy.
- Select Helix portable integer arithmetic on RISC-V and 64-bit MSVC hosts;
  compile only the upstream's explicit AAC-LC source list (no SBR).
- Reject bit-truncated LATM payloads, negative LDAC scale-factor indexes and
  LDAC bitreader overrun; keep valid Huffman lookahead using padded input.
- Reject LHDC transform-cache allocation failure rather than admitting an
  unusable decoder. Preserve upstream transform, gain and concealment policy.
- Exclude command-line Opus demo/compare executables from the codec library.

Build uses `-O2 -fwrapv`, no global fast-math, no Xtensa flags, no ASRC/FIR change.
Source stack-usage reports are emitted for resource review.

## Intentional upstream differences / limits

**LHDC PCM16 includes the selected S31 source's explicit -5.60 dB level trim.**
The retired native port did not have this behavior. Independent tone fixtures
therefore no longer meet the old unity-level expectation. The difference is also bitrate-dependent (e.g. 48k/q4 yields about 1812 for
an input target of 6000). The original unity-level test is UNCHANGED and fails;
the backend suite intentionally returns nonzero. A separately named structural
test checks coherence/reset/malformed inputs, not level acceptance. No threshold
relaxation or downstream boost hides the failed quality gate.

The S31 LHDC decoder does not expose the retired native port's channel-error /
concealment collector. `chime_reference_quality_snapshot` returns unavailable,
leaving its output unchanged, rather than publishing fabricated zero errors.
Existing transport/decoder errors remain visible. Upstream LHDC can itself
conceal/silence bad frames; successful decode is not certification or a guarantee
that neither channel was concealed.

## Licensing / verification

Preserve source headers and imported license texts. SBC is Apache-2.0;
Opus retains its BSD-style notices and IPR statements; freeaptx sources are
LGPL-2.1-or-later (the surrounding directory's CC0 statement does NOT override
those headers). Helix AAC carries RealNetworks RPSL/RCSL notices, not MIT.
LDAC/LHDC redistribution and patent/product rights need separate review; the
selected repository does not provide a blanket license grant. Public export
remains blocked pending clearance; see `THIRD_PARTY_NOTICES.md`.

Run `python scripts/test-reference-codec-port.py`,
`python scripts/test-s31-codecs.py` (GCC, including MinGW on Windows),
`node scripts/test-bluetooth.mjs`, and the normal S31 IDF build.
The backend suite also injects allocation failures across all seven codecs and
checks frontend short-capacity/unaligned-buffer compatibility. It decodes actual
SBC/AAC/Opus/aptX data, independent LDAC/LHDC
fixtures, malformed data and actual-worker integration. These are not RF,
phone interoperability, USB coexistence, CPU performance or physical-output
acceptance. No flash or playback is part of this migration.

## LDAC real-time memory contract

The S31 generic heap can fall back from HP SRAM to RTC/LP RAM. Internal/8-bit
capabilities alone do not distinguish their performance. The target LDAC unity
units therefore route **all** malloc/calloc/free calls to a 16KiB, 16-byte-aligned
HP SRAM arena (`chime_ldac_memory.c`). There is no RTC/PSRAM fallback. Allocation
and free remain single-decoder-task operations; atomic counters are diagnostic,
not a cross-task allocation API. Eight first-fit slots support pool replacement,
partial-open cleanup and reopen without a monotonic bump-pointer leak.

The imported LHDC 960/1920 IMDCT self-tests previously held 17,280 bytes of static
scratch even when LDAC was selected. The generated port now allocates that
scratch temporarily and releases it at self-test exit. OOM disables the untested
candidate instead of trusting it. No transform arithmetic is changed. With LHDC
in the default image, reclaiming these arrays offsets the LDAC SRAM reservation;
this is not an extra 16KiB taken blindly from the BT/USB heap.

`python scripts/test-ldac-memory.py` compares the actual imported LDAC decoder's
PCM hashes with heap vs arena allocation, across 12 real 44.1/48k tone/stress
fixtures, reset, exhaustion and partial-open cleanup. `python
scripts/test-s31-codecs.py --ldac-only` exercises the normal wrapper and actual
worker with the same allocator. Neither test measures S31 execution time.

The default image enables ESP-timer-based FreeRTOS runtime accounting. Existing
`/api/audio/diagnostics` scheduler counters separate task allocation from packet
wall time (ISR attribution still applies). Decoder diagnostics add
`ldac_fast_reserved_bytes`, `ldac_fast_used_bytes`, `ldac_fast_peak_bytes`,
`ldac_fast_failures`; peak/failures are boot-lifetime, not per epoch. Zero used
bytes after STOP is expected. The startup log includes arena and worker stack /
output addresses so placement remains observable. Target throughput and real
Bluetooth/USB acceptance must still be measured.
