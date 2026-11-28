"""Offline decoder tests, not hardware or audio acceptance."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("dsp_asrc", ROOT / "scripts/audit-dsp-asrc.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class AsrcAuditTests(unittest.TestCase):
    def test_production_0700_does_not_enable_muting(self):
        r = audit.decode_asrc0({0xf581: 0x0700})
        self.assertTrue(r["lockmute_bit_set"])
        self.assertFalse(r["block0_mute_ramp_enabled"])
        self.assertFalse(r["lockmute_configured_for_asrc0"])
        self.assertFalse(r["manual_mute_control_enabled"])

    def test_manual_bit_does_not_override_ramp_disable(self):
        r = audit.decode_asrc0({0xf581: 0x0701})
        self.assertTrue(r["manual_mute_requested"])
        self.assertFalse(r["manual_mute_control_enabled"])
        self.assertIsNone(r["output_actually_muted"])

    def test_candidate_bits_not_an_audio_safety_proof(self):
        for value in (0x0400, 0x0600):
            r = audit.decode_asrc0({0xf581: value})
            self.assertTrue(r["lockmute_configured_for_asrc0"])
            self.assertIsNone(r["output_actually_muted"])
            self.assertEqual(r["audio_acceptance"], "NOT_MEASURED")

    def test_block1_ramp_does_not_affect_asrc0(self):
        self.assertTrue(audit.decode_asrc0({0xf581: 0x0200})["block0_mute_ramp_enabled"])

    def test_absent_is_not_unlocked_or_unmuted(self):
        r = audit.decode_asrc0({})
        for key in ("asrc0_locked_at_read", "block0_mute_ramp_enabled", "manual_mute_requested", "ratio_input_over_output"):
            self.assertIsNone(r[key])

    def test_lock_is_bit0_not_any_lock(self):
        self.assertFalse(audit.decode_asrc0({0xf580: 2})["asrc0_locked_at_read"])
        self.assertTrue(audit.decode_asrc0({0xf580: 3})["asrc0_locked_at_read"])

    def test_ratio_direction_and_unlock_invalidates_ratio(self):
        self.assertEqual(audit.decode_asrc0({0xf580: 1, 0xf582: 0x2000})["ratio_input_over_output"], 2)
        self.assertIsNone(audit.decode_asrc0({0xf580: 0, 0xf582: 0x1000})["ratio_input_over_output"])

    def test_bad_word_types_and_ranges_rejected(self):
        for value in (True, -1, 65536, 1.0, "0x1000", None):
            with self.assertRaises(ValueError):
                audit.decode_asrc0({0xf581: value})

    def snapshot(self):
        return dict(present=True, current=True, complete=True, session_connected=True, busy=False,
                    generation=3, captured_ms=200, result=0,
                    registers=[dict(address=0xf580, valid=True, error=0, value=1)])

    def test_snapshot_never_proves_continuity(self):
        r = audit.decode_snapshot(self.snapshot())
        self.assertTrue(r["snapshot_metadata_usable"])
        self.assertTrue(r["decoded"]["asrc0_locked_at_read"])
        self.assertIsNone(r["decoded"]["continuous_lock_verified"])

    def test_stale_incomplete_or_missing_metadata_is_unusable(self):
        for key in ("current", "complete", "present", "session_connected", "generation", "captured_ms", "result", "busy"):
            data = self.snapshot()
            del data[key]
            self.assertFalse(audit.decode_snapshot(data)["snapshot_metadata_usable"])
        data = self.snapshot(); data["current"] = False
        self.assertFalse(audit.decode_snapshot(data)["snapshot_metadata_usable"])

    def test_unread_value_not_treated_as_zero(self):
        data = self.snapshot(); data["registers"][0]["valid"] = False
        self.assertIsNone(audit.decode_snapshot(data)["decoded"]["asrc0_locked_at_read"])

    def test_malformed_and_duplicate_snapshots_rejected(self):
        data = self.snapshot(); data["registers"] *= 2
        with self.assertRaises(ValueError): audit.decode_snapshot(data)
        for key, value in (("valid", 1), ("value", True), ("error", 1)):
            data = self.snapshot(); data["registers"][0][key] = value
            with self.assertRaises(ValueError): audit.decode_snapshot(data)
        with self.assertRaises(ValueError): audit.decode_snapshot({})

    def test_current_sources_and_frozen_provenance(self):
        r = audit.inventory()
        self.assertTrue(r["boot_config"]["serial_ch0_1_selected"])
        self.assertTrue(r["boot_config"]["target_is_start_pulse"])
        self.assertFalse(r["boot_config"]["lockmute_configured_for_asrc0"])
        self.assertTrue(all(r["frozen_manifest_source_matches"].values()))
        self.assertTrue(all(v is None for v in r["runtime"].values()))

    def test_nonliteral_boot_table_fails_closed(self):
        for text in ("", "static const uint16_t config[][2]={{0xf581,MODE}};",
                     "static const uint16_t config[][2]={{0xf581,0x700}};"):
            with self.assertRaises(ValueError): audit.boot_registers(text)


if __name__ == "__main__":
    unittest.main()
