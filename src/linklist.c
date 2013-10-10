/* linked list management library - by xant 
 * "$Id: d2f36a6935cf8562e772b118a6d2959bfe2ca668 $" 
 */
 
#include <stdio.h>
#ifdef THREAD_SAFE
#include <pthread.h>
#endif
#include <log.h>
#include "linklist.h"
#include <string.h>
#include <errno.h>

typedef struct __list_entry {
    struct __linked_list *list;
    struct __list_entry *prev;
    struct __list_entry *next;
    void *value;
    int tagged;
} list_entry_t;

struct __linked_list {
    list_entry_t *head;
    list_entry_t *tail;
    list_entry_t *cur;
    unsigned long pos;
    unsigned long length;
#ifdef THREAD_SAFE
    pthread_mutex_t lock;
#endif
    int free;
    free_value_callback_t free_value_cb;
};

/********************************************************************
 * Entry-based API   
 * - Internal use only
 ********************************************************************/

/* Entry creation and destruction routines */
static inline list_entry_t *create_entry();
static inline void destroy_entry(list_entry_t *entry);
static inline void *get_entry_value(list_entry_t *entry);
static inline void set_entry_value(list_entry_t *entry, void *val);

/* List and list_entry_t manipulation routines */
static inline list_entry_t *pop_entry(linked_list_t *list);
static inline int push_entry(linked_list_t *list, list_entry_t *entry);
static inline int unshift_entry(linked_list_t *list, list_entry_t *entry);
static inline list_entry_t *shift_entry(linked_list_t *list);
static inline int insert_entry(linked_list_t *list, list_entry_t *entry, unsigned long pos);
static inline list_entry_t *pick_entry(linked_list_t *list, unsigned long pos);
static inline list_entry_t *fetch_entry(linked_list_t *list, unsigned long pos);
//list_entry_t *SelectEntry(linked_list_t *list, unsigned long pos);
static inline list_entry_t *remove_entry(linked_list_t *list, unsigned long pos);
static inline long get_entry_position(list_entry_t *entry);
static inline int move_entry(linked_list_t *list, unsigned long srcPos, unsigned long dstPos);
static inline list_entry_t *subst_entry(linked_list_t *list, unsigned long pos, list_entry_t *entry);
static inline int swap_entries(linked_list_t *list, unsigned long pos1, unsigned long pos2);

#ifdef THREAD_SAFE
#define MUTEX_LOCK(__mutex) pthread_mutex_lock(__mutex) 
#define MUTEX_UNLOCK(__mutex) pthread_mutex_unlock(__mutex) 
#else
#define MUTEX_LOCK(__mutex)
#define MUTEX_UNLOCK(__mutex)
#endif

/* 
 * Create a new linked_list_t. Allocates resources and returns 
 * a linked_list_t opaque structure for later use 
 */
linked_list_t *create_list() 
{
    linked_list_t *list = (linked_list_t *)malloc(sizeof(linked_list_t));
    if(list) {
        DEBUG5("Created new linked_list_t at address 0x%p \n", list);
        init_list(list);
        list->free = 1;
    } else {
        DIE("Can't create new linklist: %s", strerror(errno));
    }
    return list;
}

/*
 * Initialize a preallocated linked_list_t pointed by list 
 * useful when using static list handlers
 */ 
void init_list(linked_list_t *list) 
{
    memset(list,  0, sizeof(linked_list_t));
#ifdef THREAD_SAFE
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&list->lock, &attr);
    pthread_mutexattr_destroy(&attr);
#endif

    DEBUG5("Initialized linked_list_t at address 0x%p \n", list);
}

/*
 * Destroy a linked_list_t. Free resources allocated for list
 */
void destroy_list(linked_list_t *list) 
{
    if(list) 
    {
        clear_list(list);
#ifdef THREAD_SAFE
        pthread_mutex_destroy(&list->lock);
#endif
        if(list->free) free(list);
    }
    DEBUG5("Destroyed linked_list_t at address 0x%p \n", list);
}

/*
 * Clear a linked_list_t. Removes all entries in list
 * Dangerous if used with value-based api ... 
 * if values are associated to entries, resources for those will not be freed.
 * clear_list() can be used safely with entry-based and tagged-based api,
 * otherwise you must really know what you are doing
 */
