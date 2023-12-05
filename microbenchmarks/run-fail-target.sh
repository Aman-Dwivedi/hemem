#!/bin/bash -x
mkdir -p data/fail-target/logs
mkdir -p data/fail-target/gups

rm data/fail-target/logs/*
rm data/fail-target/gups/*

debugfile=/tmp/debug.txt
rm -f $debugfile

./run-perf.sh >/dev/null 2>&1 &
run_perf_pid=$!

nice -20 numactl -C0,1,2,3 -m0 -- ./../src/central-manager >$debugfile 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -C8,9 -m0   -- env START_CPU=8  MISS_RATIO=0.1 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 2 0 36 8 35 0 /tmp/gups-first.txt &
gups1_pid=$!
sleep 30
nice -20 numactl -C10,11 -m0 -- env START_CPU=10 MISS_RATIO=0.1 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 2 0 36 8 35 0 /tmp/gups-second.txt &
gups2_pid=$!
sleep 30
nice -20 numactl -C12,13 -m0 -- env START_CPU=12 MISS_RATIO=0.1 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 2 0 36 8 35 0 /tmp/gups-third.txt &
gups3_pid=$!
sleep 30
nice -20 numactl -C14,15 -m0 -- env START_CPU=14 MISS_RATIO=0.1 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 2 0 36 8 35 0 /tmp/gups-fourth.txt &
gups4_pid=$! 
sleep 240
nice -20 numactl -C16,17 -m0 -- env START_CPU=16 MISS_RATIO=0.1  LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 2 0 36 8 35 0 /tmp/gups-fifth.txt &
gups5_pid=$!
sleep 180

kill -9 ${gups1_pid} 
kill -9 ${gups2_pid} 
kill -9 ${gups3_pid} 
kill -9 ${gups4_pid} 
kill -9 ${gups5_pid} 
kill -9 ${central_pid}
kill -9 ${run_perf_pid}

cp /tmp/log-$gups1_pid.txt data/fail-target/logs/first-log.txt
cp /tmp/log-$gups2_pid.txt data/fail-target/logs/second-log.txt
cp /tmp/log-$gups3_pid.txt data/fail-target/logs/third-log.txt
cp /tmp/log-$gups4_pid.txt data/fail-target/logs/fourth-log.txt
cp /tmp/log-$gups5_pid.txt data/fail-target/logs/fifth-log.txt

cp /tmp/gups-first.txt  data/fail-target/gups/first-gups.txt
cp /tmp/gups-second.txt data/fail-target/gups/second-gups.txt
cp /tmp/gups-third.txt  data/fail-target/gups/third-gups.txt
cp /tmp/gups-fourth.txt data/fail-target/gups/fourth-gups.txt
cp /tmp/gups-fifth.txt  data/fail-target/gups/fifth-gups.txt

gnuplot data/miss-ratio-fail-target.sh
gnuplot data/gups-fail-target.sh

pkill perf
