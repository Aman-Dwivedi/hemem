from statistics import mean
from statistics import stdev

def main():
    file = open("times.txt")
    lines = file.readlines()
    file.close()

    mmap_dram = []
    mmap_nvm = []
    hemem_mmap = []
    hemem_missing_fault = []
    page_fault = []
    hemem_migrate_up = []
    hemem_migrate_down = []
    uffdio_register = []
    uffdio_writeprotect = []
    memcpy_to_dram = []
    memcpy_to_nvm = []
    munmap = []
    hemem_enqueue_page = []
    mem_policy_allocate_page = []
    scans = []
    clear_bits = []
    migrates = []
    migrates_up = []
    migrates_down = []
    pebs = []

    for line in lines:
        words = line.split()
        time = float(words[1].replace(",", ""))
        label = words[0]
        if label == "mmap_dram:":
            mmap_dram.append(time)
        elif label == "mmap_nvm:":
            mmap_nvm.append(time)
        elif label == "hemem_mmap:":
            hemem_mmap.append(time)
        elif label == "hemem_missing_fault:":
            hemem_missing_fault.append(time)
        elif label == "page_fault:":
            page_fault.append(time)
        elif label == "hemem_migrate_up:":
            hemem_migrate_up.append(time)
        elif label == "hemem_migrate_down:":
            hemem_migrate_down.append(time)
        elif label == "uffdio_register:":
            uffdio_register.append(time)
        elif label == "uffdio_writeprotect:":
            uffdio_writeprotect.append(time)
        elif label == "memcpy_to_dram:":
            memcpy_to_dram.append(time)
        elif label == "memcpy_to_nvm:":
            memcpy_to_nvm.append(time)
        elif label == "hemem_enqueue_page:":
            hemem_enqueue_page.append(time)
        elif label == "mem_policy_allocate_page:":
            mem_policy_allocate_page.append(time)
        elif label == "scan:":
            scans.append(time)
        elif label == "clear_bits:":
            clear_bits.append(time)
        elif label == "migrate:":
            migrates.append(time)
        elif label == "migrate_up:":
            migrates_up.append(time)
        elif label == "migrate_down:":
            migrates_down.append(time)
        elif label == "pebs:":
            pebs.append(time)
        else:
            print(label)

    print("dram mmaps:                 ", len(mmap_dram))
    print("nvm mmaps:                  ", len(mmap_nvm))
    print("hemem missing faults:       ", len(hemem_missing_fault))
    print("page faults:                ", len(page_fault))
    print("mem policy page allocations ", len(mem_policy_allocate_page))
    print("hemem migrations up:        ", len(hemem_migrate_up))
    print("hemem migrations down:      ", len(hemem_migrate_down))
    print("uffio registers:            ", len(uffdio_register))
    print("uffdio writeprotects:       ", len(uffdio_writeprotect))
    print("memcpys to dram:            ", len(memcpy_to_dram))
    print("memcpys to nvm:             ", len(memcpy_to_nvm))
    print("scans:                      ", len(scans))
    print("clear bits:                 ", len(clear_bits))
    print("migrates:                   ", len(migrates))
    print("migrates up:                ", len(migrates_up))
    print("migrates down:              ", len(migrates_down))
    print("pebs:                       ", len(pebs))
    print("======================================")
    if len(mmap_dram) != 0:
        print("dram mmaps avg:              %1.6f (%1.6f)" % (mean(mmap_dram), stdev(mmap_dram)))
    if len(mmap_nvm) != 0:
        print("nvm mmaps avg:               %1.6f (%1.6f)" % (mean(mmap_nvm), stdev(mmap_nvm)))
    if len(hemem_missing_fault) != 0:
        print("hemem missing faults avg:    %1.6f (%1.6f)" % (mean(hemem_missing_fault), stdev(hemem_missing_fault)))
    if len(page_fault) != 0:
        print("page faults avg:             %1.6f (%1.6f)" % (mean(page_fault), stdev(page_fault)))
    if (len(mem_policy_allocate_page) != 0):
        print("mem policy page allocations: %1.6f (%1.6f)" % (mean(mem_policy_allocate_page), stdev(mem_policy_allocate_page)))
    if len(hemem_migrate_up) != 0:
        print("hemem migrations up avg:     %1.6f (%1.6f)" % (mean(hemem_migrate_up), stdev(hemem_migrate_up)))
    if len(hemem_migrate_down) != 0:
        print("hemem migrations down avg:   %1.6f (%1.6f)" % (mean(hemem_migrate_down), stdev(hemem_migrate_down)))
    if len(uffdio_register) != 0:
        print("uffio registers avg:         %1.6f (%1.6f)" % (mean(uffdio_register), stdev(uffdio_register)))
    if len(uffdio_writeprotect) != 0:
        print("uffdio writeprotects avg:    %1.6f (%1.6f)" % (mean(uffdio_writeprotect), stdev(uffdio_writeprotect)))
    if len(memcpy_to_dram) != 0:
        print("memcpys to dram avg:         %1.6f (%1.6f)" % (mean(memcpy_to_dram), stdev(memcpy_to_dram)))
    if len(memcpy_to_nvm) != 0:
        print("memcpys to nvm avg:          %1.6f (%1.6f)" % (mean(memcpy_to_nvm), stdev(memcpy_to_nvm)))
    if len(scans) != 0 and len(scans) > 1:
        print("scans avg:                   %1.6f (%1.6f)" % (mean(scans), stdev(scans)))
    if len(scans) == 1:
        print("scans avg:                   %1.6f (0.000000)" % (mean(scans)))
    if len(clear_bits) != 0 and len(clear_bits) > 1:
        print("clear bits avg:              %1.6f (%1.6f)" % (mean(clear_bits), stdev(clear_bits)))
    if len(clear_bits) == 1:
        print("clear bits avg:              %1.6f (0.000000" % (mean(clear_bits)))
    if len(migrates) != 0 and len(migrates) > 1:
        print("migrates avg:                %1.6f (%1.6f)" % (mean(migrates), stdev(migrates)))
    if len(migrates) == 1:
        print("migrates avg:                %1.6f (0.000000)" % (mean(migrates)))
    if len(migrates_up) != 0 and len(migrates_up) > 1:
        print("migrates up avg:             %1.6f (%1.6f)" % (mean(migrates_up), stdev(migrates_up)))
    if len(migrates_up) == 1:
        print("migrates up avg:             %1.6f (0.000000)" % (mean(migrates_up)))
    if len(migrates_down) != 0 and len(migrates_down) > 1:
        print("migrates down avg:           %1.6f (%1.6f)" % (mean(migrates_down), stdev(migrates_down)))
    if len(migrates_down) == 1:
        print("migrates down avg:           %1.6f (0.000000)" % (mean(migrates_down)))
    if len(pebs) != 0:
        print("pebs avg:                    %1.6f (%1.6f)" % (mean(pebs), stdev(pebs)))

main()
