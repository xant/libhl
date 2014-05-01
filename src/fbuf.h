#ifndef __FBUF_H__
#define __FBUF_H__

/**
 * @file
 *
 * @brief Dynamic (flat) buffers
 *
 * Buffer implementation. 
 * Slowly grows a buffer to contain the data.
 * - Always \\0-terminated
 * - Data is always added completely or not, never partially.
 * - Preferred maximum size of buffer:
 *   Content is only added if either the buffer does not need to be extended or
 *   if it needs to be realloc()-ed, hasn't already exceeded the preferred
 *   maximum size of the buffer if a preferred maximum size is set for the
 *   buffer.
 * - If extending the buffer fails, the buffer is left untouched.
 * - Buffer grows quickly until 64kB and then slows down to adding 1Mb at a time
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#ifndef WIN32
#include <sys/cdefs.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#define FBUF_MINLEN       128     //!< Minimum size of buffer
#define FBUF_FASTGROWSIZE 1<<18   //!< Grow quickly up to 256KB ...
#define FBUF_SLOWGROWSIZE 1<<16   //!< ... and slowly after that (+64kB)
#define FBUF_MAXLEN_NONE 0 //!< No preferred maximum length for fbuf.
#define FBUF_STATIC_INITIALIZER { 0, NULL, 0, FBUF_MAXLEN_NONE, FBUF_MINLEN, \
                                  FBUF_FASTGROWSIZE, FBUF_SLOWGROWSIZE, 0, 0 }

typedef struct __fbuf {
    unsigned int id;           //!< unique ID for the buffer for reference
    char *data;                //!< buffer. the caller should never access it directly but use 
                               //   the fbuf_data() function instead. If accessed directly,
                               //   the 'skip' member needs to be taken into account
    unsigned int len;          //!< allocated length of buffer
    unsigned int prefmaxlen;   //!< preferred maximum allocated length of buffer
    unsigned int minlen;       //!< the minimum allocated length of the buffer
    unsigned int fastgrowsize; //!< the size to quickly grow up to if writing more than misize bytes
    unsigned int slowgrowsize;
    unsigned int used;         //!< number of bytes used in buffer
    unsigned int skip;         //!< how many bytes to ignore from the beginning buffer
} fbuf_t;

/**
 * @brief Allocate and initialise a fbuf structure.
 * @param prefmaxlen preferred maximum length of buffer
 * @returns pointer to created fbuf on success; NULL otherwise.
 */
fbuf_t *fbuf_create(unsigned int prefmaxlen);

/**
 * @brief Move the fbuf from one structure to the other.
 * @param fbufsrc fbuf to move from
 * @param fbufdst fbuf to move to
 * @note fbufsrc is destroyed first
 * @see fbuf_swap()
 */
void fbuf_move(fbuf_t *fbufsrc, fbuf_t *fbufdst);

/**
 * @brief Exchange the two fbuf structures.
 * @param fbuf1 fbuf
 * @param fbuf2 fbuf
 */
void fbuf_swap(fbuf_t *fbuf1, fbuf_t *fbuf2);

/**
 * @brief Duplicate the fbuf structure into a new fbuf structure
 * @param fbufsrc fbuf
 * @returns pointer to created fbuf on success; NULL otherwise
 */
fbuf_t *fbuf_duplicate(fbuf_t *fbufsrc);

/**
 * @brief Extend the size of the buffer to newlen
 * @param fbuf fbuf
 * @param newlen length of string to fit in buffer; increment before realloc()
 *               is called.
 * @returns new buffer length (always > 0) on success, 0 otherwise.
 *
 * FBufExtended is succesfull if:
 * - the buffer is long enough to fit newlen,
 * - the buffer needs to be extended but
 *   - prefmaxlen is FBUF_MAXLEN_NONE,
 *   - prefmaxlen is set but smaller than the length of the buffer.
 *
 * @note fbuf_extend() extends beyond prefmaxlen, but only once.
 * @note if globalMaxLen is reached for a buffer an error will returned
 *       by any write/extend operation on that buffer
 */
unsigned int fbuf_extend(fbuf_t *fbuf, unsigned int newlen);

