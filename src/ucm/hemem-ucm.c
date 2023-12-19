#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/userfaultfd.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>

#include "hemem-ucm.h"
#include "channel-ucm.h"
#include "timer.h"
#include "logging.h"
#include "pebs.h"

pthread_t fault_thread;
pthread_t request_thread;
pthread_t listen_thread;
int request_epoll_fd = -1;
int fault_epoll_fd = -1;
int listen_fd = -1;

struct timeval startup;

struct hemem_process* uffds[1024];

int dramfd = -1;
int nvmfd = -1;
pthread_t stats_thread;
#ifdef USE_DMA
int uffd = -1;
#else
#ifdef USE_PARALLEL_MEMCPY
pthread_t copy_threads[MAX_COPY_THREADS];
#endif // USE_PARALLEL_MEMCPY
#endif // ! USE_DMA

#ifndef USE_DMA
#ifdef USE_PARALLEL_MEMCPY
struct pmemcpy {
  pthread_mutex_t lock;
  pthread_barrier_t barrier;
  _Atomic bool write_zeros;
  _Atomic void *dst;
  _Atomic void *src;
  _Atomic size_t length;
};

static struct pmemcpy pmemcpy;
#endif // USE_PARALLEL_MEMCPY
#endif // USE_DMA

struct hemem_process volatile *processes = NULL;
pthread_mutex_t processes_lock = PTHREAD_MUTEX_INITIALIZER;

uint64_t mem_mmaped = 0;
uint64_t mem_allocated = 0;
uint64_t pages_allocated = 0;
uint64_t pages_freed = 0;
uint64_t fastmem_allocated = 0;
uint64_t slowmem_allocated = 0;
uint64_t wp_faults_handled = 0;
uint64_t missing_faults_handled = 0;
uint64_t migrations_up = 0;
uint64_t migrations_down = 0;
uint64_t bytes_migrated = 0;
uint64_t memcpys = 0;
uint64_t memsets = 0;
uint64_t migration_waits = 0;

void *dram_devdax_mmap;
void *nvm_devdax_mmap;

void ucm_update_miss_ratio(int signum);
void *hemem_parallel_memcpy_thread(void *arg);

#ifdef STATS_THREAD
static void *hemem_stats_thread();
#endif

void hemem_ucm_init() {
#ifdef USE_DMA
  struct uffdio_api uffdio_api;
  struct uffdio_dma_channs uffdio_dma_channs;
#endif
  int ret;

  log_init("ucm");

  LOG("hemem_init: started\n");  

  dramfd = open(DRAMPATH, O_RDWR);
  if (dramfd < 0) {
    perror("dram open");
  }
  assert(dramfd >= 0);

  nvmfd = open(NVMPATH, O_RDWR);
  if (nvmfd < 0) {
    perror("nvm open");
  }
  assert(nvmfd >= 0);

  if (signal(SIGUSR2, ucm_update_miss_ratio) == SIG_ERR) {
    assert(0 && "Failed to map SIGUSR2 to the missratio update.");
  }

#ifdef USE_DMA  
  uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
  if (uffd == -1) {
    perror("uffd");
    assert(0);
  }

  uffdio_api.api = UFFD_API;
  uffdio_api.features =
      UFFD_FEATURE_PAGEFAULT_FLAG_WP | UFFD_FEATURE_MISSING_SHMEM |
      UFFD_FEATURE_MISSING_HUGETLBFS; 
  uffdio_api.ioctls = 0;
  if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
    perror("ioctl uffdio_api");
    assert(0);
  }
#endif

  request_epoll_fd = epoll_create1(0);
  if (request_epoll_fd == -1) {
    perror("epoll_create1");
    assert(0);
  }

  fault_epoll_fd = epoll_create1(0);
  if (fault_epoll_fd == -1) {
    perror("epoll_create1");
    assert(0);
  }

  listen_fd = channel_server_init();
  if (listen_fd < 0) {
    perror("channel_server_init");
    assert(0);
  }

  ret = pthread_create(&fault_thread, NULL, handle_fault, 0);
  if (ret != 0) {
    perror("pthread_create");
    assert(0);
  }

  ret = pthread_create(&listen_thread, NULL, accept_new_app, 0);
  if (ret != 0) {
    perror("pthread_create");
    assert(0);
  }

  ret = pthread_create(&request_thread, NULL, handle_request, 0);
  if (ret != 0) {
    perror("pthread_create");
    assert(0);
  }

#if DRAMSIZE != 0
  dram_devdax_mmap = mmap(NULL, DRAMSIZE, PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_POPULATE, dramfd, 0);
  if (dram_devdax_mmap == MAP_FAILED) {
    perror("dram devdax mmap");
    assert(0);
  }
#endif

  nvm_devdax_mmap = mmap(NULL, NVMSIZE, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE, nvmfd, 0);
  if (nvm_devdax_mmap == MAP_FAILED) {
    perror("nvm devdax mmap");
    assert(0);
  }

#ifndef USE_DMA
#ifdef USE_PARALLEL_MEMCPY
  uint64_t i;
  ret = pthread_barrier_init(&pmemcpy.barrier, NULL, MAX_COPY_THREADS + 1);
  assert(ret == 0);

  ret = pthread_mutex_init(&pmemcpy.lock, NULL);
  assert(ret == 0);

  for (i = 0; i < MAX_COPY_THREADS; i++) {
    ret = pthread_create(&copy_threads[i], NULL, hemem_parallel_memcpy_thread,
                       (void *)i);
    assert(ret == 0);
  }
