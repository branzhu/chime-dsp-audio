"""Synthetic fixtures only: these tests never prove board or acoustic acceptance."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path
import tempfile
from unittest.mock import patch
import unittest
import wave
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("content",ROOT/"scripts/check-audio-content.py")
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


def measurement(seconds, phase=.02, ppm=0, level=.001, noise=1e-8):
    n=round(seconds/m.WINDOW_S);t=(np.arange(n)+.5)*m.WINDOW_S
    energy=np.empty((n,4))
    for i,code in enumerate(m.CODES.values()):
        bit=code[np.floor((t*(1+ppm/1e6)+phase)/m.SYMBOL_S).astype(int)%len(code)]
        energy[:,i*2]=level*np.where(bit,1,1/9)
        energy[:,i*2+1]=level*np.where(bit,1/9,1)
    return dict(energy=energy,noise=np.full((n,4),noise),rms=np.sum(energy,axis=1),
                peak=.1,rate=48000,channel=0,windows=n)


def background(level=1e-8):
    b=measurement(5);b["energy"][:]=level;b["noise"][:]=level;b["rms"][:]=level*4
    return b


class ContentTests(unittest.TestCase):
    def test_two_markers_and_drift_fit_1min_10min(self):
        for seconds,ppm in ((60,-1750),(600,1850)):
            r=m.evaluate(measurement(seconds,phase=1.72,ppm=ppm),background(),background())
            self.assertEqual(r["result"],"CONTENT_PROBES_PASS")
            self.assertFalse(r["sub_window_gaps_verified"])
            self.assertFalse(r["sample_clock_continuity_verified"])
            self.assertEqual(r["end_to_end_acceptance"],"NOT_PROVEN")

    def test_room_equalization_offset_not_a_false_failure(self):
        a=measurement(10);a["energy"][:,0]*=.1;a["energy"][:,3]*=2
        self.assertEqual(m.evaluate(a,background(),background())["result"],"CONTENT_PROBES_PASS")

    def test_one_source_missing_low_snr_inconclusive(self):
        a=measurement(10);a["energy"][:,2:]=1e-8
        r=m.evaluate(a,background(),background())
        self.assertEqual(r["result"],"INCONCLUSIVE")
        self.assertNotEqual(r["sources"]["ldac"]["result"],"CONTENT_PROBE_PASS")

    def test_background_fan_change_active_noise_and_clip_inconclusive(self):
        for case in ("fan","active_noise","clip"):
            a=measurement(10);b=background();c=background()
            if case=="fan":c=background(1e-5)
            if case=="active_noise":a["noise"][40,:]=.0001
            if case=="clip":a["peak"]=.99
            self.assertEqual(m.evaluate(a,b,c)["result"],"INCONCLUSIVE",case)

    def test_invalid_capture_cannot_be_used_to_diagnose_system_dropout(self):
        a=measurement(10);a["peak"]=1;a["energy"][40,:]=1e-8
        self.assertEqual(m.evaluate(a,background(),background())["result"],"INCONCLUSIVE")

    def test_one_masked_carrier_and_all_low_snr_are_inconclusive(self):
        for masked in (True,False):
            a=measurement(10)
            if masked:a["energy"][:,0]=1e-8
            else:a["energy"][:]*=1e-4
            self.assertEqual(m.evaluate(a,background(),background())["result"],"INCONCLUSIVE")

    def test_hundred_ms_source_gap_rejected(self):
        for source in range(2):
            a=measurement(10);a["energy"][43,source*2:source*2+2]=1e-8
            r=m.evaluate(a,background(),background())
            self.assertEqual(r["result"],"NOT_PASSED")
            self.assertEqual(list(r["sources"].values())[source]["gap_windows"],1)

    def test_large_level_dip_rejected_even_when_remaining_signal_has_good_snr(self):
        a=measurement(10,noise=1e-12);a["energy"][43,0:2]/=100
        b=background(1e-12)
        self.assertEqual(m.evaluate(a,b,b)["result"],"NOT_PASSED")

    def test_static_tones_not_accepted_as_known_source_codes(self):
        a=measurement(10);a["energy"][:]=.001
        self.assertEqual(m.evaluate(a,background(),background())["result"],"NOT_PASSED")

    def test_local_phase_slip_cannot_hide_in_long_average(self):
        a=measurement(60);a["energy"][250:300,:]=np.roll(a["energy"][250:300,:],10,axis=0)
        self.assertEqual(m.evaluate(a,background(),background())["result"],"NOT_PASSED")

    def test_invalid_measurements_and_short_background(self):
        for value in (float("nan"),float("inf"),-1):
            a=measurement(10);a["energy"][0,0]=value
            with self.assertRaises(m.EvidenceError):m.evaluate(a,background(),background())
        a=measurement(1)
        with self.assertRaises(m.EvidenceError):m.evaluate(a,background(),background())
        a=measurement(10);a["channel"]=1
        with self.assertRaises(m.EvidenceError):m.evaluate(a,background(),background())

    def test_pcm_decoding_and_explicit_channel(self):
        self.assertTrue(np.allclose(m.decode_pcm(bytes.fromhex("0000008000000000ffffff7f"),4,1,0),[-1,0,1-2**-31],atol=1e-7))
        self.assertTrue(np.allclose(m.decode_pcm(bytes.fromhex("000000000080ffff7f"),3,1,0),[0,-1,1-2**-23]))
        x=np.array([[-32768,32767],[1,2]],dtype="<i2")
        self.assertTrue(np.allclose(m.decode_pcm(x.tobytes(),2,2,1),[32767/32768,2/32768]))
        with self.assertRaises(m.EvidenceError):m.decode_pcm(x.tobytes(),2,2,2)

    def test_actual_wav_dsp_path_with_noise_and_unequal_source_gains(self):
        rng=np.random.default_rng(54)
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp)
            def write(name,seconds,active=False):
                p=tmp/name
                with wave.open(str(p),"wb") as w:
                    w.setnchannels(1);w.setsampwidth(2);w.setframerate(48000)
                    for second in range(seconds):
                        t=second+np.arange(48000)/48000
                        x=rng.normal(0,2e-5,48000)
                        if active:x+=.5*m.marker("usb",t+.137)+.23*m.marker("ldac",t+.821)
                        w.writeframes(np.rint(x*32767).astype("<i2").tobytes())
                return p
            a=write("active.wav",10,True);b=write("before.wav",5);c=write("after.wav",5)
            r=m.evaluate(m.inspect_segment(a,0,10,0),m.inspect_segment(b,0,5,0),m.inspect_segment(c,0,5,0))
            self.assertEqual(r["result"],"CONTENT_PROBES_PASS",r)
            with self.assertRaises(m.EvidenceError):m.inspect_segment(a,0,11,0)
            with self.assertRaises(m.EvidenceError):m.inspect_segment(a,-1,1,0)
            with self.assertRaises(m.EvidenceError):m.inspect_segment(a,0,.15,0)

    def test_cli_real_wav_path_retains_evidence_without_end_to_end_claim(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp);rng=np.random.default_rng(28)
            for name,seconds in (("before",6),("active",60),("after",7)):
                with wave.open(str(tmp/(name+".wav")),"wb") as w:
                    w.setnchannels(1);w.setsampwidth(2);w.setframerate(48000)
                    for second in range(seconds):
                        x=rng.normal(0,1e-5,48000);t=second+np.arange(48000)/48000
                        if name=="active":x+=.3*m.marker("usb",t+.123)+.4*m.marker("ldac",t+.672)
                        w.writeframes(np.rint(x*32767).astype("<i2").tobytes())
            output=tmp/"result.json"
            args=[sys.executable,str(ROOT/"scripts/check-audio-content.py"),"analyze",
                  "--active",str(tmp/"active.wav"),"--before",str(tmp/"before.wav"),
                  "--after",str(tmp/"after.wav"),"--start","0","--channel","0",
                  "--duration","60","--output",str(output)]
            run=subprocess.run(args,capture_output=True,text=True,timeout=60)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            result=json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(result["result"],"CONTENT_PROBES_PASS")
            self.assertEqual(result["before_background_s"],6)
            self.assertEqual(result["after_background_s"],7)
            self.assertFalse(result["physical_source_assignment_verified"])
            self.assertFalse(result["continuous_audio_verified"])
            self.assertEqual(result["end_to_end_acceptance"],"NOT_PROVEN")
            self.assertEqual(len(result["wav_sha256"]),3)
            original=output.read_bytes();again=subprocess.run(args,capture_output=True,timeout=60)
            self.assertNotEqual(again.returncode,0);self.assertEqual(output.read_bytes(),original)

    def test_saved_capture_validation_and_file_mutation_fail_closed(self):
        with patch.object(m,"inspect_segment",return_value=measurement(60)), \
             patch.object(m.wave,"open") as opened, \
             patch.object(m,"sha256",side_effect=["a","b","c","d","b","c"]):
            opened.return_value.__enter__.return_value.getnframes.return_value=240000
            opened.return_value.__enter__.return_value.getframerate.return_value=48000
            # Keep this test focused on file evidence, not marker fitting.
            with patch.object(m,"evaluate",return_value={}):
                with self.assertRaisesRegex(m.EvidenceError,"changed during analysis"):
                    m.analyze("a","b","c",0,60,0)
        for duration in (10,59,3600):
            with self.assertRaises(m.EvidenceError):m.analyze("a","b","c",0,duration,0)

    def test_generation_levels_and_chunks_are_reproducible(self):
        for source in m.FREQUENCIES:
            t=np.arange(48000)/48000;x=m.marker(source,t)
            self.assertLess(float(np.max(np.abs(x))),.09)
            self.assertTrue(np.array_equal(x,np.concatenate([m.marker(source,t[:20000]),m.marker(source,t[20000:])])))
            # Source files deliberately duplicate to L/R; no stereo/subwoofer
            # routing or volume/room correction certification implied.


if __name__=="__main__":unittest.main()
