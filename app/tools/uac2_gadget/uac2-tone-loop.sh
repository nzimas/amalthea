#!/bin/sh
# Keeps a test tone available to whichever USB host opens the Brick input.
while test ! -e /tmp/uac2-tone-stop; do /tmp/uac_tone hw:CARD=UAC2Gadget,DEV=0 600 >>/tmp/uac2-tone.log 2>&1; sleep 1; done
