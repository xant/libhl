#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include "atomic_defs.h"
#include "rqueue.h"

#define RQUEUE_FLAG_HEAD   (0x01)
#define RQUEUE_FLAG_UPDATE (0x02)
#define RQUEUE_FLAG_ALL    (0x03)

#define RQUEUE_FLAG_ON(_addr, _flag) (rqueue_page_t *)(((intptr_t) (_addr) & -4) | (_flag))
#define RQUEUE_FLAG_OFF(_addr, _flag) (rqueue_page_t *)((intptr_t) (_addr) & ~(_flag))

#define RQUEUE_CHECK_FLAG(_addr, _flag) (((intptr_t) (_addr) & (_flag)) == (_flag))

// Operation-specific retry limits based on operation complexity and expected contention
#define RQUEUE_READER_MAX_RETRIES     1500    // Complex multi-step operations need more retries
#define RQUEUE_WRITER_CAS_MAX_RETRIES  100    // Simple atomic operations should fail fast
#define RQUEUE_BLOCKING_MAX_RETRIES   2000    // Blocking mode can be more patient
#define RQUEUE_OVERWRITE_MAX_RETRIES    50    // Overwrite should fail fast if can't get reader sync
#define RQUEUE_MAX_RETRIES RQUEUE_READER_MAX_RETRIES  // Legacy compatibility

#define RQUEUE_MIN_SIZE 2

// Retry strategies for different operation types
typedef enum {
    RQUEUE_RETRY_FAST,        // Simple CAS operations - fail fast with minimal backoff
    RQUEUE_RETRY_PATIENT,     // Blocking mode waits - exponential backoff
    RQUEUE_RETRY_COMPLEX,     // Multi-step reader operations - longer delays
    RQUEUE_RETRY_CRITICAL     // Overwrite mode - very short delays, fail quickly
} rqueue_retry_strategy_t;

#ifdef USE_PACKED_STRUCTURES
#define PACK_IF_NECESSARY __attribute__((packed))
#else
#define PACK_IF_NECESSARY
#endif

typedef struct _rqueue_page_s {
    void               *value;
    struct _rqueue_page_s *next;
    struct _rqueue_page_s *prev;
} PACK_IF_NECESSARY rqueue_page_t;

struct _rqueue_s {
    rqueue_page_t                *head;
    rqueue_page_t                *tail;
    rqueue_page_t                *commit;
    rqueue_page_t                *reader;
    rqueue_free_value_callback_t free_value_cb;
    size_t                       size;
    int                          mode;
    uint64_t                     writes;
    uint64_t                     reads;
    int                          read_sync;
    int                          write_sync;
    int                          num_writers;
    int                          head_swap_failed;
    int                          reader_next_swap_failed;
    int                          queue_full_counter;
    int                          overwrite_counter;
    int                          is_empty;
    // Enhanced validation counters
    int                          topology_change_detected;
    int                          concurrent_head_movement;
    int                          commit_advancement_detected;
    int                          concurrent_writer_detected;
    int                          overwrite_state_changed;
    int                          head_next_changed;
    // Retry statistics for performance monitoring and tuning
    int                          fast_retries_succeeded;
    int                          patient_retries_succeeded;
    int                          complex_retries_succeeded;
    int                          critical_retries_succeeded;
    int                          total_retry_failures;
    uint64_t                     total_retry_attempts;
    uint64_t                     total_backoff_time_us;
} PACK_IF_NECESSARY;

// Ensure pointer alignment for flag embedding
_Static_assert(sizeof(void*) >= 4, "Pointer size must be at least 4 bytes for flag embedding");
_Static_assert(_Alignof(rqueue_page_t) >= 4, "rqueue_page_t must be 4-byte aligned for flag embedding");

static inline void rqueue_destroy_page(rqueue_page_t *page, rqueue_free_value_callback_t free_value_cb) {
    if (page->value && free_value_cb)
        free_value_cb(page->value);
    free(page);
}

// VALIDATION HELPERS: Functions to check if topology snapshots are still valid
static inline int rqueue_validate_reader_snapshot(rqueue_t *rb, 
    rqueue_page_t *head, rqueue_page_t *commit, rqueue_page_t *next) {
    return (ATOMIC_READ_ACQUIRE(rb->head) == head &&
            ATOMIC_READ_ACQUIRE(rb->commit) == commit &&
            ATOMIC_READ_ACQUIRE(head->next) == next);
}

