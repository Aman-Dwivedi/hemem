#ifndef HEMEM_PEBS_H
#define HEMEM_PEBS_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

#include "hemem-types.h"

#define PEBS_POLICY_INTERVAL      (1000000) // in us (1 s)
#define PEBS_MIGRATE_RATE      (4L * 1024L * 1024L * 1024L) // 4GB
#define MAX_PROC_DECAY          (262144L)
//#define HOT_WRITE_THRESHOLD       (4)
#define PEBS_COOLING_THRESHOLD    (1024)
#define PEBS_COOLING_PERIOD       (5) // (5 s)

#define HOT_RING_REQS_THRESHOLD   (1024*1024)
#define COLD_RING_REQS_THRESHOLD  (1024*1024)
#define COOLING_PAGES             (16384)

#define PEBS_NPROCS 24
#define PERF_PAGES	(1 + (1 << 16))	// Has to be == 1+2^n, here 1MB
//#define SAMPLE_PERIOD	10007
#define SAMPLE_PERIOD 101
//#define SAMPLE_FREQ	100

#define EWMA_FRAC (0.5)

#ifdef TMTS

#define HEMEM_ACCESSED_FLAG ((uint64_t)0x0000000000000020UL)

// Helper to convert timeval into us
#define TO_MICROSEC(given_time) (given_time.tv_sec * 1000000ul + given_time.tv_usec)
// Scan all DRAM pages for access bit set
void tmts_scan_dram(struct hemem_process *);
// Try to upgrade an NVM page to DRAM if DRAM space available
void tmts_request_upgrade(struct hemem_process *, struct hemem_page *);
// Try to downgrade a DRAM page to NVM if NVM space available
void tmts_request_downgrade(struct hemem_process *, struct hemem_page *);
// Time intervals (in us) to check process list.
// Should be common factor of TMTS_CHECK_DRAM_HIGHPRTY and TMTS_CHECK_DRAM_LOWPRTY
#define TMTS_SLEEP_DELTA (1ul * 1000ul * 1000ul)
// Time interval (in us) to scan DRAM and downgrade untouched pages for high priority process
// Paper used 2 minutes for this
#define TMTS_CHECK_DRAM_HIGHPRTY (8ul * 60ul * 1000ul * 1000ul)
// Time interval (in us) to scan DRAM and downgrade untouched pages for low priority process
// Paper used 2 to 8 minutes for this
#define TMTS_CHECK_DRAM_LOWPRTY (2ul * 60ul * 1000ul * 1000ul)
#endif


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
