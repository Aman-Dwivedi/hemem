#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/ioctl.h>

#include "hemem-ucm.h"
#include "pebs.h"
#include "timer.h"
#include "spsc-ring.h"
#include "logging.h"
#include "hemem-shared.h"

static struct process_list processes_list;
static struct page_list dram_free_list;
static struct page_list nvm_free_list;

uint64_t hemem_pages_cnt = 0;
uint64_t other_pages_cnt = 0;
uint64_t other_processes_cnt = 0;
uint64_t total_pages_cnt = 0;
uint64_t zero_pages_cnt = 0;
uint64_t throttle_cnt = 0;
uint64_t unthrottle_cnt = 0;
uint64_t dram_cools = 0, nvm_cools = 0;
uint64_t dram_cools_finished = 0, nvm_cools_finished = 0;
uint64_t stale_candidate_count = 0;
uint64_t lost_samples = 0;

_Atomic volatile uint64_t free_ring_requests = 0;
_Atomic volatile uint64_t hot_ring_requests = 0;
_Atomic volatile uint64_t cold_ring_requests = 0;

_Atomic volatile uint64_t free_ring_requests_handled = 0;
_Atomic volatile uint64_t hot_ring_requests_handled = 0;
_Atomic volatile uint64_t cold_ring_requests_handled = 0;

static struct perf_event_mmap_page *perf_page[PEBS_NPROCS][NPBUFTYPES];
int pfd[PEBS_NPROCS][NPBUFTYPES];
int sample_periods[PEBS_NPROCS];


bool timed_cooling = false;
bool autofmmr = false;


static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, 
    int cpu, int group_fd, unsigned long flags)
{
  int ret;

  ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
		group_fd, flags);
  return ret;
}

static struct perf_event_mmap_page* perf_setup(__u64 config, __u64 config1, __u64 cpu, __u64 type)
{
  struct perf_event_attr attr;

  memset(&attr, 0, sizeof(struct perf_event_attr));

  attr.type = PERF_TYPE_RAW;
  attr.size = sizeof(struct perf_event_attr);

  attr.config = config;
  attr.config1 = config1;
  
  attr.sample_period = sample_periods[cpu];

  fprintf(stderr, "CPU %llu set with sample period %llu\n", cpu, attr.sample_period);

  attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_ADDR;
  attr.disabled = 0;
  //attr.inherit = 1;
  attr.exclude_kernel = 1;
  attr.exclude_hv = 1;
  attr.exclude_callchain_kernel = 1;
  attr.exclude_callchain_user = 1;
  attr.precise_ip = 1;

  pfd[cpu][type] = perf_event_open(&attr, -1, cpu, -1, 0);
  if(pfd[cpu][type] == -1) {
    perror("perf_event_open");
  }
  assert(pfd[cpu][type] != -1);

  size_t mmap_size = sysconf(_SC_PAGESIZE) * PERF_PAGES;
  /* printf("mmap_size = %zu\n", mmap_size); */
  struct perf_event_mmap_page *p = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, pfd[cpu][type], 0);
  if(p == MAP_FAILED) {
    perror("mmap");
  }
  assert(p != MAP_FAILED);

  return p;
}

void make_hot_request(struct hemem_process* process, struct hemem_page* page)
{
   page->ring_present = true;
   ring_buf_put(process->hot_ring, (uint64_t*)page);
   hot_ring_requests++;
}

void make_cold_request(struct hemem_process* process, struct hemem_page* page)
{
    page->ring_present = true;
    ring_buf_put(process->cold_ring, (uint64_t*)page);
    cold_ring_requests++;
}

static inline int access_to_index(uint64_t num) {
  if(num <= 0) {
    return 0;
  }
  int ret = 64 - __builtin_clzll(num);
  if(ret > NUM_HOTNESS_LEVELS - 1) {
    return NUM_HOTNESS_LEVELS - 1;
  }
  return ret;
}


void *pebs_scan_thread()
{
  struct perf_event_mmap_page *p;
  char *pbuf;
  struct perf_event_header *ph;
  struct perf_sample* ps;
  struct hemem_page* page;
  struct hemem_process* process;
  uint64_t total_accesses;
  int new_hotness, i, j, s;

  cpu_set_t cpuset;
  pthread_t thread;

  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(SCANNING_THREAD_CPU, &cpuset);
  s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }
  
  for(;;) {
    for (i = LAST_HEMEM_THREAD + 1; i < PEBS_NPROCS; i++) {
      for(j = 0; j < NPBUFTYPES; j++) {
        p = perf_page[i][j];
        pbuf = (char *)p + p->data_offset;

        __sync_synchronize();

        if(p->data_head == p->data_tail) {
          continue;
        }

	      assert(p->data_head > p->data_tail);

        ph = (void *)(pbuf + (p->data_tail % p->data_size));

        switch(ph->type) {
        case PERF_RECORD_SAMPLE:
            ps = (struct perf_sample*)ph;
            assert(ps != NULL);
            if(ps->addr != 0) {
              __u64 pfn = ps->addr & HUGE_PFN_MASK;
              process = find_process(ps->pid);

              if (process != NULL) {
		            process->samples[i]++;
                page = find_page(process, pfn);
                if (page != NULL) {
                  if (page->va != 0) {
                    process->accessed_pages[j]++;
                    page->accesses[j]++;
                    page->tot_accesses[j]++;
                    
                    total_accesses = page->accesses[DRAMREAD] + page->accesses[NVMREAD];
                    new_hotness = access_to_index(total_accesses);
                    // check for hotness change and add to ring
                    if(new_hotness > page->hot) {
                      make_hot_request(process, page);
                    }
                    else if(new_hotness < page->hot) {
                      make_cold_request(process, page);
                    }

                    page->accesses[DRAMREAD] >>= (process->process_clock - page->local_clock);
                    page->accesses[NVMREAD] >>= (process->process_clock - page->local_clock);
                    page->local_clock = process->process_clock;
                    if (page->accesses[j] > PEBS_COOLING_THRESHOLD && !timed_cooling) {
                      process->process_clock++;
                      dram_cools++;
                      nvm_cools++;
                      process->need_cool_dram = true;
                      process->need_cool_nvm = true;
                      process->cools++;
                    }
                  }
                  hemem_pages_cnt++;
                }
                else {
		              /* assert(0); */
                  other_pages_cnt++;
                }
                total_pages_cnt++;
              }
              else {
                other_processes_cnt++;
                //LOG("pebs_thread received sample from non-qtMem process %d\n", ps->pid);
              }
            }
            else {
              zero_pages_cnt++;
            }

	          /* ps->addr = 0; */
  	      break;
        case PERF_RECORD_THROTTLE:
        case PERF_RECORD_UNTHROTTLE:
          if (ph->type == PERF_RECORD_THROTTLE) {
              throttle_cnt++;
          }
          else {
              unthrottle_cnt++;
          }
          break;
        case PERF_RECORD_LOST_SAMPLES:
        case PERF_RECORD_LOST:
          lost_samples++;
          break;
        default:
          fprintf(stderr, "Unknown type %u\n", ph->type);
          //assert(!"NYI");
          break;
        }

        p->data_tail += ph->size;
      }
    }
  }

  return NULL;
}

static void pebs_migrate_down(struct hemem_process *process, struct hemem_page *page, uint64_t offset)
{
  struct timeval start, end;

  gettimeofday(&start, NULL);

  assert(page->pid == process->pid);

  page->migrating = true;
  hemem_ucm_wp_page(page, true);
  hemem_ucm_migrate_down(process, page, offset);
  process->current_dram -= pt_to_pagesize(page->pt);
  process->current_nvm += pt_to_pagesize(page->pt);
  page->migrating = false; 

  gettimeofday(&end, NULL);
  LOG_TIME("migrate_down: %f s\n", elapsed(&start, &end));
}

