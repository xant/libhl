libhl
=====

Tiny C library implementing a set of APIs to efficiently manage some basic data structures
such as : hashtables, linked lists, queues, trees, ringbuffers, red-black trees, priority queues

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
