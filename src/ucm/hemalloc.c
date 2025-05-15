#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/mman.h>
#include <unistd.h>

#include "hemalloc.h"

// Array of page heads. Each entry in this array is the head of a linked
// list PageInfo struct, which is allocated with mmap
//
// All entries in this array are initially NULL, and are created upon first
// invocation
struct PageInfo *tag_heads[NUMTAGS] = {0};

void *hemalloc(size_t size, size_t tag) {
  // If page has not been intialized yet, call mmap to create one
  if (tag_heads[tag] == NULL) {
    struct PageInfo *new_page_head = create_page(size, tag);
    if (new_page_head == NULL) {
      perror("create_page failed");
      return NULL;
    }
    tag_heads[tag] = new_page_head;
  }
  return 0;
}

void hefree(void *ptr) {
  ;
  return;
}

struct PageInfo *create_page(size_t size, size_t tag) {
  // Allocate the smallest multiple of PAGE_SIZE that is >= n
  size_t total_size = PAGE_SIZE * ((size + PAGE_SIZE - 1) / PAGE_SIZE);

  void *page_addr = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (page_addr == MAP_FAILED) {
    perror("mmap failed");
    return NULL;
  }

  // Cast the allocated page as a PageInfo pointer to set the contents.
  // The PageInfo struct will sit at the start of each page (offset 0x0)
  struct PageInfo *page_info = (struct PageInfo *)page_addr;
  page_info->tag = tag;
  page_info->page_addr = page_addr;
  page_info->page_size = total_size;
  page_info->head = create_node();
  page_info->next = NULL;

  return page_info;
}

struct MemRegion create_node() {
  struct MemRegion region = {
      .size = 0,
      .next = NULL,
  };
  return region;
}

void *allocate(struct PageInfo *page_info, size_t size) {
  // Find the smallest open slot that's larger than `size`
  struct MemRegion *curr = &(page_info->head);
  struct MemRegion *prev = &(page_info->head);
  while (true) {
    if (curr == NULL) { // This path means we've hit the end of the list

      // This is because `head` is never NULL (see `create_page()`)

      // In this case, we want to see if there is space left in this page
      // Remember that every allocation is prefixed with a `struct MemRegion`,
      // so add that to the size comparison
      uintptr_t offset = (uintptr_t)curr % PAGE_SIZE;
      if (page_info->page_size - offset >= size + sizeof(struct MemRegion)) {
        // If there is space left, we need to create a new node and append it to
        // the Linked List We do this by writing to `prev + prev->size`

        // NOTE: This pointer casting needs to be discussed. Compiler supposedly
        // implicitly converts `prev->size` to `prev->size * sizeof(struct
        // MemRegion)`, thus requiring the cast to `(char *)` to avoid this case
        struct MemRegion *new_node_addr =
            (struct MemRegion *)((char *)prev + prev->size);
        *new_node_addr = create_node();
        prev->next = new_node_addr;
        curr = prev->next;
        break;
      } else {
        // No space left in this page, caller should call `create_page()` again.
        return NULL;
      }
    } else if (curr->size > 0) {
      // This path means the current node is allocated
      goto next_iter;
    } else { // We've found an open slot!
      // NOTE: I want to design the Linked List with the invariant that the tail
      // of the list will NEVER be empty. That is, the tail of the list should
      // ALWAYS be a chunk of allocated memory. This simplified the logic here
      // and allows us to avoid checking the (curr->next == NULL) case.

      size_t region_size = curr->next - curr + sizeof(struct MemRegion);
    }
  next_iter:
    // March pointers forward
    prev = curr;
    curr = curr->next;
    continue;
  }
}
