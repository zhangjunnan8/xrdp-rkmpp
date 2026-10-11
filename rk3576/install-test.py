#!/usr/bin/env python3
"""First installation only: isolated RK3576 service on port 3390.

Legacy lab installer: requires an explicitly configured host and existing pilot layout.
Existing pilot service, credentials and active desktop sessions are not changed.
Refuses to overwrite any existing test configuration or installation.
"""
import grp
import os
import pathlib
import pwd
import shutil
import socket
import subprocess
import sys

from lab_config import check_board, desktop_account, required, xauthority
check_board()
assert "rockchip,rk3576" in pathlib.Path("/proc/device-tree/compatible").read_bytes().decode()
build = pathlib.Path(sys.argv[1]).resolve()
lab = "--lab" in sys.argv[2:]
variant = "xrdp-rk3576-lab" if lab else "xrdp-rk3576"
port = 3391 if lab else 3390
display = 50 if lab else 30
prefix = pathlib.Path((build / "install-prefix").read_text().strip())
assert prefix.parent == pathlib.Path("/opt") / variant
assert len(prefix.name) == 40 and all(c in "0123456789abcdef" for c in prefix.name)
stage = build / "stage"
config = pathlib.Path("/etc") / variant / "xrdp"
pilot = pathlib.Path("/etc/xrdp-pilot/xrdp")
assert not config.parent.exists() and not prefix.exists()
for name in (variant, variant + "-sesman"):
    assert not pathlib.Path(f"/etc/systemd/system/{name}.service").exists()
with socket.socket() as sock:
    sock.bind(("0.0.0.0", port))
assert pathlib.Path("/etc/pam.d/xrdp-sesman").is_file()
assert pathlib.Path("/opt/xrdp-0.10.6.1/lib/xorg/modules").is_dir()
account = pwd.getpwnam("xrdp")
video = grp.getgrnam("video")
if "xrdp" not in video.gr_mem:
    subprocess.run(["usermod", "-aG", "video", "xrdp"], check=True)
shutil.copytree(stage / prefix.relative_to("/"), prefix)
config.mkdir(parents=True)
for p in pilot.iterdir():
    if p.is_file():
        shutil.copy2(p, config / p.name)

def edit_ini(path, changes):
    result, section, written = [], None, set()
    def finish():
        for k, v in changes.get(section, {}).items():
            if k not in written:
                result.append(f"{k}={v}\n")
    for line in path.read_text().splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            finish(); section = stripped[1:-1]; written = set()
        if "=" in stripped and not stripped.startswith(("#", ";")):
            k = stripped.split("=", 1)[0].strip()
            if k in changes.get(section, {}):
                line = f"{k}={changes[section][k]}\n"; written.add(k)
        result.append(line)
    finish(); path.write_text("".join(result))

logs = pathlib.Path("/var/log") / variant
logs.mkdir(mode=0o750)
os.chown(logs, account.pw_uid, account.pw_gid)
edit_ini(config / "xrdp.ini", {
    "Globals": {"port": str(port), "certificate": str(config / "cert.pem"),
                "key_file": str(config / "key.pem"), "runtime_user": "xrdp", "runtime_group": "xrdp"},
    "Logging": {"LogFile": str(logs / "xrdp.log"), "LogLevel": "INFO"},
})
edit_ini(config / "sesman.ini", {
    "Globals": {"DefaultWindowManager": str(config / "startwm.sh"),
                "ReconnectScript": str(config / "reconnectwm.sh")},
    "Sessions": {"X11DisplayOffset": str(display), "MaxSessions": "4", "Policy": "Default"},
    "Logging": {"LogFile": str(logs / "sesman.log"), "LogLevel": "INFO"},
})
# A separate sesman/socket root keeps the existing :10 desktop out of tests.
for name in ("cert.pem", "key.pem", "rsakeys.ini"):
    os.chmod(config / name, 0o640); os.chown(config / name, 0, account.pw_gid)
gfx = config / "gfx.toml"
gfx.write_text(gfx.read_text().replace('h264_encoder = "x264"', 'h264_encoder = "MPP"'))
tmp = pathlib.Path("/etc/tmpfiles.d") / (variant + ".conf")
tmp.write_text(f"d /run/{variant} 0755 root root -\n"
               f"d /run/{variant}/sockdir 3777 root xrdp -\n")
subprocess.run(["systemd-tmpfiles", "--create", str(tmp)], check=True)
units = pathlib.Path("/etc/systemd/system")
(units / (variant + "-sesman.service")).write_text(f"""[Unit]
Description=RK3576 direct MPP XRDP test session manager
After=network.target
[Service]
Type=exec
ExecStartPre=/usr/bin/systemd-tmpfiles --create {tmp}
ExecStart={prefix}/sbin/xrdp-sesman --nodaemon
Restart=on-failure
[Install]
WantedBy=multi-user.target
""")
(units / (variant + ".service")).write_text(f"""[Unit]
Description=RK3576 direct MPP XRDP on port {port}
Requires={variant}-sesman.service
After=network.target {variant}-sesman.service
[Service]
Type=exec
Environment=XRDP_MPP_QP=18
Environment=XRDP_MPP_STRICT=1
ExecStart={prefix}/sbin/xrdp --nodaemon
Restart=on-failure
[Install]
WantedBy=multi-user.target
""")
subprocess.run(["systemctl", "daemon-reload"], check=True)
subprocess.run(["systemctl", "start", variant + "-sesman", variant], check=True)
print(f"Installed isolated strict-MPP validation endpoint {socket.gethostname()}:{port}")
print("This first-test installation is not enabled on boot.")
