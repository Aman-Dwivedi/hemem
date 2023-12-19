import sys


filename = sys.argv[1]
loadfile = sys.argv[2]
storefile = sys.argv[3]

infile = open(filename, encoding='utf8')
loadfile = open(loadfile, "w", encoding='utf8')
storefile = open(storefile, "w", encoding='utf8')
lines = infile.readlines()
for line in lines:
    if "LLC-load-misses" in line:
        words = line.split()
        loadfile.write(words[1] + "\n")
    elif "LLC-store-misses" in line:
        words = line.split()
        storefile.write(words[1] + "\n")

infile.close()
loadfile.close()
storefile.close()