static void pebs_migrate_up(struct hemem_process *process, struct hemem_page *page, uint64_t offset)
{
  struct timeval start, end;

  gettimeofday(&start, NULL);

  assert(page->pid == process->pid);

  page->migrating = true;
  hemem_ucm_wp_page(page, true);
  hemem_ucm_migrate_up(process, page, offset);
  process->current_dram += pt_to_pagesize(page->pt);
  process->current_nvm -= pt_to_pagesize(page->pt);
  page->migrating = false;

  gettimeofday(&end, NULL);
  LOG_TIME("migrate_up: %f s\n", elapsed(&start, &end));
}

// moves page to hot list -- called by migrate thread
void make_hot(struct hemem_process* process, struct hemem_page* page, int new_hot)
{
  assert(page != NULL);
  assert(page->va != 0);

  assert(page->pid == process->pid);

  if (page->hot == new_hot) {
    if (page->in_dram) {
      assert(page->list == &(process->dram_lists[new_hot]));
    }
    else {
      assert(page->list == &(process->nvm_lists[new_hot]));
    }

    return;
  }

  if (page->in_dram) {
    assert(page->list == &(process->dram_lists[page->hot]));
    page_list_remove(&(process->dram_lists[page->hot]), page);
    page->hot = new_hot;
    enqueue_page(&(process->dram_lists[page->hot]), page);
  }
  else {
    assert(page->list == &(process->nvm_lists[page->hot]));
    page_list_remove(&(process->nvm_lists[page->hot]), page);
    page->hot = new_hot;
    enqueue_page(&(process->nvm_lists[page->hot]), page);
  }
}

// moves page to cold list -- called by migrate thread
void make_cold(struct hemem_process* process, struct hemem_page* page, int new_hot)
{
  assert(page != NULL);
  assert(page->va != 0);

  assert(page->pid == process->pid);

  if (page->hot == new_hot) {
    if (page->in_dram) {
      assert(page->list == &(process->dram_lists[new_hot]));
    }
    else {
      assert(page->list == &(process->nvm_lists[new_hot]));
    }

    return;
  }

  if (page->in_dram) {
    assert(page->list == &(process->dram_lists[page->hot]));
    page_list_remove(&(process->dram_lists[page->hot]), page);
    page->hot = new_hot;
    enqueue_page(&(process->dram_lists[new_hot]), page);
  }
  else {
    assert(page->list == &(process->nvm_lists[page->hot]));
    page_list_remove(&(process->nvm_lists[page->hot]), page);
    page->hot = new_hot;
    enqueue_page(&(process->nvm_lists[page->hot]), page);
  }
}

struct hemem_page* partial_cool(struct hemem_process* process, bool dram)
{
  struct hemem_page *p, *current;
  uint64_t tmp_accesses[NPBUFTYPES];
  int cool_list, cur_list_index;
  bool goto_next_list = false;
  struct page_list* cur_bins;
  int i, j, new_hotness;

  struct timeval start, end;

  // do we even need to be cooling right now? If not, just return
  // where we left off last time we needed to cool. Next time this function
  // is called when cooling is needed, we pick up from here
  if (dram && !(process->need_cool_dram)) {
    return process->cur_cool_in_dram;
  } else if (!dram && !(process->need_cool_nvm)) {
    return process->cur_cool_in_nvm;
  }

  // we cool backwards through the page lists, the same order the pages
  // were inserted. The idea is, in this way, we cool the oldest pages first
  if (dram && (process->cur_cool_in_dram == NULL) && (process->cur_cool_in_dram_list == 0)) {
    for (i = NUM_HOTNESS_LEVELS-1; i > 0 && process->cur_cool_in_dram == NULL; i--) {
      // find the current oldest hottest page in DRAM
      process->cur_cool_in_dram = process->dram_lists[i].last;
      process->cur_cool_in_dram_list = i;
    }
    // dram hot list might be empty, in which case we have nothing to cool
    if (process->cur_cool_in_dram == NULL) {
      process->cur_cool_in_dram_list = 0;
      process->need_cool_dram = false;
      dram_cools_finished++;
      return NULL;
    }
  } else if ((!dram) && (process->cur_cool_in_nvm == NULL) && (process->cur_cool_in_nvm_list == 0)) {
    for (i = NUM_HOTNESS_LEVELS-1; i > 0 && process->cur_cool_in_nvm == NULL; i--) {
      // find the current oldest hottest page in NVM
      process->cur_cool_in_nvm = process->nvm_lists[i].last;
      process->cur_cool_in_nvm_list = i;
    }
    // nvm hot list might be empty, in which case we have nothing to cool
    if (process->cur_cool_in_nvm == NULL) {
      process->cur_cool_in_nvm_list = 0;
      process->need_cool_nvm = false;
      nvm_cools_finished++;
      return NULL;
    }
  }

  gettimeofday(&start, NULL);

  // set hot and cold list pointers as appropriate for memory type
  // set current to the current cooled page for the memory type here as well
  if (dram) {
    current = process->cur_cool_in_dram;
    cool_list = process->cur_cool_in_dram_list;
    cur_bins = process->dram_lists;
    if (current) {
      assert(current->pid == process->pid);
      assert(current->list == &(cur_bins[current->hot]));
    }
  } else {
    current = process->cur_cool_in_nvm;
    cool_list = process->cur_cool_in_nvm_list;
    cur_bins = process->nvm_lists;
    if (current) {
      assert(current->pid == process->pid);
      assert(current->list == &(cur_bins[current->hot]));
    }
  }
  
  // start from the current cooled page. This is either where we left off
  // last time or the end of the page list if we've gone throug the whole list
  p = current;
  cur_list_index = cool_list;
  for (i = 0; i < COOLING_PAGES; i++) {
    if (p == NULL) {
      // not pointing to a page to cool from. check the lower lists. 
      for (j = cool_list; j > 0 && p == NULL; j--) {
        p = cur_bins[j].last;
        cool_list = j;
      }
      if (p == NULL) {
        cool_list = 0;
        break;
      }
    }


    // sanity check we grabbed a page in the appropriate memory type and
    // from the appropriate list
    if (dram) {
        assert(p->pid == process->pid);
        assert(p->in_dram);
        assert(p->list == &(process->dram_lists[p->hot]));
    } else {
        assert(p->pid == process->pid);
        assert(!p->in_dram);
        assert(p->list == &(process->nvm_lists[p->hot]));
    }

    // compute the access samples this page would have had if it were up to date
    // with cooling
    for (int j = 0; j < NPBUFTYPES; j++) {
        tmp_accesses[j] = p->accesses[j] >> (process->process_clock - p->local_clock);
    }

    // is the page still hot if it was up to date with cooling?
    new_hotness = access_to_index(tmp_accesses[DRAMREAD] + tmp_accesses[NVMREAD]);
    if (new_hotness != p->hot) {
      // if the page is no longer hot, then we move it to the cold list
      p->hot = new_hotness;
      // first, we update our current pointer in prep for p being
      // moved to the cold list. This ensures our next call to
      // prev_page() stays in the appropriate list
      current = p->next;
      if(current == NULL) {
        goto_next_list = true;
      }
      page_list_remove(p->list, p);
      enqueue_page(&(cur_bins[new_hotness]), p);
    } else {
      current = p;
      cur_list_index = cool_list;
    }
    
    // have we gone through the entire hot list? If so, call for the 
    // loop iteration to goto the next list.
    if (dram && (p == process->dram_lists[cool_list].first)) {
      goto_next_list = true;
    } else if (!dram && (p == process->nvm_lists[cool_list].first)) {
      goto_next_list = true;
    } 

    // grab another page to cool
    // first if we need to goto next list then goto that list for the next.
    if(goto_next_list) {
      if(cool_list == 1) {
        // we've cooled everything thre is to cool
        cur_list_index = 0;
        current = NULL;
        if (dram) {
          process->need_cool_dram = false;
          dram_cools_finished++;
        } else {
          process->need_cool_nvm = false;
          nvm_cools_finished++;
        }
        break;
      }
      goto_next_list = false;
      cool_list--;
      p = cur_bins[cool_list].last;
    }
    else {
      p = prev_page(&(cur_bins[cool_list]), current);
    }
  }