#endif // USE_PARALLEL_MEMCPY
#endif // ! USE_DMA

#ifdef STATS_THREAD
  ret = pthread_create(&stats_thread, NULL, hemem_stats_thread, NULL);
  assert(ret == 0);
#endif

#ifdef USE_DMA
  uffdio_dma_channs.num_channs = NUM_CHANNS;
  uffdio_dma_channs.size_per_dma_request = SIZE_PER_DMA_REQUEST;
  if (ioctl(uffd, UFFDIO_DMA_REQUEST_CHANNS, &uffdio_dma_channs) == -1) {
    perror("ioctl UFFDIO_API\n");
    assert(0);
  }
#endif

  gettimeofday(&startup, NULL);

  LOG("hemem_init: finished\n");
}

void hemem_ucm_stop() {
#ifdef USE_DMA
  struct uffdio_dma_channs uffdio_dma_channs;
  uffdio_dma_channs.num_channs = NUM_CHANNS;
  uffdio_dma_channs.size_per_dma_request = SIZE_PER_DMA_REQUEST;
  if (ioctl(uffd, UFFDIO_DMA_RELEASE_CHANNS, &uffdio_dma_channs) == -1) {
    perror("ioctl UFFDIO_RELEASE_CHANNS");
    assert(0);
  }
#endif

  pebs_shutdown();
}

#ifndef USE_DMA
#ifdef USE_PARALLEL_MEMCPY
void *hemem_parallel_memcpy_thread(void *arg) {
  uint64_t tid = (uint64_t)arg;
  void *src;
  void *dst;
  size_t length;
  size_t chunk_size;
  cpu_set_t cpuset;
  pthread_t thread;

  assert(tid < MAX_COPY_THREADS);

  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(PARALLEL_MIGRATE_THREAD_CPU + tid, &cpuset);
  int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }

  for (;;) {
    int r = pthread_barrier_wait(&pmemcpy.barrier);
    assert(r == 0 || r == PTHREAD_BARRIER_SERIAL_THREAD);
    if (tid == 0) {
      memcpys++;
    }

    // grab data out of shared struct
    length = pmemcpy.length;
    chunk_size = length / MAX_COPY_THREADS;
    dst = pmemcpy.dst + (tid * chunk_size);
    if (!pmemcpy.write_zeros) {
      src = pmemcpy.src + (tid * chunk_size);
      memcpy(dst, src, chunk_size);
    } else {
      memset(dst, 0, chunk_size);
    }

    //LOG("thread %lu done copying\n", tid);

    r = pthread_barrier_wait(&pmemcpy.barrier);
    assert(r == 0 || r == PTHREAD_BARRIER_SERIAL_THREAD);
  }
  return NULL;
}

static void hemem_parallel_memcpy(void *dst, void *src, size_t length) {
  pthread_mutex_lock(&(pmemcpy.lock));
  pmemcpy.dst = dst;
  pmemcpy.src = src;
  pmemcpy.length = length;
  pmemcpy.write_zeros = false;

  int r = pthread_barrier_wait(&pmemcpy.barrier);
  assert(r == 0 || r == PTHREAD_BARRIER_SERIAL_THREAD);

  // LOG("parallel migration started\n");

  r = pthread_barrier_wait(&pmemcpy.barrier);
  assert(r == 0 || r == PTHREAD_BARRIER_SERIAL_THREAD);
  // LOG("parallel migration finished\n");
  pthread_mutex_unlock(&(pmemcpy.lock));
}
#endif // USE_PARALLEL_MEMCPY
#endif // ! USE_DMA

int add_epoll_ctl(int epoll, int fd)
{
  int ret;
  struct epoll_event event;

  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLET;

  ret = epoll_ctl(epoll, EPOLL_CTL_ADD, fd, &event);
  #ifdef HEMEM_DEBUG
  printf("add_epoll_ctl, fd=%d\n", fd);
  #endif

  if (ret != 0) {
    perror("epoll_ctl");
    return -1;
  }

  return 0;
}

int delete_epoll_ctl(int epoll, int fd)
{
  int ret;
  ret = epoll_ctl(epoll, EPOLL_CTL_DEL, fd, NULL);

  #ifdef HEMEM_DEBUG
  printf("delete socket fd:%d\n", fd);
  #endif

  if (ret != 0) {
    perror("epoll_ctl");
    return -1;
  }

  return 0;
}

void ucm_update_miss_ratio(int signum) {
  if(signum != SIGUSR2) {
    return;
  }
  
  const char* new_miss_fname = "/tmp/miss_ratio_update";
  struct hemem_process *p;

  FILE *new_miss_file = fopen(new_miss_fname, "r");
  int pid = -1;
  float new_miss = -1.0;
  fscanf(new_miss_file,"%d:%f", &pid, &new_miss);

  p = find_process(pid);

  if(p == NULL) {
    LOG("No process with PID %d currently managed", pid);
  }
  LOG("updated miss ratio of proc: %d to %f\n", pid, new_miss);

  pebs_update_process(p, new_miss);
}


struct hemem_process* ucm_add_process(int fd, struct add_process_request* request, struct add_process_response* response)
{
  pid_t pid = request->header.pid;
  struct hemem_process* process;
  uint64_t** buffer;
  int ret;
#ifdef HEMEM_QOS
  char logpath[32];
#endif

  process = (struct hemem_process*)calloc(1, sizeof(struct hemem_process));
  if (process == NULL) {
    perror("calloc");
    assert(0);
  }

