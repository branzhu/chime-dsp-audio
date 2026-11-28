"""Read-only audio resource inventory; NOT a profiler or firmware freshness check.

Print JSON to stdout. Optional linker data is labelled as an existing artifact,
not proof that it matches current sources or the firmware running on a board.
Uses only the Python standard library and never connects to hardware.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def define(text, name):
    match = re.search(r"^#define\s+" + re.escape(name) + r"\s+(\d+)[Uu]?\s*$", text, re.M)
    if not match:
        raise ValueError(f"Missing or non-literal resource constant: {name}")
    return int(match[1])


def config_value(text, name):
    match = re.search(r"^" + re.escape(name) + r"=(.*)$", text, re.M)
    if not match:
        if re.search(r"^# " + re.escape(name) + r" is not set\s*$", text, re.M):
            return False
        return None
    value = match[1].strip()
    if value == "y":
        return True
    if value.startswith('"') and value.endswith('"'):
        return value[1:-1]
    try:
        return int(value, 0)
    except ValueError:
        return value


def linker_inventory(text):
    def region(name):
        match = re.search(r"^" + re.escape(name) + r"\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)", text, re.M)
        return {"address": match[1], "bytes": int(match[2], 16)} if match else None
    coeff = re.search(r"^ \.rodata\.src_coeff\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)", text, re.M)
    return {
        "sram_window": region("sram_seg"),
        "sections": {name: region(name) for name in (".iram0.text", ".dram0.data", ".dram0.bss")},
        "src_coeff": {"address": coeff[1], "bytes": int(coeff[2], 16)} if coeff else None,
    }


def inventory(root, map_path=None):
    paths = ["software/sdkconfig", "software/main/bench_audio.c",
             "software/main/chime_bt_decoder.c", "software/main/chime_bt_src_coeff.h",
             "software/main/include/chime_bt_pcm.h", "software/main/include/chime_tusb_audio_config.h",
             "software/main/chime_render.c", "software/main/chime_bluetooth.c",
             "software/main/chime_bt_src.c", "software/main/include/chime_bt_src_internal.h",
             "software/main/CMakeLists.txt"]
    sources = {path: (root / path).read_bytes() for path in paths}
    text = {path: data.decode("utf-8") for path, data in sources.items()}
    audio = text[paths[1]]
    decoder = text[paths[2]]
    coeff = text[paths[3]]
    pcm = text[paths[4]]
    usb = text[paths[5]]
    cfg = text[paths[0]]
    # These formulas describe the current I2S/PCM layouts, not generic firmware.
    if not re.search(r"sample_rate_hz\s*=\s*48000", audio):
        raise ValueError("Review the inventory: I2S output is no longer literal 48 kHz")
    shape = re.search(r"src_coeff\[(\d+)\]\[(\d+)\]\[(\d+)\]", coeff)
    if not shape:
        raise ValueError("Missing SRC coefficient array shape")
    banks, rows, taps = map(int, shape.groups())
    if rows != define(coeff, "CHIME_SRC_PHASES") + 1 or taps != define(coeff, "CHIME_SRC_TAPS"):
        raise ValueError("SRC shape and declared constants disagree")
    frames = define(audio, "DMA_FRAMES")
    descriptors = define(audio, "DMA_DESCRIPTORS")
    queue_frames = define(pcm, "CHIME_BT_QUEUE_FRAMES")
    hz = 48000
    cpu = config_value(cfg, "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ")
    result = {
        "schema_version": 1,
        "evidence": "static source and optional existing linker artifact; no board measurements",
        "source_sha256": {path: hashlib.sha256(data).hexdigest() for path, data in sources.items()},
        "configuration": {key: config_value(cfg, key) for key in (
            "CONFIG_IDF_TARGET", "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ", "CONFIG_FREERTOS_HZ",
            "CONFIG_FREERTOS_UNICORE", "CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS", "CONFIG_SPIRAM",
            "CONFIG_BT_BLUEDROID_PINNED_TO_CORE", "CONFIG_BT_CTRL_PINNED_TO_CORE",
            "CONFIG_LWIP_TCPIP_TASK_PRIO", "CONFIG_LWIP_TCPIP_TASK_AFFINITY_NO_AFFINITY")},
        "output": {"rate_hz": hz, "frames_per_block": frames,
            "block_us": frames * 1e6 / hz,
            "nominal_cycles_per_core_per_block": cpu * 1e6 * frames / hz if cpu else None,
            "dma_descriptors": descriptors, "dma_bytes": descriptors * frames * define(audio, "FRAME_BYTES"),
            "dma_capacity_ms": descriptors * frames * 1000 / hz},
        "src": {"banks": banks, "rows_per_bank": rows, "taps": taps,
            "coefficient_bytes": banks * rows * taps * 4,
            "one_bank_bytes": rows * taps * 4,
            "stereo_macs_per_output_block": frames * 2 * taps,
            "stereo_macs_per_second": hz * 2 * taps,
            "note": "MAC counts exclude coefficient interpolation, copies, cache stalls and scheduling"},
        "bluetooth": {"pcm_frames": queue_frames, "pcm_payload_bytes": queue_frames * 4,
            "raw_queue_slots": define(decoder, "RAW_DEPTH"),
            "raw_packet_max_bytes": define(decoder, "RAW_MAX_BYTES"),
            "decoder_output_scratch_bytes": define(decoder, "PCM_BYTES"),
            "rates": {}},
        "usb": {"fifo_bytes": define(usb, "CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ"),
            "fifo_capacity_ms": define(usb, "CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ") * 1000 / (hz * 4)},
        "runtime": {"board_firmware_identity": None, "cpu_time": None, "ready_wait": None,
            "isr_load": None, "heap_low_water": None, "stack_high_water": None,
            "dma_underruns": None, "ldac_cpu_by_rate": None},
    }
    for rate in (44100, 48000):
        targets = {label: (rate * define(pcm, constant) + 999) // 1000 for label, constant in (
            ("minimum_frames", "CHIME_BT_TARGET_MIN_MS"), ("startup_frames", "CHIME_BT_TARGET_START_MS"),
            ("recovery_frames", "CHIME_BT_TARGET_MAX_MS"))}
        result["bluetooth"]["rates"][str(rate)] = {
            "pcm_capacity_ms": queue_frames * 1000 / rate,
            "startup_free_frames": queue_frames - targets["startup_frames"],
            "startup_free_ms": (queue_frames - targets["startup_frames"]) * 1000 / rate, **targets}
    if map_path is not None and map_path.is_file():
        data = map_path.read_bytes()
        result["linker"] = {"available": True, "sha256": hashlib.sha256(data).hexdigest(),
            "matches_current_sources": None, **linker_inventory(data.decode("utf-8"))}
    else:
        result["linker"] = {"available": False, "matches_current_sources": None}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--map", type=Path, help="Existing map; default software/build/audio_dsp_firmware.map")
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        report = inventory(root, args.map if args.map is not None else root / "software/build/audio_dsp_firmware.map")
    except (OSError, ValueError) as exc:
        parser.exit(1, f"audio resource audit failed: {exc}\n")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
