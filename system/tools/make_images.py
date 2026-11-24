#!/usr/bin/env python3
"""Build ext4 rootfs and Android boot image, retaining exact matched kernel."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path('/work')
BUILD = ROOT/'build'
IMAGES = ROOT/'images'


def run(*args, **kwargs):
    subprocess.run(args, check=True, **kwargs)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-only',action='store_true',help='Retain existing checksum-verified boot/recovery artifacts')
    args=parser.parse_args()
    previous=None
    if args.root_only:
        previous=json.loads((IMAGES/'manifest.json').read_text())
        for name in ('amalthea-boot.img','amalthea-initramfs.cpio.gz'):
            assert hashlib.sha256((IMAGES/name).read_bytes()).hexdigest()==previous[name]['sha256'], name
    IMAGES.mkdir(exist_ok=True)
    filesystem = IMAGES/'amalthea-rootfs.img'
    native_root=Path('/tmp/amalthea-image-root')
    if native_root.exists():
        shutil.rmtree(native_root)
    # mke2fs 1.45 cannot list xattrs on Docker's macOS bind-mount symlinks.
    # A native container staging tree also gives all system files root ownership.
    shutil.copytree(BUILD/'root',native_root,symlinks=True)
    with filesystem.open('wb') as stream:
        stream.truncate(524288000)
    # Disable modern ext4 features unsupported by the retained Linux 4.9 BSP.
    run('mke2fs', '-q', '-t', 'ext4', '-b', '4096', '-I', '256', '-L', 'AMALTHEA',
        '-O', '^metadata_csum,^64bit', '-E', 'lazy_itable_init=0,lazy_journal_init=0',
        '-d', str(native_root), str(filesystem))
    run('e2fsck', '-fn', str(filesystem))
    if args.root_only:
        previous['amalthea-rootfs.img']={'bytes':filesystem.stat().st_size,'sha256':hashlib.sha256(filesystem.read_bytes()).hexdigest()}
        (IMAGES/'manifest.json').write_text(json.dumps(previous,indent=2)+'\n')
        print(json.dumps(previous,indent=2))
        return
    ram = Path('/tmp/amalthea-ramroot')
    if ram.exists():
        shutil.rmtree(ram)
    for name in ('bin', 'sbin', 'lib', 'dev', 'proc', 'sys', 'tmp', 'newroot', 'mnt/UDISK'):
        (ram/name).mkdir(parents=True)
    shutil.copy2(BUILD/'busybox', ram/'bin/busybox')
    shutil.copy2(ROOT/'rootfs/bin/adb_shell', ram/'bin/adb_shell')
    for name in (BUILD/'busybox-applets.txt').read_text().splitlines():
        if name != 'busybox':
            (ram/'bin'/name).symlink_to('busybox')
    (ram/'sbin').rmdir()
    (ram/'sbin').symlink_to('bin')
    shutil.copy2(ROOT/'initramfs/init', ram/'init')
    (ram/'init').chmod(0o755)
    (ram/'rdinit').symlink_to('init')
    shutil.copy2(BUILD/'root/sbin/amalthea-usb-recovery', ram/'bin/amalthea-usb-recovery')
    shutil.copy2(BUILD/'root/sbin/amalthea-adbd', ram/'bin/amalthea-adbd')
    shutil.copy2(BUILD/'root/sbin/adbd', ram/'bin/adbd')
    for name in ('libc.so.6', 'libpthread.so.0', 'libgcc_s.so.1', 'ld-linux-aarch64.so.1'):
        shutil.copy2(BUILD/'root/lib'/name, ram/'lib'/name)
    # cpio archive includes device nodes even on a host filesystem without mknod.
    run('mknod', str(ram/'dev/console'), 'c', '5', '1')
    run('mknod', str(ram/'dev/null'), 'c', '1', '3')
    listing=subprocess.check_output(['find', '.', '-print0'],cwd=ram)
    archive=subprocess.check_output(['cpio','--null','-o','--format=newc','--owner=0:0'],input=listing,cwd=ram)
    packed=gzip.compress(archive,compresslevel=9,mtime=0)
    (IMAGES/'amalthea-initramfs.cpio.gz').write_bytes(packed)
    original=(BUILD/'boot.img').read_bytes()
    assert original[:8] == b'ANDROID!'
    kernel_size=struct.unpack_from('<I',original,8)[0]
    page=struct.unpack_from('<I',original,36)[0]
    second_size=struct.unpack_from('<I',original,24)[0]
    dt_size=struct.unpack_from('<I',original,40)[0]
    assert second_size==0 and dt_size==0 and page==2048
    kernel=original[page:page+kernel_size]
    header=bytearray(original[:page])
    struct.pack_into('<I',header,16,len(packed))
    sha=hashlib.sha1()
    for data in (kernel,packed,b''):
        sha.update(data);sha.update(struct.pack('<I',len(data)))
    header[576:608]=sha.digest()+bytes(12)
    align=lambda data:data+bytes((-len(data))%page)
    boot=bytes(header)+align(kernel)+align(packed)
    assert len(boot)<=len(original), 'Custom ramdisk exceeds boot partition'
    boot+=bytes(len(original)-len(boot))
    assert boot[page:page+kernel_size]==kernel
    (IMAGES/'amalthea-boot.img').write_bytes(boot)
    manifest={name:{'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
              for name in ('amalthea-boot.img','amalthea-rootfs.img','amalthea-initramfs.cpio.gz')
              for path in [IMAGES/name]}
    manifest['retained_kernel']={'bytes':len(kernel),'sha256':hashlib.sha256(kernel).hexdigest()}
    manifest['rootfs_contents']='New allowlisted filesystem; no stock init, gaming menu, emulators or SD hook'
    (IMAGES/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))


if __name__=='__main__':
    main()
