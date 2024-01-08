#!/bin/sh
mkdir -p data/colocate/logs
mkdir -p data/colocate/gups

rm data/colocate/logs/*
rm data/colocate/gups/*

debugfile=/tmp/debug.txt
rm -f $debugfile

./run-perf.sh >/dev/null 2>&1 &
run_perf_pid=$!

nice -20 numactl -C0,1,2,3 -m0 -- ./../src/central-manager >$debugfile 2>&1 &
central_pid=$!
sleep 5
nice -20 numactl -C8,9 -m0   -- env START_CPU=8  MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 2 0 36 8 34 0 /tmp/gups-first.txt &
gups1_pid=$!
sleep 10
nice -20 numactl -C10,11 -m0 -- env START_CPU=10 MISS_RATIO=0.8 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 2 0 36 8 34 0 /tmp/gups-second.txt &
gups2_pid=$!
sleep 10
nice -20 numactl -C12,13 -m0 -- env START_CPU=12 MISS_RATIO=0.6 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 2 0 36 8 34 0 /tmp/gups-third.txt &
gups3_pid=$!
sleep 10
nice -20 numactl -C14,15 -m0 -- env START_CPU=14 MISS_RATIO=0.4 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 2 0 36 8 34 0 /tmp/gups-fourth.txt &
gups4_pid=$!
sleep 10
nice -20 numactl -C16,17 -m0 -- env START_CPU=16 MISS_RATIO=0.2 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 2 0 36 8 34 0 /tmp/gups-fifth.txt &
gups5_pid=$!
sleep 60
nice -20 numactl -C18,19 -m0 -- env START_CPU=18 MISS_RATIO=0.1 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 2 0 36 8 34 0 /tmp/gups-sixth.txt &
gups6_pid=$!
sleep 100
kill -s USR1 $gups5_pid
sleep 50
echo $gups1_pid:0.1 > /tmp/miss_ratio_update
kill -s USR2 $central_pid
sleep 100

kill -9 ${gups1_pid} 
kill -9 ${gups2_pid} 
kill -9 ${gups3_pid} 
kill -9 ${gups4_pid} 
kill -9 ${gups5_pid} 
kill -9 ${gups6_pid} 
kill -9 ${central_pid}
kill -9 ${run_perf_pid}

cp /tmp/log-$gups1_pid.txt data/colocate/logs/first-log.txt
cp /tmp/log-$gups2_pid.txt data/colocate/logs/second-log.txt
cp /tmp/log-$gups3_pid.txt data/colocate/logs/third-log.txt
cp /tmp/log-$gups4_pid.txt data/colocate/logs/fourth-log.txt
cp /tmp/log-$gups5_pid.txt data/colocate/logs/fifth-log.txt
cp /tmp/log-$gups6_pid.txt data/colocate/logs/sixth-log.txt

cp /tmp/gups-first.txt  data/colocate/gups/first-gups.txt
cp /tmp/gups-second.txt data/colocate/gups/second-gups.txt
cp /tmp/gups-third.txt  data/colocate/gups/third-gups.txt
cp /tmp/gups-fourth.txt data/colocate/gups/fourth-gups.txt
cp /tmp/gups-fifth.txt  data/colocate/gups/fifth-gups.txt
cp /tmp/gups-sixth.txt  data/colocate/gups/sixth-gups.txt

gnuplot data/miss-ratio-colocate.sh
gnuplot data/gups-colocate.sh

pkill perf
