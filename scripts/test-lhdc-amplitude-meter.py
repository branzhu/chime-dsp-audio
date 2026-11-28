"""Unit checks for the offline LHDC audit meter (no external dependencies)."""
import importlib.util,math,struct,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('audit',ROOT/'scripts/investigate-lhdc-amplitude.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
with tempfile.TemporaryDirectory(dir=ROOT/'tmp',prefix='lhdc-meter-') as d:
    path=Path(d)/'sine.pcm'
    for rate in [44100,48000,96000,192000]:
      for bits,fs in [(16,2**15),(24,2**23),(32,2**31),(32,2**27)]:
       for level in [-6,-20,-40]:
        payload=bytearray()
        for i in range(rate//5):
          for ch in range(2):
            v=round(fs*10**(level/20)*math.sin(2*math.pi*1000*i/rate+ch*.37))
            payload.extend(v.to_bytes(bits//8,'little',signed=True))
        path.write_bytes(payload);rows=audit.meter(path,bits,fs,rate,.01)
        for ch,row in enumerate(rows):
            assert row['channel']=='LR'[ch]
            assert abs(row['sine_amplitude_dbfs']-level)<.025,(bits,level,row)
            assert abs(row['rms_dbfs']-(level-3.01029995664))<.025
            assert row['sine_energy_fraction']>.99998
            assert row['rail_samples_total']==0
    # Nonzero right channel with zero left; a natural zero is not concealed PCM.
    path.write_bytes(b''.join(struct.pack('<hh',0,16384) for _ in range(4800)))
    left,right=audit.meter(path,16,2**15,48000,.01)
    assert left['peak_dbfs'] is None and left['rms_dbfs'] is None
    assert left['zero_samples_total']==4800 and right['zero_samples_total']==0
    assert abs(right['peak_dbfs']+6.02059991328)<1e-9
    # Signed endpoint accounting works for both int32 PCM and Q5.27 containers.
    for fs in [2**31,2**27]:
        path.write_bytes(b''.join(struct.pack('<ii',-fs,fs-1) for _ in range(4800)))
        for row in audit.meter(path,32,fs,48000,.01):assert row['rail_samples_total']==4800
print('PASS LHDC meter: 48 sine format/rate/level combinations, L/R separation, peak/RMS conventions, zero samples, signed full-scale endpoints')
