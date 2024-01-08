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
#include <linux/userfaultfd.h>
#include <poll.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sched.h>

#include "hemem-shared.h"
#include "channel-shared.h"
#include "channel-app.h"
#include "hemem-app.h"
#include "logging.h"

pthread_t remap_thread;

int dramfd = -1;
int nvmfd = -1;
pid_t pid;
long uffd = -1;
uint64_t msg_id = 0;
bool is_init = false;
__thread bool internal_call = false;
volatile bool enable_remap = false;
pthread_mutex_t channel_lock;

int request_fd;
int remap_fd;

double target_miss_ratio =  0.1;

uint64_t required_dram = DRAMSIZE;

static __thread struct msg_header *response;

void* process_request(int fd, void* request)
{
  int len;
  size_t request_size;
  struct msg_header* request_header;
  //struct msg_header* new_response;
  int ret;

  memset(response, 0, sizeof(*response));

  request_header = (struct msg_header*)request;
  request_size = request_header->msg_size;
  
  if (fd == request_fd) {
      pthread_mutex_lock(&channel_lock);
  }

  #ifdef HEMEM_DEBUG
  printf("client sends request, fd=%d, operation=%d, request_size=%ld\n", fd, request_header->operation, request_size);
  #endif

  if (request_header->operation == GET_UFFD) {
    send_fd(fd, NULL, 0, ((struct get_uffd_request*)request)->uffd);
  }
  else {
    ret = write_msg(fd, request, request_size);
    if (ret != 0) {
      perror("request send fails");
      assert(0);
    }
  }

  len = read(fd, response, MAX_SIZE);
  if (len < sizeof(struct msg_header)) {
    perror("invalid header");
    assert(0);
  }

  #ifdef HEMEM_DEBUG
  printf("fd=%d, operation=%d\n", fd, request_header->operation);
  #endif

  assert(len == response->msg_size);
/*  if (len != response.msg_size) {
    new_response = realloc(response, response->msg_size);
    response = new_response;
    ret = read_msg(fd, (char*)response + len, response->msg_size - len);
    if (ret != 0) {
      assert(0);
    }
  }
*/
  if (fd == request_fd) {
    pthread_mutex_unlock(&channel_lock);
  }
  
  if (request_header->operation == RECORD_REMAP_FD) {
    enable_remap = true;
  }
  return response;
}

struct alloc_response* alloc_space(void *addr, size_t length)
{
  struct alloc_request request;
  struct alloc_response* response;

  request.header.operation = ALLOC_SPACE;
  request.header.pid = pid;
  request.header.msg_size = sizeof(request);
  request.addr = addr;
  request.length = length;
  
  response = process_request(request_fd, &request);

  //todo: parse the response, which should use malloc
  return response;
}

int free_space(void *addr, size_t length)
{
  struct free_request request;
  struct free_response* response;
  enum status_code status;

  request.header.operation = FREE_SPACE;
  request.header.pid = pid;
  request.header.msg_size = sizeof(request);
  request.addr = addr;
  request.length = length;
  
  response = process_request(request_fd, &request);
  status = response->header.status;  

  return status;
}

int add_process()
{
  struct add_process_request request;
  struct add_process_response* response;
  enum status_code status;

  request.header.operation = ADD_PROCESS;
  request.header.pid = pid;
  request.target_miss_ratio = target_miss_ratio;
  request.req_dram = required_dram;
  request.header.msg_size = sizeof(request);
#ifdef LLAMA
  request.zero = true;
#else
  request.zero = false;
#endif

  response = process_request(request_fd, &request);
  status = response->header.status;  

  return status;
}

int remove_process()
{
  struct remove_process_request request;
  struct remove_process_response* response;
  enum status_code status;

  request.header.operation = REMOVE_PROCESS;
  request.header.pid = pid;
  request.header.msg_size = sizeof(request);

  // here it uses the remap_fd to let the central manager record the remap sock fd
  response = process_request(request_fd, &request);
  status = response->header.status;  

  return status;
}

int get_uffd(long uffd)
{
  struct get_uffd_request request;
  struct get_uffd_response* response;
  enum status_code status;

  request.header.operation = GET_UFFD;
  request.header.pid = pid;
  request.header.msg_size = sizeof(request);
  request.uffd = uffd;

  response = process_request(request_fd, &request);
  status = response->header.status;  

  return status;
}

int record_remap_fd()
{
  struct record_remap_fd_request request;
  struct record_remap_fd_response* response;
  enum status_code status;

  request.header.operation = RECORD_REMAP_FD;
  request.header.pid = pid;
  request.header.msg_size = sizeof(request);

  response = process_request(remap_fd, &request);
  status = response->header.status;

  return status;
}

