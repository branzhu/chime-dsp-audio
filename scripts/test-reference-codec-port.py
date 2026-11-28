"""Deterministic S31 generation, immutable source pins and defensive port sites."""
from pathlib import Path
import tempfile,shutil,importlib.util,json,hashlib,sys
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[1];component=root/'software/components/chime_codec_reference'
def load(path):
    spec=importlib.util.spec_from_file_location('s31_port_test',path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
with tempfile.TemporaryDirectory(prefix='chime-s31-port-') as temp:
    temp=Path(temp);copy=temp/'component';shutil.copytree(component,copy,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
    port=load(copy/'prepare.py');port.generate(temp/'one');port.generate(temp/'two')
    one=(temp/'one/CHIME_PORT.json').read_bytes();assert one==(temp/'two/CHIME_PORT.json').read_bytes()
    receipt=json.loads(one)
    for name,digest in receipt['files'].items():assert hashlib.sha256((temp/'one'/name).read_bytes()).hexdigest()==digest
    pins=json.loads((copy/'UPSTREAM.json').read_text(encoding='utf-8'))
    assert pins['repository']=='https://github.com/WillyBilly06/ESP32-S31-A2DP-Codecs'
    for name in pins['files']:
        source=copy/'vendor'/name;data=source.read_bytes();source.write_bytes(data+b'tampered')
        try:
            try:port.generate(temp/'rejected')
            except RuntimeError as e:assert 'Upstream codec source changed' in str(e)
            else:raise AssertionError('Unreviewed source accepted: '+name)
            assert not (temp/'rejected').exists()
        finally:source.write_bytes(data)
    unpack=(temp/'one/external/libldac-dec/src/unpack_ldac.o.c').read_text(encoding='utf-8')
    assert 'p_ac->a_idsf[iqu] < 0 || p_ac->a_idsf[iqu] > 31' in unpack and 's31_ldac_bit_limit' in unpack
    assert 's31_ldac_bit_limit=24' in (temp/'one/external/libldac-dec/src/ldaclib_api.o.c').read_text(encoding='utf-8')
    for unit in ('ldacBT.c','ldaclib.c'):
        assert '#define calloc chime_ldac_fast_calloc' in (temp/'one/external/libldac-dec/src'/unit).read_text(encoding='utf-8')
    lhdc=(temp/'one/external/lhdc-v5/lhdc_imdct.c').read_text(encoding='utf-8')
    assert 'static float tin[' not in lhdc and 'static float tout[' not in lhdc
    for suffix in ('960','1920'):
        assert f's{suffix}_ok=0;return;' in lhdc
    assert 'data_len * 8 - frame_bit_offset' in (temp/'one/stack/a2dp/latm_parser.c').read_text(encoding='utf-8')
print(f"PASS S31 codec port: {len(pins['files'])} per-file tamper rejections before output; deterministic final-byte receipt; bounded LDAC/LATM and init-OOM guards")
