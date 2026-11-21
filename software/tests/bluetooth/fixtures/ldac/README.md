# LDAC decoder test vectors

Six raw LDAC streams with full three-byte frame headers (no RTP / A2DP payload
headers), generated from our synthetic dual-channel tones. SHA-256, encoder
version, input definition and quality mapping are in PROVENANCE.json.

Generator: `../../generate_ldac_fixtures.c` (not part of firmware).
Source: AOSP libldac at `e8ff0f96f26b84b47711c549e0d60baa425cd70e`,
Apache-2.0. Check out this version outside tracked source, then on GCC:

```
cc -std=gnu11 -O2 -I<libldac>/inc -I<libldac>/src \
  software/tests/bluetooth/generate_ldac_fixtures.c \
  <libldac>/src/ldacBT.c <libldac>/src/ldaclib.c -lm -o <generator>
<generator> <output-directory>
```

On MSVC compile as C11. A test-only empty `unistd.h` and a forced-include header
`#define __attribute__(x)` are sufficient for these two AOSP translation units;
unistd functions are not called and the only attribute is `unused`. These shims
must not be applied to production SDK/decoder code. Floating math/encoder
compiler versions may change encoded bytes; do not overwrite pinned files or
hashes without reviewing regenerated vectors and decoder checks.

The real decoder test checks numeric amplitude/frequency/channel isolation and
frame lengths, reset identity and malformed data. These are independently
encoded vectors, not official decoded PCM golden outputs or LDAC certification.
AOSP encoder code is not shipped or linked into this project's firmware.
