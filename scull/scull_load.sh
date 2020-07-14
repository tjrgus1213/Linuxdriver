#!/bin/sh
module="scull"
device="scull"
mode="644"

insmod ./${module}.ko $* || exit 1

rm -f /dev/${device}

major=$(awk -v var=$device '$2 == var {print $1}' /proc/devices)

mknod /dev/${device} c $major 0

group="staff"
grep -q "^staff:" /etc/group || group="wheel"
chgrp $group /dev/${device}
chmod $mode /dev/${device}