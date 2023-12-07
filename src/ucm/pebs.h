#ifndef HEMEM_PEBS_H
#define HEMEM_PEBS_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

#include "hemem-types.h"

#define PEBS_POLICY_INTERVAL      (1000000) // in us (1 s)
#define PEBS_MIGRATE_RATE      (4UL * 1024UL * 1024UL * 1024UL) // 4GB
//#define HOT_WRITE_THRESHOLD       (4)
#define PEBS_COOLING_THRESHOLD    (64)

#define HOT_RING_REQS_THRESHOLD   (1024*1024)
#define COLD_RING_REQS_THRESHOLD  (1024*1024)
#define COOLING_PAGES             (16384)

#define PEBS_NPROCS 24
#define PERF_PAGES	(1 + (1 << 16))	// Has to be == 1+2^n, here 1MB
//#define SAMPLE_PERIOD	10007
#define SAMPLE_PERIOD 101
//#define SAMPLE_FREQ	100

#define EWMA_FRAC (0.5)

struct perf_sample {
    struct perf_event_header header;
    __u64	ip;
    __u32 pid, tid;    /* if PERF_SAMPLE_TID */
    __u64 addr;        /* if PERF_SAMPLE_ADDR */
};

void pebs_remove_page(struct hemem_process *process, struct hemem_page *page);
struct hemem_page* pebs_pagefault(struct hemem_process *process);
void pebs_init(void);
void pebs_stats();
void pebs_shutdown();
void pebs_add_process(struct hemem_process *process); 
void pebs_remove_process(struct hemem_process *process);
void pebs_update_process(struct hemem_process *process, double new_miss_ratio);

#endif /*  HEMEM_PEBS_H  */
