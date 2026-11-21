# Selected upstream LDAC / LHDC V5 codecs

User-selected source: https://github.com/WillyBilly06/ESP32-A2DP-SINK-WITH-CODECS-UPDATED
Pinned commit: 45281c5212cf5e22eb1d804e75439d2ac051f7c0 (reviewed 2026-11-19).
UPSTREAM.json records SHA-256 for every imported file; vendor/ retains original bytes.

## What is linked

- LDAC: esp-idf/components/bt/host/bluedroid/external/libldac-dec plus
  native_bt/codecs/ldac/prepare_port.py, dbt_ldac_{lib,bt,decoder}.c and the pure
  configuration/frame parser. This is NOT the retired hegdi/libldacdec port.
- LHDC: native_bt/codecs/lhdcv5/source_snapshot and its prepare_port.py and
  dbt_lhdcv5_codec.c adapter, including the upstream target-level correction,
  configuration-sized storage, single-slot scratch and bounded entropy ports.
  This replaces the older independent decoder component's src/ compilation.
- The first lifecycle/allocation functions from native_bt/codecs/common/dbt_codec.c.
  The upstream codec worker, transport/host, renderer, OTA, storage and hardware
  deployment tools are NOT linked or executed.

prepare.py verifies ALL source pins before generating any output, then invokes
both original upstream generators. It removes only their endpoint-validator
functions because Chime already validates the negotiated CIE in its pinned
Bluedroid shim. It selects lifecycle functions before the upstream worker.
The separate CHIME_PORT.json records actual final generated hashes; the original
upstream port receipts describe their own unmodified generator outputs.

One local defensive change rejects negative LDAC scale-factor indexes as well
as indexes >31. The imported source uses int8_t, while its original validation
checked only >31; ASan malformed-frame tests exposed ga_sf[-1]. This change
rejects malformed input before dequantization, not a different decoder.

Chime's sole existing decoder worker creates/destroys the reference adapters.
The adapter makes one-frame media descriptors for the upstream codec interface,
copies the synchronous callback output, and retains no PCM loan. LDAC's S32
output is rounded/saturated into the existing PCM16 container; no gain/DSP is
added. Rounding avoids a -1 DC bias from tiny negative transform residuals.
LHDC uses the upstream S16 path. Only 44.1/48 kHz stereo is exposed, regardless
of the wider upstream decoder capabilities. LHDC is V5 only, not V2/V3.

No original ESP32 -mfpu=auto or global LTO/fast-math is copied to RISC-V S31.
The component uses -O2 -fwrapv and the upstream guarded arithmetic; target
LHDC quality notifications remain enabled. The one-slot backend has one owner.
Existing source priority, STOP/SS protection, bounded queues, output-fault latch
and sole I2S writer remain unchanged. LHDC V5 is now enabled by default.

## Provenance / license limitations

Preserve the original README, source headers, source_snapshot/SOURCE.json and
its README. The snapshot identifies fourteen files from WillyBilly06/LHDC-V5-Decoder
revision cf857ce1a1429965e161e54adbc3f293fd9ce273 plus two upstream working changes.
The selected repository's libldac-dec directory has no standalone explicit
license grant. Do NOT label it MIT based on a parent README, AOSP's encoder
license, or the deleted legacy decoder's licensing claims. The imported
native_bt adapter code also has no verified covering grant in the
selected reference revision. Chime is a downstream integrator and has not
established the complete licensing chain for LDAC and these adapter sources.
Users must assess upstream information and their own use/redistribution rights;
this statement does not supply missing permission or release the publisher from
its obligations. These sources are excluded from Chime's own Apache-2.0 grant.

The separate LHDC-V5-Decoder README at the comparison revision explicitly
licenses that decoder implementation under Apache-2.0; see the preserved
[declaration](../../../LICENSES/LHDC-V5-NOTICE.md) and
[third-party notices](../../../THIRD_PARTY_NOTICES.md). It does not license all
reference adapters or grant codec patent rights. Preserve provenance for the
two working-change files. LDAC/adapter redistribution and codec patent / product
permissions remain review items; the local public exporter remains blocked.
Importing the user-selected source
is not a Sony/Savitech certification or legal clearance statement.

## Tests / build

Use the normal IDF build; no separate upstream SDK checkout is required at build
time, and no global SDK files are edited. Turn off CHIME_BT_LHDC_V5 for six codecs;
turn off CHIME_BT_VENDOR_CODECS for SBC/AAC; external-codec off gives stock SBC.
No fallback to the retired decoders is built.

