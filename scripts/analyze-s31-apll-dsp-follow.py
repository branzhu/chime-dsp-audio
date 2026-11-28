#!/usr/bin/env python3
"""Offline analysis of S31 SDM + DSP status polling; NOT output-audio acceptance."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('apll_input_probe', Path(__file__).with_name('analyze-s31-apll-probe.py'))
input_probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(input_probe)
EXPECTED_CONFIG = [1, 5, 0x700, 0, 2, 0, 2, 0x9000, 0x9000]


def analyze(report):
    if report['magic'] != 0x44535046 or report['version'] != 1:
        raise ValueError('not a DSP-follow report')
    # Identical input-clock/sequence fixture layout; normalize identity explicitly
    # for the common input-only checker, never mislabel it as DSP output capture.
    normalized = copy.deepcopy(report)
    normalized['magic'] = 0x41504c4c
    input_result = input_probe.analyze(normalized)
    rows = []
    for p in report['phase'][:report['phases']]:
        d = p['dsp']
        elapsed = (p['end_us'] - p['begin_us']) / 1e6
        n = d['settled_samples']
        mean = d['settled_ratio_sum'] / n if n else None
        failures = sum(d[k] for k in ['errors', 'pll_unlocked', 'asrc_unlocked', 'core_bad'])
        head = d['head'][:d['head_count']]
        coverage = (len(head) == d['head_count'] == 64 and d['head_count'] <= len(d['head']) and
                    0 < n <= d['samples'] and d['samples'] >= elapsed * 100 and
                    d['max_gap_us'] <= 10000 and d['max_span_us'] <= 5000 and
                    all(0 < s['span_us'] <= 5000 and s['pll'] & 1 and s['lock'] & 1 and
                        (s['core'] & 7) in (1, 3, 4) for s in head) and
                    all(0 <= a['offset_us'] < b['offset_us'] <= elapsed * 1e6
                        for a, b in zip(head, head[1:])))
        plausible = (mean is not None and 0xF00 <= d['settled_min'] <= mean <= d['settled_max'] <= 0x1100)
        rows.append({'id': p['id'], 'polls': d['samples'], 'settled_polls': n,
                     'sampled_failure_count': failures,
                     'max_poll_gap_us': d['max_gap_us'], 'max_read_span_us': d['max_span_us'],
                     'ratio_min': d['ratio_min'], 'ratio_max': d['ratio_max'],
                     'settled_ratio_mean': mean, 'settled_ratio_min': d['settled_min'],
                     'settled_ratio_max': d['settled_max'],
                     'poll_observation_pass': not failures and coverage and plausible})
    config_ok = (report['dsp_started'] == 1 and report['dsp_boot_error'] == 0 and
                 report['dsp_failed_reg'] == 0 and
                 report['dsp_config_before'] == report['dsp_config_after'] == EXPECTED_CONFIG)
    direction = False
    separation = None
    fit_ok = False
    if len(rows) == 15 and all(r['settled_ratio_mean'] is not None for r in rows):
        base = rows[0]['settled_ratio_mean']
        low, high = rows[12]['settled_ratio_mean'], rows[13]['settled_ratio_mean']
        separation = high - low
        direction = low < base < high and 1 <= separation <= 4
        fit_ok = True
        for r, p in zip(rows, input_result['phases']):
            measured_ratio_ppm = (r['settled_ratio_mean'] / base - 1) * 1e6
            residual = measured_ratio_ppm - p['measured_relative_ppm']
            r['coarse_ratio_relative_ppm'] = measured_ratio_ppm
            r['coarse_ratio_minus_bclk_ppm'] = residual
            fit_ok &= abs(residual) <= 1e6 / base  # one coarse ratio LSB
    observed = (input_result['digital_observation_pass'] and config_ok and
                all(r['poll_observation_pass'] for r in rows) and direction and fit_ok)
    return {'scope': 'INPUT_DIGITAL_PLUS_DSP_STATUS_POLLING_NOT_DSP_OUTPUT_ACCEPTANCE',
            'input_digital_observation_pass': input_result['digital_observation_pass'],
            'dsp_frozen_boot_and_config_pass': config_ok,
            'coarse_ratio_direction_pass': direction,
            'ratio_high_minus_low_lsb': separation,
            'coarse_ratio_fit_within_one_lsb': bool(fit_ok),
            'dsp_status_observation_pass': bool(observed),
            'phases': rows, 'input_analysis': input_result,
            'not_proven': ['unlock events shorter than/asynchronous to polling windows',
                           'DSP fixed-clock digital output or sample continuity',
                           'analog/sub-12.5ns waveform integrity at DSP pins',
                           'DSP protection/muting, audible quality, USB/BT closed loop']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(json.loads(args.report.read_text(encoding='utf-8'))), indent=2))
