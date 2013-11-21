/**
 * \file
 *
 * \brief Dynamic (flat) buffers
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
 *
 * \todo An offset field should be added to prevent copying the buffer to the
 *       start all the time.
 */
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef WIN32
#include <strings.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/param.h>
#else
#include <io.h>
#include <sys/types.h>
#define bcopy(src, dst, count)	memcpy((void *)dst, (const void *)src, (size_t) count) 
#define bzero(addr, count)		memset((addr), 0, (count))
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#define MIN(a, b)	((a) < (b) ? (a) : (b))
#endif


#include "fbuf.h"

#ifdef DEBUG_FBUF
#define DEBUG_FBUF_INFO(fbuf, msg)	do { \
					    static int _debug = 0; \
					    if (++_debug == 1) { \
						DBG("%s: %d: %s, len = %d, prefmaxlen = %d, used = %d\n", \
						       __FUNCTION__, fbuf->id, msg, fbuf->len, fbuf->prefmaxlen, fbuf->used); \
						DBG("%s: %s\n", \
						       __FUNCTION__, hex_escape(fbuf->data, fbuf->used)); \
					    } \
					    --_debug; \
					} while (0)
#else
#define DEBUG_FBUF_INFO(fbuf, msg)	/* nop */ //!< Debug printf
#endif

#define FBUF_MINSIZE		8192		//!< Minimum size of buffer
#define FBUF_FASTGROWSIZE	64*1024		//!< Grow quickly up to 64kB (x 2) ...
#define FBUF_SLOWGROWSIZE	64*1024		//!< ... and slowly after that (+64kB)
#define FBUF_LINE_EST		127		//!< estimated for the length of a printf statement
#define FBUF_READ_EST		1024		//!< default value for fbuf_read()
#define FBUF_WRITE_EST		10240		//!< default value for fbuf_write()

static int fbuf_count = 0;
static unsigned int globalMaxLen = 0;		//!< hard limit for size of all buffers

/**
 * \brief Allocate and initialise a fbuf structure.
 * \param prefmaxlen preferred maximum length of buffer
 * \returns pointer to created fbuf on success; NULL otherwise.
 */
fbuf_t *
fbuf_create(unsigned int prefmaxlen)
{
    fbuf_t *fbuf = (fbuf_t *)calloc(1, sizeof(fbuf_t));
    if (fbuf) {
	DEBUG_FBUF_INFO(fbuf, "creating");
        fbuf->id = fbuf_count++;
        fbuf->prefmaxlen = prefmaxlen;

    }

    return fbuf;
}

/**
 * \brief Move the fbuf from one structure to the other.
 * \param fbufsrc fbuf to move from
 * \param fbufdst fbuf to move to
 * \note fbufsrc is destroyed first
 * \see fbuf_swap()
 */
void
fbuf_move(fbuf_t *fbufsrc, fbuf_t *fbufdst)
{
    DEBUG_FBUF_INFO(fbufsrc, "source");
    DEBUG_FBUF_INFO(fbufsrc, "destination");
    fbuf_destroy(fbufdst);
    bcopy(fbufsrc, fbufdst, sizeof(fbuf_t));
    bzero(fbufsrc, sizeof(fbuf_t));
    fbufsrc->id = fbuf_count++;
}

/**
 * \brief Exchange the two fbuf structures.
 * \param fbuf1 fbuf
 * \param fbuf2 fbuf
 */
void
fbuf_swap(fbuf_t *fbuf1, fbuf_t *fbuf2)
{
    fbuf_t fbuf3;

    DEBUG_FBUF_INFO(fbuf1, "swap 1");
    DEBUG_FBUF_INFO(fbuf2, "swap 2");

    bcopy(fbuf1, &fbuf3, sizeof(fbuf_t));
    bcopy(fbuf2, fbuf1, sizeof(fbuf_t));
    bcopy(&fbuf3, fbuf2, sizeof(fbuf_t));
}

/**
 * \brief Duplicate the fbuf structure into a new fbuf structure
 * \param fbufsrc fbuf
 * \returns pointer to created fbuf on success; NULL otherwise
 */
