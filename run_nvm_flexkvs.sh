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

nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env MISS_RATIO=1.0 REQ_DRAM=0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./apps/flexkvs/kvsbench -t 4 -T 120 -w 20 -h 0.25 127.0.0.1:11211 -S $((32*1024*1024*1024)) &
flexkvs_pid=$!
wait ${flexkvs_pid}

kill -9 ${central_pid}
kill -9 ${run_perf_pid}
pkill perf

cp /tmp/log-$flexkvs_pid.txt data/dynamic/logs/flexkvs-log.txt
