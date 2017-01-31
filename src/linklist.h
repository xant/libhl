/** 
 * @file linklist.h
 * @author Andrea Guzzo
 * @date 22/09/2013
 * @brief Fast thread-safe linklist implementation
 * @note   In case of failures reported from the pthread interface
 *         abort() will be called. Callers can catch SIGABRT if more
 *         actions need to be taken.
 */
#ifndef HL_LINKLIST_H
#define HL_LINKLIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#ifdef WIN32
#ifdef THREAD_SAFE
#include <w32_pthread.h>
#endif
#endif
#include <string.h> // for memset

/**
 * @brief Callback that, if provided, will be called to release the value resources
 *        when an item is being removed from the list
 */
typedef void (*free_value_callback_t)(void *v);

typedef int (*list_comparator_callback_t)(void *v1, void *v2);

/**
 * @brief Opaque structure representing the actual linked list descriptor
 */
typedef struct _linked_list_s linked_list_t;


/********************************************************************
 * Common API 
 ********************************************************************/

/* List creation and destruction routines */

/**
 * @brief Create a new list
 * @return a newly allocated and initialized list
 */
linked_list_t *list_create();

/**
 * @brief Initialize a pre-allocated list
 *
 * This function can be used to initialize a statically defined list
 * @return 0 on success; -1 otherwise
 */
int list_init(linked_list_t *list);

/**
 * @brief Release all resources related to the list
 * @param list : A valid pointer to a linked_list_t structure
 */
void list_destroy(linked_list_t *list);

/**
 * @brief remove all items from the list
 * @param list : A valid pointer to a linked_list_t structure
 */
void list_clear(linked_list_t *list);

/**
 * @brief Return the total count of items in the list
 * @param list : A valid pointer to a linked_list_t structure
 * @return the actual number of items stored in the list
 */
size_t list_count(linked_list_t *list);

/**
 * @brief Set the callback which must be called to release values stored in the list
 * @param list : A valid pointer to a linked_list_t structure
 * @param free_value_cb : an free_value_callback_t function
 */
void list_set_free_value_callback(linked_list_t *list, free_value_callback_t free_value_cb);

/**
 * @brief Lock the list
 * @param list : A valid pointer to a linked_list_t structure
 */
void list_lock(linked_list_t *list);

/**
 * @brief Unlock the list
 * @param list : A valid pointer to a linked_list_t structure
 */
void list_unlock(linked_list_t *list);

/********************************************************************
 * Value-based API 
 ********************************************************************/


/* List access routines */

/**
 * @brief Remove last value from the list
 * @param list : A valid pointer to a linked_list_t structure
 * @return The value previous tail of the list
 */
void *list_pop_value(linked_list_t *list);

/**
 * @brief Append a new value to the list (tail)
 * @param list : A valid pointer to a linked_list_t structure
 * @param val : The value to store in the tail of the list
 * @return : 0 if success, -1 otherwise
 */
int list_push_value(linked_list_t *list, void *val);

/**
 * @brief Insert a new value at the beginning of the least (head)
 * @param list : A valid pointer to a linked_list_t structure
 * @param val : The value to store in the head of the list
 * @return : 0 if success, -1 otherwise
 */
int list_unshift_value(linked_list_t *list, void *val);

/**
 * @brief Remove the first value from the list
 * @param list : A valid pointer to a linked_list_t structure
 * @return The previous value stored in the tail of the list
 */

void *list_shift_value(linked_list_t *list);

/**
 * @brief Insert a value at a specific position
 * @param list : A valid pointer to a linked_list_t structure
 * @param val : The value to store at pos
 * @param pos : The position (offset) where to store the value
 * @return 0 if success, -1 otherwise
 *
 * If the list is shorter than pos-1 empty values will be inserted up to
 * that position before inserting the new one
 */
int list_insert_value(linked_list_t *list, void *val, size_t pos);


/**
 * @brief Set the value at a specific position
 * @param list : A valid pointer to a linked_list_t structure
 * @param pos : The position (offset) where to store the value
 * @param val : The value to store at pos
 *
 * This function will replace the value at pos if present or insert it if missing
 * filling in the gaps with NULL values if the length of the list is shorter than pos
 */
void *list_set_value(linked_list_t *list, size_t pos, void *val);

/**
 * @brief Replace the value stored at a specific position with a new value
 * @param list : A valid pointer to a linked_list_t structure
 * @param pos : The position of the value we want to replace
 * @param val : The new value
 */
void *list_subst_value(linked_list_t *list, size_t pos, void *val);