fbuf_t *
fbuf_duplicate(fbuf_t *fbufsrc)
{
    fbuf_t *fbufdst = fbuf_create(fbufsrc->prefmaxlen);
    if (!fbufdst)
	return NULL;
    fbuf_add(fbufdst, fbufsrc->data);

    DEBUG_FBUF_INFO(fbufsrc, "original");
    DEBUG_FBUF_INFO(fbufdst, "duplicate");

    return fbufdst;
}

/**
 * \brief Extend the size of the buffer to newlen
 * \param fbuf fbuf
 * \param newlen length of string to fit in buffer; increment before realloc()
 *               is called.
 * \returns new buffer length (always > 0) on success, 0 otherwise.
 *
 * FBufExtended is succesfull if:
 * - the buffer is long enough to fit newlen,
 * - the buffer needs to be extended but
 *   - prefmaxlen is FBUF_MAXLEN_NONE,
 *   - prefmaxlen is set but smaller than the length of the buffer.
 *
 * \note fbuf_extend() extends beyond prefmaxlen, but only once.
 * \note if globalMaxLen is reached:
 *  - if prefmaxlen is set for the buffer is not extended.
 *  - if prefmaxlen is not set for the buffer exit(99) is called.
 */
unsigned int
fbuf_extended(fbuf_t *fbuf, unsigned int newlen)
{
    char *p;

    newlen++;		// Include room for a '\0' terminator

    // Do we need to extend the buffer?
    if (newlen <= fbuf->len)
	return fbuf->len;

    // We may only extend the buffer the current length of the buffer is less
    // then prefmaxlen (if this soft limit is set).
    if (fbuf->prefmaxlen != FBUF_MAXLEN_NONE && fbuf->len > fbuf->prefmaxlen) {
	errno = ENOMEM;
        return 0;
    }

    // We may only extend the buffer if the new length is less then the global
    // hard limit on fbuf buffer lengths.
    if (globalMaxLen && newlen > globalMaxLen) {
	// If a prefmax is set for this buffer assume that the code can handle
	// errors. Otherwise exit with an error.
	if (fbuf->prefmaxlen != FBUF_MAXLEN_NONE) {
	    errno = ENOMEM;
	    return 0;
	} else {
	    exit(99);
	}
    }

    // Calculate the new size for the buffer
    // - Start with a minimum size.
    // - Grow quickly at first.
    // - Grow more slowly once the threshold has been reached.
    // - Cap the new length if it exceeds global maximum if set.
    while (newlen > fbuf->len) {
	if (fbuf->len == 0)
	    fbuf->len = FBUF_MINSIZE;
	else if (fbuf->len < FBUF_FASTGROWSIZE)
	    fbuf->len *= 2;
	else
	    fbuf->len += FBUF_SLOWGROWSIZE;
    }
    if (globalMaxLen && fbuf->len > globalMaxLen)
	fbuf->len = globalMaxLen;

    p = (char *)realloc(fbuf->data, fbuf->len);
    if (p) {
	if (!fbuf->data)
	    p[0] = '\0';		// terminate the new buffer string
	fbuf->data = p;
	DEBUG_FBUF_INFO(fbuf, "extended");
	
	return fbuf->len;
    } else {
	return 0;
    }
}

/*!
 * \brief Shrink the memory used by the buffer to fit the contents.
 * \param fbuf fbuf
 * \returns new fbuf length.
 * \note fbuf_shrink() frees the memory in the fbuf if fbuf->len == 0.
 */
unsigned int
fbuf_shrink(fbuf_t *fbuf)
{
    unsigned int newlen, len = fbuf->len;
    char *p;

    if (fbuf->used == 0)
	len = 0;

    do {
	newlen = len;
	if (len <= FBUF_MINSIZE)
	    break;
	else if (len <= FBUF_FASTGROWSIZE)
	    len /= 2;
	else
	    len -= FBUF_SLOWGROWSIZE;
    } while (len >= fbuf->used+1);
    // len is now the first size smaller than required, newlen is the last
    // size that fits the buffer.

    if (newlen == fbuf->len)		// nothing to be done
	return fbuf->len;

    if (newlen == 0) {
	free(fbuf->data);
	fbuf->data = NULL;
	fbuf->len = 0;
    } else if ((p = (char *)realloc(fbuf->data, newlen)) != NULL) {
	fbuf->data = p;
	fbuf->len = newlen;
    }

    DEBUG_FBUF_INFO(fbuf, "shrunk");
    return fbuf->len;
}