  process->pid = request->header.pid;
  process->exited = false;
#ifdef HEMEM_QOS
  process->target_miss_ratio = request->target_miss_ratio;
#endif
  process->valid_uffd = false;
  for (int i = 0; i < NUM_HOTNESS_LEVELS; i++) {
    pthread_mutex_init(&(process->dram_lists[i].list_lock), NULL);
    pthread_mutex_init(&(process->nvm_lists[i].list_lock), NULL);
  }
  pthread_mutex_init(&(process->pages_lock), NULL);
  pthread_mutex_init(&(process->process_lock), NULL);

  buffer = (uint64_t**)malloc(sizeof(uint64_t*) * CAPACITY);
  assert(buffer); 
  process->hot_ring = ring_buf_init(buffer, CAPACITY);
  buffer = (uint64_t**)malloc(sizeof(uint64_t*) * CAPACITY);
  assert(buffer); 
  process->cold_ring = ring_buf_init(buffer, CAPACITY);
  buffer = (uint64_t**)malloc(sizeof(uint64_t*) * CAPACITY);
  assert(buffer); 
  process->free_page_ring = ring_buf_init(buffer, CAPACITY);
  pthread_mutex_init(&(process->free_page_ring_lock), NULL);
  
  process->pages = NULL;

  process->cur_cool_in_dram = NULL;
  process->cur_cool_in_nvm = NULL;
  process->cur_cool_in_dram_list = 0;
  process->cur_cool_in_nvm_list = 0;
  process->need_cool_dram = false;
  process->need_cool_nvm = false;
  process->migrations_up = process->migrations_down = 0;
  process->migration_waits = 0;

  process->zero = request->zero;

  process->max_dram = request->req_dram;

#ifdef HEMEM_QOS  
  snprintf(&logpath[0], sizeof(logpath) - 1, "/tmp/log-%d.txt", process->pid);
  process->logfd = fopen(logpath, "w");
  if (process->logfd == NULL) {
    perror("process log fopen");
  }
  assert(process->logfd != NULL);
#endif

  add_process(process);

  pebs_add_process(process);

  response->header.status = SUCCESS;
  response->header.pid = pid;
  response->header.operation = ADD_PROCESS;
  response->header.msg_size = sizeof(struct add_process_response);

  ret = write_msg(fd, (char*)response, response->header.msg_size);
  if (ret != 0) {
    fprintf(stderr, "write fails\n");
    assert(0);
  }

  return process;
}

int ucm_remove_process(struct remove_process_request* request, struct remove_process_response* response)
{
  struct hemem_process* process;
  pid_t pid = request->header.pid;

  process = find_process(request->header.pid);
  if (process != NULL) {
    process->exited = true;
    remove_process(process);
    pebs_remove_process(process);

    //todo:free memory, pages, linked list, hash table...
    ring_buf_free(process->hot_ring);
    ring_buf_free(process->cold_ring);
    //free(process);
  }

  response->header.status = SUCCESS;
  response->header.pid = pid;
  response->header.operation = REMOVE_PROCESS;
  response->header.msg_size = sizeof(struct remove_process_response);

  return 0;
}

int ucm_alloc_space(struct alloc_request* request, struct alloc_response* response)
{
  pid_t pid = request->header.pid;
  struct hemem_process* process;
  struct hemem_page *page;
  void* addr;
  size_t length;
  uint64_t page_boundry;
  uint64_t offset;
  bool in_dram;
  uint64_t pagesize;
  void* ucm_addr;
  struct hemem_page_app* page_app = NULL;
 
  response->header.pid = pid;
  response->header.operation = ALLOC_SPACE;
  
  process = find_process(request->header.pid);
  if (process == NULL) {
    response->header.status = FAILED;
    response->num_pages = 0;
    response->header.msg_size = sizeof(struct alloc_response);
    //response->pages = NULL;
    return -1;
  }

  addr = request->addr;
  length = request->length;
  assert(addr != 0);
  assert(length != 0);

  for (page_boundry = (uint64_t)addr; page_boundry < (uint64_t)addr + length;) {
    page = pebs_pagefault(process);
    assert(page != NULL);

    offset = page->devdax_offset;
    in_dram = page->in_dram;
    pagesize = pt_to_pagesize(page->pt);

    ucm_addr = (in_dram ? dram_devdax_mmap + offset : nvm_devdax_mmap + offset);
//  #ifndef USE_DMA
//    hemem_parallel_memset(ucm_addr, 0, pagesize);
//  #else
    if (process->zero) {
       memset(ucm_addr, 0, pagesize);
    }
//  #endif
    memsets++;

    //LOG("allocated page for process %d at va %lx\n", pid, page_boundry);

    page->va = page_boundry;
    page->pid = process->pid;
    page->uffd = process->uffd;
    assert(page->va != 0);
    assert(page->va % HUGEPAGE_SIZE == 0);
    page->migrating = false;
    page->migrations_up = page->migrations_down = 0;

    add_page(process, page);

    page_app = &(response->pages[response->num_pages]);
    page_app->va = page_boundry;
    page_app->devdax_offset = offset;
    page_app->in_dram = in_dram;
    page_app->pt = page->pt;
    response->num_pages++;
    page_boundry += pagesize;

    mem_allocated += pagesize;
    process->mem_allocated += pagesize;
    pages_allocated++;
  }
  
  response->header.msg_size = sizeof(struct alloc_response) + response->num_pages * sizeof(struct hemem_page_app);
  response->header.status = SUCCESS;
  return 0;
}