/**
 * @brief Pick the value at a specific position
 * @param list : A valid pointer to a linked_list_t structure
 * @param pos : The position (offset) of the requested value
 * @return : The value stored at pos if any, NULL otherwise
 *
 * Note this is a read-only access and the value will not be removed from the list
 */
void *list_pick_value(linked_list_t *list, size_t pos);

/**
 * @brief Fetch (aka: Pick and Remove) the value at a specific position
 * @param list : A valid pointer to a linked_list_t structure
 * @param pos : The position (offset) of the requested value
 * @return : The value stored at pos if any, NULL otherwise
 *
 * Note this is a read-write access and the value will be removed from the list before returning it.
 * The value will not be released so the free_value_callback won't be called in this case
 */
void *list_fetch_value(linked_list_t *list, size_t pos);

/**
 * @brief Move an existing value to a new position
 * @param list : A valid pointer to a linked_list_t structure
 * @param srcPos : The actual position of the value we want to move
 * @param dstPos : The new position where to move the value to
 * @return : 0 if success, -1 otherwise
 */ 
int list_move_value(linked_list_t *list, size_t srcPos, size_t dstPos);

/**
 * @brief Swap two values
 * @param list : A valid pointer to a linked_list_t structure
 * @param pos1 : The position of the first value to swap with a second one
 * @param pos2 : The position of the second value to swap with the first
 * @return 0 if success, -1 otherwise
 */
int list_swap_values(linked_list_t *list, size_t pos1, size_t pos2);


/**
 * @brief Callback for the value iterator
 * @return 1 to go ahead with the iteration,
 *         0 to stop the iteration,
 *        -1 to remove the current item from the list and go ahead with the iteration
 *        -2 to remove the current item from the list and stop the iteration
 */
typedef int (*item_handler_t)(void *item, size_t idx, void *user);

/* list iterator. This iterator can be used for both Tag-based and Value-based lists.
 * If tagged, items can simply be casted to a tagged_value_t pointer.
 * @return The number of items visited during the iteration
 */
int list_foreach_value(linked_list_t *list, item_handler_t item_handler, void *user);

/********************************************************************
 * Tag-based API 
 ********************************************************************/

/**
 * @brief Tagged Value
 *
 * This structure represent a tagged_value_t and is the main datatype 
 * you will have to handle when workin with the tagged-based api. 
 * If user extract such structure from the list (removing it from the list)
 * then he MUST release its resources trough a call to destroy_tagged_value
 * when finished using it.
 * If a new tagged_value must be created and inserted in a list, then 
 * list_create_tagged_value() should be used to allocate resources and obtain 
 * a pointer to a tagged_value_t structure.
 */ 
typedef struct _tagged_value_s {
    char *tag;
    void *value;
    size_t vlen;
    char type;
#define TV_TYPE_STRING 0
#define TV_TYPE_BINARY 1
#define TV_TYPE_LIST   2
} tagged_value_t;


/* List creation and destruction routines */

/* Tagged List access routines (same of previous but with tag support */
/**
 * @brief Allocate resources for a new tagged value
 * @param tag : The tag
 * @param val : The value
 * @param len : The size of the value
 * @return a newly created tagged value with the provided tag and value
 *
 * Both the tag and the value will be copied. len will be the size used by the copy
 */
tagged_value_t *list_create_tagged_value(char *tag, void *val, size_t len);

/**
 * @brief Allocate resources for a new tagged value without copying the value
 * @param tag : The tag
 * @param val : The value
 * @return A newly created tagged value with the provided tag and value
 *
 * Only the tag will be copied, the value will just point 
 * to the provided value without it being copied 
 */
tagged_value_t *list_create_tagged_value_nocopy(char *tag, void *val);

/**
 * @brief Create a tagged value where the value is a linked_list_t
 * @param tag : The tag
 * @param list: The list used as value
 * @return A newly created tagged value with type TV_TYPE_LIST
 *
 * This function is just an accessor to set the tagged_value->type properly
 * when using it to store a list
 */
tagged_value_t *list_create_tagged_sublist(char *tag, linked_list_t *list);

/**
 * @brief Release resources used by the tagged value tval
 * @param tval : The tagged value to release
 */
void list_destroy_tagged_value(tagged_value_t *tval);

/**
 * @brief Same as pop_value but expect the value to be a pointer to a tagged_value_t structure
 * @param list : A valid pointer to a linked_list_t structure holding tagged values
 * @return The tagged value stored at the end of the list
 */
tagged_value_t *list_pop_tagged_value(linked_list_t *list);

/**
 * @brief Same as push_value but when using the list to store tagged values
 * @param list : A valid pointer to a linked_list_t structure holding tagged values
 * @param tval: The new tagged value to store
 * @return 0 if success, -1 otherwise
 */
