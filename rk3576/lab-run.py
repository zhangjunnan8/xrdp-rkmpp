#!/usr/bin/env python3
"""Unattended animation via a real AVC420 client on isolated port 3391 / :50.
Read password from stdin, never store it. Runtime output must stay outside Git.
Owns Xvfb :97 and its client only; never kills or restarts XRDP sessions.
"""
import json, os, pathlib, signal, socket, subprocess, sys, time
from lab_config import check_board, desktop_account, required, xauthority
check_board()
account = desktop_account()
build=pathlib.Path(sys.argv[1]).resolve();out=pathlib.Path(sys.argv[2]).resolve()
duration=int(sys.argv[3]) if len(sys.argv)>3 else 60
mode=sys.argv[4] if len(sys.argv)>4 else 'cycle'
size=sys.argv[5] if len(sys.argv)>5 else '2560x1440'
assert 10<=duration<=7200 and size in ['1920x1080','2560x1440','3840x2160']
assert mode in ['drag','color','mixed','cycle']
assert not pathlib.Path('/tmp/.X11-unix/X97').exists(), 'Test Xvfb display is already occupied'
out.mkdir(exist_ok=False)
password=sys.stdin.readline().rstrip('\r\n');assert password
base=pathlib.Path(required('XRDP_LAB_CLIENT_TOOLS'))
client_binary=base/'freerdp-build/client/X11/xfreerdp'
server_log=pathlib.Path('/var/log/xrdp-rk3576-lab/xrdp.log')
offset=server_log.stat().st_size
processes=[];logs=[];profile=None;client=None
def spawn(args,name,**kwargs):
    f=(out/name).open('w');logs.append(f)
    p=subprocess.Popen(args,stdout=f,stderr=f,start_new_session=True,**kwargs);processes.append(p);return p
def as_user(display,args):
    return ['runuser','-u',account.pw_name,'--','env','DISPLAY='+display,'XAUTHORITY='+xauthority(account),*map(str,args)]
def recent():
    with server_log.open() as f:f.seek(offset);return f.read()
try:
    xvfb=spawn([str(base/'root/usr/bin/Xvfb'),':97','-screen','0',size+'x24','-nolisten','tcp','-ac'],'xvfb.log')
    for _ in range(40):
        assert xvfb.poll() is None,'Xvfb failed'
        if pathlib.Path('/tmp/.X11-unix/X97').exists():break
        time.sleep(.1)
    client=spawn(as_user(':97',[client_binary,'/v:127.0.0.1:3391','/u:'+account.pw_name,'/from-stdin:force',
        '/cert:tofu','/size:'+size,'/dynamic-resolution','/gfx:AVC420','/network:lan','/log-level:INFO']),
        'client.log',stdin=subprocess.PIPE)
    client.stdin.write((password+'\n').encode());client.stdin.close();del password
    for _ in range(160):
        assert client.poll() is None,'RDP client exited before hardware frame'
        if 'first hardware frame complete' in recent():break
        time.sleep(.25)
    else:raise AssertionError('No real hardware encoded frame')
    assert 'display 50' in recent(),'Unexpected desktop; refusing to draw'
    time.sleep(3)
    profiler=pathlib.Path(__file__).with_name('profile-session.py')
    profile=spawn(['python3','-S','-u',str(profiler),str(out/'profile'),str(duration+30)],'profiler.log')
    for _ in range(80):
        assert profile.poll() is None,'Profiler failed'
        if 'READY' in (out/'profiler.log').read_text():break
        time.sleep(.1)
    else:raise AssertionError('Profiler not ready')
    print(f'LAB animation={mode} seconds={duration} size={size} out={out}',flush=True)
    app=spawn(as_user(':50',[build/'desktop-motion',str(duration),mode]),'animation.log')
    started=time.monotonic();last=-1
    while app.poll() is None:
        assert client.poll() is None,'Client disconnected during animation'
        elapsed=int(time.monotonic()-started)
        if elapsed//15!=last:
            last=elapsed//15;print(f'LAB running {elapsed}s',flush=True)
        assert elapsed<duration+15,'Animation exceeded deadline'
        time.sleep(.5)
    assert app.returncode==0
    time.sleep(2)
    (out/'profile'/'stop').touch();assert profile.wait(timeout=10)==0
    live=recent();(out/'server-live.log').write_text(live)
    assert 'switching to x264' not in live and '[ERROR]' not in live,'Live encoding error'
    # Existing X11 helper sends WM_DELETE_WINDOW so FreeRDP exits normally.
    closer=build/'test-desktop'
    subprocess.run(as_user(':97',[closer,'close-client']),check=True,stdout=subprocess.DEVNULL)
    assert client.wait(timeout=10)==0
    (out/'server.log').write_text(recent())
    print('LAB passed',flush=True)
finally:
    if profile and profile.poll() is None:
        profile.send_signal(signal.SIGTERM)
        try:profile.wait(timeout=10)
        except subprocess.TimeoutExpired:pass
    for p in reversed(processes):
        if p.poll() is None:
            os.killpg(p.pid,signal.SIGTERM)
            try:p.wait(timeout=10)
            except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait()
    for f in logs:f.close()