void clear_list(linked_list_t *list) 
{
    list_entry_t *e;
    /* Destroy all entries still in list */
    while((e = shift_entry(list)) != NULL)
    {
        /* if there is a tagged_value_t associated to the entry, 
        * let's free memory also for it */
        if(e->tagged && e->value)
            destroy_tagged_value((tagged_value_t *)e->value);
        else if (list->free_value_cb)
            list->free_value_cb(e->value);
        
        destroy_entry(e);
    }
}

/* Returns actual lenght of linked_list_t pointed by l */
unsigned long list_count(linked_list_t *l) 
{
    unsigned long len;
    MUTEX_LOCK(&l->lock);
    len = l->length;
    MUTEX_UNLOCK(&l->lock);
    return len;
}

void set_free_value_callback(linked_list_t *list, free_value_callback_t free_value_cb) {
    MUTEX_LOCK(&list->lock);
    list->free_value_cb = free_value_cb;
    MUTEX_UNLOCK(&list->lock);
}

void list_lock(linked_list_t *list) {
    MUTEX_LOCK(&list->lock);
}

void list_unlock(linked_list_t *list) {
    MUTEX_UNLOCK(&list->lock);
}

/* 
 * Create a new list_entry_t structure. Allocates resources and returns  
 * a pointer to the just created list_entry_t opaque structure
 */
static inline list_entry_t *create_entry() 
{
    list_entry_t *new_entry = (list_entry_t *)calloc(1, sizeof(list_entry_t));
    if (!new_entry)
        DIE("Can't create new entry: %s", strerror(errno));
    DEBUG5("Created Entry at address 0x%p \n", new_entry);
    return new_entry;
}

/* 
 * Free resources allocated for a list_entry_t structure. 
 * If the entry is linked in a list this routine will also unlink correctly
 * the entry from the list.
 */
static inline void destroy_entry(list_entry_t *entry) 
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
        DEBUG5("Destroyed Entry at address 0x%p \n", entry);
        free(entry);
    }
}

/*
 * Get a pointer to the value associated to entry. 
 */
void *get_entry_value(list_entry_t *entry) 
{
    return entry->value;
}

/* 
 * Associate a value to the list_entry_t pointed by entry.
 * 
 */
void set_entry_value(list_entry_t *entry, void *val) 
{
    entry->value = val;
}

/*
 * Pops a list_entry_t from the end of the list (or bottom of the stack
 * if you are using the list as a stack)
 */
static inline list_entry_t *pop_entry(linked_list_t *list) 
{
    list_entry_t *entry;
    MUTEX_LOCK(&list->lock);

    entry = list->tail;
    if(entry) 
    {
        list->tail = entry->prev;
        if(list->tail)
            list->tail->next = NULL;
        list->length--;
        entry->list = NULL;
        if (list->cur == entry)
            list->cur = NULL;
    }
    if(list->length == 0)
        list->head = list->tail = NULL;

    MUTEX_UNLOCK(&list->lock);
    return entry;
}

/*
 * Pushs a list_entry_t at the end of a list
 */
static inline int push_entry(linked_list_t *list, list_entry_t *entry) 
{
    list_entry_t *p;
    if(!entry)
        return -1;
    MUTEX_LOCK(&list->lock);
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
    MUTEX_UNLOCK(&list->lock);
    return 0;
}
 
/*
 * Retreive a list_entry_t from the beginning of a list (or top of the stack
 * if you are using the list as a stack) 
 */
static inline list_entry_t *shift_entry(linked_list_t *list) 
{
    list_entry_t *entry;
    MUTEX_LOCK(&list->lock);
    entry = list->head;
    if(entry) 
    {
        list->head = entry->next;
        if(list->head) 
            list->head->prev = NULL;
        list->length--;
        entry->list = NULL;
        if (list->cur == entry)
            list->cur = NULL;
    }
    if(list->length == 0)
        list->head = list->tail = NULL;
    MUTEX_UNLOCK(&list->lock);
    return entry;
}


/* 
 * Insert a list_entry_t at the beginning of a list (or at the top if the stack)
 */
