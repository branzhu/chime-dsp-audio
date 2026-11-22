English | [简体中文](README.zh-CN.md)

# PCB design

[`audio-dsp.epro2`](audio-dsp.epro2) is the PCB project export. Try importing it with a compatible version of **EasyEDA Pro**; compatibility with a specific release has not yet been verified.

The board uses an **ESP32-S31**, an **ADAU1451**, and **two TAS5825M** amplifiers. Check firmware pin assignments and I²C addresses in `software/components/board_support/include/board_support.h` against the PCB before use.

## Reference files

- [Schematic PDF](schematic.pdf)
- [PCB layout PDF](pcb.pdf)
- [Bill of materials (XLSX)](bom.xlsx)

## Before manufacturing or assembly

These files are **not a Gerber / NC Drill manufacturing package or a production-validated design**. Check the BOM against the board revision and component specifications before ordering. Pick-and-place files, the fabrication stack-up, and assembly instructions are not supplied.

Check ERC / DRC, power supplies and ratings, impedance and clocks, USB, cooling, BTL loads, and fault handling. Review any embedded libraries, models, or images under their own terms. Read the [safety notes](../docs/safety.md) before powering the board.

## License and notices

Copyright 2026 Chime contributors. Original PCB / schematic design contributions in the project export, PDFs, and BOM are licensed under **CERN-OHL-P-2.0** (SPDX-License-Identifier: CERN-OHL-P-2.0). See the [full license](../LICENSES/CERN-OHL-P-2.0.txt).

This README, the root [NOTICE](../NOTICE), and [third-party notices](../THIRD_PARTY_NOTICES.md) contain applicable Notices. Retain applicable notices, record the date and a brief description of modifications, and ensure product recipients can access the relevant notices. Third-party footprints, models, and images are not relicensed; no third-party patent, trademark, or certification rights are granted. The license does not certify editability, completeness, or manufacturability. Project-authored text in this README is licensed under CC-BY-4.0.
