"""Profile only the isolated 3391 / :50 benchmark; remove own probes in finally.
Never attach to the active Windows session. Does not change XRDP services.
"""
import os, sys, time, json, pathlib, subprocess, signal, re, socket, ipaddress, pwd
from lab_config import check_board, desktop_account, required, xauthority
check_board()
account = desktop_account()
assert len(sys.argv) == 3, "profile-session.py NEW_OUTPUT_DIRECTORY SECONDS"
duration=int(sys.argv[2]); assert 5 <= duration <= 7200
connections=subprocess.check_output(["ss","-tnp","( sport = :3391 )"],text=True)
client_ip=str(ipaddress.IPv4Address(required('XRDP_LAB_CLIENT')))
lines=[x for x in connections.splitlines() if 'ESTAB' in x]
assert len(lines)==1 and lines[0].split()[4].startswith(client_ip+':'), 'Unexpected lab client'
peers=re.findall(r"pid=(\d+)",connections); assert len(peers)==1
peer=int(peers[0])
xpid=None;wm=None
for process in pathlib.Path("/proc").iterdir():
    if not process.name.isdigit(): continue
    try:
        cmd=(process/"cmdline").read_bytes().split(b"\0")
        if b":50" in cmd and any(b"Xorg" in x for x in cmd): xpid=int(process.name)
        if cmd and cmd[0].endswith(b"xfwm4") and any(x in (process/"environ").read_bytes().split(b'\0') for x in [b'DISPLAY=:50',b'DISPLAY=:50.0']): wm=int(process.name)
    except OSError: pass
assert xpid is not None
# A sesman restart can leave an orphan :50 and create the next client on :51.
# Refuse to measure an animation on a different X server from the RDP connection.
session_log=pathlib.Path('/var/log/xrdp-rk3576-lab/xrdp.log').read_text()
connected=re.findall(r'xrdp_pid='+str(peer)+r' connected to Xorg_pid=(\d+)',session_log)
assert connected and int(connected[-1])==xpid,'Connected lab client is not on :50; clean up the lab session first'
connection_log=session_log[session_log.rindex('xrdp_pid='+str(peer)+' connected to'):]
assert 'first hardware frame complete' in connection_log and 'switching to x264' not in connection_log,'Current connection has not demonstrated hardware encoding'


root = pathlib.Path('/sys/kernel/debug/tracing')
tag = 'cbmeasure_' + time.strftime('%H%M%S')
out = pathlib.Path(sys.argv[1]).resolve()
out.mkdir(mode=0o700)
inst = root / 'instances' / tag
group = tag
xorg = next(line.split()[-1] for line in pathlib.Path(f'/proc/{xpid}/maps').read_text().splitlines() if line.endswith('/libxorgxrdp.so'))
xrdp = os.readlink(f'/proc/{peer}/exe')
mpp=next(line.split()[-1] for line in pathlib.Path(f'/proc/{peer}/maps').read_text().splitlines() if 'librockchip_mpp.so' in line)
targets = [xpid, peer] + ([wm] if wm else [])
registered = []
stop = False
def stopping(*args):
    global stop
    stop = True
signal.signal(signal.SIGTERM, stopping)
signal.signal(signal.SIGINT, stopping)

def read(p):
    try: return pathlib.Path(p).read_text().strip()
    except (OSError, ValueError): return None

def write(p, s):
    pathlib.Path(p).write_text(str(s))

def control(s):
    fd=os.open(root/'uprobe_events',os.O_WRONLY|os.O_APPEND)
    try: os.write(fd,s.encode())
    finally: os.close(fd)

def add(kind, name, file, offset, args=''):
    control(f'{kind}:{group}/{name} {file}:0x{offset:x} {args}\n')
    registered.append(name)

def stat(p):
    s=read(p)
    if not s: return None
    a=s[s.rfind(')')+2:].split()
    return {'name': s[s.find('(')+1:s.rfind(')')], 'user':int(a[11]), 'sys':int(a[12]),
            'rss':int(a[21]), 'cpu':int(a[36]), 'state':a[0]}

def snapshot():
    d={'mono':time.monotonic(), 'wall':time.time(), 'processes':{}}
    for pid in targets:
        s=stat(f'/proc/{pid}/stat')
        if s is None: continue
        s['fds']=len(list(pathlib.Path(f'/proc/{pid}/fd').iterdir()))
        s['threads']={}
        for t in pathlib.Path(f'/proc/{pid}/task').iterdir():
            v=stat(t/'stat')
            if v:
                v['schedstat']=read(t/'schedstat')
                s['threads'][t.name]=v
        d['processes'][str(pid)]=s
    d['cpu']=[line for line in read('/proc/stat').splitlines() if line.startswith('cpu')]
    d['gpu']=read('/sys/class/devfreq/27800000.gpu/load')
    d['irq']=[line for line in read('/proc/interrupts').splitlines() if 'rkvenc' in line]
    d['network']={name:read('/sys/class/net/end1/statistics/'+name) for name in ['rx_bytes','tx_bytes','rx_dropped','tx_dropped','rx_errors','tx_errors']}
    d['freq']={p.parent.name:read(p) for p in pathlib.Path('/sys/devices/system/cpu/cpufreq').glob('policy*/scaling_cur_freq')}
    d['temp']={p.parent.name:read(p) for p in pathlib.Path('/sys/class/thermal').glob('thermal_zone*/temp')}
    d['mpp']=read('/proc/mpp_service/rkvenc-core0/sessions-info')
    return d

