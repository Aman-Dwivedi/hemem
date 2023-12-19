#ifndef HEMEM_SHARED_H
#define HEMEM_SHARED_H

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>

#define KB(x)		(((uint64_t)x) * 1024)
#define MB(x)		(KB(x) * 1024)
#define GB(x)		(MB(x) * 1024)
#define TB(x)		(GB(x) * 1024)

#define MEM_BARRIER() __sync_synchronize()

//#define HEMEM_DEBUG

#define NVMSIZE   (512L * (1024L * 1024L * 1024L))
#define DRAMSIZE  (128L * (1024L * 1024L * 1024L))

//#define NVMSIZE   (16L * (1024L * 1024L * 1024L))
//#define DRAMSIZE  (16L * (1024L * 1024L * 1024L))

#define DRAMPATH  "/dev/dax0.0"
#define NVMPATH   "/dev/dax1.0"

#define BASEPAGE_SIZE	  (4UL * 1024UL)
#define HUGEPAGE_SIZE 	(2UL * 1024UL * 1024UL)
#define GIGAPAGE_SIZE   (1024UL * 1024UL * 1024UL)
#define PAGE_SIZE 	    HUGEPAGE_SIZE
#define PAGE_ROUND_UP(x) (((x) + (HUGEPAGE_SIZE)-1) & (~((HUGEPAGE_SIZE)-1)))

#define FASTMEM_PAGES   ((DRAMSIZE) / (PAGE_SIZE))
#define SLOWMEM_PAGES   ((NVMSIZE) / (PAGE_SIZE))

#define BASEPAGE_MASK	(BASEPAGE_SIZE - 1)
#define HUGEPAGE_MASK	(HUGEPAGE_SIZE - 1)
#define GIGAPAGE_MASK   (GIGAPAGE_SIZE - 1)

#define BASE_PFN_MASK	(BASEPAGE_MASK ^ UINT64_MAX)
#define HUGE_PFN_MASK	(HUGEPAGE_MASK ^ UINT64_MAX)
#define GIGA_PFN_MASK   (GIGAPAGE_MASK ^ UINT64_MAX)

#define MAX_FAULT_PAGES 16

#define STATS_THREAD_CPU  (0)

enum memtypes {
  FASTMEM = 0,
  SLOWMEM = 1,
  NMEMTYPES,
};

enum pagetypes {
  HUGEP = 0,
  BASEP = 1,
  NPAGETYPES
};

enum operation {
  ALLOC_SPACE = 0,
  FREE_SPACE = 1,
  ADD_PROCESS = 2,
  REMOVE_PROCESS = 3,
  GET_UFFD = 4,
  REMAP_PAGES = 5,
  RECORD_REMAP_FD = 6
};

enum status_code {
  SUCCESS = 0,
  FAILED = 1,
  INVALID_REQUEST = 2,
  NO_SPACE = 3
};

struct hemem_page_app {
  uint64_t va;
  uint64_t ucm_va;
  uint64_t devdax_offset;
  bool in_dram;
  enum pagetypes pt;
};

struct msg_header {
  int status;
  pid_t pid;
  enum operation operation;
  size_t msg_size;
};

struct alloc_request {
  struct msg_header header;
  void* addr;
  size_t length;
};

struct alloc_response {
  struct msg_header header;
  size_t num_pages;
  struct hemem_page_app pages[];
};

struct free_request {
  struct msg_header header;
  void* addr;
  size_t length;
};

struct free_response {
  struct msg_header header;
};

struct add_process_request {
  struct msg_header header; 
  double target_miss_ratio;
  uint64_t req_dram;
  bool zero;
};

struct add_process_response {
  struct msg_header header;
};

struct remove_process_request {
  struct msg_header header;
};

struct remove_process_response {
  struct msg_header header;
};

struct get_uffd_request {
  struct msg_header header;
  long uffd;
};

struct get_uffd_response {
  struct msg_header header;
};

struct remap_request {
  struct msg_header header;
  size_t num_pages;
  struct hemem_page_app pages[];
};

struct remap_response {
  struct msg_header header;
};

struct record_remap_fd_request {
  struct msg_header header;
};

struct record_remap_fd_response {
  struct msg_header header;
};

static inline uint64_t pt_to_pagesize(enum pagetypes pt)
{
  switch(pt) {
  case HUGEP: return HUGEPAGE_SIZE;
  case BASEP: return BASEPAGE_SIZE;
  default: assert(!"Unknown page type");
  }
}

static inline enum pagetypes pagesize_to_pt(uint64_t pagesize)
{
  switch (pagesize) {
    case BASEPAGE_SIZE: return BASEP;
    case HUGEPAGE_SIZE: return HUGEP;
    default: assert(!"Unknown page ssize");
  }
}

#endif /* HEMEM_SHARED_H */
