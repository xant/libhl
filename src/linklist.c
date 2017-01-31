/* linked list management library - by xant
 */

//#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "linklist.h"
#include "atomic_defs.h"

typedef struct _list_entry_s {
    struct _linked_list_s *list;
    struct _list_entry_s *prev;
    struct _list_entry_s *next;
    void *value;
    int tagged;
} list_entry_t;

struct _linked_list_s {
    list_entry_t *head;
    list_entry_t *tail;
    list_entry_t *cur;
    size_t  pos;
    size_t length;
#ifdef THREAD_SAFE
    pthread_mutex_t lock;
#endif
    free_value_callback_t free_value_cb;
    int refcnt;
    list_entry_t *slices;
};

struct _slice_s {
    linked_list_t *list;
    size_t offset;
    size_t length;
};

/********************************************************************
 * Entry-based API
 * - Internal use only
 ********************************************************************/

/* Entry creation and destruction routines */
static inline list_entry_t *create_entry();
static inline void destroy_entry(list_entry_t *entry);

/* List and list_entry_t manipulation routines */
static inline list_entry_t *pop_entry(linked_list_t *list);
static inline int push_entry(linked_list_t *list, list_entry_t *entry);
static inline int unshift_entry(linked_list_t *list, list_entry_t *entry);
static inline list_entry_t *shift_entry(linked_list_t *list);
static inline int insert_entry(linked_list_t *list, list_entry_t *entry, size_t pos);
static inline list_entry_t *pick_entry(linked_list_t *list, size_t pos);
static inline list_entry_t *fetch_entry(linked_list_t *list, size_t pos);
//list_entry_t *SelectEntry(linked_list_t *list, size_t pos);
static inline list_entry_t *remove_entry(linked_list_t *list, size_t pos);
static inline long get_entry_position(list_entry_t *entry);
static inline int move_entry(linked_list_t *list, size_t srcPos, size_t dstPos);
static inline list_entry_t *subst_entry(linked_list_t *list, size_t pos, list_entry_t *entry);
static inline int swap_entries(linked_list_t *list, size_t pos1, size_t pos2);

/*
 * Create a new linked_list_t. Allocates resources and returns
 * a linked_list_t opaque structure for later use
 */
linked_list_t *
list_create()
{
    linked_list_t *list = (linked_list_t *)calloc(1, sizeof(linked_list_t));
    if(list) {
        if (list_init(list) != 0) {
            free(list);
            return NULL;
        }
    }
    return list;
}

/*
 * Initialize a preallocated linked_list_t pointed by list
 * useful when using static list handlers
 */
int
list_init(linked_list_t *list __attribute__ ((unused)))
{
#ifdef THREAD_SAFE
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0) {
        return -1;
    }
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(&list->lock, &attr) != 0) {
        return -1;
    }
    pthread_mutexattr_destroy(&attr);
#endif
    return 0;
}

/*
 * Destroy a linked_list_t. Free resources allocated for list
 */
void
list_destroy(linked_list_t *list)
{
    if(list)
    {
        while (list->slices)
            slice_destroy(list->slices->value);
        list_clear(list);
#ifdef THREAD_SAFE
        MUTEX_DESTROY(list->lock);
#endif
        free(list);
    }
}

static void
list_destroy_tagged_value_internal(tagged_value_t *tval, void (*free_cb)(void *v))
{
    if(tval)
    {
        free(tval->tag);
        if(tval->value) {
            if(tval->type == TV_TYPE_LIST)
                list_destroy((linked_list_t *)tval->value);
            else if (free_cb)
                free_cb(tval->value);
            else if (tval->vlen)
                free(tval->value);
        }
        free(tval);
    }
}

/*
 * Clear a linked_list_t. Removes all entries in list
 * if values are associated to entries, resources for those will not be freed.
 * list_clear() can be used safely with entry-based and tagged-based api,
 * otherwise you must really know what you are doing
 */
