#!/bin/bash

BACKEND=/tmp/backend.img
SIZE=2097152
LOOP=""
TESTS_PASSED=0
TESTS_FAILED=0

pass() { echo -e "  PASS"; TESTS_PASSED=$((TESTS_PASSED + 1)); }
fail() { echo -e "  FAIL"; TESTS_FAILED=$((TESTS_FAILED + 1)); }

race_count_since() {
    sudo dmesg | awk -v ts="$1" '
        /^\[/ {
            gsub(/[\[\]]/, "", $1)
            if ($1+0 > ts+0) print $0
        }
    ' | grep -c "RACE detected" || true
}

dmesg_ts() {
    sudo dmesg | tail -1 | grep -oP '(?<=\[)\s*[\d.]+(?=\])' | tr -d ' '
}

cleanup() {
    sudo dmsetup remove my0      2>/dev/null || true
    sudo dmsetup remove delayed  2>/dev/null || true
    if [ -n "$LOOP" ]; then
        sudo losetup -d "$LOOP" 2>/dev/null || true
    fi
    rm -f "$BACKEND"
}
trap cleanup EXIT

echo -e "dm-race-detector tests"
echo ""

echo ">>> подготовка устройств..."
dd if=/dev/zero of="$BACKEND" bs=1M count=1024 status=none
LOOP=$(sudo losetup --find --show "$BACKEND")
sudo dmsetup create delayed --table "0 $SIZE delay $LOOP 0 300"
sudo dmsetup create my0     --table "0 $SIZE mytarget /dev/mapper/delayed"
echo "    loop=$LOOP  delay=300ms  /dev/mapper/my0 готово"
echo ""


echo "TEST 1: Write-Write overlap — RACE"
TS=$(dmesg_ts)
sudo dd oflag=direct if=/dev/urandom of=/dev/mapper/my0 bs=32k count=1 seek=4  2>/dev/null &
sleep 0.05
sudo dd oflag=direct if=/dev/urandom of=/dev/mapper/my0 bs=8k  count=1 seek=17 2>/dev/null
wait
COUNT=$(race_count_since "$TS")
if [ "$COUNT" -gt 0 ]; then
    pass
else
    fail ""
fi


echo "TEST 2: Read-Write overlap — RACE"
TS=$(dmesg_ts)
sudo dd iflag=direct if=/dev/mapper/my0 of=/dev/null bs=32k count=1 skip=4  2>/dev/null &
sleep 0.05
sudo dd oflag=direct if=/dev/urandom   of=/dev/mapper/my0 bs=8k  count=1 seek=17 2>/dev/null
wait
COUNT=$(race_count_since "$TS")
if [ "$COUNT" -gt 0 ]; then
    pass
else
    fail ""
fi


echo "TEST 3: Read-Read overlap — NO RACE"
TS=$(dmesg_ts)
sudo dd iflag=direct if=/dev/mapper/my0 of=/dev/null bs=32k count=1 skip=4  2>/dev/null &
sleep 0.05
sudo dd iflag=direct if=/dev/mapper/my0 of=/dev/null bs=8k  count=1 skip=17 2>/dev/null
wait
COUNT=$(race_count_since "$TS")
if [ "$COUNT" -eq 0 ]; then
    pass 
else
    fail "обнаружено $COUNT гонок"
fi


echo "TEST 4: Non-overlapping writes — NO RACE"
TS=$(dmesg_ts)
sudo dd oflag=direct if=/dev/urandom of=/dev/mapper/my0 bs=32k count=1 seek=0    2>/dev/null &
sleep 0.05
sudo dd oflag=direct if=/dev/urandom of=/dev/mapper/my0 bs=32k count=1 seek=1000 2>/dev/null
wait
COUNT=$(race_count_since "$TS")
if [ "$COUNT" -eq 0 ]; then
    pass
else
    fail "обнаружено $COUNT гонок"
fi


echo "TEST 5: Stress: 10 overlapping writes — MANY RACES"
TS=$(dmesg_ts)
for i in $(seq 1 10); do
    sudo dd oflag=direct if=/dev/urandom of=/dev/mapper/my0 \
        bs=4k count=1 seek=0 2>/dev/null &
done
wait
COUNT=$(race_count_since "$TS")
if [ "$COUNT" -gt 0 ]; then
    pass
else
    fail
fi

echo ""
echo -e ">>> итог"
echo -e "PASSED: $TESTS_PASSED  FAILED: $TESTS_FAILED"
