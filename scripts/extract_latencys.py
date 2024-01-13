import sys


filename = sys.argv[1]
outfile = sys.argv[2]
percentile = sys.argv[3]

infile = open(filename, encoding='utf8')
outfile = open(outfile, "w", encoding='utf8')
lines = infile.readlines()

for line in lines:
    if "TP:" in line:
        words = line.split()
        if percentile == '50':
            lat = words[4].split('=')
            outfile.write(lat[1] + "\n")
        elif percentile == '90':
            lat = words[6].split("=")
            outfile.write(lat[1] + "\n")
        elif percentile == '95':
            lat = words[8].split("=")
            outfile.write(lat[1] + "\n")
        elif percentile == '99':
            lat = words[10].split("=")
            outfile.write(lat[1] + "\n")
        else:
            print("unknown latency")