int ucm_free_space(struct free_request* request, struct free_response* response)
{
  pid_t pid = request->header.pid;
  struct hemem_process* process;
  struct hemem_page* page;
  void* addr;
  size_t length;
  uint64_t page_boundry;
  uint64_t pagesize;

  response->header.pid = pid;
  response->header.operation = FREE_SPACE;
  response->header.msg_size = sizeof(struct free_response);

  process = find_process(request->header.pid);
  if (process == NULL) {
    response->header.status = FAILED;
    return -1;
  }

  addr = request->addr;
  length = request->length;
  assert(addr != 0);
  assert(length != 0);

  for (page_boundry = (uint64_t)addr; page_boundry < (uint64_t)addr + length;) {
    page = find_page(process, page_boundry);
    if (page != NULL) {
      pagesize = pt_to_pagesize(page->pt);

      remove_page(process, page);
      pebs_remove_page(process, page);
      mem_allocated -= pagesize;
      process->mem_allocated -= pagesize;
      pages_freed += 1;
      page_boundry += pagesize;
    } else {
      page_boundry += PAGE_SIZE;
    }
  } 
  
  response->header.status = SUCCESS;
  return 0;
}

int ucm_get_uffd(int fd, struct hemem_process* process, struct get_uffd_response* response)
{
  process->uffd = recv_fd(fd);
  process->valid_uffd = true;

  uffds[process->uffd] = process;
  add_epoll_ctl(fault_epoll_fd, process->uffd);

  response->header.status = SUCCESS;
  response->header.pid = process->pid;
  response->header.operation = GET_UFFD;
  response->header.msg_size = sizeof(struct get_uffd_response);
  return 0;
}

int ucm_record_remap_fd(int fd, struct record_remap_fd_request* request, struct record_remap_fd_response* response)
{
  pid_t pid = request->header.pid;
  struct hemem_process* process;

  response->header.pid = pid;
  response->header.operation = RECORD_REMAP_FD;
  response->header.msg_size = sizeof(struct record_remap_fd_response);

  process = find_process(request->header.pid);
  if (process == NULL) {
    response->header.status = FAILED;
    return -1;
  }

  process->remap_fd = fd;
  response->header.status = SUCCESS;
  return 0;
}


#ifdef STATS_THREAD
static void *hemem_stats_thread() {
  cpu_set_t cpuset;
  pthread_t thread;

  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(STATS_THREAD_CPU, &cpuset);
  int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }

  for (;;) {
    sleep(1);

    hemem_print_stats();
    hemem_clear_stats();
  }
  return NULL;
}
#endif

void add_process(struct hemem_process *process) {
  struct hemem_process *p;
  pthread_mutex_lock(&processes_lock);
  HASH_FIND(phh, processes, &(process->pid), sizeof(pid_t), p);
  assert(p == NULL);
  HASH_ADD(phh, processes, pid, sizeof(pid_t), process);
  pthread_mutex_unlock(&processes_lock);
}

void remove_process(struct hemem_process *process) {
  pthread_mutex_lock(&processes_lock);
  HASH_DELETE(phh, processes, process);
  pthread_mutex_unlock(&processes_lock);
}

struct hemem_process *find_process(pid_t pid) {
  struct hemem_process *process;
  pthread_mutex_lock(&processes_lock);
  HASH_FIND(phh, processes, &pid, sizeof(pid_t), process);
  pthread_mutex_unlock(&processes_lock);
  return process;
}

void add_page(struct hemem_process* process, struct hemem_page *page) {
  struct hemem_page *p;
  pthread_mutex_lock(&(process->pages_lock));
  HASH_FIND(hh, process->pages, &(page->va), sizeof(uint64_t), p);
  if (p != NULL) {
    // don't re-add page to hash table
    pthread_mutex_unlock(&(process->pages_lock));
    return;
  }
  HASH_ADD(hh, process->pages, va, sizeof(uint64_t), page);
  pthread_mutex_unlock(&(process->pages_lock));
}

void remove_page(struct hemem_process* process, struct hemem_page *page) {
  pthread_mutex_lock(&(process->pages_lock));
  HASH_DELETE(hh, process->pages, page);
  pthread_mutex_unlock(&(process->pages_lock));
}

struct hemem_page *find_page(struct hemem_process *process, uint64_t va) {
  struct hemem_page *page;
  pthread_mutex_lock(&(process->pages_lock));
  HASH_FIND(hh, process->pages, &va, sizeof(uint64_t), page);
  pthread_mutex_unlock(&(process->pages_lock));
  return page;
}

#if 0
static void hemem_ucm_mmap_populate(struct hemem_page *page) {
  void* addr;
  uint64_t offset;
  bool in_dram;
  uint64_t pagesize;

  assert(page != NULL);
  offset = page->devdax_offset;
  in_dram = page->in_dram;
  pagesize = pt_to_pagesize(page->pt);
  addr = (in_dram ? dram_devdax_mmap + offset : nvm_devdax_mmap + offset);
  assert((uint64_t)addr % HUGEPAGE_SIZE == 0);

#ifndef USE_DMA
  hemem_parallel_memset(addr, 0, pagesize);
#else
  memset(addr, 0, pagesize);
#endif
  memsets++;
}
#endif

