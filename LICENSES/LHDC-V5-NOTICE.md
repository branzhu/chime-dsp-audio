# LHDC V5 upstream license declaration

Source: [WillyBilly06/LHDC-V5-Decoder README](https://github.com/WillyBilly06/LHDC-V5-Decoder/blob/cf857ce1a1429965e161e54adbc3f293fd9ce273/README.md#license)

Revision: `cf857ce1a1429965e161e54adbc3f293fd9ce273`  
SHA-256 of the complete upstream README retrieved for this audit: `80912397b3e01db7a57f9be5fcbc47ce460dd6f73925d0cea8592836aea60bcd`

The following is the upstream License section, quoted verbatim:

> This decoder implementation is released under the **Apache License 2.0**. Note that this
> does **not** grant any rights to the LHDC codec itself, which is the property of Savitech;
> obtaining LHDC licensing for your use case is your responsibility.

The Apache-2.0 text is included in the repository root LICENSE. This is an
upstream declaration about that decoder implementation, not a license grant
for all files in the separate ESP32 reference repository or for codec patents.
The selected upstream revision does not contain a root LICENSE file.

Snapshot provenance is recorded in
`software/components/chime_codec_reference/vendor/native_bt/codecs/lhdcv5/source_snapshot/SOURCE.json`:
fourteen portable C/header files match the comparison revision; the two
`decoder/lhdc_imdct.c` and `decoder/lhdc_imdct.h` files include the tested
user-provided working changes. Their changes and authorship must remain
traceable; comparison hashes do not grant rights to modifications.
Chime's generated-port edits are documented in the component README and
prepare scripts. Original snapshot bytes and hash pins are preserved.
