/*
 * =====================================================================================
 *
 *       Filename:  gups.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  02/21/2018 02:36:27 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (),
 *   Organization:
 *
 * =====================================================================================
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <sched.h>

#include "../src/ucm/timer.h"
//#include "../src/hemem.h"

#include "gups.h"

#define MAX_THREADS     24

#define GUPS_PAGE_SIZE      (4 * 1024)
#define PAGE_NUM            3
#define PAGES               2048


#define SEC_TO_NS (1000 * 1000 * 1000)
#define HIST_START_NS 0
#define HIST_BUCKET_NS 10 
#define HIST_BUCKETS 256
#define BUFSIZE 1000000

uint32_t *thread_hist[MAX_THREADS];

#ifdef HOTSPOT
extern uint64_t hotset_start;
extern double hotset_fraction;
#endif

#define IGNORE_STRAGGLERS

int threads;
int start_cpu = 10;

uint64_t hot_start = 0;
volatile uint64_t hotsize = 0;
bool move_hotset = false;

struct gups_args {
  int tid;                      // thread id
  uint64_t *indices;       // array of indices to access
  void* field;                  // pointer to start of thread's region
  uint64_t iters;          // iterations to perform
  uint64_t size;           // size of region
  uint64_t elt_size;       // size of elements
  uint64_t hot_start;            // start of hot set
};

uint64_t thread_gups[MAX_THREADS];

static unsigned long updates, nelems;

uint64_t tot_updates = 0;
volatile bool received_signal = false;


static inline uint64_t get_nanos(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t) ts.tv_sec * SEC_TO_NS + ts.tv_nsec;
}

static inline void record_latency(uint64_t thread, uint64_t nanos)
{
    size_t bucket = (nanos - HIST_START_NS) / HIST_BUCKET_NS;
    if (bucket >= HIST_BUCKETS) {
        bucket = HIST_BUCKETS - 1;
    }
    thread_hist[thread][bucket]++;
    /*__sync_fetch_and_add(&c->hist[bucket], 1)*/;
}

static inline void hist_fract_buckets(uint32_t *hist, uint64_t total,
        double *fracs, size_t *idxs, size_t num)
{
    size_t i, j;
    uint64_t sum = 0, goals[num];
    for (j = 0; j < num; j++) {
        goals[j] = total * fracs[j];
    }
    for (i = 0, j = 0; i < HIST_BUCKETS && j < num; i++) {
        sum += hist[i];
        for (; j < num && sum >= goals[j]; j++) {
            idxs[j] = i;
        }
    }
}

static inline int hist_value(size_t i)
{
    if (i == HIST_BUCKETS - 1) {
        return -1;
    }

    return i * HIST_BUCKET_NS + HIST_START_NS;
}

uint32_t *hist, *glbl_hist;
double fracs[6] = { 0.5, 0.9, 0.95, 0.99, 0.999, 0.9999 };
size_t fracs_pos[sizeof(fracs) / sizeof(fracs[0])];

