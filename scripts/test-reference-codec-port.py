"""Determinism, complete provenance pins, and no output before rejection."""
from pathlib import Path
import tempfile,shutil,importlib.util,json,hashlib,sys
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[1]
component=root/"software/components/chime_codec_reference"
def load(path):
    spec=importlib.util.spec_from_file_location("reference_port_test",path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
with tempfile.TemporaryDirectory(prefix="chime-reference-port-") as temp:
    temp=Path(temp);copy=temp/"component"
    shutil.copytree(component,copy,ignore=shutil.ignore_patterns("__pycache__","*.pyc"))
    port=load(copy/"prepare.py");port.generate(temp/"one");port.generate(temp/"two")
    one=(temp/"one/CHIME_PORT.json").read_bytes()
    assert one==(temp/"two/CHIME_PORT.json").read_bytes(),"non-deterministic port"
    receipt=json.loads(one)
    for name,digest in receipt["files"].items():
        assert hashlib.sha256((temp/"one"/name).read_bytes()).hexdigest()==digest
    pins=json.loads((copy/"UPSTREAM.json").read_text(encoding="utf-8"))
    for name in pins["files"]:
        source=copy/"vendor"/name;data=source.read_bytes();source.write_bytes(data+b"tampered")
        try:
            try:port.generate(temp/"rejected")
            except RuntimeError as error:assert "Upstream codec source changed" in str(error)
            else:raise AssertionError("unreviewed file accepted: "+name)
            assert not (temp/"rejected").exists(),"wrote partial output before source rejection"
        finally:source.write_bytes(data)
    unpack=(temp/"one/ldac/unpack_ldac.o.c").read_text(encoding="utf-8")
    assert "p_ac->a_idsf[iqu] < 0 || p_ac->a_idsf[iqu] > 31" in unpack
    adapter=(temp/"one/dbt_lhdcv5_codec.c").read_text(encoding="utf-8")
    decoder=(temp/"one/lhdc/lhdc_dec.c").read_text(encoding="utf-8")
    for stage in ("configuration","singleton","workspace-size","state-allocation","decoder-init-or-entropy","pcm-scratch"):
        assert 'chime_reference_init_failed("lhdc-'+stage+'"' in adapter
    for stage in ("workspace","buffers","imdct","window","entropy"):
        assert 'chime_reference_init_failed("lhdc-'+stage+'"' in decoder
    common=(temp/"one/dbt_codec_lifecycle.c").read_text(encoding="utf-8")
    assert "dbt_codec_worker_step" not in common and "dbt_codec_open" in common
    assert "uint8_t dbt_a2dp_ldac_validate(" not in (temp/"one/dbt_ldac.c").read_text(encoding="utf-8")
    assert "uint8_t dbt_a2dp_lhdcv5_validate(" not in (temp/"one/dbt_lhdcv5_codec.c").read_text(encoding="utf-8")
print(f"PASS reference codec port: {len(pins['files'])} per-file tamper rejections before output, deterministic actual-byte receipt, signed-index guard and codec-only lifecycle")
