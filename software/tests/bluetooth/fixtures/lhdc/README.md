# LHDC V5 decoder fixtures (NOT official LHDC vectors)

Generated using the **independent unofficial encoder**, not this decoder:
<https://github.com/LingYi0110/liblhdcv5-c> at
`699a624f4f04b6419af2f4a5ef3cc40dd39d2855` (Apache-2.0).
No Savitech binary, song, or captured user audio is included.

`PROVENANCE.json` pins the encoder, generator, signal and all output hashes.
Each file concatenates 80 raw frames: `[LE16 length/flags][payload]`.
There is no RTP or two-byte A2DP count/sequence header. Each frame represents
240 stereo samples even at 44.1kHz. Left: 997 Hz / amplitude 6000; right:
2003 Hz / amplitude 4000, PCM16. q3..q8 exercise 256/320/400/500/900/1000kbps
quality indices (actual frame sizes are sample-rate dependent). Negotiation only
advertises 256..500kbps; q7/q8 are extra decoder regression inputs, not advertised
transport modes.

## Reproduce

Compile `software/tests/bluetooth/generate_lhdc_fixtures.c` plus the pinned
encoder's `cirbuf.c`, `lhdcv5_api.c`, and `lhdc_v5/*.c`, include its root directory.
Use C11, optimization O2 and **disable floating-point contraction** (GCC/Clang:
`-ffp-contract=off`; MSVC: `/fp:strict`). Link libm on Unix. Run the resulting
program with this fixture directory as its only argument.

The Windows generation used MSVC 2022 x64, with a test-only forced header:

```c
#include <intrin.h>
#include <string.h>
#define __builtin_bswap32 _byteswap_ulong
#define __builtin_memcpy memcpy
```

Do not assume other compilers produce byte-identical frames without checking
hashes; compiler math may change encoder rounding. The encoder is never linked
into the firmware.

`test_lhdc.c` checks channel tone coherence, the **known ~0.56 amplitude deficit**,
reset identity, truncations/output bounds and malformed frames. This is not an
official Savitech encoder/decoder golden comparison. It intentionally does not
hide the current quality release gate.
