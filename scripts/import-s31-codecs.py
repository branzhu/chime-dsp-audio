"""Import only codec libraries from a local checkout of the selected S31 repo.
Run with a fetched checkout; no SDK install, transport or application is imported.
"""
import argparse, hashlib, io, json, subprocess, tarfile
from pathlib import Path
COMMIT = '71071a783811799d2977e4e87f733acdc2220c29'
PREFIX = 'esp-idf-v6.1-codecs/components/bt/host/bluedroid/'
LIBS = ['external/'+n for n in ('sbc/decoder','libaac-lc','libfreeaptx','libldac-dec','lhdc-v5','opus','opus_config')]
PATHS = LIBS + ['stack/a2dp/latm_parser.c','stack/a2dp/latm_parser.h','CMakeLists.txt']
def main():
    p=argparse.ArgumentParser();p.add_argument('checkout',type=Path);a=p.parse_args()
    root=Path(__file__).resolve().parents[1]/'software/components/chime_codec_reference'
    data=subprocess.check_output(['git','-C',str(a.checkout),'archive',COMMIT,*[PREFIX+x for x in PATHS]])
    files={}
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        for item in tar:
            if not item.isfile():continue
            rel=item.name[len(PREFIX):];path=Path(rel)
            if path.suffix not in ('.c','.h','.inc','.txt','.md') and not any(x in path.name.upper() for x in ('LICENSE','COPYING','AUTHORS','NOTICE','README')):continue
            # Exclude examples/tests and non-codec backends; retain copyright texts.
            if any(x in path.parts for x in ('tests','test','demo','doc','examples')):continue
            raw=tar.extractfile(item).read();dest=root/'vendor/s31'/rel
            dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(raw)
            files['s31/'+rel]=hashlib.sha256(raw).hexdigest()
    (root/'UPSTREAM.json').write_text(json.dumps({'repository':'https://github.com/WillyBilly06/ESP32-S31-A2DP-Codecs','commit':COMMIT,'files':dict(sorted(files.items()))},indent=2)+'\n',encoding='utf-8')
    print('Imported',len(files),'byte-exact pinned files')
if __name__=='__main__':main()
