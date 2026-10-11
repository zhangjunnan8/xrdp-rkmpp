#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""CMake's private build helper. Writes only inside its build directory."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def run(command, cwd=None, env=None, log=None):
    print('+', shlex.join([str(x) for x in command]), flush=True)
    if log is None:
        subprocess.run(command, cwd=cwd, env=env, check=True)
    else:
        with log.open('w') as output:
            subprocess.run(command, cwd=cwd, env=env, stdout=output,
                           stderr=subprocess.STDOUT, check=True)


def git(args, directory, *command):
    return subprocess.check_output([args.git, '-C', str(directory), *command],
                                   text=True).strip()


def inside(root, path):
    resolved = path.resolve()
    if resolved == root or root not in resolved.parents:
        raise RuntimeError('Generated output must be a child of the build directory: ' + str(path))
    return resolved


def fetch(args):
    source = inside(args.root, args.upstream)
    marker = source / '.xrdp-rkmpp-fetch'
    if source.exists() and any(source.iterdir()) and not marker.is_file():
        raise RuntimeError('Refusing to reuse an unmarked dependency directory: ' + str(source))
    source.mkdir(parents=True, exist_ok=True)
    marker.write_text(args.revision + '\n', encoding='utf-8')
    if not (source / '.git').exists():
        run([args.git, 'init', str(source)])
        run([args.git, '-C', str(source), 'remote', 'add', 'origin', args.repository])
    if git(args, source, 'remote', 'get-url', 'origin') != args.repository:
        raise RuntimeError('Dependency repository URL changed; use a new build directory')
    # Fetch the immutable commit itself, without the full upstream history.
    run([args.git, '-C', str(source), 'fetch', '--depth=1', 'origin', args.revision])
    run([args.git, '-C', str(source), 'checkout', '--detach', args.revision])
    run([args.git, '-C', str(source), 'submodule', 'update', '--init', '--recursive', '--depth=1'])


def prepare(args):
    if git(args, args.upstream, 'rev-parse', 'HEAD') != args.revision:
        raise RuntimeError('Upstream checkout does not match the pinned commit')
    if git(args, args.upstream, 'status', '--porcelain', '--untracked-files=no'):
        raise RuntimeError('Upstream checkout has tracked changes; inspect it before rebuilding')
    submodules = git(args, args.upstream, 'submodule', 'status', '--recursive')
    # Preserve the leading status character; strip() above removes only the first
    # space for an initialized module. +/-/U still indicate a bad checkout.
    if any(line.startswith(('-', '+', 'U')) for line in submodules.splitlines()):
        raise RuntimeError('Upstream submodules do not match their pinned gitlinks')
    if args.kind == 'xrdp':
        for name in ('libpainter/configure.ac', 'librfxcodec/configure.ac', 'ulalaca/Makefile.am'):
            if not (args.upstream / name).is_file():
                raise RuntimeError('Missing pinned XRDP submodule: ' + name)

    work = inside(args.root, args.work)
    marker = work / '.xrdp-rkmpp-managed'
    if work.exists():
        if any(work.iterdir()) and not marker.is_file():
            raise RuntimeError('Refusing to replace an unmarked build tree: ' + str(work))
        shutil.rmtree(work)
    # Create the ownership marker before copying so an interrupted configure can
    # safely retry. Never patch or delete the pristine Git checkout itself.
    work.mkdir(parents=True)
    marker.write_text(args.kind + '\n', encoding='utf-8')
    for source in args.upstream.iterdir():
        if source.name in ('.git', '.xrdp-rkmpp-fetch'):
            continue
        target = work / source.name
        if source.is_dir():
            shutil.copytree(source, target, ignore=shutil.ignore_patterns('.git'), symlinks=True)
        else:
            shutil.copy2(source, target, follow_symlinks=False)
    patch = 'prepare.py' if args.kind == 'xrdp' else 'prepare-xorg.py'
    run([sys.executable, '-S', str(args.repo / 'rk3576' / patch), str(work)])
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'upstream-commit').write_text(args.revision + '\n', encoding='utf-8')


