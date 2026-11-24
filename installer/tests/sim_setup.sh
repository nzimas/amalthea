#!/bin/sh
# (re)create the simulated stock Brick (container $SIM_CONTAINER, default amalthea-sim)
C=${SIM_CONTAINER:-amalthea-sim}
docker rm -f "$C" >/dev/null 2>&1
docker run -d --name "$C" --platform linux/arm64 ubuntu:20.04 sleep infinity >/dev/null
docker exec "$C" sh -c '
mkdir -p /sim/sys/mmcblk0p4 /sim/sys/mmcblk0p5 /sim/batt /mnt/UDISK /etc
head -c 15728640 /dev/urandom > /sim/p4; head -c 524288000 /dev/urandom > /sim/p5
echo 30720 > /sim/sys/mmcblk0p4/size; echo 1024000 > /sim/sys/mmcblk0p5/size
echo ${BATTERY:-80} > /sim/batt/capacity; echo Charging > /sim/batt/status
echo "Hardware : TG3040" > /sim/cpuinfo
echo "/dev/by-name/UDISK /mnt/UDISK ext4 rw 0 0" > /sim/mounts
echo 1.0.6 > /etc/version
sha256sum /sim/p4 | cut -d " " -f 1 > /sim/stockboot.sha; sha256sum /sim/p5 | cut -d " " -f 1 > /sim/stockroot.sha
echo stock > /sim/state'
docker cp "$1/payload/boot.sha256" "$C":/sim/amaboot.sha
echo sim ready
