#!/bin/sh

module="scull"
device="scull"

rmmod ${module} $* || exit 1

rm -f /dev/${device}

echo Scull is unloaded!