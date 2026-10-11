# SPDX-License-Identifier: Apache-2.0
"""Explicit site identity for fixed-port lab helpers. No private site defaults."""
import os
import pathlib
import pwd
import socket


def required(name):
    value = os.environ.get(name, "")
    if not value or any(c in value for c in "\r\n\0"):
        raise RuntimeError("Set " + name + " explicitly for the isolated lab")
    return value


def check_board():
    if os.geteuid() != 0 or socket.gethostname() != required("XRDP_LAB_HOSTNAME"):
        raise RuntimeError("Expected root on the explicitly configured lab host")
    if b"rockchip,rk3576" not in pathlib.Path("/proc/device-tree/compatible").read_bytes():
        raise RuntimeError("Expected an RK3576 board")


def desktop_account():
    account = pwd.getpwnam(required("XRDP_LAB_USER"))
    if account.pw_uid == 0:
        raise RuntimeError("Use a non-root desktop account")
    return account


def xauthority(account):
    return str(pathlib.Path(account.pw_dir) / ".Xauthority")
