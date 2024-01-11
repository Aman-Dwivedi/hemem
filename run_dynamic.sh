#!/bin/bash -x
mkdir -p data/dynamic/logs
mkdir -p data/dynamic/perf

rm data/dynamic/logs/*
rm data/dynamic/perf/*

debugfile=/tmp/debug.txt
rm -f $debugfile

./run_perf.sh >/dev/null 2>&1 &
run_perf_pid=$!

FLEXKV_SIZE=$((320*1024*1024*1024))
RUNTIME=600
WARMUP=200
DYNTIME=500
HOTFRAC1=0.15
HOTFRAC2=0.30

nice -20 numactl -N0 -m0 --physcpubind=0-3 -- ./src/central-manager >$debugfile 2>&1 &
central_pid=$!
sleep 30
nice -20 numactl -N0 -m0 --physcpubind=19-23 -- env MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem-llama.so ./apps/llama.cpp/main -m /mnt/sda1/LLaMa2/Llama-2-70b-hf/ggml-model-f16.gguf --threads 4 -p "The key to happiness in one short sentence is:" -n 120 -e > data/dynamic/perf/llama.txt 2>&1 &
llama_pid=$!
perf stat -e instructions -I 1000 -p ${llama_pid} -o data/dynamic/perf/llama-ipc.txt &
./wait-llama.sh data/dynamic/perf/llama.txt
nice -20 numactl -N0 -m0 --physcpubind=14-18 -- env START_CPU=14 REQ_DRAM=0 MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./microbenchmarks/gups-pebs 4 0 37 8 36 1 data/dynamic/perf/gups.txt > data/dynamic/perf/gups-setup.txt 2>&1 &
gups_pid=$!
perf stat -e instructions -I 1000 -p ${gups_pid} -o data/dynamic/perf/gups-ipc.txt &
./wait-gups.sh data/dynamic/perf/gups-setup.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env MISS_RATIO=1.0 LD_PRELOAD=/home/amanda/hemem/src/libhemem.so ./apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} -D ${DYNTIME} -H ${HOTFRAC2} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > data/dynamic/perf/flexkvs.txt &
flexkvs_pid=$!
./wait-kvsbench.sh data/dynamic/perf/flexkvs.txt
sleep 300
echo ${gups_pid}:0.5 > /tmp/miss_ratio_update
kill -s USR2 ${central_pid}
kill -s USR1 ${gups_pid}
wait ${flexkvs_pid}
#kill -9 ${gups_pid}
kill -s USR2 ${gups_pid}
kill -9 ${llama_pid}
kill -9 ${central_pid}

kill -9 ${run_perf_pid}
pkill perf


cp /tmp/log-$flexkvs_pid.txt data/dynamic/logs/flexkvs-log.txt
cp /tmp/log-$gups_pid.txt data/dynamic/logs/gups-log.txt
cp /tmp/log-$llama_pid.txt data/dynamic/logs/llama-log.txt