/**
 * @brief Shrink the memory used by the buffer to fit the contents.
 * @param fbuf fbuf
 * @returns new fbuf length.
 * @note fbuf_shrink() frees the memory in the fbuf if fbuf->len == 0.
 */
unsigned int fbuf_shrink(fbuf_t *fbuf);

/**
 * @brief Clear the fbuf.
 * @param fbuf fbuf
 *
 * Resets the 'used' count to zero and terminates the buffer at position 0.
 *
 * @see fbuf_destroy(), fbuf_free()
 */
void fbuf_clear(fbuf_t *fbuf);

/**
 * @brief Destroys all information in the fbuf.
 * @param fbuf fbuf
 * 
 * Deallocates all memory used in fbuf.
 *
 * @see fbuf_clear(), fbuf_free()
 */
void fbuf_destroy(fbuf_t *fbuf);

/**
 * @brief Deallocates the fbuf structure after destroying it.
 * @param fbuf fbuf
 *
 * Destroys the fbuf and deallocates the structure. The pointer to the fbuf is
 * no longer valid after the function returns.
 *
 * @see fbuf_clear(), fbuf_destroy()
 */
void fbuf_free(fbuf_t *fbuf);

int fbuf_add_binary(fbuf_t *fbuf, const char *data, int len);

/**
 * @brief Add data to the buffer.
 * @param fbuf fbuf
 * @param data string to be added
 * @returns number of characters added on success; -1 otherwise.
 *
 * Adds the string data to the fbuf if the buffer can be extended to fit.
 *
 * @see fbuf_concat()
 */
int fbuf_add(fbuf_t *fbuf, const char *data);

/**
 * @brief Add data to the buffer, including a trailing newline.
 * @param fbuf fbuf
 * @param data string to be added
 * @returns number of characters added on success; -1 otherwise.
 *
 * Adds the string data to the fbuf if the buffer can be extended to fit.
 *
 * @see fbuf_concat()
 */
int fbuf_add_ln(fbuf_t *fbuf, const char *data);

/**
 * @brief Prepend the string to the fbuf.
 * @param fbuf fbuf
 * @param data string to the added to the start of the fbuf
 * @returns number of characters added on succes; -1 otherwise.
 */
int fbuf_prepend(fbuf_t *fbuf, const char *data);

int fbuf_prepend_binary(fbuf_t *fbuf, const char *data, int len);

/**
 * @brief Concatenates fbufsrc after fbufdst.
 * @param fbufdst fbuf to add to
 * @param fbufsrc fbuf to add to fbufdst
 * @returns number of characters added on success; -1 otherwise
 *
 * Concatenates fbufsrc to the end of fbufdst if fbufdst can be extended to fit
 * both strings.
 */
int fbuf_concat(fbuf_t *fbufdst, fbuf_t *fbufsrc);

/**
 * @brief Copy the string fbufsrc into fbufdst.
 * @param fbufsrc fbuf to copy from
 * @param fbufdst fbuf to copy to
 * @returns number of characters added on success; -1 otherwise.
 *
 * Clears fbufdst and copies the string from fbufsrc into the fbufdst if
 * fbufdst can be extended to fit the string in fbufsrc. fbufdst is not shrunk.
 */
int fbuf_copy(fbuf_t *fbufsrc, fbuf_t *fbufdst); 

/**
 * @brief Set the fbuf to a string.
 * @param fbuf fbuf
 * @param data string
 * @returns number of characters copied on success; -1 otherwise.
 */
int fbuf_set(fbuf_t *fbuf, const char *data);

/**
 * @brief Add a string produced through printf to the fbuf.
 * @param fbuf fbuf
 * @param fmt printf style format string
 * @param ... printf style parameter list
 * @returns number characters copied on success; -1 otherwise.
 */
int fbuf_printf(fbuf_t *fbuf, const char *fmt, ...);

/**
 * @brief Read at most explen bytes from a file.
 * @param fbuf fbuf
 * @param file file descriptor
 * @param explen estimate of bytes to be read
 * @returns number of characters added to fbuf on success; -1 otherwise.
 */
int fbuf_fread(fbuf_t *fbuf, FILE *file, unsigned int explen);