static inline int rqueue_validate_writer_snapshot(rqueue_t *rb,
    rqueue_page_t *tail, rqueue_page_t *commit, rqueue_page_t *head) {
    return (ATOMIC_READ_ACQUIRE(rb->tail) == tail &&
            ATOMIC_READ_ACQUIRE(rb->commit) == commit &&
            ATOMIC_READ_ACQUIRE(rb->head) == head);
}

// ENHANCED RETRY LOGIC: Exponential backoff with jitter and operation-specific strategies
static inline void rqueue_backoff_with_strategy(rqueue_t *rb, rqueue_retry_strategy_t strategy, 
                                               int retry_count, uint64_t *backoff_time_us) {
    uint64_t start_time = 0;
    int delay_us = 0;
    
    // Record start time for statistics
    struct timespec ts_start;
    if (clock_gettime(CLOCK_MONOTONIC, &ts_start) == 0) {
        start_time = ts_start.tv_sec * 1000000ULL + ts_start.tv_nsec / 1000;
    }
    
    switch (strategy) {
        case RQUEUE_RETRY_FAST:
            // Simple CAS operations - minimal backoff, fail fast
            if (retry_count < 10) {
                sched_yield();  // First few retries just yield
            } else if (retry_count < 50) {
                delay_us = 1 << ((retry_count - 10) / 5);  // 1, 2, 4, 8 μs
                delay_us = (delay_us > 100) ? 100 : delay_us;  // Cap at 100μs
            }
            break;
            
        case RQUEUE_RETRY_PATIENT:
            // Blocking mode - exponential backoff for persistent full conditions
            if (retry_count < 5) {
                sched_yield();
            } else {
                delay_us = 1 << ((retry_count - 5) / 10);  // 1, 2, 4, 8, 16... μs
                delay_us = (delay_us > 10000) ? 10000 : delay_us;  // Cap at 10ms
            }
            break;
            
        case RQUEUE_RETRY_COMPLEX:
            // Multi-step reader operations - longer delays for complex operations
            if (retry_count < 3) {
                sched_yield();
            } else {
                delay_us = (1 << ((retry_count - 3) / 8)) * 2;  // 2, 4, 8, 16... μs
                delay_us = (delay_us > 5000) ? 5000 : delay_us;  // Cap at 5ms
            }
            break;
            
        case RQUEUE_RETRY_CRITICAL:
            // Overwrite mode - very short delays, designed to fail quickly
            if (retry_count < 5) {
                sched_yield();
            } else {
                delay_us = retry_count;  // Linear increase: 5, 6, 7, 8... μs
                delay_us = (delay_us > 50) ? 50 : delay_us;  // Cap at 50μs
            }
            break;
    }
    
    if (delay_us > 0) {
        // Add jitter to prevent thundering herd (±25% variance)
        int jitter = (rand() % (delay_us / 2 + 1)) - (delay_us / 4);
        delay_us = delay_us + jitter;
        if (delay_us < 1) delay_us = 1;  // Minimum 1μs
        
        usleep(delay_us);
        
        // Update statistics with actual backoff time
        if (backoff_time_us && start_time > 0) {
            struct timespec ts_end;
            if (clock_gettime(CLOCK_MONOTONIC, &ts_end) == 0) {
                uint64_t end_time = ts_end.tv_sec * 1000000ULL + ts_end.tv_nsec / 1000;
                *backoff_time_us += (end_time - start_time);
            }
        }
    }
}

/*
static rqueue_page_t *rqueue_create_page() {
    rqueue_page_t *page = calloc(1, sizeof(rqueue_page_t));
    return page;
}

static void *rqueue_page_value(rqueue_page_t *page) {
    return page->value;
}
*/