  if (dram) {
    process->cur_cool_in_dram_list = cur_list_index;
  }
  else {
    process->cur_cool_in_nvm_list = cur_list_index;
  }

  gettimeofday(&end, NULL);
  LOG_TIME("partial_cool: %f s\n", elapsed(&start, &end));

  return current;
}

// convenience function for catching pages where we migrate or otherwise move a page
// from one list to another. If the page in question was our "bookmark" pointer for
// cooling, then we need to update that bookmark. Here, for simplicity, we just
// reset our bookmark to the end of the hot list
void update_current_cool_page(struct hemem_process *process, struct hemem_page *page)
{
  if (page == process->cur_cool_in_dram) {
    // first a set of sanity checks
    assert(page->pid == process->pid);
    assert(page->in_dram);
    assert(page->list == &(process->dram_lists[page->hot]));
    // then just reset the bookmark pointer to the last page in list
    // just restart
    process->cur_cool_in_dram = NULL;
    process->cur_cool_in_dram_list = 0;
  } else if (page == process->cur_cool_in_nvm) {
    // first, a bunch of sanity checks
    assert(page->pid == process->pid);
    assert(!(page->in_dram));
    assert(page->list == &(process->nvm_lists[page->hot]));
    // then just reset the bookmark pointer to the last page in list
    process->cur_cool_in_nvm = NULL;
    process->cur_cool_in_nvm_list = 0;
  }
}


// The PEBS thread communicates with the policy thread via request rings. The
// only thread allowed to maniuplate the hot and cold lists is the policy thread
// to prevent deadlocks or race conditions, and the ring buffers solve that.
// Here, the policy thread will handle the ring buffer requests by placing
// the pages in the ring buffers into the appropriate lists
void handle_ring_requests(struct hemem_process *process)
{
  int num_ring_reqs;
  struct hemem_page* page = NULL;
  uint64_t tmp_accesses[NPBUFTYPES];
  int new_hotness;

  // free pages using free page ring buffer
  // we take all pages from the free ring rather than until
  // meeting some threshold of requests handled to free up
  // as much space as quick as possible
  while(!ring_buf_empty(process->free_page_ring)) {
    struct page_list *list;
    pthread_mutex_lock(&(process->free_page_ring_lock));
    page = (struct hemem_page*)ring_buf_get(process->free_page_ring);
    pthread_mutex_unlock(&(process->free_page_ring_lock));
    if (page == NULL) {
      // ring buffer was empty
      break;
    }


    list = page->list;
    assert(list != NULL);

    // list sanity checks
    // either in the correct list or in a ring. 
    if (page->in_dram) {
      assert(page->pid == process->pid);
      assert(page->list == &(process->dram_lists[page->hot]) || page->ring_present);
    } else {
      assert(page->pid == process->pid);
      assert(page->list == &(process->nvm_lists[page->hot]) || page->ring_present);
    } 

    // check whether the page being freed is our bookmark cool page
    update_current_cool_page(process, page);
    
    // remove page from its list and put it into the appropriate free list
    page_list_remove(list, page);

    // reset page stats
    page->present = false;
    page->pid = -1;
    page->hot = COLD;
    for (int i = 0; i < NPBUFTYPES; i++) {
      page->accesses[i] = 0;
      page->tot_accesses[i] = 0;
    }

    if (page->in_dram) {
      enqueue_page(&dram_free_list, page);

      // update process DRAM stats
      process->current_dram -= pt_to_pagesize(page->pt);
    }
    else {
      enqueue_page(&nvm_free_list, page);
      process->current_nvm -= pt_to_pagesize(page->pt);
    }
    page->in_free_ring = false;

    free_ring_requests_handled++;
  }

  page = NULL;
  num_ring_reqs = 0;
  // handle hot requests from hot buffer by moving pages to hot list
  while(!ring_buf_empty(process->hot_ring) && num_ring_reqs < HOT_RING_REQS_THRESHOLD) {
	  page = (struct hemem_page*)ring_buf_get(process->hot_ring);
    if (page == NULL) {
      // ring buffer was empty
      break;
    }


    if (!page->present) {
      // page has been freed
      if (page->in_dram) {
        assert(page->list == &dram_free_list);
      } else {
        assert(page->list == &nvm_free_list);
      }
      hot_ring_requests_handled++;
      continue;
    }

    assert(page->pid == process->pid);
    
    // compute the access samples this page would have had if it were up to date
    // with cooling
    for (int j = 0; j < NPBUFTYPES; j++) {
        tmp_accesses[j] = page->accesses[j] >> (process->process_clock - page->local_clock);
    }
    new_hotness = access_to_index(tmp_accesses[DRAMREAD] + tmp_accesses[NVMREAD]);
   
    // is page even still hot?
    if (new_hotness < page->hot) {
      // page has been cooled and is no longer hot, just move to cold list
      // first, check to see if we need to update our cooling bookmark
      update_current_cool_page(process, page);
      page->ring_present = false;
      num_ring_reqs++;
      make_cold(process, page, new_hotness);
      hot_ring_requests_handled++;
      continue;
    }

    // page is still hot, so we can move it to the hot list
    // do we need to update our cooling bookmark?
    update_current_cool_page(process, page);
    page->ring_present = false;
    num_ring_reqs++;
    make_hot(process, page, new_hotness);
    //printf("hot ring, hot pages:%llu\n", num_ring_reqs);
    
    hot_ring_requests_handled++;
  }

  page = NULL;
  num_ring_reqs = 0;
  // handle cold requests from cold buffer by moving pages to cold list
  while(!ring_buf_empty(process->cold_ring) && num_ring_reqs < COLD_RING_REQS_THRESHOLD) {
    page = (struct hemem_page*)ring_buf_get(process->cold_ring);
    if (page == NULL) {
      // ring buffer was empty
      break;
    }


    if (!page->present) {
      // page has been freed
      if (page->in_dram) {
        assert(page->list == &dram_free_list);
      } else {
        assert(page->list == &nvm_free_list);
      }
      cold_ring_requests_handled++;
      continue;
    }

    assert(page->pid == process->pid);
    
    // compute the access samples this page would have had if it were up to date
    // with cooling
    for (int j = 0; j < NPBUFTYPES; j++) {
        tmp_accesses[j] = page->accesses[j] >> (process->process_clock - page->local_clock);
    }
    new_hotness = access_to_index(tmp_accesses[DRAMREAD] + tmp_accesses[NVMREAD]);

    if (new_hotness >= page->hot) {
      // page is now hot and should actually move to the hot list
      // if not already there
      update_current_cool_page(process, page);
      page->ring_present = false;
      num_ring_reqs++;
      make_hot(process, page, new_hotness);
      cold_ring_requests_handled++;
      continue;
    }

    // check if we need to update our cooling bookmark, then move page
    // to the cold list
    update_current_cool_page(process, page);
    page->ring_present = false;
    num_ring_reqs++;
    make_cold(process, page, new_hotness);
    //printf("cold ring, cold pages:%llu\n", num_ring_reqs);
    cold_ring_requests_handled++;
  }
}