/**
 * \brief Clear the fbuf.
 * \param fbuf fbuf
 *
 * Resets the 'used' count to zero and terminates the buffer at position 0.
 *
 * \see fbuf_destroy(), fbuf_free()
 */
void
fbuf_clear(fbuf_t *fbuf)
{
    fbuf->used = 0;
    if (fbuf->len > 0)
	fbuf->data[0] = '\0';
}

/**
 * \brief Destroys all information in the fbuf.
 * \param fbuf fbuf
 * 
 * Deallocates all memory used in fbuf.
 *
 * \see fbuf_clear(), fbuf_free()
 */
void
fbuf_destroy(fbuf_t *fbuf)
{
    if (fbuf->data)
	free(fbuf->data);
    fbuf->data = NULL;
    fbuf->used = fbuf->len = 0;

    DEBUG_FBUF_INFO(fbuf, "destroyed");
}

/**
 * \brief Deallocates the fbuf structure after destroying it.
 * \param fbuf fbuf
 *
 * Destroys the fbuf and deallocates the structure. The pointer to the fbuf is
 * no longer valid after the function returns.
 *
 * \see fbuf_clear(), fbuf_destroy()
 */
void
fbuf_free(fbuf_t *fbuf)
{
    if (fbuf->data)
	free(fbuf->data);

#ifdef DEBUG_BUILD
    fbuf->data = NULL;
    fbuf->used = fbuf->len = 0;
#endif

    DEBUG_FBUF_INFO(fbuf, "destroyed");

    free(fbuf);
}

int
fbuf_add_binary(fbuf_t *fbuf, const char *data, int len)
{
    if (!fbuf_extended(fbuf, fbuf->used+len))
	return -1;

    memcpy(fbuf->data+fbuf->used, data, len);
    fbuf->used += len;
    fbuf->data[fbuf->used] = 0;

    return len;
}

/**
 * \brief Add data to the buffer.
 * \param fbuf fbuf
 * \param data string to be added
 * \returns number of characters added on success; -1 otherwise.
 *
 * Adds the string data to the fbuf if the buffer can be extended to fit.
 *
 * \see fbuf_concat()
 */
int
fbuf_add(fbuf_t *fbuf, const char *data)
{
    int datalen;

    if (!data || data[0] == '\0')	// nothing to be done
	return 0;

    datalen = strlen(data);
    return fbuf_add_binary(fbuf, data, datalen);
}

/**
 * \brief Add data to the buffer, including a trailing newline.
 * \param fbuf fbuf
 * \param data string to be added
 * \returns number of characters added on success; -1 otherwise.
 *
 * Adds the string data to the fbuf if the buffer can be extended to fit.
 *
 * \see fbuf_concat()
 */
int
fbuf_add_nl(fbuf_t *fbuf, const char *data)
{
    int n1 = 0, n2 = 0;

    n1 = fbuf_add(fbuf, data);
    if (n1 == -1)
	return -1;

    n2 = fbuf_add(fbuf, "\n");
    if (n2 == -1)
	return -1;

    return n1 + n2;
}

/**
 * \brief Concatenates fbufsrc after fbufdst.
 * \param fbufdst fbuf to add to
 * \param fbufsrc fbuf to add to fbufdst
 * \returns number of characters added on success; -1 otherwise
 *
 * Concatenates fbufsrc to the end of fbufdst if fbufdst can be extended to fit
 * both strings.
 */
int
fbuf_concat(fbuf_t *fbufdst, fbuf_t *fbufsrc)
{
    int datalen;

    if (fbufsrc->used == 0)		// nothing to be done
	return 0;

    datalen = fbufsrc->used;
    if (!fbuf_extended(fbufdst, fbufdst->used+datalen))
	return -1;

    memcpy(fbufdst->data+fbufdst->used, fbufsrc->data, datalen+1);
    fbufdst->used += datalen;

    return datalen;
}

