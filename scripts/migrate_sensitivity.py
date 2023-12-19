import re
import sys
import numpy as np

MAX_LATENCY = 200

tput_pattern = re.compile(r"TP: total=[0-9]+\.[0-9]+ mops")
hist_pattern = re.compile(r"Hist\[[0-9]+\]=[0-9]+")

rate= sys.argv[1]

miss_ratios = []
latencies = []
throughput = []

infile = open("results/sensitivity_" + rate + "_miss_ratios.txt", encoding='utf8')
lines = infile.readlines()
for line in lines:
    words = line.split()
    miss_ratios.append(words[1])

infile = open("results/sensitivity_" + rate + "_flexkv_1.txt", encoding='utf8').read()
# Throughput of app
matches = tput_pattern.findall(infile)
for match in matches:
    match = re.compile(r"[0-9]+\.[0-9]+").search(match)
    if match:
        throughput.append(np.float64(match.group(0)))

latencies = np.zeros(MAX_LATENCY, dtype=np.int64)
matches = hist_pattern.findall(infile)
for hist_elem in matches:
    hist_elem = re.compile(r"[0-9]+").findall(hist_elem)
    index = int(hist_elem[0])
    value = int(hist_elem[1])
    if(index >= MAX_LATENCY):
        latencies[MAX_LATENCY - 1] += value
    else:
        latencies[index] += value

infile = open("results/sensitivity_" + rate + "_flexkv_2.txt", encoding='utf8').read()
matches = tput_pattern.findall(infile)
for match in matches:
    match = re.compile(r"[0-9]+\.[0-9]+").search(match)
    if match:
        throughput.append(np.float64(match.group(0)))

latencies = np.zeros(MAX_LATENCY, dtype=np.int64)
matches = hist_pattern.findall(infile)
for hist_elem in matches:
    hist_elem = re.compile(r"[0-9]+").findall(hist_elem)
    index = int(hist_elem[0])
    value = int(hist_elem[1])
    if(index >= MAX_LATENCY):
        latencies[MAX_LATENCY - 1] += value
    else:
        latencies[index] += value

cdf = []
total_sum = np.sum(latencies)
cur_sum = 0.0
for i in range(MAX_LATENCY):
    cur_sum += latencies[i]
    cdf.append(cur_sum / total_sum)

length = len(throughput)
miss_ratios_clipped = miss_ratios[-length:] 
outfile = open("sensitivity_" + rate + "_summary_miss_ratios.out", "w", encoding='utf8')
for miss in miss_ratios_clipped:
    outfile.write(miss)
    outfile.write("\n")

outfile = open("sensitivity_" + rate + "_summary_throughput.out", "w", encoding='utf8')
for tput in throughput:
    outfile.write(str(tput) + "\n")
outfile.close()

outfile = open("sensitivity_" + rate + "_summary_latencies.out", "w", encoding='utf8')
for i in range(MAX_LATENCY):
    outfile.write(str(cdf[i]) + "\n")
outfile.close()

