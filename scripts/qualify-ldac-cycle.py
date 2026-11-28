"""Qualify a 48kHz/31s periodic LDAC fixture before any looping is allowed.
Offline only. Compares encoded periods AND real decoder output across seams.
No device access, playback, gain changes, or RF acceptance.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import numpy as np


def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def split_frames(data):
    frames=[];off=0
    while off<len(data):
        if len(data)-off<3:raise ValueError('truncated record')
        size,nf=struct.unpack_from('<HB',data,off);off+=3;end=off+size
        if not 1<=nf<=15 or end>len(data):raise ValueError('record bounds')
        for _ in range(nf):
            if end-off<3:raise ValueError('short frame')
            a,b,c=data[off:off+3];n=((b&7)<<6)+(c>>2)+4
            if a!=0xaa or b>>5!=1 or (b>>3)&3!=2 or n>515 or n<4 or off+n>end:raise ValueError('not complete 48k stereo LDAC')
            frames.append(data[off:off+n]);off+=n
        if off!=end:raise ValueError('packet tail')
    return frames

def records(frames):
    out=bytearray()
    for i in range(0,len(frames),3):
        group=frames[i:i+3];payload=b''.join(group)
        out+=struct.pack('<HB',len(payload),len(group))+payload
    return bytes(out)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--encoder',type=Path,required=True);p.add_argument('--decoder',type=Path,required=True)
    p.add_argument('--encoder-source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--quality',choices=('MQ','SQ','HQ'),default='MQ');a=p.parse_args()
    pin='e8ff0f96f26b84b47711c549e0d60baa425cd70e'
    if subprocess.check_output(['git','-C',str(a.encoder_source),'rev-parse','HEAD'],text=True).strip()!=pin or subprocess.check_output(['git','-C',str(a.encoder_source),'status','--porcelain'],text=True):raise ValueError('clean pinned Sony/AOSP source required')
    a.output.mkdir(parents=True,exist_ok=False)
    spec=importlib.util.spec_from_file_location('content',Path(__file__).with_name('check-audio-content.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    rate=48000;seconds=31;cycle_samples=rate*seconds;cycle_frames=cycle_samples//128
    assert cycle_samples%128==0 and seconds/(len(m.CODES['ldac'])*m.SYMBOL_S)==4
    t=np.arange(cycle_samples)/rate;x=np.rint(m.marker('ldac',t)*32767).astype('<i2')
    pcm=np.column_stack((x,x)).tobytes();assert np.max(np.abs(x.astype(np.int32)))/32768<=.09
    source=a.output/'four-periods.s16le';source.write_bytes(pcm*4)
    encoded=a.output/'continuous.packets'
    subprocess.run([str(a.encoder.resolve()),str(source.resolve()),str(rate),str(encoded.resolve()),a.quality],check=True)
    frames=split_frames(encoded.read_bytes());assert len(frames)>=cycle_frames*4
    period=frames[cycle_frames:cycle_frames*2]
    assert period==frames[cycle_frames*2:cycle_frames*3]==frames[cycle_frames*3:cycle_frames*4], 'encoder periods not identical; looping NOT qualified'
    payload=records(period);(a.output/'cycle.packets').write_bytes(payload)
    replay=a.output/'three-cycles.packets';replay.write_bytes(records(period*3))
    continuous_pcm=a.output/'continuous-decoded.s16le';replay_pcm=a.output/'replay-decoded.s16le'
    for path,out in ((encoded,continuous_pcm),(replay,replay_pcm)):
        subprocess.run([str(a.decoder.resolve()),str(path.resolve()),str(rate),str(out.resolve())],check=True)
    reference=np.fromfile(continuous_pcm,dtype='<i2').reshape(-1,2);decoded=np.fromfile(replay_pcm,dtype='<i2').reshape(-1,2)
    assert len(decoded)==cycle_samples*3
    # Fresh decoder needs overlap warmup once only. Every subsequent seam and
    # every sample of periods 2 and 3 must equal continuous decoding exactly.
    assert np.array_equal(decoded[512:cycle_samples],reference[cycle_samples+512:cycle_samples*2])
    assert np.array_equal(decoded[cycle_samples:],reference[cycle_samples*2:cycle_samples*4])
    peak=float(np.max(np.abs(decoded.astype(np.int32)))/32768);assert peak<=.09
    digest=hashlib.sha256(payload).digest()
    header=struct.pack('<8sIIIIII32s',b'LDACRP01',rate,cycle_samples,len(period)//3,len(payload),1,0,digest)
    assert len(header)==64
    container=a.output/'flash-fixture.bin';container.write_bytes(header+payload)
    receipt=dict(schema=1,kind='qualified_periodic_ldac',rate=rate,cycle_s=seconds,cycle_input_frames=cycle_samples,cycle_encoded_frames=len(period),packet_encoded_frames=3,
        packets=len(period)//3,payload_bytes=len(payload),quality=a.quality,pcm_peak=peak,encoder_commit=pin,
        encoder_sha256=sha(a.encoder),decoder_sha256=sha(a.decoder),generator_sha256=sha(__file__),
        encoded_periods_compared=3,encoded_periods_bit_identical=True,
        decoder_comparison='bit-exact after 512-sample initial warmup; all samples including both subsequent seams',
        decoder_periods_compared=3,decoded_sha256=sha(replay_pcm),payload_sha256=digest.hex(),container_sha256=sha(container),
        rf_tested=False,device_tested=False,physical_source_assignment_verified=False)
    (a.output/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print(json.dumps(receipt,indent=2))
if __name__=='__main__':main()
