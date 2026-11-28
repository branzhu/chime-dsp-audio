"""Offline transport planner tests; no endpoints, playback, or RF used."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
spec=importlib.util.spec_from_file_location('client',Path(__file__).with_name('ldac_replay_client.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Planner(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.path=Path(self.temp.name)/'packets'
    def frame(self,rate=48000):return bytes((0xaa,0x32 if rate==48000 else 0x12,0))+bytes(129)
    def test_trim_and_account(self):
        self.path.write_bytes(struct.pack('<HB',132*15,15)+self.frame()*15)
        batches=m.packets_for_window(self.path,48000,.01) # ceil(480/128)=4
        self.assertEqual(len(batches),1);first,body=batches[0]
        self.assertEqual(first,0);self.assertEqual(struct.unpack_from('<IHB',body), (0,132*4,4));self.assertEqual(len(body),7+132*4)
    def test_long_sequence_and_bounds(self):
        record=struct.pack('<HB',132*5,5)+self.frame()*5;self.path.write_bytes(record*100)
        batches=m.packets_for_window(self.path,48000,1)
        expected=0
        for first,body in batches:
            self.assertEqual(first,expected);self.assertLessEqual(len(body),8192);off=4;count=0
            while off<len(body):
                size,nf=struct.unpack_from('<HB',body,off);expected+=nf*128;off+=3+size;count+=1
            self.assertEqual(off,len(body));self.assertLessEqual(count,8)
        self.assertEqual(expected,((48000+127)//128)*128)
    def test_reject_bad_inputs(self):
        valid=struct.pack('<HB',132,1)+self.frame()
        for bad in (b'',valid[:-1],valid+b'\0',struct.pack('<HB',132,2)+self.frame(),struct.pack('<HB',132,1)+self.frame(44100),struct.pack('<HB',0,1)):
            self.path.write_bytes(bad)
            with self.assertRaises(ValueError):m.packets_for_window(self.path,48000,.001)
    def test_no_source_looping(self):
        self.path.write_bytes(struct.pack('<HB',132,1)+self.frame())
        with self.assertRaisesRegex(ValueError,'shorter'):m.packets_for_window(self.path,48000,70)
    def test_ahead_budget_accounts_for_anchor_uncertainty(self):
        planner=object.__new__(m.LdacReplay)
        planner.packet_starts=list(range(0,768*80,768))
        planner.batch_ends=list(range(8,81,8))
        planner.batches=[(i*768,b'') for i in range(0,80,8)]
        self.assertEqual(planner.choose_lead(48000,0,0),.30)
        self.assertLessEqual(planner.choose_lead(48000,0,.15),.20)
        with self.assertRaises(ValueError):planner.choose_lead(48000,0,.25)
    def test_44100(self):
        self.path.write_bytes(struct.pack('<HB',132*4,4)+self.frame(44100)*4)
        batch=m.packets_for_window(self.path,44100,.01);self.assertEqual(batch[0][1][6],4)
if __name__=='__main__':unittest.main()