try:
    inst.mkdir()
    write(inst/'tracing_on','0')
    write(inst/'trace_clock','mono')
    write(inst/'buffer_size_kb','4096')
    def offset(file, symbol):
        # Resolve symbol virtual address to the backing ELF file offset.
        symbols=subprocess.check_output(['nm',file],text=True,stderr=subprocess.DEVNULL)
        if not symbols.strip(): symbols=subprocess.check_output(['nm','-D',file],text=True)
        match=re.search(r'^([0-9a-fA-F]+) [tT] '+re.escape(symbol)+r'$',symbols,re.M)
        if not match:
            symbols=subprocess.check_output(['nm','-D',file],text=True)
            match=re.search(r'^([0-9a-fA-F]+) [tT] '+re.escape(symbol)+r'$',symbols,re.M)
        assert match,(file,symbol)
        address=int(match[1],16)
        for line in subprocess.check_output(['readelf','-Wl',file],text=True).splitlines():
            words=line.split()
            if words and words[0]=='LOAD':
                off,va,size=int(words[1],16),int(words[2],16),int(words[4],16)
                if va<=address<va+size:return address-va+off
        raise AssertionError((file,symbol,'not in a load segment'))
    for name,file,symbol,args in [
        ('capture',xorg,'rdpCapture',''),
        ('nv12',xorg,'a8r8g8b8_to_nv12_709fr_box','width=%x6:s32 height=%x7:s32'),
        ('encode',xrdp,'xrdp_encoder_mpp_encode','width=%x4:s32 height=%x5:s32'),
        ('sync_begin',mpp,'mpp_buffer_sync_begin_f',''),
        ('sync_end',mpp,'mpp_buffer_sync_end_f',''),
    ]:
        off=offset(file,symbol)
        add('p',name+'_in',file,off,args)
        add('r',name+'_out',file,off,'rc=$retval:s32')
    add('p','frame_end',xrdp,offset(xrdp,'xrdp_egfx_frame_end'),'frame=%x1:s32')
    add('p','frame_start',xrdp,offset(xrdp,'xrdp_egfx_frame_start'),'frame=%x1:s32')
    add('p','ack',xrdp,offset(xrdp,'xrdp_mm_egfx_frame_ack'),'queue=%x1:u32 frame=%x2:s32 decoded=%x3:s32')
    if re.search(r' [tT] mpp_release_input$',subprocess.check_output(['nm',xrdp],text=True),re.M):
        add('p','input_ready',xrdp,offset(xrdp,'mpp_release_input'))
    tids=[]
    for pid in [xpid,peer]:
        tids += [t.name for t in pathlib.Path(f'/proc/{pid}/task').iterdir()]
    for name in registered:
        ev=inst/'events'/group/name
        write(ev/'filter',' || '.join('common_pid == '+t for t in tids))
        write(ev/'enable','1')
    metadata={'tag':tag,'directory':str(out),'start_wall':time.time(),'start_mono':time.monotonic(),
              'targets':targets,'roles':{'Xorg':xpid,'xrdp':peer,'xfwm4':wm},'traced_tids':tids,'hz':os.sysconf('SC_CLK_TCK'),'probes':registered,
              'xrdp':xrdp,'xorg':xorg,'duration_limit':duration,
              'harness_commit':subprocess.check_output(['runuser','-u',pwd.getpwuid(pathlib.Path(__file__).stat().st_uid).pw_name,'--','git','-C',str(pathlib.Path(__file__).resolve().parent),'rev-parse','HEAD'],text=True).strip()}
    write(out/'metadata.json',json.dumps(metadata,indent=2))
    write(inst/'tracing_on','1')
    print('READY '+str(out),flush=True)
    start=time.monotonic()
    with (out/'samples.jsonl').open('w') as f:
        n=0
        while not stop and time.monotonic()-start < duration and not (out/'stop').exists():
            d=snapshot()
            if n%2==0:
                d['tcp']=subprocess.run(['ss','-tinp','( sport = :3391 )'],capture_output=True,text=True).stdout
            f.write(json.dumps(d)+'\n');f.flush()
            n+=1
            if n%10==0:
                print('SAMPLING seconds='+str(n)+' gpu='+str(d['gpu'])+' mpp='+str(d['mpp'].splitlines()[-1]),flush=True)
            time.sleep(max(0,start+n-time.monotonic()))
    write(inst/'tracing_on','0')
    write(out/'trace.txt',read(inst/'trace'))
    stats={p.parent.name:read(p) for p in (inst/'per_cpu').glob('cpu*/stats')}
    write(out/'trace-stats.json',json.dumps(stats,indent=2))
    write(out/'end.json',json.dumps({'mono':time.monotonic(),'wall':time.time()},indent=2))
    print('SAVED '+str(out),flush=True)
finally:
    if inst.exists():
        write(inst/'tracing_on','0')
        for name in registered:
            p=inst/'events'/group/name/'enable'
            if p.exists(): write(p,'0')
        inst.rmdir()
    for name in reversed(registered):
        control(f'-:{group}/{name}\n')
    print('CLEANUP complete',flush=True)