void
list_clear(linked_list_t *list)
{
    list_entry_t *e;
    /* Destroy all entries still in list */
    while((e = shift_entry(list)) != NULL)
    {
        /* if there is a tagged_value_t associated to the entry,
        * let's free memory also for it */
        if(e->tagged && e->value)
            list_destroy_tagged_value_internal((tagged_value_t *)e->value, list->free_value_cb);
        else if (list->free_value_cb)
            list->free_value_cb(e->value);

        destroy_entry(e);
    }
}

/* Returns actual lenght of linked_list_t pointed by l */
size_t
list_count(linked_list_t *l)
{
    size_t len;
    MUTEX_LOCK(l->lock);
    len = l->length;
    MUTEX_UNLOCK(l->lock);
    return len;
}

void
list_set_free_value_callback(linked_list_t *list, free_value_callback_t free_value_cb)
{
    MUTEX_LOCK(list->lock);
    list->free_value_cb = free_value_cb;
    MUTEX_UNLOCK(list->lock);
}

void
list_lock(linked_list_t *list __attribute__ ((unused)))
{
    MUTEX_LOCK(list->lock);
}

void
list_unlock(linked_list_t *list __attribute__ ((unused)))
{
    MUTEX_UNLOCK(list->lock);
}

/*
 * Create a new list_entry_t structure. Allocates resources and returns
 * a pointer to the just created list_entry_t opaque structure
 */
static inline
list_entry_t *create_entry()
{
    list_entry_t *new_entry = (list_entry_t *)calloc(1, sizeof(list_entry_t));
    /*
    if (!new_entry) {
        fprintf(stderr, "Can't create new entry: %s", strerror(errno));
    }
    */
    return new_entry;
}

/*
 * Free resources allocated for a list_entry_t structure
 * If the entry is linked in a list this routine will also unlink correctly
 * the entry from the list.
 */
static inline void
destroy_entry(list_entry_t *entry)
{
    long pos;
    if(entry)
    {
        if(entry->list)
        {
            /* entry is linked in a list...let's remove that reference */
            pos = get_entry_position(entry);
            if(pos >= 0)
                remove_entry(entry->list, pos);
        }
        free(entry);
    }
}

/*
 * Pops a list_entry_t from the end of the list (or bottom of the stack
 * if you are using the list as a stack)
 */
static inline
list_entry_t *pop_entry(linked_list_t *list)
{
    list_entry_t *entry;
    MUTEX_LOCK(list->lock);

    entry = list->tail;
    if(entry)
    {
        list->tail = entry->prev;
        if(list->tail)
            list->tail->next = NULL;
        list->length--;

        entry->list = NULL;
        entry->prev = NULL;
        entry->next = NULL;

        if (list->cur == entry)
            list->cur = NULL;
    }
    if(list->length == 0)
        list->head = list->tail = NULL;

    MUTEX_UNLOCK(list->lock);
    return entry;
}

/*
 * Pushs a list_entry_t at the end of a list
 */
static inline int
push_entry(linked_list_t *list, list_entry_t *entry)
{
    list_entry_t *p;
    if(!entry)
        return -1;
    MUTEX_LOCK(list->lock);
    if(list->length == 0)
    {
        list->head = list->tail = entry;
    }
    else
    {
        p = list->tail;
        p->next = entry;
        entry->prev = p;
        entry->next = NULL;
        list->tail = entry;
    }
    list->length++;
    entry->list = list;
    MUTEX_UNLOCK(list->lock);
    return 0;
}

/*
 * Retreive a list_entry_t from the beginning of a list (or top of the stack
 * if you are using the list as a stack)
 */
static inline
list_entry_t *shift_entry(linked_list_t *list)
{
    list_entry_t *entry;
    MUTEX_LOCK(list->lock);
    entry = list->head;
    if(entry)
    {
        list->head = entry->next;
        if(list->head)
            list->head->prev = NULL;
        list->length--;

        entry->list = NULL;
        entry->prev = NULL;
        entry->next = NULL;

        if (list->cur == entry)
            list->cur = NULL;
        else if (list->pos)
            list->pos--;
    }
    if(list->length == 0)
        list->head = list->tail = NULL;
    MUTEX_UNLOCK(list->lock);
    return entry;
}


