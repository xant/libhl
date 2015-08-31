/**
 * @file trie.h
 *
 * @brief Trie (prefix tree) implementation
 *
 */

#ifndef HL_TRIE_H
#define HL_TRIE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stdint.h>


typedef struct _trie_s trie_t;

typedef void (*trie_free_value_callback_t)(void *);

/**
 * @brief Create a new Trie
 * @param free_value_cb Optional callback that, if defined, will be invoked
 *                      when a node holding a stored value is being destroyed
 * @return a newly allocated and initialized trie
 */
trie_t *trie_create(trie_free_value_callback_t free_value_cb);

/**
 * @brief Release all the resources used by a trie
 * @param A valid pointer to a trie structure
 */
void trie_destroy(trie_t *trie);

/**
 * @brief Insert a new value in the trie
 *
 * @param trie  A valid pointer to a trie structure
 * @param key   The key (null-terminated) to which we want to store the value
 * @param value A pointer to the value to store
 * @param vsize The size of the value
 * @param copy  A boolean determining if the value should be internally copied
 *              or only referenced
 *
 * @return The total number of newly created nodes to store the value at the
 *         specified key
 *
 * @note No nodes might have been created if a previous value for the same key
 *       was present in the trie
 */
int trie_insert(trie_t *trie, char *key, void *value, size_t vsize, int copy);

/**
 * @brief Lookup the trie for a given key, if found returns the stored value
 *        otherwise insert a new value for the given key
 *
 * @param trie       A valid pointer to a trie structure
 * @param key        The key (null-terminated) to which we want to store the
 *                   value
 * @param value      A pointer to the value to store
 * @param vsize      The size of the value
 * @param prev_value If not NULL, the referenced pointer will be set to point 
 *                   to the value found at the given key
 * @param prev_vsize If not NULL, the size of the previous value will be stored
 *                   at the address pointed by prev_vsize
 * @param copy       A boolean determining if the value should be internally
 *                   copied or only referenced
 *
 * @return The total number of newly created nodes to store the value at the
 *         specified key
 *
 * @note No nodes might have been created if a previous value for the same key
 *       was present in the trie
 *
 */
int trie_find_or_insert(trie_t *trie,
                        char *key,
                        void *value,
                        size_t vsize,
                        void **prev_value,
                        size_t *prev_vsize,
                        int copy);

/**
 * @brief Lookup the trie for a given key, if found update the value optionally
 *        returning the previous one, if not found insert a new value for the
 *        given key
 *
 * @param trie        A valid pointer to a trie structure
 * @param key         The key (null-terminated) to which we want to store the
 *                    value
 * @param value       A pointer to the value to store
 * @param vsize       The size of the value
 * @param prev_value  If not NULL, the referenced pointer will be set to point 
 *                    to the value stored at the key being updated
 * @param prev_vsize  If not NULL, the size of the previous value will be stored
 *                    at the address pointed by prev_vsize
 * @param copy        A boolean determining if the value should be internally
 *                    copied or only referenced
 *
 * @return The total number of newly created nodes to store the value at the
 *         specified key
 *
 * @note No nodes might have been created if a previous value for the same key
 *       was present in the trie
 *
 */
int trie_find_and_insert(trie_t *trie,
                         char *key,
                         void *value,
                         size_t vsize,
                         void **prev_value,
                         size_t *prev_vsize,
                         int copy);

/**
 * @brief Lookup the trie for a given key
 * @param trie   A valid pointer to a trie structure
 * @param key    The key (null-terminated string) to which we want to store
 *               the value
 * @param vsize  If not NULL, the size of the returned value will be stored
 *               at the address pointed by vsize
 * @return The stored value if any, NULL otherwise
 */
void *trie_find(trie_t *trie, char *key, size_t *vsize);

/**
 * @brief Removes a key from the trie and optionally returns the stored value
 * @param trie   A valid pointer to a trie structure
 * @param key    The key (null-terminated string) to which we want to store
 *               the value
 * @param value  If not NULL, the referenced pointer will be set to point to
 *               the value stored at the key being removed
 * @param vsize  If not NULL, the size of the returned value will be stored
 *               at the address pointed by vsize
 *
 * @note If a value pointer has been provided, the value will not be freed
 *       regardless of a free_value_callback being present
 *
 * @return The total number of internal nodes removed
 */
int trie_remove(trie_t *trie, char *key, void **value, size_t *vsize);

#ifdef __cplusplus
}
#endif

#endif
