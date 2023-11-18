MAXMEM=/home/aditya/hemem-ucm/
MODEL=/mnt/sda1/models/Llama-2-70b-chat-hf/ggml-model-f16.gguf
LLAMACPP=/home/aditya/llama.cpp

THREADS=22

mkdir -p ${LLAMACPP}/results/

# Run in NVM (numa memory node 2)
nice -20 numactl -C2-23 -m2 -- ${LLAMACPP}/main -m ${MODEL} --threads ${THREADS} -p "The key to happiness in one short sentence is:" \
    -n 40 -e > ${LLAMACPP}/results/nvm_generated.txt 2> ${LLAMACPP}/results/nvm_results.txt

# Run in DRAM (numa memory node 0)
nice -20 numactl -C2-23 -m0 -- ${LLAMACPP}/main -m ${MODEL} --threads ${THREADS} -p "The key to happiness in one short sentence is:" \
    -n 40 -e > ${LLAMACPP}/results/dram_generated.txt 2> ${LLAMACPP}/results/dram_results.txt

