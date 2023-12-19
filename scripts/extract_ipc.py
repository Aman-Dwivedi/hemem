import sys


filename = sys.argv[1]
outfile = sys.argv[2]

infile = open(filename, encoding='utf8')
outfile = open(outfile, "w", encoding='utf8')
lines = infile.readlines()
for line in lines:
    if "instructions" in line:
        words = line.split()
        outfile.write(words[1] + "\n");

infile.close()
outfile.close()