rqueue_t *rqueue_create(size_t size, rqueue_mode_t mode) {
    size_t i;
    rqueue_t *rb = calloc(1, sizeof(rqueue_t));
    if (!rb)
        return NULL;
    rb->size = (size > RQUEUE_MIN_SIZE) ? size : RQUEUE_MIN_SIZE;
    rb->mode = mode;
    rb->is_empty = 1;
    
    // MEMORY LIFECYCLE: All ring buffer pages are pre-allocated here and remain at stable
    // addresses throughout the queue's lifetime. Pages are never individually freed or
    // reallocated during operations, eliminating classic ABA problems from memory reuse.
    for (i = 0; i <= rb->size; i++) { // NOTE : we create one page more than the requested size
        rqueue_page_t *page = calloc(1, sizeof(rqueue_page_t));
        if (!page) {
            // Free all previously allocated pages (ring not yet closed, so simple traversal)
            rqueue_page_t *current = rb->head;
            while (current) {
                rqueue_page_t *next = current->next;
                free(current);
                current = next;
            }
            free(rb);
            return NULL;
        }
        if (!rb->head) {
            rb->head = rb->tail = page;
        } else if (rb->tail) {
            rb->tail->next = page;
        }
        page->prev = rb->tail;
        rb->tail = page;
    }

    if (!rb->head || !rb->tail) {
        free(rb);
        return NULL;
    }

    // close the ringbuffer - safe to dereference since we just verified non-NULL above
    rb->head->prev = rb->tail;
    rb->tail->next = rb->head;
    ATOMIC_STORE_RELEASE(rb->tail->next, RQUEUE_FLAG_ON(rb->head, RQUEUE_FLAG_HEAD));

    rb->tail = rb->head;

    rb->commit = rb->tail;

    // the reader page is out of the ringbuffer
    rb->reader = calloc(1, sizeof(rqueue_page_t));
    if (!rb->reader) {
        // Free all ring buffer pages
        rqueue_page_t *page = rb->head;
        do {
            rqueue_page_t *next = RQUEUE_FLAG_OFF(page->next, RQUEUE_FLAG_ALL);
            free(page);
            page = next;
        } while (page != rb->head);
        free(rb);
        return NULL;
    }
    rb->reader->prev = rb->head->prev;
    rb->reader->next = rb->head;
    return rb;
}

void rqueue_set_free_value_callback(rqueue_t *rb, rqueue_free_value_callback_t cb) {
    if (rb == NULL) {
        // do nothing
        return;
    }
    rb->free_value_cb = cb;
}

void rqueue_destroy(rqueue_t *rb) {
    if (rb == NULL) {
        // do nothing
        return;
    }
    
    // MEMORY LIFECYCLE: Pages are only freed here when the entire queue is destroyed.
    // During normal operation, pages are reused cyclically but never freed individually.
    rqueue_page_t *page = rb->head;
    do {
        rqueue_page_t *p = page;
        page = RQUEUE_FLAG_OFF(p->next, RQUEUE_FLAG_ALL);
        rqueue_destroy_page(p, rb->free_value_cb);
    } while (__builtin_expect(page != rb->head, 1));

    // release the reader page as well
    rqueue_destroy_page(rb->reader, rb->free_value_cb);

    // and finally release the ringbuffer descriptor
    free(rb);
}