int hemem_ucm_munmap(struct hemem_page *page) {
  int ret;
  void *addr;
  uint64_t offset;
  bool in_dram;
  uint64_t pagesize;

  assert(page != NULL);
  offset = page->devdax_offset;
  in_dram = page->in_dram;
  pagesize = pt_to_pagesize(page->pt);
  addr = (in_dram ? dram_devdax_mmap + offset : nvm_devdax_mmap + offset);
  assert((uint64_t)addr % HUGEPAGE_SIZE == 0);

  ret = munmap(addr, pagesize);
  return ret;
}


void hemem_ucm_migrate_up(struct hemem_process *process, struct hemem_page *page, uint64_t dram_offset) {
  void *old_addr;
  void *new_addr;
  struct timeval migrate_start, migrate_end;
  struct timeval remap_start, remap_end;
  struct timeval start, end;
  uint64_t old_addr_offset, new_addr_offset;
  uint64_t pagesize;
  struct hemem_page_app page_app;
#ifdef USE_DMA
  struct uffdio_dma_copy uffdio_dma_copy;
#endif

  assert(!page->in_dram);

#ifdef HEMEM_DEBUG
  LOG("hemem_migrate_up, pid: %d, migrate up addr: %lx, dramread: %" PRId64 ", nvmread: %" PRId64 "\n", page->pid, page->va, page->tot_accesses[DRAMREAD], page->tot_accesses[NVMREAD]);
#endif

  gettimeofday(&migrate_start, NULL);

  assert(page != NULL);

  pagesize = pt_to_pagesize(page->pt);

  old_addr_offset = page->devdax_offset;
  new_addr_offset = dram_offset;

  old_addr = nvm_devdax_mmap + old_addr_offset;
  assert((uint64_t)old_addr_offset < NVMSIZE);
  assert((uint64_t)old_addr_offset + pagesize <= NVMSIZE);

  new_addr = dram_devdax_mmap + new_addr_offset;
  assert((uint64_t)new_addr_offset < DRAMSIZE);
  assert((uint64_t)new_addr_offset + pagesize <= DRAMSIZE);

  gettimeofday(&start, NULL);
#ifdef USE_DMA
  uffdio_dma_copy.src[0] = (uint64_t)old_addr;
  uffdio_dma_copy.dst[0] = (uint64_t)new_addr;
  uffdio_dma_copy.len[0] = pagesize;
  uffdio_dma_copy.count = 1;
  uffdio_dma_copy.mode = 0;
  uffdio_dma_copy.copy = 0;
  if (ioctl(uffd, UFFDIO_DMA_COPY, &uffdio_dma_copy) == -1) {
    LOG("hemem_migrate_up, ioctl dma_copy fails for src:%p, dst:%p\n",
        old_addr, new_addr);
    assert(false);
  }
#else
#ifdef USE_PARALLEL_MEMCPY
  hemem_parallel_memcpy(new_addr, old_addr, pagesize);
#else
  memcpy(new_addr, old_addr, pagesize);
#endif // USE_PARALLEL_MEMCPY
#endif // ! USE_DMA
  gettimeofday(&end, NULL);
  LOG_TIME("memcpy_to_dram: %f s\n", elapsed(&start, &end));

  process->migrations_up++;
  page->migrations_up++;
  migrations_up++;

  page->devdax_offset = dram_offset;

  bytes_migrated += pagesize;

  page_app.va = page->va;
  page_app.devdax_offset = page->devdax_offset;
  page_app.in_dram = true;
  page_app.pt = page->pt;
  gettimeofday(&remap_start, NULL);
  remap_pages(process->pid, process->remap_fd, &page_app, 1);
  gettimeofday(&remap_end, NULL);
  LOG_TIME("hemem_remap_page_up: %f s\n", elapsed(&remap_start, &remap_end));
  gettimeofday(&migrate_end, NULL);
  LOG_TIME("hemem_migrate_up: %f s\n", elapsed(&migrate_start, &migrate_end));

  page->in_dram = true;
}