void remap_page(struct hemem_page_app* page)
{
  void *newptr;
  uint64_t pagesize;
  int fd;
  bool in_dram = page->in_dram;

  pagesize = pt_to_pagesize(page->pt);

  if (in_dram) {
    fd = dramfd;
  }
  else {
    fd = nvmfd;
  }

  newptr = libc_mmap((void*)page->va, pagesize, 
                    PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_POPULATE | MAP_FIXED,
                    fd, page->devdax_offset);
  if (newptr == MAP_FAILED) {
    perror("newptr mmap");
    assert(0);
  }

  // re-register new mmap region with userfaultfd
  struct uffdio_register uffdio_register;
  uffdio_register.range.start = (uint64_t)newptr;
  uffdio_register.range.len = pagesize;
  uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;
  uffdio_register.ioctls = 0;
  if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
    perror("ioctl uffdio_register");
    assert(0);
  }
  assert((uint64_t)newptr != 0);
  assert((uint64_t)newptr % pagesize == 0);
}

void *handle_remap()
{
  char remap_buf[MAX_SIZE];
  int len;
  struct remap_request* request;
  struct remap_response response;
  struct msg_header* header;
  int num_pages;

  internal_call = true;
#if 0
  cpu_set_t cpuset;
  pthread_t thread;

  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(REMAP_THREAD_CPU, &cpuset);
  int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }
#endif

  while (1) {
    if (!enable_remap) {
      continue;
    }

    memset(remap_buf, 0, MAX_SIZE);
    len = read(remap_fd, remap_buf, MAX_SIZE);
    if (len < sizeof(header)) {
      perror("invalid header");
      assert(0);
    }

    header = (struct msg_header*)remap_buf;
    if(header->operation != REMAP_PAGES) {
      perror("Invaid remap request");
      assert(0);
    }

    request = (struct remap_request*)remap_buf;
    num_pages = request->num_pages;

    for (int i = 0; i < num_pages; i++) {
      remap_page(&(request->pages[i]));
    }

    response.header.status = SUCCESS;
    response.header.pid = pid;
    response.header.msg_size = sizeof(response);
    response.header.operation = REMAP_PAGES;

    len = write(remap_fd, &response, sizeof(response));
    if (len < 0) {
      perror("remap reply");
      assert(0);
    }
  }
  internal_call = false;
}

void hemem_app_init()
{
  struct uffdio_api uffdio_api;
  internal_call = true;
  enum status_code status;
  char log_name[64];
  char* target_miss_ratio_str = NULL;
  char* target_dram_str = NULL;

  response = (void*)calloc(1, MAX_SIZE);
  if (response == NULL) {
    perror("calloc error");
    assert(0);
  }

  pid = getpid();
  printf("process id = %d\n", pid);
  sprintf(log_name, "logs-app-%d", pid);

  log_init("app");

  LOG("hemem_app_init: started\n");

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

  uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
  if (uffd == -1) {
    perror("uffd");
    assert(0);
  }

  uffdio_api.api = UFFD_API;
  uffdio_api.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP |  UFFD_FEATURE_MISSING_SHMEM | UFFD_FEATURE_MISSING_HUGETLBFS;
  uffdio_api.ioctls = 0;
  if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
    perror("ioctl uffdio_api");
    assert(0);
  }

  request_fd = channel_client_init(REQUEST);
  if (request_fd < 0) {
    perror("channel_init");
    assert(0);
  }

  #ifdef HEMEM_DEBUG
  printf("request_fd=%d\n", request_fd);
  #endif

  remap_fd = channel_client_init(REMAP);
  if (remap_fd < 0) {
    perror("channel_init");
    assert(0);
  }

  #ifdef HEMEM_DEBUG
  printf("remap_fd=%d\n", remap_fd);
  #endif

  if (pthread_mutex_init(&channel_lock, NULL) != 0) {
    perror("mutex init");
    assert(0);
  }

  if (pthread_create(&remap_thread, NULL, handle_remap, 0)) {
    perror("pthread create");
    assert(0);  
  }

  target_miss_ratio_str = getenv("MISS_RATIO");
  if (target_miss_ratio_str != NULL) {
    target_miss_ratio = atof(target_miss_ratio_str);
  }

  LOG("miss_ratio=%f\n",  target_miss_ratio);

  target_dram_str = getenv("REQ_DRAM");
  if (target_dram_str != NULL) {
    required_dram = atoll(target_dram_str);
  }
  fprintf(stderr, "DRAM Requested: %lu\n", required_dram);
  
  status = add_process();
  if (status != 0) {
    perror("add process");
    assert(0);
  }

  status = get_uffd(uffd);
  if (status != 0) {
    perror("get uffd");
    assert(0);
  }

  status = record_remap_fd();
  if (status != 0) {
    LOG("status = %u\n", status);
    perror("record remap fd");
    assert(0);
  }

  is_init = true;

  LOG("hemem_app_init: finished\n");

  internal_call = false;
}

void hemem_app_stop()
{
  remove_process();
}

