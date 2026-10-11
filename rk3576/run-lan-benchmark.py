#!/usr/bin/env python3
"""One command from the development machine; uses existing SSH keys/known_hosts.
Requires an explicitly configured LAN client and an isolated board service on 3391.
No passwords are saved. Never restarts any XRDP service or changes the live port.
"""
import argparse,getpass,ipaddress,json,pathlib,re,shlex,sys,time
import paramiko
p=argparse.ArgumentParser()
p.add_argument('build',help='Absolute board build directory')
p.add_argument('label',help='Unique lowercase run label')
p.add_argument('--config',type=pathlib.Path,required=True,help='Private site JSON; see examples/lab.example.json')
p.add_argument('--seconds',type=int,default=60)
p.add_argument('--mode',choices=['cycle','drag','color','mixed'],default='cycle')
p.add_argument('--size',choices=['1920x1080','2560x1440','3840x2160'],default='2560x1440')
p.add_argument('--resize-soak',action='store_true',help='Five consecutive phases with dynamic 1440p/1080p/4K/1080p/1440p resize; seconds per phase')
p.add_argument('--password-stdin',action='store_true')
p.add_argument('--output',type=pathlib.Path,default=pathlib.Path('.local/validation'))
a=p.parse_args()
assert re.fullmatch('[a-z0-9-]{1,60}',a.label) and 10<=a.seconds<=1700
assert not a.resize_soak or a.seconds<=300
cfg=json.loads(a.config.read_text(encoding="utf-8"))
keys={'board_host','board_hostname','board_repo','board_build_root','board_results_root',
      'client_host','client_user','client_ip','client_base','rdp_user'}
if set(cfg)!=keys or any(not isinstance(v,str) or not v or any(c in v for c in '\r\n\0') for v in cfg.values()):
    p.error('Site configuration must contain exactly the nonempty fields in lab.example.json')
for key in ['board_repo','board_build_root','board_results_root','client_base']:
    path=pathlib.PurePosixPath(cfg[key])
    if not path.is_absolute() or '..' in path.parts or path==pathlib.PurePosixPath('/'):
        p.error(key+' must be an absolute, non-root directory without parent traversal')
    cfg[key]=str(path)
if not re.fullmatch(r'[A-Za-z0-9_.-]+',cfg['board_host']):
    p.error('board_host must be an IPv4 address or DNS name')
for key in ['client_user','rdp_user']:
    if not re.fullmatch(r'[a-z_][a-z0-9_-]*[$]?',cfg[key]) or cfg[key]=='root':
        p.error(key+' must be a non-root Linux account')
cfg['client_ip']=str(ipaddress.IPv4Address(cfg['client_ip']))
build_path=pathlib.PurePosixPath(a.build)
if not build_path.is_absolute() or '..' in build_path.parts or pathlib.PurePosixPath(cfg['board_build_root']) not in build_path.parents:
    p.error('build must be a child of board_build_root')
board_env={'XRDP_LAB_HOSTNAME':cfg['board_hostname'],'XRDP_LAB_USER':cfg['rdp_user'],
           'XRDP_LAB_CLIENT':cfg['client_ip']}
client_env={'XRDP_LAB_DECODER':'ffmpeg','XRDP_LAB_CLIENT_BASE':cfg['client_base'],
            'XRDP_LAB_SERVER':cfg['board_host'],'XRDP_LAB_USER':cfg['rdp_user']}
password=sys.stdin.readline().rstrip('\r\n') if a.password_stdin else getpass.getpass('RDP password: ')
assert password
clients=[];owned=False;sftp=None;channel=None
remote_client=cfg['client_base']+'/run-'+a.label
remote_board=cfg['board_results_root']+'/bench-'+a.label
local=a.output/a.label;local.mkdir(parents=True,exist_ok=False)
def connect(host,user):
    c=paramiko.SSHClient();c.load_system_host_keys();c.connect(host,username=user,timeout=12)
    clients.append(c);return c
