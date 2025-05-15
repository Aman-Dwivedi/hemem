#include <stdbool.h>
#include <stddef.h>

#include <unistd.h>

#define NUMTAGS 10
#define PAGE_SIZE sysconf(_SC_PAGESIZE)

// Linked List node that represents a region of `size` bytes in sub-page memory.
//
// If `size` is `0`, then the region is unused, and its true size is given by
// `next - curr`, where `curr` is the address of this struct
struct MemRegion {
  size_t size;
  struct MemRegion *next;
};

// Linked List node for a page. Contains metadata as well as a pointer to the
// page itself and will always sit at the beginning of a page (offset 0x0)
struct PageInfo {
  size_t tag;
  size_t page_size;
  void *page_addr;
  struct PageInfo *next;
  struct MemRegion head;
};

// Allocates `size` bytes in a page marked with `tag`.
void *hemalloc(size_t size, size_t tag);

// Frees the memory at the address given by `ptr`.
void hefree(void *ptr);

// Allocates an empty page and sets the beginning metadata.
struct PageInfo *create_page(size_t size, size_t tag);

// Creates a Linked List node for a free memory region.
//
// To indicate that this region is in use, set the `size` field.
struct MemRegion create_node();

void *allocate(struct PageInfo *page_info, size_t size);

struct MemRegion *find_free(struct MemRegion *ptr, size_t size);
