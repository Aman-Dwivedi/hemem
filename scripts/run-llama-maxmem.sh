MAXMEM=/home/aditya/hemem-ucm/
MODEL=/mnt/sda1/models/Llama-2-70b-chat-hf/ggml-model-f16.gguf
LLAMACPP=/home/aditya/llama.cpp

THREADS=22

export LD_LIBRARY_PATH=./src:./Hoard/src:$LD_LIBRARY_PATH;
echo 1000000 > /proc/sys/vm/max_map_count;

mkdir -p ${LLAMACPP}/results/

nice -20 numactl -C0-1 -m0 -- ${MAXMEM}/src/central-manager &
central_pid=$!
sleep 30
nice -20 numactl -C2-23 -m0 -- env LD_PRELOAD=${MAXMEM}/src/libhemem-llama.so \
    ${LLAMACPP}/main -m ${MODEL} --threads ${THREADS} -p "The key to happiness in one short sentence is:" -n 40 -e > ${LLAMACPP}/results/hemem_generated.txt 2> ${LLAMACPP}/results/hemem_results.txt
kill -9 ${central_pid}
sleep 5
