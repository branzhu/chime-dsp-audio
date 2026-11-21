# Chime LHDC V5 adapter

This component contains only the Chime-facing LHDC V5 wrapper. The decoder core
is provided by `chime_codec_reference`, which is generated from the pinned
WillyBilly06 reference-codec sources during the normal IDF build.

The wrapper performs bounded V5 frame validation and forwards valid frames to
`chime_reference_open(1, ...)` / `chime_reference_decode(...)`.
It does not contain a second decoder or a runtime fallback implementation.

The authoritative provenance, generated-source hashes, license notes and build
instructions are in:

- `../chime_codec_reference/README.md`
- `../chime_codec_reference/UPSTREAM.json`

Supported output is PCM16 at 44.1/48 kHz stereo through the existing single
Bluetooth decoder worker. This component does not add another task, I2S writer,
or audio owner.
