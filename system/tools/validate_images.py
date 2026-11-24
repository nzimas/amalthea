#!/usr/bin/env python3
"""Validate packaged boot structure, retained kernel and recovery essentials."""
import gzip
import hashlib
import json
from pathlib import Path
import stat
import struct

repo = Path(__file__).resolve().parents[1]
images = repo / 'images'
manifest = json.loads((images / 'manifest.json').read_text())
for name in ('amalthea-boot.img', 'amalthea-rootfs.img', 'amalthea-initramfs.cpio.gz'):
    data = (images / name).read_bytes()
    assert len(data) == manifest[name]['bytes'], name
    assert hashlib.sha256(data).hexdigest() == manifest[name]['sha256'], name
boot = (images / 'amalthea-boot.img').read_bytes()
original = (repo / 'build/boot.img').read_bytes()
assert boot[:8] == original[:8] == b'ANDROID!'
kernel_size = struct.unpack_from('<I', boot, 8)[0]
ramdisk_size = struct.unpack_from('<I', boot, 16)[0]
page = struct.unpack_from('<I', boot, 36)[0]
assert page == 2048 and len(boot) == len(original) == 15728640
kernel = boot[page:page + kernel_size]
assert kernel == original[page:page + kernel_size]
assert hashlib.sha256(kernel).hexdigest() == manifest['retained_kernel']['sha256']
ramdisk_start = page + ((kernel_size + page - 1) // page) * page
ramdisk = boot[ramdisk_start:ramdisk_start + ramdisk_size]
assert ramdisk == (images / 'amalthea-initramfs.cpio.gz').read_bytes()
boot_id = hashlib.sha1()
for data in (kernel, ramdisk, b''):
    boot_id.update(data)
    boot_id.update(struct.pack('<I', len(data)))
assert boot_id.digest() == boot[576:596]
archive = gzip.decompress(ramdisk)
position = 0
entries = {}
while True:
    assert archive[position:position + 6] == b'070701'
    fields = [int(archive[position + 6 + j * 8:position + 14 + j * 8], 16) for j in range(13)]
    size, name_size = fields[6], fields[11]
    name = archive[position + 110:position + 110 + name_size - 1].decode()
    position = (position + 110 + name_size + 3) & ~3
    data = archive[position:position + size]
    position = (position + size + 3) & ~3
    if name == 'TRAILER!!!':
        break
    entries[name] = (fields, data)
for name in ('init', 'bin/busybox', 'bin/adbd', 'bin/adb_shell',
             'bin/amalthea-usb-recovery', 'bin/amalthea-adbd'):
    assert stat.S_ISREG(entries[name][0][1]) and entries[name][0][1] & 0o111, name
assert entries['rdinit'][1] == b'init'
assert stat.S_ISCHR(entries['dev/console'][0][1])
assert stat.S_ISCHR(entries['dev/null'][0][1])
for name in ('lib/ld-linux-aarch64.so.1', 'lib/libc.so.6', 'lib/libpthread.so.0', 'lib/libgcc_s.so.1'):
    assert name in entries, name
config = (repo / 'build/busybox.config').read_text()
for name in ('STATIC', 'FEATURE_STAT_FORMAT', 'FEATURE_MD5_SHA1_SUM_CHECK',
             'WATCHDOG', 'FEATURE_MOUNT_LOOP', 'SWITCH_ROOT', 'INIT'):
    assert f'CONFIG_{name}=y' in config, name
print('PASS: artifact hashes, unchanged BSP kernel, Android boot ID, RAM recovery executables, device nodes and required BusyBox capabilities')