static void hemem_mmap_populate(void* addr, size_t length)
{
  void* newptr;
  uint64_t offset;
  struct hemem_page_app *page;
  bool in_dram;
  uint64_t page_boundry;
  uint64_t pagesize;
  struct alloc_response* response;
  #ifndef ONE_MEM_REQUEST
  size_t req_mem_size;
  size_t remaining_length = length;
  #endif

  assert(addr != 0);
  assert(length != 0);

  #ifdef ONE_MEM_REQUEST
  response = alloc_space((void*)addr, length);
  if (response->header.status != 0) {
    perror("hemem_mmap_populate allloc fails");
    assert(0);
  }
  #endif
 
  for (page_boundry = (uint64_t)addr; page_boundry < (uint64_t)addr + length;) {
    int index = 0;
    #ifndef ONE_MEM_REQUEST
    req_mem_size = remaining_length > MAX_MEM_LEN_PER_REQ ? MAX_MEM_LEN_PER_REQ : remaining_length;
    response = alloc_space((void*)page_boundry, req_mem_size);
    if (response->header.status != 0) {
        perror("hemem_mmap_populate allloc fails");
        assert(0);
    }

    remaining_length -= req_mem_size;
    #endif
    int num_pages = response->num_pages;
    while (index < num_pages) {
        page = &(response->pages[index++]);
        assert(page != NULL);

        offset = page->devdax_offset;
        in_dram = page->in_dram;
        pagesize = pt_to_pagesize(page->pt);

        // now that we have an offset determined via the policy algorithm, actually map
        // the page for the application
        newptr = libc_mmap((void*)page_boundry, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE | MAP_FIXED, (in_dram ? dramfd : nvmfd), offset);
        if (newptr == MAP_FAILED) {
          perror("newptr mmap");
          assert(0);
        }

        if (newptr != (void*)page_boundry) {
          fprintf(stderr, "hemem: mmap populate: warning, newptr != page boundry\n");
        }

        // re-register new mmap region with userfaultfd
        struct uffdio_register uffdio_register;
        uffdio_register.range.start = (uint64_t)newptr;
        uffdio_register.range.len = pagesize;
        uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;
        uffdio_register.ioctls = 0;
        if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
          perror("ioctl uffdio_register");
          assert(0);
        }

        assert((uint64_t)newptr != 0);
        assert((uint64_t)newptr % pagesize == 0);

        page_boundry += pagesize;
    }
  }

}

#define PAGE_ROUND_UP(x) (((x) + (HUGEPAGE_SIZE)-1) & (~((HUGEPAGE_SIZE)-1)))

void* hemem_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  void *p;

  internal_call = true;

  assert(is_init);
  assert(length != 0);
  
  if ((flags & MAP_PRIVATE) == MAP_PRIVATE) {
    flags &= ~MAP_PRIVATE;
    flags |= MAP_SHARED;
    LOG("hemem_mmap: changed flags to MAP_SHARED\n");
  }

  if ((flags & MAP_ANONYMOUS) == MAP_ANONYMOUS) {
    flags &= ~MAP_ANONYMOUS;
    LOG("hemem_mmap: unset MAP_ANONYMOUS\n");
  }

  if ((flags & MAP_HUGETLB) == MAP_HUGETLB) {
    flags &= ~MAP_HUGETLB;
    LOG("hemem_mmap: unset MAP_HUGETLB\n");
  }

  if (addr != NULL) {
    flags |= MAP_FIXED;
  }
  
  // reserve block of memory
  length = PAGE_ROUND_UP(length);
  p = libc_mmap(addr, length, prot, flags, dramfd, offset);
  if (p == NULL || p == MAP_FAILED) {
    perror("mmap");
  }
  assert(p != NULL && p != MAP_FAILED);
  if (addr != NULL) {
    assert(p == addr);
  }
  
  // register with uffd
  struct uffdio_register uffdio_register;
  uffdio_register.range.start = (uint64_t)p;
  uffdio_register.range.len = length;
  uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;
  uffdio_register.ioctls = 0;
  if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
    perror("ioctl uffdio_register");
    assert(0);
  }
   
//  if ((flags & MAP_POPULATE) == MAP_POPULATE) {
    hemem_mmap_populate(p, length);
//  }
  
  internal_call = false;
  
  return p;
}

int hemem_munmap(void* addr, size_t length)
{
  int ret;

  internal_call = true;
  //fprintf(stderr, "munmap(%p, %lu)\n", addr, length);

  #ifdef ONE_MEM_REQUEST
  free_space(addr, length);
  #else
  size_t remaining_length = length;
  size_t req_mem_size;
  uint64_t page_boundry;
  for (page_boundry = (uint64_t)addr; page_boundry < (uint64_t)addr + length;) {
    req_mem_size = (remaining_length > MAX_MEM_LEN_PER_REQ) ? MAX_MEM_LEN_PER_REQ : remaining_length;
    free_space((void*)page_boundry, req_mem_size);
    remaining_length -= req_mem_size;
    page_boundry += req_mem_size;
  }
  #endif
  
  struct uffdio_range uffdio_range;
  uffdio_range.start = (uint64_t)addr;
  uffdio_range.len = length;
  if (ioctl(uffd, UFFDIO_UNREGISTER, &uffdio_range) == -1) {
    perror("ioctl uffdio_unregister");
    assert(0);
  }

  ret = libc_munmap(addr, length);
  if (ret != 0) {
      perror("libc_mumap");
      assert(0);
  }

  internal_call = false;

  return ret;
}