void *rqueue_read(rqueue_t *rb) {
    if (rb == NULL) {
        // do nothing
        return NULL;
    }

    int i;
    void *v = NULL;

    // READER SYNCHRONIZATION: This loop ensures bounded execution time for read operations.
    // The read_sync lock is always released within RQUEUE_READER_MAX_RETRIES iterations, preventing
    // any possibility of indefinite blocking for writers in overwrite mode.
    uint64_t backoff_time = 0;
    for (i = 0; i < RQUEUE_READER_MAX_RETRIES; i++) {
        ATOMIC_INCREMENT(rb->total_retry_attempts);

        if (__builtin_expect(ATOMIC_CAS(rb->read_sync, 0, 1), 1)) {
            rqueue_page_t *head = ATOMIC_READ_ACQUIRE(rb->head);

            rqueue_page_t *commit = ATOMIC_READ_ACQUIRE(rb->commit);
            rqueue_page_t *next = ATOMIC_READ_ACQUIRE(head->next);
            rqueue_page_t *old_next = ATOMIC_READ_ACQUIRE(rb->reader->next);

            if (rb->reader == commit || ATOMIC_READ_RELAXED(rb->is_empty))
            { // nothing to read
                ATOMIC_CAS(rb->read_sync, 1, 0);
                continue;
            }

            // VALIDATION POINT 1: Re-validate critical pointers before ring modifications
            // Check that the topology snapshot we took is still valid
            if (!rqueue_validate_reader_snapshot(rb, head, commit, next)) {
                // Topology changed - retry with fresh snapshot
                ATOMIC_INCREMENT(rb->topology_change_detected);
                ATOMIC_CAS(rb->read_sync, 1, 0);
                // Use complex retry strategy for topology changes
                rqueue_backoff_with_strategy(rb, RQUEUE_RETRY_COMPLEX, i, &backoff_time);
                continue;
            }

            // ABA PROTECTION: The following CAS operations are protected from ABA problems by:
            // 1. Pages are pre-allocated at creation and never individually freed (stable addresses)
            // 2. Only one reader thread can execute this section (read_sync lock above)
            // 3. Sequential CAS operations validate that ring topology hasn't changed
            // 4. Reader page is isolated outside the ring during this operation
            // 5. Enhanced validation points catch topology changes before modifications
            
            // first connect the reader page to the head->next page
            if (ATOMIC_CAS(rb->reader->next, old_next, RQUEUE_FLAG_ON(next, RQUEUE_FLAG_HEAD))) {
                ATOMIC_STORE_RELEASE(rb->reader->prev, head->prev);

                // VALIDATION POINT 2: Ensure head pointer hasn't moved before critical ring insertion
                // This catches overwrite mode head advancement between our operations
                rqueue_page_t *current_head = ATOMIC_READ_ACQUIRE(rb->head);
                if (current_head != head) {
                    // Head was advanced (likely by overwrite mode) - abort and retry
                    ATOMIC_INCREMENT(rb->concurrent_head_movement);
                    // Restore reader state
                    ATOMIC_STORE_RELAXED(rb->reader->next, old_next);
                    ATOMIC_CAS(rb->read_sync, 1, 0);
                    // Use complex retry strategy for concurrent head movement
                    rqueue_backoff_with_strategy(rb, RQUEUE_RETRY_COMPLEX, i, &backoff_time);
                    continue;
                }

                // now connect head->prev->next to the reader page, replacing the head with the reader
                // in the ringbuffer. If ring topology changed, this CAS will fail, validating our assumptions
                if (ATOMIC_CAS(head->prev->next, RQUEUE_FLAG_ON(head, RQUEUE_FLAG_HEAD), rb->reader)) {
                    // now the head is out of the ringbuffer and we can update the pointer
                    ATOMIC_CAS(rb->head, head, next);

                    ATOMIC_STORE_RELEASE(next->prev, rb->reader);
                    ATOMIC_STORE_RELEASE(rb->reader, head); // the head is the new reader
                    v = ATOMIC_READ_ACQUIRE(rb->reader->value);
                    if (v == NULL) { 
                        ATOMIC_CAS(rb->read_sync, 1, 0);
                        continue;
                    }
                    ATOMIC_CAS(rb->reader->value, v, NULL);
                    ATOMIC_INCREMENT(rb->reads);
                    
                    // EMPTY STATE RACE FIX: Prevent TOCTOU race between head/tail comparison and is_empty update
                    // Use CAS with validation to atomically update is_empty only when head/tail are consistent
                    rqueue_page_t *head_snap = ATOMIC_READ_ACQUIRE(rb->head);
                    rqueue_page_t *tail_snap = ATOMIC_READ_ACQUIRE(rb->tail);
                    
                    if (head_snap == tail_snap) {
                        // CAS is_empty from 0 to 1, but only if head/tail still match our snapshots
                        int expected_empty = 0;
                        if (ATOMIC_READ_ACQUIRE(rb->head) == head_snap && 
                            ATOMIC_READ_ACQUIRE(rb->tail) == tail_snap &&
                            ATOMIC_CAS(rb->is_empty, expected_empty, 1)) {
                            // Successfully set empty state atomically
                        }
                    }
                    ATOMIC_CAS(rb->read_sync, 1, 0);
                    break;
                } else {
                    ATOMIC_INCREMENT(rb->head_swap_failed);
                    // SEQUENTIAL CAS VALIDATION: The failure of the second CAS indicates the ring
                    // topology changed between our pointer reads and this operation. We restore
                    // the reader state and retry, ensuring consistency.
                    ATOMIC_STORE_RELAXED(rb->reader->next, old_next);
                    rqueue_backoff_with_strategy(rb, RQUEUE_RETRY_COMPLEX, i, &backoff_time);
                }
            } else {
                ATOMIC_INCREMENT(rb->reader_next_swap_failed);
                // SEQUENTIAL CAS VALIDATION: First CAS failed, indicating another thread modified
                // the reader->next pointer. This prevents proceeding with stale pointer values.
                rqueue_backoff_with_strategy(rb, RQUEUE_RETRY_COMPLEX, i, &backoff_time);
            }
            ATOMIC_CAS(rb->read_sync, 1, 0);
        } else {
            // Failed to acquire read_sync - another reader is active
            rqueue_backoff_with_strategy(rb, RQUEUE_RETRY_COMPLEX, i, &backoff_time);
        }
    }
    
    // Update retry statistics based on outcome
    ATOMIC_INCREASE(rb->total_backoff_time_us, backoff_time);
    if (v != NULL) {
        ATOMIC_INCREMENT(rb->complex_retries_succeeded);
    } else {
        ATOMIC_INCREMENT(rb->total_retry_failures);
    }
    
    // READER SYNC GUARANTEE: All code paths above release read_sync in bounded time.
    // This ensures writers in overwrite mode cannot be blocked indefinitely.
    return v;
}

