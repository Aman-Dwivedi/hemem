import sys

def main():
    if len(sys.argv) != 3:
        print("Usage:", sys.argv[0], "pebs_file hotset_file")
        exit()

    pebs_file = sys.argv[1]
    hotset_file = sys.argv[2]

    hotset_starts = []
    hotset_ends = []

    file = open(hotset_file)
    lines = file.readlines()
    file.close()

    for line in lines:
        words = line.split()
        start = int(words[-3], 0)
        end = int(words[-1], 0)
        if start not in hotset_starts:
            hotset_starts.append(start)
            hotset_ends.append(end)

    file = open(pebs_file)
    lines = file.readlines()
    file.close()

    hot_dramreads = 0
    hot_nvmreads = 0
    hot_writes = 0
    tot_dramreads = 0
    tot_nvmreads = 0
    tot_writes = 0

    for line in lines:
        words = line.split()
        addr = int(words[0].replace(":", ""), 0)
        tot_dramreads += int(words[1])
        tot_nvmreads += int(words[2])
        tot_writes += int(words[3])
        for i in range(0, len(hotset_starts)):
            if addr >= hotset_starts[i] and addr < hotset_ends[i]:
                hot_dramreads += int(words[1])
                hot_nvmreads += int(words[2])
                hot_writes += int(words[3])
                break

    print("Hot DRAM reads", hot_dramreads)
    print("Tot DRAM reads", tot_dramreads)
    print("Hot NVM reads", hot_nvmreads)
    print("Tot NVM reads", tot_nvmreads)
    print("Hot reads", hot_dramreads + hot_nvmreads)
    print("Tot reads", tot_dramreads + tot_nvmreads)
    print("Hot writes", hot_writes)
    print("Tot writes", tot_writes)
    print("Tot samples", tot_dramreads + tot_nvmreads + tot_writes)

main()
