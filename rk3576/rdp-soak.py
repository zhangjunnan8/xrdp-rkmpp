#!/usr/bin/env python3
"""Root-only isolated :30 / :98 RDP test. Password is read once from stdin.

Requires port 3390 test service and Xvfb :98. Never targets the pilot on 3389.
Synthetic desktop only; generated screenshots/logs must stay outside Git.
"""
import json
import os
import pathlib
import signal
import socket
import subprocess
import sys
import time

from lab_config import check_board, desktop_account, required, xauthority
check_board()
account = desktop_account()
desktop = str(pathlib.Path(sys.argv[1]).resolve())
out = pathlib.Path(sys.argv[2]).resolve()
duration = int(sys.argv[3]) if len(sys.argv) > 3 else 300
assert 30 <= duration <= 1800
out.mkdir(exist_ok=False)
password = sys.stdin.readline().rstrip("\r\n")
assert password
client_binary = str(pathlib.Path(required("XRDP_LAB_CLIENT_TOOLS"))/"freerdp-build/client/X11/xfreerdp")
server_log = pathlib.Path("/var/log/xrdp-rk3576/xrdp.log")
offset = server_log.stat().st_size
client = app = None
samples = []
try:
    with (out / "client.log").open("w") as log:
        client = subprocess.Popen(["runuser", "-u", account.pw_name, "--", "env", "DISPLAY=:98",
            client_binary, "/v:127.0.0.1:3390", "/u:"+account.pw_name, "/from-stdin:force", "/cert:tofu",
            "/size:1920x1080", "/dynamic-resolution", "/gfx:AVC420", "/network:lan", "/log-level:INFO"],
            stdin=subprocess.PIPE, stdout=log, stderr=log, start_new_session=True)
        client.stdin.write((password+"\n").encode()); client.stdin.close(); del password
        for _ in range(80):
            assert client.poll() is None, "RDP client exited during startup"
            with server_log.open("r") as f:
                f.seek(offset); text = f.read()
            if "first hardware frame complete" in text:
                break
            time.sleep(0.25)
        else:
            raise AssertionError("No completed MPP frame in real RDP session")
        assert "display 30" in text
        main = int(subprocess.check_output(["systemctl", "show", "-p", "MainPID", "--value", "xrdp-rk3576"]))
        peers = []
        for p in pathlib.Path("/proc").iterdir():
            if not p.name.isdigit(): continue
            try:
                stat = (p / "stat").read_text().rsplit(") ",1)[1].split()
                if int(stat[1]) == main: peers.append(int(p.name))
            except (OSError, IndexError): pass
        assert len(peers) == 1, peers
        pid = peers[0]
        devices = []
        for p in pathlib.Path(f"/proc/{pid}/fd").iterdir():
            try:
                target = os.readlink(p)
                if "/dev/mpp_service" in target: devices.append(target)
            except OSError: pass
        assert devices, "Session does not hold the MPP device"
        app_log = (out / "desktop.log").open("w")
        app = subprocess.Popen(["runuser", "-u", account.pw_name, "--", "env", "DISPLAY=:30",
            "XAUTHORITY="+xauthority(account), desktop, str(duration)],
            stdout=app_log, stderr=app_log, start_new_session=True)
        start = time.monotonic(); next_resize = 60; next_sample = 0; resize_count = 0
        print(f"SOAK started seconds={duration} xrdp_pid={pid} device={devices[0]}", flush=True)
        while time.monotonic()-start < duration:
            elapsed = time.monotonic()-start
            assert client.poll() is None, "RDP client disconnected unexpectedly"
            if elapsed >= next_sample:
                status = pathlib.Path(f"/proc/{pid}/status").read_text().splitlines()
                rss = next(int(x.split()[1]) for x in status if x.startswith("VmRSS:"))
                sample = dict(seconds=round(elapsed,1), rss_kb=rss,
                              fds=len(list(pathlib.Path(f"/proc/{pid}/fd").iterdir())))
                samples.append(sample); print(json.dumps(sample), flush=True); next_sample += 30
            if elapsed >= next_resize:
                size = ["1280", "720"] if resize_count % 2 == 0 else ["1920", "1080"]
                subprocess.run(["runuser", "-u", account.pw_name, "--", "env", "DISPLAY=:98",
                                desktop, "resize-client", *size], check=True)
                resize_count += 1; next_resize += 60
            time.sleep(0.5)
        assert app.wait(timeout=10) == 0
        app_log.close()
        with server_log.open("r") as f:
            f.seek(offset); running_text = f.read()
        (out / "samples.json").write_text(json.dumps(samples,indent=2)+"\n")
        # Check the live session separately from expected transport shutdown.
        assert "switching to x264" not in running_text and "[ERROR]" not in running_text
        subprocess.run(["runuser", "-u", account.pw_name, "--", "env", "DISPLAY=:98",
                        desktop, "close-client"], check=True)
        client.wait(timeout=10)
        with server_log.open("r") as f:
            f.seek(offset); text = f.read()
        (out / "server.log").write_text(text)
        assert "switching to x264" not in text
        assert text.count("first hardware frame complete") >= resize_count+1
        print(f"SOAK passed duration={duration} resizes={resize_count}", flush=True)
finally:
    for process in (app, client):
        if process is not None and process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL); process.wait()