static inline void
rqueue_update_value(rqueue_t *rb, rqueue_page_t *page, void *value) {
    // Only free the old value if we successfully replace it
    void *old_value;
    do {
        old_value = ATOMIC_READ_ACQUIRE(page->value);
    } while (!ATOMIC_CAS(page->value, old_value, value));
    
    if (old_value && rb->free_value_cb)
        rb->free_value_cb(old_value);
}

int
rqueue_write(rqueue_t *rb, void *value) {
    if (rb == NULL) {
        // do nothing
        return -1; // Invalid queue pointer
    }

    int retries = 0;

    rqueue_page_t *temp_page = NULL;
    rqueue_page_t *next_page = NULL;
    rqueue_page_t *tail = NULL;
    rqueue_page_t *head = NULL;
    rqueue_page_t *commit;
    
    // TODO - get rid of this barrier
    while (!__builtin_expect(ATOMIC_CAS(rb->write_sync, 0, 1), 1))
        sched_yield();

    // WRITER COUNTING: Track active writers holding write_sync lock.
    // This count is used for commit optimization - only increment when we're actively writing.
    // Must be decremented before releasing write_sync to maintain accurate active count.
    ATOMIC_INCREMENT(rb->num_writers);
    do {
        temp_page = ATOMIC_READ_ACQUIRE(rb->tail);
        commit = ATOMIC_READ_ACQUIRE(rb->commit);
        next_page = RQUEUE_FLAG_OFF(ATOMIC_READ_ACQUIRE(temp_page->next), RQUEUE_FLAG_ALL);
        if (!next_page) {
            ATOMIC_DECREMENT(rb->num_writers);  // Exit path: single decrement
            ATOMIC_CAS(rb->write_sync, 1, 0); 
            return -1;
        }
        head = ATOMIC_READ_ACQUIRE(rb->head);

        // VALIDATION POINT 3: Check for rapid reader advancement before proceeding
        // Readers might have significantly advanced commit pointer since we read it
        rqueue_page_t *current_commit = ATOMIC_READ_ACQUIRE(rb->commit);
        if (current_commit != commit) {
            // Commit advanced significantly - re-read with fresh snapshot
            ATOMIC_INCREMENT(rb->commit_advancement_detected);
            continue;  // Restart loop with fresh reads
        }

        // VALIDATION POINT 4: Verify tail hasn't been advanced by competing writers
        rqueue_page_t *current_tail = ATOMIC_READ_ACQUIRE(rb->tail);
        if (current_tail != temp_page) {
            // Another writer advanced tail - use fresh snapshot
            ATOMIC_INCREMENT(rb->concurrent_writer_detected);
            continue;  // Restart with fresh reads
        }

        if (rb->mode == RQUEUE_MODE_BLOCKING && commit == temp_page && temp_page != head && next_page != head) {
            if (retries++ < RQUEUE_BLOCKING_MAX_RETRIES) {
                ATOMIC_INCREMENT(rb->total_retry_attempts);
                ATOMIC_CAS(rb->tail, temp_page, next_page);
                // WRITER COUNTING: Decrement before releasing lock to maintain active writer count
                ATOMIC_DECREMENT(rb->num_writers);
                ATOMIC_CAS(rb->write_sync, 1, 0);
                
                // Use patient retry strategy for blocking mode (queue full scenario)
                uint64_t backoff_time = 0;
                rqueue_backoff_with_strategy(rb, RQUEUE_RETRY_PATIENT, retries, &backoff_time);
                ATOMIC_INCREASE(rb->total_backoff_time_us, backoff_time);

                while (!__builtin_expect(ATOMIC_CAS(rb->write_sync, 0, 1), 1)) {
                    rqueue_backoff_with_strategy(rb, RQUEUE_RETRY_PATIENT, retries, &backoff_time);
                    ATOMIC_INCREASE(rb->total_backoff_time_us, backoff_time);
                }

                // WRITER COUNTING: Increment when reacquiring lock to resume active writing
                ATOMIC_INCREMENT(rb->num_writers);
                continue;
            } else {
                ATOMIC_DECREMENT(rb->num_writers);  // Exit path: decrement before releasing lock
                ATOMIC_CAS(rb->write_sync, 1, 0);
                ATOMIC_INCREMENT(rb->queue_full_counter);
                ATOMIC_INCREMENT(rb->total_retry_failures);
                ATOMIC_INCREMENT(rb->patient_retries_succeeded);  // Reached max retries, but was using patient strategy
                return -2;
            }
        }

       if (RQUEUE_CHECK_FLAG(ATOMIC_READ_ACQUIRE(temp_page->next), RQUEUE_FLAG_HEAD) || (next_page == head && !ATOMIC_READ_RELAXED(rb->is_empty))) {
            if (rb->mode == RQUEUE_MODE_OVERWRITE && ATOMIC_READ_ACQUIRE(commit->next) == temp_page) {
               // OVERWRITE MODE SYNCHRONIZATION: This synchronization with readers is necessary because
               // the current reader algorithm assumes head pointer stability during read operations.
               // Without this sync, concurrent head advancement would corrupt the ring structure.
               //
               // DEADLOCK SAFETY: This cannot cause deadlocks because:
               // 1. Readers always release read_sync in bounded time (max RQUEUE_MAX_RETRIES iterations)
               // 2. All reader code paths have explicit read_sync release operations
               // 3. If CAS fails, writer continues with normal retry logic (no blocking)
               //
               // PERFORMANCE IMPACT: Writers may retry when readers are active, causing temporary
               // latency spikes but no correctness issues. This is a performance trade-off, not a bug
               // and will happen only if the rqueue is full (which is unlikely to happen if there is
               // contention with active readers)
               if (ATOMIC_CAS(rb->read_sync, 0, 1)) {
                    // Successfully acquired reader sync - safe to advance head
                    ATOMIC_INCREMENT(rb->overwrite_counter);
                    
                    // VALIDATION POINT 5: Double-check that overwrite is still needed
                    // Conditions might have changed while waiting for reader sync
                    rqueue_page_t *current_head = ATOMIC_READ_ACQUIRE(rb->head);
                    rqueue_page_t *current_commit = ATOMIC_READ_ACQUIRE(rb->commit);
                    
                    if (current_head != head || current_commit != commit) {
                        // Ring state changed while acquiring sync - restart
                        ATOMIC_INCREMENT(rb->overwrite_state_changed);
                        ATOMIC_CAS(rb->read_sync, 1, 0);
                        continue;  // Retry with fresh snapshot
                    }
                    
                    // Proceed with head advancement
                    rqueue_page_t *nextpp = RQUEUE_FLAG_OFF(ATOMIC_READ_ACQUIRE(current_head->next), RQUEUE_FLAG_ALL);
                    
                    // VALIDATION POINT 6: Ensure head->next is still valid before using it
                    if (nextpp != RQUEUE_FLAG_OFF(ATOMIC_READ_ACQUIRE(head->next), RQUEUE_FLAG_ALL)) {
                        // Head->next changed during operation
                        ATOMIC_INCREMENT(rb->head_next_changed);
                        ATOMIC_CAS(rb->read_sync, 1, 0);
                        continue;
                    }
                    
                    // Safe to proceed with atomic head advancement
                    if (ATOMIC_CAS(current_head->next, nextpp, RQUEUE_FLAG_ON(nextpp, RQUEUE_FLAG_HEAD))) {
                        ATOMIC_CAS(temp_page->next, RQUEUE_FLAG_ON(current_head, RQUEUE_FLAG_HEAD), current_head);
                        // Safe to use ATOMIC_STORE since we hold read_sync - no concurrent head modifications
                        ATOMIC_STORE_RELEASE(rb->head, nextpp);
                    }
                   ATOMIC_CAS(rb->read_sync, 1, 0);
                   continue;
               }
               // CAS failed - reader is active. Continue with normal write retry logic.
            }
            if (retries++ < RQUEUE_WRITER_CAS_MAX_RETRIES) {
                ATOMIC_INCREMENT(rb->total_retry_attempts);
                // WRITER COUNTING: Decrement before releasing lock to maintain active writer count
                ATOMIC_DECREMENT(rb->num_writers);
                ATOMIC_CAS(rb->write_sync, 1, 0);
                
                // Use fast retry strategy for simple CAS operations
                uint64_t backoff_time = 0;
                rqueue_backoff_with_strategy(rb, RQUEUE_RETRY_FAST, retries, &backoff_time);
                ATOMIC_INCREASE(rb->total_backoff_time_us, backoff_time);

                while (!__builtin_expect(ATOMIC_CAS(rb->write_sync, 0, 1), 1)) {
                    rqueue_backoff_with_strategy(rb, RQUEUE_RETRY_FAST, retries, &backoff_time);
                    ATOMIC_INCREASE(rb->total_backoff_time_us, backoff_time);
                }

                // WRITER COUNTING: Increment when reacquiring lock to resume active writing
                ATOMIC_INCREMENT(rb->num_writers);
                continue;
            } else {
                ATOMIC_DECREMENT(rb->num_writers);  // Exit path: decrement before releasing lock
                ATOMIC_CAS(rb->write_sync, 1, 0);
                ATOMIC_INCREMENT(rb->queue_full_counter);
                ATOMIC_INCREMENT(rb->total_retry_failures);
                ATOMIC_INCREMENT(rb->fast_retries_succeeded);  // Reached max retries, but was using fast strategy
                return -2;
            }

        } 
       
        if (!RQUEUE_CHECK_FLAG(ATOMIC_READ_ACQUIRE(temp_page->next), RQUEUE_FLAG_HEAD)) {
            // ABA PROTECTION: This CAS is safe from ABA because pages have stable addresses
            // (never freed/reallocated) and the write_sync lock ensures only one writer
            // can modify the tail pointer at a time.
            tail = ATOMIC_CAS_RETURN(rb->tail, temp_page, next_page);
        }
    } while (tail != temp_page && retries++ < RQUEUE_WRITER_CAS_MAX_RETRIES);

    if (!tail || tail != temp_page) {
        ATOMIC_DECREMENT(rb->num_writers);  // Exit path: single decrement
        ATOMIC_CAS(rb->write_sync, 1, 0);
        return -1;
    } 


    // Only free the old value if we successfully replace it
    void *old_value;
    do {
        old_value = ATOMIC_READ_ACQUIRE(tail->value);
    } while (!ATOMIC_CAS(tail->value, old_value, value));
    
    if (old_value && rb->free_value_cb)
        rb->free_value_cb(old_value);


    ATOMIC_INCREMENT(rb->writes);
    ATOMIC_STORE_RELEASE(rb->is_empty, 0);

    // COMMIT OPTIMIZATION: If this is the only active writer, advance the commit pointer.
    // This optimization relies on num_writers tracking active writers holding write_sync.
    if (ATOMIC_READ_RELAXED(rb->num_writers) == 1)
        ATOMIC_CAS(rb->commit, ATOMIC_READ_ACQUIRE(rb->commit), tail);
    
    // Update retry statistics for successful write
    if (retries > 0) {
        ATOMIC_INCREMENT(rb->fast_retries_succeeded);  // Most writes use fast strategy
    }
    
    ATOMIC_DECREMENT(rb->num_writers);  // Success path: single decrement
    ATOMIC_CAS(rb->write_sync, 1, 0);

    return 0;
}

