/** 
 * @file linklist.h
 * @author Andrea Guzzo
 * @date 22/09/2013
 * @brief Fast thread-safe linklist implementation
 */
#ifndef __LINKLIST_H__
#define __LINKLIST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
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

/**
 * @brief Opaque structure representing the actual linked list descriptor
 */
typedef struct __linked_list linked_list_t;


/********************************************************************
 * Common API 
 ********************************************************************/

/* List creation and destruction routines */

/**
 * @brief Create a new list
 * @return a newly allocated and initialized list
 */
linked_list_t *create_list();

/**
 * @brief Initialize a pre-allocated list
 *
 * This function can be used to initialize a statically defined list
 */
void init_list(linked_list_t *list);

/**
 * @brief Release all resources related to the list
 * @arg list : A valid pointer to a linked_list_t structure
 */
void destroy_list(linked_list_t *list);

/**
 * @brief remove all items from the list
 * @arg list : A valid pointer to a linked_list_t structure
 */
void clear_list(linked_list_t *list);

/**
 * @brief Return the total count of items in the list
 * @arg list : A valid pointer to a linked_list_t structure
 * @return the actual number of items stored in the list
 */
unsigned long list_count(linked_list_t *list);

/**
 * @brief Set the callback which must be called to release values stored in the list
 * @arg list : A valid pointer to a linked_list_t structure
 * @arg cb : an free_value_callback_t function
 */
void set_free_value_callback(linked_list_t *list, free_value_callback_t free_value_cb);

/**
 * @brief Lock the list
 * @arg list : A valid pointer to a linked_list_t structure
 */
void list_lock(linked_list_t *list);

/**
 * @brief Lock the list
 * @arg list : A valid pointer to a linked_list_t structure
 */
void list_unlock(linked_list_t *list);

/********************************************************************
 * Value-based API 
 ********************************************************************/


/* List access routines */

/**
 * @brief Remove last value from the list
 * @arg list : A valid pointer to a linked_list_t structure
 * @return The value previous tail of the list
 */
void *pop_value(linked_list_t *list);

/**
 * @brief Append a new value to the list (tail)
 * @arg list : A valid pointer to a linked_list_t structure
 * @arg val : The value to store in the tail of the list
 * @return : 0 if success, -1 otherwise
 */
int push_value(linked_list_t *list, void *val);

/**
 * @brief Insert a new value at the beginning of the least (head)
 * @arg list : A valid pointer to a linked_list_t structure
 * @arg val : The value to store in the head of the list
 * @return : 0 if success, -1 otherwise
 */
int unshift_value(linked_list_t *list, void *val);

/**
 * @brief Remove the first value from the list
 * @arg list : A valid pointer to a linked_list_t structure
 * @return The previous value stored in the tail of the list
 */

void *shift_value(linked_list_t *list);
/**
 * @brief Insert a value at a specific position
 * @arg list : A valid pointer to a linked_list_t structure
 * @arg val : The value to store at pos
 * @arg pos : The position (offset) where to store the value
 * @return 0 if success, -1 otherwise
 *
 * If the list is shorter than pos-1 empty values will be inserted up to
 * that position before inserting the new one
 */
int insert_value(linked_list_t *list, void *val, unsigned long pos);

/**
 * @brief Pick the value at a specific position
 * @arg list : A valid pointer to a linked_list_t structure
 * @arg pos : The position (offset) of the requested value
 * @return : The value stored at pos if any, NULL otherwise
 *
 * Note this is a read-only access and the value will not be removed from the list
 */
void *pick_value(linked_list_t *list, unsigned long pos);

/**
 * @brief Fetch (aka: Pick and Remove) the value at a specific position
 * @arg list : A valid pointer to a linked_list_t structure
 * @arg pos : The position (offset) of the requested value
 * @return : The value stored at pos if any, NULL otherwise
 *
 * Note this is a read-write access and the value will be removed from the list before returning it.
 * The value will not be released so the free_value_callback won't be called in this case
 */
void *fetch_value(linked_list_t *list, unsigned long pos);

/**
 * @brief Move an existing value to a new position
 * @arg list : A valid pointer to a linked_list_t structure
 * @arg srcPos : The actual position of the value we want to move
 * @arg dstPos : The new position where to move the value to
 * @return : 0 if success, -1 otherwise
 */ 
int move_value(linked_list_t *list, unsigned long srcPos, unsigned long dstPos);

/**
 * @brief Replace the value stored at a specific position with a new value
 * @arg list : A valid pointer to a linked_list_t structure
 * @arg pos : The position of the value we want to replace
 * @arg newVal : The new value
 */
void *subst_value(linked_list_t *list, unsigned long pos, void *newVal);

/**
 * @brief Swap two values
 * @arg list : A valid pointer to a linked_list_t structure
 * @pos1 : The position of the first value to swap with a second one
 * @pos2 : The position of the second value to swap with the first
 * @return 0 if success, -1 otherwise
 */
int swap_values(linked_list_t *list, unsigned long pos1, unsigned long pos2);


/**
 * @brief Callback for the value iterator
 */
typedef int (*item_handler_t)(void *item, unsigned long idx, void *user);

/* list iterator. This iterator can be used for both Tag-based and Value-based lists.
 * If tagged, items can simply be casted to a tagged_value_t pointer.
 */