static inline int unshift_entry(linked_list_t *list, list_entry_t *entry) 
{
    list_entry_t *p;
    if(!entry)
        return -1;
    MUTEX_LOCK(&list->lock);
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
    MUTEX_UNLOCK(&list->lock);
    return 0;
}

/*
 * Instert an entry at a specified position in a linked_list_t
 */
static inline int insert_entry(linked_list_t *list, list_entry_t *entry, unsigned long pos) 
{
    list_entry_t *prev, *next;
    if(pos == 0)
        return unshift_entry(list, entry);
    else if(pos == list->length)
        return push_entry(list, entry);
    else if (pos > list->length) {
        unsigned int i;
        for (i = list->length; i < pos; i++) {
            list_entry_t *emptyEntry = create_entry();
            push_entry(list, emptyEntry);
        }
        push_entry(list, entry);
    }
    prev = pick_entry(list, pos-1);
    MUTEX_LOCK(&list->lock);
    if(prev) 
    {
        next = prev->next;
        prev->next = entry;
        entry->prev = prev;
        entry->next = next;
        next->prev = entry;
        list->length++;
        MUTEX_UNLOCK(&list->lock);
        return 0;
    }
    MUTEX_UNLOCK(&list->lock);
    return -1;
}

/* 
 * Retreive the list_entry_t at pos in a linked_list_t without removing it from the list
 */
static inline list_entry_t *pick_entry(linked_list_t *list, unsigned long pos) 
{
    unsigned int i;
    list_entry_t *entry;
    if(list->length <= pos)
        return NULL;
    MUTEX_LOCK(&list->lock);
    if (list->cur) {
        entry = list->cur;
        if (list->pos != pos) {
            if (list->pos < pos) {
                for(i=list->pos; i < pos; i++) 
                    entry = entry->next;
            } else if (list->pos > pos) {
                for(i=list->pos; i > pos; i--) 
                    entry = entry->prev;
            }
        }
    } else {
        if (pos > list->length/2) 
        {
            entry = list->tail;
            for(i=list->length - 1;i>pos;i--) 
                entry = entry->prev;
        }
        else 
        {
            entry = list->head;
            for(i=0;i<pos;i++) 
                entry = entry->next;
        }
    }
    if (list->pos != pos) {
        list->pos = pos;
        list->cur = entry;
    }
    MUTEX_UNLOCK(&list->lock);
    return entry;
}

/* Retreive the list_entry_t at pos in a linked_list_t removing it from the list 
 * XXX - no locking here because this routine is just an accessor to other routines
 * XXX - POSSIBLE RACE CONDITION BETWEEN pick_entry and remove_entry
 * Caller MUST destroy returned entry trough destroy_entry() call
 */
static inline list_entry_t *fetch_entry(linked_list_t *list, unsigned long pos) 
{
    list_entry_t *entry = NULL;
    if(pos == 0 )
        return shift_entry(list);
    else if(pos == list->length - 1)
        return pop_entry(list);

    entry = remove_entry(list, pos); 
    return entry;
}

/*
list_entry_t *SelectEntry(linked_list_t *list, unsigned long pos) 
{
}
*/

static inline int move_entry(linked_list_t *list, unsigned long srcPos, unsigned long dstPos) 
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
                WARN("Can't restore entry at index %lu while moving to %lu\n", srcPos, dstPos);
            }
        }
    }
    /* TODO - Unimplemented */
    return -1;
}

/* XXX - still dangerous ... */
static inline int swap_entries(linked_list_t *list, unsigned long pos1, unsigned long pos2) 
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
static inline list_entry_t *subst_entry(linked_list_t *list, unsigned long pos, list_entry_t *entry)
{
    list_entry_t *old;
    old = fetch_entry(list,  pos);
    if(!old)
        return NULL;
    insert_entry(list, entry, pos);
    if (list->cur == old)
        list->cur = entry;
    /* XXX - NO CHECK ON INSERTION */
    return old;
}

