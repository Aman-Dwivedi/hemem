#!/bin/bash -x
mkdir -p data/static-fair/logs
mkdir -p data/static-fair/perf

rm data/static-fair/logs/*
rm data/static-fair/perf/*

debugfile=/tmp/debug.txt
rm -f $debugfile

./run_perf.sh >/dev/null 2>&1 &
run_perf_pid=$!

FLEXKV_SIZE=$((320*1024*1024*1024))
RUNTIME=600
WARMUP=200
HOTFRAC1=0.15

nice -20 numactl -N0 -m0 --physcpubind=0-3 -- ./src/central-manager-fair >$debugfile 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > data/static-fair/perf/flexkvs-isolated.txt &
flexkvs_pid=$!
./wait-kvsbench.sh data/static-fair/perf/flexkvs-isolated.txt
wait ${flexkvs_pid}
kill -9 ${central_pid}
cp /tmp/log-$flexkvs_pid.txt data/static-fair/logs/flexkvs-isolated-log.txt

sleep 5

nice -20 numactl -N0 -m0 --physcpubind=0-3 -- ./src/central-manager-fair >$debugfile 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -N0 -m0 --physcpubind=14-23 -- env START_CPU=14  MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./microbenchmarks/gups-pebs 8 0 38 8 36 0 data/static-fair/perf/bggups.txt > data/static-fair/perf/bggups-setup.txt &
bggups_pid=$!
perf stat -e instructions -I 1000 -p ${bggups_pid} -o data/static-fair/perf/bggups-ipc.txt &
./wait-gups.sh data/static-fair/perf/bggups-setup.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > data/static-fair/perf/flexkvs-gups.txt &
flexkvs_pid=$!
./wait-kvsbench.sh data/static-fair/perf/flexkvs-gups.txt
wait ${flexkvs_pid}
kill -9 ${bggups_pid}
kill -9 ${central_pid}
cp /tmp/log-$flexkvs_pid.txt data/static-fair/logs/flexkvs-gups-log.txt
cp /tmp/log-$bggups_pid.txt data/static-fair/logs/gups-log.txt

sleep 5

nice -20 numactl -N0 -m0 --physcpubind=0-3 -- ./src/central-manager-fair >$debugfile 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -N0 -m0 --physcpubind=14-23 -- env OMP_THREAD_LIMIT=8 MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./apps/gapbs/bc -n 50 -g 29 > data/static-fair/perf/gapbs.txt &
gapbs_pid=$!
perf stat -e instructions -I 1000 -p ${gapbs_pid} -o data/static-fair/perf/gapbs-ipc.txt  &
./wait-gapbs.sh data/static-fair/perf/gapbs.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > data/static-fair/perf/flexkvs-gapbs.txt &
flexkvs_pid=$!
./wait-kvsbench.sh data/static-fair/perf/flexkvs-gapbs.txt
wait ${flexkvs_pid}
kill -9 ${gapbs_pid}
kill -9 ${central_pid}
cp /tmp/log-$flexkvs_pid.txt data/static-fair/logs/flexkvs-gapbs-log.txt
cp /tmp/log-$gapbs_pid.txt data/static-fair/logs/gapbs-log.txt

sleep 5

nice -20 numactl -N0 -m0 --physcpubind=0-3 --  ./src/central-manager-fair >$debugfile 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -N0 -m0 --physcpubind=14-23 -- env OMP_THREAD_LIMIT=8 MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./apps/nas-bt-c-benchmark/NPB-OMP/bin/bt.E -n 50 -g 28 > data/static-fair/perf/bt.txt &
bt_pid=$!
perf stat -e instructions -I 1000 -p ${bt_pid} -o data/static-fair/perf/bt-ipc.txt  &
./wait-bt.sh data/static-fair/perf/bt.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > data/static-fair/perf/flexkvs-bt.txt &
flexkvs_pid=$!
./wait-kvsbench.sh data/static-fair/perf/flexkvs-bt.txt
wait ${flexkvs_pid}
kill -9 ${bt_pid}
kill -9 ${central_pid}
cp /tmp/log-$flexkvs_pid.txt data/static-fair/logs/flexkvs-bt-log.txt
cp /tmp/log-$bt_pid.txt data/static-fair/logs/bt-log.txt


#gnuplot data/miss-ratio-colocate.sh
#gnuplot data/gups-colocate.sh

kill -9 ${run_perf_pid}
pkill perf

