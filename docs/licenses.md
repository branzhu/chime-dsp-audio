English | [简体中文](licenses.zh-CN.md)

# Licenses and third-party materials

The project uses separate licenses for different materials; it does not relicense all third-party content under one grant. See [License scope](../LICENSES/README.md) for full boundaries and [Third-party notices](../THIRD_PARTY_NOTICES.md) for attribution, pinned upstream revisions, and unresolved permissions. These detailed notices are currently in Simplified Chinese; the summary below does not replace them or the original license texts.

## Project-owned materials

| Material | License / boundary |
| --- | --- |
| Project-owned firmware, web UI, scripts, tools, tests, and original DSP configuration contributions | [Apache-2.0](../LICENSE); excludes third-party code, libraries, and third-party content in generated outputs |
| Original design contributions in the EasyEDA project export, schematic and PCB PDFs, and BOM | [CERN-OHL-P-2.0](../LICENSES/CERN-OHL-P-2.0.txt); retain Notices; embedded third-party materials are not automatically included |
| Project-owned models in the published STEP / 3MF files | [CC-BY-4.0](../LICENSES/CC-BY-4.0.txt); does not supply or license unpublished native Creo sources |
| Project-authored public documentation | CC-BY-4.0; retain attribution and links, and identify changes; third-party quotations and license texts are excluded |
| Logos / branding assets, names, and trademarks | Excluded from the above grants; no endorsement or certification rights granted |

These grants cover only rights the contributors are entitled to grant. Exchange models are not complete parametric mechanical source, and the PCB export is not a validated manufacturing package.

## Unresolved permissions for imported code

Chime is a downstream integrator of the LDAC implementation and reference adapter code. The maintainers have not established the complete authorization chain. Users must assess the sources and pinned revisions in [Third-party notices](../THIRD_PARTY_NOTICES.md), applicable law, and the rights needed for their use / redistribution; contact rights holders or qualified advisers where necessary.

**Attribution, research-only wording, or asking users to make their own assessment does not grant permission, remove responsibility, or supply missing authorization.** LDAC and reference adapter code with unverified permissions are not relicensed under Chime's Apache-2.0 or another blanket grant. Their redistribution remains an unresolved issue.

The current seven-codec import is pinned to [ESP32-S31-A2DP-Codecs](https://github.com/WillyBilly06/ESP32-S31-A2DP-Codecs). The historical LHDC upstream declaration in [LHDC-V5-NOTICE.md](../LICENSES/LHDC-V5-NOTICE.md) does not by itself establish a covering grant for this modified S31 snapshot. Review the current manifest, individual source notices, and redistribution / patent rights separately. Helix AAC uses RealNetworks RPSL/RCSL notices, not MIT. freeaptx's LGPL source headers are not overridden by its parent directory's CC0 statement.

## Dependencies and firmware distribution

- freeaptx source headers and COPYING specify **LGPL-2.1-or-later**. Distribution of statically linked firmware requires satisfying applicable corresponding-source, modification, notice, relinking, and debugging reverse-engineering obligations. Uploading source or an upstream URL alone does not prove compliance.
- Historical dependency, removed from the current manifest/lock: `esp_audio_codec` 2.6.2 uses **Espressif Modified MIT**, restricted to use with Espressif products. It is not standard MIT or a general-purpose open-source license. Check the package's internal third-party codec terms separately.
- ESP-IDF, mDNS, esp_tinyusb, TinyUSB, cJSON, and the USB descriptors retain their own licenses and notices. The SDK root license does not cover every dependency under one set of terms.
- Check third-party rights in SigmaStudio / vendor-library outputs and in PCB / mechanical assets separately. Provenance hashes establish integrity, not authorization.

## Before reuse or redistribution

1. Check the applicable license texts, scope, NOTICE, and third-party provenance for the version you use.
2. Obtain sufficient authorization for LDAC / reference adapter code, or actually remove the relevant sources and dependencies and validate the resulting build. Disabling a Kconfig option is not source removal.
3. Prepare and verify LGPL and other dependency distribution materials; check third-party rights in DSP, PCB, and model files.
4. Check shared logs and modified releases for credentials and personal data. Retain safety limitations and third-party provenance; do not present build success as authorization or safety certification.

These are reuse and redistribution considerations, not comprehensive legal advice. Licensing project-owned materials does not establish clearance for all imported content.
