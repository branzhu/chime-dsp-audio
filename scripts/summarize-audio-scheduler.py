"""Offline per-task allocation report from audio observations (not RF acceptance).

FreeRTOS counters may include ISR attribution. Named tasks are not exhaustive;
wall-time SRC/decode measurements must not be added to these CPU allocations.
"""
import argparse
import json
from pathlib import Path


def report(rows, warmup=5):
    intervals=[]
    for previous,current in zip(rows,rows[1:]):
        if previous['play_elapsed_s'] < warmup:
            continue
        a,b=previous['sample'],current['sample']
        if a['boot_id']!=b['boot_id'] or a['firmware']['elf_sha256']!=b['firmware']['elf_sha256']:
            raise ValueError('boot/image changed')
        if a['bt']['epoch']!=b['bt']['epoch'] or a['bt']['error'] or b['bt']['error']:
            continue  # Never mix fault/cleanup task allocation into steady state.
        x,y=a['scheduler'],b['scheduler']
        if not x['enabled'] or not y['enabled'] or x['counter_bits']!=32 or y['counter_bits']!=32:
            raise ValueError('unsupported scheduler counters')
        dt=y['sampled_us']-x['sampled_us']
        host=(current['host_monotonic_s']-previous['host_monotonic_s'])*1e6
        if not 0<dt<3e6 or not 0<host<3e6:
            raise ValueError('invalid/unsynchronized interval')
        old={t['name']:t for t in x['tasks']};tasks={}
        for t in y['tasks']:
            if t['name'] not in old:
                continue
            p=old[t['name']]
            if (p.get('affinity'),p['priority'])!=(t.get('affinity'),t['priority']):
                raise ValueError('task affinity/priority changed')
            delta=(t['runtime_us']-p['runtime_us'])%(1<<32)
            if delta>dt+max(50000,x['capture_span_us']+y['capture_span_us']):
                raise ValueError('implausible counter delta/task restart')
            tasks[t['name']]=dict(us=delta,affinity=t.get('affinity'),priority=t['priority'])
        intervals.append(dict(seconds=dt/1e6,tasks=tasks,capture_span_us=y['capture_span_us'],host_delta_us=host,esp_delta_us=dt))
    if not intervals:
        raise ValueError('no qualified steady intervals')
    names=set.intersection(*(set(i['tasks']) for i in intervals));seconds=sum(i['seconds'] for i in intervals)
    tasks={}
    for name in sorted(names):
        t=intervals[0]['tasks'][name]
        tasks[name]=dict(affinity=t['affinity'],priority=t['priority'],
            mean_core_percent=100*sum(i['tasks'][name]['us'] for i in intervals)/(seconds*1e6),
            max_interval_percent=max(100*i['tasks'][name]['us']/(i['seconds']*1e6) for i in intervals))
    return dict(intervals=len(intervals),steady_seconds=seconds,tasks=tasks,
        max_capture_span_us=max(i['capture_span_us'] for i in intervals),
        max_host_esp_interval_difference_us=max(abs(i['host_delta_us']-i['esp_delta_us']) for i in intervals),
        firmware=rows[-1]['sample']['firmware'],
        caveat='Task allocated time may include ISR; named tasks are not exhaustive. Not audio/RF acceptance.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('observations',type=Path);p.add_argument('--output',type=Path);p.add_argument('--warmup',type=float,default=5)
    a=p.parse_args();data=report([json.loads(s) for s in a.observations.read_text(encoding='utf-8').splitlines()],a.warmup)
    text=json.dumps(data,indent=2,ensure_ascii=False)+'\n'
    if a.output:a.output.write_text(text,encoding='utf-8')
    print(text)

