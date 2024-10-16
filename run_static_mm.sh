#!/bin/bash -x

HEMEM=/home/amanda/hemem
OUTPUT=/home/amanda/hemem/data/static-mm

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

nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env LD_PRELOAD=${HEMEM}/src/libmmap_populate.so ${HEMEM}/apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > ${OUTPUT}/perf/flexkvs-isolated.txt &
flexkvs_pid=$!
./wait-kvsbench.sh ${OUTPUT}/perf/flexkvs-isolated.txt
wait ${flexkvs_pid}

sleep 5

nice -20 numactl -N0 -m0 --physcpubind=14-23 -- env START_CPU=14 LD_PRELOAD=${HEMEM}/src/libmmap_populate.so ${HEMEM}/microbenchmarks/gups-pebs 8 0 38 8 36 1 ${OUTPUT}/perf/bggups.txt > ${OUTPUT}/perf/bggups-setup.txt &
bggups_pid=$!
perf stat -e instructions -I 1000 -p ${bggups_pid} -o ${OUTPUT}/perf/bggups-ipc.txt &
./wait-gups.sh ${OUTPUT}/perf/bggups-setup.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env LD_PRELOAD=${HEMEM}/src/libmmap_populate.so ${HEMEM}/apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > ${OUTPUT}/perf/flexkvs-gups.txt &
flexkvs_pid=$!
./wait-kvsbench.sh ${OUTPUT}/perf/flexkvs-gups.txt
kill -s USR1 ${bggups_pid}
wait ${flexkvs_pid}
kill -s USR2 ${bggups_pid}

sleep 5

nice -20 numactl -N0 -m0 --physcpubind=14-23 -- env OMP_THREAD_LIMIT=8 LD_PRELOAD=${HEMEM}/src/libmmap_populate.so ${HEMEM}/apps/gapbs/bc -n 50 -g 29 > ${OUTPUT}/perf/gapbs.txt &
gapbs_pid=$!
perf stat -e instructions -I 1000 -p ${gapbs_pid} -o ${OUTPUT}/perf/gapbs-ipc.txt  &
./wait-gapbs.sh ${OUTPUT}/perf/gapbs.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env LD_PRELOAD=${HEMEM}/src/libmmap_populate.so ${HEMEM}/apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > ${OUTPUT}/perf/flexkvs-gapbs.txt &
flexkvs_pid=$!
./wait-kvsbench.sh ${OUTPUT}/perf/flexkvs-gapbs.txt
kill -s USR1 ${gapbs_pid}
wait ${flexkvs_pid}
kill -9 ${gapbs_pid}

sleep 5

nice -20 numactl -N0 -m0 --physcpubind=14-23 -- env OMP_THREAD_LIMIT=8 LD_PRELOAD=${HEMEM}/src/libmmap_populate.so ${HEMEM}/apps/nas-bt-c-benchmark/NPB-OMP/bin/bt.E -n 50 -g 28 > ${OUTPUT}/perf/bt.txt &
bt_pid=$!
perf stat -e instructions -I 1000 -p ${bt_pid} -o ${OUTPUT}/perf/bt-ipc.txt  &
./wait-bt.sh ${OUTPUT}/perf/bt.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env LD_PRELOAD=${HEMEM}/src/libmmap_populate.so ${HEMEM}/apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > ${OUTPUT}/perf/flexkvs-bt.txt &
flexkvs_pid=$!
./wait-kvsbench.sh ${OUTPUT}/perf/flexkvs-bt.txt
kill -s USR1 ${bt_pid}
wait ${flexkvs_pid}
kill -9 ${bt_pid}

#gnuplot data/miss-ratio-colocate.sh
#gnuplot data/gups-colocate.sh

kill -9 ${run_perf_pid}
pkill perf