void hemem_ucm_migrate_down(struct hemem_process *process, struct hemem_page *page, uint64_t nvm_offset) {
  void *old_addr;
  void *new_addr;
  struct timeval migrate_start, migrate_end;
  struct timeval remap_start, remap_end;
  struct timeval start, end;
  uint64_t old_addr_offset, new_addr_offset;
  uint64_t pagesize;
  struct hemem_page_app page_app;
#ifdef USE_DMA
  struct uffdio_dma_copy uffdio_dma_copy;
#endif

  assert(page->in_dram);

#ifdef HEMEM_DEBUG
  LOG("hemem_migrate_down, pid: %d, migrate down addr: %lx, dramread: %" PRId64 ", nvmread: %" PRId64 "\n", page->pid, page->va, page->tot_accesses[DRAMREAD], page->tot_accesses[NVMREAD]);
#endif  
  
  gettimeofday(&migrate_start, NULL);

  pagesize = pt_to_pagesize(page->pt);

  assert(page != NULL);
  old_addr_offset = page->devdax_offset;
  new_addr_offset = nvm_offset;

  old_addr = dram_devdax_mmap + old_addr_offset;
  assert((uint64_t)old_addr_offset < DRAMSIZE);
  assert((uint64_t)old_addr_offset + pagesize <= DRAMSIZE);
  assert((uint64_t)old_addr % HUGEPAGE_SIZE == 0);

  new_addr = nvm_devdax_mmap + new_addr_offset;
  assert((uint64_t)new_addr_offset < NVMSIZE);
  assert((uint64_t)new_addr_offset + pagesize <= NVMSIZE);
  assert((uint64_t)new_addr % HUGEPAGE_SIZE == 0);

  gettimeofday(&start, NULL);
#ifdef USE_DMA
  uffdio_dma_copy.src[0] = (uint64_t)old_addr;
  uffdio_dma_copy.dst[0] = (uint64_t)new_addr;
  uffdio_dma_copy.len[0] = pagesize;
  uffdio_dma_copy.count = 1;
  uffdio_dma_copy.mode = 0;
  uffdio_dma_copy.copy = 0;
  if (ioctl(uffd, UFFDIO_DMA_COPY, &uffdio_dma_copy) == -1) {
    LOG("hemem_migrate_down, ioctl dma_copy fails for src:%p, dst:%p\n",
        old_addr, new_addr);
    assert(false);
  }
#else
#ifdef USE_PARALLEL_MEMCPY
  hemem_parallel_memcpy(new_addr, old_addr, pagesize);
#else
  memcpy(new_addr, old_addr, pagesize);
#endif // USE_PARALLEL_MEMCPY
#endif // ! USE_DMA
  gettimeofday(&end, NULL);
  LOG_TIME("memcpy_to_nvm: %f s\n", elapsed(&start, &end));

  process->migrations_down++;
  page->migrations_down++;
  migrations_down++;

  page->devdax_offset = nvm_offset;

  bytes_migrated += pagesize;

  page_app.va = page->va;
  page_app.devdax_offset = page->devdax_offset;
  page_app.in_dram = false;
  page_app.pt = page->pt;
  gettimeofday(&remap_start, NULL);
  remap_pages(process->pid, process->remap_fd, &page_app, 1);
  gettimeofday(&remap_end, NULL);
  LOG_TIME("hemem_remap_page_down: %f s\n", elapsed(&remap_start, &remap_end));

  gettimeofday(&migrate_end, NULL);
  LOG_TIME("hemem_migrate_down: %f s\n", elapsed(&migrate_start, &migrate_end));

  page->in_dram = false;
}

void hemem_ucm_wp_page(struct hemem_page *page, bool protect) {
  uint64_t addr = page->va;
  struct uffdio_writeprotect wp;
  struct timeval start, end;
  int ret;
  uint64_t pagesize = pt_to_pagesize(page->pt);

  // LOG("hemem_wp_page: wp addr %lx pte: %lx\n", addr,
  // hemem_va_to_pa(addr));

  assert(addr != 0);
  assert(addr % HUGEPAGE_SIZE == 0);

  gettimeofday(&start, NULL);
  wp.range.start = addr;
  wp.range.len = pagesize;
  wp.mode = (protect ? UFFDIO_WRITEPROTECT_MODE_WP : 0);
  ret = ioctl(page->uffd, UFFDIO_WRITEPROTECT, &wp);

  if (ret < 0) {
    if (errno == EBADF || errno == ENOENT) {
      // page was freed, probably fine to just ignore
      LOG("tried to write-protect a feeed page");
    } else {
      perror("uffdio writeprotect");
      assert(0);
    }
  }
  gettimeofday(&end, NULL);

  LOG_TIME("uffdio_writeprotect: %f s\n", elapsed(&start, &end));
}

void handle_wp_fault(struct hemem_process *process, uint64_t page_boundry) {
  struct hemem_page *page;
  //struct hemem_page_app page_app;

  page = find_page(process, page_boundry);
  assert(page != NULL);

  migration_waits++;
  process->migration_waits++;

  //LOG("hemem: handle_wp_fault: waiting for migration for page %lx\n",
  //    page_boundry);

  while (page->migrating)
    ;
}

void* process_request(int fd, void* request)
{
  int len;
  int ret;
  void* response;
  size_t request_size = ((struct msg_header*)request)->msg_size;

  response = (void*)calloc(1, MAX_SIZE);
  if (response == NULL) {
    perror("calloc error");
    assert(0);
  }
 
  ret = write_msg(fd, request, request_size);
  if (ret != 0) {
    perror("request send fails");
    assert(0);
  }

  len = read(fd, response, MAX_SIZE);
  if (len < sizeof(struct msg_header)) {
    perror("invalid header");
    assert(0);
  }

  return response;
}

int remap_pages(pid_t pid, int remap_fd,
                struct hemem_page_app* fault_pages,
                int num_fault_pages)
{
  struct remap_request* request;
  struct remap_response* response;
  size_t msg_size = sizeof(struct remap_request) + sizeof(struct hemem_page_app) * num_fault_pages;
  enum status_code status;

  request = (struct remap_request*)malloc(msg_size);
  if (request == NULL) {
    return -1;
  }
  memset(request, 0, msg_size);
  request->header.pid = pid;
  request->header.operation = REMAP_PAGES;
  request->header.msg_size = msg_size;

  request->num_pages = num_fault_pages;
  memcpy(request->pages, fault_pages, sizeof(struct hemem_page_app) * num_fault_pages);

  response = process_request(remap_fd, request);
  if (response->header.status != 0) {
    free(response);
    return -1;
  }
 
  free(request);

  status = response->header.status;
  free(response);
  return status;
}

