#!/usr/bin/env python3
"""Host-only frame arithmetic for S31 HW SRC mixing; NOT a hardware emulator.

No board access, firmware configuration changes, or third-party dependencies.
The ideal rational accumulator is a conservation oracle, NOT measured HW output.
"""
import json
from fractions import Fraction


def ideal_stream(lengths, up=160, down=147):
    remainder = 0
    counts = []
    for frames in lengths:
        count, remainder = divmod(frames * up + remainder, down)
        counts.append(count)
    return counts, remainder


def analyze():
    batches = []
    for frames in (128, 256, 512):
        # 147 blocks make an integer total for every tested input block size.
        lengths = [frames] * 147
        counts, remainder = ideal_stream(lengths)
        exact = frames * 160
        naive = (frames * 160 // 147) * 147
        assert remainder == 0 and sum(counts) == exact
        # Query model in esp_asrc 1.1.0 HW driver, before allocation alignment.
        capacity = frames * 160 // 147 + 16
        assert capacity >= max(counts)
        batches.append({
            "input_frames": frames,
            "input_period_ms": float(Fraction(frames * 1000, 44100)),
            "calls_per_second": float(Fraction(44100, frames)),
            "ideal_output_counts": sorted(set(counts)),
            "driver_query_capacity_frames": capacity,
            "input_pcm16_stereo_bytes": frames * 4,
            "output_capacity_bytes_before_alignment": capacity * 4,
            "ideal_total_after_147_calls": exact,
            "naive_per_call_floor_lost_frames": exact - naive,
        })
    # Different partitions must conserve the same rational total; this does not
    # assert that the real peripheral satisfies this property (Gate H must test).
    mixed = [128, 256, 512, 147, 1] * 1000
    counts, rem = ideal_stream(mixed)
    whole, whole_rem = ideal_stream([sum(mixed)])
    assert (sum(counts), rem) == (whole[0], whole_rem)

    drift = []
    # Conversion maps source nominal time to output nominal time. It does NOT
    # remove the source oscillator's fractional rate error.
    for source_rate in (44100, 48000):
        for ppm in (-300, -100, 100, 300, 2000):
            source_actual = Fraction(source_rate) * (1 + Fraction(ppm, 1000000))
            converted_actual = source_actual * Fraction(48000, source_rate)
            fixed_clock_error = converted_actual - 48000
            matched_clock_error = converted_actual - Fraction(48000) * (1 + Fraction(ppm, 1000000))
            assert matched_clock_error == 0
            drift.append({
                "source_nominal_rate": source_rate,
                "source_error_ppm": ppm,
                "fixed_i2s_queue_delta_frames_per_second": float(fixed_clock_error),
                "fixed_i2s_queue_delta_frames_in_10_minutes": float(fixed_clock_error * 600),
                "ideal_matched_i2s_queue_delta": float(matched_clock_error),
            })
    return {
        "kind": "HOST_ARITHMETIC_NOT_HARDWARE_MEASUREMENT",
        "component_version": "esp_asrc 1.1.0",
        "component_commit": "71418e93ac42f1aa5778ef83212194df89c4b2bb",
        "ideal_partition_conservation": "PASS",
        "ideal_clock_matching": "PASS",
        "batches": batches,
        "drift": drift,
        "not_modeled": [
            "HW residual/EOF semantics and startup/tail delay",
            "APLL/divider update continuity, jitter and slew",
            "USB host feedback dynamics and BT packet jitter",
            "DMA resource/contention/timeout/cancellation and CPU cost",
            "audio quality and DSP lock/mute behavior",
        ],
    }


if __name__ == "__main__":
    print(json.dumps(analyze(), indent=2))
