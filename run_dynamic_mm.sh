#!/bin/bash -x
HEMEM=/home/amanda/hemem
OUTPUT=/home/amanda/hemem/data/dynamic-mm
MODEL=/mnt/sda1/models/Llama-2-70b-chat-hf/ggml-model-f16.gguf

export LD_LIBRARY_PATH=${HEMEM}/src:$LD_LIBRARY_PATH;
echo 1000000 > /proc/sys/vm/max_map_count;

mkdir -p ${OUTPUT}
mkdir -p ${OUTPUT}/logs
mkdir -p ${OUTPUT}/perf

rm ${OUTPUT}/logs/*
rm ${OUTPUT}/perf/*

./run_perf.sh >/dev/null 2>&1 &
run_perf_pid=$!

FLEXKV_SIZE=$((320*1024*1024*1024))
RUNTIME=600
WARMUP=200
DYNTIME=400
HOTFRAC1=0.15
HOTFRAC2=0.30

nice -20 numactl -N0 -m0 --physcpubind=19-23 -- env LD_PRELOAD=${HEMEM}/src/libmmap_populate_llama.so ${HEMEM}/apps/llama.cpp/main -m ${MODEL} --threads 4 -p "The key to happiness in one short sentence is:" -n 120 -e > ${OUTPUT}/perf/llama.txt 2>&1 &
llama_pid=$!
perf stat -e instructions -I 1000 -p ${llama_pid} -o ${OUTPUT}/perf/llama-ipc.txt &
./wait-llama.sh ${OUTPUT}/perf/llama.txt
nice -20 numactl -N0 -m0 --physcpubind=14-18 -- env START_CPU=14 LD_PRELOAD=${HEMEM}/src/libmmap_populate.so ${HEMEM}/microbenchmarks/gups-pebs 4 0 37 8 36 1 ${OUTPUT}/perf/gups.txt > ${OUTPUT}/perf/gups-setup.txt 2>&1 &
gups_pid=$!
perf stat -e instructions -I 1000 -p ${gups_pid} -o ${OUTPUT}/perf/gups-ipc.txt &
./wait-gups.sh ${OUTPUT}/perf/gups-setup.txt
nice -20 numactl -N0 -m0 --physcpubind=4-13 -- env LD_PRELOAD=${HEMEM}/src/libmmap_populate.so ${HEMEM}/apps/flexkvs/kvsbench -t 4 -T ${RUNTIME} -w ${WARMUP} -h ${HOTFRAC1} -D ${DYNTIME} -H ${HOTFRAC2} 127.0.0.1:11211 -S ${FLEXKV_SIZE} > ${OUTPUT}/perf/flexkvs.txt &
flexkvs_pid=$!
./wait-kvsbench.sh ${OUTPUT}/perf/flexkvs.txt
kill -s USR1 ${llama_pid}
sleep 300
kill -s USR1 ${gups_pid}
wait ${flexkvs_pid}
#kill -9 ${gups_pid}
kill -s USR2 ${gups_pid}
kill -9 ${llama_pid}

kill -9 ${run_perf_pid}
pkill perf

