"""Verify deterministic/fail-closed SDK generation; emit actual AVDTP hook for C tests.
Usage: python -X utf8 scripts/test-bluetooth-sdk-patch.py <IDF_PATH>
No SDK files are modified. Fixtures are copies inside workspace tmp/.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile

BTA_DELAY_HASH = 'dab853ead6c6fb0fd9c024719b0edbf243456d7d9bbb0ea1ae3dd3b890927b60'
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('vendor_patch', ROOT/'scripts/patch-bluetooth-vendor.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
sdk = Path(sys.argv[1])/'components/bt'
original = {n:(sdk/'host/bluedroid'/n).read_bytes() for n in patch.PINS}
with tempfile.TemporaryDirectory(dir=ROOT/'tmp', prefix='bt-sdk-test-') as t:
    base=Path(t); fixture=base/'bt'; out=base/'out'
    for n,b in original.items():
        p=fixture/'host/bluedroid'/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
    patch.generate(fixture,out)
    result={p.name:p.read_bytes() for p in out.iterdir()}
    patch.generate(fixture,out)
    assert result=={p.name:p.read_bytes() for p in out.iterdir()}
    delay_out=base/'delay'
    patch.generate_delay(fixture,delay_out)
    delay_result=(delay_out/'btc_av.c').read_bytes()
    patch.generate_delay(fixture,delay_out)
    assert delay_result==(delay_out/'btc_av.c').read_bytes()
    assert b'chime_bt_vendor.h' not in delay_result
    assert b'if (delay_value == 0)' in delay_result
    for n,b in original.items():
        p=fixture/'host/bluedroid'/n;p.write_bytes(b+b'\n/* SDK changed */\n')
        try: patch.generate(fixture,out)
        except ValueError: pass
        else: raise AssertionError('tampered input accepted: '+n)
        assert result=={p.name:p.read_bytes() for p in out.iterdir()}
        if n.endswith('/btc_av.c'):
            try:patch.generate_delay(fixture,delay_out)
            except ValueError:pass
            else:raise AssertionError('delay-only accepted tamper')
            assert delay_result==(delay_out/'btc_av.c').read_bytes()
        p.write_bytes(b)
    assert original=={n:(sdk/'host/bluedroid'/n).read_bytes() for n in original}
    for n,b in json.loads((ROOT/'software/components/chime_freeaptx/UPSTREAM.json').read_text())['files'].items():
        assert hashlib.sha256((ROOT/'software/components/chime_freeaptx'/n).read_bytes()).hexdigest()==b
    # Extract from generated source with the SAME function scanner; execute this
    # function itself with small AVDTP/allocator fakes, not a rewritten algorithm.
    captured=[]
    patch.function_edit(result['avdt_scb_act.c'].decode(),'avdt_scb_hdl_pkt_no_frag',lambda s:captured.append(s) or s)
    destination=ROOT/'tmp/bluetooth-tests/avdt-patched.inc'
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(captured[0],encoding='utf-8')
    for name in ['bta_av_co_audio_sink_supports_config','bta_av_co_audio_peer_src_supports_codec']:
        captured=[]
        patch.function_edit(result['bta_av_co.c'].decode(),name,lambda s:captured.append(s) or s)
        (destination.parent/(name+'.inc')).write_text(captured[0],encoding='utf-8')
    for mode,source in [('vendor',result['btc_av.c']),('recovery',delay_result)]:
        captured=[]
        patch.function_edit(source.decode(),'btc_a2d_sink_set_delay_value',lambda s:captured.append(s) or s)
        (destination.parent/('bt-set-delay-'+mode+'.inc')).write_text(captured[0],encoding='utf-8')
    bta=(sdk/'host/bluedroid/bta/av/bta_av_aact.c').read_bytes()
    # Pin actual runtime-dispatch function too; fail closed if it changes.
    assert hashlib.sha256(bta).hexdigest()==BTA_DELAY_HASH
    captured=[]
    patch.function_edit(bta.decode(),'bta_av_set_delay_value',lambda s:captured.append(s) or s)
    (destination.parent/'bt-send-delay.inc').write_text(captured[0],encoding='utf-8')
print('PASS SDK generator: deterministic; every pin tamper rejects before output; original SDK unchanged; emitted actual AVDTP function')
