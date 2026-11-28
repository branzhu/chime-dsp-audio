"""Reproducible LHDC amplitude audit. All edits to codec sources are test-only.

python scripts/investigate-lhdc-amplitude.py --standalone tmp/lhdc-v5-reference \
    --encoder tmp/lhdcv5-encoder

Requires GCC/MinGW and the two pinned third-party source checkouts; no downloads.
Emits PCM, exact encoded inputs, hashes, build logs, counters and L/R CSV meters.
Not an on-device/RF measurement. Product pipeline is PCM16 44.1/48k only.
"""
import argparse,array,csv,hashlib,importlib.util,json,math,os,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
REF=ROOT/'software/components/chime_codec_reference'
TEST=ROOT/'software/tests/bluetooth'
PINS={'standalone':'cf857ce1a1429965e161e54adbc3f293fd9ce273',
      'encoder':'699a624f4f04b6419af2f4a5ef3cc40dd39d2855'}

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(args,env=None):
    p=subprocess.run([str(x) for x in args],cwd=ROOT,env=env,capture_output=True,text=True,timeout=180)
    if p.returncode:raise RuntimeError(f'command failed ({p.returncode}): {args}\n{p.stdout}\n{p.stderr}')
    return p.stdout

def meter(path,bits,fullscale,rate,skip_seconds=.1):
    data=path.read_bytes()
    if bits==24:
        vals=[int.from_bytes(data[i:i+3],'little',signed=True) for i in range(0,len(data),3)]
    else:
        vals=array.array('h' if bits==16 else 'i');vals.frombytes(data)
        if sys.byteorder!='little':vals.byteswap()
    rows=[]
    for ch in range(2):
        v=vals[ch::2];skip=round(rate*skip_seconds);steady=v[skip:]
        if not steady:raise RuntimeError('Insufficient steady-state PCM: '+str(path))
        n=len(steady);energy=sum(x*x for x in steady);peak=max(abs(x) for x in steady)
        # Two-parameter least squares avoids finite-window quadrature bias.
        ss=cc=sc=ys=yc=0.
        for i,x in enumerate(steady):
            phase=2*math.pi*1000*(i+skip)/rate;s=math.sin(phase);c=math.cos(phase)
            ss+=s*s;cc+=c*c;sc+=s*c;ys+=x*s;yc+=x*c
        det=ss*cc-sc*sc;a=(ys*cc-yc*sc)/det;b=(yc*ss-ys*sc)/det
        def db(x):return 20*math.log10(x/fullscale) if x else None
        hi=fullscale-1;lo=-fullscale
        rows.append(dict(channel='LR'[ch],samples_total=len(v),samples_measured=n,
            skip_samples=skip,peak_dbfs=db(peak),rms_dbfs=db(math.sqrt(energy/n)),
            sine_amplitude_dbfs=db(math.hypot(a,b)),
            sine_energy_fraction=(a*ys+b*yc)/energy if energy else 0,
            zero_samples_total=sum(x==0 for x in v),zero_samples_measured=sum(x==0 for x in steady),
            rail_samples_total=sum(x<=lo or x>=hi for x in v)))
    return rows