/*
 * Insert a list_entry_t at the beginning of a list (or at the top if the stack)
 */
static inline int
unshift_entry(linked_list_t *list, list_entry_t *entry)
{
    list_entry_t *p;
    if(!entry)
        return -1;
    MUTEX_LOCK(list->lock);
    if(list->length == 0)
    {
        list->head = list->tail = entry;
    }
    else
    {
        p = list->head;
        p->prev = entry;
        entry->prev = NULL;
        entry->next = p;
        list->head = entry;
    }
    list->length++;
    entry->list = list;
    if (list->cur)
        list->pos++;
    MUTEX_UNLOCK(list->lock);
    return 0;
}

/*
 * Instert an entry at a specified position in a linked_list_t
 */
static inline int
insert_entry(linked_list_t *list, list_entry_t *entry, size_t pos)
{
    list_entry_t *prev, *next;
    int ret = -1;
    MUTEX_LOCK(list->lock);
    if(pos == 0) {
        ret = unshift_entry(list, entry);
    } else if(pos == list->length) {
        ret = push_entry(list, entry);
    } else if (pos > list->length) {
        unsigned int i;
        for (i = list->length; i < pos; i++) {
            list_entry_t *emptyEntry = create_entry();
            if (!emptyEntry || push_entry(list, emptyEntry) != 0)
            {
                if (emptyEntry)
                    destroy_entry(emptyEntry);
                MUTEX_UNLOCK(list->lock);
                return -1;
            }
        }
        ret = push_entry(list, entry);
    }

    if (ret == 0) {
        MUTEX_UNLOCK(list->lock);
        return ret;
    }

    prev = pick_entry(list, pos-1);
    if(prev)
    {
        next = prev->next;
        prev->next = entry;
        entry->prev = prev;
        entry->next = next;
        if (next)
            next->prev = entry;
        list->length++;
        ret = 0;
    }
    MUTEX_UNLOCK(list->lock);
    return ret;
}

/*
 * Retreive the list_entry_t at pos in a linked_list_t without removing it from the list
 */
static inline
list_entry_t *pick_entry(linked_list_t *list, size_t pos)
{
    unsigned int i;
    list_entry_t *entry;

    MUTEX_LOCK(list->lock);

    if(list->length <= pos) {
        MUTEX_UNLOCK(list->lock);
        return NULL;
    }

    size_t half_length = list->length >> 1;
    /* we rely on integer underflow for the argument to abs(). */
    if (list->cur && (size_t)abs((int)(list->pos - pos)) < half_length) {
        entry = list->cur;
        if (list->pos != pos) {
            if (list->pos < pos) {
                for(i=list->pos; i < pos; i++)  {
                    entry = entry->next;
                }
            } else if (list->pos > pos) {
                for(i=list->pos; i > pos; i--)  {
                    entry = entry->prev;
                }
            }
        }
    } else {
        if (pos > half_length)
        {
            entry = list->tail;
            for(i=list->length - 1;i>pos;i--)  {
                entry = entry->prev;
            }
        }
        else
        {
            entry = list->head;
            for(i=0;i<pos;i++) {
                entry = entry->next;
            }
        }
    }
    if (entry) {
        list->pos = pos;
        list->cur = entry;
    }

    MUTEX_UNLOCK(list->lock);
    return entry;
}

/* Retreive the list_entry_t at pos in a linked_list_t removing it from the list
 * XXX - no locking here because this routine is just an accessor to other routines
 * Caller MUST destroy the returned entry trough destroy_entry() call
 */
static inline
list_entry_t *fetch_entry(linked_list_t *list, size_t pos)
{
    list_entry_t *entry = NULL;
    if(pos == 0 )
        return shift_entry(list);
    else if(pos == list_count(list) - 1)
        return pop_entry(list);

    entry = remove_entry(list, pos);
    return entry;
}

