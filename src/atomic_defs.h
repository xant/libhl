#ifndef HL_ATOMIC_DEFS_H
#define HL_ATOMIC_DEFS_H

#define ATOMIC_READ(_v) __sync_fetch_and_add(&(_v), 0)
#define ATOMIC_INCREMENT(_v) (void)__sync_fetch_and_add(&(_v), 1)
#define ATOMIC_DECREMENT(_v) (void)__sync_fetch_and_sub(&(_v), 1)
#define ATOMIC_INCREASE(_v, _n) __sync_add_and_fetch(&(_v), (_n))
#define ATOMIC_DECREASE(_v, _n) __sync_sub_and_fetch(&(_v), (_n))
#define ATOMIC_CAS(_v, _o, _n) __sync_bool_compare_and_swap(&(_v), (_o), (_n))
#define ATOMIC_CAS_RETURN(_v, _o, _n) __sync_val_compare_and_swap(&(_v), (_o), (_n))

#define ATOMIC_SET(_v, _n) {\
    int _b = 0;\
    do {\
        _b = ATOMIC_CAS(_v, ATOMIC_READ(_v), _n);\
    } while (__builtin_expect(!_b, 0));\
}

#define ATOMIC_SET_IF(_v, _c, _n, _t) {\
    _t _o = ATOMIC_READ(_v);\
    while (__builtin_expect((_o _c (_n)) && !ATOMIC_CAS(_v, _o, _n), 0)) \
        _o = ATOMIC_READ(_v);\
}


#ifdef THREAD_SAFE

#define __POSIX_C_SOURCE
#include <pthread.h>

#ifdef __MACH__
#include <libkern/OSAtomic.h>
#endif

#define MUTEX_INIT(_mutex) if (__builtin_expect(pthread_mutex_init(&(_mutex), 0) != 0, 0)) { abort(); }
#define MUTEX_DESTROY(_mutex) pthread_mutex_destroy(&(_mutex))
#define MUTEX_LOCK(_mutex) if (__builtin_expect(pthread_mutex_lock(&(_mutex)) != 0, 0)) { abort(); }
#define MUTEX_UNLOCK(_mutex) if (__builtin_expect(pthread_mutex_unlock(&(_mutex)) != 0, 0)) { abort(); }
#ifdef __MACH__
#define SPIN_INIT(_mutex) ((_mutex) = 0)
#define SPIN_DESTROY(_mutex)
#define SPIN_LOCK(_mutex) OSSpinLockLock(&(_mutex))
#define SPIN_UNLOCK(_mutex) OSSpinLockUnlock(&(_mutex))
#else
#define SPIN_INIT(_mutex) pthread_spin_init(&(_mutex), 0)
#define SPIN_DESTROY(_mutex) pthread_spin_destroy(&(_mutex))
#define SPIN_LOCK(_mutex) if (__builtin_expect(pthread_spin_lock(&(_mutex)) != 0, 0)) { abort(); }
#define SPIN_UNLOCK(_mutex) if (__builtin_expect(pthread_spin_unlock(&(_mutex)) != 0, 0)) { abort(); }
#endif
#else
#define MUTEX_INIT(_mutex)
#define MUTEX_DESTROY(_mutex)
#define MUTEX_LOCK(_mutex)
#define MUTEX_UNLOCK(_mutex)
#define SPIN_INIT(_mutex)
#define SPIN_DESTROY(_mutex)
#define SPIN_LOCK(_mutex)
#define SPIN_UNLOCK(_mutex)
#endif

#endif //ATOMIC_DEFS_H

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
