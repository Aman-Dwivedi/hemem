#!/bin/bash -x

HEMEM=/home/amanda/hemem
OUTPUT=/home/amanda/hemem/data/static

export LD_LIBRARY_PATH=${HEMEM}/src:$LD_LIBRARY_PATH;
echo 1000000 > /proc/sys/vm/max_map_count;

mkdir -p ${OUTPUT}/logs
mkdir -p ${OUTPUT}/perf

rm ${OUTPUT}/logs/*
rm ${OUTPUT}/perf/*

./run_perf.sh >/dev/null 2>&1 &
run_perf_pid=$!

FLEXKV_SIZE=$((320*1024*1024*1024))
RUNTIME=600
WARMUP=200
HOTFRAC1=0.15

nice -20 numactl -N0 -m0 --physcpubind=0-3 -- env TIMEDCOOLING=1 ${HEMEM}/src/central-manager > ${OUTPUT}/logs/cm_flexkvs-isolated.txt 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env MISS_RATIO=1.0 LD_PRELOAD=${HEMEM}/src/libhemem.so ${HEMEM}/apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > ${OUTPUT}/perf/flexkvs-isolated.txt &
flexkvs_pid=$!
./wait-kvsbench.sh ${OUTPUT}/perf/flexkvs-isolated.txt
echo ${flexkvs_pid}:0.05 > /tmp/miss_ratio_update
kill -s USR2 ${central_pid}
wait ${flexkvs_pid}
kill -9 ${central_pid}
cp /tmp/log-$flexkvs_pid.txt ${OUTPUT}/logs/flexkvs-isolated-log.txt

sleep 5

nice -20 numactl -N0 -m0 --physcpubind=0-3 -- env TIMEDCOOLING=1 ${HEMEM}/src/central-manager > ${OUTPUT}/logs/cm_flexkvs-gups.txt 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -N0 -m0 --physcpubind=14-23 -- env START_CPU=14  MISS_RATIO=1.0 LD_PRELOAD=${HEMEM}/src/libhemem.so ${HEMEM}/microbenchmarks/gups-pebs 8 0 38 8 36 0 ${OUTPUT}/perf/bggups.txt > ${OUTPUT}/perf/bggups-setup.txt &
bggups_pid=$!
perf stat -e instructions -I 1000 -p ${bggups_pid} -o ${OUTPUT}/perf/bggups-ipc.txt &
./wait-gups.sh ${OUTPUT}/perf/bggups-setup.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env MISS_RATIO=1.0 LD_PRELOAD=${HEMEM}/src/libhemem.so ${HEMEM}/apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > ${OUTPUT}/perf/flexkvs-gups.txt &
flexkvs_pid=$!
./wait-kvsbench.sh ${OUTPUT}/perf/flexkvs-gups.txt
echo ${flexkvs_pid}:0.05 > /tmp/miss_ratio_update
kill -s USR2 ${central_pid}
wait ${flexkvs_pid}
kill -9 ${bggups_pid}
kill -9 ${central_pid}
cp /tmp/log-$flexkvs_pid.txt ${OUTPUT}/logs/flexkvs-gups-log.txt
cp /tmp/log-$bggups_pid.txt ${OUTPUT}/logs/gups-log.txt

sleep 5

nice -20 numactl -N0 -m0 --physcpubind=0-3 -- env TIMEDCOOLING=1 ${HEMEM}/src/central-manager > ${OUTPUT}/logs/cm_flexkvs-gapbs.txt 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -N0 -m0 --physcpubind=14-23 -- env OMP_THREAD_LIMIT=8 MISS_RATIO=1.0 LD_PRELOAD=${HEMEM}/src/libhemem.so ${HEMEM}/apps/gapbs/bc -n 50 -g 29 > ${OUTPUT}/perf/gapbs.txt &
gapbs_pid=$!
perf stat -e instructions -I 1000 -p ${gapbs_pid} -o ${OUTPUT}/perf/gapbs-ipc.txt  &
./wait-gapbs.sh ${OUTPUT}/perf/gapbs.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env MISS_RATIO=1.0 LD_PRELOAD=${HEMEM}/src/libhemem.so ${HEMEM}/apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > ${OUTPUT}/perf/flexkvs-gapbs.txt &
flexkvs_pid=$!
./wait-kvsbench.sh ${OUTPUT}/perf/flexkvs-gapbs.txt
echo ${flexkvs_pid}:0.05 > /tmp/miss_ratio_update
kill -s USR2 ${central_pid}
wait ${flexkvs_pid}
kill -9 ${gapbs_pid}
kill -9 ${central_pid}
cp /tmp/log-$flexkvs_pid.txt ${OUTPUT}/logs/flexkvs-gapbs-log.txt
cp /tmp/log-$gapbs_pid.txt ${OUTPUT}/logs/gapbs-log.txt

sleep 5

nice -20 numactl -N0 -m0 --physcpubind=0-3 -- env TIMEDCOOLING=1  ${HEMEM}/src/central-manager > ${OUTPUT}/logs/cm_flexkvs-bt.txt 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -N0 -m0 --physcpubind=14-23 -- env OMP_THREAD_LIMIT=8 MISS_RATIO=1.0 LD_PRELOAD=${HEMEM}/src/libhemem.so ${HEMEM}/apps/nas-bt-c-benchmark/NPB-OMP/bin/bt.E -n 50 -g 28 > ${OUTPUT}/perf/bt.txt &
bt_pid=$!
perf stat -e instructions -I 1000 -p ${bt_pid} -o ${OUTPUT}/perf/bt-ipc.txt  &
./wait-bt.sh ${OUTPUT}/perf/bt.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env MISS_RATIO=1.0 LD_PRELOAD=${HEMEM}/src/libhemem.so ${HEMEM}/apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > ${OUTPUT}/perf/flexkvs-bt.txt &
flexkvs_pid=$!
./wait-kvsbench.sh ${OUTPUT}/perf/flexkvs-bt.txt
echo ${flexkvs_pid}:0.05 > /tmp/miss_ratio_update
kill -s USR2 ${central_pid}
wait ${flexkvs_pid}
kill -9 ${bt_pid}
kill -9 ${central_pid}
cp /tmp/log-$flexkvs_pid.txt ${OUTPUT}/logs/flexkvs-bt-log.txt
cp /tmp/log-$bt_pid.txt ${OUTPUT}/logs/bt-log.txt


#gnuplot data/miss-ratio-colocate.sh
#gnuplot data/gups-colocate.sh

kill -9 ${run_perf_pid}
pkill perf
