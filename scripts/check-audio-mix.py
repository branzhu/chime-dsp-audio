"""Read-only USB+LDAC runtime-counter check (60s then 600s).

Never starts/stops playback, changes volume, writes DSP or claims acoustic
acceptance. Requires the explicit ELF hash and diagnostics schema 1.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import time
import urllib.request


class EvidenceError(ValueError):
    pass


def get(data, path):
    try:
        for key in path.split("."):
            data = data[key]
        return data
    except (KeyError, TypeError):
        raise EvidenceError("missing field: " + path) from None


def number(data, path):
    value = get(data, path)
    if type(value) not in (int, float) or not float("-inf") < value < float("inf"):
        raise EvidenceError("non-finite/non-number field: " + path)
    return value


class Window:
    FAULTS = ("bt.overflows", "bt.underruns", "bt.pcm_gap_events", "bt.pcm_gap_blocks",
              "bt.rebuffer_events", "bt.rebuffer_resumes", "bt.rebuffer_blocks", "bt.discarded_frames",
              "render.usb_short", "render.bt_short", "render.clips", "render.deadline_misses",
              "decoder.capacity_timeouts")
    PROGRESS = ("usb.rx_bytes", "usb.rx_packets", "bt.pcm_frames", "bt.rendered_frames", "render.blocks",
                "decoder.packets", "decoder.ldac_pcm_frames")
    IDENTITIES = ("boot_id", "firmware.elf_sha256", "usb.epoch", "bt.epoch", "bt.output_id", "bt.rate", "decoder.epoch")

    def __init__(self, duration, elf_sha):
        if duration not in (60, 600):
            raise EvidenceError("only 60s and 600s acceptance windows are supported")
        if not re.fullmatch("[0-9a-f]{64}", elf_sha):
            raise EvidenceError("expected explicit SHA256 of the ELF")
        self.duration, self.elf_sha = duration, elf_sha
        self.samples = []

    def add(self, sample, host_elapsed):
        if type(host_elapsed) not in (int, float) or not 0 <= host_elapsed < float("inf"):
            raise EvidenceError("invalid host monotonic time")
        if number(sample, "schema") != 1:
            raise EvidenceError("unsupported diagnostics schema")
        if get(sample, "firmware.elf_sha256") != self.elf_sha:
            raise EvidenceError("running ELF differs from requested ELF")
        if not isinstance(get(sample, "boot_id"), str) or not re.fullmatch("[0-9a-f]{16}", sample["boot_id"]):
            raise EvidenceError("missing/invalid boot identity")
        if not 0 <= number(sample, "snapshot_span_us") <= 50000:
            raise EvidenceError("software snapshot spans over 50ms or is invalid")
        for path in ("source.stopped", "usb.suspended", "usb.muted"):
            if get(sample, path) is not False:
                raise EvidenceError("source not ready: " + path)
        for path in ("source.amps_enabled", "source.dsp_ready", "usb.installed", "usb.streaming", "usb.wanted",
                     "usb.coherent", "bt.connected", "bt.playing", "bt.output_active", "bt.primed", "decoder.valid"):
            if get(sample, path) is not True:
                raise EvidenceError("source/snapshot not ready: " + path)
        for path, expected in (("source.selected", 3), ("source.rendering", 3), ("source.sound_error", 0),
                               ("usb.error", 0), ("bt.error", 0), ("bt.codec", 6), ("bt.channels", 2), ("bt.stream_state", 2)):
            if number(sample, path) != expected:
                raise EvidenceError("unexpected state: " + path)
        if number(sample, "bt.rate") not in (44100, 48000):
            raise EvidenceError("this check covers LDAC 44.1/48k only")
        if not 0 <= number(sample, "bt.queued_frames") <= 8192:
            raise EvidenceError("BT queue outside configured bound")
        for path in self.FAULTS + self.PROGRESS + ("sampled_us", "usb.epoch", "bt.epoch", "bt.output_id", "decoder.epoch"):
            value = number(sample, path)
            if type(value) is not int or value < 0:
                raise EvidenceError("invalid counter: " + path)
        if number(sample, "bt.epoch") != number(sample, "decoder.epoch"):
            raise EvidenceError("decoder belongs to another BT epoch")
        if self.samples:
            previous, previous_host = self.samples[-1]
            for path in self.IDENTITIES:
                if get(sample, path) != get(previous, path):
                    raise EvidenceError("epoch/rate/firmware changed: " + path)
            device_dt = (sample["sampled_us"] - previous["sampled_us"]) / 1e6
            host_dt = host_elapsed - previous_host
            if not (0 < device_dt <= 3 and 0 < host_dt <= 3):
                raise EvidenceError("missing observations or clock/counter restart (>3s gap)")
            if abs(device_dt - host_dt) > 0.5:
                raise EvidenceError("host/device elapsed times disagree")
            for path in self.FAULTS:
                if get(sample, path) != get(previous, path):
                    raise EvidenceError("fault/short/clip/deadline counter changed: " + path)
            for path in self.PROGRESS:
                if get(sample, path) <= get(previous, path):
                    raise EvidenceError("source progress stopped or reset: " + path)
        # Keep immutable observations even if a caller later reuses its object.
        self.samples.append((json.loads(json.dumps(sample)), host_elapsed))

    def result(self):
        if len(self.samples) < 2:
            raise EvidenceError("insufficient observations")
        first, host0 = self.samples[0]; last, host1 = self.samples[-1]
        device_seconds = (last["sampled_us"] - first["sampled_us"]) / 1e6
        host_seconds = host1 - host0
        if min(device_seconds, host_seconds) < self.duration:
            raise EvidenceError("full requested interval not observed")
        rates = {}
        for path, expected, tolerance in (("usb.rx_bytes", 192000, .05),
                                          ("bt.pcm_frames", first["bt"]["rate"], .05),
                                          ("bt.rendered_frames", 48000, .01),
                                          ("render.blocks", 48000 / 128, .01)):
            rate = (get(last, path) - get(first, path)) / device_seconds
            rates[path] = rate
            if abs(rate / expected - 1) > tolerance:
                raise EvidenceError("average source/render rate out of range: " + path)
        return {"result": "RUNTIME_COUNTERS_PASS", "duration_requested_s": self.duration,
                "device_elapsed_s": device_seconds, "host_elapsed_s": host_seconds,
                "observations": len(self.samples), "elf_sha256": self.elf_sha,
                "rates_per_s": rates,
                "queue_delta_frames": last["bt"]["queued_frames"] - first["bt"]["queued_frames"],
                "audio_content_verified": False, "digital_clock_continuity_verified": False,
                "mic_result": "NOT_MEASURED", "end_to_end_acceptance": "NOT_PROVEN"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True, help="explicit board HTTP URL; proxies disabled")
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--duration", type=int, choices=(60, 600), default=60)
    parser.add_argument("--output", type=Path, required=True, help="new evidence directory (must not exist)")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    sha = hashlib.sha256(args.elf.read_bytes()).hexdigest()
    window = Window(args.duration, sha)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    endpoint = args.url.rstrip("/") + "/api/audio/diagnostics"
    start = None
    result = None
    with (args.output / "observations.jsonl").open("x", encoding="utf-8") as log:
        try:
            while True:
                with opener.open(endpoint, timeout=2) as response:
                    raw = response.read(65537)
                if len(raw) > 65536:
                    raise EvidenceError("unexpectedly large diagnostics response")
                sample = json.loads(raw)
                now = time.monotonic()
                if start is None:
                    start = now
                elapsed = now - start
                # Record rejected observations, too. Never silently discard a failure.
                log.write(json.dumps({"host_elapsed_s": elapsed, "observed_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "sample": sample}) + "\n")
                log.flush()
                window.add(sample, elapsed)
                device_elapsed = (sample["sampled_us"] - window.samples[0][0]["sampled_us"]) / 1e6
                if min(elapsed, device_elapsed) >= args.duration:
                    result = window.result()
                    break
                time.sleep(1)
        except (EvidenceError, OSError, ValueError) as exc:
            result = {"result": "NOT_PASSED", "reason": str(exc), "accepted_observations": len(window.samples),
                      "audio_content_verified": False, "end_to_end_acceptance": "NOT_PROVEN"}
    result["duration_requested_s"] = args.duration
    result["expected_elf_sha256"] = sha
    result["endpoint"] = endpoint
    result["trace_sha256"] = hashlib.sha256((args.output / "observations.jsonl").read_bytes()).hexdigest()
    (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["result"] == "RUNTIME_COUNTERS_PASS" else 2


if __name__ == "__main__":
    raise SystemExit(main())