/**
 * @brief Read at most explen bytes from a file.
 * @param fbuf fbuf
 * @param fd file descriptor
 * @param explen estimate of bytes to be read
 * @returns number of characters added to fbuf on success; -1 otherwise.
 */
int fbuf_read(fbuf_t *fbuf, int fd, unsigned int explen);

int fbuf_read_ln(fbuf_t *fbuf, int fd);
int fbuf_fread_ln(fbuf_t *fbuf, FILE *file);

/**
 * @brief Write data from the fbuf to the file descriptor
 * @param fbuf fbuf
 * @param fd file descriptor for write()
 * @param nbytes bytecount to pass to write()
 * @returns number of bytes written
 *
 * @note if nbytes is zero, all of fbuf is written.
 * @note data written is removed from the fbuf.
 */
int fbuf_write(fbuf_t *fbuf, int fd, unsigned int nbytes);

/**
 * @brief Remove bytes from the beginning of the buffer.
 * @param fbuf fbuf
 * @param len  number of bytes to remove
 * @returns new value for used
 */
int fbuf_remove(fbuf_t *fbuf, unsigned int len);

/**
 * @brief Remove leading whitespace.
 * @param fbuf fbuf
 * @returns number of bytes removed.
 */
int fbuf_trim(fbuf_t *fbuf);

/**
 * @brief Remove trailing whitespace.
 * @param fbuf fbuf
 * @returns number of bytes removed.
 */
int fbuf_rtrim(fbuf_t *fbuf);

/**
 * @brief Return a pointer to the buffer.
 * @param fbuf fbuf
 * @returns pointer to actual buffer, or NULL.
 *
 * @note Do not modify the buffer (mainly inserting '\\0') without using
 * fbuf_remove() to remove the modified content afterwards.
 *
 * @see fbuf_end()
 */
char *fbuf_data(fbuf_t *fbuf);

/**
 * @brief Return a pointer to the end of the buffer ('\\0').
 * @param fbuf fbuf
 * @return returns pointer to actual buffer, or NULL.
 *
 * @see fbuf_data()
 */
char *fbuf_end(fbuf_t *fbuf);

/**
 * @brief Set the used value on the fbuf.
 * @param fbuf fbuf
 * @param newused new value for used
 * @returns new value for used
 * @note used value can only be reduced.
 */
int fbuf_set_used(fbuf_t *fbuf, unsigned int newused);

/**
 * @brief Return the number of characters in the buffer.
 * @param fbuf fbuf
 * @returns number of bytes in fbuf.
 */
unsigned int fbuf_used(fbuf_t *fbuf);
/**
 * @brief Return current length of buffer.
 * @param fbuf fbuf
 * @returns current allocated length of buffer.
 */
unsigned int fbuf_len(fbuf_t *fbuf);

/**
 * @brief Set the prefmaxlen value on the fbuf.
 * @param fbuf fbuf
 * @param prefmaxlen new value for prefmaxlen. If UINT_MAX is passed no new
 *             value will be set but the current value will be still returned.
 * @returns Previous value for prefmaxlen.
 * @note if the buffer is already bigger than the newly configured prefmaxlen
 *       it will be truncated to to fit the actual prefmaxlen value
 */
unsigned int fbuf_prefmaxlen(fbuf_t *fbuf, unsigned int prefmaxlen);

/**
 * @brief Set a hard limit on the size of all buffers and return the
 *        previously configured value.
 * @param maxlen hard limit on the size of a buffer. If UINT_MAX is passed no new
 *               value will be set but the current value will be still returned.
 *               0 means no limit.
 * @return Return the previous hard limit on the size of all buffers.
 *
 * @note If this limits is reached for a buffer an error will returned by any
 *       write/extend operation on the buffer
 */
unsigned int fbuf_maxlen(unsigned int maxlen);

unsigned int fbuf_minlen(fbuf_t *fbuf, unsigned int minlen);

unsigned int fbuf_fastgrowsize(fbuf_t *fbuf, unsigned int size);
unsigned int fbuf_slowgrowsize(fbuf_t *fbuf, unsigned int size);

#ifdef __cplusplus
}
#endif

#endif