static inline int
move_entry(linked_list_t *list, size_t srcPos, size_t dstPos)
{
    list_entry_t *e;

    e = fetch_entry(list, srcPos);
    if(e)
    {
        if(insert_entry(list, e, dstPos) == 0)
            return 0;
        else
        {
            if(insert_entry(list, e, srcPos) != 0)
            {
                //fprintf(stderr, "Can't restore entry at index %lu while moving to %lu\n", srcPos, dstPos);
            }
        }
    }
    /* TODO - Unimplemented */
    return -1;
}

/* XXX - still dangerous ... */
static inline int
swap_entries(linked_list_t *list, size_t pos1, size_t pos2)
{
    list_entry_t *e1;
    list_entry_t *e2;
    if(pos2 > pos1)
    {
        e2 = fetch_entry(list, pos2);
        insert_entry(list, e2, pos1);
        e1 = fetch_entry(list,  pos1+1);
        insert_entry(list, e1, pos2);
    }
    else if(pos1 > pos2)
    {
        e1 = fetch_entry(list, pos1);
        insert_entry(list, e1, pos2);
        e2 = fetch_entry(list, pos2+1);
        insert_entry(list, e2, pos1);
    }
    else
        return -1;

    /* TODO - Unimplemented */
    return 0;
}

/* return old entry at pos */
static inline
list_entry_t *subst_entry(linked_list_t *list, size_t pos, list_entry_t *entry)
{
    list_entry_t *old;

    MUTEX_LOCK(list->lock);

    old = fetch_entry(list,  pos);
    if(!old) {
        MUTEX_UNLOCK(list->lock);
        return NULL;
    }
    insert_entry(list, entry, pos);

    MUTEX_UNLOCK(list->lock);
    /* XXX - NO CHECK ON INSERTION */
    return old;
}

/* XXX - POSSIBLE RACE CONDITION BETWEEN pick_entry and the actual removal */
static inline
list_entry_t *remove_entry(linked_list_t *list, size_t pos)
{
    list_entry_t *next, *prev;
    list_entry_t *entry = pick_entry(list, pos);
    MUTEX_LOCK(list->lock);
    if(entry)
    {
        prev = entry->prev;
        next = entry->next;
        if (pos == 0)
            list->head = next;
        else if (pos == list->length - 1)
            list->tail = prev;

        if(prev)
            prev->next = next;
        if(next)
            next->prev = prev;

        list->length--;
        entry->list = NULL;
        entry->prev = NULL;
        entry->next = NULL;

        if (list->cur == entry) {
            list->cur = NULL;
            list->pos = 0;
        } else if (list->pos > pos) {
            list->pos--;
        }
        MUTEX_UNLOCK(list->lock);
        return entry;
    }
    MUTEX_UNLOCK(list->lock);
    return NULL;
}

/* return position of entry if linked in a list.
 * Scans entire list so it can be slow for very long lists */
long
get_entry_position(list_entry_t *entry)
{
    int i = 0;
    linked_list_t *list;
    list_entry_t *p;
    list = entry->list;

    if (!list)
        return -1;

    MUTEX_LOCK(list->lock);
    if(list)
    {
        p  = list->head;
        while(p)
        {
            if(p == entry) {
                MUTEX_UNLOCK(list->lock);
                return i;
            }
            p = p->next;
            i++;
        }
    }
    MUTEX_UNLOCK(list->lock);
    return -1;
}

void *
list_pop_value(linked_list_t *list)
{
    void *val = NULL;
    list_entry_t *entry = pop_entry(list);
    if(entry)
    {
        val = entry->value;
        destroy_entry(entry);
    }
    return val;
}

int
list_push_value(linked_list_t *list, void *val)
{
    int res;
    list_entry_t *new_entry = create_entry();
    if(!new_entry)
        return -1;
    new_entry->value = val;
    res = push_entry(list, new_entry);
    if(res != 0)
        destroy_entry(new_entry);
    return res;
}

int
list_unshift_value(linked_list_t *list, void *val)
{
    int res;
    list_entry_t *new_entry = create_entry();
    if(!new_entry)
        return -1;
    new_entry->value = val;
    res = unshift_entry(list, new_entry);
    if(res != 0)
        destroy_entry(new_entry);
    return res;
}

