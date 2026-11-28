"""Offline ADAU1451 ASRC0 configuration audit. Never connects to hardware.

A cached /api/dsp/status JSON can be decoded with --snapshot. Neither a
source configuration nor one lock snapshot establishes continuous playback.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
NAMES = {0xF100: "ASRC_INPUT0", 0xF140: "ASRC_OUT_RATE0",
         0xF580: "ASRC_LOCK", 0xF581: "ASRC_MUTE", 0xF582: "ASRC0_RATIO"}


def word(value):
    if type(value) is not int or not 0 <= value <= 0xffff:
        raise ValueError("register value must be an unsigned 16-bit integer")
    return value


def decode_asrc0(registers):
    """Decode bit semantics, not whether output is actually safe or silent."""
    r = {word(k): word(v) for k, v in registers.items()}
    mute = r.get(0xF581)
    lock = None if 0xF580 not in r else bool(r[0xF580] & 1)
    ramp = None if mute is None else not bool(mute & 0x100)
    requested = None if mute is None else bool(mute & 1)
    auto = None if mute is None else bool(mute & 0x400) and ramp
    ratio = r.get(0xF582)
    return {
        "registers_hex": {NAMES[k]: f"0x{v:04x}" for k, v in r.items() if k in NAMES},
        "serial_ch0_1_selected": None if 0xF100 not in r else r[0xF100] == 1,
        "target_is_start_pulse": None if 0xF140 not in r else (r[0xF140] & 15) == 5,
        "asrc0_locked_at_read": lock,
        "block0_mute_ramp_enabled": ramp,
        "lockmute_bit_set": None if mute is None else bool(mute & 0x400),
        "lockmute_configured_for_asrc0": auto,
        "manual_mute_requested": requested,
        "manual_mute_control_enabled": ramp,
        "ratio_input_over_output": ratio / 4096 if lock is True and ratio is not None else None,
        "ratio_lsb_ppm_near_unity": 1e6 / 4096,
        # Clock loss can freeze the ASRC filter: control bits do not prove zero output.
        "output_actually_muted": None,
        "continuous_lock_verified": None,
        "audio_acceptance": "NOT_MEASURED",
    }


def boot_registers(text):
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    match = re.search(r"static\s+const\s+uint16_t\s+config\[\]\[2\]\s*=\s*\{(.*?)\};", text, re.S)
    if not match:
        raise ValueError("literal DSP boot config table not found; review parser")
    literals = re.findall(r"\{\s*(0x[0-9a-fA-F]+|[0-9]+)\s*,\s*(0x[0-9a-fA-F]+|[0-9]+)\s*\}", match[1])
    remainder = re.sub(r"\{\s*(0x[0-9a-fA-F]+|[0-9]+)\s*,\s*(0x[0-9a-fA-F]+|[0-9]+)\s*\}", "", match[1])
    if remainder.replace(",", "").strip():
        raise ValueError("nonliteral DSP boot config; review parser")
    result = {}
    for address, value in literals:
        address, value = int(address, 0), int(value, 0)
        if address in result:
            raise ValueError("duplicate DSP boot register")
        result[word(address)] = word(value)
    if not {0xF100, 0xF140, 0xF581} <= result.keys():
        raise ValueError("required ASRC boot registers missing")
    return result


def decode_snapshot(data):
    if not isinstance(data, dict) or not isinstance(data.get("registers"), list):
        raise ValueError("expected /api/dsp/status object with registers array")
    values, seen = {}, set()
    for item in data["registers"]:
        if not isinstance(item, dict) or type(item.get("valid")) is not bool:
            raise ValueError("each register requires a boolean valid flag")
        address = word(item.get("address"))
        if address in seen:
            raise ValueError("duplicate snapshot register")
        seen.add(address)
        if item["valid"]:
            if type(item.get("error")) is not int or item["error"] != 0:
                raise ValueError("valid snapshot register must have error=0")
            values[address] = word(item.get("value"))
    decoded = decode_asrc0(values)
    usable = (all(data.get(k) is True for k in ("present", "current", "session_connected", "complete"))
              and data.get("busy") is False and type(data.get("result")) is int and data["result"] == 0
              and type(data.get("generation")) is int and data["generation"] > 0
              and type(data.get("captured_ms")) is int and data["captured_ms"] >= 0)
    return {"snapshot_metadata_usable": usable, "generation": data.get("generation"),
            "captured_ms": data.get("captured_ms"),
            "interpretation": "cached point observation only" if usable else "stale/incomplete or missing metadata; not current evidence",
            "decoded": decoded}


def inventory(root=ROOT):
    paths = ["software/main/chime_dsp_boot.c", "dsp/ADAU1451.ssprj",
             "software/main/generated/manifest.json", "software/main/bench_dsp_status.c",
             "software/main/bench_bridge.c", "software/main/bench_sound.c"]
    source = {p: (root / p).read_bytes() for p in paths}
    manifest = json.loads(source[paths[2]])
    matches = {p: (root / p).is_file() and hashlib.sha256((root / p).read_bytes()).hexdigest() == sha
               for p, sha in manifest["source_sha256"].items()}
    return {"evidence_kind": "offline source audit, not running firmware",
            "source_sha256": {p: hashlib.sha256(v).hexdigest() for p, v in source.items()},
            "frozen_manifest_source_matches": matches,
            "running_firmware_matches_workspace": None,
            "boot_config": decode_asrc0(boot_registers(source[paths[0]].decode("utf-8"))),
            "runtime": {"lock_loss_count": None, "max_relock_ms": None,
                        "digital_transients": None, "mix_1min_stable": None, "mix_10min_stable": None}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path, help="decode an existing cached status JSON; no device IO")
    args = parser.parse_args()
    try:
        report = inventory()
        if args.snapshot:
            report["snapshot"] = decode_snapshot(json.loads(args.snapshot.read_text(encoding="utf-8-sig")))
        print(json.dumps(report, ensure_ascii=False, indent=2))
    except (OSError, ValueError, KeyError, TypeError) as exc:
        parser.exit(2, f"DSP ASRC audit: {exc}\n")


if __name__ == "__main__":
    main()
