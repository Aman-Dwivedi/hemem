#!/bin/bash -x
HEMEM=/home/amanda/hemem
OUTPUT=/home/amanda/hemem/microbenchmarks/data/colocate-autofmmr

export LD_LIBRARY_PATH=${HEMEM}/src:$LD_LIBRARY_PATH;
echo 1000000 > /proc/sys/vm/max_map_count;

mkdir -p ${OUTPUT}/logs
mkdir -p ${OUTPUT}/gups

rm ${OUTPUT}/logs/*
rm ${OUTPUT}/gups/*

debugfile=${OUTPUT}/logs/cm.txt
rm -f $debugfile

./run-perf.sh >/dev/null 2>&1 &
run_perf_pid=$!

nice -20 numactl -C0,1,2,3 -m0 -- env AUTOFMMR=1 TIMEDCOOLING=1 ${HEMEM}/src/central-manager >$debugfile 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -C8,9 -m0   -- env START_CPU=8  MISS_RATIO=1.0 LD_PRELOAD=${HEMEM}/src/libhemem.so ${HEMEM}/microbenchmarks/gups-pebs 2 0 36 8 35 0 /tmp/gups-first.txt &
gups1_pid=$!
sleep 30
nice -20 numactl -C10,11 -m0 -- env START_CPU=10 MISS_RATIO=0.5 LD_PRELOAD=${HEMEM}/src/libhemem.so ${HEMEM}/microbenchmarks/gups-pebs 2 0 36 8 35 0 /tmp/gups-second.txt &
gups2_pid=$!
sleep 30
nice -20 numactl -C12,13 -m0 -- env START_CPU=12 MISS_RATIO=0.3 LD_PRELOAD=${HEMEM}/src/libhemem.so ${HEMEM}/microbenchmarks/gups-pebs 2 0 36 8 35 0 /tmp/gups-third.txt &
gups3_pid=$!
sleep 30
nice -20 numactl -C14,15 -m0 -- env START_CPU=14 MISS_RATIO=0.1 LD_PRELOAD=${HEMEM}/src/libhemem.so ${HEMEM}/microbenchmarks/gups-pebs 2 0 36 8 35 0 /tmp/gups-fourth.txt &
gups4_pid=$! 
sleep 30
sleep 150
kill -s USR1 $gups4_pid
sleep 60
nice -20 numactl -C16,17 -m0 -- env START_CPU=16 MISS_RATIO=0.1 LD_PRELOAD=${HEMEM}/src/libhemem.so ${HEMEM}/microbenchmarks/gups-pebs 2 0 36 8 34 0 /tmp/gups-fifth.txt &
gups5_pid=$!
sleep 60
echo $gups1_pid:0.1 > /tmp/miss_ratio_update
kill -s USR2 $central_pid
sleep 420

kill -9 ${gups1_pid} 
kill -9 ${gups2_pid} 
kill -9 ${gups3_pid} 
kill -9 ${gups4_pid} 
kill -9 ${gups5_pid} 
kill -9 ${central_pid}
kill -9 ${run_perf_pid}

sleep 5

cp /tmp/log-$gups1_pid.txt ${OUTPUT}/logs/first-log.txt
cp /tmp/log-$gups2_pid.txt ${OUTPUT}/logs/second-log.txt
cp /tmp/log-$gups3_pid.txt ${OUTPUT}/logs/third-log.txt
cp /tmp/log-$gups4_pid.txt ${OUTPUT}/logs/fourth-log.txt
cp /tmp/log-$gups5_pid.txt ${OUTPUT}/logs/fifth-log.txt

cp /tmp/gups-first.txt  ${OUTPUT}/gups/first-gups.txt
cp /tmp/gups-second.txt ${OUTPUT}/gups/second-gups.txt
cp /tmp/gups-third.txt  ${OUTPUT}/gups/third-gups.txt
cp /tmp/gups-fourth.txt ${OUTPUT}/gups/fourth-gups.txt
cp /tmp/gups-fifth.txt  ${OUTPUT}/gups/fifth-gups.txt

sleep 5

gnuplot ${OUTPUT}/miss-ratio-colocate-autofmmr.sh
gnuplot ${OUTPUT}/gups-colocate-autofmmr.sh

pkill perf
