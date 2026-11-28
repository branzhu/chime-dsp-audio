#!/usr/bin/env python3
"""Host-only analyzer guard tests; fabricated inputs are NOT board evidence."""
import json
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('probe_analyzer', Path(__file__).with_name('analyze-s31-apll-probe.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def fixture():
    base = 382101
    phases = []
    for i, delta in enumerate([0, 1, 0, -1, 0, 106, 107, 106, 0, -64, 64, 0, -193, 193, 0]):
        seconds = 10 if i < 5 else 5
        bclk = round(3_072_000 * seconds * (4 * 65536 + base + delta) / (4 * 65536 + base))
        hist = [0] * 64
        hist[13] = bclk * 2
        phases.append(dict(id=i,sdm=base+delta,begin_us=0,end_us=seconds*1_000_000,
                           bclk_begin=0,bclk_end=bclk,pulse_hist=hist,pulse_overflow=0,pulse_last=0,
                           error=0,rx_frames=48000*seconds,sequence_errors=0,word_errors=0,
                           rx_errors=0,tx_errors=0,rx_overflows=0,tx_overflows=0,
                           mclk_before=17,mclk_after=17,div_ctrl_before=256,div_ctrl_after=256,
                           write_before_us=0,write_after_us=0))
    return dict(magic=0x41504c4c,version=1,phases=15,original_sdm=base,phase=phases,
                state=3,error=0,restored=1,amps_low=1,dsp_reset_low=1,
                original_ctrl=17,original_div=256,final_sdm=base,original_odiv=2,final_odiv=2,
                armed=1,setup_stage=8,writes=15,total_sequence_errors=0,total_word_errors=0,
                total_rx_errors=0,total_tx_errors=0,total_rx_overflows=0,total_tx_overflows=0,
                pulse_overflow=0,pulse_last=0)


class AnalyzerTests(unittest.TestCase):
    def test_fabricated_good(self):
        self.assertTrue(m.analyze(fixture())['digital_observation_pass'])

    def test_fail_closed_error_cases(self):
        for field in ['sequence_errors','word_errors','rx_errors','tx_errors','rx_overflows','tx_overflows','pulse_last','pulse_overflow']:
            r=fixture();r['phase'][3][field]=1
            self.assertFalse(m.analyze(r)['digital_observation_pass'],field)

    def test_short_pulse(self):
        r=fixture();r['phase'][5]['pulse_hist'][2]=1
        self.assertFalse(m.analyze(r)['digital_observation_pass'])

    def test_missing_capture(self):
        r=fixture();r['phase'][5]['pulse_hist']=[0]*64
        self.assertFalse(m.analyze(r)['digital_observation_pass'])

    def test_no_frequency_response(self):
        r=fixture();r['phase'][13]['bclk_end']=r['phase'][0]['bclk_end']
        self.assertFalse(m.analyze(r)['digital_observation_pass'])

    def test_changed_divider(self):
        r=fixture();r['phase'][5]['mclk_after']+=1
        self.assertFalse(m.analyze(r)['digital_observation_pass'])

    def test_incomplete_or_unsafe(self):
        for field in ['restored','amps_low','dsp_reset_low']:
            r=fixture();r[field]=0
            self.assertFalse(m.analyze(r)['digital_observation_pass'])
        r=fixture();r['phases']=0
        self.assertFalse(m.analyze(r)['digital_observation_pass'])
        r=fixture();r['state']=2
        self.assertFalse(m.analyze(r)['digital_observation_pass'])

    def test_no_valid_frames(self):
        r=fixture();r['phase'][0]['rx_frames']=0
        self.assertFalse(m.analyze(r)['digital_observation_pass'])

    def test_changed_between_phases(self):
        r=fixture();r['phase'][3]['mclk_before']=r['phase'][3]['mclk_after']=99
        self.assertFalse(m.analyze(r)['digital_observation_pass'])

    def test_wrong_order_or_restore(self):
        r=fixture();r['phase'][2]['id']=1
        self.assertFalse(m.analyze(r)['digital_observation_pass'])
        r=fixture();r['final_sdm']+=1
        self.assertFalse(m.analyze(r)['digital_observation_pass'])

    def test_tail_errors(self):
        r=fixture();r['total_word_errors']=1
        self.assertFalse(m.analyze(r)['digital_observation_pass'])

    def test_archived_board_reports(self):
        evidence = Path(__file__).resolve().parent.parent / 'docs/evidence/s31-apll-hot-trim-2026-11-28'
        for run in ['run04', 'run05']:
            r = json.loads((evidence / (run + '-report.json')).read_text(encoding='utf-8'))
            self.assertTrue(m.analyze(r)['digital_observation_pass'], run)

    def test_bad_identity(self):
        r=fixture();r['magic']=0
        with self.assertRaises(ValueError):m.analyze(r)


if __name__ == '__main__':
    unittest.main()
