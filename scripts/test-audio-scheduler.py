"""Synthetic scheduler accounting tests; no hardware/performance claims."""
import copy
import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('scheduler',Path(__file__).with_name('summarize-audio-scheduler.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class Scheduler(unittest.TestCase):
    def rows(self):
        rows=[]
        for i in range(3):
            rows.append(dict(play_elapsed_s=5+i,host_monotonic_s=10+i,sample=dict(
                boot_id='boot',firmware=dict(elf_sha256='image'),bt=dict(epoch=1,error=0),
                scheduler=dict(enabled=True,counter_bits=32,sampled_us=(10+i)*1000000,capture_span_us=100,
                    tasks=[dict(name='source_pcm',priority=6,affinity=0,runtime_us=(i*250000+(1<<32)-100000)%(1<<32))]))))
        return rows
    def test_counter_wrap(self):
        r=m.report(self.rows());self.assertEqual(r['steady_seconds'],2);self.assertEqual(r['tasks']['source_pcm']['mean_core_percent'],25)
    def test_fault_not_steady(self):
        r=self.rows();r[-1]['sample']['bt']['error']=257
        self.assertEqual(m.report(r)['intervals'],1)
    def test_epoch_not_steady(self):
        r=self.rows();r[-1]['sample']['bt']['epoch']=2
        self.assertEqual(m.report(r)['intervals'],1)
    def test_identity_change_rejected(self):
        for field in ('boot_id','firmware'):
            r=self.rows();r[-1]['sample'][field]='changed'
            with self.assertRaises((ValueError,TypeError)):m.report(r)
    def test_counter_restart_rejected(self):
        r=self.rows();r[-1]['sample']['scheduler']['tasks'][0]['runtime_us']=0
        with self.assertRaises(ValueError):m.report(r)
    def test_affinity_change_rejected(self):
        r=self.rows();r[-1]['sample']['scheduler']['tasks'][0]['affinity']=1
        with self.assertRaises(ValueError):m.report(r)
    def test_network_response_jitter_reported_not_cpu_time(self):
        r=self.rows();r[1]['host_monotonic_s']+=.7
        x=m.report(r);self.assertAlmostEqual(x['max_host_esp_interval_difference_us'],700000);self.assertEqual(x['tasks']['source_pcm']['mean_core_percent'],25)
    def test_no_qualified_window(self):
        with self.assertRaises(ValueError):m.report(self.rows(),10)

if __name__=='__main__':unittest.main()

