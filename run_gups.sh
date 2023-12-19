#!/bin/bash -x
export LD_LIBRARY_PATH=./src:./Hoard/src:$LD_LIBRARY_PATH
echo 1000000 > /proc/sys/vm/max_map_count

debugfile=/tmp/debug.txt
rm -f $debugfile

./run_perf.sh >/dev/null 2>&1 &
run_perf_pid=$!

nice -20 numactl -N0 -m0 --physcpubind=0-3 -- ./src/central-manager >$debugfile 2>&1 &
central_pid=$!
sleep 30

rm data/dynamic/perf/gups.txt
sleep 1
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env MISS_RATIO=0.1 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./microbenchmarks/gups-pebs 4 0 38 8 36 0 data/dynamic/perf/gups.txt &
gups_pid=$!
sleep 240
kill -s USR2 $gups_pid
sleep 1
kill -9 ${central_pid}
kill -9 ${run_perf_pid}
pkill perf

cp /tmp/log-$gups_pid.txt data/dynamic/logs/gups-log.txt