void *
list_shift_value(linked_list_t *list)
{
    void *val = NULL;
    list_entry_t *entry = shift_entry(list);
    if(entry)
    {
        val = entry->value;
        destroy_entry(entry);
    }
    return val;
}

int
list_insert_value(linked_list_t *list, void *val, size_t pos)
{
    int res;
    list_entry_t *new_entry = create_entry();
    if(!new_entry)
        return -1;
    new_entry->value = val;
    res=insert_entry(list, new_entry, pos);
    if(res != 0)
        destroy_entry(new_entry);
    return res;
}

void *
list_pick_value(linked_list_t *list, size_t pos)
{
    list_entry_t *entry = pick_entry(list, pos);
    if(entry)
        return entry->value;
    return NULL;
}

void *
list_fetch_value(linked_list_t *list, size_t pos)
{
    void *val = NULL;
    list_entry_t *entry = fetch_entry(list, pos);
    if(entry)
    {
        val = entry->value;
        destroy_entry(entry);
    }
    return val;
}

/* just an accessor to move_entry */
int
list_move_value(linked_list_t *list, size_t srcPos, size_t dstPos)
{
    return move_entry(list, srcPos, dstPos);
}

void *
list_set_value(linked_list_t *list, size_t pos, void *newval)
{
    void *old_value = NULL;
    MUTEX_LOCK(list->lock);
    list_entry_t *entry = pick_entry(list, pos);
    if (entry) {
        old_value = entry->value;
        entry->value = newval;
    } else {
        list_insert_value(list, newval, pos);
    }
    MUTEX_UNLOCK(list->lock);
    return old_value;
}

/* return old value at pos */
void *
list_subst_value(linked_list_t *list, size_t pos, void *newval)
{
    void *old_value = NULL;
    MUTEX_LOCK(list->lock);
    list_entry_t *entry = pick_entry(list, pos);
    if (entry) {
        old_value = entry->value;
        entry->value = newval;
    }
    MUTEX_UNLOCK(list->lock);
    return old_value;
}

int
list_swap_values(linked_list_t *list,  size_t pos1, size_t pos2)
{
    return swap_entries(list, pos1, pos2);
}

int
list_foreach_value(linked_list_t *list, int (*item_handler)(void *item, size_t idx, void *user), void *user)
{
    MUTEX_LOCK(list->lock);
    slice_t slice = {
        .list = list,
        .offset = 0,
        .length = list->length
    };
    MUTEX_UNLOCK(list->lock);
    return slice_foreach_value(&slice, item_handler, user);
}

tagged_value_t *
list_create_tagged_value_nocopy(char *tag, void *val)
{
    tagged_value_t *newval = (tagged_value_t *)calloc(1, sizeof(tagged_value_t));
    if(!newval) {
        //fprintf(stderr, "Can't create new tagged value: %s", strerror(errno));
        return NULL;
    }

    if(tag)
        newval->tag = strdup(tag);
    if (val)
        newval->value = val;

    return newval;
}

/*
 * Allocates resources for a new tagged_value_t initializing both tag and value
 * to what received as argument.
 * if vlen is 0 or negative, then val is assumed to be a string and
 * strdup is used to copy it.
 * Return a pointer to the new allocated tagged_value_t.
 */
tagged_value_t *
list_create_tagged_value(char *tag, void *val, size_t vlen)
{
    tagged_value_t *newval = (tagged_value_t *)calloc(1, sizeof(tagged_value_t));
    if(!newval) {
        //fprintf(stderr, "Can't create new tagged value: %s", strerror(errno));
        return NULL;
    }

    if(tag)
        newval->tag = strdup(tag);
    if(val)
    {
        if(vlen)
        {
            newval->value = malloc(vlen+1);
            if(newval->value)
            {
                memcpy(newval->value, val, vlen);
                memset((char *)newval->value+vlen, 0, 1);
                newval->vlen = vlen;
            } else {
                //fprintf(stderr, "Can't copy value: %s", strerror(errno));
                free(newval->tag);
                free(newval);
                return NULL;
            }
            newval->type = TV_TYPE_BINARY;
        }
        else
        {
            newval->value = (void *)strdup((char *)val);
            newval->vlen = strlen((char *)val);
            newval->type = TV_TYPE_STRING;
        }
    }
    return newval;
}