static inline list_entry_t *remove_entry(linked_list_t *list, unsigned long pos) 
{
    list_entry_t *next, *prev;
    list_entry_t *entry = pick_entry(list, pos);
    MUTEX_LOCK(&list->lock);
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
        if (list->cur == entry) {
            list->cur = NULL;
            list->pos = 0;
        }
        MUTEX_UNLOCK(&list->lock);
        return entry;
    }
    MUTEX_UNLOCK(&list->lock);
    return NULL;
}

/* return position of entry if linked in a list.
 * Scans entire list so it can be slow for very long lists */
long get_entry_position(list_entry_t *entry) 
{
    int i = 0;
    linked_list_t *list;
    list_entry_t *p;
    list = entry->list;
    if(list) 
    {
        p  = list->head;
        while(p) 
        {
            if(p == entry) return i;
            p = p->next;
            i++;
        }
    }
    return -1;
}

void *pop_value(linked_list_t *list) 
{
    void *val = NULL;
    list_entry_t *entry = pop_entry(list);
    if(entry) 
    {
        val = get_entry_value(entry);
        destroy_entry(entry);
    }
    return val;
}

int push_value(linked_list_t *list, void *val) 
{
    int res;
    list_entry_t *new_entry = create_entry();
    if(!new_entry)
        return -1;
    set_entry_value(new_entry, val);
    res = push_entry(list, new_entry);
    if(res != 0)
        destroy_entry(new_entry);
    return res;
}

int unshift_value(linked_list_t *list, void *val) 
{
    int res;
    list_entry_t *new_entry = create_entry();
    if(!new_entry)
        return -1;
    set_entry_value(new_entry, val);
    res = unshift_entry(list, new_entry);
    if(res != 0)
        destroy_entry(new_entry);
    return res;
}

void *shift_value(linked_list_t *list) 
{
    void *val = NULL;
    list_entry_t *entry = shift_entry(list);
    if(entry) 
    {
        val = get_entry_value(entry);
        destroy_entry(entry);
    }
    return val;
}

int insert_value(linked_list_t *list, void *val, unsigned long pos) 
{
    int res;
    list_entry_t *new_entry = create_entry();
    if(!new_entry)
        return -1;
    set_entry_value(new_entry, val);
    res=insert_entry(list, new_entry, pos);
    if(res != 0)
        destroy_entry(new_entry);
    return res;
}

void *pick_value(linked_list_t *list, unsigned long pos) 
{
    list_entry_t *entry = pick_entry(list, pos);
    if(entry)
        return get_entry_value(entry);
    return NULL;
}

void *fetch_value(linked_list_t *list, unsigned long pos) 
{
    void *val = NULL;
    list_entry_t *entry = fetch_entry(list, pos);
    if(entry) 
    {
        val = get_entry_value(entry);
        destroy_entry(entry);
    }
    return val;
}

/* just an accessor to move_entry */
int move_value(linked_list_t *list, unsigned long srcPos, unsigned long dstPos)
{
    return move_entry(list, srcPos, dstPos);
}

/* return old value at pos */
void *subst_value(linked_list_t *list, unsigned long pos, void *newval)
{
    list_entry_t *new_entry;
    list_entry_t *old_entry;
    void *oldVal;
    new_entry = create_entry();
    if(new_entry)
    {
        set_entry_value(new_entry, newval);
        old_entry = subst_entry(list, pos, new_entry);
        if(old_entry)
        {
            oldVal = get_entry_value(old_entry);
            destroy_entry(old_entry);
            return oldVal;
        }
    }
    return NULL;
}

int swap_values(linked_list_t *list,  unsigned long pos1, unsigned long pos2)
{
    return swap_entries(list, pos1, pos2);
}

void foreach_list_value(linked_list_t *list, int (*item_handler)(void *item, unsigned long idx, void *user), void *user)
{
    unsigned long i;
    /* TODO - maybe should lock list while iterating? */
    MUTEX_LOCK(&list->lock);
    for(i=0;i<list_count(list);i++) {
        if (item_handler(pick_value(list, i), i, user) == 0)
            break;
    }
    MUTEX_UNLOCK(&list->lock);
}

tagged_value_t *create_tagged_value_nocopy(char *tag, void *val) 
{
    tagged_value_t *newval = (tagged_value_t *)calloc(1, sizeof(tagged_value_t));
    if(!newval)
        DIE("Can't create new tagged value: %s", strerror(errno));
    if(tag)
        newval->tag = strdup(tag);
    if (val)
        newval->value = val;
    DEBUG5("Created tagged_value_t (nocopy) at address 0x%p \n", newval);
    return newval;
}

