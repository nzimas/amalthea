# Kernel IRQ-mode startup is separate from thread_run_flag/thread_stopped_flag.
# The manual device_chose syscall does not join this startup worker.
# Source this file from early boot, before forcing a USB role or binding a gadget.
wait_usb_boot_workers() {
 usb_proc_root=${1:-/proc}
 usb_wait_left=${2:-150}
 while :; do
  usb_boot_pending=
  for usb_comm_path in "$usb_proc_root"/[0-9]*/comm; do
   test -r "$usb_comm_path" || continue
   read -r usb_comm < "$usb_comm_path" || continue
   case "$usb_comm" in
    usb_id_irq|usb_device_chos*|usb_host_chose)
     usb_boot_pending="$usb_boot_pending ${usb_comm_path%/comm}:$usb_comm" ;;
   esac
  done
  test -n "$usb_boot_pending" || { echo 'USB boot workers complete';return 0; }
  test "$usb_wait_left" -gt 0 || { echo "USB boot worker timeout:$usb_boot_pending";return 1; }
  echo "Waiting for USB boot workers:$usb_boot_pending"
  usb_wait_left=$((usb_wait_left-1))
  usleep 100000
 done
}