/*
 * Allocates resources for a new tagged_value_t
 * containing a linked_list_t instead of a simple buffer.
 * This let us define folded linked_list_t and therefore represent
 * trees (or a sort of folded hashrefs)
 */
tagged_value_t *
list_create_tagged_sublist(char *tag, linked_list_t *sublist)
{
    tagged_value_t *newval = (tagged_value_t *)calloc(1, sizeof(tagged_value_t));
    if(!newval) {
        //fprintf(stderr, "Can't create new tagged value: %s", strerror(errno));
        return NULL;
    }

    if(tag)
        newval->tag = strdup(tag);
    newval->type = TV_TYPE_LIST;
    newval->value = sublist;
    return newval;
}

/* Release resources for tagged_value_t pointed by tval */
void
list_destroy_tagged_value(tagged_value_t *tval)
{
    list_destroy_tagged_value_internal(tval, NULL);
}

tagged_value_t *
list_set_tagged_value(linked_list_t *list, char *tag, void *value, size_t len, int copy)
{
    int i;

    tagged_value_t *tval;
    if (copy)
        tval = list_create_tagged_value(tag, value, len);
    else
        tval = list_create_tagged_value_nocopy(tag, value);

    MUTEX_LOCK(list->lock);
    for (i = 0; i < (int)list->length; i++) {
        tagged_value_t *tv = list_pick_tagged_value(list, i);
        if (tv && tv->tag && tv->tag[0] == tag[0] &&
            strcmp(tv->tag, tag) == 0)
        {
            MUTEX_UNLOCK(list->lock);
            if (!list_set_value(list, i, tval)) {
                list_destroy_tagged_value(tval);
                return NULL;
            }
            return tv;
        }
    }
    if (list_push_tagged_value(list, tval) == 0) {
        list_destroy_tagged_value(tval);
        tval = NULL;
    }
    MUTEX_UNLOCK(list->lock);
    return NULL;
}

/* Pops a tagged_value_t from the list pointed by list */
tagged_value_t *
list_pop_tagged_value(linked_list_t *list)
{
    return (tagged_value_t *)list_pop_value(list);
}

/*
 * Pushes a new tagged_value_t into list. user must give a valid tagged_value_t pointer
 * created trough a call to create_tagged_value() routine
 */
int
list_push_tagged_value(linked_list_t *list, tagged_value_t *tval)
{
    list_entry_t *new_entry;
    int res = 0;
    if(tval)
    {
        new_entry = create_entry();
        if(new_entry)
        {
            new_entry->tagged = 1;
            new_entry->value = tval;
            res = push_entry(list, new_entry);
            if(res != 0)
                destroy_entry(new_entry);
        }
    }
    return res;
}

int
list_unshift_tagged_value(linked_list_t *list, tagged_value_t *tval)
{
    int res = 0;
    list_entry_t *new_entry;
    if(tval)
    {
        new_entry = create_entry();
        if(new_entry)
         {
            new_entry->tagged = 1;
            new_entry->value = tval;
            res = unshift_entry(list, new_entry);
            if(res != 0)
                destroy_entry(new_entry);
        }
    }
    return res;
}

tagged_value_t *
shift_tagged_value(linked_list_t *list)
{
    return (tagged_value_t *)list_shift_value(list);
}

int
list_insert_tagged_value(linked_list_t *list, tagged_value_t *tval, size_t pos)
{
    int res = 0;
    list_entry_t *new_entry;
    if(tval)
    {
        new_entry = create_entry();
        if(new_entry)
        {
            new_entry->tagged = 1;
            new_entry->value = tval;
            res = insert_entry(list, new_entry, pos);
            if(res != 0)
                destroy_entry(new_entry);
        }
    }
    return res;
}

