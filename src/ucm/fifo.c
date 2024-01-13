#include <pthread.h>
#include <stdlib.h>

#include "hemem-ucm.h"
#include "fifo.h"
#include "logging.h"

void enqueue_page(struct page_list *queue, struct hemem_page *entry)
{
  pthread_mutex_lock(&(queue->list_lock));
  assert(entry->prev == NULL);
  entry->next = queue->first;
  if(queue->first != NULL) {
    assert(queue->first->prev == NULL);
    queue->first->prev = entry;
  } else {
    assert(queue->last == NULL);
    assert(queue->numentries == 0);
    queue->last = entry;
  }

  queue->first = entry;
  entry->list = queue;
  queue->numentries++;
  pthread_mutex_unlock(&(queue->list_lock));
}

struct hemem_page *dequeue_page(struct page_list *queue)
{
  pthread_mutex_lock(&(queue->list_lock));
  struct hemem_page *ret = queue->last;

  if(ret == NULL) {
    //assert(queue->numentries == 0);
    pthread_mutex_unlock(&(queue->list_lock));
    return ret;
  }

  queue->last = ret->prev;
  if(queue->last != NULL) {
    queue->last->next = NULL;
  } else {
    queue->first = NULL;
  }

  ret->prev = ret->next = NULL;
  ret->list = NULL;
  assert(queue->numentries > 0);
  queue->numentries--;
  pthread_mutex_unlock(&(queue->list_lock));

  return ret;
}

void page_list_remove(struct page_list *list, struct hemem_page *page)
{
  pthread_mutex_lock(&(list->list_lock));
  if (list->first == NULL) {
    assert(list->last == NULL);
    assert(list->numentries == 0);
    pthread_mutex_unlock(&(list->list_lock));
    //LOG("page_list_remove_page: list was empty!\n");
    return;
  }

  if (list->first == page) {
    list->first = page->next;
  }

  if (list->last == page) {
    list->last = page->prev;
  }

  if (page->next != NULL) {
    page->next->prev = page->prev;
  }

  if (page->prev != NULL) {
    page->prev->next = page->next;
  }

  assert(list->numentries > 0);
  list->numentries--;
  page->next = NULL;
  page->prev = NULL;
  page->list = NULL;
  pthread_mutex_unlock(&(list->list_lock));
}

struct hemem_page* prev_page(struct page_list *list, struct hemem_page *page)
{
  struct hemem_page *next_page;
  pthread_mutex_lock(&(list->list_lock));
  if (page == NULL) {
    next_page = list->last;
  }
  else {
    next_page = page->prev;
    assert(page->list == list);
  }
  pthread_mutex_unlock(&(list->list_lock));
  return next_page;
}

#if defined(HEMEM_QOS) || defined(FAIR_SHARE)
void enqueue_process(struct process_list *queue, struct hemem_process *entry)
{
  struct hemem_process *current, *prev, *old_prev;

  pthread_mutex_lock(&(queue->list_lock));
  pthread_mutex_lock(&(entry->process_lock));
  if (queue->first == NULL) {
    // list is empty
    assert(queue->last == NULL);
    assert(queue->numentries == 0);
    queue->first = entry;
    queue->last = entry;
    entry->next = entry->prev = NULL;
    entry->list = queue;
    queue->numentries++;
    pthread_mutex_unlock(&(entry->process_lock));
    pthread_mutex_unlock(&(queue->list_lock));
    return;
  }
  
  // lock the current head
  pthread_mutex_lock(&(queue->first->process_lock));
  if (queue->first->target_miss_ratio > entry->target_miss_ratio) {
    // new process goes to front of list
    entry->next = queue->first;
    entry->prev = NULL;
    entry->list = queue;
    entry->next->prev = entry;
    queue->first = entry;
    queue->numentries++;
    pthread_mutex_unlock(&(entry->process_lock));
    pthread_mutex_unlock(&(entry->next->process_lock));
    pthread_mutex_unlock(&(queue->list_lock));
    return;
  }

  // walk list to find where to insert
  prev = queue->first;
  current = queue->first->next;

  if (current != NULL) {
    pthread_mutex_lock(&(current->process_lock));
  }

  while (current != NULL) {
    if (current->target_miss_ratio > entry->target_miss_ratio) {
      break;
    }
    old_prev = prev;
    prev = current;
    current = current->next;
    pthread_mutex_unlock(&(old_prev->process_lock));
    if (current != NULL) {
      pthread_mutex_lock(&(current->process_lock));
    }
  }

  if (current != NULL) {
    entry->next = current;
    entry->prev = current->prev;
    entry->list = queue;
    current->prev = entry;
    entry->prev->next = entry;
    pthread_mutex_unlock(&(prev->process_lock));
    pthread_mutex_unlock(&(entry->process_lock));
    pthread_mutex_unlock(&(current->process_lock));
  } else {
    entry->next = NULL;
    entry->prev = prev;
    entry->list = queue;
    prev->next = entry;
    queue->last = entry;
    pthread_mutex_unlock(&(prev->process_lock));
    pthread_mutex_unlock(&(entry->process_lock));
  }

  queue->numentries++;
  pthread_mutex_unlock(&(queue->list_lock));
}
#else
void enqueue_process(struct process_list *queue, struct hemem_process *entry)
{
  pthread_mutex_lock(&(queue->list_lock));
  assert(entry->prev == NULL);
  entry->next = queue->first;
  if(queue->first != NULL) {
    assert(queue->first->prev == NULL);
    queue->first->prev = entry;
  } else {
    assert(queue->last == NULL);
    assert(queue->numentries == 0);
    queue->last = entry;
  }

  queue->first = entry;
  entry->list = queue;
  queue->numentries++;
  pthread_mutex_unlock(&(queue->list_lock));
}
#endif

