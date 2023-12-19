#!/bin/bash -x
mkdir -p data/equal/logs
mkdir -p data/equal/gups

rm data/equal/logs/*
rm data/equal/gups/*

debugfile=/tmp/debug.txt
rm -f $debugfile

./run-perf.sh >/dev/null 2>&1 &
run_perf_pid=$!

nice -20 numactl -C0,1,2,3 -m0 -- ./../src/central-manager >$debugfile 2>&1 &
central_pid=$!
sleep 15
nice -20 numactl -C4,5,6,7 -m0   -- env START_CPU=4  MISS_RATIO=0.1 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 4 0 36 8 34 0 /tmp/gups-first.txt &
gups1_pid=$!
sleep 30
nice -20 numactl -C8,9,10,11 -m0 -- env START_CPU=8 MISS_RATIO=0.1 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 4 0 36 8 34 0 /tmp/gups-second.txt &
gups2_pid=$!
sleep 30
nice -20 numactl -C12,13,14,15 -m0 -- env START_CPU=12 MISS_RATIO=0.1 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 4 0 36 8 34 0 /tmp/gups-third.txt &
gups3_pid=$!
sleep 30
nice -20 numactl -C16,17,18,19 -m0 -- env START_CPU=16 MISS_RATIO=0.1 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 4 0 36 8 34 0 /tmp/gups-fourth.txt &
gups4_pid=$!
sleep 200

kill -9 ${gups1_pid} 
kill -9 ${gups2_pid} 
kill -9 ${gups3_pid} 
kill -9 ${gups4_pid} 
kill -9 ${central_pid}
kill -9 ${run_perf_pid}

cp /tmp/log-$gups1_pid.txt data/equal/logs/first-log.txt
cp /tmp/log-$gups2_pid.txt data/equal/logs/second-log.txt
cp /tmp/log-$gups3_pid.txt data/equal/logs/third-log.txt
cp /tmp/log-$gups4_pid.txt data/equal/logs/fourth-log.txt

cp /tmp/gups-first.txt  data/equal/gups/first-gups.txt
cp /tmp/gups-second.txt data/equal/gups/second-gups.txt
cp /tmp/gups-third.txt  data/equal/gups/third-gups.txt
cp /tmp/gups-fourth.txt data/equal/gups/fourth-gups.txt

gnuplot data/miss-ratio-equal.sh
gnuplot data/gups-equal.sh

pkill perf