/**
 * \brief Copy the string fbufsrc into fbufdst.
 * \param fbufsrc fbuf to copy from
 * \param fbufdst fbuf to copy to
 * \returns number of characters added on success; -1 otherwise.
 *
 * Clears fbufdst and copies the string from fbufsrc into the fbufdst if
 * fbufdst can be extended to fit the string in fbufsrc. fbufdst is not shrunk.
 */
int
fbuf_copy(fbuf_t *fbufsrc, fbuf_t *fbufdst)
{
    if (!fbuf_extended(fbufdst, fbufsrc->used))
	return -1;
    fbuf_clear(fbufdst);
    strcpy(fbufdst->data, fbufsrc->data);
    fbufdst->used = fbufsrc->used;

    return fbufdst->used;
}

/**
 * \brief Set the fbuf to a string.
 * \param fbuf fbuf
 * \param data string
 * \returns number of characters copied on success; -1 otherwise.
 */
int
fbuf_set(fbuf_t *fbuf, const char *data)
{
    int datalen = strlen(data);

    if (!fbuf_extended(fbuf, datalen))
	return -1;

    memcpy(fbuf->data, data, datalen+1);
    fbuf->used = datalen;

    return datalen;
}

/**
 * \brief Add a string produced through printf to the fbuf.
 * \param fbuf fbuf
 * \param fmt printf style format string
 * \param ... printf style parameter list
 * \returns number characters copied on success; -1 otherwise.
 */
int
fbuf_printf(fbuf_t *fbuf, const char *fmt, ...)
{
    va_list args;
    unsigned int n;

    if (!fbuf_extended(fbuf, fbuf->used+FBUF_LINE_EST))
        return -1;

    va_start(args, fmt);
    n = vsnprintf(fbuf->data+fbuf->used, fbuf->len-fbuf->used, fmt, args);
    va_end(args);

    if (n >= fbuf->len-fbuf->used) {		// some chars were discarded
        // extend the buffer and try again
        if (!fbuf_extended(fbuf, fbuf->used+n)) {
            fbuf->data[fbuf->used] = '\0';	// chop off any added content
            return -1;
        }
        va_start(args, fmt);
        n = vsnprintf(fbuf->data+fbuf->used, fbuf->len-fbuf->used, fmt, args);
        va_end(args);
    }

    fbuf->used += n;
    fbuf->data[fbuf->used] = '\0';		// terminate the buffer string

    va_end(args);

    return n;
}

/**
 * \brief Read at most explen bytes from a file.
 * \param fbuf fbuf
 * \param file file descriptor
 * \param explen estimate of bytes to be read
 * \returns number of characters added to fbuf on success; -1 otherwise.
 */
int
fbuf_fread(fbuf_t *fbuf, FILE *file, unsigned int explen)
{
    int n;

    if (explen == 0)
	explen = FBUF_READ_EST;			// one 80 char line and some

    if (!fbuf_extended(fbuf, fbuf->used+explen))
	return -1;

    n = fread(fbuf->data+fbuf->used, 1, explen, file);
    if (n > 0)
	fbuf->used += n;
    fbuf->data[fbuf->used] = '\0';		// terminate the buffer string

    return n;
}


/**
 * \brief Read at most explen bytes from a file.
 * \param fbuf fbuf
 * \param fd file descriptor
 * \param explen estimate of bytes to be read
 * \returns number of characters added to fbuf on success; -1 otherwise.
 */
int
fbuf_read(fbuf_t *fbuf, int fd, unsigned int explen)
{
    int n;

    if (explen == 0)
	explen = FBUF_READ_EST;			// one 80 char line and some

    if (!fbuf_extended(fbuf, fbuf->used+explen))
	return -1;

    n = read(fd, fbuf->data+fbuf->used, explen);
    if (n > 0)
	fbuf->used += n;
    fbuf->data[fbuf->used] = '\0';		// terminate the buffer string

    return n;
}

/**
 * \brief Write data from the fbuf to the file descriptor
 * \param fbuf fbuf
 * \param fd file descriptor for write()
 * \param nbytes bytecount to pass to write()
 * \returns number of bytes written
 *
 * \note if nbytes is zero, all of fbuf is written.
 * \note data written is removed from the fbuf.
 */
