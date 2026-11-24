#!/usr/bin/env python3
"""Mock adb for installer/tests/run.sh: a simulated TG3040 in a Docker container (sim). Partitions are files /sim/p4, /sim/p5."""
import os, subprocess, sys
C = 'amalthea-sim'
ENV = ['-e', 'BOOT_DEV=/sim/p4', '-e', 'ROOT_DEV=/sim/p5', '-e', 'SYSBLOCK=/sim/sys', '-e', 'BATT=/sim/batt',
       '-e', 'CPUINFO=/sim/cpuinfo', '-e', 'MOUNTS=/sim/mounts']
def sh(cmd, env=()):
    return subprocess.run(['docker', 'exec', *ENV, *env, C, 'sh', '-c', cmd], capture_output=True, text=True)
def state():
    return sh('cat /sim/state').stdout.strip()
LOG = os.environ.get('MOCK_LOG')
args = sys.argv[1:]
if LOG: open(LOG, 'a').write(' '.join(args) + '\n')
if args and args[0] == '-s': args = args[2:]
cmd = args[0]
if cmd == 'start-server': sys.exit(0)
if cmd == 'devices':
    print('List of devices attached')
    if state() != 'bricked': print('SIMSERIAL01\tdevice')
    print(); sys.exit(0)
if cmd == 'push':
    r = subprocess.run(['docker', 'cp', args[1], f'{C}:{args[2]}'], capture_output=True); sys.exit(r.returncode)
if cmd == 'pull':
    r = subprocess.run(['docker', 'cp', f'{C}:{args[1]}', args[2]], capture_output=True); sys.exit(r.returncode)
if cmd == 'shell':
    line = ' '.join(args[1:])
    if line.startswith('reboot'):
        # The bootloader starts whatever is in p4; the Amalthea initramfs follows boot.mode.
        boot = r'''
BASE=/mnt/UDISK/amalthea-linux
h=$(sha256sum /sim/p4 | cut -d " " -f 1)
if [ "$h" = "$(cat /sim/stockboot.sha)" ]; then
  [ "$(sha256sum /sim/p5 | cut -d " " -f 1)" = "$(cat /sim/stockroot.sha)" ] && echo stock > /sim/state || echo bricked > /sim/state
elif [ "$h" = "$(cut -d " " -f 1 /sim/amaboot.sha)" ]; then
  rm -f /tmp/amalthea-recovery-reason
  rescue() { echo "$1" > /tmp/amalthea-recovery-reason; echo recovery > /sim/state; exit 0; }
  mode=$(cat $BASE/boot.mode 2>/dev/null)
  case "$mode" in
   install)
    (cd $BASE && sha256sum -c rootfs.sha256 >/dev/null) || rescue "Install image checksum failed"
    [ "$(cat /sim/batt/capacity)" -ge 30 ] || rescue "Battery too low for installation"
    echo rescue > $BASE/boot.mode
    [ -f /sim/fail-root-write ] && rescue "Root write failed"
    dd if=$BASE/rootfs.img of=/sim/p5 bs=1048576 conv=notrunc 2>/dev/null
    a=$(sha256sum /sim/p5 | cut -d " " -f 1); [ "$a" = "$(cut -d " " -f 1 $BASE/rootfs.sha256)" ] || rescue "Written root checksum failed"
    echo $a > $BASE/installed-root.sha256; echo system > $BASE/boot.mode; echo amalthea > /sim/state ;;
   system) echo amalthea > /sim/state ;;
   *) rescue "No validated Amalthea boot mode selected" ;;
  esac
else echo bricked > /sim/state; fi
echo booted: $(cat /sim/state)
'''
        r = sh(boot); sys.stderr.write(r.stdout + r.stderr); sys.exit(0)
    if 'pidof amalthea' in line: sys.exit(0)
    r = sh(line, ['-e', f'SYSTEM_KIND={state()}'])
    sys.stdout.write((r.stdout + r.stderr).replace('\n', '\r\n')); sys.exit(0)
sys.exit(f'mock adb: unsupported {args}')
