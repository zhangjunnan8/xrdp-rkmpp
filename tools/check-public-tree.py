#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only release hygiene, syntax and local Markdown-link checks.

Default: tracked and non-ignored new working files. --ref: exact Git tree.
This is a targeted hygiene check, not a guarantee that all secrets are detectable.
"""
import argparse
import ast
import ipaddress
import pathlib
import re
import subprocess
import sys
from urllib.parse import unquote

ROOT = pathlib.Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ref", help="Audit this exact Git tree instead of working files")
    args = parser.parse_args()
    if args.ref:
        names = git("ls-tree", "-rz", "--name-only", args.ref).decode().split("\0")
    else:
        names = git("ls-files", "-z", "--cached", "--others", "--exclude-standard").decode().split("\0")
    files = {}
    for name in sorted(set(names) - {""}):
        p = ROOT / name
        if args.ref:
            files[name] = git("show", args.ref + ":" + name)
        elif p.is_file():
            files[name] = p.read_bytes()
    errors = []
    private_nets = tuple(ipaddress.ip_network(n) for n in ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"))
    secret_patterns = {
        "private-key marker": r"-----BEGIN (?:RSA |EC |DSA |OPENSSH |PGP )?PRIVATE KEY",
        "access-token pattern": r"(?:gh[pousr]_[A-Za-z0-9]{25,}|github_pat_[A-Za-z0-9_]{35,}|AKIA[0-9A-Z]{16}|xox[baprs]-[A-Za-z0-9-]{20,})",
        "literal credential candidate": r'''(?i)(?:password|passwd|token|secret|api_key)\s*[:=]\s*["'][^"'\r\n]{1,150}["']''',
    }
    python_count = 0
    link_count = 0
    for name, data in files.items():
        p = pathlib.PurePosixPath(name)
        if p.parts[0] in (".local", "secrets", "artifacts") or p.suffix.lower() in (".pem", ".key", ".pfx", ".p12", ".ppk", ".xwd", ".pcap", ".log", ".bundle", ".zip"):
            errors.append(name + ": private/generated file type")
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            errors.append(name + ": unexpected binary file; review manually")
            continue
        for label, pattern in secret_patterns.items():
            if re.search(pattern, text):
                errors.append(name + ": " + label)
        # The network definitions above are policy constants, not site addresses.
        if name != "tools/check-public-tree.py":
            for match in re.finditer(r"(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w.])", text):
                try:
                    ip = ipaddress.ip_address(match.group())
                except ValueError:
                    continue
                if any(ip in net for net in private_nets):
                    errors.append(name + ": RFC1918 address; replace with a role/example")
                    break
        if p.suffix == ".py":
            try:
                ast.parse(text, filename=name, feature_version=(3, 8))
                python_count += 1
            except SyntaxError as exc:
                errors.append(name + ": " + str(exc))
        if p.suffix == ".md":
            for target in re.findall(r"\]\(([^)]+)\)", text):
                target = target.strip("<>").split("#", 1)[0]
                if not target or re.match(r"^[a-zA-Z][\w+.-]*:", target):
                    continue
                resolved = (ROOT / p.parent / unquote(target)).resolve()
                try:
                    relative = resolved.relative_to(ROOT).as_posix()
                except ValueError:
                    errors.append(name + ": link outside source tree")
                    continue
                if relative not in files:
                    errors.append(name + ": missing local link " + target)
                link_count += 1
    print("files=%d python_syntax=%d local_links=%d" % (len(files), python_count, link_count))
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print("PASS: targeted tree hygiene, Python 3.8 syntax and local file links")
    return 0


if __name__ == "__main__":
    sys.exit(main())
