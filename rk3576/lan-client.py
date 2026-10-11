#!/usr/bin/env python3
"""Isolated client helper on the separate x86 LAN host; portable packages only.
Password comes from stdin; stop by creating OUTPUT/stop. Owns only Xvfb :97.
"""
import os,pathlib,re,signal,subprocess,sys,time
base=pathlib.Path(os.environ['XRDP_LAB_CLIENT_BASE'])
server=os.environ['XRDP_LAB_SERVER'];user=os.environ['XRDP_LAB_USER']
assert base.is_absolute() and '..' not in base.parts and str(base)!='/'
assert re.fullmatch(r'[A-Za-z0-9_.-]+',server)
assert re.fullmatch(r'[a-z_][a-z0-9_-]*[$]?',user) and user!='root'
assert re.fullmatch(r'run-[a-z0-9-]{1,60}',sys.argv[1])
out=base/sys.argv[1];size=sys.argv[2] if len(sys.argv)>2 else '2560x1440'
assert out.parent==base and size in ['1920x1080','2560x1440','3840x2160']
assert not pathlib.Path('/tmp/.X11-unix/X97').exists()
out.mkdir(exist_ok=False);password=sys.stdin.readline().rstrip('\r\n');assert password
decoder=os.environ.get('XRDP_LAB_DECODER','openh264')
assert decoder in ['openh264','ffmpeg']
client_prefix=base/('freerdp-ffmpeg-install' if decoder=='ffmpeg' else 'freerdp-install')
env=dict(os.environ,DISPLAY=':97',LD_LIBRARY_PATH=str(client_prefix/'lib')+':'+str(base/'root/usr/lib/x86_64-linux-gnu'))
processes=[]
try:
    with (out/'xvfb.log').open('w') as xvlog,(out/'client.log').open('w') as log:
        xv=subprocess.Popen([str(base/'root/usr/bin/Xvfb'),':97','-screen','0',size+'x24','-nolisten','tcp','-ac'],env=env,stdout=xvlog,stderr=xvlog);processes.append(xv)
        for _ in range(50):
            assert xv.poll() is None,'Xvfb failed'
            if pathlib.Path('/tmp/.X11-unix/X97').exists():break
            time.sleep(.1)
        client=subprocess.Popen([str(client_prefix/'bin/xfreerdp'),'/v:'+server+':3391',
            '/u:'+user,'/from-stdin:force','/cert:tofu','/size:'+size,'/dynamic-resolution',
            '/gfx:AVC420','/network:lan','/log-level:INFO'],env=env,stdin=subprocess.PIPE,stdout=log,stderr=log)
        processes.append(client);client.stdin.write((password+'\n').encode());client.stdin.close();del password
        time.sleep(5);assert client.poll() is None,'Client startup failed'
        print('CLIENT READY '+str(out),flush=True)
        deadline=time.monotonic()+1800
        while client.poll() is None and not (out/'stop').exists() and time.monotonic()<deadline:time.sleep(.5)
        if client.poll() is None:
            subprocess.run([str(base/'test-desktop'),'close-client'],env=env,check=True)
            client.wait(timeout=10)
        print('CLIENT exit='+str(client.returncode),flush=True)
finally:
    for p in reversed(processes):
        if p.poll() is None:
            p.terminate()
            try:p.wait(timeout=10)
            except subprocess.TimeoutExpired:p.kill();p.wait()