tagged_value_t *
list_pick_tagged_value(linked_list_t *list, size_t pos)
{
    return (tagged_value_t *)list_pick_value(list, pos);
}

tagged_value_t *
list_fetch_tagged_value(linked_list_t *list, size_t pos)
{
    return (tagged_value_t *)list_fetch_value(list, pos);
}

/*
 * ... without removing it from the list
 */
tagged_value_t *
list_get_tagged_value(linked_list_t *list, char *tag)
{
    int i;
    tagged_value_t *tval;
    for(i = 0;i < (int)list_count(list); i++)
    {
        tval = list_pick_tagged_value(list, i);
        if (!tval) {
            continue;
        }
        if(strcmp(tval->tag, tag) == 0)
            return tval;
    }
    return NULL;
}

/*
 * ... without removing it from the list
 * USER MUST NOT FREE MEMORY FOR RETURNED VALUES
 * User MUST create a new list, pass it as 'values'
 * and destroy it when no more needed .... entries
 * returned inside the 'values' list MUST not be freed,
 * because they reference directly the real entries inside 'list'.
 */
size_t
list_get_tagged_values(linked_list_t *list, char *tag, linked_list_t *values)
{
    int i;
    int ret;
    tagged_value_t *tval;
    ret = 0;
    for(i = 0;i < (int)list_count(list); i++)
    {
        tval = list_pick_tagged_value(list, i);
        if (!tval) {
            continue;
        }
        if(strcmp(tval->tag, tag) == 0)
        {
            list_push_value(values, tval->value);
            ret++;
        }
    }
    return ret;
}

static inline void
swap_entry_node_val(list_entry_t *p1, list_entry_t *p2)
{
    if (!p1 || !p2) return;

    void *tmp = p1->value;
    p1->value = p2->value;
    p2->value = tmp;
}

static inline void
list_quick_sort(list_entry_t *head,
               list_entry_t *tail,
               list_entry_t *pivot,
               int length,
               list_comparator_callback_t comparator)
{
    if (!head || !tail || !pivot || length < 2 || !comparator) return;

    if (length == 2) {
        if (comparator(head->value, tail->value) < 0)
            swap_entry_node_val(head, tail);
        return;
    }

    void *pvalue = pivot->value;
    list_entry_t *p1 = head, *p2 = tail;

    for (;;) {

        while(p1 && p1 != pivot && comparator(p1->value, pvalue) > 0)
            p1 = p1->next;

        while(p2 && p2 != pivot && comparator(p2->value, pvalue) < 0)
            p2 = p2->prev;

        if (p1 == p2 || !p1 || !p2)
            break;

        if (p1 == pivot) {
            // all the elements on the left of the pivot are smaller
            // so we can't just swap values anymore
            if (p2->prev)
                p2->prev->next = p2->next;
            if (p2->next)
                p2->next->prev = p2->prev;

            if (pivot->prev)
                pivot->prev->next = p2;
            else if (pivot == pivot->list->head)
                pivot->list->head = p2;

            if (p2 == pivot->list->tail)
                pivot->list->tail = p2->prev;

            list_entry_t *tmp = p2->prev;
            p2->prev = pivot->prev;
            pivot->prev = p2;
            if (p2->prev)
                p2->prev->next = p2;

            p2->next = pivot;
            if (p2->next == head)
                head = p2;
            if (p2 == tail)
                tail = tmp;
            p2 = tmp;

            if (p1 != pivot)
                p1 = p1->next;


        } else if (p2 == pivot) {
            // all the elements on the right of the pivot are bigger
            // so we can't just swap values anymore
            if (p1->prev)
                p1->prev->next = p1->next;
            if (p1->next)
                p1->next->prev = p1->prev;

            if (pivot->next)
                pivot->next->prev = p1;
            else if (pivot == pivot->list->tail)
                pivot->list->tail = p1;

            if (p1 == pivot->list->head)
                pivot->list->head = p1->next;

            list_entry_t *tmp = p1->next;
            p1->next = pivot->next;
            pivot->next = p1;
            if (p1->next)
                p1->next->prev = p1;

            p1->prev = pivot;
            if (p1->prev == tail)
                tail = p1;
            if (p1 == head)
                head = tmp;
            p1 = tmp;

            if (p2 != pivot)
                p2 = p2->prev;

        } else {
            swap_entry_node_val(p1, p2);

            if (p1 != pivot)
                p1 = p1->next;
            if (p2 != pivot)
                p2 = p2->prev;
        }

    }

    // TODO - optimize the pivot selection on the sublists
    //        (it could be done while traversing the list
    //        earlier in this function)
    int l1 = 0;
    p1 = head;
    while (p1 != pivot) {
        p1 = p1->next;
        l1++;
    }
    int l2 = length - (l1 + 1);
    int i;
    list_entry_t *pv1 = head, *pv2 = tail;
    for (i = 0; pv1 && pv1->next && i < l1/2; ++i)
        pv1 = pv1->next;
    for (i = 0; pv2 && pv2->prev && i < l2/2; ++i)
        pv2 = pv2->prev;

    // recursion here
    if (l1 > 1 && pivot->prev && head != pivot->prev)
        list_quick_sort(head, pivot->prev, pv1, l1, comparator);
    if (l2 > 1 && pivot->next && tail != pivot->next)
        list_quick_sort(pivot->next, tail, pv2, l2, comparator);
}