def command(c,args,env=None):
    prefix=['env',*[f'{k}={v}' for k,v in (env or {}).items()]] if env else []
    ch=c.get_transport().open_session();ch.set_combine_stderr(True)
    ch.exec_command(shlex.join(prefix+list(args)));return ch
def drain(ch,timeout):
    deadline=time.monotonic()+timeout
    while not ch.exit_status_ready() or ch.recv_ready():
        if ch.recv_ready():print(ch.recv(65536).decode(errors='replace'),end='',flush=True)
        else:time.sleep(.1)
        if time.monotonic()>deadline:raise TimeoutError('Remote run exceeded deadline')
    return ch.recv_exit_status()
try:
    board=connect(cfg['board_host'],'root');client=connect(cfg['client_host'],cfg['client_user'])
    # SSHClient rejects unknown/mismatched host keys; no shared fingerprint in source.
    identity=command(board,['python3','-S','-c','from lab_config import check_board, desktop_account; check_board(); desktop_account()'],
                     dict(board_env,PYTHONPATH=cfg['board_repo']+'/rk3576'))
    assert drain(identity,10)==0,'Board identity/configuration check failed'
    for c,path in [(board,remote_board),(client,remote_client)]:
        check=c.open_sftp()
        try:check.stat(path)
        except FileNotFoundError:pass
        else:raise FileExistsError(path)
        finally:check.close()
    sftp=client.open_sftp()
    sftp.put(str(pathlib.Path(__file__).with_name('lan-client.py')),cfg['client_base']+'/lan-client.py')
    channel=command(client,['python3','-u',cfg['client_base']+'/lan-client.py','run-'+a.label,'3840x2160' if a.resize_soak else a.size],client_env)
    owned=True;channel.sendall((password+'\n').encode());channel.shutdown_write();del password
    deadline=time.monotonic()+30;ready=''
    while 'CLIENT READY' not in ready:
        if channel.recv_ready():
            part=channel.recv(65536).decode(errors='replace');ready+=part;print(part,end='',flush=True)
        elif channel.exit_status_ready():raise RuntimeError('Client startup failed')
        else:time.sleep(.1)
        if time.monotonic()>deadline:raise TimeoutError('Client did not become ready')
    time.sleep(3)
    download=board.open_sftp()
    sizes=['2560x1440','1920x1080','3840x2160','1920x1080','2560x1440'] if a.resize_soak else [a.size]
    for index,size in enumerate(sizes):
        dest=local/str(index) if a.resize_soak else local
        if a.resize_soak:
            dest.mkdir();w,h=size.split('x')
            resize=command(client,[cfg['client_base']+'/test-desktop','resize-client',w,h],{'DISPLAY':':97'})
            assert drain(resize,10)==0;time.sleep(4)
        remote=remote_board+'-'+str(index) if a.resize_soak else remote_board
        print('RUN '+str(index)+' '+size,flush=True)
        run=command(board,['python3','-S','-u',cfg['board_repo']+'/rk3576/lab-existing-client.py',a.build,remote,str(a.seconds),a.mode],board_env)
        rc=drain(run,a.seconds+60)
        for name in ['analysis.json','animation.log','server-live.log']:
            try:download.get(remote+'/'+name,str(dest/name))
            except FileNotFoundError:pass
        assert rc==0,'Benchmark failed; inspect remote logs'
        assert 'screen='+size in (dest/'animation.log').read_text(),'RDP display size did not follow resize'
    download.close();print('RESULT '+str(local.resolve()))
finally:
    if owned and sftp:
        try:
            with sftp.open(remote_client+'/stop','w'):pass
            if channel:drain(channel,20)
        except (OSError,TimeoutError) as e:print('Client cleanup: '+str(e),file=sys.stderr)
    if sftp:sftp.close()
    for c in clients:c.close()
