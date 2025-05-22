#include <stdbool.h>
#include <stddef.h>

#include <unistd.h>

#define NUMTAGS 10
#define PAGE_SIZE sysconf(_SC_PAGESIZE)

// Smallest size of a memory chunk
#define MIN_CHUNK_SIZE 64

// Linked List node for a chunk of sub-page memory.
// This struct will always precede the data it refers to in memory.
struct MemRegion {
  size_t size;
  struct MemRegion *next;
};

// Linked List node for a page. Contains metadata as well as a pointer to the
// page itself and will always sit at the beginning of a page (offset 0x0)
struct PageInfo {
  size_t tag;

  // This value represents the total number of bytes that `mmap` was called with
  size_t page_size;

  void *page_addr;
  struct PageInfo *next;

  // These head nodes will be placed directly after the PageInfo struct
  struct MemRegion *free_head;
  struct MemRegion *alloc_head;

  // These tail nodes will help determine how much space is left in the page
  struct MemRegion *free_tail;
  struct MemRegion *alloc_tail;
};

/*
 * Memory layout idea:
|PageInfo|MemRegion head|....................|MemRegion|<data>..................
                                             ^          ^^^^^^
                                           curr         size page size
*/

// Allocates `size` bytes in a page marked with `tag`.
void *hemalloc(size_t size, size_t tag);

// Frees the memory at the address given by `ptr`.
void freehee(void *ptr);

// Allocates a given number of pages as a single page.
struct PageInfo *create_page(size_t num_pages, size_t tag);

struct MemRegion *get_free_chunk(struct PageInfo *page_info, size_t size);

struct MemRegion *find_free(struct MemRegion *ptr, size_t size);