void handle_missing_fault(struct hemem_process *process,
                        uint64_t page_boundry) {
  void *addr;
  struct timeval missing_start, missing_end;
  struct timeval start, end;
  struct hemem_page *page;
  uint64_t offset;
  bool in_dram;
  uint64_t pagesize;
  struct hemem_page_app page_app;

  assert(page_boundry != 0);

  gettimeofday(&missing_start, NULL);

  gettimeofday(&start, NULL);
  // let policy algorithm do most of the heavy lifting of finding a free page
  page = pebs_pagefault(process);
  assert(page != NULL);

  gettimeofday(&end, NULL);
  LOG_TIME("page_fault: %f s\n", elapsed(&start, &end));

  offset = page->devdax_offset;
  in_dram = page->in_dram;
  pagesize = pt_to_pagesize(page->pt);

  //LOG("handled page fault for process %u for va %lx\n", process->pid, page_boundry);

  addr = (in_dram ? dram_devdax_mmap + offset : nvm_devdax_mmap + offset);

//#ifdef USE_DMA
  if (process->zero) {
    memset(addr, 0, pagesize);
  }
//#else
//  hemem_parallel_memset(addr, 0, pagesize);
//#endif
  memsets++;

  // use mmap return addr to track new page's virtual address
  page->uffd = process->uffd;
  page->pid = process->pid;
  page->va = page_boundry;
  assert(page->va != 0);
  assert(page->va % HUGEPAGE_SIZE == 0);
  page->migrations_up = page->migrations_down = 0;

  mem_allocated += pagesize;
  process->mem_allocated += pagesize;

  // place in hemem's page tracking list
  add_page(process, page);


  missing_faults_handled++;
  pages_allocated++;
  gettimeofday(&missing_end, NULL);
  LOG_TIME("hemem_missing_fault: %f s\n",
           elapsed(&missing_start, &missing_end));

  page_app.va = page_boundry;
  page_app.devdax_offset = page->devdax_offset;
  page_app.in_dram = page->in_dram;
  page_app.pt = page->pt;

  remap_pages(process->pid, process->remap_fd, &page_app, 1);  
}

void *handle_fault() {
  static struct uffd_msg msg[MAX_UFFD_MSGS];
  ssize_t nread;
  uint64_t fault_addr;
  uint64_t fault_flags;
  uint64_t page_boundry;
  struct uffdio_range range;
  int ret;
  int nmsgs;
  int i;
  int num_ready_fds, ready_fd;
  struct epoll_event epoll_events[MAX_EVENTS];
  struct hemem_process *process;

  cpu_set_t cpuset;
  pthread_t thread;

  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(FAULT_THREAD_CPU, &cpuset);
  int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }

  for (;;) {
    num_ready_fds = epoll_wait(fault_epoll_fd, epoll_events, MAX_EVENTS, 0);
    for (i = 0; i < num_ready_fds; i++) {
      ready_fd = epoll_events[i].data.fd;

      if ((epoll_events[i].events & EPOLLERR) || (epoll_events[i].events & EPOLLHUP) || (!(epoll_events[i].events & EPOLLIN))) {
        fprintf(stderr, "epoll not EPOLLIN in for uffd\n");
        continue;
      }

      //TODO: map uffd to process
      process = uffds[ready_fd];

      nread = read(ready_fd, &msg[0], MAX_UFFD_MSGS * sizeof(struct uffd_msg));
      if (nread == 0) {
        fprintf(stderr, "EOF on userfaultfd\n");
        assert(0);
      }

      if (nread < 0) {
        if (errno == EAGAIN) {
          continue;
        }
        perror("read");
        assert(0);
      }

      if ((nread % sizeof(struct uffd_msg)) != 0) {
        fprintf(stderr, "invalid msg size: [%ld]\n", nread);
        assert(0);
      }

      nmsgs = nread / sizeof(struct uffd_msg);

      for (i = 0; i < nmsgs; i++) {
        // TODO: check page fault event, handle it
        if (msg[i].event & UFFD_EVENT_PAGEFAULT) {
          fault_addr = (uint64_t)msg[i].arg.pagefault.address;
          fault_flags = msg[i].arg.pagefault.flags;

          // allign faulting address to page boundry
          // huge page boundry in this case due to dax allignment
          page_boundry = fault_addr & ~(PAGE_SIZE - 1);
          struct hemem_page *found_page;
          if (fault_flags & UFFD_PAGEFAULT_FLAG_WP) {
            handle_wp_fault(process, page_boundry);
          } else if((found_page = find_page(process, page_boundry))) {
            LOG("Found existing page at %lx, migrating? %d\n", page_boundry, found_page->migrating);
            handle_wp_fault(process, page_boundry);
          } else {
            handle_missing_fault(process, page_boundry);
          }

          // wake the faulting thread
          range.start = (uint64_t)page_boundry;
          range.len = PAGE_SIZE;

          ret = ioctl(process->uffd, UFFDIO_WAKE, &range);

          if (ret < 0) {
            if (ret == -EBADF || ret == -ENOENT) {
              if (!process->exited) {
                perror("uffdio wake");
                assert(0);
              }
            } else {
              perror("uffdio wake");
              assert(0);
            }
          }
        } else if (msg[i].event & UFFD_EVENT_UNMAP) {
          fprintf(stderr, "Received an unmap event\n");
          assert(0);
        } else if (msg[i].event & UFFD_EVENT_REMOVE) {
          fprintf(stderr, "received a remove event\n");
          assert(0);
        } else {
          fprintf(stderr, "received a non page fault event\n");
          assert(0);
        }
      }
    }
  }
}

