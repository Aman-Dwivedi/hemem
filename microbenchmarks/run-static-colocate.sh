#!/bin/bash -x
mkdir -p data/static/logs
mkdir -p data/static/gups

rm data/static/logs/*
rm data/static/gups/*

debugfile=/tmp/debug.txt
rm -f $debugfile

./run-perf.sh >/dev/null 2>&1 &
run_perf_pid=$!

nice -20 numactl -N0 -m0 --physcpubind=0-3 -- ./../src/central-manager >$debugfile 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env START_CPU=4  MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 8 0 38 8 36 0 /tmp/gups-isolated.txt > /tmp/gups-isolated-setup.txt &
gups_pid=$!
./../wait-gups.sh /tmp/gups-isolated-setup.txt
sleep 30
echo $gups_pid:0.1 > /tmp/miss_ratio_update
kill -s USR2 $central_pid
sleep 200
kill -s USR2 $gups_pid
sleep 1
kill -9 ${gups_pid}
kill -9 ${central_pid}
cp /tmp/log-$gups_pid.txt data/static/logs/gups-isolated-log.txt
cp /tmp/gups-isolated.txt  data/static/gups/isolated-gups.txt

sleep 5

nice -20 numactl -N0 -m0 --physcpubind=0-3 -- ./../src/central-manager >$debugfile 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -N0 -m0 --physcpubind=14-23 -- env START_CPU=14  MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 8 0 38 8 36 0 /tmp/bggups.txt > /tmp/bggups-setup.txt &
bggups_pid=$!
perf stat -e instructions -I 1000 -p ${bggups_pid} -o /tmp/bggups-ipc.txt &
./../wait-gups.sh /tmp/bggups-setup.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env START_CPU=4  MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 8 0 38 8 36 0 /tmp/gups-gups.txt > /tmp/gups-gups-setup.txt &
gups_pid=$!
./../wait-gups.sh /tmp/gups-gups-setup.txt
sleep 30
echo $gups_pid:0.1 > /tmp/miss_ratio_update
kill -s USR2 $central_pid
sleep 200
kill -s USR2 $gups_pid
sleep 1
kill -9 ${gups_pid}
kill -9 ${bggups_pid}
kill -9 ${central_pid}
cp /tmp/log-$gups_pid.txt data/static/logs/gups-gups-log.txt
cp /tmp/gups-gups.txt  data/static/gups/gups-gups.txt

sleep 5

nice -20 numactl -N0 -m0 --physcpubind=0-3 -- ./../src/central-manager >$debugfile 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -N0 -m0 --physcpubind=14-23 -- env OMP_THREAD_LIMIT=8 MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./../apps/gapbs/bc -n 50 -g 29 > /tmp/gapbs.txt &
gapbs_pid=$!
perf stat -e instructions -I 1000 -p ${gapbs_pid} -o /tmp/gapbs-ipc.txt  &
./../wait-gapbs.sh /tmp/gapbs.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env START_CPU=4  MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 8 0 38 8 36 0 /tmp/gups-gapbs.txt > /tmp/gups-gapbs-setup.txt &
gups_pid=$!
./../wait-gups.sh /tmp/gups-gapbs-setup.txt
sleep 30
echo $gups_pid:0.1 > /tmp/miss_ratio_update
kill -s USR2 $central_pid
sleep 200
kill -s USR2 $gups_pid
sleep 1
kill -9 ${gups_pid}
kill -9 ${gapbs_pid}
kill -9 ${central_pid}
cp /tmp/log-$gups_pid.txt data/static/logs/gups-gapbs-log.txt
cp /tmp/gups-gapbs.txt  data/static/gups/gapbs-gups.txt

sleep 5

nice -20 numactl -N0 -m0 --physcpubind=0-3 --  ./../src/central-manager >$debugfile 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -N0 -m0 --physcpubind=14-23 -- env OMP_THREAD_LIMIT=8 MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./../apps/nas-bt-c-benchmark/NPB-OMP/bin/bt.E -n 50 -g 28 > /tmp/bt.txt &
bt_pid=$!
perf stat -e instructions -I 1000 -p ${bt_pid} -o /tmp/bt-ipc.txt  &
./../wait-bt.sh /tmp/bt.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env START_CPU=4  MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./gups-pebs 8 0 38 8 36 0 /tmp/gups-bt.txt > /tmp/gups-bt-setup.txt &
gups_pid=$!
./../wait-gups.sh /tmp/gups-bt-setup.txt
sleep 30
echo $gups_pid:0.1 > /tmp/miss_ratio_update
kill -s USR2 $central_pid
sleep 200
kill -s USR2 $gups_pid
sleep 1
kill -9 ${gups_pid}
kill -9 ${bt_pid}
kill -9 ${central_pid}
cp /tmp/log-$gups_pid.txt data/static/logs/gups-bt-log.txt
cp /tmp/gups-bt.txt  data/static/gups/bt-gups.txt


#gnuplot data/miss-ratio-colocate.sh
#gnuplot data/gups-colocate.sh

kill -9 ${run_perf_pid}
pkill perf
