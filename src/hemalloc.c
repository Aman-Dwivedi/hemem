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
  // Clamp size to MIN_CHUNK_SIZE
  if (size < MIN_CHUNK_SIZE)
    size = MIN_CHUNK_SIZE;

  // If page has not been intialized yet, call mmap to create one
  if (tag_heads[tag] == NULL) {
    struct PageInfo *new_page_head = create_page(size, tag);
    if (new_page_head == NULL) {
      perror("create_page failed");
      return NULL;
    }
    tag_heads[tag] = new_page_head;
  }
  struct MemRegion *chunk = get_free_chunk(tag_heads[tag], size);
  // TODO: Do this part lmfao
  return 0;
}

void freehee(void *ptr) {
  ;
  return;
}

struct PageInfo *create_page(size_t size, size_t tag) {
  // Allocate the smallest multiple of PAGE_SIZE that is
  // >= size + sizeof(struct PageInfo)
  size_t total_size =
      ((size + sizeof(struct PageInfo) + PAGE_SIZE - 1) / PAGE_SIZE);

  // TODO: replace mmap with hemem_mmap
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
  page_info->next = NULL;
  // Place the first free node directly after the PageInfo struct in memory
  page_info->free_head = page_info->free_tail =
      (struct MemRegion *)((char *)page_info + sizeof(struct PageInfo));

  // There are initially no allocated chunks
  page_info->alloc_head = page_info->alloc_tail = NULL;

  return page_info;
}

// uh don't call this with page_info == NULL lol, bad things will happen. :^)
struct MemRegion *get_free_chunk(struct PageInfo *page_info, size_t size) {
  // Traverse the free list
  // Best Fit strategy: find the smallest open slot that's larger than `size`

  struct MemRegion *curr = page_info->free_head;
  struct MemRegion *prev = NULL;

  size_t minimum = ~0;
  struct MemRegion *selected_chunk = NULL;
  struct MemRegion *selected_chunk_prev = NULL;

  while (curr != NULL) {
    // If a new minimum is found, set it and save the node pointer
    if (size <= curr->size && curr->size < minimum) {
      selected_chunk = curr;
      selected_chunk_prev = prev;
      minimum = curr->size;
    }
    // March pointers forward
    prev = curr;
    curr = curr->next;
  }

  // If we find a chunk big enough, proceed
  // Otherwise return NULL, which indicates to caller that a new page must be
  // `mmap`d.
  if (selected_chunk != NULL) {
    // Check if splitting the node would result in a memory chunk that's too
    // small to allocate
    if (selected_chunk->size - size >
        sizeof(struct MemRegion) + MIN_CHUNK_SIZE) {
      // Split the chunk into 2 chunks.

      // The total free space we have is the original size of the empty region
      size_t total_free_space = selected_chunk->size;

      // Calculate the address of the new node
      struct MemRegion *new_node =
          (struct MemRegion *)((uintptr_t)selected_chunk +
                               sizeof(struct MemRegion) + size);

      // Set the new node's contents
      *new_node = (struct MemRegion){
          .size = total_free_space - size - sizeof(struct MemRegion),
          .next = selected_chunk->next,
      };

      // Set the pointers
      selected_chunk->next = new_node;
    }
  } else {
    // If no chunk was found, return NULL
    return NULL;
  }

  // At this point, if the chunk needed to be split, it has been.
  // All that needs to be done now, is remove the selected chunk from the free
  // list.

  if (selected_chunk_prev != NULL)
    selected_chunk_prev->next = selected_chunk->next;

  // If the selected chunk was the head of the free list, set new head
  if (page_info->free_head == selected_chunk)
    page_info->free_head = selected_chunk_prev;

  // If the selected chunk was the tail of the free list, set new tail
  if (page_info->free_tail == selected_chunk) {
    if (selected_chunk_prev->next != NULL) {
      page_info->free_tail = selected_chunk_prev->next;
    } else {
      page_info->free_tail = selected_chunk_prev;
    }
  }

  // Unset the selected chunk's next pointer before returning it.
  selected_chunk->next = NULL;
  return selected_chunk;
}
