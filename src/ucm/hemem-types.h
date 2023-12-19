#ifndef HEMEM_TYPES_H
#define HEMEM_TYPES_H

#define HEMEM_QOS

enum pbuftype {
    DRAMREAD = 0,
    NVMREAD = 1,
//    WRITE = 2,
    NPBUFTYPES
};

enum HOTNESS {
  COLD,
  HOT1,
  HOT2,
  HOT3,
  HOT4,
  HOT5,
  HOT6,
  HOT7,
  HOT8,
  HOT9,
  HOT10,
  NUM_HOTNESS_LEVELS
};

struct hemem_page {
  uint64_t va;
  pid_t pid;
  uint64_t devdax_offset;
  bool in_dram;
  long  uffd;
  enum pagetypes pt;
  volatile bool migrating;
  bool present;
  uint64_t hot;
  uint64_t migrations_up, migrations_down;
  uint64_t local_clock;
  bool ring_present;
  bool in_free_ring;
  uint64_t accesses[NPBUFTYPES];
  uint64_t tot_accesses[NPBUFTYPES];

  UT_hash_handle hh;
  struct hemem_page *next, *prev;
  struct page_list *list;
};

struct hemem_process {
  pid_t pid;
  long uffd;
  bool exited;
  bool valid_uffd;
  int remap_fd;

  bool zero;

#ifdef HEMEM_QOS
  _Atomic uint64_t volatile accessed_pages[NPBUFTYPES];
  _Atomic uint64_t volatile wrong_memtype;
  _Atomic uint64_t volatile samples[24];
  double target_miss_ratio;
  double volatile current_miss_ratio;
  FILE* logfd;
  uint64_t migrate_up_bytes, migrate_down_bytes;
  uint64_t migrations_up, migrations_down;
  uint64_t migration_waits;
  int64_t dram_delta;
  double ratio;
#endif
  volatile uint64_t mem_allocated;
  volatile uint64_t current_dram;
  volatile uint64_t current_nvm;
  //volatile uint64_t allowed_dram;
  uint64_t max_dram;

  struct page_list dram_lists[NUM_HOTNESS_LEVELS + 1];
  struct page_list nvm_lists[NUM_HOTNESS_LEVELS + 1];
  int cur_cool_in_dram_list;
  int cur_cool_in_nvm_list;
  uint64_t process_clock;
  struct hemem_page *cur_cool_in_dram;
  struct hemem_page *cur_cool_in_nvm;
  volatile bool need_cool_dram;
  volatile bool need_cool_nvm;
  uint64_t cools;

  volatile ring_handle_t hot_ring;
  volatile ring_handle_t cold_ring;
  volatile ring_handle_t free_page_ring;
  pthread_mutex_t free_page_ring_lock;

  struct hemem_page* pages;
  pthread_mutex_t pages_lock;
  UT_hash_handle phh;

  struct hemem_process *next, *prev;
  struct process_list *list;

  pthread_mutex_t process_lock;
};

#endif
