#!/bin/bash -x
mkdir -p data/migrate/gups
mkdir -p data/migrate/logs

debugfile=/tmp/debug.txt
rm -f $debugfile

./../run_perf.sh >/dev/null 2>&1 &
run_perf_pid=$!

nice -20 numactl -C0,1,2,3 -m0 -- ../src/central-manager >$debugfile 2>&1 &
central_pid=$!
sleep 30


rm data/migrate/gups/gups_$1_ready.txt
rm data/migrate/gups/bggups_$1_ready.txt
sleep 1
nice -20 numactl -N0 -m0 --physcpubind=14-23 -- env START_CPU=14 MISS_RATIO=1.0 LD_PRELOAD=../src/libhemem.so ./gups-pebs 8 0 38 8 36 0 data/migrate/gups/bggups_$1.txt > data/migrate/gups/bggups_$1_ready.txt  2>&1&
bggups_pid=$!
#pqos -m "mbl:[14-23]" -r -u csv -o bggupsbw.txt &
#bggupsbw_pid=$!
./../wait-gups.sh data/migrate/gups/bggups_$1_ready.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env START_CPU=4 MISS_RATIO=0.1 LD_PRELOAD=../src/libhemem.so ./gups-pebs 8 0 38 8 36 0 data/migrate/gups/gups_$1.txt > data/migrate/gups/gups_$1_ready.txt 2>&1 &
gups_pid=$!
#pqos -m "mbl:[4-13]" -r -u csv -o data/migrate/gups/gups-bw.txt &
#gupsbw_pid=$!
./../wait-gups.sh data/migrate/gups/gups_$1_ready.txt
sleep 300
kill -s USR1 $gups_pid
sleep 120
kill -s USR2 $gups_pid
kill -9 ${bggups_pid}
sleep 2
#kill -9 ${gupsbw_pid}
#kill -9 ${bggupsbw_pid}
kill -9 ${gups_pid}
kill -9 ${central_pid}
kill -9 ${run_perf_pid}
pkill perf

cp /tmp/log-$gups_pid.txt data/migrate/logs/sensitivity_$1_miss_ratios.txt