def configure(args):
    # A failed reconfigure must not leave an old installation manifest looking
    # like the output of the current source/settings.
    manifest = args.root / 'install-env.sh'
    if manifest.exists() or manifest.is_symlink():
        manifest.unlink()
    prepare(args)
    env = os.environ.copy()
    env.update(CC=args.cc, CXX=args.cxx, CFLAGS=args.cflags,
               CXXFLAGS=args.cxxflags, LDFLAGS=args.ldflags)
    run(['autoreconf', '-fi'], cwd=args.work, env=env, log=args.output / 'autoreconf.log')
    command = ['sh', './configure', '--prefix=' + args.prefix]
    if args.kind == 'xrdp':
        command += ['--sysconfdir=/etc/xrdp-rkmpp', '--localstatedir=/var',
                    '--runstatedir=/run/xrdp-rkmpp', '--with-socketdir=/run/xrdp-rkmpp/sockdir',
                    '--with-systemdsystemunitdir=' + args.prefix + '/lib/systemd/system',
                    '--enable-pam-config=debian', '--enable-strict-locations',
                    '--enable-mpp', '--enable-x264', '--enable-openh264',
                    '--enable-jpeg', '--enable-pixman', '--enable-fuse']
    else:
        headers = args.xrdp_stage / args.xrdp_prefix.lstrip('/') / 'include'
        if not headers.is_dir():
            raise RuntimeError('XRDP must be built and staged before xorgxrdp')
        env['XRDP_CFLAGS'] = '-I' + str(headers)
        command += ['--without-simd']
    run(command, cwd=args.work, env=env, log=args.output / 'configure.log')


def compile_helpers(args):
    integration = args.repo / 'rk3576'
    # CMake's Release/RelWithDebInfo flags include -DNDEBUG. Keep assertions in
    # validation programs active regardless of the selected runtime build type.
    cc = [args.cc] + shlex.split(args.cflags) + ['-UNDEBUG', '-Wall', '-Wextra', '-Werror']
    ld = shlex.split(args.ldflags)
    if args.kind == 'xrdp':
        for mode in ('direct', 'lifecycle'):
            run(cc + ['-I.', '-Icommon', '-Ixrdp', '-Ilibxrdp', '-Ilibipm',
                      str(integration / ('test_' + mode + '.c')),
                      'xrdp/xrdp_encoder_mpp.o', 'xrdp/xrdp_encoder_x264.o',
                      'xrdp/xrdp_tconfig.o', 'common/.libs/libcommon.a',
                      'third_party/tomlc99/.libs/libtoml.a', '-lrockchip_mpp',
                      '-lx264', '-lssl', '-lcrypto', '-ldl', '-lpthread', '-lm']
                + ld + ['-o', str(args.output / ('test-' + mode))], cwd=args.work)
        for name, source, libs in [('desktop-motion', 'desktop-motion.c', ['-lX11', '-lm']),
                                   ('test-desktop', 'test_desktop.c', ['-lX11'])]:
            run(cc + [str(integration / source)] + libs + ld
                + ['-o', str(args.output / name)])
        binary = args.output / 'test-pipeline'
        run(cc + ['-fsanitize=undefined', '-fno-sanitize-recover=all',
                  str(integration / 'test-pipeline.c')] + ld + ['-o', str(binary)])
        run([str(binary)], log=args.output / 'pipeline-test.log')
    else:
        for suffix, sanitize, log in [('', [], 'conversion-test.log'),
                                      ('-ubsan', ['-fsanitize=undefined', '-fno-sanitize-recover=all'],
                                       'conversion-ubsan.log')]:
            binary = args.output / ('test-nv12' + suffix)
            run(cc + sanitize + [str(integration / 'test-nv12.c'),
                                  str(integration / 'nv12-neon.c'),
                                  str(args.work / 'upstream-nv12-reference.c')]
                + ld + ['-o', str(binary)])
            run([str(binary)], log=args.output / log)


