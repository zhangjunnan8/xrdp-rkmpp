#!/usr/bin/env python3
"""Promote a verified build on 3391 only, with no connected lab clients.
Port 3390/3389 services and their Xorg displays are never changed.
Backs up configuration, verifies paths, and installs immutable version prefixes.
"""
import datetime,os,pathlib,shutil,signal,socket,subprocess,sys,time
from lab_config import check_board, desktop_account, required, xauthority
check_board()
account = desktop_account()
assert b'rockchip,rk3576' in pathlib.Path('/proc/device-tree/compatible').read_bytes()
assert len(sys.argv) in [3,4]
daily=len(sys.argv)==4 and sys.argv[3]=='--daily'
assert len(sys.argv)==3 or daily
build=pathlib.Path(sys.argv[1]).resolve();xorg=pathlib.Path(sys.argv[2]).resolve()
assert 'ESTAB' not in subprocess.check_output(['ss','-tnp','( sport = :3391 )'],text=True),'Disconnect lab clients first'
def install(out,parent):
    prefix=pathlib.Path((out/'install-prefix').read_text().strip())
    assert prefix.parent==pathlib.Path(parent) and len(prefix.name)==40
    assert all(c in '0123456789abcdef' for c in prefix.name)
    assert (out/'source-commit').read_text().strip()==prefix.name
    staged=out/'stage'/prefix.relative_to('/')
    assert staged.is_dir(),staged
    if prefix.exists():
        # Existing immutable releases are reused only if every staged file matches.
        for f in staged.rglob('*'):
            if f.is_file():assert (prefix/f.relative_to(staged)).read_bytes()==f.read_bytes(),f
    else:shutil.copytree(staged,prefix)
    return prefix
prefix=install(build,'/opt/xrdp-rk3576-lab')
xprefix=install(xorg,'/opt/xrdp-rk3576-xorg')
tool_dir=pathlib.Path('/opt/xrdp-rk3576-tools')/prefix.name
tool_dir.mkdir(parents=True,exist_ok=True)
tool=tool_dir/'desktop-motion'
if tool.exists():assert tool.read_bytes()==(build/'desktop-motion').read_bytes()
else:shutil.copy2(build/'desktop-motion',tool)
command=pathlib.Path('/usr/local/bin/xrdp-motion-test')
if command.exists() or command.is_symlink():
    assert command.is_symlink() and command.resolve().is_relative_to('/opt/xrdp-rk3576-tools')
    command.unlink()
command.symlink_to(tool)
config=pathlib.Path('/etc/xrdp-rk3576-lab/xrdp');assert config.is_dir()
backup=pathlib.Path('/root')/('xrdp-lab-release-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
shutil.copytree(config,backup/'config')
units=pathlib.Path('/etc/systemd/system')
for name in ['xrdp-rk3576-lab','xrdp-rk3576-lab-sesman']:shutil.copy2(units/(name+'.service'),backup/(name+'.service'))
s=(config/'sesman.ini').read_text();lines=s.splitlines();changed=False
for i,line in enumerate(lines[:-1]):
    if line.strip()=='param=-modulepath':
        assert lines[i+1].startswith('param=');lines[i+1]='param='+str(xprefix)+'/lib/xorg/modules,/usr/lib/xorg/modules';changed=True
assert changed;(config/'sesman.ini').write_text('\n'.join(lines)+'\n')
for name,binary in [('xrdp-rk3576-lab','xrdp'),('xrdp-rk3576-lab-sesman','xrdp-sesman')]:
    p=units/(name+'.service');lines=p.read_text().splitlines()
    lines=[line for line in lines if not line.startswith('Environment=XRDP_MPP_')]
    for i,line in enumerate(lines):
        if line.startswith('ExecStart='):lines[i]=f'ExecStart={prefix}/sbin/{binary} --nodaemon'
    if binary=='xrdp':
        index=lines.index('[Service]')+1
        lines[index:index]=['Environment=XRDP_MPP_QP=18',f'Environment=XRDP_MPP_STRICT={0 if daily else 1}',
                            'Environment=XRDP_MPP_DIRECT=1','Environment=XRDP_MPP_PIPELINE=1']
    p.write_text('\n'.join(lines)+'\n')
subprocess.run(['systemctl','stop','xrdp-rk3576-lab','xrdp-rk3576-lab-sesman'],check=True)
# PAM sessions may outlive sesman. An orphan chansrv can unlink the next
# session's identically named socket on reconnect. Stop only this namespace.
for p in pathlib.Path('/proc').iterdir():
    if not p.name.isdigit():continue
    try:
        cmd=(p/'cmdline').read_bytes().split(b'\0')
        env=(p/'environ').read_bytes().split(b'\0')
        if (cmd[0].startswith(b'/opt/xrdp-rk3576-lab/') and cmd[0].endswith(b'/sbin/xrdp-chansrv')
                and f'XRDP_SOCKET_PATH=/run/xrdp-rk3576-lab/sockdir/{account.pw_uid}'.encode() in env):
            os.kill(int(p.name),signal.SIGTERM)
    except (OSError,IndexError):pass
time.sleep(1)
for p in pathlib.Path('/proc').iterdir():
    if not p.name.isdigit():continue
    try:
        cmd=(p/'cmdline').read_bytes().split(b'\0')
        if cmd[0]==b'/usr/lib/xorg/Xorg' and b':50' in cmd:
            assert b'-modulepath' in cmd and b'xrdp' in cmd[cmd.index(b'-modulepath')+1]
            os.kill(int(p.name),signal.SIGTERM)
    except (OSError,IndexError):pass
for _ in range(100):
    if not pathlib.Path('/tmp/.X11-unix/X50').exists():break
    time.sleep(.1)
else:raise RuntimeError('Lab display did not stop; refusing to start over it')
subprocess.run(['systemctl','daemon-reload'],check=True)
subprocess.run(['systemctl','start','xrdp-rk3576-lab-sesman','xrdp-rk3576-lab'],check=True)
if daily:subprocess.run(['systemctl','enable','xrdp-rk3576-lab-sesman','xrdp-rk3576-lab'],check=True)
print('Installed optimized 3391. Backup:',backup)
