#!/bin/sh
set -eu
cd /work
archive=sources/busybox-1.37.0.tar.bz2
(cd sources && sha256sum -c busybox-1.37.0.tar.bz2.sha256)
test -d build/busybox-1.37.0 || tar -xjf "$archive" -C build
cd build/busybox-1.37.0
make allnoconfig > ../busybox-config.log
python3 - <<'PY'
from pathlib import Path
enabled='''BUSYBOX STATIC LONG_OPTS FEATURE_VERBOSE_USAGE FEATURE_STAT_FORMAT FEATURE_MD5_SHA1_SUM_CHECK FEATURE_INSTALLER INSTALL_APPLET_SYMLINKS
ASH SH_IS_ASH ASH_OPTIMIZE_FOR_SIZE ASH_INTERNAL_GLOB ASH_BASH_COMPAT ASH_JOB_CONTROL
ASH_ALIAS ASH_EXPAND_PRMT ASH_ECHO ASH_PRINTF ASH_TEST ASH_HELP ASH_GETOPTS
FEATURE_SH_MATH FEATURE_SH_MATH_64 FEATURE_SH_STANDALONE FEATURE_PREFER_APPLETS
INIT FEATURE_USE_INITTAB FEATURE_INIT_QUIET FEATURE_INIT_COREDUMPS
MOUNT MOUNTPOINT FEATURE_MOUNT_FLAGS FEATURE_MOUNT_LOOP FEATURE_MOUNT_FSTAB
UMOUNT FEATURE_UMOUNT_ALL LOSETUP SWITCH_ROOT PIVOT_ROOT
MKDIR LN RM MV CP LS FEATURE_LS_SORTFILES CAT CHMOD CHOWN SYNC DD FEATURE_DD_IBS_OBS
SHA256SUM MD5SUM SHA1SUM UNAME HOSTNAME DATE SLEEP USLEEP AWK SED GREP EGREP CUT TR
READLINK PRINTF ECHO TEST TEST1 TEST2 TRUE FALSE PS TOP PIDOF KILL KILLALL
REBOOT HALT POWEROFF DMESG HEXDUMP OD HEAD TAIL STAT DU DF FREE FIND XARGS
CPIO FEATURE_CPIO_O GZIP GUNZIP ZCAT TAR FEATURE_TAR_CREATE
MDEV FEATURE_MDEV_DAEMON INSMOD RMMOD LSMOD MODPROBE MODINFO
CHROOT ENV SETSID WATCHDOG TOUCH MKFIFO MKNOD BLOCKDEV
FEATURE_DEVPTS FEATURE_SEAMLESS_GZ FEATURE_TAR_LONG_OPTIONS
'''.split()
p=Path('.config');lines=p.read_text().splitlines()
for name in enabled:
 key='CONFIG_'+name
 lines=[line for line in lines if not line.startswith(key+'=') and line!='# '+key+' is not set']
 lines.append(key+'=y')
p.write_text('\n'.join(lines)+'\n')
PY
yes '' | make oldconfig >> ../busybox-config.log
make -j4 EXTRA_CFLAGS='-Os -fno-asynchronous-unwind-tables -fno-unwind-tables' > ../busybox-build.log 2>&1
cp busybox ../busybox
cp .config ../busybox.config
../busybox --help | head -n 2
readelf -h ../busybox | head -n 12
../busybox --list > ../busybox-applets.txt
grep -qx losetup ../busybox-applets.txt
grep -qx switch_root ../busybox-applets.txt