int
fbuf_write(fbuf_t *fbuf, int fd, unsigned int nbytes)
{
    int n = 0;

    if (nbytes == 0)
	nbytes = fbuf->used;

    if (nbytes > fbuf->used)
	nbytes = fbuf->used;

    if (nbytes > 0) {
	n = write(fd, fbuf->data, nbytes);
	if (n > 0)
	    fbuf_remove(fbuf, n);
    }

    return n;
}

/**
 * \brief Remove bytes from the beginning of the buffer.
 * \param fbuf fbuf
 * \param len  number of bytes to remove
 * \returns new value for used
 */
int
fbuf_remove(fbuf_t *fbuf, unsigned int len)
{
    if (len >= fbuf->used) {
        fbuf->used = 0;
	fbuf->data[fbuf->used] = '\0';		// terminate the buffer string
    } else if (len) {
	fbuf->used -= len;
	memmove(fbuf->data, fbuf->data+len, fbuf->used+1);
    }

    return fbuf->used;
}

/**
 * \brief Remove leading whitespace.
 * \param fbuf fbuf
 * \returns number of bytes removed.
 */
int
fbuf_trim(fbuf_t *fbuf)
{
    unsigned int i = 0;
    while (i < fbuf->used && isspace(fbuf->data[i]))
	i++;

    fbuf_remove(fbuf, i);

    return i;
}

/**
 * \brief Return a pointer to the buffer.
 * \param fbuf fbuf
 * \returns pointer to actual buffer, or NULL.
 *
 * \note Do not modify the buffer (mainly inserting '\\0') without using
 * fbuf_remove() to remove the modified content afterwards.
 *
 * \see fbuf_end()
 */
char *
fbuf_data(fbuf_t *fbuf)
{
    return fbuf->data;
}

/**
 * \brief Return a pointer to the end of the buffer ('\0').
 * \param fbuf fbuf
 * \return returns pointer to actual buffer, or NULL.
 *
 * \see FBData()
 */
char *
fbuf_end(fbuf_t *fbuf)
{
    if (fbuf->data)
	return fbuf->data + fbuf->used;
    else
	return NULL;
}

/**
 * \brief Set the used value on the fbuf.
 * \param fbuf fbuf
 * \param newused new value for used
 * \returns new value for used
 * \note used value can only be reduced.
 */
int
fbuf_set_used(fbuf_t *fbuf, unsigned int newused)
{
    if (newused < fbuf->used) {
	fbuf->used = newused;
	fbuf->data[fbuf->used] = '\0';
    }

    return fbuf->used;
}

/**
 * \brief Return the number of characters in the buffer.
 * \param fbuf fbuf
 * \returns number of bytes in fbuf.
 */
unsigned int
fbuf_used(fbuf_t *fbuf)
{
    return fbuf->used;
}

/**
 * \brief Return current length of buffer.
 * \param fbuf fbuf
 * \returns current allocated length of buffer.
 */
unsigned int
fbuf_len(fbuf_t *fbuf)
{
    return fbuf->len;
}

/**
 * \brief Set the prefmaxlen value on the fbuf.
 * \param fbuf fbuf
 * \param newprefmaxlen new value for prefmaxlen
 * \returns new value for prefmaxlen
 * \note prefmaxlen is set, but buffer is not resized.
 */
unsigned int
fbuf_set_pref_maxlen(fbuf_t *fbuf, unsigned int newprefmaxlen)
{
    return (fbuf->prefmaxlen = newprefmaxlen);
}

/**
 * \brief Return the currently set preferred maximum length of the buffer.
 * \param fbuf fbuf
 * \returns Current value for prefmaxlen.
 */
unsigned int
fbuf_pref_maxlen(fbuf_t *fbuf)
{
    return fbuf->prefmaxlen;
}

/**
 * \brief Set a hard limit on the size of all buffers.
 * \param maxlen hard limit on the size of a buffer.
 *
 * \note If this limits is reached for a buffer:
 * - if a prefmaxlen is set for a buffer an error is returned
 * - exit(99) is called otherwise.
 */
unsigned int
fbuf_set_maxlen(unsigned int maxlen)
{
    return (globalMaxLen = maxlen);
}
/**
 * \brief Return the current hard limit on the size of all buffers.
 */
unsigned int
fbuf_maxlen(void)
{
    return globalMaxLen;
}