scripts/test-reference-codec-port.py tests deterministic generation and rejects
a tamper in each of 146 imported files before output. scripts/test-bluetooth.mjs
runs actual reference cores against the independent encoder fixtures and malformed
inputs, allocation-failure cleanup, singleton recovery and reopen identity.
BT_LDAC_ASAN=1 / BT_LHDC_ASAN=1 enable ASan. The MSVC host shim maps only GCC syntax,
branch hints and clz to exact host equivalents. Optional LHDC diagnostic counters
are disabled on MSVC because its atomics do not satisfy the upstream lock-free
assertion; firmware retains them. Host tests do not establish RF/RTOS throughput.


## S31 allocation contract / startup diagnostics (2026-11-19)

The upstream common codec hooks require **8-byte aligned** allocations and reject
misaligned pointers as DBT_NO_SPACE even if the heap allocation succeeded. IDF
TLSF normally guarantees only 4-byte alignment. The adapter now uses the shared
chime_reference_memory.h shim for both codec hooks and wrapper state: overflow-
checked zeroed over-allocation, align to 8, and bytewise saved original pointer
for exact free. This path is shared by host and target; no reference arithmetic,
queues or I2S ownership changed. Per allocation overhead is sizeof(void*)+7 bytes.

Generated LHDC failure branches now report configuration / singleton / workspace /
state allocation / decoder-init-or-entropy / PCM scratch and the inner init
workspace / buffers / IMDCT / window / entropy stage, without changing the return
values. Imported vendor bytes stay pinned and unchanged; CHIME_PORT.json records
these generated diagnostics. The wrapper logs the actual DBT configure/open result,
failed allocation request, successful allocation count and target internal heap
statistics. Success logs codec, rate, accounted codec bytes, count and alignment=8.
The existing worker now reports generic open failure as ESP_FAIL rather than
claiming every NULL means ESP_ERR_NO_MEM. There is no automatic retry or fallback.

Lifecycle tests use a raw heap that deliberately returns addresses congruent to
4 modulo 8, and verify zeroing, bounds/overflow, allocation-fault cleanup and
exact original-pointer freeing under ASan. A raw unaligned hook is explicitly
rejected by the reference lifecycle, reproducing the pre-fix contract failure.
LHDC is also compiled without LHDC_HOST_BUILD in a second host test variant to
exercise target window-init allocations and their failures. Only placement
attributes / platform logging / wall-clock access are stubbed there; codec math
and hooks are real. This does not prove hardware heap state or sustained playback.


## Product LHDC media-header compatibility

The reference media adapter takes frame count from bits 7:2 and accepts all four
low-bit values. Chime's old worker incorrectly rejected any nonzero low bits;
this extra restriction is removed. A phone's 506-byte 09:02:7d:04 packet prefix
represents count2, sequence2, first frame length252. Product full-frame/count/
length/sequence validation remains. Worker unit tests exhaust all counts and low
bits, and an integration variant links the real worker + product frame wrapper +
reference decoder against independent encoder fixtures, checking identical PCM
for all four low-bit values. This reproduces the layout, not the phone's full
compressed payload or RF/RTOS performance.


## LDAC ABR regression / rejection capture

A 661-byte post-AVDTP packet with prefix 05:aa:32:00 is a legal layout (five
132-byte frames); the four-byte prefix alone cannot validate the later frames.
The product worker's existing length arithmetic already agrees with upstream.
No acceptance checks are relaxed on this evidence. New test-only Sony/AOSP
encoder fixtures preserve actual live HQ/SQ/MQ/Q0/Q1 switching packet boundaries,
including mixed frame lengths and 21 packets matching that prefix/length. Actual
worker -> wrapper -> reference-core PCM is compared byte-for-byte against direct
sequential frame decoding without reopening between bitrate changes.

On an LDAC layout fault, the product worker now invalidates the stream before
logging the failing frame index/offset/header/declared length/remainder and a
bounded dump of the original rejected packet (up to1024 bytes, truncation marked).
It does not log on normal packets, allocate capture buffers, resync or auto-retry.
ASan tests check each-frame corruption, count/truncation failures, zero PCM before
whole-packet rejection, log-after-invalidation ordering and exact bounded capture.
The earlier encoder fixtures reproduce the layout, not the phone's payload.