struct hemem_page* find_candidate_nvm_page(struct hemem_process *process) {
  struct hemem_page* p;
//  struct hemem_page *starting_page;
//  int tot_accesses;

  for(int i = NUM_HOTNESS_LEVELS-1; i >= 0; i--) {
    p = dequeue_page(&(process->nvm_lists[i]));
/*
    if (p == NULL) {
      // got null. list empy. move on.
      continue;
    }

    // check if the page is stale in the list. if it is then oops. move on.
    tot_accesses = (p->accesses[DRAMREAD] + p->accesses[NVMREAD]) >> (process->process_clock - p->local_clock);
    starting_page = p;
    while (access_to_index(tot_accesses) < p->hot) {
      if (access_to_index(tot_accesses) < p->hot) {
        stale_candidate_count++;
      }
      enqueue_page(&(process->nvm_lists[i]), p);
      p = dequeue_page(&(process->nvm_lists[i]));
      if (p == starting_page) {
        break;
      }
    }
*/  
    if (p != NULL) {
      // found something hot. we should try to promote it.
      assert(p->pid == process->pid);
      return p;
    }
  }
  return NULL;
}

void process_migrate_down(struct hemem_process *process, uint64_t migrate_down_bytes)
{
  uint64_t migrated_bytes;
  uint64_t old_offset;
  struct hemem_page *cp, *np;
  struct timeval now;

  for (migrated_bytes = 0; migrated_bytes < migrate_down_bytes;) {
    if (migrated_bytes >= PEBS_MIGRATE_RATE) {
      break;
    }

    // get the coldest possible dram page
    for(int i = 0; i < NUM_HOTNESS_LEVELS; i++) {
      cp = dequeue_page(&(process->dram_lists[i]));
      if(cp != NULL) {
        break;
      }
    }

    if (cp == NULL) {
      // no dram pages to move down
      break;
    }

    assert(cp->pid == process->pid);

    if (cp == process->cur_cool_in_dram) {
      assert(cp->in_dram);
      // then just reset the bookmark pointer to the last page in list
      // just restart
      process->cur_cool_in_dram = NULL;
      process->cur_cool_in_dram_list = 0;
    }

    np = dequeue_page(&nvm_free_list);
    if (np != NULL) {
      assert(!(np->present));
      assert(np->pid == -1);

      old_offset = cp->devdax_offset;
      pebs_migrate_down(process, cp, np->devdax_offset);
      np->devdax_offset = old_offset;
      np->in_dram = true;
      np->present = false;
      assert(np->hot == COLD);
      for (int i = 0; i < NPBUFTYPES; i++) {
        assert(np->accesses[i] == 0);
        assert(np->tot_accesses[i] == 0);
      }

      enqueue_page(&(process->nvm_lists[cp->hot]), cp);
      
      enqueue_page(&dram_free_list, np);
      migrated_bytes += pt_to_pagesize(cp->pt);
    } else {
      // no free NVM pages to move, so put it back into
      // dram cold list and bail out
      gettimeofday(&now, NULL);
      LOG("%f\tpolicy thread found no NVM free pages\n", elapsed(&startup, &now));
      enqueue_page(&(process->dram_lists[cp->hot]), cp);
      break;
    }
    //assert(np != NULL);
  }
  gettimeofday(&now, NULL);
  //LOG("%f\tprocess %d has migrated %ld bytes down\n", elapsed(&startup, &now), process->pid, migrated_bytes);
}

void process_migrate_up(struct hemem_process *process, uint64_t migrate_up_bytes)
{
  uint64_t migrated_bytes;
  uint64_t old_offset;
  uint64_t tmp_accesses[NPBUFTYPES];
  struct hemem_page *p, *np;
  struct timeval now;
  int new_hotness;

  for (migrated_bytes = 0; migrated_bytes < migrate_up_bytes;) {
    if (migrated_bytes >= PEBS_MIGRATE_RATE) {
      break;
    }
    p = find_candidate_nvm_page(process);
    if (p == NULL) {
      // truly nothing in any list
      break;
    }


    if (p == process->cur_cool_in_nvm) {
      assert(!p->in_dram);
      // then just reset the bookmark pointer to the last page in list
      // just restart
      process->cur_cool_in_nvm = NULL;
      process->cur_cool_in_nvm_list = 0;
    }

    assert(p->pid == process->pid);

    // compute the access samples this page would have had if it were up to date
    // with cooling
    for (int j = 0; j < NPBUFTYPES; j++) {
      tmp_accesses[j] = p->accesses[j] >> (process->process_clock - p->local_clock);
    }

    new_hotness = access_to_index(tmp_accesses[DRAMREAD] + tmp_accesses[NVMREAD]);
    /*
    if (new_hotness == COLD) {
      p->hot = new_hotness;
      enqueue_page(&(process->nvm_lists[p->hot]), p);
      continue;
    }
    */
    
    // TODO
    // try to find a page for it. in the free list.
    np = dequeue_page(&dram_free_list);
    if (np == NULL) {
      gettimeofday(&now, NULL);
      LOG("%f\tpolicy thread found no DRAM free pages\n", elapsed(&startup, &now));
      p->hot = new_hotness;
      enqueue_page(&(process->nvm_lists[p->hot]), p);
      break;
      //}
    }
    assert(!np->present);
    assert(np->pid == -1);

    old_offset = p->devdax_offset;
    pebs_migrate_up(process, p, np->devdax_offset);
    np->devdax_offset = old_offset;
    np->in_dram = false;
    np->present = false;
    assert(np->hot == COLD);
    for (int i = 0; i < NPBUFTYPES; i++) {
      assert(np->accesses[i] == 0);
      assert(np->tot_accesses[i] == 0);
    }

    p->hot = new_hotness;
    enqueue_page(&(process->dram_lists[p->hot]), p);

    enqueue_page(&nvm_free_list, np);
    migrated_bytes += pt_to_pagesize(p->pt);
  }
  gettimeofday(&now, NULL);
  //LOG("%f\tprocess %d has migrated %ld bytes up\n", elapsed(&startup, &now), process->pid, migrated_bytes);
}

static inline double calc_miss_ratio(struct hemem_process *process)
{
  return ((1.0 * process->accessed_pages[NVMREAD]) / (1.0 * (process->accessed_pages[DRAMREAD] + process->accessed_pages[NVMREAD])));
}

static inline int64_t max(int64_t a, int64_t b)
{
  return a > b ? a : b;
}

static inline int64_t min(int64_t a, int64_t b)
{
  return a < b ? a : b;
}

