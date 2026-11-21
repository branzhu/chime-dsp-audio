# Chime firmware

ESP-IDF 6.1 / ESP32-S31 firmware for the Chime DSP audio board.

- [Project overview](../README.md)
- [Build and host tests](../docs/build.md)
- [Architecture and code map](../docs/architecture.md)
- [Safety and known issues](../docs/safety.md)
- [Licenses and third-party materials](../docs/licenses.md)

From an ESP-IDF environment at the repository root:

```sh
idf.py --preview -C software build
```

This is a bench/development firmware, not a production safety claim. Project-owned software is licensed under Apache-2.0; third-party code retains its own terms (see [scope](../LICENSES/README.md) and [notices](../THIRD_PARTY_NOTICES.md)). Imported LDAC and some reference adapter sources have unresolved redistribution permissions; disabling a build option does not remove those sources from a published repository.

Do not erase NVS, flash only the app over an older smaller partition table, or start playback at a high host/phone volume. See the build and safety documents before flashing. Sources and generated DSP data are hash-pinned; retain the repository byte-preservation attributes.