void
list_sort(linked_list_t *list, list_comparator_callback_t comparator)
{
    MUTEX_LOCK(list->lock);
    list_entry_t *pivot = pick_entry(list, (list->length/2) - 1);
    list_quick_sort(list->head, list->tail, pivot, list->length, comparator);
    list->cur = NULL;
    list->pos = 0;
    MUTEX_UNLOCK(list->lock);
}

slice_t *
slice_create(linked_list_t *list, size_t offset, size_t length)
{
    slice_t *slice = calloc(1, sizeof(slice_t));
    slice->list = list;
    slice->offset = offset;
    slice->length = length;
    list_entry_t *e = create_entry();
    e->value = slice;
    list_entry_t *cur = list->slices;
    if (!cur) {
        list->slices = e;
    } else {
        while (cur->next)
            cur = cur->next;
        cur->next = e;
        e->prev = cur;
    }

    return slice;
}

void
slice_destroy(slice_t *slice)
{
    linked_list_t *list = slice->list;
    list_entry_t *cur = list->slices;
    list_entry_t *prev = NULL;
    while (cur) {
        if (cur->value == slice) {
            if (prev) {
                prev->next = cur->next;
                cur->next->prev = prev;
            } else {
                list->slices = cur->next;
            }
            destroy_entry(cur);
            break;
        }
        prev = cur;
        cur = cur->next;
    }    
    free(slice);
}

int
slice_foreach_value(slice_t *slice, int (*item_handler)(void *item, size_t idx, void *user), void *user)
{
    linked_list_t *list = slice->list;
    MUTEX_LOCK(list->lock);
    size_t idx = 0;
    list_entry_t *e = pick_entry(list, slice->offset);
    while(e && idx < slice->length) {
        int rc = item_handler(e->value, idx++, user);
        if (rc == 0) {
            break;
        } else if (rc == -1 || rc == -2) {
            list_entry_t *d = e;
            e = e->next;
            if (list->head == list->tail && list->tail == d) {
                list->head = list->tail = NULL;
            } else if (d == list->head) {
                list->head = d->next;
                list->head->prev = NULL;
            } else if (d == list->tail) {
                list->tail = d->prev;
                list->tail->next = NULL;
            } else {
                e->prev = d->prev;
                e->prev->next = e;
            }
            d->list = NULL;
            if (list->cur == d)
                list->cur = NULL;
            list->length--;
            slice->length--;
            // the callback got the value and will take care of releasing it
            destroy_entry(d);
            if (rc == -2) // -2 means : remove and stop the iteration
                break;
            // -1 instead means that we still want to remove the item
            // but we also want to go ahead with the iteration
        } else {
            e = e->next;
        }
    }
    MUTEX_UNLOCK(list->lock);
    return idx;
}

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