#if defined(HEMEM_QOS) || defined(FAIR_SHARE)
void process_list_remove(struct process_list *list, struct hemem_process *process)
{
  struct hemem_process *tmp, *next, *prev, *current, *old_prev;
  pthread_mutex_lock(&(list->list_lock));
  if (list->first == NULL) {
    assert(list->last == NULL);
    assert(list->numentries == 0);
    pthread_mutex_unlock(&(list->list_lock));
    LOG("fifo.c: Called remove on empty process list\n");
    return;
  }

  pthread_mutex_lock(&(list->first->process_lock));
  // process is first in list
  if (list->first == process) {
    // only one process in list
    tmp = list->first;
    next = tmp->next;
    list->first = next;
    if (next == NULL) {
      list->last = next;
      process->prev = process->next = NULL;
      process->list = NULL;
      pthread_mutex_unlock(&(process->process_lock));
    } else {
      pthread_mutex_lock(&(next->process_lock));
      next->prev = NULL;
      process->prev = process->next = NULL;
      process->list = NULL;
      pthread_mutex_unlock(&(tmp->process_lock));
      pthread_mutex_unlock(&(next->process_lock));
    }
    list->numentries--;
    pthread_mutex_unlock(&(list->list_lock));
    return;
  }

  prev = list->first;
  current = list->first->next;

  if (current != NULL) {
    pthread_mutex_lock(&(current->process_lock));
  }

  while (current != NULL) {
    if (current == process) {
      break;
    }
    old_prev = prev;
    prev = current;
    current = current->next;
    pthread_mutex_unlock(&(old_prev->process_lock));
    if (current != NULL) {
      pthread_mutex_lock(&(current->process_lock));
    }
  }

  if (current == process) {
    if (current->next != NULL) {
      next = current->next;
      pthread_mutex_lock(&(next->process_lock));
      prev->next = next;
      next->prev = prev;
      current->prev = current->next = NULL;
      current->list = NULL;
      pthread_mutex_unlock(&(prev->process_lock));
      pthread_mutex_unlock(&(current->process_lock));
      pthread_mutex_unlock(&(next->process_lock));
    } else {
      prev->next = NULL;
      current->next = current->prev = NULL;
      current->list = NULL;
      list->last = prev;
      pthread_mutex_unlock(&(prev->process_lock));
      pthread_mutex_unlock(&(current->process_lock));
    }
  } else {
    LOG("fifo.c: process not found in list\n");
    pthread_mutex_unlock(&(list->list_lock));
    return;
  }

  list->numentries--;
  pthread_mutex_unlock(&(list->list_lock));
}
#else
void process_list_remove(struct process_list *list, struct hemem_process *process)
{
  pthread_mutex_lock(&(list->list_lock));
  if (list->first == NULL) {
    assert(list->last == NULL);
    assert(list->numentries == 0);
    pthread_mutex_unlock(&(list->list_lock));
    //LOG("process_list_remove: list was empty!\n");
    return;
  }

  if (list->first == process) {
    list->first = process->next;
  }

  if (list->last == process) {
    list->last = process->prev;
  }

  if (process->next != NULL) {
    process->next->prev = process->prev;
  }

  if (process->prev != NULL) {
    process->prev->next = process->next;
  }

  assert(list->numentries > 0);
  list->numentries--;
  process->next = NULL;
  process->prev = NULL;
  process->list = NULL;
  pthread_mutex_unlock(&(list->list_lock));
}
#endif
struct hemem_process *peek_process(struct process_list *list)
{
  struct hemem_process *ret;
  pthread_mutex_lock(&(list->list_lock));
  ret = list->first;
  pthread_mutex_unlock(&(list->list_lock));

  return ret;
}
