#!/bin/bash
set -e

BACKEND=/tmp/backend.img
SIZE=2097152

cleanup() {
    echo "cleanup"
    sudo dmsetup remove my0      2>/dev/null || true
    sudo dmsetup remove delayed  2>/dev/null || true
    sudo losetup -d "$LOOP"      2>/dev/null || true
    rm -f "$BACKEND"
}
trap cleanup EXIT

echo "backing file"
dd if=/dev/zero of="$BACKEND" bs=1M count=1024 status=none
LOOP=$(sudo losetup --find --show "$BACKEND")

echo "delay-устройство (задержка 300мс на запись)"
sudo dmsetup create delayed --table "0 $SIZE delay $LOOP 0 300"

echo "детектор поверх delay"
sudo dmsetup create my0 --table "0 $SIZE mytarget /dev/mapper/delayed"

echo "запускаем гонку"
sudo dd oflag=direct if=/dev/urandom of=/dev/mapper/my0 bs=32k count=1 seek=4 &
PID=$!
sleep 0.05
sudo dd oflag=direct if=/dev/urandom of=/dev/mapper/my0 bs=8k count=1 seek=17
wait $PID

echo ""
sudo dmesg | tail -5 | grep RACE || echo "(гонок не обнаружено)"
