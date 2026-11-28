"""Synthetic counter fixtures only; not live hardware evidence."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mix_check", ROOT / "scripts/check-audio-mix.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
SHA = "a" * 64


def sample(second, rate=48000):
    s = dict(schema=1, boot_id="0123456789abcdef", sampled_us=1000000 + second * 1000000, snapshot_span_us=100,
             firmware=dict(elf_sha256=SHA),
             source=dict(stopped=False, amps_enabled=True, dsp_ready=True, selected=3, rendering=3, sound_error=0),
             usb=dict(installed=True, streaming=True, wanted=True, coherent=True, suspended=False, muted=False, error=0, epoch=2),
             bt=dict(connected=True, playing=True, output_active=True, primed=True, error=0, codec=6, rate=rate, channels=2,
                     stream_state=2, epoch=3, output_id=4, queued_frames=4608),
             render={}, decoder=dict(valid=True, epoch=3))
    for path in m.Window.FAULTS:
        group, key = path.split("."); s[group][key] = 0
    for path, speed in (("usb.rx_bytes", 192000), ("usb.rx_packets", 8000), ("bt.pcm_frames", rate),
                        ("bt.rendered_frames", 48000), ("render.blocks", 375), ("decoder.packets", 100), ("decoder.ldac_pcm_frames", rate)):
        group, key = path.split("."); s[group][key] = second * speed + 100
    return s


class MixCheckTests(unittest.TestCase):
    def test_full_1min_and_10min_counter_windows(self):
        for seconds in (60, 600):
            for rate in (44100, 48000):
                w=m.Window(seconds,SHA)
                for i in range(seconds+1): w.add(sample(i,rate),i)
                r=w.result()
                self.assertEqual(r["result"],"RUNTIME_COUNTERS_PASS")
                self.assertFalse(r["audio_content_verified"])
                self.assertEqual(r["end_to_end_acceptance"],"NOT_PROVEN")

    def test_empty_short_and_missing_observations_do_not_pass(self):
        w=m.Window(60,SHA)
        with self.assertRaises(m.EvidenceError): w.result()
        for i in range(60): w.add(sample(i),i)
        with self.assertRaises(m.EvidenceError): w.result()
        with self.assertRaises(m.EvidenceError): w.add(sample(65),65)

    def test_wrong_firmware_no_stream_mute_and_stale_snapshot(self):
        for group,key,value in (("firmware","elf_sha256","b"*64),("usb","streaming",False),
                                ("usb","muted",True),("usb","coherent",False),("bt","connected",False),
                                ("bt","codec",1),("bt","rate",96000),("bt","primed",False),
                                ("source","amps_enabled",False),("source","rendering",0),("decoder","valid",False)):
            s=sample(0);s[group][key]=value
            with self.assertRaises(m.EvidenceError,msg=group+key):m.Window(60,SHA).add(s,0)

    def test_every_fault_increment_and_reset_rejected(self):
        for path in m.Window.FAULTS:
            w=m.Window(60,SHA);w.add(sample(0),0);s=sample(1)
            group,key=path.split(".");s[group][key]=1
            with self.assertRaises(m.EvidenceError,msg=path):w.add(s,1)
            w=m.Window(60,SHA);s0=sample(0);s0[group][key]=9;w.add(s0,0)
            with self.assertRaises(m.EvidenceError,msg=path):w.add(sample(1),1)

    def test_capacity_wait_is_not_a_fault_or_cpu_utilization(self):
        w=m.Window(60,SHA)
        for i in range(61):
            s=sample(i);s["decoder"].update(capacity_waits=i*10,capacity_wait_us=27000,
                                          max_capacity_wait_us=31000,slow_packets=i)
            w.add(s,i)
        self.assertEqual(w.result()["result"],"RUNTIME_COUNTERS_PASS")

    def test_old_firmware_without_capacity_timeout_field_fails_closed(self):
        s=sample(0);del s["decoder"]["capacity_timeouts"]
        with self.assertRaises(m.EvidenceError):m.Window(60,SHA).add(s,0)

    def test_every_progress_stall_and_reset_rejected(self):
        for path in m.Window.PROGRESS:
            for value in (100,0):
                w=m.Window(60,SHA);w.add(sample(0),0);s=sample(1)
                group,key=path.split(".");s[group][key]=value
                with self.assertRaises(m.EvidenceError,msg=path):w.add(s,1)

    def test_every_epoch_and_boot_change_rejected(self):
        for path in m.Window.IDENTITIES:
            w=m.Window(60,SHA);w.add(sample(0),0);s=sample(1)
            if "." in path:
                group,key=path.split(".");s[group][key]=s[group][key]+1 if type(s[group][key]) is int else "b"*64
            else:s[path]="fedcba9876543210"
            with self.assertRaises(m.EvidenceError,msg=path):w.add(s,1)

    def test_bool_nan_negative_and_missing_counters_rejected(self):
        for value in (True, float("nan"), float("inf"), -1):
            s=sample(0);s["usb"]["rx_bytes"]=value
            with self.assertRaises(m.EvidenceError):m.Window(60,SHA).add(s,0)
        s=sample(0);del s["decoder"]["valid"]
        with self.assertRaises(m.EvidenceError):m.Window(60,SHA).add(s,0)

    def test_host_device_times_must_agree_and_be_monotone(self):
        for host in (0,-1,2):
            w=m.Window(60,SHA);w.add(sample(0),0)
            with self.assertRaises(m.EvidenceError):w.add(sample(1),host)
        s=sample(0);s["snapshot_span_us"]=50001
        with self.assertRaises(m.EvidenceError):m.Window(60,SHA).add(s,0)

    def test_slow_stream_not_mistaken_for_stability(self):
        w=m.Window(60,SHA)
        for i in range(61):
            s=sample(i);s["usb"]["rx_bytes"]=100+i*100
            w.add(s,i)
        with self.assertRaises(m.EvidenceError):w.result()

    def test_observations_not_mutated_by_caller(self):
        w=m.Window(60,SHA);s=sample(0);w.add(s,0);s["bt"]["epoch"]=999
        self.assertEqual(w.samples[0][0]["bt"]["epoch"],3)

    def test_only_real_acceptance_durations(self):
        for duration in (0,1,59,3600):
            with self.assertRaises(m.EvidenceError):m.Window(duration,SHA)


if __name__ == "__main__": unittest.main()