void *pebs_policy_thread()
{
  cpu_set_t cpuset;
  pthread_t thread;
  int ret;
  uint64_t migrate_down_bytes;
  struct timeval start, end;
  double migrate_time;
  struct hemem_process *process, *tmp;
  struct timeval now;
  //uint64_t requested_dram, remaining_dram, dram_taking, dram_portion;
  //double slack;
  int64_t tmp_dram[NUM_HOTNESS_LEVELS];
  int i, j;
  int nvm_hot_pages_left_to_migrate, pages_from_cur_dram;
  //int num_victims;
  int64_t interprocess_migrate = PEBS_MIGRATE_RATE / 2;
  int64_t intraprocess_migrate = PEBS_MIGRATE_RATE / 2;
  uint64_t migrate_share;

  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(POLICY_THREAD_CPU, &cpuset);
  ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (ret != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }

  // Store last time period cooled
  bool needs_cooling = false;
  struct timeval last_cooled;
  gettimeofday(&last_cooled, NULL);

  for (;;) {
    gettimeofday(&start, NULL);
    if (timed_cooling) {
      needs_cooling = false;
      // Check if we need to cool processes (Currently used by AutoFMMR)
      if(elapsed(&last_cooled, &start) > PEBS_COOLING_PERIOD) {
        last_cooled = start;
        needs_cooling = true;
      }
    }

    double total_ratio = 0;
    double total_procs = 0;

    // go through once to handle ring requests and calculate current miss ratios 
    process = peek_process(&processes_list);
    while (process != NULL) {
      pthread_mutex_lock(&(process->process_lock));
      handle_ring_requests(process);

      process->dram_delta = 0;

      if (process->accessed_pages[DRAMREAD] + process->accessed_pages[NVMREAD] != 0) {
        if (process->current_miss_ratio == -1) {
          // first time we have actual data to compute, but don't want to include the -1.0 values in the EWMA, so
          // just comute a raw miss ratio here
          process->current_miss_ratio = calc_miss_ratio(process);
        } else {
          process->current_miss_ratio = (EWMA_FRAC * calc_miss_ratio(process)) + ((1 - EWMA_FRAC) * process->current_miss_ratio);
        }
        process->accessed_pages[DRAMREAD] = 0; process->accessed_pages[NVMREAD] = 0;
      } else {
        process->current_miss_ratio = 0;
      }
     
      for (int xxx = LAST_HEMEM_THREAD + 1; xxx < PEBS_NPROCS; xxx++) {
	      process->samples[xxx] = 0;
	    }
    
      // figure out how much dram we need to reallocate based on ratio diffsj
      // if our current miss ratio is less than target, then we are good and
      // can even take dram from this process if needed; if current miss
      // ratio is larger than the target, this process needs more dram

      
      //if (process->current_miss_ratio == -1.0) {
      // // skip this process -- we don't have current access information for it
      //  // TODO: Should we consider taking DRAM from these processes (maybe as a last resort?)
      //  tmp = process;
      //  process = process->next;
      //  pthread_mutex_unlock(&(tmp->process_lock));
      //  continue;
      //}
      // Can move out of condition to apply to all policies
      if(timed_cooling && needs_cooling) {
        process->process_clock++;
        process->need_cool_dram = true;
        process->need_cool_nvm = true;
        process->cools++;
      }

      if(autofmmr) {
        uint64_t full_fast_pages = DRAMSIZE / PAGE_SIZE;
        uint64_t curr_fast_pages = process->current_dram / PAGE_SIZE;
        uint64_t full_fast_shares = 0;
        uint64_t curr_fast_shares = 0;
        for(int i = NUM_HOTNESS_LEVELS; i > 0; --i) {
          uint64_t this_tier_pages = min(process->dram_lists[i].numentries + process->nvm_lists[i].numentries, full_fast_pages);
          full_fast_shares += (1 << i) * this_tier_pages;
          full_fast_pages -= this_tier_pages;

          this_tier_pages = min(process->dram_lists[i].numentries + process->nvm_lists[i].numentries, curr_fast_pages);
          curr_fast_shares += (1 << i) * this_tier_pages;
          curr_fast_pages -= this_tier_pages;
        }
        if(full_fast_shares) {
          process->ratio = (1.0 * curr_fast_shares) / full_fast_shares;
          total_ratio += (1.0 * curr_fast_shares) / full_fast_shares;
          ++total_procs;
        }
      } else {
        // compute total accesses and total dram accesses
        uint64_t total_bin_accesses = 0;
        uint64_t total_dram_accesses = 0;
        for (int i = NUM_HOTNESS_LEVELS; i > 0; --i) {
          uint64_t this_tier_pages = process->dram_lists[i].numentries + process->nvm_lists[i].numentries;
          total_bin_accesses += (1 << i) * this_tier_pages;

          this_tier_pages = process->dram_lists[i].numentries;
          total_dram_accesses += (1 << i) * this_tier_pages;
        }

        // compute ideal bins needed to achieve target
        // start from highest bins for both DRAM and NVM. The highest count pages should all be in
        // DRAM (eventually)
        uint64_t ideal_bin_accesses = 0;
        for (int i = NUM_HOTNESS_LEVELS; i > 0; --i) {
          uint64_t this_tier_pages = process->dram_lists[i].numentries + process->nvm_lists[i].numentries;
          ideal_bin_accesses += (1 << i) * this_tier_pages;

          if (((1.0 * ideal_bin_accesses) / total_bin_accesses) >= (1 - process->target_miss_ratio)) {
            break;
          }
        }
        if (ideal_bin_accesses) {
          process->ratio = (1.0 * total_dram_accesses) / ideal_bin_accesses;
          total_ratio += process->ratio;
          ++total_procs;
        }
      }

      tmp = process;
      process = process->next;
      pthread_mutex_unlock(&(tmp->process_lock));
    }

    total_ratio /= total_procs;
    // Figure out how many pages gets us to target performance
    int64_t take_pages = 0;
    int64_t get_pages = 0;
    int64_t taking_procs = 0;
    int64_t getting_procs = 0;
    process = peek_process(&processes_list);
    while (process != NULL) {
      pthread_mutex_lock(&(process->process_lock));
      
      int64_t proc_req_pages = 0;

      if (autofmmr) {
        uint64_t full_fast_pages = DRAMSIZE / PAGE_SIZE;
        uint64_t proc_full_fast_share = 0;
        for(int i = NUM_HOTNESS_LEVELS; i > 0; --i) {
          uint64_t this_tier_pages = min(process->dram_lists[i].numentries + process->nvm_lists[i].numentries, full_fast_pages);
          proc_full_fast_share += (1 << i) * this_tier_pages;
          full_fast_pages -= this_tier_pages;
        }
        double proc_req_fast_share = proc_full_fast_share * total_ratio;
        LOG("Process %d: full fast share: %lu, req fast share %.1f, curr fast share %.1f\n", 
          process->pid, proc_full_fast_share, proc_req_fast_share, process->ratio * proc_full_fast_share);
        for(int i = NUM_HOTNESS_LEVELS; i > 0; --i) {
          uint64_t this_tier_pages = min((process->dram_lists[i].numentries + process->nvm_lists[i].numentries), (uint64_t)proc_req_fast_share / (1 << i));
          proc_req_fast_share -= (1 << i) * this_tier_pages;
          proc_req_pages += this_tier_pages;
          //if(this_tier_pages)
          //  printf("\ttier %d: this tier pages %ld, proc req %ld\n", i, this_tier_pages, proc_req_pages);
        }
      } else {
        uint64_t total_bin_accesses = 0;
        uint64_t total_dram_accesses = 0;
        for (int i = NUM_HOTNESS_LEVELS; i > 0; --i) {
          uint64_t this_tier_pages = process->dram_lists[i].numentries + process->nvm_lists[i].numentries;
          total_bin_accesses += (1 << i) * this_tier_pages;

          this_tier_pages = process->dram_lists[i].numentries;
          total_dram_accesses += (1 << i) * this_tier_pages;
        }

        uint64_t ideal_bin_accesses = 0;
        for (int i = NUM_HOTNESS_LEVELS; i > 0; --i) {
          uint64_t this_tier_pages = process->dram_lists[i].numentries + process->nvm_lists[i].numentries;
          ideal_bin_accesses += (1 << i) * this_tier_pages;
          proc_req_pages += this_tier_pages;

          if (((1.0 * ideal_bin_accesses) / total_bin_accesses) >= (1 - process->target_miss_ratio)) {
            break;
          }
        }
        double proc_req_fast_share = ideal_bin_accesses * total_ratio;
        LOG("Process %d: ideal fast share: %ld, req fast share %.1f, curr fast share %.1f\n", 
          process->pid, ideal_bin_accesses, proc_req_fast_share, process->ratio * ideal_bin_accesses);

      }
        
      process->dram_delta = (proc_req_pages - ((int64_t)(process->current_dram / PAGE_SIZE)));

      // Decay page requests
      if(process->prev_page_transfer * process->dram_delta < 0) {
        // Requesting pages now, while previously giving pages or vice-versa; decay
        process->decay_factor *= 8;
        if(process->decay_factor > MAX_PROC_DECAY)
          process->decay_factor = MAX_PROC_DECAY;
      } else if(process->prev_page_transfer * process->dram_delta > 0) {
        // Repeated request or repeated giving; undecay
        process->decay_factor /= 2;
        if(process->decay_factor < 1)
          process->decay_factor = 1;
      }
      process->prev_page_transfer = process->dram_delta;
      process->dram_delta /= process->decay_factor;
        
      if(process->dram_delta < 0) {
        take_pages += -1 * process->dram_delta;
        ++taking_procs;
      } else {
        get_pages += process->dram_delta;
        ++getting_procs;
      }
      LOG("\treq dram_delta: %ld, req_pages %ld, curr dram %lu, get_pages %ld, take_pages %ld\n", 
        process->dram_delta, proc_req_pages, (int64_t)process->current_dram / PAGE_SIZE, get_pages, take_pages);
          
      tmp = process;
      process = process->next;
      pthread_mutex_unlock(&(tmp->process_lock));
    }
    // Fix an amount of pages to transfer
    int64_t transfer_pages = min(take_pages, get_pages);
    if(transfer_pages > interprocess_migrate / 2 / (int64_t)PAGE_SIZE)
      transfer_pages = interprocess_migrate / 2 / (int64_t)PAGE_SIZE;
    transfer_pages -= dram_free_list.numentries;
    if(transfer_pages < 0)
      transfer_pages = 0;
    LOG("Transfer pages %ld\n", transfer_pages);
    int64_t num_processes = ((processes_list.numentries > 0) ? processes_list.numentries : 1);
    // Negotiate getting these pages for the processes
    process = peek_process(&processes_list);
    while (process != NULL) {
      pthread_mutex_lock(&(process->process_lock));
      // Assign pages proportionately based on requested amount
      // Process donating pages
      if(process->dram_delta < 0) {
        if(transfer_pages >= 1 && take_pages >= 1) {
          process->dram_delta = transfer_pages * ((double)process->dram_delta / (double)take_pages) * (int64_t)PAGE_SIZE;
        }
        else
          process->dram_delta = 0;
      }
      // Process receiving pages
      else if(process->dram_delta > 0 && get_pages >= 1) {
        process->dram_delta = (transfer_pages + dram_free_list.numentries) * ((double)process->dram_delta / (double)get_pages) * (int64_t)PAGE_SIZE;
      } else {
        process->dram_delta = 0;
        // No process is receiving pages, but we have free DRAM pages
        if (dram_free_list.numentries > 0 && get_pages == 0) {
          // Proportionately hand it out to all processes
          process->dram_delta += dram_free_list.numentries * (int64_t)PAGE_SIZE / getting_procs;
          if(process->dram_delta > interprocess_migrate / getting_procs)
            process->dram_delta = interprocess_migrate / getting_procs;
        } 
      }

      // round down to hugepage size
      process->dram_delta -= (process->dram_delta % PAGE_SIZE);

      LOG("Process %d: curr ratio %f; target ratio %f; dram_delta: %ld; decay factor: %ld\n", 
        process->pid, process->ratio, total_ratio, process->dram_delta, process->decay_factor);

      tmp = process;
      process = process->next;
      pthread_mutex_unlock(&(tmp->process_lock));
    }
    // Set remaining migrate rate = TOTAL_RATE - interprocess_rate
    migrate_share = intraprocess_migrate / num_processes;

    //LOG("dram needed: %ld\tdram taking %ld\n", delta_need, delta_take);
    
    // make room on DRAM for by migrating down pages for each process
    process = peek_process(&processes_list);
    while (process != NULL) {
      pthread_mutex_lock(&(process->process_lock));
      process->migrate_down_bytes = 0;
      process->migrate_up_bytes = 0;

      if (process->dram_delta > 0) {
        // process can have more DRAM, so it can migrate things up if it can
        process->migrate_up_bytes = process->dram_delta;
        process->migrate_down_bytes = 0;
        //LOG("process %u (current miss ratio: %f) allocated %ld more dram, now allowed %lu dram\n", process->pid, process->current_miss_ratio, process->dram_delta, process->current_dram + process->dram_delta);
        //LOG("process %u migrating %lu bytes down and %lu bytes up\n", process->pid, process->migrate_down_bytes, process->migrate_up_bytes);
      } else if (process->dram_delta < 0) {
        // process has too much dram, so it needs to migrate things down
        process->migrate_up_bytes = 0;
        process->migrate_down_bytes = -1 * process->dram_delta;
        //LOG("process %u (current miss ratio: %f) allocated %ld less dram, now allowed %lu dram\n", process->pid, process->current_miss_ratio, -1 * process->dram_delta, process->current_dram - process->dram_delta);
        //LOG("process %u migrating %lu bytes down and %lu bytes up\n", process->pid, process->migrate_down_bytes, process->migrate_up_bytes);
      }

      // for each hotness in NVM we ask: how many pages of DRAM are we allowed to replace?
      for (i = 0; i < NUM_HOTNESS_LEVELS; i++) {
        tmp_dram[i] = process->dram_lists[i].numentries;
      }
      migrate_down_bytes = 0;
      for (i = NUM_HOTNESS_LEVELS - 1; i > 2; i--) {
        // algo:
        // -for each NVM hotness we want to get how many pages we can fit into
        //  DRAM if we swap colder pages
        // -tmp_dram is to prevent double counting.
        // 1) how many pages are at this NVM hotness.
        // 2) count how many DRAM pages are lower than this hotness
        // 3) repeat for each DRAM hotness
        nvm_hot_pages_left_to_migrate = process->nvm_lists[i].numentries;
        for (j = 0; j < i; j++) {
          // if we got all the hot pages up then we stop checking
          if(nvm_hot_pages_left_to_migrate <= 0) {
            break;
          }

          // if this level of DRAM has no pages left bail.
          if(tmp_dram[j] == 0) {
            continue;
          }

          // pages we want from this DRAM level is min(pages at this DRAM level, pages we want to move up)
          pages_from_cur_dram = min(tmp_dram[j], nvm_hot_pages_left_to_migrate);
          tmp_dram[j] -= pages_from_cur_dram;
          assert(tmp_dram[j] >= 0);
          migrate_down_bytes += pages_from_cur_dram * PAGE_SIZE;
        }
      }
      migrate_down_bytes -= min(migrate_down_bytes, process->migrate_up_bytes);

      if (migrate_down_bytes > (migrate_share / 2)) {
        migrate_down_bytes = (migrate_share / 2);
      }
      process->migrate_down_bytes += migrate_down_bytes;
      process->migrate_up_bytes += migrate_down_bytes;
      LOG("Process %d: intra-process migrate %lu\n", process->pid, migrate_down_bytes);
      //LOG("process %u migrating %lu bytes down and %lu bytes up\n", process->pid, process->migrate_up_bytes, process->migrate_down_bytes);

      // migrate down first to free up DRAM space
      gettimeofday(&now, NULL);
      //LOG("%f\tprocess %d is migrating %ld bytes down\n", elapsed(&startup, &now), process->pid, process->migrate_down_bytes);
      process_migrate_down(process, process->migrate_down_bytes);

      tmp = process;
      process = process->next;
      pthread_mutex_unlock(&(tmp->process_lock));
    }
    
    // fill DRAM by migrating up pages for each process
    process = peek_process(&processes_list);
    while (process != NULL) {
      pthread_mutex_lock(&(process->process_lock));
      
      // now migrate up to newly freed DRAM space
      gettimeofday(&now, NULL);
      //LOG("%f\tprocess %d is migrating %ld bytes up\n", elapsed(&startup, &now), process->pid, process->migrate_up_bytes);
      process_migrate_up(process, process->migrate_up_bytes);
      
      process->cur_cool_in_dram = partial_cool(process, true);
      process->cur_cool_in_nvm = partial_cool(process, false);

      tmp = process;
      process = process->next;
      pthread_mutex_unlock(&(tmp->process_lock));
    }
    gettimeofday(&end, NULL);
    migrate_time = PEBS_POLICY_INTERVAL * elapsed(&start, &end);
    if (migrate_time < (1.0 * PEBS_POLICY_INTERVAL)) {
      usleep((uint64_t)((1.0 * PEBS_POLICY_INTERVAL) - migrate_time));
    }
  }

  return NULL;
}

