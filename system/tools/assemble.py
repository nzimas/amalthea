#!/usr/bin/env python3
"""Assemble an allowlisted independent rootfs, never a pruned stock rootfs."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess

ROOT = Path('/work')
DONOR = ROOT/'donor'
OUT = ROOT/'build/root'
APP = Path('/app/build/brick/amalthea')
manifest = []


def donor_resolve(logical):
    parts = list(PurePosixPath(logical).parts[1:])
    resolved = []
    for _ in range(40):
        if not parts:
            return DONOR.joinpath(*resolved)
        part = parts.pop(0)
        if part in ('', '.'):
            continue
        if part == '..':
            resolved.pop()
            continue
        node = DONOR.joinpath(*resolved, part)
        if node.is_symlink():
            target = PurePosixPath(os.readlink(node))
            if target.is_absolute():
                resolved = []
                parts = list(target.parts[1:])+parts
            else:
                parts = list(target.parts)+parts
        else:
            resolved.append(part)
    raise ValueError(f'symlink loop: {logical}')


def copy(source, destination, origin):
    target = OUT/destination.lstrip('/')
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    shutil.copymode(source, target)
    source_sha=hashlib.sha256(target.read_bytes()).hexdigest()
    elf_header=target.read_bytes()[:20]
    if elf_header[:4]==b'\x7fELF' and elf_header[18:20]==b'\xb7\x00':
        subprocess.run(['strip','--strip-debug',str(target)],check=True)
    manifest.append({'path': destination, 'origin': origin,
                     'source_sha256': source_sha,
                     'bytes': target.stat().st_size,
                     'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
    return target


installed_libs = set()


def binary(source, destination, origin):
    copy(source, destination, origin)
    text = subprocess.check_output(['readelf', '-l', '-d', str(source)], text=True)
    libs = re.findall(r'Shared library: \[(.*?)\]', text)
    interpreter = re.search(r'Requesting program interpreter: (.*?)\]', text)
    if interpreter:
        libs.append(PurePosixPath(interpreter[1]).name)
    for name in libs:
        if name in installed_libs:
            continue
        installed_libs.add(name)
        logical = next((f'{directory}/{name}' for directory in
                        ('/lib', '/usr/lib', '/usr/trimui/lib')
                        if (DONOR/f'{directory[1:]}/{name}').exists() or
                        (DONOR/f'{directory[1:]}/{name}').is_symlink()), None)
        if not logical:
            raise FileNotFoundError(f'missing dependency {name} for {source}')
        binary(donor_resolve(logical), '/lib/'+name, 'BSP/runtime dependency '+logical)


def main():
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    for name in ('bin', 'sbin', 'lib', 'usr/bin', 'usr/sbin', 'usr/libexec/amalthea',
                 'etc/alsa', 'proc', 'sys', 'dev', 'tmp', 'run', 'root', 'newroot',
                 'mnt/UDISK', 'mnt/SDCARD', 'data', 'boot', 'var', 'usr/share/amalthea'):
        (OUT/name).mkdir(parents=True, exist_ok=True)
    copy(ROOT/'build/busybox', '/bin/busybox', 'BusyBox 1.37.0 built from verified source')
    subprocess.run([str(ROOT/'build/busybox'), '--install', '-s', str(OUT/'bin')], check=True)
    # BusyBox is invoked by basename, with one physical static executable.
    for path in (OUT/'bin').iterdir():
        if path.is_symlink():
            path.unlink()
            path.symlink_to('busybox')
    (OUT/'sbin/init').symlink_to('../bin/busybox')
    for name in ('poweroff', 'reboot', 'halt', 'switch_root', 'mdev', 'insmod'):
        (OUT/'sbin'/name).symlink_to('../bin/busybox')
    binary(APP, '/usr/bin/amalthea', 'Amalthea appliance built from its source')
    for source, destination in (
        ('/bin/adbd', '/sbin/adbd'),
        ('/usr/trimui/bin/trimui_inputd', '/usr/libexec/amalthea/inputd'),
        ('/usr/trimui/bin/keymon', '/usr/libexec/amalthea/keymon'),
        ('/usr/trimui/bin/hardwareservice', '/usr/libexec/amalthea/hardware'),
        ('/usr/bin/tinymix', '/usr/bin/tinymix'),
    ):
        binary(donor_resolve(source), destination, 'Retained board support '+source)
    # Keep matched display modules available for fallback, not automatic loading.
    for name in ('pvrsrvkm.ko', 'dc_sunxi.ko'):
        copy(DONOR/'lib/modules/4.9.191'/name, '/lib/modules/4.9.191/'+name, 'Matched board display module')
    for name in ('rgx.fw.22.102.54.38', 'rgx.sh.22.102.54.38'):
        copy(DONOR/'lib/firmware'/name, '/lib/firmware/'+name, 'Matched board GPU firmware')
    for source in sorted((ROOT/'rootfs').rglob('*')):
        if source.is_file():
            target=copy(source, '/'+str(source.relative_to(ROOT/'rootfs')), 'Amalthea Linux system source')
            if target.read_bytes().startswith(b'#!'):
                target.chmod(0o755)
    (OUT/'etc/amalthea-root').write_text('Amalthea Linux 1\n')
    (OUT/'etc/os-release').write_text('NAME="Amalthea Linux"\nID=amalthea\nVERSION_ID=1\nPRETTY_NAME="Amalthea Linux 1"\n')
    (OUT/'etc/passwd').write_text('root:x:0:0:root:/root:/bin/sh\n')
    (OUT/'etc/group').write_text('root:x:0:\n')
    (OUT/'etc/ld.so.conf').write_text('/lib\n')
    subprocess.run(['ldconfig', '-r', str(OUT)], check=True)
    (OUT/'var').rmdir()
    (OUT/'var').symlink_to('/run')
    (OUT/'data/amalthea').symlink_to('/mnt/UDISK/tracker-linux')
    (OUT/'usr/share/amalthea/manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    (ROOT/'build/root-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    forbidden = ('MainUI', 'runtrimui.sh', 'procd', 'opkg', 'RetroArch', 'retroarch')
    assert not any(p.name in forbidden for p in OUT.rglob('*'))
    print(f'Allowlisted rootfs: {len(manifest)} files, {sum(p["bytes"] for p in manifest)} bytes')


if __name__ == '__main__':
    main()
