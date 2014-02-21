libhl
=====

Tiny C library implementing an API to manage some basic datatypes such as hashtables, linked lists, queues, trees, ringbuffers

The provided APIs are :

- hashtable.[ch]  :  A thread-safe hashtable implementation
- linklist.[ch]   :  Thread-safe double linked lists (with also a tag-based API)
- rbtree.[ch]     :  A generic red/black tree implementation
- fbuf.[ch]       :  Dynamically-growing flat buffers
- queue.[ch]      :  A lock-free thread-safe flat queue implementation
- rqueue.[ch]     :  A lock-free thread-safe ring queue implementation (vaule-oriented ringbuffers)
- rbuf.[ch]       :  Byte-oriented ringbuffers
- refcnt.[ch]     :  Reference-count memory manager
