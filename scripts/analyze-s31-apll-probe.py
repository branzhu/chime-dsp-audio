#!/usr/bin/env python3
"""Analyze standalone S31 APLL probe evidence. Offline only, no device writes.

A digital observation PASS is NOT analog waveform, DSP or end-to-end acceptance.
"""
import argparse
import json
from pathlib import Path

MASK = (1 << 31) - 1


def analyze(report):
    if report['magic'] != 0x41504C4C or report['version'] != 1:
        raise ValueError('unknown probe report')
    count = report['phases']
    if not 0 <= count <= len(report['phase']):
        raise ValueError('invalid phase count')
    rows = []
    base_frequency = None
    original = report['original_sdm']
    carry = (original & ~255) | 255
    targets = [original, original+1, original, original-1, original,
               carry, carry+1, carry, original, original-64, original+64,
               original, original-193, original+193, original]
    for p in report['phase'][:count]:
        elapsed = p['end_us'] - p['begin_us']
        if elapsed <= 0:
            raise ValueError('invalid elapsed time')
        clocks = (p['bclk_end'] - p['bclk_begin']) & MASK
        fs = clocks * 1e6 / elapsed / 64
        if base_frequency is None:
            base_frequency = fs
        # Relative ratio cancels the assumed XTAL frequency; fixed dividers.
        expected_relative = ((4 * 65536 + p['sdm']) / (4 * 65536 + original) - 1) * 1e6
        measured_relative = (fs / base_frequency - 1) * 1e6
        hist = {str(i): n for i, n in enumerate(p['pulse_hist']) if n}
        pulses = sum(p['pulse_hist'])
        ratio = pulses / (2 * clocks) if clocks else 0
        bins = [int(i) for i in hist]
        # Broad digital guard around nominal 13 x 12.5 ns half-cycles. Preserve
        # every observed bin; do not hide anomalies inside a summary PASS.
        pulse_guard = bool(bins) and min(bins) >= 10 and max(bins) <= 16
        errors = sum(p[k] for k in ['sequence_errors', 'word_errors', 'rx_errors',
                                    'tx_errors', 'rx_overflows', 'tx_overflows'])
        index = len(rows)
        planned_seconds = 10 if index < 5 else 5
        good = (index < len(targets) and p['id'] == index and
                p['sdm'] == targets[index] and
                planned_seconds <= elapsed / 1e6 < planned_seconds + 0.1 and
                p['rx_frames'] > 40000 * planned_seconds and
                not p['error'] and not errors and pulse_guard and
                not p['pulse_overflow'] and not p['pulse_last'] and
                .995 < ratio < 1.005 and abs(measured_relative - expected_relative) < 10 and
                p['mclk_before'] == p['mclk_after'] == report['original_ctrl'] and
                p['div_ctrl_before'] == p['div_ctrl_after'] == report['original_div'])
        rows.append({'id': p['id'], 'sdm_delta': p['sdm'] - original,
                     'elapsed_seconds': elapsed / 1e6, 'measured_fs_hz': fs,
                     'expected_relative_ppm': expected_relative,
                     'measured_relative_ppm': measured_relative,
                     'relative_error_ppm': measured_relative - expected_relative,
                     'valid_rx_frames': p['rx_frames'], 'error_count': errors,
                     'pulse_hist_ticks_12_5ns': hist,
                     'pulse_coverage_vs_bclk': ratio,
                     'sdm_write_duration_us': p['write_after_us'] - p['write_before_us'],
                     'digital_observation_pass': good})
    tail_errors = sum(report[k] for k in ['total_sequence_errors', 'total_word_errors',
                                           'total_rx_errors', 'total_tx_errors',
                                           'total_rx_overflows', 'total_tx_overflows',
                                           'pulse_overflow', 'pulse_last'])
    complete = (count == 15 and report['state'] == 3 and report['error'] == 0 and
                report['armed'] == 1 and report['setup_stage'] == 8 and
                report['writes'] == 15 and tail_errors == 0)
    safe = bool(report['restored'] and report['amps_low'] and report['dsp_reset_low'] and
                report['final_sdm'] == original and
                report['final_odiv'] == report['original_odiv'])
    return {'scope': 'ON_CHIP_DIGITAL_OBSERVATION_NOT_GLITCH_FREE_CERTIFICATION',
            'completed_15_phases': complete, 'safe_final_state': safe,
            'digital_observation_pass': complete and safe and all(r['digital_observation_pass'] for r in rows),
            'phases': rows,
            'not_proven': ['sub-12.5ns/analog pulse integrity at DSP pins',
                           'DSP ASRC tracking or audio output quality',
                           'USB/BT clock-recovery loop and load coexistence',
                           'other chips, temperatures, voltages or long-duration stability']}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('report', type=Path)
    a = p.parse_args()
    print(json.dumps(analyze(json.loads(a.report.read_text(encoding='utf-8'))), indent=2))
