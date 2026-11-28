#!/usr/bin/env python3
"""Offline synthetic negative tests, distinct from real board evidence."""
import importlib.util
import json
from pathlib import Path
import unittest


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


m = load('dsp_follow', 'analyze-s31-apll-dsp-follow.py')
base = load('input_fixture', 'test-s31-apll-probe.py')


def fixture():
    r = base.fixture()
    r.update(magic=0x44535046, dsp_started=1, dsp_boot_error=0, dsp_failed_reg=0,
             dsp_config_before=m.EXPECTED_CONFIG[:], dsp_config_after=m.EXPECTED_CONFIG[:])
    for p in r['phase']:
        ratio = round(4096 * (4*65536+p['sdm']) / (4*65536+r['original_sdm']))
        p['dsp'] = dict(samples=2000, errors=0, pll_unlocked=0, asrc_unlocked=0, core_bad=0,
                        max_gap_us=2000, max_span_us=800, ratio_min=ratio, ratio_max=ratio,
                        settled_samples=1800, settled_ratio_sum=ratio*1800,
                        settled_min=ratio, settled_max=ratio, head_count=64,
                        head=[dict(offset_us=1000*i+1000,span_us=700,pll=1,lock=1,ratio=ratio,core=3)
                              for i in range(64)])
    return r


class Tests(unittest.TestCase):
    def test_synthetic_good(self):
        self.assertTrue(m.analyze(fixture())['dsp_status_observation_pass'])

    def test_read_or_lock_errors(self):
        for key in ['errors','pll_unlocked','asrc_unlocked','core_bad']:
            r=fixture();r['phase'][2]['dsp'][key]=1
            self.assertFalse(m.analyze(r)['dsp_status_observation_pass'],key)

    def test_sparse_or_slow_polls(self):
        for key,value in [('max_gap_us',10001),('max_span_us',5001),('samples',1),('settled_samples',0)]:
            r=fixture();r['phase'][4]['dsp'][key]=value
            self.assertFalse(m.analyze(r)['dsp_status_observation_pass'],key)

    def test_no_ratio_response(self):
        r=fixture()
        for p in r['phase']:
            d=p['dsp'];d['settled_ratio_sum']=4096*d['settled_samples'];d['settled_min']=d['settled_max']=4096
        self.assertFalse(m.analyze(r)['dsp_status_observation_pass'])

    def test_changed_config(self):
        r=fixture();r['dsp_config_after'][2]=0x600
        self.assertFalse(m.analyze(r)['dsp_status_observation_pass'])

    def test_input_errors_not_hidden(self):
        r=fixture();r['phase'][6]['word_errors']=1
        self.assertFalse(m.analyze(r)['dsp_status_observation_pass'])

    def test_head_unlock(self):
        r=fixture();r['phase'][0]['dsp']['head'][3]['lock']=0
        self.assertFalse(m.analyze(r)['dsp_status_observation_pass'])

    def test_bad_head_coverage(self):
        for count in [-1, 0, 63, 65]:
            r=fixture();r['phase'][0]['dsp']['head_count']=count
            self.assertFalse(m.analyze(r)['dsp_status_observation_pass'])
        r=fixture();r['phase'][0]['dsp']['head'][2]['offset_us']=0
        self.assertFalse(m.analyze(r)['dsp_status_observation_pass'])

    def test_unsafe(self):
        r=fixture();r['amps_low']=0
        self.assertFalse(m.analyze(r)['dsp_status_observation_pass'])

    def test_archived_board_reports(self):
        evidence = Path(__file__).resolve().parent.parent / 'docs/evidence/s31-apll-dsp-follow-2026-11-28'
        for run in ['run01', 'run02']:
            r=json.loads((evidence/(run+'-report.json')).read_text(encoding='utf-8'))
            self.assertTrue(m.analyze(r)['dsp_status_observation_pass'],run)

    def test_identity(self):
        r=fixture();r['magic']=0x41504c4c
        with self.assertRaises(ValueError):m.analyze(r)


if __name__ == '__main__':
    unittest.main()
