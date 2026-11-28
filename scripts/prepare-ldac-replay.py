"""Offline continuous quiet WAV -> real LDAC packets + provenance receipt.
Uses an explicitly supplied host encoder, never plays audio or installs codecs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import wave


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--wav',type=Path,required=True)
    p.add_argument('--encoder',type=Path,required=True);p.add_argument('--encoder-source',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--quality',choices=('MQ','SQ','HQ'),default='MQ');a=p.parse_args()
    pin='e8ff0f96f26b84b47711c549e0d60baa425cd70e'
    rev=subprocess.check_output(['git','-C',str(a.encoder_source),'rev-parse','HEAD'],text=True).strip()
    dirty=subprocess.check_output(['git','-C',str(a.encoder_source),'status','--porcelain'],text=True)
    if rev!=pin or dirty:raise ValueError('require clean pinned Sony/AOSP host encoder source')
    a.output.mkdir(parents=True,exist_ok=False)
    raw=a.output/'input.s16le';packet=a.output/'probe.packets';peak=0
    with wave.open(str(a.wav),'rb') as w,raw.open('wb') as f:
        rate=w.getframerate();count=w.getnframes()
        if w.getsampwidth()!=2 or w.getnchannels()!=2 or rate not in (44100,48000) or count not in (rate*70,rate*610):raise ValueError('quiet PCM16 stereo 44.1/48k, exactly 70/610s required')
        while True:
            data=w.readframes(rate)
            if not data:break
            peak=max(peak,max(abs(v[0]) for v in struct.iter_unpack('<h',data))/32768)
            if peak>.09:raise ValueError('refusing non-quiet diagnostic signal')
            f.write(data)
    subprocess.run([str(a.encoder.resolve()),str(raw.resolve()),str(rate),str(packet.resolve()),a.quality],check=True)
    receipt=dict(schema=1,rate=rate,duration_s=count/rate,pcm_peak=peak,quality=a.quality,
        pcm_wav=str(a.wav.resolve()),pcm_sha256=sha(a.wav),raw_sha256=sha(raw),packets_sha256=sha(packet),
        encoder_commit=pin,encoder_sha256=sha(a.encoder),generator_sha256=sha(Path(__file__).parents[1]/'software/tests/bluetooth/encode_ldac_replay.c'),
        looped=False,played=False,rf_tested=False,encoder_license='Apache-2.0 test-only; not linked into firmware')
    (a.output/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print(json.dumps(receipt,indent=2))
if __name__=='__main__':main()