/* 
 * Allocates resources for a new tagged_value_t initializing both tag and value
 * to what received as argument.
 * if vlen is 0 or negative, then val is assumed to be a string and 
 * strdup is used to copy it.
 * Return a pointer to the new allocated tagged_value_t.
 */
tagged_value_t *create_tagged_value(char *tag, void *val, unsigned long vlen) 
{
    tagged_value_t *newval = (tagged_value_t *)calloc(1, sizeof(tagged_value_t));
    if(!newval)
        DIE("Can't create new tagged value: %s", strerror(errno));

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
                DIE("Can't copy value: %s", strerror(errno));
            }
            newval->type = TV_TYPE_BINARY;
        } 
        else 
        {
            newval->value = (void *)strdup((char *)val);
            newval->vlen = (unsigned long)strlen((char *)val);
            newval->type = TV_TYPE_STRING;
        }
    }
    DEBUG5("Created tagged_value_t at address 0x%p \n", newval);
    return newval;
}

/* 
 * Allocates resources for a new tagged_value_t 
 * containing a linked_list_t instead of a simple buffer.
 * This let us define folded linked_list_t and therefore represent
 * trees (or a sort of folded hashrefs)
 */
tagged_value_t *create_tagged_sublist(char *tag, linked_list_t *sublist)  
{
    tagged_value_t *newval = (tagged_value_t *)calloc(1, sizeof(tagged_value_t));
    if(!newval)
        DIE("Can't create new tagged value: %s", strerror(errno));
    if(tag)
        newval->tag = strdup(tag);
    newval->type = TV_TYPE_LIST;
    newval->value = sublist;
    return newval;
}

/* Release resources for tagged_value_t pointed by tval */
void destroy_tagged_value(tagged_value_t *tval) 
{
    if(tval) 
    {
        if(tval->tag)
            free(tval->tag);
        if(tval->value) {
            if(tval->type == TV_TYPE_LIST) 
                destroy_list((linked_list_t *)tval->value);
            else 
                free(tval->value);
        }
        free(tval);
    }
    DEBUG5("Destroyed tagged_value_t at address 0x%p \n", tval);
}

/* Pops a tagged_value_t from the list pointed by list */
tagged_value_t *pop_tagged_value(linked_list_t *list) 
{
    return (tagged_value_t *)pop_value(list);
}

/* 
 * Pushes a new tagged_value_t into list. user must give a valid tagged_value_t pointer 
 * created trough a call to create_tagged_value() routine 
 */
int push_tagged_value(linked_list_t *list, tagged_value_t *tval) 
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

int unshift_tagged_value(linked_list_t *list, tagged_value_t *tval) 
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
 
tagged_value_t *shift_tagged_value(linked_list_t *list) 
{
    return (tagged_value_t *)shift_value(list);
}

int insert_tagged_value(linked_list_t *list, tagged_value_t *tval, unsigned long pos) 
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

tagged_value_t *pick_tagged_value(linked_list_t *list, unsigned long pos) 
{
    return (tagged_value_t *)pick_value(list, pos);
}

tagged_value_t *fetch_tagged_value(linked_list_t *list, unsigned long pos) 
{
    return (tagged_value_t *)fetch_value(list, pos);
}

/* 
 * ... without removing it from the list
 */
tagged_value_t *get_tagged_value(linked_list_t *list, char *tag) 
{
    int i;
    tagged_value_t *tval;
    for(i = 0;i < (int)list_count(list); i++)
    {
        tval = pick_tagged_value(list, i);
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
unsigned long get_tagged_values(linked_list_t *list, char *tag, linked_list_t *values) 
{
    int i;
    int ret;
    tagged_value_t *tval;
    ret = 0;
    for(i = 0;i < (int)list_count(list); i++)
    {
        tval = pick_tagged_value(list, i);
        if(strcmp(tval->tag, tag) == 0)
        {
            push_value(values, tval->value);
            ret++;
        }
    }
    return ret;
}

