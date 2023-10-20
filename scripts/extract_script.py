import matplotlib.pyplot as plt
import re
import sys
import numpy as np

MAX_LATENCY = 200

if len(sys.argv) >= 2:
    BG_PREFIXES=sys.argv[1].split(",")
else:
    BG_PREFIXES=["bg_dram_base", "bg_hemem", "bg_mini_hemem"]
print(BG_PREFIXES)

if len(sys.argv) >= 3:
    BG_APPS=sys.argv[2].split(",")
else:
    BG_APPS=["alone", "gups", "gapbs"]
print(BG_APPS)

if len(sys.argv) >= 4:
    RES=sys.argv[3]
else:
    RES="./results"
print(RES)

if len(sys.argv) >= 5:
    KEY_LATS=np.array(sys.argv[4].split(",")).astype(np.float)
else:
    KEY_LATS=[0.5, 0.9, 0.99]

hist_pattern = re.compile(r"Hist\[[0-9]+\]=[0-9]+")
tput_pattern = re.compile(r"Final throughput = [0-9]+\.[0-9]+ mops")

for file_pref in BG_PREFIXES:
    latencies = dict()
    throughput = dict()
    #print(file_pref, "\nApp\tThroughput\tAvg latency\t95%% latency\t99%% latency")
    for app in BG_APPS:
        infile = open(RES + "/" + file_pref + "_" + app + "_flexkv.txt", encoding='utf8').read()
        # Latency histogram for app
        latencies[app] = np.zeros(MAX_LATENCY, dtype=np.int64)
        matches = hist_pattern.findall(infile)
        for hist_elem in matches:
            hist_elem = re.compile(r"[0-9]+").findall(hist_elem)
            index = int(hist_elem[0])
            value = int(hist_elem[1])
            if(index >= MAX_LATENCY):
                latencies[app][MAX_LATENCY - 1] += value
            else:
                latencies[app][index] += value
        # Throughput of app
        throughput[app] = ""
        match = tput_pattern.search(infile)
        if match:
            match = re.compile(r"[0-9]+\.[0-9]+").search(match.group(0))
            if match:
                throughput[app] = match.group(0)
    
    outfile = open(file_pref + "_summary.out", "w", encoding='utf8')
    outfile.write(file_pref + "\n")
    # Output throughputs
    outfile.write("Throughput;")
    for app in BG_APPS:
        outfile.write(app + ";")
    outfile.write("\n;")
    for app in BG_APPS:
        outfile.write(throughput[app] + ";")
    outfile.write("\n")

    # Calculate cdf for the different apps
    cdf = dict()
    total_sum = dict()
    for app in BG_APPS:
        cdf[app] = []
        total_sum[app] = np.sum(latencies[app])
        curr_sum = 0.0
        for i in range(MAX_LATENCY):
            curr_sum += latencies[app][i]
            cdf[app].append(curr_sum / total_sum[app])

    # Output key latency points
    for lats in KEY_LATS:
        outfile.write(str(np.round(lats * 100, 2)) + "% latencies;")
        for app in BG_APPS:
            outfile.write(app + ";")
        outfile.write("\n;")
        for app in BG_APPS:
            outfile.write(str(np.searchsorted(cdf[app], lats)) + ";")
        outfile.write("\n")

    # Output histogram
    outfile.write("Latency percentiles;")
    for app in BG_APPS:
        outfile.write(app + ";")
    outfile.write("\n")
    for i in range(MAX_LATENCY):
        outfile.write(str(i) + ";")
        for app in BG_APPS:
            outfile.write(str(cdf[app][i]) + ";")
        outfile.write("\n")
    outfile.close()
