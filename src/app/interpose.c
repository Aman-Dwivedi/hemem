#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <libsyscall_intercept_hook_point.h>
#include <syscall.h>
#include <errno.h>
#include <stdint.h>
#define __USE_GNU
#include <dlfcn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <assert.h>
#include <unistd.h>
#include <malloc.h>

#include "interpose.h"
#include "hemem-app.h"
#include "logging.h"

void* (*libc_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset) = NULL;
int (*libc_munmap)(void *addr, size_t length) = NULL;
void* (*libc_malloc)(size_t size) = NULL;
void (*libc_free)(void* ptr) = NULL;
void* (*libc_sbrk)(intptr_t increment) = NULL;

static int mmap_filter(void *addr, size_t length, int prot, int flags, int fd, off_t offset, uint64_t *result)
{

  //TODO: figure out which mmap calls should go to libc vs hemem
  // non-anonymous mappings should probably go to libc (e.g., file mappings)
  if (((flags & MAP_ANONYMOUS) != MAP_ANONYMOUS) && !((fd == dramfd) || (fd == nvmfd))) {
    //LOG("hemem interpose: calling libc mmap due to non-anonymous, non-devdax mapping: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    return 1;
  }

  if ((prot & PROT_EXEC) == PROT_EXEC) {
    // filter out code mappings
    return 1;
  }

  if ((flags & MAP_STACK) == MAP_STACK) {
    // pthread mmaps are called with MAP_STACK
    //LOG("hemem interpose: calling libc mmap due to stack mapping: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    return 1;
  }
  
  if ((fd == dramfd) || (fd == nvmfd)) {
    //LOG("hemem interpose: calling libc mmap due to hemem devdax mapping\n");
    return 1;
  }

  if (internal_call) {
    //LOG("hemem interpose: calling libc mmap due to internal memory call: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    return 1;
  }
  
  if (!is_init) {
    //LOG("hemem interpose: calling libc mmap due to hemem init in progress\n");
    return 1;
  }

#ifndef LLAMA
  if (length < 1UL * 1024UL * 1024UL * 1024UL) {
    LOG("hemem interpose calling libc mmap due to small allocation size: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    return 1;
  }
#endif

  LOG("hemem interpose: calling hemem mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
  if ((*result = (uint64_t)hemem_mmap(addr, length, prot, flags, fd, offset)) == (uint64_t)MAP_FAILED) {
    // hemem failed for some reason, try libc
    LOG("hemem mmap failed\n\tmmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
  }
  return 0;
}


static int munmap_filter(void *addr, size_t length, uint64_t* result)
{
  if (internal_call) {
    return 1;
  }

#ifndef LLAMA
  if (length < 1UL * 1024UL * 1024UL * 1024UL) {
    //LOG("hemem interpose calling libc munmap due to small allocation size: munmap(0x%lx, %ld)\n", (uint64_t)addr, length);
    return 1;
  }
#endif

  if ((*result = hemem_munmap(addr, length)) == -1) {
    //LOG("hemem munmap failed\n\tmunmap(0x%lx, %ld)\n", (uint64_t)addr, length);
  }
  return 0;
}

#ifdef LLAMA
#define LLAMA_SIZE (80UL * 1024UL * 1024UL * 1024UL)
#endif

#if 0
//extern uint64_t end;
void* original_end = NULL;
void* original_end_after_llama_size = NULL;
void* llama_end = NULL;
extern void* end;

void* sbrk(intptr_t increment)
{
#ifdef LLAMA
  printf("sbrk called with increment %ld\n", increment);

  if (internal_call) {
    // ignore, some internal library function called malloc and we don't want to handle our own allocations
    return libc_sbrk(increment);
  }

  if (increment <= 0) {
    // ignore, malloc/free could have been called with a negative increment to free memory
    return end;
  }

  internal_call = true;
  
  // get old program break and increase it to the llama model size plus extra page for alignment
  void* old_end = libc_sbrk(LLAMA_SIZE + HUGEPAGE_SIZE);
  assert(old_end != (void*)-1);
  original_end = old_end;

  // align to hugepage address
  void* old_end_aligned = (void*)((uint64_t)old_end + ((-(uint64_t)old_end) & (HUGEPAGE_SIZE - 1)));
  
  original_end_after_llama_size = (void*)((uint64_t)old_end_aligned + LLAMA_SIZE);
  assert(((uint64_t)old_end_aligned % HUGEPAGE_SIZE) == 0);
  fprintf(stderr, "brk called, old end: %p\tmapping at aligned %p\n", old_end, old_end_aligned);
  
  // call hemem_mmap to map and manage the memory for the llama model
  void *p = hemem_mmap(old_end_aligned, LLAMA_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  assert(p == old_end_aligned);

  //end = (uint64_t)old_end_aligned + LLAMA_SIZE;
  internal_call = false;
  return old_end_aligned;
#else
  return libc_sbrk(increment);
#endif
}

static int brk_filter(void* addr, uint64_t* result)
{
#ifdef LLAMA
  printf("brk called with addr %p\n", addr);

  if (internal_call) {
    // ignore, some internal library function called malloc and we don't want to handle our own allocations
    return 1;
  }

  if (original_end != NULL) {
    if ((addr >= original_end) && (addr < original_end_after_llama_size)) {
      // ignore, we've already allocated enough memory for the llama model
      return 0;
    }
    else {
      // ignore, let libc malloc resovle this call; addr is not within the region we already allocated for some reason
      return 0;
    }
  }

  internal_call = true;
  
  // get old program break and increase it to the llama model size plus extra page for alignment
  void* old_end = sbrk(LLAMA_SIZE + HUGEPAGE_SIZE);
  assert(old_end != (void*)-1);
  original_end = old_end;

  // align to hugepage address
  void* old_end_aligned = (void*)((uint64_t)old_end + ((-(uint64_t)old_end) & (HUGEPAGE_SIZE - 1)));
  
  original_end_after_llama_size = (void*)((uint64_t)old_end_aligned + LLAMA_SIZE);
  assert(((uint64_t)old_end_aligned % HUGEPAGE_SIZE) == 0);
  fprintf(stderr, "brk called, old end: %p\tmapping at aligned %p\n", old_end, old_end_aligned);
  
  // call hemem_mmap to map and manage the memory for the llama model
  result = hemem_mmap(old_end_aligned, LLAMA_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  
  //end = (uint64_t)old_end_aligned + LLAMA_SIZE;
  internal_call = false;
  return 0;
#else
  return 1;
#endif
}
#endif

static void* bind_symbol(const char *sym)
{
  void *ptr;
  if ((ptr = dlsym(RTLD_NEXT, sym)) == NULL) {
    fprintf(stderr, "hemem memory manager interpose: dlsym failed (%s)\n", sym);
    abort();
  }
  return ptr;
}

static int hook(long syscall_number, long arg0, long arg1, long arg2, long arg3,	long arg4, long arg5,	long *result)
{
	if (syscall_number == SYS_mmap) {
	  return mmap_filter((void*)arg0, (size_t)arg1, (int)arg2, (int)arg3, (int)arg4, (off_t)arg5, (uint64_t*)result);
	} else if (syscall_number == SYS_munmap) {
    return munmap_filter((void*)arg0, (size_t)arg1, (uint64_t*)result);
  } else {
    // ignore non-mmap system calls
		return 1;
	}
}

static __attribute__((constructor)) void init(void)
{
  libc_mmap = bind_symbol("mmap");
  libc_munmap = bind_symbol("munmap");
  libc_malloc = bind_symbol("malloc");
  libc_free = bind_symbol("free");
  intercept_hook_point = hook;

#ifdef LLAMA
  int ret = mallopt(M_MMAP_THRESHOLD, 0);
  if (ret != 1) {
    perror("mallopt");
  }
  assert(ret == 1);
#endif

  hemem_app_init();
}

static __attribute__((destructor)) void hemem_shutdown(void)
{
  hemem_app_stop();
}
