"""Generate quiet dual-source probes or analyze an explicit saved PCM WAV.

Offline only: never selects an endpoint, plays audio, records a mic, changes
volume, or claims sample-level/DMA/clock/end-to-end acceptance. Requires numpy.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import wave
import numpy as np

RATE = 48000
SYMBOL_S = .25
WINDOW_S = .1
# Independent, repeatable 31-symbol markers; no source is ever entirely silent.
CODES = {"usb": np.array([int(x) for x in "1111100011011101010000100101100"]),
         "ldac": np.array([int(x) for x in "1011010001111001000011010110110"])}
FREQUENCIES = {"usb": (997, 1213), "ldac": (1543, 1871)}
PROTOCOL = {"schema": 1, "kind": "dual_source_complementary_am", "usb_rate": RATE,
            "symbol_s": SYMBOL_S, "analysis_window_s": WINDOW_S,
            "frequencies_hz": FREQUENCIES,
            "codes": {k: "".join(str(v) for v in code) for k, code in CODES.items()},
            "high_amplitude_dbfs": -24, "low_over_high_amplitude": 1/3,
            "min_window_snr_db": 15, "min_median_snr_db": 20,
            "min_carrier_window_snr_db": 10, "min_carrier_median_snr_db": 15,
            "max_background_change_db": 6, "max_fitted_clock_ppm": 2000,
            "transition_s": .01}


class EvidenceError(ValueError):
    pass


def marker(source, times):
    """Stateless reference, including smooth transitions across chunk/loop seams."""
    t = np.asarray(times, dtype=np.float64)
    q = t / SYMBOL_S
    symbol = np.floor(q).astype(np.int64)
    code = CODES[source]
    now = code[symbol % len(code)].astype(np.float64)
    before = code[(symbol-1) % len(code)].astype(np.float64)
    fade = np.clip((q-symbol)*SYMBOL_S/.01, 0, 1)
    fade = .5-.5*np.cos(np.pi*fade)
    control = before+(now-before)*fade
    high = 10**(-24/20)
    a = high*(1/3+control*2/3)
    b = high*(1-control*2/3)
    f0, f1 = FREQUENCIES[source]
    return a*np.sin(2*np.pi*f0*t)+b*np.sin(2*np.pi*f1*t)


def generate(output, duration, ldac_rate=48000):
    if duration not in (60, 600):
        raise EvidenceError("only 60s and 600s windows are supported")
    if ldac_rate not in (44100,48000):raise EvidenceError("LDAC 44.1/48k only")
    output.mkdir(parents=True, exist_ok=False)
    manifest = dict(PROTOCOL, test_duration_s=duration, probe_duration_s=duration+10,
                    note="5s lead/tail allowance; no playback or recording performed",
                    source_rates={"usb":RATE,"ldac":ldac_rate}, files={})
    for source in FREQUENCIES:
        path=output/(source+"-probe.wav")
        rate=RATE if source=="usb" else ldac_rate
        with wave.open(str(path), "wb") as w:
            w.setnchannels(2);w.setsampwidth(2);w.setframerate(rate)
            for second in range(duration+10):
                t=second+np.arange(rate)/rate
                pcm=np.rint(marker(source,t)*32767).astype("<i2")
                w.writeframes(np.column_stack((pcm,pcm)).tobytes())
        manifest["files"][path.name]=sha256(path)
    (output/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    return manifest


def sha256(path):
    h=hashlib.sha256()
    with Path(path).open("rb") as f:
        for b in iter(lambda:f.read(1<<20),b""):h.update(b)
    return h.hexdigest()


def decode_pcm(data, width, channels, channel):
    if channel < 0 or channel >= channels:
        raise EvidenceError("explicit channel index outside recording")
    if width == 2:
        x=np.frombuffer(data,dtype="<i2").astype(np.float64)/32768
    elif width == 3:
        b=np.frombuffer(data,dtype=np.uint8).reshape(-1,3).astype(np.int32)
        v=b[:,0]|(b[:,1]<<8)|(b[:,2]<<16)
        x=((v^0x800000)-0x800000).astype(np.float64)/8388608
    elif width == 4:
        x=np.frombuffer(data,dtype="<i4").astype(np.float64)/2147483648
    else:
        raise EvidenceError("only uncompressed signed PCM16/24/32 WAV is supported")
    return x.reshape(-1,channels)[:,channel]


def inspect_segment(path, start_s, duration_s, channel):
    if not math.isfinite(start_s) or not math.isfinite(duration_s) or start_s<0 or duration_s<=0:
        raise EvidenceError("invalid WAV time interval")
    with wave.open(str(path),"rb") as w:
        if w.getcomptype() != "NONE" or w.getframerate() not in (44100,48000,96000):
            raise EvidenceError("unsupported capture format; no implicit resampling")
        rate=w.getframerate();n=round(WINDOW_S*rate)
        count=round(duration_s/WINDOW_S)
        if not math.isclose(count*WINDOW_S,duration_s,abs_tol=1e-6):
            raise EvidenceError("interval must contain full 100ms analysis windows")
        first=round(start_s*rate)
        if first+count*n>w.getnframes():raise EvidenceError("recording shorter than requested interval")
        w.setpos(first);window=np.hanning(n);freq=np.fft.rfftfreq(n,1/rate)
        centers=[f for pair in FREQUENCIES.values() for f in pair]
        masks=[np.abs(freq-f)<=20 for f in centers]
        # Same-width neighboring bands catch active fan/broadband interference.
        neighbors=[[np.abs(freq-(f+offset))<=20 for offset in (-100,-60,60,100)] for f in centers]
        energy=[];noise=[];rms=[];peak=0
        for _ in range(count):
            raw=w.readframes(n)
            if len(raw)!=n*w.getnchannels()*w.getsampwidth():raise EvidenceError("truncated WAV")
            x=decode_pcm(raw,w.getsampwidth(),w.getnchannels(),channel)
            peak=max(peak,float(np.max(np.abs(x))))
            power=np.abs(np.fft.rfft(x*window))**2*2/(n*np.sum(window**2))
            energy.append([float(np.sum(power[m])) for m in masks])
            noise.append([max(float(np.sum(power[m])) for m in group) for group in neighbors])
            rms.append(float(np.mean(x*x)))
        return dict(energy=np.array(energy),noise=np.array(noise),rms=np.array(rms),peak=peak,
                    rate=rate,channel=channel,windows=count)


def align_signature(contrast, code):
    # Unknown phone playback phase and independent clock drift. Find one global
    # affine mapping, not a new best match per block (which could conceal loss).
    t=(np.arange(len(contrast))+.5)*WINDOW_S
    period=len(code)*SYMBOL_S
    best=None
    for ppm in range(-2000,2001,50):
        q=t*(1+ppm/1e6)
        for phase in np.arange(0,period,.02):
            symbol=(q+phase)/SYMBOL_S
            fraction=symbol-np.floor(symbol)
            valid=(fraction>.24)&(fraction<.76) # avoid 100ms windows crossing transitions
            if np.sum(valid)<len(t)*.35:continue
            expected=code[np.floor(symbol).astype(np.int64)%len(code)]*2-1
            signed=contrast*expected
            # Bounded contrast, so one burst cannot dominate global alignment.
            score=float(np.mean(signed[valid]))
            if best is None or score>best[0]:best=(score,ppm,float(phase),valid,expected)
    score,ppm,phase,valid,expected=best
    bad=valid & ((contrast*expected)<.25)
    # Check each 5s subwindow as well as global fit: a local replay/phase slip
    # must not disappear into a 10-minute average.
    local=[]
    for begin in range(0,len(t),50):
        v=valid[begin:begin+50];b=bad[begin:begin+50]
        if np.sum(v):local.append(float(np.sum(b)/np.sum(v)))
    return dict(score=score,clock_fit_ppm=ppm,phase_s=phase,
                checked_windows=int(np.sum(valid)),bad_windows=int(np.sum(bad)),
                max_local_bad_fraction=max(local),
                matched=score>=.55 and int(np.sum(bad))==0)


def evaluate(active, before, after):
    """Arrays here are measurements/fixtures, not endpoint or provenance proof."""
    for measurement in (active,before,after):
        n=measurement["windows"]
        for key,shape in (("energy",(n,4)),("noise",(n,4)),("rms",(n,))):
            x=measurement[key]
            if x.shape!=shape or not np.all(np.isfinite(x)) or np.any(x<0):
                raise EvidenceError("invalid/negative/nonfinite measurement: "+key)
        if not math.isfinite(measurement["peak"]) or not 0<=measurement["peak"]<=1:
            raise EvidenceError("invalid capture peak")
    if active["windows"]<100 or min(before["windows"],after["windows"])<50:
        raise EvidenceError("need >=10s active and >=5s each pre/post background")
    for key in ("rate","channel"):
        if len({x[key] for x in (active,before,after)})!=1:raise EvidenceError("capture formats/channels differ")
    reasons=[];sources={};inconclusive=False;failed=False
    if max(x["peak"] for x in (active,before,after))>=.98:
        reasons.append("capture clipped or too close to full scale");inconclusive=True
    quiet=[float(np.mean(x["rms"])) for x in (before,after)]
    background_change_db=10*math.log10(max(quiet+[1e-20])/max(min(quiet),1e-20))
    if background_change_db>6:
        reasons.append("pre/post background changed by >6dB (e.g. fan/load/environment)");inconclusive=True
    invalid_capture=inconclusive
    # Maximum observed pre/post band energy, not their average. An isolated
    # noisy background can therefore withhold acceptance, never improve SNR.
    baseline=np.maximum(np.max(before["energy"],axis=0),np.max(after["energy"],axis=0))
    floor=np.maximum(active["noise"],baseline[None,:])
    for i,(source,code) in enumerate(CODES.items()):
        pair=slice(i*2,i*2+2)
        total=np.sum(active["energy"][:,pair],axis=1)
        noise=np.sum(floor[:,pair],axis=1)
        snr=10*np.log10(np.maximum(total,1e-20)/np.maximum(noise,1e-20))
        median=float(np.median(snr));minimum=float(np.min(snr))
        median_power=max(float(np.median(total)),1e-20)
        # A pronounced loss in an otherwise strong marker is actionable;
        # a marker uniformly too weak cannot distinguish no playback vs low SNR.
        gap=total<median_power/16
        carrier_snr=10*np.log10(np.maximum(active["energy"][:,pair],1e-20)/np.maximum(floor[:,pair],1e-20))
        carrier_ok=bool(np.all(carrier_snr>=10) and np.all(np.median(carrier_snr,axis=0)>=15))
        good_snr=bool(np.all(snr>=15)) and carrier_ok
        if median<20:
            state="INCONCLUSIVE";inconclusive=True
            detail="marker too weak/noisy to judge; do not automatically raise volume"
        elif np.any(gap):
            state="NOT_PASSED";failed=True;detail=">=100ms analysis window with strong marker loss"
        elif not good_snr:
            state="INCONCLUSIVE";inconclusive=True;detail="background masks at least one active window"
        else:
            power=active["energy"][:,pair]
            # Remove fixed room/EQ gain difference between the two carriers,
            # not a time-varying gain: otherwise an acoustic notch could invert
            # the raw power contrast. Constant tones still cannot pass a code.
            log_ratio=10*np.log10(np.maximum(power[:,0],1e-20)/np.maximum(power[:,1],1e-20))
            low,high=np.percentile(log_ratio,[20,80])
            contrast=np.tanh((log_ratio-(low+high)/2)*np.log(10)/20)
            signature=align_signature(contrast,code)
            signature["modulation_span_db"]=float(high-low)
            signature["matched"] &= bool(high-low>=10)
            state="CONTENT_PROBE_PASS" if signature["matched"] else "NOT_PASSED"
            failed |= not signature["matched"]
            detail="both carriers and complementary AM code checked"
            sources[source]=dict(result=state,detail=detail,median_snr_db=median,min_snr_db=minimum,
                                 gap_windows=int(np.sum(gap)),gap_window_indices=np.flatnonzero(gap).tolist(),signature=signature)
            continue
        sources[source]=dict(result=state,detail=detail,median_snr_db=median,min_snr_db=minimum,
                             gap_windows=int(np.sum(gap)),gap_window_indices=np.flatnonzero(gap).tolist())
    result="INCONCLUSIVE" if invalid_capture else "NOT_PASSED" if failed else "INCONCLUSIVE" if inconclusive else "CONTENT_PROBES_PASS"
    return dict(result=result,sources=sources,reasons=reasons,background_change_db=background_change_db,
                analyzed_s=active["windows"]*WINDOW_S,analysis_window_s=WINDOW_S,
                before_background_s=before["windows"]*WINDOW_S,after_background_s=after["windows"]*WINDOW_S,
                continuous_audio_verified=False,sub_window_gaps_verified=False,sample_clock_continuity_verified=False,
                physical_source_assignment_verified=False,end_to_end_acceptance="NOT_PROVEN")


def analyze(active_path,before_path,after_path,start,duration,channel):
    if duration not in (60,600):raise EvidenceError("only 60s/600s acceptance windows")
    # Caller records background with BOTH test sources silent, SAME gain/mic,
    # in this order before -> active -> after, after compilation/fan settling.
    paths={"active":active_path,"before":before_path,"after":after_path}
    hashes={k:sha256(v) for k,v in paths.items()}
    a=inspect_segment(active_path,start,duration,channel)
    def whole_background(path):
        with wave.open(str(path),"rb") as w:
            windows=w.getnframes()//round(w.getframerate()*WINDOW_S)
        if windows<50:raise EvidenceError("background recording must contain >=5s")
        return inspect_segment(path,0,windows*WINDOW_S,channel)
    b=whole_background(before_path);c=whole_background(after_path)
    result=evaluate(a,b,c)
    if hashes!={k:sha256(v) for k,v in paths.items()}:
        raise EvidenceError("WAV changed during analysis; freeze capture files first")
    result.update(protocol=PROTOCOL,capture_start_s=start,capture_channel=channel,
                  wav_sha256=hashes,analyzer_sha256=sha256(Path(__file__)))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest="command",required=True)
    g=sub.add_parser("generate");g.add_argument("--output",type=Path,required=True)
    g.add_argument("--duration",type=int,choices=(60,600),required=True)
    g.add_argument("--ldac-rate",type=int,choices=(44100,48000),default=48000)
    a=sub.add_parser("analyze")
    for name in ("active","before","after"):a.add_argument("--"+name,type=Path,required=True)
    a.add_argument("--start",type=float,required=True);a.add_argument("--channel",type=int,required=True)
    a.add_argument("--duration",type=int,choices=(60,600),required=True)
    a.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    if args.command=="generate":r=generate(args.output,args.duration,args.ldac_rate)
    else:
        if args.output.exists():raise EvidenceError("output exists; preserve evidence")
        r=analyze(args.active,args.before,args.after,args.start,args.duration,args.channel)
        with args.output.open("x",encoding="utf-8") as f:f.write(json.dumps(r,indent=2)+"\n")
    print(json.dumps(r,indent=2))
    return 0 if args.command=="generate" or r["result"]=="CONTENT_PROBES_PASS" else 2


if __name__=="__main__":raise SystemExit(main())