static void *print_instantaneous_gups(void *arg)
{
  char *log_filename = (char *)(arg);
  FILE *tot;
  uint64_t tot_gups, tot_last_second_gups = 0;
  int hx;
  uint64_t msg_total;
  uint64_t current_runtime = 0;
  uint64_t warmup_runtime = 100;
  fprintf(stderr, "Opening instantaneous gups at %s\n", log_filename);
  fflush(stderr);
  tot = fopen(log_filename, "w");
  if (tot == NULL) {
    perror("fopen");
  }

  for (;;) {
    tot_gups = 0;
    msg_total = 0;
    for (int i = 0; i < threads; i++) {
      tot_gups += thread_gups[i];

      for (int j = 0; j < HIST_BUCKETS; j++) {
        hx = thread_hist[i][j];
        msg_total += hx;
        hist[j] += hx;
        if (current_runtime >= warmup_runtime) {
          glbl_hist[j] += hx;
        }
        thread_hist[i][j] = 0;
      }
    }
    fprintf(tot, "%ld\t%.10f\t", rdtscp(), (1.0 * (abs(tot_gups - tot_last_second_gups))) / (1.0e9));
    fprintf(stdout, "%ld\t%.10f\t", rdtscp(), (1.0 * (abs(tot_gups - tot_last_second_gups))) / (1.0e9));
    tot_updates += abs(tot_gups - tot_last_second_gups);
    tot_last_second_gups = tot_gups;

    hist_fract_buckets(hist, msg_total, fracs, fracs_pos,
                sizeof(fracs) / sizeof(fracs[0]));

    fprintf(tot, "50p=%d ns\t90p=%d ns\t95p=%d ns\t"
           "99p=%d ns\t99.9p=%d ns\t99.99p=%d ns \n",
           hist_value(fracs_pos[0]), hist_value(fracs_pos[1]),
           hist_value(fracs_pos[2]), hist_value(fracs_pos[3]),
           hist_value(fracs_pos[4]), hist_value(fracs_pos[5]));
    fflush(tot);
    fprintf(stdout, "50p=%d ns\t90p=%d ns\t95p=%d ns\t"
           "99p=%d ns\t99.9p=%d ns\t99.99p=%d ns \n",
           hist_value(fracs_pos[0]), hist_value(fracs_pos[1]),
           hist_value(fracs_pos[2]), hist_value(fracs_pos[3]),
           hist_value(fracs_pos[4]), hist_value(fracs_pos[5]));
    fflush(stdout);
    memset(hist, 0, sizeof(*hist) * HIST_BUCKETS);


    ++current_runtime;
    sleep(1);
  }

  return NULL;
}

static uint64_t lfsr_fast(uint64_t lfsr)
{
  lfsr ^= lfsr >> 7;
  lfsr ^= lfsr << 9;
  lfsr ^= lfsr >> 13;
  return lfsr;
}

char *filename = "indices1.txt";

FILE *hotsetfile = NULL;

volatile bool done_gups = false;
unsigned completed_gups[MAX_THREADS] = {0};

static void *do_gups(void *arguments)
{
  //printf("do_gups entered\n");
  struct gups_args *args = (struct gups_args*)arguments;
  uint64_t *field = (uint64_t*)(args->field);
  uint64_t i;
  uint64_t index1, index2;
  uint64_t lfsr;
  uint64_t iters = args->iters;
  uint64_t start, end;

  cpu_set_t cpuset;
  pthread_t thread;

  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(start_cpu + args->tid, &cpuset);
  int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }
  fprintf(stderr, "pinned thread %d to core %d\n", args->tid, start_cpu + args->tid);

  srand(args->tid);
  lfsr = rand();

  index1 = 0;
  index2 = 0;
  done_gups = false;
  completed_gups[args->tid] = 0;
 // fprintf(hotsetfile, "Thread %d region: %p - %p\thot set: %p - %p\n", args->tid, field, field + (args->size * elt_size), field + args->hot_start, field + args->hot_start + (args->hotsize * elt_size));   

  for (i = 0; i < iters || iters == 0; i++) {
    start = get_nanos();
    lfsr = lfsr_fast(lfsr);
    if (lfsr % 100 < 90) {
      lfsr = lfsr_fast(lfsr);
      index1 = args->hot_start + (lfsr % hotsize);
      uint64_t tmp = field[index1];
      tmp = tmp + i;
      field[index1] = tmp;
    }
    else {
      lfsr = lfsr_fast(lfsr);
      index2 = lfsr % (args->size);
      uint64_t tmp = field[index2];
      tmp = tmp + i;
      field[index2] = tmp;
    }
    end = get_nanos();
    record_latency(args->tid, end - start);
    if (i % 1000 == 0) {
      thread_gups[args->tid] += 1000;
    }
#ifdef IGNORE_STRAGGLERS
    if(done_gups)
      break;
#endif
  }
  done_gups = true;
  completed_gups[args->tid] = i;
  return 0;
}

void signal_handler() 
{
  received_signal = true;
  hotsize += (hotsize / 2);
}

void kill_gups()
{
  done_gups = true;
}

