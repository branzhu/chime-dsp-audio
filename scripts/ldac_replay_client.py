"""Explicit default-off LDAC test transport; real RF/phone is NOT exercised.
Preloads continuous real encoded packets, then uploads against an independent
absolute host timeline. ESP32 independently releases them at sample deadlines.
No PCM-demand polling, automatic retry, looping, volume, or DSP changes.
"""
import bisect
import hashlib
import http.client
import json
from pathlib import Path
import struct
import threading
import time
from urllib.parse import urlsplit


def packets_for_window(path,rate,seconds):
    raw=Path(path).read_bytes();offset=0;packets=[];samples=0
    limit=int((rate*seconds+127)//128)*128
    while offset<len(raw):
        if len(raw)-offset<3:raise ValueError('truncated LDAC record')
        size,nf=struct.unpack_from('<HB',raw,offset);offset+=3
        if not 3<=size<=2047 or not 1<=nf<=15 or offset+size>len(raw):raise ValueError('bad LDAC record')
        payload=raw[offset:offset+size];offset+=size
        # Validate raw frame boundaries/rate/stereo before any network/audio.
        cursor=0;ends=[]
        for _ in range(nf):
            if len(payload)-cursor<3:raise ValueError('truncated frame')
            a,b,c=payload[cursor:cursor+3]
            size_frame=((b&7)<<6)+(c>>2)+4
            if a!=0xaa or b>>5!=(1 if rate==48000 else 0) or (b>>3)&3!=2 or not 4<=size_frame<=515 or cursor+size_frame>len(payload):
                raise ValueError('invalid LDAC stereo frame/rate/bounds')
            cursor+=size_frame;ends.append(cursor)
        if cursor!=len(payload):raise ValueError('unaccounted LDAC packet tail')
        if samples>=limit:continue # ignore encoder flush beyond requested window
        count=min(nf,(limit-samples)//128)
        packet=payload[:ends[count-1]]
        packets.append((samples,struct.pack('<HB',len(packet),count)+packet));samples+=count*128
    if samples!=limit:raise ValueError('encoded stream shorter than requested window')
    batches=[];index=0
    while index<len(packets):
        first=packets[index][0];body=bytearray(struct.pack('<I',first));n=0
        while index<len(packets) and n<8 and len(body)+len(packets[index][1])<=8192:
            body.extend(packets[index][1]);index+=1;n+=1
        if not n:raise ValueError('packet exceeds ingress bounds')
        batches.append((first,bytes(body)))
    return batches


class LdacReplay:
    def __init__(self,url,path,manifest,rate,seconds,ppm=0):
        u=urlsplit(url)
        if u.scheme!='http' or u.path not in ('','/') or u.username or u.password or u.query or u.fragment:
            raise ValueError('explicit plain-HTTP board origin required')
        if rate not in (44100,48000) or seconds not in (70,610) or not -2000<=ppm<=2000:raise ValueError('test bounds')
        receipt=json.loads(Path(manifest).read_text(encoding='utf-8'))
        if receipt['packets_sha256']!=hashlib.sha256(Path(path).read_bytes()).hexdigest() or receipt['rate']!=rate or receipt['duration_s']<seconds or receipt['pcm_peak']>.09:
            raise ValueError('unverified/non-quiet/wrong-rate fixture')
        self.batches=packets_for_window(path,rate,seconds)
        # Select an ahead window from packet density only, NEVER PCM demand.
        # Reserve two of 32 ingress slots for anchor/scheduling uncertainty.
        starts=[];batch_ends=[]
        for first,body in self.batches:
            off=4;sample=first
            while off<len(body):
                size,nf=struct.unpack_from('<HB',body,off);starts.append(sample);sample+=nf*128;off+=3+size
            batch_ends.append(len(starts))
        self.packet_starts=starts;self.batch_ends=batch_ends
        self.lead=self.choose_lead(rate,ppm,0)
        self.host=u.hostname;self.port=u.port or 80;self.origin=url.rstrip('/');self.rate=rate;self.seconds=seconds;self.ppm=ppm
        self.cancel=threading.Event();self.thread=None;self.error=None;self.events=[];self.started=False
    def choose_lead(self,rate,ppm,anchor_margin):
        for lead in (.30,.25,.20,.15,.10,.08,.05):
            if lead<anchor_margin+.02:continue
            occupancy=max(end-bisect.bisect_left(self.packet_starts,first-(lead+anchor_margin)*rate*(1+ppm/1e6)) for (first,_),end in zip(self.batches,self.batch_ends))
            if occupancy<=30:return lead
        raise ValueError('network uncertainty/packet density exceeds bounded timed ingress')
    def connection_new(self):return http.client.HTTPConnection(self.host,self.port,timeout=1)
    def post(self,path,body,connection=None):
        own=connection is None;c=connection or self.connection_new()
        try:
            c.request('POST',path,body,{'Origin':self.origin,'X-Chime-LDAC-Replay':'1','Content-Type':'application/octet-stream'})
            r=c.getresponse();data=r.read()
            if r.status!=200 or json.loads(data).get('accepted') is not True:raise RuntimeError(f'LDAC ingress HTTP {r.status}: {data!r}')
        finally:
            if own:c.close()
    def start(self):
        self.post('/api/audio/ldac-replay',struct.pack('<IHh',self.rate,self.seconds,self.ppm))
        self.started=True
        try:
            c=self.connection_new();t0=time.monotonic()
            try:
                c.request('GET','/api/audio/diagnostics');r=c.getresponse();a=json.loads(r.read());t1=time.monotonic()
                if r.status!=200:raise RuntimeError('replay diagnostics unavailable')
            finally:c.close()
            s=a['ldac_replay']
            self.events.append(dict(event='time_anchor',host_monotonic_s=t1,rtt_s=t1-t0,replay_active=s['active'],simulated=a['bt']['simulated']))
            if t1-t0>.25 or not s['active'] or a['bt']['connected'] or not a['bt']['simulated']:
                raise RuntimeError(f'uncertain time anchor/not admitted: RTT={t1-t0:.3f}s active={s["active"]} simulated={a["bt"]["simulated"]}')
            self.lead=self.choose_lead(self.rate,self.ppm,(t1-t0)/2+.02)
            self.base=(t0+t1)/2+(s['start_us']-a['sampled_us'])/1e6
            self.events.append(dict(event='start',host_monotonic_s=t1,epoch=s['epoch'],anchor_rtt_s=t1-t0,base_host_s=self.base,ahead_s=self.lead,simulated=True,rf_tested=False))
            self.thread=threading.Thread(target=self.feed,name='LDAC-upload',daemon=True);self.thread.start()
        except BaseException:
            self.stop();raise
    def feed(self):
        c=self.connection_new()
        try:
            for i,(sample,body) in enumerate(self.batches):
                due=self.base+sample/self.rate/(1+self.ppm/1e6)-self.lead
                if i and self.cancel.wait(max(0,due-time.monotonic())):return
                if self.cancel.is_set():return
                t0=time.monotonic();self.post('/api/audio/ldac-replay/packets',body,c);t1=time.monotonic()
                self.events.append(dict(event='upload',first_frame=sample,host_monotonic_s=t1,http_s=t1-t0))
        except BaseException as e:
            self.error=repr(e);self.cancel.set()
            self.events.append(dict(event='upload_failed',first_frame=sample,host_monotonic_s=time.monotonic(),error=self.error))
        finally:c.close()
    def check(self):
        if self.error:raise RuntimeError('simulated LDAC TRANSPORT failed: '+self.error)
    def stop(self):
        self.cancel.set()
        if self.thread:self.thread.join(timeout=2)
        if self.thread and self.thread.is_alive():raise RuntimeError('LDAC upload thread did not stop')
        if self.started:
            self.post('/api/audio/ldac-replay',b'0');self.started=False


class PreloadedLdacReplay(LdacReplay):
    """Explicit read-only board fixture; no HTTP packet uploads during playback."""
    def __init__(self,url,manifest,rate,seconds,ppm=0):
        u=urlsplit(url)
        if u.scheme!='http' or u.path not in ('','/') or u.username or u.password or u.query or u.fragment:raise ValueError('explicit board origin required')
        r=json.loads(Path(manifest).read_text(encoding='utf-8'))
        if rate!=48000 or seconds not in (70,610) or not -2000<=ppm<=2000 or r.get('kind')!='qualified_periodic_ldac' or r.get('rate')!=rate or r.get('cycle_input_frames')!=1488000 or r.get('encoded_periods_bit_identical') is not True or r.get('decoder_periods_compared',0)<3 or not 0<=r.get('pcm_peak',1)<=.09:
            raise ValueError('seam-qualified quiet 48k/31s fixture receipt required')
        self.expected_sha=r['payload_sha256'];self.host=u.hostname;self.port=u.port or 80;self.origin=url.rstrip('/')
        self.rate=rate;self.seconds=seconds;self.ppm=ppm;self.cancel=threading.Event();self.thread=None;self.error=None;self.events=[];self.started=False
    def diagnostics(self):
        c=self.connection_new()
        try:
            c.request('GET','/api/audio/diagnostics');r=c.getresponse();a=json.loads(r.read())
            if r.status!=200:raise RuntimeError('diagnostics unavailable')
            return a
        finally:c.close()
    def verify_fixture(self,a):
        s=a['ldac_replay']
        if not s['flash_enabled'] or not s['flash_ready'] or s['fixture_sha256']!=self.expected_sha or s['fixture_frames']!=1488000:
            raise RuntimeError('board fixture pin/readiness does not match qualified receipt')
    def prepare(self):
        self.post('/api/audio/ldac-replay',b'P')
        a=self.diagnostics();self.verify_fixture(a)
        if a['ldac_replay']['active'] or a['source']['amps_enabled']:raise RuntimeError('fixture preparation was not idle')
        self.events.append(dict(event='prepared',host_monotonic_s=time.monotonic(),fixture_sha256=self.expected_sha,hot_path_http_packets=False,rf_tested=False))
    def start(self):
        self.post('/api/audio/ldac-replay',struct.pack('<IHh',0,self.seconds,self.ppm));self.started=True
        try:
            a=self.diagnostics();self.verify_fixture(a);s=a['ldac_replay']
            if not s['active'] or not s['preloaded'] or a['bt']['connected'] or not a['bt']['simulated']:raise RuntimeError('preloaded replay not admitted')
            self.events.append(dict(event='start_preloaded',host_monotonic_s=time.monotonic(),epoch=s['epoch'],start_us=s['start_us'],hot_path_http_packets=False,simulated=True,rf_tested=False))
        except BaseException:
            self.stop();raise
