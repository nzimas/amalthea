#!/usr/bin/env python3
"""Exercise actual barrier shell code with proc fixtures, without touching USB."""
from pathlib import Path
import subprocess
import tempfile
helper = Path(__file__).resolve().parents[1] / 'rootfs/usr/libexec/amalthea/usb-boot-barrier.sh'
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    for pid, name in [('1', 'init'), ('2', 'usb_id_irq'), ('3', 'irq/88-usb_id')]:
        (root / pid).mkdir()
        (root / pid / 'comm').write_text(name+'\n')
    # The transient boot worker disappears; the permanent IRQ handler remains.
    # A function has its own positional arguments: use an outer named variable.
    script = '. "$1"; fixture_root=$2; usleep() { rm "$fixture_root/2/comm"; }; wait_usb_boot_workers "$fixture_root" 2'
    result = subprocess.run(['sh', '-c', script, 'test', str(helper), directory], capture_output=True, text=True)
    assert result.returncode == 0 and 'Waiting' in result.stdout and 'complete' in result.stdout, result
    # Must not treat a worker which stays alive as successfully synchronized.
    (root / '2/comm').write_text('usb_device_chos\n')
    result = subprocess.run(['sh', '-c', '. "$1"; usleep() { :; }; wait_usb_boot_workers "$2" 1', 'test', str(helper), directory], capture_output=True, text=True)
    assert result.returncode == 1 and 'timeout' in result.stdout, result
    (root / '2/comm').unlink()
    result = subprocess.run(['sh', '-c', '. "$1"; wait_usb_boot_workers "$2" 0', 'test', str(helper), directory], capture_output=True, text=True)
    assert result.returncode == 0, result
print('PASS: waits for transient kernel boot worker, ignores persistent IRQ handler, rejects timeout')
