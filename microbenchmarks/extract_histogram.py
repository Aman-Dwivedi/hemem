import matplotlib.pyplot as plt
import re
import sys
import numpy as np

MAX_LATENCY = 2560

hist_pattern = re.compile(r"Hist\[[0-9]+\]=[0-9]+")


infile = open(sys.argv[1], encoding='utf8').read()
outfile = open("latency_cdf.txt", "w", encoding='utf8')
# Latency histogram for app
latencies = np.zeros(MAX_LATENCY, dtype=np.int64)
matches = hist_pattern.findall(infile)

for hist_elem in matches:
    hist_elem = re.compile(r"[0-9]+").findall(hist_elem)
    index = int(hist_elem[0])
    value = int(hist_elem[1])
    if(index >= MAX_LATENCY):
        latencies[(MAX_LATENCY) - 1] += value
    else:
        latencies[index] += value

cdf = []
total_sum = np.sum(latencies)
curr_sum = 0.0
for i in range(MAX_LATENCY):
    curr_sum += latencies[i]
    cdf.append(curr_sum / total_sum)

for i in range(0, MAX_LATENCY, 10):
    outfile.write(str(i) + ";")
    outfile.write(str(cdf[i]) + ";")
    outfile.write("\n")