def link_artifact(args, name, target):
    inside(args.root, args.root / 'bin' / name)
    (args.root / 'bin').mkdir(exist_ok=True)
    # A regular file is never overwritten; these are just convenience symlinks.
    original = args.root / 'bin' / name
    if original.is_symlink():
        original.unlink()
    elif original.exists():
        raise RuntimeError('Refusing to replace a non-symlink artifact: ' + str(original))
    original.symlink_to(os.path.relpath(target, original.parent))


def stage(args):
    stage_dir = inside(args.root, args.output / 'stage')
    command = [args.make, '-C', str(args.work), 'install', 'DESTDIR=' + str(stage_dir)]
    if args.kind == 'xorgxrdp':
        command += ['moduledir=' + args.prefix + '/lib/xorg/modules']
    run(command, log=args.output / 'install.log')
    compile_helpers(args)
    (args.output / 'install-prefix').write_text(args.prefix + '\n', encoding='utf-8')
    (args.output / 'source-commit').write_text(args.source_commit + '\n', encoding='utf-8')
    info = dict(source_commit=args.source_commit, upstream_commit=args.revision,
                source_dirty=bool(git(args, args.repo, 'status', '--porcelain')),
                component=args.kind, prefix=args.prefix)
    (args.output / 'build-info.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
    if args.kind == 'xrdp':
        for name in ('xrdp', 'xrdp-sesman'):
            link_artifact(args, name, stage_dir / args.prefix.lstrip('/') / 'sbin' / name)
        link_artifact(args, 'xrdp-rkmpp-motion-test', args.output / 'desktop-motion')
    else:
        paths = dict(REPO=str(args.repo), SHA=args.source_commit,
                     XRDP_BUILD=str(args.root / 'xrdp'), XORG_BUILD=str(args.output),
                     PREFIX=args.xrdp_prefix, XPREFIX=args.prefix)
        text = '# Generated by a successful staged build; does not install files.\n'
        text += '\n'.join(name + '=' + shlex.quote(value) for name, value in paths.items()) + '\n'
        (args.root / 'install-env.sh').write_text(text, encoding='utf-8')
    print('Staged ' + args.kind + ': ' + str(stage_dir), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['fetch', 'prepare', 'configure', 'stage'])
    parser.add_argument('--kind', required=True, choices=['xrdp', 'xorgxrdp'])
    for name in ('repo', 'root', 'upstream'):
        parser.add_argument('--' + name, required=True, type=Path)
    for name in ('work', 'output'):
        parser.add_argument('--' + name, type=Path)
    for name in ('git', 'revision'):
        parser.add_argument('--' + name, required=True)
    for name in ('source-commit', 'prefix'):
        parser.add_argument('--' + name)
    for name, default in [('cc', 'cc'), ('cxx', 'c++'), ('cflags', '-O2 -g'),
                           ('cxxflags', '-O2 -g'), ('ldflags', ''), ('make', 'make'),
                           ('xrdp-prefix', '')]:
        parser.add_argument('--' + name, default=default)
    parser.add_argument('--xrdp-stage', type=Path)
    parser.add_argument('--repository')
    args = parser.parse_args()
    args.root = args.root.resolve()
    args.repo = args.repo.resolve()
    if args.root == args.repo or args.root in args.repo.parents:
        raise RuntimeError('The build directory must not be the source directory or its parent')
    if args.action != 'fetch':
        if not all((args.work, args.output, args.source_commit, args.prefix)):
            parser.error('prepare/configure/stage require work, output, source-commit and prefix')
        args.work = inside(args.root, args.work)
        args.output = inside(args.root, args.output)
    elif not args.repository:
        parser.error('fetch requires repository')
    {'fetch': fetch, 'prepare': prepare, 'configure': configure, 'stage': stage}[args.action](args)


if __name__ == '__main__':
    main()
