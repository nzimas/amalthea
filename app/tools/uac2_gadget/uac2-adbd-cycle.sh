#!/bin/sh
sleep 1
echo "== adbd cycle $(cat /proc/uptime) pid=$(pidof adbd)" >> /tmp/uac2-trial.log
kill $(pidof adbd)
