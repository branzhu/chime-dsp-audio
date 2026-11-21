# Public regression fixtures

These small fixtures let host regressions run without private development documents or vendor datasheet copies.

- `sigma/registers.json`: 530 register addresses, identifiers and access modes transcribed from the ADAU145x Rev. D overview. The source URL and local source-text hash are retained; no datasheet prose/PDF is bundled.
- `sigma/plus-3.4.0.json`, `classic-4.7.json`, `zero-chip-alias.json`: minimal protocol bytes and assertions from local native-driver loopback tests. Workstation paths, driver binaries and collection metadata are omitted. Original record hashes identify provenance, not an authorization grant.
- `calibration/lr-unity-left.json`: original historical measurement values and their conditions. This is a regression fixture, not a calibration or safety claim for the current firmware/DSP gains.

Codec fixtures remain under `software/tests/bluetooth/fixtures/`, with their own provenance and generators. Neither group replaces board, RF or acoustic validation.