def run_ablations(out,standalone,cc):
    """Deliberately mixed HOST-ONLY variants identify contributions, never firmware."""
    env={k:v for k,v in os.environ.items() if not k.startswith('LHDC_')}
    tree=out/'port/external/lhdc-v5';dest=out/'ablations';dest.mkdir(exist_ok=True)
    original=(out/'s31/lhdc_dec.c').read_text(encoding='utf-8')
    reference=(standalone/'decoder/lhdc_dec.c').read_text(encoding='utf-8')
    start='static float lhdc_dec_gain_exponent(';end='/* Per-rate output level normalization.'
    gain=reference[reference.index(start):reference.index(end)]
    gain=gain.replace('int bit_depth,\n                                    uint32_t sample_rate)', 'int bit_depth)')
    assert 'uint32_t sample_rate)' not in gain
    gain=gain.replace('    int L = enc_frame_len;', '    uint32_t sample_rate=diag_sample_rate;\n    int L = enc_frame_len;')
    sns=dest/'lhdc_sns_synth.c';sns.write_text((standalone/'decoder/lhdc_sns_synth.c').read_text(encoding='utf-8'),encoding='utf-8')
    rows=[];receipts={}
    variants=[('gain_function_only_INCOMPATIBLE',1,0,0,0),('sns_only',0,1,0,0),
              ('gain_and_length',1,0,0,1),('gain_length_sns',1,1,0,1),('gain_length_sns_rate',1,1,1,1)]
    for label,with_gain,with_sns,with_rate,with_length in variants:
        text=original
        if with_gain:text='unsigned diag_sample_rate;\n'+text[:text.index(start)]+gain+text[text.index(end):]
        if with_length:
            site='&dec->sns_params, enc_frame_len, bit_depth);'
            assert text.count(site)==1
            text=text.replace(site,'&dec->sns_params, (enc_frame_len / n_ch), bit_depth);')
        if with_rate:
            assert text.count('return r * r;')==1
            text=text.replace('return r * r;','return r;')
        unit=dest/(label+'.c');unit.write_text(text,encoding='utf-8')
        sources=[TEST/'test_lhdc_amplitude.c',unit,*[p for p in sorted(tree.glob('*.c')) if p.name!='lhdc_dec.c' and (p.name!='lhdc_sns_synth.c' or not with_sns)],*([sns] if with_sns else [])]
        exe=dest/(label+'.exe')
        run([cc,'-std=gnu11','-O2','-fwrapv',f'-I{tree}','-DLHDC_HOST_BUILD',*(['-DLHDC_GAIN_ABLATION'] if with_gain else []),*sources,'-o',exe,'-lm'])
        receipts[label]=sha(unit)
        for rate in [44100,48000,96000,192000]:
          for bits in [16,24]:
            case=f'{rate}-{bits}-q5--20';encoded=out/(case+'.lhdc')
            if not encoded.exists():raise RuntimeError('Run the full matrix before ablations: '+str(encoded))
            pcm=dest/(case+'.'+label+'.pcm');stats=pcm.with_suffix('.json')
            run([exe,rate,bits,encoded,pcm,stats],env)
            for row in meter(pcm,16 if bits==16 else 32,2**(15 if bits==16 else 31),rate):
                rows.append(dict(case=case,variant=label,pcm_sha256=sha(pcm),**row))
        print('ablation',label,flush=True)
    with (out/'ablations.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,list(rows[0]));w.writeheader();w.writerows(rows)
    (dest/'receipt.json').write_text(json.dumps(dict(instrumented_sources=receipts,sns_sha256=sha(sns),scope='HOST-ONLY intentionally mixed variants; not firmware'),indent=2)+'\n')

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--standalone',required=True,type=Path)
    ap.add_argument('--encoder',required=True,type=Path);ap.add_argument('--output',type=Path,default=ROOT/'tmp/lhdc-amplitude')
    ap.add_argument('--ablations-only',action='store_true',help='Requires a previously completed full matrix')
    ap.add_argument('--quick',action='store_true',help='44.1/48k,16/24 bits,q5 only')
    ap.add_argument('--frames',type=int,default=120);a=ap.parse_args()
    if a.frames<80:ap.error('--frames must be >=80 (startup exclusion)')
    a.standalone=a.standalone.resolve();a.encoder=a.encoder.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    for name,path in [('standalone',a.standalone),('encoder',a.encoder)]:
        head=run(['git','-C',path,'rev-parse','HEAD']).strip()
        if head!=PINS[name]:raise RuntimeError(f'{name}: expected {PINS[name]}, got {head}')
        # Some archive checkouts have an empty index. Verify source bytes against
        # Git objects rather than trusting the index or a clean status message.
        entries=run(['git','-C',path,'ls-tree','-r','HEAD','--',*(['decoder'] if name=='standalone' else [])])
        for entry in entries.splitlines():
            meta,relative=entry.split('\t',1);mode,kind,digest=meta.split()
            if Path(relative).suffix not in ('.c','.h','.inc'):continue
            data=(path/relative).read_bytes().replace(b'\r\n',b'\n')
            actual=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
            if actual!=digest:raise RuntimeError(f'{name}: source differs from pinned Git blob: {relative}')
    cc=os.environ.get('S31_CC') or shutil.which('gcc') or 'C:/Qt/Tools/mingw1310_64/bin/gcc.exe'
    os.environ['PATH']=str(Path(cc).parent)+os.pathsep+os.environ.get('PATH','')
    if a.ablations_only:
        run_ablations(out,a.standalone,cc);return
    # Never let inherited diagnostic overrides change the measured algorithm.
    env={k:v for k,v in os.environ.items() if not k.startswith('LHDC_')}
    log=open(out/'build.log','w',encoding='utf-8')
    def compile(exe,sources,includes=(),defines=()):
        cmd=[cc,'-std=gnu11','-O2','-fwrapv','-ffunction-sections','-fdata-sections',*[f'-I{x}' for x in includes],
             *[f'-D{x}' for x in defines],*sources,'-Wl,--gc-sections','-o',exe,'-lm']
        log.write(' '.join(map(str,cmd))+'\n');log.flush();log.write(run(cmd));log.flush()
    def once(text,old,new):
        if text.count(old)!=1:raise RuntimeError('Instrumentation site moved: '+old)
        return text.replace(old,new)
    # Generate the very same product port as CMake; verifies EVERY imported hash.
    spec=importlib.util.spec_from_file_location('prepare_s31',REF/'prepare.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
    port=out/'port';mod.generate(port)
    receipts={}
    for label,tree in [('s31',port/'external/lhdc-v5'),('standalone',a.standalone/'decoder')]:
        dest=out/label;dest.mkdir(exist_ok=True)
        text=(tree/'lhdc_dec.c').read_text(encoding='utf-8')
        decl='unsigned long long diag_concealed,diag_zero_filled[2],diag_preclip[2],diag_nonfinite[2],diag_channel_errors;\n'
        text=decl+text
        # Count known causes of zero filling; never infer concealment from zeros.
        marker='    int conceal = (frame_peak > 2.0f * clip_lim);'
        left='dec->pcm_mid[n]' if label=='s31' else 'out_l[n]'
        right='ch_pcm[n]' if label=='s31' else 'out_r[n]'
        instrumentation='''
    for (int n=0;n<samples_per_ch;n++) {
        float samples[2]={LEFT*sc,RIGHT*sc};
        for(int c=0;c<2;c++) {if(!isfinite(samples[c]))diag_nonfinite[c]++;
            if(fabsf(samples[c])>clip_lim)diag_preclip[c]++;}
    }
    if(conceal){diag_concealed++;diag_zero_filled[0]+=samples_per_ch;diag_zero_filled[1]+=samples_per_ch;}
'''.replace('LEFT',left).replace('RIGHT',right)
        text=once(text,marker,marker+instrumentation)
        # Includes R fallback silence; count source failures separately.
        # Slot-aware standalone has multiple channel paths: instrument fallback memset sites.
        if label=='s31':
            text=once(text,'for (int n = 0; n < samples_per_ch; n++) ch_pcm[n] = 0.0f;',
                      'diag_channel_errors++;diag_zero_filled[1]+=samples_per_ch;for (int n = 0; n < samples_per_ch; n++) ch_pcm[n] = 0.0f;')
        else:
            site='for (int n = 0; n < samples_per_ch; n++) out_r[n] = 0.0f;'
            assert text.count(site)==2
            text=text.replace(site,'diag_channel_errors++;diag_zero_filled[1]+=samples_per_ch;'+site)
        if label=='standalone' and os.name=='nt':
            # MinGW's CRT lacks C11 timespec_get. Profiling clock only; no PCM math.
            text=once(text,'struct timespec ts;\n      timespec_get(&ts, TIME_UTC);\n      return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;',
                      'return (int64_t)clock() * 1000000 / CLOCKS_PER_SEC;')
        unit=dest/'lhdc_dec.c';unit.write_text(text,encoding='utf-8')
        sources=[unit,*[p for p in sorted(tree.glob('*.c')) if p.name!='lhdc_dec.c']]
        compile(out/(label+'.exe'),[TEST/'test_lhdc_amplitude.c',*sources],[tree],['LHDC_HOST_BUILD',*(['LHDC_STANDALONE'] if label=='standalone' else [])])
        receipts[label]={p.name:sha(p) for p in sorted(tree.iterdir()) if p.is_file()}
        receipts[label]['instrumented_lhdc_dec.c']=sha(unit)
    compile(out/'encode.exe',[TEST/'generate_lhdc_amplitude.c',a.encoder/'cirbuf.c',a.encoder/'lhdcv5_api.c',*sorted((a.encoder/'lhdc_v5').glob('*.c'))],[a.encoder])
    # Build the selected real codecs, sharing the existing regression-suite cache.
    # Its known unity-gain assertion is a result, not a reason to change the gain.
    build=subprocess.run([sys.executable,ROOT/'scripts/test-s31-codecs.py'],cwd=ROOT,capture_output=True,text=True,env=env)
    (out/'s31-regression.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    cache=ROOT/'tmp/s31-codec-tests';objects=sorted(cache.glob('codec-*.o'))
    if not objects:raise RuntimeError('S31 regression compilation did not produce codec objects')
    # Verify all required object receipts exist, rather than treating an early compile failure as a gain failure.
    if 'Compiled ' not in build.stdout:raise RuntimeError('S31 bootstrap compilation failed: '+build.stderr+build.stdout)
    compile(out/'pipeline.exe',[TEST/'test_lhdc_amplitude_pipeline.c',ROOT/'software/components/chime_lhdc/chime_lhdc.c',
        ROOT/'software/main/chime_bt_pcm.c',ROOT/'software/main/chime_bt_src.c',ROOT/'software/main/chime_mixer.c',*objects],
        [REF,ROOT/'software/main/include',ROOT/'software/components/chime_lhdc',ROOT/'software/components/chime_ldac',
         TEST/'stubs',ROOT/'software/tests/usb/stubs',ROOT/'software/tests/audio/stubs'],
        ['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CONFIG_CHIME_BT_VENDOR_CODECS=1','CONFIG_CHIME_BT_LHDC_V5=1','CHIME_S31_BACKEND=1','CHIME_BT_REAL_LHDC=1'])
    rows=[];cases=[];comparisons=[]
    rates=[44100,48000] if a.quick else [44100,48000,96000,192000]
    qualities=[5] if a.quick else [3,5,8]
    for rate in rates:
      for bits in [16,24]:
       for quality in qualities:
        for level in [-6,-20,-40]:
            case=f'{rate}-{bits}-q{quality}-{level}';prefix=out/case
            encoded=Path(str(prefix)+'.lhdc');source=Path(str(prefix)+'.source.pcm')
            enc=json.loads(run([out/'encode.exe',rate,bits,quality,level,a.frames,encoded,source],env))
            case_info=dict(case=case,rate=rate,bits=bits,quality=quality,level_dbfs=level,**enc,
                encoded_sha256=sha(encoded),source_sha256=sha(source),
                wire_bitrate_bps=enc['encoded_bytes']*8*rate/(enc['spf']*a.frames))
            cases.append(case_info)
            def add(stage,p,b,fs,sr,decoder='s31',**extra):
                for row in meter(p,b,fs,sr):
                    if stage=='raw_decoder':
                        ci='LR'.index(row['channel'])
                        row.update(preclip_samples=counts['preclip_samples'][ci],zero_filled_samples=counts['zero_filled_samples'][ci],nonfinite_samples=counts['nonfinite_samples'][ci])
                    rows.append(dict(case=case,rate=rate,source_bits=bits,quality=quality,input_dbfs=level,
                        decoder=decoder,stage=stage,pcm_sha256=sha(p),**row,**extra))
            add('source',source,bits,2**(bits-1),rate,'encoder_input')
            for decoder in ['s31','standalone']:
                pcm=Path(str(prefix)+f'.{decoder}.pcm');stats=Path(str(prefix)+f'.{decoder}.json')
                run([out/(decoder+'.exe'),rate,bits,encoded,pcm,stats],env);counts=json.loads(stats.read_text(encoding='utf-8'))
                for row in counts.get('preclip_samples',[]):assert row>=0
                add('raw_decoder',pcm,16 if bits==16 else 32,2**(15 if bits==16 else 31),rate,decoder,
                    concealed_frames=counts['concealed_frames'],rejected_frames=counts['rejected_frames'],
                    channel_errors=counts['channel_errors'],counter_file=stats.name)
            p0=Path(str(prefix)+'.s31.pcm');p1=Path(str(prefix)+'.standalone.pcm')
            comparisons.append(dict(case=case,identical=p0.read_bytes()==p1.read_bytes(),s31_sha256=sha(p0),standalone_sha256=sha(p1)))
            if bits==16 and rate<=48000:
                run([out/'pipeline.exe',rate,encoded,prefix],env)
                callback=Path(str(prefix)+'.callback.pcm')
                if callback.read_bytes()!=p0.read_bytes():raise RuntimeError('Raw/callback mismatch: '+case)
                for stage,b,fs,sr in [('callback',16,2**15,rate),('convert',32,2**27,rate),('resampler',32,2**27,48000),('mixer',32,2**31,48000)]:
                    add(stage,Path(str(prefix)+f'.{stage}.pcm'),b,fs,sr)
            # Differential diagnostics ONLY. Never set these in firmware.
            for override,value in [('LHDC_LEVELDB','0'),('LHDC_RATEPOW','1')]:
                if override=='LHDC_RATEPOW' and rate<=48000:continue
                pcm=Path(str(prefix)+f'.{override}.pcm');stats=Path(str(prefix)+f'.{override}.json')
                run([out/'s31.exe',rate,bits,encoded,pcm,stats],{**env,override:value})
                add(override,pcm,16 if bits==16 else 32,2**(15 if bits==16 else 31),rate,'s31_diagnostic_only')
            print(case,flush=True)
    fields=list(dict.fromkeys(k for row in rows for k in row))
    with (out/'measurements.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fields);w.writeheader();w.writerows(rows)
    manifest=dict(s31=json.loads((REF/'UPSTREAM.json').read_text(encoding='utf-8'))['commit'],**PINS,
        compiler=run([cc,'--version']).splitlines()[0],frames=a.frames,frequency_hz=1000,
        warmup_seconds=.1,fullscale_convention='sine peak relative to 2^(bits-1); RMS sine is peak-3.0103 dB',
        scope='host digital replay; production packet worker + real codecs, PCM queue, FIR and mixer; fake RTOS/radio; nominal clock, source gain unity, one BT lane',
        pipeline_unsupported='24-bit,96/192k: raw only; never relabel as supported product pipeline measurements',
        harness_sha256={str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__).resolve(),TEST/'generate_lhdc_amplitude.c',TEST/'test_lhdc_amplitude.c',TEST/'test_lhdc_amplitude_pipeline.c']},
        source_receipts=receipts,cases=cases,comparisons=comparisons,regression_exit=build.returncode)
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8');log.close()
    print(f'Wrote {len(rows)} channel measurements to {out}/measurements.csv')
    if not a.quick:run_ablations(out,a.standalone,cc)
if __name__=='__main__':main()