static struct hemem_page* pebs_allocate_page(struct hemem_process* process)
{
  struct timeval start, end;
  struct hemem_page *page;

  gettimeofday(&start, NULL);

  pthread_mutex_lock(&(process->process_lock));

  if (process->current_dram < process->max_dram) {
    page = dequeue_page(&dram_free_list);
    if (page != NULL) {
      assert(page->in_dram);
      assert(!page->present);

      page->present = true;
      page->pid = process->pid;
      enqueue_page(&(process->dram_lists[COLD]), page);

      gettimeofday(&end, NULL);
      LOG_TIME("mem_policy_allocate_page: %f s\n", elapsed(&start, &end));

      process->current_dram += pt_to_pagesize(page->pt);

      pthread_mutex_unlock(&(process->process_lock));

      return page;
    }
  }

  // DRAM is full, fall back to NVM
  page = dequeue_page(&nvm_free_list);
  if (page != NULL) {
    assert(!page->in_dram);
    assert(!page->present);

    page->present = true;
    page->pid = process->pid;
    enqueue_page(&(process->nvm_lists[COLD]), page);

    gettimeofday(&end, NULL);
    LOG_TIME("mem_policy_allocate_page: %f s\n", elapsed(&start, &end));

    process->current_nvm += pt_to_pagesize(page->pt);

    pthread_mutex_unlock(&(process->process_lock));

    return page;
  }