void foreach_list_value(linked_list_t *list, item_handler_t item_handler, void *user);

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
 * create_tagged_value() should be used to allocate resources and obtain 
 * a pointer to a tagged_value_t structure.
 */ 
typedef struct __tagged_value {
    char *tag;
    void *value;
    unsigned long vlen;
    char type;
#define TV_TYPE_STRING 0
#define TV_TYPE_BINARY 1
#define TV_TYPE_LIST   2
} tagged_value_t;


/* List creation and destruction routines */

/* Tagged List access routines (same of previous but with tag support */
/**
 * @brief Allocate resources for a new tagged value
 * @arg tag : The tag
 * @arg val : The value
 * @arg len : The size of the value
 * @return a newly created tagged value with the provided tag and value
 *
 * Both the tag and the value will be copied. len will be the size used by the copy
 */
tagged_value_t *create_tagged_value(char *tag, void *val, unsigned long len);

/**
 * @brief Allocate resources for a new tagged value without copying the value
 * @arg tag : The tag
 * @arg val : The value
 * @return A newly created tagged value with the provided tag and value
 *
 * Only the tag will be copied, the value will just point 
 * to the provided value without it being copied 
 */
tagged_value_t *create_tagged_value_no_copy(char *tag, void *val);

/**
 * @brief Create a tagged value where the value is a linked_list_t
 * @arg tag : The tag
 * @arg list: The list used as value
 * @return A newly created tagged value with type TV_TYPE_LIST
 *
 * This function is just an accessor to set the tagged_value->type properly
 * when using it to store a list
 */
tagged_value_t *create_tagged_sublist(char *tag, linked_list_t *list);

/**
 * @brief Release resources used by the tagged value tval
 * @arg tval : The tagged value to release
 */
void destroy_tagged_value(tagged_value_t *tval);

/**
 * @brief Same as pop_value but expect the value to be a pointer to a tagged_value_t structure
 * @arg list : A valid pointer to a linked_list_t structure holding tagged values
 * @return The tagged value stored at the end of the list
 */
tagged_value_t *pop_tagged_value(linked_list_t *list);

/**
 * @brief Same as push_value but when using the list to store tagged values
 * @arg list : A valid pointer to a linked_list_t structure holding tagged values
 * @arg tval: The new tagged value to store
 * @return 0 if success, -1 otherwise
 */
int push_tagged_value(linked_list_t *list, tagged_value_t *tVal);

/**
 * @brief Same as unshift_value but when using the list to store tagged values
 * @arg list : A valid pointer to a linked_list_t structure holding tagged values
 * @arg tval: The new tagged value to store
 * @return 0 if success, -1 otherwise
 */
int unshift_tagged_value(linked_list_t *list, tagged_value_t *tval);

/**
 * @brief Same as shift_value but when using the list to store tagged values
 * @arg list : A valid pointer to a linked_list_t structure holding tagged values
 * @return The tagged value stored in the head of the list, NULL if the list is empty
 */
tagged_value_t *shift_tagged_value(linked_list_t *list);

/**
 * @brief Same as insert_value but when using the list to store tagged values
 * @arg list : A valid pointer to a linked_list_t structure holding tagged values
 * @arg tval: The new tagged value to store
 * @arg pos: The position (index) where to store the new tagged value
 * @return 0 if success, -1 otherwise
 */
int insert_tagged_value(linked_list_t *list, tagged_value_t *tval, unsigned long pos);

/**
 * @brief Same as pick_value but when using the list to store tagged values
 * @arg list : A valid pointer to a linked_list_t structure holding tagged values
 * @arg pos : The position (offset) of the requested tagged value
 * @return : The tagged value stored at pos if any, NULL otherwise
 *
 * Note this is a read-only access and the tagged value will not be removed from the list
 */
tagged_value_t *pick_tagged_value(linked_list_t *list, unsigned long pos);

/**
 * @brief Same as fetch_value but when using the list to store tagged values
 * @arg list : A valid pointer to a linked_list_t structure holding tagged values
 * @arg pos : The position (offset) of the requested tagged value
 * @return : The tagged value stored at pos if any, NULL otherwise
 *
 * Note this is a read-write access and the tagged value will be removed from 
 * the list before returning it.
 * The tagged value will not be released
 */
tagged_value_t *fetch_tagged_value(linked_list_t *list, unsigned long pos);

/**
 * @brief Get a tagged value from the list by using its tag instead of the position
 * @arg list : A valid pointer to a linked_list_t structure holding tagged values
 * @arg tag  : The tag of the value we are looking for
 * @return The first tagged value in the list whose tag matches the provided tag
 *
 * Note this is a read-only access and the tagged value will not be removed from the list
 */
tagged_value_t *get_tagged_value(linked_list_t *list, char *tag);


/**
 * @brief Get all value pointers for all tagged values matching a specific tag
 * @arg list : A valid pointer to a linked_list_t structure holding tagged values
 * @arg tag  : The tag of the values we are looking for
 * @arg values : a valid pointer to a linked_list_t structure where to put the 
 *               value pointers held by the tagged_value_t items matching the provided tag
 * @return The number of tagged values matching the tag and added to the values linked list
 *
 * Note The caller MUST NOT release resources for the returned values
 * (since still pointed by the tagged_value_t still in list)
 */
unsigned long get_tagged_values(linked_list_t *list, char *tag, linked_list_t *values);



#ifdef __cplusplus
}
#endif

#endif