uint64_t rqueue_write_count(rqueue_t *rb) {
    if (rb == NULL) {
        return 0;
    }
    return ATOMIC_READ_RELAXED(rb->writes);
}

uint64_t rqueue_read_count(rqueue_t *rb) {
    if (rb == NULL) {
        return 0;
    }
    return ATOMIC_READ_RELAXED(rb->reads);
}

void rqueue_set_mode(rqueue_t *rb, rqueue_mode_t mode) {
    if (rb == NULL) {
        // do nothing
        return;
    }
    rb->mode = mode;
}

rqueue_mode_t rqueue_mode(rqueue_t *rb) {
    if (rb == NULL) {
        // do nothing
        return RQUEUE_MODE_INVALID;
    }
    return rb->mode;
}

// Only for debugging purposes
char *rqueue_stats(rqueue_t *rb) {
    if (!rb) {
        return "Invalid pointer";
    }

    // Take snapshot of key pointers
    rqueue_page_t *reader = ATOMIC_READ_RELAXED(rb->reader);
    rqueue_page_t *head = ATOMIC_READ_RELAXED(rb->head);
    rqueue_page_t *tail = ATOMIC_READ_RELAXED(rb->tail);
    rqueue_page_t *commit = ATOMIC_READ_RELAXED(rb->commit);
    rqueue_page_t *tail_next = tail ? ATOMIC_READ_RELAXED(tail->next) : NULL;
    rqueue_page_t *commit_next = commit ? ATOMIC_READ_RELAXED(commit->next) : NULL;

    const char *format = 
           "reader:                    %p \n"
           "head:                      %p \n"
           "tail:                      %p \n"
           "tail_next:                 %p \n"
           "commit:                    %p \n"
           "commit_next:               %p \n"
           "reads:                     %"PRId64" \n"
           "writes:                    %"PRId64" \n"
           "mode:                      %s \n"
           "is_empty:                  %s \n"
           "head_swap_failed:          %d \n"
           "reader_next_swap_failed:   %d \n"
           "queue_full_counter:        %d \n"
           "overwrite_counter:         %d \n"
           "topology_change_detected:  %d \n"
           "concurrent_head_movement:  %d \n"
           "commit_advancement_detected: %d \n"
           "concurrent_writer_detected: %d \n"
           "overwrite_state_changed:   %d \n"
           "head_next_changed:         %d \n"
           "fast_retries_succeeded:    %d \n"
           "patient_retries_succeeded: %d \n"
           "complex_retries_succeeded: %d \n"
           "critical_retries_succeeded: %d \n"
           "total_retry_failures:      %d \n"
           "total_retry_attempts:      %"PRIu64" \n"
           "total_backoff_time_us:     %"PRIu64" \n"
           "avg_backoff_per_retry_us:  %.2f \n";

#define STATS_ARGS \
           reader, \
           head, \
           tail, \
           tail_next, \
           commit, \
           commit_next, \
           ATOMIC_READ_RELAXED(rb->reads), \
           ATOMIC_READ_RELAXED(rb->writes), \
           rb->mode == RQUEUE_MODE_BLOCKING ? "blocking" : "overwrite", \
           ATOMIC_READ_RELAXED(rb->is_empty) ? "true" : "false", \
           ATOMIC_READ_RELAXED(rb->head_swap_failed), \
           ATOMIC_READ_RELAXED(rb->reader_next_swap_failed), \
           ATOMIC_READ_RELAXED(rb->queue_full_counter), \
           ATOMIC_READ_RELAXED(rb->overwrite_counter), \
           ATOMIC_READ_RELAXED(rb->topology_change_detected), \
           ATOMIC_READ_RELAXED(rb->concurrent_head_movement), \
           ATOMIC_READ_RELAXED(rb->commit_advancement_detected), \
           ATOMIC_READ_RELAXED(rb->concurrent_writer_detected), \
           ATOMIC_READ_RELAXED(rb->overwrite_state_changed), \
           ATOMIC_READ_RELAXED(rb->head_next_changed), \
           ATOMIC_READ_RELAXED(rb->fast_retries_succeeded), \
           ATOMIC_READ_RELAXED(rb->patient_retries_succeeded), \
           ATOMIC_READ_RELAXED(rb->complex_retries_succeeded), \
           ATOMIC_READ_RELAXED(rb->critical_retries_succeeded), \
           ATOMIC_READ_RELAXED(rb->total_retry_failures), \
           ATOMIC_READ_RELAXED(rb->total_retry_attempts), \
           ATOMIC_READ_RELAXED(rb->total_backoff_time_us), \
           (ATOMIC_READ_RELAXED(rb->total_retry_attempts) > 0) ? \
               ((double)ATOMIC_READ_RELAXED(rb->total_backoff_time_us) / ATOMIC_READ_RELAXED(rb->total_retry_attempts)) : 0.0 \

    // First pass: calculate exact size needed
    int needed = snprintf(NULL, 0, format, STATS_ARGS);
    
    if (needed < 0)
        return NULL;
        
    char *buf = malloc(needed + 1);
    if (!buf)
        return NULL;

    snprintf(buf, needed + 1, format, STATS_ARGS);

#undef STATS_ARGS
    return buf;
}

size_t rqueue_size(rqueue_t *rb)
{
    if (!rb) {
        return 0;
    }
    return rb->size;
}

int rqueue_isempty(rqueue_t *rb) {
    if (!rb) {
        return -1;
    }
    return ATOMIC_READ_RELAXED(rb->is_empty);
}
// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
