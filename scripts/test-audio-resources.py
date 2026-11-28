"""Tests for the read-only audio inventory, not firmware performance tests."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("audio_resources", ROOT / "scripts/audit-audio-resources.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class ResourceAuditTests(unittest.TestCase):
    def test_literal_defines_fail_closed(self):
        self.assertEqual(audit.define("#define FRAMES 128U\n", "FRAMES"), 128)
        with self.assertRaises(ValueError):
            audit.define("#define FRAMES (64 * 2)\n", "FRAMES")
        with self.assertRaises(ValueError):
            audit.define("#define OTHER_FRAMES 128\n", "FRAMES")

    def test_config_keeps_unknown_distinct_from_disabled(self):
        text = '# CONFIG_OFF is not set\nCONFIG_ON=y\nCONFIG_CORE=0x1\nCONFIG_TARGET="esp32s31"\n'
        self.assertIs(audit.config_value(text, "CONFIG_OFF"), False)
        self.assertIs(audit.config_value("# CONFIG_OFF is not set\r\n", "CONFIG_OFF"), False)
        self.assertIs(audit.config_value(text, "CONFIG_ON"), True)
        self.assertIsNone(audit.config_value(text, "CONFIG_MISSING"))
        self.assertEqual(audit.config_value(text, "CONFIG_CORE"), 1)
        self.assertEqual(audit.config_value(text, "CONFIG_TARGET"), "esp32s31")

    def test_linker_same_line_and_wrapped_sections(self):
        text = ("sram_seg 0x2f000000 0x0007afc0 xrw\n"
                ".iram0.text 0x2f000000 0x1bc5c\n"
                ".dram0.data\n  0x2f01bc80 0x4d64\n"
                " .rodata.src_coeff\n  0x401e6878 0x90900 libmain.a(src.obj)\n")
        report = audit.linker_inventory(text)
        self.assertEqual(report["sram_window"]["bytes"], 503744)
        self.assertEqual(report["sections"][".dram0.data"]["bytes"], 19812)
        self.assertEqual(report["src_coeff"]["bytes"], 592128)
        self.assertIsNone(report["sections"][".dram0.bss"])

    def test_missing_linker_symbols_are_unknown_not_zero(self):
        report = audit.linker_inventory("not a linker map")
        self.assertIsNone(report["sram_window"])
        self.assertIsNone(report["src_coeff"])

    def test_current_resource_formulas_and_provenance(self):
        report = audit.inventory(ROOT)
        self.assertFalse(report["linker"]["available"])
        self.assertIsNone(report["linker"]["matches_current_sources"])
        self.assertEqual(report["output"]["frames_per_block"], 128)
        self.assertAlmostEqual(report["output"]["block_us"], 2666.6666667)
        self.assertEqual(report["output"]["dma_bytes"], 4096)
        self.assertEqual(report["src"]["coefficient_bytes"], 592128)
        self.assertEqual(report["src"]["stereo_macs_per_second"], 18432000)
        rates = report["bluetooth"]["rates"]
        self.assertEqual(rates["44100"]["startup_frames"], 4234)
        self.assertEqual(rates["48000"]["startup_free_frames"], 3584)
        self.assertTrue(all(value is None for value in report["runtime"].values()))
        self.assertTrue(all(len(value) == 64 for value in report["source_sha256"].values()))

    def test_missing_optional_map_does_not_invent_data(self):
        with tempfile.TemporaryDirectory() as directory:
            report = audit.inventory(ROOT, Path(directory) / "absent.map")
        self.assertEqual(report["linker"], {"available": False, "matches_current_sources": None})

    def test_missing_sources_fail_instead_of_using_stale_defaults(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(OSError):
                audit.inventory(Path(directory))


if __name__ == "__main__":
    unittest.main()
