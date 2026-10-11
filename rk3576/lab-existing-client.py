#!/usr/bin/env python3
"""Run synthetic workload only on :50 with an already connected lab client.
The client address must be explicitly selected by XRDP_LAB_CLIENT for profiling.
No service restarts; suitable for a separate LAN decoding host.
"""
import json,os,pathlib,signal,socket,subprocess,sys,time
from lab_config import check_board, desktop_account, required, xauthority
check_board()
account = desktop_account()
build=pathlib.Path(sys.argv[1]).resolve();out=pathlib.Path(sys.argv[2]).resolve()
seconds=int(sys.argv[3]) if len(sys.argv)>3 else 60
mode=sys.argv[4] if len(sys.argv)>4 else 'cycle'
assert 10<=seconds<=7000 and mode in ['cycle','drag','color','mixed']
out.mkdir(exist_ok=False);processes=[]
log=pathlib.Path('/var/log/xrdp-rk3576-lab/xrdp.log');offset=log.stat().st_size
try:
    with (out/'profiler.log').open('w') as f:
        profiler=subprocess.Popen(['python3','-S','-u',str(pathlib.Path(__file__).with_name('profile-session.py')),
            str(out/'profile'),str(seconds+20)],stdout=f,stderr=f);processes.append(profiler)
        for _ in range(100):
            assert profiler.poll() is None,'Profiler failed'
            if 'READY' in (out/'profiler.log').read_text():break
            time.sleep(.1)
        else:raise AssertionError('Profiler not ready')
        with (out/'animation.log').open('w') as app_log:
            app=subprocess.Popen(['runuser','-u',account.pw_name,'--','env','DISPLAY=:50',
                'XAUTHORITY='+xauthority(account),str(build/'desktop-motion'),str(seconds),mode],
                stdout=app_log,stderr=app_log);processes.append(app)
            started=time.monotonic();last=-1
            while app.poll() is None:
                elapsed=int(time.monotonic()-started)
                if elapsed//15!=last:last=elapsed//15;print(f'LAB {mode} {elapsed}s',flush=True)
                assert elapsed<seconds+15
                time.sleep(.5)
            assert app.returncode==0
        time.sleep(2);(out/'profile'/'stop').touch();assert profiler.wait(timeout=10)==0
    with log.open() as f:f.seek(offset);live=f.read()
    (out/'server-live.log').write_text(live)
    assert 'switching to x264' not in live and '[ERROR]' not in live
    subprocess.run(['python3','-S',str(pathlib.Path(__file__).with_name('analyze-session.py')),str(out)],check=True,stdout=subprocess.DEVNULL)
    analysis=json.loads((out/'analysis.json').read_text())
    assert analysis['counts'].get('encode_out',0)>seconds*5,'No useful encoded workload; benchmark invalid'
    assert all(int(code)==0 for code in analysis['all_encode_return_codes']),'Encoder returned an error'
    for stats in analysis['trace_stats'].values():
        for line in stats.splitlines():
            name,_,value=line.partition(':')
            if name in ['overrun','commit overrun','dropped events']:assert int(value)==0,'Trace loss; benchmark invalid'
    print('LAB passed '+str(out),flush=True)
finally:
    for p in reversed(processes):
        if p.poll() is None:
            p.send_signal(signal.SIGTERM)
            try:p.wait(timeout=10)
            except subprocess.TimeoutExpired:p.kill();p.wait()