On 2026-11-21 the complete failing phone packet was captured: 657 bytes with
four declared 164-byte frames, but frame2 sync is 3f instead of aa at original
packet offset165 (payload offset164). Total length and later frame boundaries
align; rejection is an invalid sync, not a truncated packet or ABR-size mismatch.
The exact unmodified packet and source hashes are in the ldac-phone-failure test
fixtures. Actual-worker tests require rejection before any PCM and exact capture;
a synthetic sync-only corrected control passes layout but the real core still
rejects its first frame before PCM. It is not a production repair or proof that
only one byte is corrupt (nor proof of body corruption vs decoder compatibility). Capture is fault-time only: phone/SDK/local
memory origin remains unproven. See docs/bluetooth-ldac-captured-failure-2026-11-21.md.
Production acceptance/recovery behavior is unchanged.

A second full 657-byte phone sample also fails frame2 at packet offset165, now
with 15:22:60 (invalid sync and mono mode). Its preceding output has zero gap
blocks and 4471 queued PCM frames. Frame-isolation tests now retain both exact
packets and inspect actual SDK errors: sample2 frames3/4 decode; both sample1
and sample2 frame1 reject with SYNTAX_SPEC (267). Sample1's sync-corrected frame2
alone decodes; sample2's synthetic full-header-corrected frame2 rejects with
SYNTAX_GRAD_D (264). Cold and known-valid warm-up controls agree. The product
worker still rejects each original WHOLE packet before PCM; no partial salvage
or header repair is added. These are host syntax/ownership tests, not proof of
corruption origin, phone-history equivalence, RF correctness or audible PCM.


## LHDC glitch diagnostics (not a confirmed playback fix)

The wrapper now starts the already-compiled upstream quality collector before
opening LHDC and exposes a bounded atomic snapshot facade. Counters cover this
boot and do not reset on reopen; metadata describes the last exceptional attempt.
Right-channel silence, peak concealment, nonfinite and clipping events may occur
without decode returning an error. No vendor bytes, arithmetic or PCM are changed.
The facade omits the wrapper's synthetic RTP sequence/timestamp/SSRC to avoid
misrepresenting these as phone packet metadata. Raw float-container peak bits are
not bitrate, acoustic level or speaker voltage. A clipped count alone is not proof
of a decoder defect or justification to change gain.

Product BT FLOW separately counts post-prefill zero-filled 128-frame gaps, even
when recovery occurs before the original fatal 100ms timeout. Initial prefill is
separate; these diagnostics reset per output admission, not per boot. BT DECODE
reports bounded atomic completed-packet snapshots with independent epoch and WALL
times (including preemption). The existing low-priority owner logs these and
LHDC QUALITY once per second and once after stopping; no new normal PCM/decode
UART, I2C, queue growth, gain, SRC, retry or source-policy change is introduced.
Zero gap/quality counters do not rule out every I2S/clock or numerical issue.

MSVC cannot satisfy upstream's lock-free collector static assert. Its facade test
therefore mocks ONLY that collector while linking the real wrapper/lifecycle/core;
it is explicitly not a target atomic-collection test. GCC-compatible builds test
real collector counts against known LHDC fixtures. Target firmware keeps the
upstream assert and real collector. RF/RTOS/phone-music/analog validation is pending.

Gap blocks count short render attempts, including the terminal timeout attempt.
Only nonfatal ESP_OK zero blocks are passed to the I2S writer; these are not
measurements of DMA-transmitted/lost frames or physical speaker output.


## LDAC cross-stage input evidence (2026-11-21)

The product worker now records bounded IEEE CRC32/metadata at submission and
before processing LDAC buffers, then samples again on existing fault paths.
Only faults print the three phases plus comparison, after invalidating input.
CRC mismatch alone does not reject otherwise valid input or add recovery.
Changed data pointer/length is rejected before payload access, and fault dumping
is suppressed if metadata changes. Scope is the current original SDK buffer,
not a reconstructed multi-packet frame; CRC matches are not an atomic snapshot
or proof against collisions/concurrent overwrite. No codec arithmetic/vendor
bytes changed. Target descriptor is88 bytes, queue depth remains12. See
`docs/bluetooth-ldac-input-evidence-2026-11-21.md` for validation and limitations.

## Legacy decoder cleanup (2026-11-21)

The old chime_ldac/src and chime_lhdc/src trees, LDAC unity/noflash
core and linker file, and their legacy license/provenance files were removed.
The two Chime components now contain only their wrappers, headers, CMake
registration and current README. Their wrappers still call this component;
no codec math, current vendor pins, generated-port behavior or runtime
fallback was changed. Historical bring-up documents are not build inputs.