int main(int argc, char **argv)
{
  unsigned long expt;
  unsigned long size, elt_size;
  unsigned long tot_hot_size;
  int log_hot_size;
  struct timeval starttime, stoptime;
  double secs, gups;
  int i;
  void *p;
  struct gups_args** ga;
  pthread_t t[MAX_THREADS];
  char *log_filename;
  bool wait_for_signal = false;
  char *start_cpu_str;

  // Stop waiting on receiving signal
  signal(SIGUSR1, signal_handler);
  signal(SIGUSR2, kill_gups);

  if (argc < 6) {
    fprintf(stderr, "Usage: %s [threads] [updates per thread] [exponent] [data size (bytes)] [noremap/remap] [wait] [instantaneous_filename]\n", argv[0]);
    fprintf(stderr, "  threads\t\t\tnumber of threads to launch\n");
    fprintf(stderr, "  updates per thread\t\tnumber of updates per thread (0 = unlimited)\n");
    fprintf(stderr, "  exponent\t\t\tlog size of region\n");
    fprintf(stderr, "  data size\t\t\tsize of data in array (in bytes)\n");
    fprintf(stderr, "  hot size\t\t\tlog size of hot set\n");
    fprintf(stderr, "  wait\t\t\twait for signal to start GUPS [1=true, default 0]\n");
    fprintf(stderr, "  log filename\t\t\tthe filename of instantaneous gups.\n");
    return 0;
  }

  gettimeofday(&starttime, NULL);

  start_cpu_str = getenv("START_CPU");
  if (start_cpu_str != NULL) {
    start_cpu = atoi(start_cpu_str);
  }

  threads = atoi(argv[1]);
  assert(threads <= MAX_THREADS);
  ga = (struct gups_args**)malloc(threads * sizeof(struct gups_args*));

  updates = atol(argv[2]);
  updates -= updates % 256;
  expt = atoi(argv[3]);
  assert(expt > 8);
  assert((updates % 256 == 0));
  size = (unsigned long)(1) << expt;
  size -= (size % 256);
  assert(size > 0 && (size % 256 == 0));
  elt_size = atoi(argv[4]);
  log_hot_size = atof(argv[5]);
  tot_hot_size = (unsigned long)(1) << log_hot_size;

  if(argc > 6 && atoi(argv[6]))
    wait_for_signal = true;
  log_filename = argv[7];

  fprintf(stderr, "%lu updates per thread (%d threads)\n", updates, threads);
  fprintf(stderr, "field of 2^%lu (%lu) bytes\n", expt, size);
  fprintf(stderr, "%ld byte element size (%ld elements total)\n", elt_size, size / elt_size);
  fflush(stderr);

  p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE, -1, 0);
  if (p == MAP_FAILED) {
    perror("mmap");
    assert(0);
  }

  gettimeofday(&stoptime, NULL);
  fprintf(stderr, "Init took %.4f seconds\n", elapsed(&starttime, &stoptime));
  fprintf(stderr, "Region address: %p - %p\t size: %ld\n", p, (p + size), size);
  fflush(stderr);

  nelems = (size / threads) / elt_size; // number of elements per thread
  fprintf(stderr, "Elements per thread: %lu\n", nelems);
  fflush(stderr);

  //memset(thread_gups, 0, sizeof(thread_gups));

  hotsetfile = fopen("hotsets.txt", "w");
  if (hotsetfile == NULL) {
    perror("fopen");
    assert(0);
  }

  gettimeofday(&stoptime, NULL);
  secs = elapsed(&starttime, &stoptime);
  printf("Initialization time: %.4f seconds.\n", secs);
  fflush(stdout);

  //hemem_start_timing();

  //pthread_t print_thread;
  //int pt = pthread_create(&print_thread, NULL, print_instantaneous_gups, NULL);
  //assert(pt == 0);


  hot_start = 0;
  hotsize = (tot_hot_size / threads) / elt_size;
  //printf("hot_start: %p\thot_end: %p\thot_size: %lu\n", p + hot_start, p + hot_start + (hotsize * elt_size), hotsize);

  gettimeofday(&starttime, NULL);
  for (i = 0; i < threads; i++) {
    //printf("starting thread [%d]\n", i);
    ga[i] = (struct gups_args*)malloc(sizeof(struct gups_args));
    ga[i]->tid = i;
    ga[i]->field = p + (i * nelems * elt_size);
    ga[i]->iters = updates;
    ga[i]->size = nelems;
    ga[i]->elt_size = elt_size;
    ga[i]->hot_start = 0;        // hot set at start of thread's region
    thread_hist[i] = calloc(HIST_BUCKETS, sizeof(*thread_hist[i]));
    if (thread_hist[i] == NULL) {
      fprintf(stderr, "error allocating histogram for thread %d failed\n", i);
    }
    assert(thread_hist[i] != NULL);
  }

  hist = calloc(HIST_BUCKETS, sizeof(*hist));
  glbl_hist = calloc(HIST_BUCKETS, sizeof(*glbl_hist));
  if (hist == NULL || glbl_hist == NULL) {
    fprintf(stderr, "error allocating global histogram\n");
  }


  if(updates != 0) {
    // run through gups once to touch all memory
    // spawn gups worker threads
    for (i = 0; i < threads; i++) {
      ga[i]->iters = updates * 2;
      int r = pthread_create(&t[i], NULL, do_gups, (void*)ga[i]);
      assert(r == 0);
    }

    // wait for worker threads
    for (i = 0; i < threads; i++) {
      int r = pthread_join(t[i], NULL);
      assert(r == 0);
    }
    //hemem_print_stats();

    gettimeofday(&stoptime, NULL);

    secs = elapsed(&starttime, &stoptime);
    printf("Elapsed time: %.4f seconds.\n", secs);
    gups = 0;
    for(int i = 0; i < threads; ++i)
      gups += completed_gups[i];
    gups /= (secs * 1.0e9);
    printf("GUPS = %.10f\n", gups);
    fflush(stdout);
  }
  filename = "indices2.txt";

  memset(thread_gups, 0, sizeof(thread_gups));
  /*
  if(wait_for_signal) {
    fprintf(stderr, "Waiting for signal\n");
    while(!received_signal);
    fprintf(stderr, "Received signal\n");
  }
  */
  pthread_t print_thread;
  int pt = pthread_create(&print_thread, NULL, print_instantaneous_gups, log_filename);
  assert(pt == 0);

  fprintf(stderr, "Timing.\n");
  gettimeofday(&starttime, NULL);

  //hemem_clear_stats();
  // spawn gups worker threads
  for (i = 0; i < threads; i++) {
    int r = pthread_create(&t[i], NULL, do_gups, (void*)ga[i]);
    assert(r == 0);
  }

  // wait for worker threads
  for (i = 0; i < threads; i++) {
    ga[i]->iters = updates;
    int r = pthread_join(t[i], NULL);
    assert(r == 0);
  }
  gettimeofday(&stoptime, NULL);
  //hemem_print_stats();
  //hemem_clear_stats();

  secs = elapsed(&starttime, &stoptime);
  printf("Elapsed time: %.4f seconds.\n", secs);
  gups = 0;
  for(int i = 0; i < threads; ++i)
    gups += completed_gups[i];
  gups /= (secs * 1.0e9);
  printf("GUPS = %.10f\n", gups);

  for(i = 0; i < HIST_BUCKETS; ++i) {
    if(glbl_hist[i] != 0) {
      fprintf(stdout, "Hist[%d]=%d\n", i*HIST_BUCKET_NS, glbl_hist[i]);
      fflush(stdout);
    }
  }
  //memset(thread_gups, 0, sizeof(thread_gups));

#if 0
  FILE* pebsfile = fopen("pebs.txt", "w+");
  assert(pebsfile != NULL);
  for (uint64_t addr = (uint64_t)p; addr < (uint64_t)p + size; addr += (2*1024*1024)) {
    struct hemem_page *pg = get_hemem_page(addr);
    assert(pg != NULL);
    if (pg != NULL) {
      fprintf(pebsfile, "0x%lx:\t%lu\t%lu\t%lu\n", pg->va, pg->tot_accesses[DRAMREAD], pg->tot_accesses[NVMREAD], pg->tot_accesses[WRITE]);
    }
  }
#endif
  //hemem_stop_timing();

  for (i = 0; i < threads; i++) {
    //free(ga[i]->indices);
    free(ga[i]);
  }
  free(ga);

  //getchar();

  munmap(p, size);

  return 0;
}