  assert(!"Out of memory");
}

struct hemem_page* pebs_pagefault(struct hemem_process *process)
{
  struct hemem_page *page;

  // do the heavy lifting of finding the devdax file offset to place the page
  page = pebs_allocate_page(process);
  assert(page != NULL);

  return page;
}

void pebs_remove_page(struct hemem_process *process, struct hemem_page *page)
{
  assert(page != NULL);

  //LOG("pebs: remove page, put this page into free_page_ring: va: 0x%lx\n", page->va);

  page->in_free_ring = true;

  while (ring_buf_full(process->free_page_ring));
  pthread_mutex_lock(&(process->free_page_ring_lock));
  ring_buf_put(process->free_page_ring, (uint64_t*)page); 
  free_ring_requests++;
  pthread_mutex_unlock(&(process->free_page_ring_lock));

}

void pebs_update_process(struct hemem_process *process, double new_miss_ratio)
{
  process_list_remove(&processes_list, process);
  pthread_mutex_lock(&(process->process_lock));
  process->target_miss_ratio = new_miss_ratio;
  pthread_mutex_unlock(&(process->process_lock));
  enqueue_process(&processes_list, process);
}

void pebs_add_process(struct hemem_process *process)
{
  // new process gets to start with the amount of allowed dram
  // equal to the amount of cold dram the processes of lower
  // priority are using
  enqueue_process(&processes_list, process);
  pthread_mutex_lock(&(process->process_lock));
  process->current_miss_ratio = -1;
  process->current_nvm = 0;
  process->current_dram = 0;
  pthread_mutex_unlock(&(process->process_lock));
}

void pebs_remove_process(struct hemem_process *process)
{
  process_list_remove(&processes_list, process);
  pthread_mutex_lock(&(process->process_lock));
  process->current_dram = 0;
  process->current_nvm = 0;
  pthread_mutex_unlock(&(process->process_lock));
  struct hemem_page *p;
  for(int i = NUM_HOTNESS_LEVELS-1; i >= 0; i--) {
    while(1) {
      p = dequeue_page(&(process->dram_lists[i]));
      if(p == NULL)
        break;
      // reset page stats
      p->present = false;
      p->pid = -1;
      p->hot = COLD;
      for (int i = 0; i < NPBUFTYPES; i++) {
        p->accesses[i] = 0;
        p->tot_accesses[i] = 0;
      }
      enqueue_page(&dram_free_list, p);
    }
  }
  /*
  // allocate the newly freed dram among all the remaining processes
  // policy thread wil correct actual allocations later
  struct hemem_process *tmp, *tmp1;
  tmp = peek_process(&processes_list);
  while (tmp != NULL) {
    pthread_mutex_lock(&(tmp->process_lock));
    tmp1 = tmp;
    tmp = tmp->next;
    pthread_mutex_unlock(&(tmp1->process_lock));
  }*/
}

void pebs_init(void)
{
  pthread_t kswapd_thread;
  pthread_t scan_thread;
  int ret;
  char* sample_period_file_name = NULL;
  FILE* sample_period_file = NULL;

  LOG("pebs_init: started\n");

  for (int i = LAST_HEMEM_THREAD + 1; i < PEBS_NPROCS; i++) {
    sample_periods[i] = SAMPLE_PERIOD;
  }

  sample_period_file_name = getenv("SAMPLE_PERIODS");
  if (sample_period_file_name != NULL) {
    sample_period_file = fopen(sample_period_file_name, "r");
    if (sample_period_file != NULL) {
      for (int i = 0; i < PEBS_NPROCS; i++) {
        fscanf(sample_period_file, "%d", &sample_periods[i]);
      }
    } else {
      fprintf(stderr, "unable to open sample period file %s\n", sample_period_file_name);
    }
  }

  for (int i = LAST_HEMEM_THREAD + 1; i < PEBS_NPROCS; i++) {
    perf_page[i][DRAMREAD] = perf_setup(0x1d3, 0, i, DRAMREAD);      // MEM_LOAD_L3_MISS_RETIRED.LOCAL_DRAM
    perf_page[i][NVMREAD] = perf_setup(0x80d1, 0, i, NVMREAD);     // MEM_LOAD_RETIRED.LOCAL_PMM
    //perf_page[i][WRITE] = perf_setup(0x82d0, 0, i, WRITE);    // MEM_INST_RETIRED.ALL_STORES
  }

  pthread_mutex_init(&(dram_free_list.list_lock), NULL);
  for (int i = 0; i < DRAMSIZE / PAGE_SIZE; i++) {
    struct hemem_page *p = calloc(1, sizeof(struct hemem_page));
    p->devdax_offset = i * PAGE_SIZE;
    p->present = false;
    p->in_dram = true;
    p->ring_present = false;
    p->in_free_ring = false;
    p->pid = -1;
    p->pt = pagesize_to_pt(PAGE_SIZE);

    enqueue_page(&dram_free_list, p);
  }

  pthread_mutex_init(&(nvm_free_list.list_lock), NULL);
  for (int i = 0; i < NVMSIZE / PAGE_SIZE; i++) {
    struct hemem_page *p = calloc(1, sizeof(struct hemem_page));
    p->devdax_offset = i * PAGE_SIZE;
    p->present = false;
    p->in_dram = false;
    p->ring_present = false;
    p->in_free_ring = false;
    p->pid = -1;
    p->pt = pagesize_to_pt(PAGE_SIZE);

    enqueue_page(&nvm_free_list, p);
  }

  char *c_timed_cooling = getenv("TIMEDCOOLING");
  if (c_timed_cooling != NULL) {
    timed_cooling = atoi(c_timed_cooling);
  }
  printf("TIMED COOLING %d\n", timed_cooling);

  char *c_autofmmr = getenv("AUTOFMMR");
  if (c_autofmmr != NULL) {
    autofmmr = atoi(c_autofmmr);
  }
  printf("AUTOFMMR %d\n", autofmmr);

  ret = pthread_create(&scan_thread, NULL, pebs_scan_thread, NULL);
  assert(ret == 0);
  
  ret = pthread_create(&kswapd_thread, NULL, pebs_policy_thread, NULL);
  assert(ret == 0);

  LOG("Memory management policy is PEBS\n");

  LOG("pebs_init: finished\n");

}

