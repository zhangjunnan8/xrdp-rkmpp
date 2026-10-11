#!/usr/bin/env python3
"""Analyze synthetic phase intervals. No third-party dependencies required."""
import json,re,pathlib,collections,statistics,sys
p=pathlib.Path(sys.argv[1]);profile=p/'profile'
meta=json.loads((profile/'metadata.json').read_text())
samples=[json.loads(s) for s in (profile/'samples.jsonl').read_text().splitlines()]
events=[]
for line in (profile/'trace.txt').read_text().splitlines():
    m=re.search(r'(\S+)-(\d+)\s+\[(\d+)\]\s+\S+\s+(\d+\.\d+): (\w+): (.*)',line)
    if m:events.append(dict(tid=int(m[2]),t=float(m[4]),event=m[5],args={k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',m[6])}))
events.sort(key=lambda e:e['t'])
stacks=collections.defaultdict(list);calls=collections.defaultdict(list);frames={}
for e in events:
    n=e['event']
    if n.endswith('_in'):stacks[(e['tid'],n[:-3])].append(e)
    elif n.endswith('_out'):
        q=stacks[(e['tid'],n[:-4])]
        if q:
            a=q.pop();calls[n[:-4]].append(dict(start=a['t'],end=e['t'],ms=(e['t']-a['t'])*1000,args=a['args'],rc=e['args'].get('rc')))
    elif n in ['frame_start','frame_end','ack']:
        f=frames.setdefault(e['args']['frame'],{});f[n]=e['t'];f[n+'_args']=e['args']
def stats(values):
    if not values:return {}
    values=sorted(values);i=(len(values)-1)*.95;lo=int(i);hi=min(lo+1,len(values)-1)
    return dict(n=len(values),mean=round(statistics.mean(values),4),p50=round(statistics.median(values),4),p95=round(values[lo]+(i-lo)*(values[hi]-values[lo]),4),max=round(max(values),4))
text=(p/'animation.log').read_text()
phases=[(float(t),mode) for t,mode in re.findall(r'PHASE mono=([\d.]+) mode=(\w+)',text)]
end=float(re.search(r'ANIMATION done mono=([\d.]+)',text)[1])
report={'source':meta,'counts':dict(collections.Counter(e['event'] for e in events)),'phases':[]}
report['resources']={}
for role,pid in meta['roles'].items():
    values=[s['processes'][str(pid)] for s in samples if str(pid) in s['processes']]
    if values:
        report['resources'][role]={key:{'first':v[0],'last':v[-1],'min':min(v),'max':max(v)}
            for key in ['rss','fds'] if (v:=[s[key] for s in values if key in s])}
pending=set();max_pending=0
for event in events:
    if event['event']=='frame_start':pending.add(event['args']['frame']);max_pending=max(max_pending,len(pending))
    elif event['event']=='ack':pending={f for f in pending if f>event['args']['frame']}
report['max_observed_unacknowledged_frames']=max_pending
for index,(start,mode) in enumerate(phases):
    stop=phases[index+1][0] if index+1<len(phases) else end
    begin=start+2 # Drop startup/phase-transition transient consistently.
    selected={name:[c for c in v if begin<=c['start']<stop] for name,v in calls.items()}
    part={'mode':mode,'seconds':round(stop-begin,3),'encoded_fps':round(len(selected['encode'])/(stop-begin),3),'ms':{k:stats([c['ms'] for c in v]) for k,v in selected.items()},'cpu':{},'tx_mbps':[]}
    part['ack_ms']=stats([(f['ack']-f['frame_end'])*1000 for f in frames.values() if 'ack'in f and 'frame_end'in f and begin<=f['frame_end']<stop])
    cpu=collections.defaultdict(list)
    for a,b in zip(samples,samples[1:]):
        if not begin<=a['mono']<b['mono']<=stop:continue
        dt=b['mono']-a['mono']
        for name,pid in meta['roles'].items():
            x=a['processes'].get(str(pid));y=b['processes'].get(str(pid))
            if x and y:cpu[name].append((y['user']+y['sys']-x['user']-x['sys'])/meta['hz']/dt*100)
        part['tx_mbps'].append((int(b['network']['tx_bytes'])-int(a['network']['tx_bytes']))*8/dt/1e6)
    part['cpu']={k:stats(v) for k,v in cpu.items()};part['tx_mbps']=stats(part['tx_mbps'])
    report['phases'].append(part)
report['trace_stats']=json.loads((profile/'trace-stats.json').read_text())
report['all_encode_return_codes']=dict(collections.Counter(c['rc'] for c in calls['encode']))
(p/'analysis.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k not in ['source','trace_stats']},indent=2))