int process_msg(int fd)
{
  int ret;
  int len;
  char recv_buf[MAX_SIZE];
  char* send_buf;
  struct msg_header* request_header;
  struct msg_header* response_header;
  struct hemem_process* process;
  char* new_send_buf;
  struct alloc_request* alloc_req;

  len = read(fd, recv_buf, MAX_SIZE);
  if (len < sizeof(struct msg_header)) {
    fprintf(stderr, "invalid request\n");
    assert(0);
  }

  send_buf = malloc(MAX_SIZE);
  memset(send_buf, 0, MAX_SIZE);
  request_header = (struct msg_header*)recv_buf;

  #ifdef HEMEM_DEBUG
  printf("fd=%d, operation=%d\n", fd, request_header->operation);
  #endif

  switch(request_header->operation) {
  case ALLOC_SPACE:
    alloc_req = (struct alloc_request*)recv_buf;
    len = sizeof(struct alloc_response) + sizeof(struct hemem_page_app) * (alloc_req->length / HUGEPAGE_SIZE + (alloc_req->length % HUGEPAGE_SIZE != 0));
    if (len > MAX_SIZE) {
        new_send_buf = realloc(send_buf, len);
        send_buf = new_send_buf;
    }
    ret = ucm_alloc_space((struct alloc_request*)recv_buf, (struct alloc_response*)send_buf);
    break;
  case FREE_SPACE:
    ret = ucm_free_space((struct free_request*)recv_buf, (struct free_response*)send_buf);
    break;
  case ADD_PROCESS:
    process = ucm_add_process(fd, (struct add_process_request*)recv_buf, (struct add_process_response*)send_buf);
    memset(send_buf, 0, MAX_SIZE);
    ret = ucm_get_uffd(fd, process, (struct get_uffd_response*)send_buf);
    break;
  case REMOVE_PROCESS:
    ret = ucm_remove_process((struct remove_process_request*)recv_buf, (struct remove_process_response*)send_buf);
    break;
  case RECORD_REMAP_FD:
    ret = ucm_record_remap_fd(fd, (struct record_remap_fd_request*)recv_buf, (struct record_remap_fd_response*)send_buf);
    delete_epoll_ctl(request_epoll_fd, fd);
    break;
  default:
    fprintf(stderr, "invalid request\n");
    //response_header->operation = request_header->operation;
    //response_header->pid = request_header->pid;
    //response_header->status = INVALID_REQUEST;
    //response_header->msg_size = sizeof(struct msg_header);
    //break;
    return -1;
  }

  response_header = (struct msg_header*)send_buf;
  ret = write_msg(fd, send_buf, response_header->msg_size);
  free(send_buf);
  if (ret != 0) {
    fprintf(stderr, "write_msg fails\n");
    return -1;
  }

  return 0;
}

void *accept_new_app()
{

  int ret;
  int cli_fd;
  socklen_t cli_addr_len;
  struct sockaddr_un cli_addr;

  cpu_set_t cpuset;
  pthread_t thread;

  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(LISTEN_THREAD_CPU, &cpuset);
  int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }

  while (1) {
    cli_addr_len = sizeof(struct sockaddr_un);
    cli_fd = accept(listen_fd, (struct sockaddr*)&cli_addr, &cli_addr_len);
    #ifdef HEMEM_DEBUG   
    printf("accept, cli_fd=%d\n", cli_fd);
    #endif

    if (cli_fd == -1) {
      perror("accept");
      assert(0);
    }

    ret = add_epoll_ctl(request_epoll_fd, cli_fd);
    if (ret != 0) {
      perror("add_epoll_ctl");
      assert(0);
    }
  }
 
  return NULL;
}

void *handle_request()
{
  int num_ready_fds;
  struct epoll_event epoll_events[MAX_EVENTS];
  int ready_fd;
  int ret;
  cpu_set_t cpuset;
  pthread_t thread;

  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(REQUEST_THREAD_CPU, &cpuset);
  int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }

  while (1) {
    num_ready_fds = epoll_wait(request_epoll_fd, epoll_events, MAX_EVENTS, 0);
    for (int i = 0; i < num_ready_fds; i++) {
      ready_fd = epoll_events[i].data.fd;

      if ((epoll_events[i].events & EPOLLERR)
        || (epoll_events[i].events & EPOLLHUP)
        || (!(epoll_events[i].events & EPOLLIN))) {
        ret = delete_epoll_ctl(request_epoll_fd, ready_fd); 
        if (ret != 0) {
          perror("delete_epoll_ctl");
          //todo: what should we do if error
        } 
        fprintf(stderr, "deleted epoll fd %d\n", ready_fd);
        close(ready_fd);
        continue;
      }
   
      ret = process_msg(ready_fd);
      if (ret != 0) {
        perror("process_msg");
        assert(0);
      }
    }
  }
}

void hemem_print_stats() {
  LOG_STATS("mem_allocated: [%lu]\tpages_allocated: [%lu]\tmissing_faults_handled: "
      "[%lu]\tbytes_migrated: [%lu]\tmigrations_up: [%lu]\tmigrations_down: "
      "[%lu]\tmigration_waits: [%lu]\n",
      mem_allocated, pages_allocated, missing_faults_handled, bytes_migrated,
      migrations_up, migrations_down, migration_waits);
  pebs_stats();
}

void hemem_clear_stats() {
  pages_allocated = 0;
  pages_freed = 0;
  missing_faults_handled = 0;
  migrations_up = 0;
  migrations_down = 0;
}

struct hemem_page *get_hemem_page(struct hemem_process* process, uint64_t va) {
  return find_page(process, va);
}

void hemem_start_timing(void) { timing = true; }

void hemem_stop_timing(void) { timing = false; }
