#include <libsyscall_intercept_hook_point.h>
#include <syscall.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#define __USE_GNU
#include <dlfcn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <assert.h>

#define HUGEPAGE_SIZE 	(2UL * 1024UL * 1024UL)
#define PAGE_ROUND_UP(x) (((x) + (HUGEPAGE_SIZE)-1) & (~((HUGEPAGE_SIZE)-1)))


static int mmap_filter(void *addr, size_t length, int prot, int flags, int fd, off_t offset, uint64_t *result)
{
  void *p;

  fprintf(stderr, "mmap called with length %d\n ", length);

  if ((flags & MAP_ANONYMOUS) != MAP_ANONYMOUS) {
    return 1;
  }

  if ((flags & MAP_STACK) == MAP_STACK) {
    return 1;
  }
/*
  if (length < 1UL * 1024UL * 1024UL * 1024UL) {
    return 1;
  }
*/
  if ((flags & MAP_PRIVATE) == MAP_PRIVATE) {
    flags &= ~MAP_PRIVATE;
    flags |= MAP_SHARED;
  }

  if ((flags & MAP_ANONYMOUS) != MAP_ANONYMOUS) {
    flags |= MAP_ANONYMOUS;
    fd = -1;
  }

  if ((flags & MAP_HUGETLB) != MAP_HUGETLB) {
    flags |= MAP_HUGETLB;
  }

  if ((flags & MAP_POPULATE) != MAP_POPULATE) {
    flags |= MAP_POPULATE;
  }

  // reserve block of memory
  length = PAGE_ROUND_UP(length);

  return 1;
}

static int hook(long syscall_number, long arg0, long arg1, long arg2, long arg3,	long arg4, long arg5,	long *result)
{
	if (syscall_number == SYS_mmap) {
	  return mmap_filter((void*)arg0, (size_t)arg1, (int)arg2, (int)arg3, (int)arg4, (off_t)arg5, (uint64_t*)result);
  } else {
    // ignore non-mmap system calls
		return 1;
	}
}
  
static __attribute__((constructor)) void
init(void)
{
	// Set up the callback function
	intercept_hook_point = hook;
}