void pebs_shutdown()
{
  assert(0);

  for (int i = 0; i < PEBS_NPROCS; i++) {
    for (int j = 0; j < NPBUFTYPES; j++) {
      ioctl(pfd[i][j], PERF_EVENT_IOC_DISABLE, 0);
      //munmap(perf_page[i][j], sysconf(_SC_PAGESIZE) * PERF_PAGES);
    }
  }
}

void count_pages()
{
  struct hemem_process *process;//, *tmp;
  struct timeval now;
  int i;
  gettimeofday(&now, NULL);
  double dram_usage = 0, nvm_usage = 0;
  //process = peek_process(&processes_list);
  process = processes_list.first;
  while (process != NULL) {
    //pthread_mutex_lock(&(process->process_lock));
    fprintf(process->logfd, "%ld\t%f\t%lu\t%lu", rdtscp(), process->current_miss_ratio, process->current_dram, process->current_nvm);
    //fprintf(process->logfd, "%ld\t%f\t%lu\t%lu", rdtscp(), calc_miss_ratio(process), process->current_dram, process->current_nvm);
    fprintf(process->logfd, "\tdram_lists: [%lu", process->dram_lists[COLD].numentries);
    for (i = 1; i < NUM_HOTNESS_LEVELS; i++) {
      fprintf(process->logfd, ", %lu", process->dram_lists[i].numentries);
    }
    fprintf(process->logfd, "]");
    fprintf(process->logfd, "\tnvm_lists: [%lu", process->nvm_lists[COLD].numentries);
    for (i = 1; i < NUM_HOTNESS_LEVELS; i++) {
      fprintf(process->logfd, ", %lu", process->nvm_lists[i].numentries);
    }
    fprintf(process->logfd, "]\tmigrations_up: %lu\tmigrations_down: %lu\tmigration_waits: %lu\tDRAM_samples: %lu\tNVM_samples: %lu\tcools: %lu\n", process->migrations_up, process->migrations_down, process->migration_waits, process->accessed_pages[DRAMREAD], process->accessed_pages[NVMREAD], process->cools);
    fflush(process->logfd);
    LOG_STATS("\tprocess [%d]\tdram_lists: [%lu", process->pid, process->dram_lists[COLD].numentries);
    for (i = 1; i < NUM_HOTNESS_LEVELS; i++) {
      LOG_STATS(", %lu", process->dram_lists[i].numentries);
    }
    LOG_STATS("]\tnvm_lists: [%lu", process->nvm_lists[COLD].numentries);
    for (i = 1; i < NUM_HOTNESS_LEVELS; i++) {
      LOG_STATS(", %lu", process->nvm_lists[i].numentries);
    }
    LOG_STATS("]\tcurrent_miss_ratio: %f\ttarget_miss_ratio: %f\tcurrent_dram: [%ld]\tcurrent_nvm: [%ld]\n", process->current_miss_ratio, process->target_miss_ratio, process->current_dram, process->current_nvm);

    LOG_STATS("\t\tDRAM accesses: [%"PRIu64"]\tNVM accesses: [%"PRIu64"]\twrong memtype: [%"PRIu64"]\tsamples: [", process->accessed_pages[DRAMREAD], process->accessed_pages[NVMREAD], process->wrong_memtype);
    for (i = LAST_HEMEM_THREAD + 1; i < PEBS_NPROCS ; i++) {
      LOG_STATS("%"PRIu64", ", process->samples[i]);
    }
    LOG_STATS("]\tmigration_up: [%lu]\tmigrations_down: [%lu]\tcools: [%lu]\n", process->migrations_up, process->migrations_down, process->cools);
    // To allow redirect by external scripts, we print to stdout
    fprintf(stdout, "p%d: %.0f GB DRAM, %.0f GB NVM,\t", process->pid, 
      ((double)process->current_dram) / (1024.0 * 1024.0 * 1024.0), 
      ((double)process->current_nvm) / (1024.0 * 1024.0 * 1024.0));
    dram_usage += ((double)process->current_dram) / (1024.0 * 1024.0 * 1024.0);
    nvm_usage += ((double)process->current_nvm) / (1024.0 * 1024.0 * 1024.0);
    //tmp = process;
    process = process->next;
    //pthread_mutex_unlock(&(tmp->process_lock));
  }
  fprintf(stdout, "total: %.0f GB DRAM, %.0f GB NVM\n", dram_usage, nvm_usage);
  fflush(stdout);
}

void pebs_stats()
{
  /* TODO: change to per-process
  LOG_STATS("\tdram_hot_list.numentries: [%ld]\tdram_cold_list.numentries: [%ld]\tnvm_hot_list.numentries: [%ld]\tnvm_cold_list.numentries: [%ld]\themem_pages: [%lu]\ttotal_pages: [%lu]\tzero_pages: [%ld]\tthrottle/unthrottle_cnt: [%ld/%ld]\tcools: [%ld]\n",
          dram_hot_list.numentries,
          dram_cold_list.numentries,
          nvm_hot_list.numentries,
          nvm_cold_list.numentries,
          hemem_pages_cnt,
          total_pages_cnt,
          zero_pages_cnt,
          throttle_cnt,
          unthrottle_cnt,
          cools);
  hemem_pages_cnt = total_pages_cnt =  throttle_cnt = unthrottle_cnt = 0;
  */

  LOG_STATS("\tnum_processes: [%lu]\tdram_free: [%lu]\tnvm_free: [%lu]\thot_ring: [%lu]\thot_handled: [%ld]\tcold_ring: [%ld]\tcold_handled: [%ld]\tfree_ring: [%ld]\tfree_handled: [%ld]\tstale_candidates: [%ld]\n",
        processes_list.numentries,
        dram_free_list.numentries,
        nvm_free_list.numentries,
        hot_ring_requests,
        hot_ring_requests_handled,
        cold_ring_requests,
        cold_ring_requests_handled,
        free_ring_requests,
        free_ring_requests_handled,
        stale_candidate_count);
  LOG_STATS("\themem_pages: [%lu]\tother_pages: [%lu]\tzero_pages: [%ld]\tother_processes: [%ld]\tthrottle/unthrottle: [%ld/%ld]\tlost_samples: [%ld]\n",
        hemem_pages_cnt,
        total_pages_cnt - hemem_pages_cnt,
        zero_pages_cnt,
        other_processes_cnt,
        throttle_cnt,
        unthrottle_cnt,
        lost_samples);
  count_pages();

  hemem_pages_cnt = total_pages_cnt = other_processes_cnt = 0;
}
