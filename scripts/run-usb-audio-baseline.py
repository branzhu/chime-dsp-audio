"""Windows real USB playback/capture, optionally with explicit simulated LDAC.
Neither mode proves real Bluetooth/RF acceptance.
Explicit devices, quiet PCM16 probe, no volume/service/DSP changes. WinMM only.
Run capture after compilation/fan settling. Keep recorded files for offline QA.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import struct
import time
import traceback
import urllib.request
import wave

RATE=48000
class CapsOut(c.Structure):
    _fields_=[("mid",w.WORD),("pid",w.WORD),("ver",w.DWORD),("name",w.WCHAR*32),("formats",w.DWORD),("channels",w.WORD),("reserved",w.WORD),("support",w.DWORD)]
class CapsIn(c.Structure):
    _fields_=CapsOut._fields_[:-1]
class Format(c.Structure):
    _pack_=2
    _fields_=[("tag",w.WORD),("channels",w.WORD),("rate",w.DWORD),("bytes_s",w.DWORD),("align",w.WORD),("bits",w.WORD),("extra",w.WORD)]
class Header(c.Structure):
    pass
Header._fields_=[("data",c.c_void_p),("length",w.DWORD),("recorded",w.DWORD),("user",c.c_size_t),("flags",w.DWORD),("loops",w.DWORD),("next",c.POINTER(Header)),("reserved",c.c_size_t)]
class Position(c.Structure):
    _fields_=[("kind",w.DWORD),("value",w.DWORD),("rest",w.DWORD)]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--play-seconds",type=int,choices=(4,70,610),required=True)
    parser.add_argument("--usb-name",required=True);parser.add_argument("--mic-name",required=True)
    parser.add_argument("--url",required=True);parser.add_argument("--elf",type=Path,required=True)
    parser.add_argument("--ldac-preloaded-manifest",type=Path)
    parser.add_argument("--ldac-packets",type=Path)
    parser.add_argument("--ldac-manifest",type=Path)
    parser.add_argument("--ldac-rate",type=int,choices=(44100,48000),default=48000)
    parser.add_argument("--ldac-ppm",type=int,default=0)
    args=parser.parse_args()
    replay=None
    if args.ldac_preloaded_manifest:
        if args.ldac_packets or args.ldac_manifest or args.play_seconds==4:parser.error("preloaded and streaming modes are exclusive, 70/610s only")
        from ldac_replay_client import PreloadedLdacReplay
        replay=PreloadedLdacReplay(args.url,args.ldac_preloaded_manifest,args.ldac_rate,args.play_seconds,args.ldac_ppm)
    elif args.ldac_packets or args.ldac_manifest:
        if not(args.ldac_packets and args.ldac_manifest) or args.play_seconds==4:
            parser.error("replay requires packets + manifest and 70/610s window")
        from ldac_replay_client import LdacReplay
        replay=LdacReplay(args.url,args.ldac_packets,args.ldac_manifest,args.ldac_rate,args.play_seconds,args.ldac_ppm)
    args.output.mkdir(parents=True,exist_ok=False)
    def save(name,x):(args.output/name).write_text(json.dumps(x,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    mm=c.WinDLL("winmm")
    for kind in ("In","Out"):
        for name,types in (("Open",[c.POINTER(c.c_void_p),w.UINT,c.POINTER(Format),c.c_size_t,c.c_size_t,w.DWORD]),
            ("PrepareHeader",[c.c_void_p,c.POINTER(Header),w.UINT]),("UnprepareHeader",[c.c_void_p,c.POINTER(Header),w.UINT]),
            ("Reset",[c.c_void_p]),("Close",[c.c_void_p]),("GetPosition",[c.c_void_p,c.POINTER(Position),w.UINT])):
            f=getattr(mm,"wave"+kind+name);f.argtypes=types;f.restype=w.UINT
        getattr(mm,"wave"+kind+"GetDevCapsW").argtypes=[c.c_size_t,c.c_void_p,w.UINT]
    for name in ("waveInAddBuffer","waveOutWrite"):
        getattr(mm,name).argtypes=[c.c_void_p,c.POINTER(Header),w.UINT]
    for name in ("waveInStart","waveInStop"):
        getattr(mm,name).argtypes=[c.c_void_p]
    def check(value):
        if value:raise RuntimeError("WinMM error "+str(value))
    devices={};ids={}
    for kind,cls,needle in (("Out",CapsOut,args.usb_name),("In",CapsIn,args.mic_name)):
        rows=[]
        for i in range(getattr(mm,"wave"+kind+"GetNumDevs")()):
            caps=cls();check(getattr(mm,"wave"+kind+"GetDevCapsW")(i,c.byref(caps),c.sizeof(caps)))
            rows.append(dict(id=i,name=caps.name,channels=caps.channels))
        devices[kind]=rows;matches=[x for x in rows if needle in x["name"]]
        if len(matches)!=1:raise RuntimeError("need one explicit device match: "+kind+" "+needle)
        ids[kind]=matches[0]["id"]
    save("devices.json",devices)
    with wave.open(str(args.probe),"rb") as source:
        assert (source.getframerate(),source.getnchannels(),source.getsampwidth())==(RATE,2,2)
        data=source.readframes(args.play_seconds*RATE)
    assert len(data)==args.play_seconds*RATE*4
    peak=max(abs(v[0]) for v in struct.iter_unpack("<h",data))/32768
    assert peak<=.09,"not a quiet diagnostic probe; refusing playback"
    fmt=Format(1,2,RATE,RATE*4,4,16,0)
    # Query format without opening playback/capture. Do not use WAVE_MAPPER.
    check(mm.waveOutOpen(None,ids["Out"],c.byref(fmt),0,0,1))
    check(mm.waveInOpen(None,ids["In"],c.byref(fmt),0,0,1))
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    def get(path):
        with opener.open(args.url.rstrip("/")+path,timeout=2) as r:return json.load(r)
    expected=hashlib.sha256(args.elf.read_bytes()).hexdigest()
    before=get("/api/audio/diagnostics");status=get("/api/status")
    save("preflight.json",dict(audio=before,status=status,expected_elf_sha256=expected,probe_sha256=hashlib.sha256(args.probe.read_bytes()).hexdigest(),peak=peak))
    assert before["firmware"]["elf_sha256"]==expected
    if replay and "src_accel" in before:
        accel=before["src_accel"]
        assert accel["complete"] and accel["enabled"] and accel["reason"]==0, ("CPU1 SRC not qualified",accel)
        if "error_bound_q27" in accel:
            assert 0<=accel["max_error_q27"]<=accel["error_bound_q27"]<=26

    assert not before["usb"]["streaming"] and not before["bt"]["connected"] and not before["bt"].get("simulated",False)
    if replay:assert before.get("ldac_replay_enabled") is True
    assert not before["source"]["amps_enabled"] and before["source"]["dsp_ready"]
    assert not status["bridge_connected"] and not status["dsp_external"] and not status["tuning_armed"] and not status["tuning_pending"]
    if replay and hasattr(replay,"prepare"):replay.prepare();save("ldac-preparation.json",replay.events)
    # No FFT/compilation during capture; prepare allocations first.
    output_buffer=c.create_string_buffer(data)
    capture_buffer=c.create_string_buffer((args.play_seconds+25)*RATE*4)
    out_header=Header(data=c.addressof(output_buffer),length=len(data))
    in_header=Header(data=c.addressof(capture_buffer),length=c.sizeof(capture_buffer))
    input_handle=c.c_void_p();output_handle=c.c_void_p();prepared_in=prepared_out=False
    events=[];observations=[];fault=None;play_started=None;play_ended=None
    def position():
        pos=Position(kind=2);check(mm.waveInGetPosition(input_handle,c.byref(pos),c.sizeof(pos)))
        if pos.kind==2:return int(pos.value)
        if pos.kind==4:return int(pos.value)//4
        if pos.kind==1:return round(pos.value*RATE/1000)
        raise RuntimeError("unsupported capture position type "+str(pos.kind))
    def event(name):
        e=dict(event=name,host_monotonic_s=time.monotonic(),capture_frame=position());events.append(e);save("events.json",events);return e
    try:
        check(mm.waveInOpen(c.byref(input_handle),ids["In"],c.byref(fmt),0,0,0))
        check(mm.waveInPrepareHeader(input_handle,c.byref(in_header),c.sizeof(in_header)));prepared_in=True
        check(mm.waveInAddBuffer(input_handle,c.byref(in_header),c.sizeof(in_header)))
        check(mm.waveInStart(input_handle));event("capture_start")
        deadline=time.monotonic()+8
        while position()<5*RATE:
            if time.monotonic()>deadline:raise RuntimeError("capture not advancing")
            time.sleep(.02)
        # Open only after background capture, so opening USB cannot contaminate it.
        event("background_before_end")
        check(mm.waveOutOpen(c.byref(output_handle),ids["Out"],c.byref(fmt),0,0,0))
        check(mm.waveOutPrepareHeader(output_handle,c.byref(out_header),c.sizeof(out_header)));prepared_out=True
        play_started=event("play_submit")
        check(mm.waveOutWrite(output_handle,c.byref(out_header),c.sizeof(out_header)))
        if replay:replay.start();event("simulated_ldac_started")
        next_poll=0
        while not out_header.flags&1:
            now=time.monotonic();elapsed=now-play_started["host_monotonic_s"]
            if elapsed>args.play_seconds+8:raise RuntimeError("USB playback completion timeout")
            if now>=next_poll:
                a=get("/api/audio/diagnostics");row=dict(host_monotonic_s=time.monotonic(),play_elapsed_s=elapsed,sample=a)
                observations.append(row)
                with (args.output/"observations.jsonl").open("a",encoding="utf-8") as f:f.write(json.dumps(row)+"\n")
                assert a["boot_id"]==before["boot_id"] and a["firmware"]["elf_sha256"]==expected
                assert not a["bt"]["connected"] and not a["bt"]["playing"]
                if replay:
                    replay.check()
                    assert a["bt"]["error"]==0 and a["ldac_replay"]["error"]==0, ("LDAC errors",a["bt"]["error"],a["ldac_replay"]["error"],a["ldac_replay"].get("failure_reason"))
                    if elapsed>=5:assert a["bt"]["simulated"] and a["bt"]["primed"] and a["ldac_replay"]["active"]
                assert a["source"]["sound_error"]==0 and a["usb"]["error"]==0
                if elapsed>=2:
                    assert a["usb"]["streaming"] and a["source"]["amps_enabled"] and a["source"]["rendering"]==3
                next_poll=now+1
            time.sleep(.02)
        play_ended=event("play_done")
    except BaseException as e:
        fault=repr(e)
        (args.output/"failure-traceback.txt").write_text(traceback.format_exc(),encoding="utf-8")
    finally:
        if replay:
            try:replay.stop()
            except Exception as e:fault=fault or "replay STOP: "+repr(e)
            save("ldac-replay-events.json",replay.events)
        if output_handle.value:
            mm.waveOutReset(output_handle)
            if prepared_out:mm.waveOutUnprepareHeader(output_handle,c.byref(out_header),c.sizeof(out_header))
            mm.waveOutClose(output_handle)
        if input_handle.value:
            try:
                if fault is None:
                    settle=position()+2*RATE;deadline=time.monotonic()+5
                    while position()<settle:
                        if time.monotonic()>deadline:fault="capture stopped during tail settling";break
                        time.sleep(.02)
                    event("background_after_start")
                    target=position()+5*RATE;deadline=time.monotonic()+8
                    while position()<target:
                        if time.monotonic()>deadline:fault="post-background capture stopped";break
                        time.sleep(.02)
                    event("capture_end")
            except BaseException as e:
                fault=fault or "capture cleanup: "+repr(e)
            finally:
                mm.waveInStop(input_handle);mm.waveInReset(input_handle)
                if prepared_in:mm.waveInUnprepareHeader(input_handle,c.byref(in_header),c.sizeof(in_header))
                mm.waveInClose(input_handle)
    recorded=int(in_header.recorded);pcm=capture_buffer.raw[:recorded]
    def wav(name,lo,hi):
        with wave.open(str(args.output/name),"wb") as f:
            f.setnchannels(2);f.setsampwidth(2);f.setframerate(RATE);f.writeframes(pcm[lo*4:hi*4])
    wav("capture.wav",0,recorded//4)
    if play_started and play_ended:
        begin=play_started["capture_frame"];end=play_ended["capture_frame"]
        after_begin=next((e["capture_frame"] for e in events if e["event"]=="background_after_start"),end)
        wav("before.wav",0,begin);wav("active.wav",begin,end);wav("after.wav",after_begin,recorded//4)
    try:save("after-status.json",get("/api/audio/diagnostics"))
    except Exception as e:save("after-status-error.json",dict(error=str(e)))
    candidates=[r for r in observations if 5<=r["play_elapsed_s"]<=args.play_seconds-2]
    rates={};counter_pass=False
    if fault is None and args.play_seconds>=70:
        try:
            assert len(candidates)>=2
            first,last=candidates[0],candidates[-1]
            seconds=(last["sample"]["sampled_us"]-first["sample"]["sampled_us"])/1e6
            host=last["host_monotonic_s"]-first["host_monotonic_s"]
            assert min(seconds,host)>= (600 if args.play_seconds==610 else 60)
            for row,prev in zip(candidates[1:],candidates[:-1]):
                assert 0<row["host_monotonic_s"]-prev["host_monotonic_s"]<3
                assert 0<row["sample"]["sampled_us"]-prev["sample"]["sampled_us"]<3000000
                for key in ("usb_short","bt_short","clips","deadline_misses"):
                    assert row["sample"]["render"][key]==first["sample"]["render"][key],key
                if first["sample"]["bt"].get("pipeline_enabled"):
                    assert row["sample"]["bt"]["pipeline_enabled"]
                    assert row["sample"]["bt"]["pipeline_underruns"]==first["sample"]["bt"]["pipeline_underruns"]
                    assert row["sample"]["bt"]["pipeline_consumed"]>prev["sample"]["bt"]["pipeline_consumed"]
                    assert row["sample"]["bt"]["pipeline_produced"]>prev["sample"]["bt"]["pipeline_produced"]
                    assert 0<=row["sample"]["bt"]["pipeline_ready_blocks"]<=4
                assert row["sample"]["usb"]["epoch"]==first["sample"]["usb"]["epoch"]
                assert row["sample"]["render"]["blocks"]>prev["sample"]["render"]["blocks"]
                assert row["sample"]["usb"]["rx_bytes"]>prev["sample"]["usb"]["rx_bytes"]
            for group,key,rate,tol in (("usb","rx_bytes",192000,.05),("render","blocks",375,.01)):
                actual=(last["sample"][group][key]-first["sample"][group][key])/seconds
                assert abs(actual/rate-1)<=tol,(key,actual);rates[key]=actual
            if replay:
                base=candidates[0]["sample"];previous=base
                for row in candidates[1:]:
                    a=row["sample"]
                    assert a["bt"]["epoch"]==base["bt"]["epoch"] and a["bt"]["simulated"]
                    for key in ("overflows","underruns","pcm_gap_events","pcm_gap_blocks","rebuffer_events","discarded_frames"):
                        assert a["bt"][key]==base["bt"][key],key
                    assert a["bt"]["codec"]==6 and a["bt"]["rate"]==args.ldac_rate
                    assert a["decoder"]["valid"] and a["decoder"]["capacity_timeouts"]==0
                    for key in ("packets","pcm_frames","rendered_frames"):
                        assert a["bt"][key]>previous["bt"][key],key
                    assert a["ldac_replay"]["submitted_frames"]>previous["ldac_replay"]["submitted_frames"]
                    previous=a
                for key,expected_rate in (("pcm_frames",args.ldac_rate),("rendered_frames",48000)):
                    actual=(last["sample"]["bt"][key]-first["sample"]["bt"][key])/seconds
                    assert abs(actual/expected_rate-1)<=.01,(key,actual)
                    rates["bt_"+key]=actual
                if args.ldac_preloaded_manifest:
                    assert last["sample"]["ldac_replay"]["completed_cycles"]>first["sample"]["ldac_replay"]["completed_cycles"]
            counter_pass=True
        except Exception as e:fault="USB baseline counter gate: "+repr(e)
    result=dict(result=("USB_SIMULATED_LDAC_COUNTERS_PASS" if replay else "USB_ONLY_COUNTERS_PASS") if counter_pass else "SHORT_ROUTE_CAPTURED" if fault is None else "NOT_PASSED",error=fault,
        requested_play_s=args.play_seconds,observations=len(observations),captured_frames=recorded//4,
        firmware_elf_sha256=expected,rates_per_s=rates,mic_content_verified=False,
        bluetooth_tested=False,ldac_simulated=bool(replay),ldac_preloaded=bool(args.ldac_preloaded_manifest),rf_tested=False,end_to_end_mix_accepted=False)
    save("result.json",result);print(json.dumps(result,indent=2))
    return 0 if fault is None else 2


if __name__=="__main__":raise SystemExit(main())
