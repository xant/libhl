#ifndef __ATOMIC_DEFS_H__
#define __ATOMIC_DEFS_H__

#define ATOMIC_READ(__v) __sync_fetch_and_add(&(__v), 0)
#define ATOMIC_INCREMENT(__v) (void)__sync_fetch_and_add(&(__v), 1)
#define ATOMIC_DECREMENT(__v) (void)__sync_fetch_and_sub(&(__v), 1)
#define ATOMIC_INCREASE(__v, __n) __sync_add_and_fetch(&(__v), (__n))
#define ATOMIC_DECREASE(__v, __n) __sync_sub_and_fetch(&(__v), (__n))
#define ATOMIC_CAS(__v, __o, __n) __sync_bool_compare_and_swap(&(__v), (__o), (__n))
#define ATOMIC_CAS_RETURN(__v, __o, __n) __sync_val_compare_and_swap(&(__v), (__o), (__n))

#define ATOMIC_SET(__v, __n) {\
    int __b = 0;\
    do {\
        __b = ATOMIC_CAS(__v, ATOMIC_READ(__v), __n);\
    } while (__builtin_expect(!__b, 0));\
}

#define ATOMIC_SET_IF(__v, __c, __n, __t) {\
    __t __o = ATOMIC_READ(__v);\
    while (__builtin_expect((__o __c (__n)) && !ATOMIC_CAS(__v, __o, __n), 0)) \
        __o = ATOMIC_READ(__v);\
}

#endif //__ATOMIC_DEFS_H__

/* vim: tabstop=4 shiftwidth=4 expandtab: */
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
