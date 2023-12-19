import re
import sys
import numpy as np

tput_pattern = re.compile(r"TP: total=[0-9]+\.[0-9]+ mops")

filename = sys.argv[1]
outfile = sys.argv[2]

throughput = dict()
infile = open(filename, encoding='utf8').read()
outfile = open(outfile, "w", encoding='utf8')
# Throughput of app
matches = tput_pattern.findall(infile)
for match in matches:
    match = re.compile(r"[0-9]+\.[0-9]+").search(match)
    if match:
        tput = np.float64(match.group(0)) * 1000
        outfile.write(str(tput) + "\n")

