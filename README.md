libhl
=====

C library implementing a set of APIs to efficiently manage some basic data structures
such as : hashtables, linked lists, queues, trees, ringbuffers, red-black trees, priority queues, skip lists

The provided APIs are :

- hashtable.[ch]  :  A thread-safe hashtable implementation
- linklist.[ch]   :  Thread-safe double linked lists (with also a tag-based API)
- rbtree.[ch]     :  A generic red/black tree implementation
- fbuf.[ch]       :  Dynamically-growing flat buffers
- queue.[ch]      :  A lock-free thread-safe flat (dynamically growing) queue implementation
- rqueue.[ch]     :  A lock-free thread-safe circular (fixed size) queue implementation (aka: vaule-oriented ringbuffers)
- rbuf.[ch]       :  Byte-oriented ringbuffers
- refcnt.[ch]     :  Reference-count memory manager
- binheap.[ch]    :  A binomial heap implementation (building block for the priority queue implementation)
- pqueue.[ch]     :  A priority queue implementation
- skiplist.[ch]   :  A skip list implementation
- graph.[ch]      :  A generic graph implementation which allow defining chooser functions to determine paths

Provided APIs typically don't depend on each other and can be simply included in an existing project by 
copying both the .c and the .h files plus, if necessary, bsd_queue.h and/or atomic_defs.h into the project sourcetree.

The only exceptions are:

- queue => depending on: refcnt
- pqueue => depending on: binheap
- graph => depending on: hashtable