int list_push_tagged_value(linked_list_t *list, tagged_value_t *tval);

/**
 * @brief Same as unshift_value but when using the list to store tagged values
 * @param list : A valid pointer to a linked_list_t structure holding tagged values
 * @param tval: The new tagged value to store
 * @return 0 if success, -1 otherwise
 */
int list_unshift_tagged_value(linked_list_t *list, tagged_value_t *tval);

/**
 * @brief Same as shift_value but when using the list to store tagged values
 * @param list : A valid pointer to a linked_list_t structure holding tagged values
 * @return The tagged value stored in the head of the list, NULL if the list is empty
 */
tagged_value_t *list_shift_tagged_value(linked_list_t *list);

/**
 * @brief Same as insert_value but when using the list to store tagged values
 * @param list : A valid pointer to a linked_list_t structure holding tagged values
 * @param tval: The new tagged value to store
 * @param pos: The position (index) where to store the new tagged value
 * @return 0 if success, -1 otherwise
 */
int list_insert_tagged_value(linked_list_t *list, tagged_value_t *tval, size_t pos);

/**
 * @brief Same as pick_value but when using the list to store tagged values
 * @param list : A valid pointer to a linked_list_t structure holding tagged values
 * @param pos : The position (offset) of the requested tagged value
 * @return : The tagged value stored at pos if any, NULL otherwise
 *
 * Note this is a read-only access and the tagged value will not be removed from the list
 */
tagged_value_t *list_pick_tagged_value(linked_list_t *list, size_t pos);

/**
 * @brief Same as fetch_value but when using the list to store tagged values
 * @param list : A valid pointer to a linked_list_t structure holding tagged values
 * @param pos : The position (offset) of the requested tagged value
 * @return : The tagged value stored at pos if any, NULL otherwise
 *
 * Note this is a read-write access and the tagged value will be removed from 
 * the list before returning it.
 * The tagged value will not be released
 */
tagged_value_t *list_fetch_tagged_value(linked_list_t *list, size_t pos);

/**
 * @brief Get a tagged value from the list by using its tag instead of the position
 * @param list : A valid pointer to a linked_list_t structure holding tagged values
 * @param tag  : The tag of the value we are looking for
 * @return The first tagged value in the list whose tag matches the provided tag
 *
 * Note this is a read-only access and the tagged value will not be removed from the list
 */
tagged_value_t *list_get_tagged_value(linked_list_t *list, char *tag);

/**
 * @brief Set a new tagged value in the list. If the list already
 *        contains values with the same tag, the first occurrence will be replaced with the new value
 *        (but still at the same index in the list)
 * @param list: The list used as value
 * @param tval: The new tagged value to insert to the list 
 * @return The previous tagged_value_t matching the given tag if any; NULL otherwise
 * @note If a tagged value with the same tag is already contained in the list, 
 *       this function will replace the old tagged_value_t structure with the
 *       new one preserving the position in the list.\n
 *       If no matching tagged_value_t structure is found, then the new one
 *       is added to the end of the list
 */
tagged_value_t *list_set_tagged_value(linked_list_t *list, char *tag, void *value, size_t len, int copy);


/**
 * @brief Get all value pointers for all tagged values matching a specific tag
 * @param list : A valid pointer to a linked_list_t structure holding tagged values
 * @param tag  : The tag of the values we are looking for
 * @param values : a valid pointer to a linked_list_t structure where to put the 
 *               value pointers held by the tagged_value_t items matching the provided tag
 * @return The number of tagged values matching the tag and added to the values linked list
 *
 * Note The caller MUST NOT release resources for the returned values
 * (since still pointed by the tagged_value_t still in list)
 */
size_t list_get_tagged_values(linked_list_t *list, char *tag, linked_list_t *values);

/**
 * @brief Sort the content of the list using an in-place quicksort algorithm and a
 *        provided callback able to compare the value stored in the list
 * @param list : A valid pointer to a linked_list_t structure holding tagged values
 * @param comparator : A valid list_comparator_callback_t callback able to compare the
 *                     actual value stored in the list
 */
void list_sort(linked_list_t *list, list_comparator_callback_t comparator);


/********************************************************************
 * Slice API 
 ********************************************************************/

typedef struct  _slice_s slice_t;

slice_t *slice_create(linked_list_t *list, size_t offset, size_t length);

void slice_destroy(slice_t *slice);

int slice_foreach_value(slice_t *slice, item_handler_t item_handler, void *user);

#ifdef __cplusplus
}
#endif

#endif

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
