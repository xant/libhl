#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#ifndef WIN32
#include <strings.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/param.h>
#else
#include <io.h>
#include <sys/types.h>
#define bcopy(src, dst, count) memcpy((void *)dst, (const void *)src, (size_t) count) 
#define bzero(addr, count) memset((addr), 0, (count))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif


#include "fbuf.h"

#ifdef DEBUG_FBUF
#define DEBUG_FBUF_INFO(fbuf, msg) \
    do { \
        __thread static int _debug = 0; \
        if (++_debug == 1) { \
            DBG("%s: %d: %s, len = %d, maxlen = %u, minlen = %u, " \
                "slowgrowsize = %u, fastgrowsize = %u, used = %d\n", \
                __FUNCTION__, fbuf->id, msg, fbuf->len, fbuf->maxlen, \
                fbuf->minlen, fbuf->slowgrowsize, fbuf->fastgrowsize, fbuf->used); \
            DBG("%s: %s\n", \
                   __FUNCTION__, hex_escape(fbuf->data, fbuf->used)); \
        } \
        --_debug; \
    } while (0)
#else
#define DEBUG_FBUF_INFO(fbuf, msg)        /* nop */ //!< Debug printf
#endif

#define FBUF_LINE_EST     128     //!< estimated for the length of a printf statement
#define FBUF_READ_EST     1024    //!< default value for fbuf_read()
#define FBUF_WRITE_EST    10240   //!< default value for fbuf_write()

static int fbuf_count = 0;

fbuf_t *
fbuf_create(unsigned int maxlen)
{
    fbuf_t *fbuf = (fbuf_t *)calloc(1, sizeof(fbuf_t));

    if (fbuf) {
        DEBUG_FBUF_INFO(fbuf, "creating");
        fbuf->id = __sync_fetch_and_add(&fbuf_count, 1);
        fbuf->maxlen = maxlen;
        fbuf->fastgrowsize = FBUF_FASTGROWSIZE;
        fbuf->slowgrowsize = FBUF_SLOWGROWSIZE;
        fbuf->minlen = FBUF_MINLEN;
    }

    return fbuf;
}

unsigned int
fbuf_fastgrowsize(fbuf_t *fbuf, unsigned int size)
{
    unsigned int old = fbuf->fastgrowsize;
    if (size)
        fbuf->fastgrowsize = size;
    return old;
}

unsigned int
fbuf_slowgrowsize(fbuf_t *fbuf, unsigned int size)
{
    unsigned int old = fbuf->slowgrowsize;
    if (size)
        fbuf->slowgrowsize = size;
    return old;
}

unsigned int
fbuf_maxlen(fbuf_t *fbuf, unsigned int len)
{
    unsigned int old = fbuf->maxlen;
    if (len < UINT_MAX)
        fbuf->maxlen = len;
    if (fbuf->len > fbuf->maxlen) {
        fbuf_shrink(fbuf);
        char *new_data = realloc(fbuf->data, fbuf->maxlen +1);
        if (!new_data)
            return 0;
        fbuf->data = new_data;
        fbuf->len = fbuf->maxlen + 1;
        if (fbuf->used >= fbuf->maxlen)
            fbuf->used = fbuf->maxlen;
        fbuf->data[fbuf->maxlen] = 0;
    }
    return old;
}

unsigned int
fbuf_minlen(fbuf_t *fbuf, unsigned int len)
{
    unsigned int old = fbuf->minlen;
    if (len) {
        fbuf->minlen = len;
        if (fbuf->used < fbuf->minlen && fbuf->len > fbuf->minlen) {
            char *new_data = realloc(fbuf->data, fbuf->minlen);
            if (!new_data)
                return 0;
            fbuf->len = fbuf->minlen;
            fbuf->data = new_data;
        }
    }

    return old;
}

void
fbuf_move(fbuf_t *fbufsrc, fbuf_t *fbufdst)
{
    DEBUG_FBUF_INFO(fbufsrc, "source");
    DEBUG_FBUF_INFO(fbufsrc, "destination");
    fbuf_destroy(fbufdst);
    bcopy(fbufsrc, fbufdst, sizeof(fbuf_t));
    fbufsrc->data = NULL;
    fbufsrc->used = fbufsrc->len = fbufsrc->skip = 0;
    fbufsrc->id = __sync_fetch_and_add(&fbuf_count, 1);
}

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

fbuf_t *
fbuf_duplicate(fbuf_t *fbufsrc)
{
    fbuf_t *fbufdst = fbuf_create(fbufsrc->maxlen);
    if (!fbufdst)
        return NULL;

    if (fbufsrc->used)
        fbuf_add_binary(fbufdst, fbufsrc->data + fbufsrc->skip, fbufsrc->used);

    DEBUG_FBUF_INFO(fbufsrc, "original");
    DEBUG_FBUF_INFO(fbufdst, "duplicate");

    return fbufdst;
}

unsigned int
fbuf_extend(fbuf_t *fbuf, unsigned int newlen)
{
    char *p;

    newlen++; // Include room for a '\0' terminator

    // check if we already have enough space
    unsigned int available_space = fbuf->len - fbuf->skip;

    // Do we need to extend the buffer?
    if (newlen <= available_space) {
        return fbuf->len;
    } else if (fbuf->skip && newlen <= fbuf->len) {
        if (fbuf->used)
            memmove(fbuf->data, fbuf->data + fbuf->skip, fbuf->used+1);
        else
            fbuf->data[0] = '\0';
        fbuf->skip = 0;
        return fbuf->len;
    }

    if (fbuf->maxlen != FBUF_MAXLEN_NONE && newlen > fbuf->maxlen + 1) {
        errno = ENOMEM;
        return 0;
    }

    // Calculate the new size for the buffer
    // - Start with a minimum size.
    // - Grow quickly at first.
    // - Grow more slowly once the threshold has been reached.
    // - Cap the new length if it exceeds global maximum if set.
    unsigned int fastgrowsize = fbuf->fastgrowsize ? fbuf->fastgrowsize : FBUF_FASTGROWSIZE;
    unsigned int slowgrowsize = fbuf->slowgrowsize ? fbuf->slowgrowsize : FBUF_SLOWGROWSIZE;
    while (newlen > fbuf->len) {
        if (fbuf->len == 0)
            fbuf->len = fbuf->minlen ? fbuf->minlen : FBUF_MINLEN;
        else if (fbuf->len < fastgrowsize)
            fbuf->len = fastgrowsize;
        else
            fbuf->len += slowgrowsize;
    }

    if (fbuf->maxlen && fbuf->len > fbuf->maxlen)
        fbuf->len = fbuf->maxlen + 1;

    if (fbuf->len == 0)
        return 0;

    if (fbuf->skip) {
        p = (char *)malloc(fbuf->len);
        if (!p)
            return 0;

        if (fbuf->used)
            memcpy(p, fbuf->data + fbuf->skip, fbuf->used);
        fbuf->skip = 0;
    } else {
        p = (char *)realloc(fbuf->data, fbuf->len);
    }

    if (p) {
        if (!fbuf->data)
            p[0] = '\0';                // terminate the new buffer string
        fbuf->data = p;
        DEBUG_FBUF_INFO(fbuf, "extended");
        
        return fbuf->len;
    } else {
        return 0;
    }
}

unsigned int
fbuf_shrink(fbuf_t *fbuf)
{
    unsigned int newlen, len = fbuf->len;
    char *p;

    if (fbuf->skip) {
        if (fbuf->used)
            memmove(fbuf->data, fbuf->data + fbuf->skip, fbuf->used+1);
        else
            fbuf->data[0] = '\0';
        fbuf->skip = 0;
    }

    if (fbuf->used == 0)
        len = 0;

    unsigned int fastgrowsize = fbuf->fastgrowsize ? fbuf->fastgrowsize : FBUF_FASTGROWSIZE;
    unsigned int slowgrowsize = fbuf->slowgrowsize ? fbuf->slowgrowsize : FBUF_SLOWGROWSIZE;

    do {
        newlen = len;
        if (len <= fbuf->minlen)
            break;
        else if (len <= fastgrowsize)
            len /= 2;
        else
            len -= slowgrowsize;
    } while (len >= fbuf->used+1);
    // len is now the first size smaller than required, newlen is the last
    // size that fits the buffer.

    if (newlen == fbuf->len)                // nothing to be done
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

void
fbuf_clear(fbuf_t *fbuf)
{
    fbuf->used = 0;
    fbuf->skip = 0;
    if (fbuf->len > 0)
        fbuf->data[0] = '\0';
}

unsigned int fbuf_attach(fbuf_t *fbuf, char *buf, int len, int used)
{
    int previously_used = fbuf->used;
    free(fbuf->data);
    fbuf->skip = 0;
    if (used < len)
        fbuf->data = buf;
    else {
        fbuf->data = realloc(buf, used + 1);
        if (!fbuf->data)
            return 0;
    }
    fbuf->len = len;
    fbuf->used = used;
    fbuf->data[used] = '\0';
    return previously_used;
}

unsigned int fbuf_detach(fbuf_t *fbuf, char **buf, int *len)
{
    if (!fbuf->used)
        return 0;

    if (fbuf->skip)
        fbuf_shrink(fbuf);

    unsigned int used = fbuf->used;
    if (len)
        *len = fbuf->len;

    *buf = fbuf->data;

    fbuf->used = 0;
    fbuf->skip = 0;
    fbuf->len = 0;
    fbuf->data = NULL;

    return used;
}

void
fbuf_destroy(fbuf_t *fbuf)
{
    free(fbuf->data);
    fbuf->data = NULL;
    fbuf->used = fbuf->len = fbuf->skip = 0;

    DEBUG_FBUF_INFO(fbuf, "destroyed");
}

void
fbuf_free(fbuf_t *fbuf)
{
    free(fbuf->data);

#ifdef DEBUG_BUILD
    fbuf->data = NULL;
    fbuf->used = fbuf->len = fbuf->skip = 0;
#endif

    DEBUG_FBUF_INFO(fbuf, "destroyed");

    free(fbuf);
}

int
fbuf_add_binary(fbuf_t *fbuf, const char *data, int len)
{
    if (len <= 0 || !fbuf_extend(fbuf, fbuf->used+len))
        return -1;

    memcpy(fbuf->data+fbuf->skip+fbuf->used, data, len);
    fbuf->used += len;
    fbuf->data[fbuf->skip+fbuf->used] = 0;

    return len;
}

int
fbuf_add(fbuf_t *fbuf, const char *data)
{
    int datalen;

    if (!data || data[0] == '\0')        // nothing to be done
        return 0;

    datalen = strlen(data);
    return fbuf_add_binary(fbuf, data, datalen);
}

int
fbuf_prepend_binary(fbuf_t *fbuf, const char *data, int len)
{
    if (len <= 0 || !data)
        return 0; // nothing to be done

    if ((int)fbuf->skip >= len) {
        memcpy(fbuf->data + fbuf->skip - len, data, len);
        fbuf->skip -= len;
    } else {
        if (!fbuf_extend(fbuf, fbuf->used+len))
            return -1;

        memmove(fbuf->data+fbuf->skip + len,
                fbuf->data + fbuf->skip, fbuf->used+1);

        memcpy(fbuf->data + fbuf->skip, data, len);
    }

    fbuf->used += len;

    return len;
}

int
fbuf_prepend(fbuf_t *fbuf, const char *data)
{
    if (!data || data[0] == '\0')
        return 0; // nothing to be done
    return fbuf_prepend_binary(fbuf, data, strlen(data));
}

int
fbuf_add_ln(fbuf_t *fbuf, const char *data)
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

int
fbuf_concat(fbuf_t *fbufdst, fbuf_t *fbufsrc)
{
    int datalen;

    if (fbufsrc->used == 0)                // nothing to be done
        return 0;

    datalen = fbufsrc->used;
    if (!fbuf_extend(fbufdst, fbufdst->used+datalen))
        return -1;

    memcpy(fbufdst->data+fbufdst->skip+fbufdst->used, fbufsrc->data+fbufsrc->skip, datalen+1);
    fbufdst->used += datalen;

    return datalen;
}

int
fbuf_copy(fbuf_t *fbufsrc, fbuf_t *fbufdst)
{
    if (!fbuf_extend(fbufdst, fbufsrc->used))
        return -1;
    fbuf_clear(fbufdst);
    memcpy(fbufdst->data + fbufdst->skip, fbufsrc->data + fbufsrc->skip, fbufsrc->used);
    fbufdst->used = fbufsrc->used;
    fbufdst->data[fbufdst->skip+fbufdst->used] = 0;

    return fbufdst->used;
}

int
fbuf_set(fbuf_t *fbuf, const char *data)
{
    int datalen = strlen(data);

    if (!fbuf_extend(fbuf, datalen))
        return -1;

    memcpy(fbuf->data + fbuf->skip, data, datalen+1);
    fbuf->used = datalen;

    return datalen;
}

int
fbuf_nprintf(fbuf_t *fbuf, int max, const char *fmt, ...)
{
    va_list args;
    unsigned int n;

    if (!fbuf_extend(fbuf, fbuf->used+max))
        return -1;

    va_start(args, fmt);
    n = vsnprintf(fbuf->data + fbuf->skip + fbuf->used,
            fbuf->len - (fbuf->skip + fbuf->used), fmt, args);
    va_end(args);

    fbuf->used += n;
    fbuf->data[fbuf->skip + fbuf->used] = '\0'; // terminate the buffer string

    va_end(args);

    return n;
}

int
fbuf_printf(fbuf_t *fbuf, const char *fmt, ...)
{
    va_list args;
    unsigned int n;

    if (!fbuf_extend(fbuf, fbuf->used+FBUF_LINE_EST))
        return -1;

    va_start(args, fmt);
    n = vsnprintf(fbuf->data + fbuf->skip + fbuf->used,
            fbuf->len - (fbuf->skip + fbuf->used), fmt, args);
    va_end(args);

    if (n >= fbuf->len - (fbuf->skip + fbuf->used)) { // some chars were discarded
        // extend the buffer and try again
        if (!fbuf_extend(fbuf, fbuf->used+n)) {
            fbuf->data[fbuf->skip + fbuf->used] = '\0'; // chop off any added content
            return -1;
        }
        va_start(args, fmt);
        n = vsnprintf(fbuf->data + fbuf->skip + fbuf->used,
                fbuf->len - (fbuf->skip + fbuf->used), fmt, args);
        va_end(args);
    }

    fbuf->used += n;
    fbuf->data[fbuf->skip + fbuf->used] = '\0'; // terminate the buffer string

    va_end(args);

    return n;
}

int
fbuf_fread(fbuf_t *fbuf, FILE *file, unsigned int explen)
{
    int n;

    if (explen == 0)
        explen = FBUF_READ_EST;                        // one 80 char line and some

    if (!fbuf_extend(fbuf, fbuf->used+explen))
        return -1;

    n = fread(fbuf->data + fbuf->skip + fbuf->used, 1, explen, file);
    if (n > 0)
        fbuf->used += n;
    fbuf->data[fbuf->skip + fbuf->used] = '\0'; // terminate the buffer string

    return n;
}

int
fbuf_fread_ln(fbuf_t *fbuf, FILE *file)
{
    int initial = fbuf->used;

    while(!fbuf->used || fbuf->data[fbuf->skip + fbuf->used - 1] != '\n')
    {
        if (!fbuf_extend(fbuf, fbuf->used + 1))
            return -1;
        int rb = fread(fbuf->data + fbuf->skip + fbuf->used, 1, 1, file);
        if (rb > 0) {
            fbuf->used++;
        } else {
            return -1;
        }

    }

    fbuf->used--;
    fbuf->data[fbuf->skip + fbuf->used] = '\0'; // terminate the buffer string
    if (fbuf->used && fbuf->data[fbuf->skip + fbuf->used - 1] == '\r') {
        fbuf->used--;
        fbuf->data[fbuf->skip + fbuf->used] = '\0'; // terminate the buffer string
    }

    return fbuf->used - initial;
}

int
fbuf_read(fbuf_t *fbuf, int fd, unsigned int explen)
{
    int n;

    if (explen == 0)
        explen = FBUF_READ_EST; // one 80 char line and some

    if (!fbuf_extend(fbuf, fbuf->used + explen))
        return -1;

    n = read(fd, fbuf->data + fbuf->skip + fbuf->used, explen);
    if (n > 0)
        fbuf->used += n;
    fbuf->data[fbuf->skip + fbuf->used] = '\0'; // terminate the buffer string

    return n;
}

int
fbuf_read_ln(fbuf_t *fbuf, int fd)
{
    int initial = fbuf->used;

    while(!fbuf->used || fbuf->data[fbuf->skip + fbuf->used - 1] != '\n')
    {
        if (!fbuf_extend(fbuf, fbuf->used + 1))
            return -1;
        int rb = read(fd, fbuf->data + fbuf->skip + fbuf->used, 1);
        if (rb > 0) {
            fbuf->used++;
        } else {
            return -1;
        }

    }

    fbuf->used--;
    fbuf->data[fbuf->skip + fbuf->used] = '\0'; // terminate the buffer string
    if (fbuf->used && fbuf->data[fbuf->skip + fbuf->used - 1] == '\r') {
        fbuf->used--;
        fbuf->data[fbuf->skip + fbuf->used] = '\0'; // terminate the buffer string
    }

    return fbuf->used - initial;
}

int
fbuf_write(fbuf_t *fbuf, int fd, unsigned int nbytes)
{
    int n = 0;

    if (nbytes == 0)
        nbytes = fbuf->used;

    if (nbytes > fbuf->used)
        nbytes = fbuf->used;

    if (nbytes > 0) {
        n = write(fd, fbuf->data + fbuf->skip, nbytes);
        if (n > 0)
            fbuf_remove(fbuf, n);
    }

    return n;
}

int
fbuf_remove(fbuf_t *fbuf, unsigned int len)
{
    if (len >= fbuf->used) {
        fbuf->used = 0;
        fbuf->skip = 0;
        fbuf->data[0] = '\0'; // terminate the buffer string
    } else if (len) {
        fbuf->skip += len;
        fbuf->used -= len;
        if (fbuf->skip >= fbuf->len / 2) {
            memmove(fbuf->data, fbuf->data + fbuf->skip, fbuf->used+1);
            fbuf->skip = 0;
        }
    }
    return fbuf->used;
}

int
fbuf_trim(fbuf_t *fbuf)
{
    unsigned int i = 0;
    while (i < fbuf->used && isspace(fbuf->data[fbuf->skip + i]))
        i++;

    fbuf_remove(fbuf, i);

    return i;
}

int
fbuf_rtrim(fbuf_t *fbuf)
{
    unsigned int i = 0;

    while (fbuf->used > 0 && isspace(fbuf->data[fbuf->skip + fbuf->used - 1])) {
        fbuf->used -= 1;
        i++;
    }
    fbuf->data[fbuf->skip + fbuf->used] = '\0';

    return i;
}


char *
fbuf_data(fbuf_t *fbuf)
{
    return fbuf->data + fbuf->skip;
}

char *
fbuf_end(fbuf_t *fbuf)
{
    if (fbuf->data)
        return fbuf->data + fbuf->skip + fbuf->used;
    else
        return NULL;
}

int
fbuf_set_used(fbuf_t *fbuf, unsigned int newused)
{
    if (newused < fbuf->used) {
        fbuf->used = newused;
        fbuf->data[fbuf->skip + fbuf->used] = '\0';
    }

    return fbuf->used;
}

unsigned int
fbuf_used(fbuf_t *fbuf)
{
    return fbuf->used;
}

unsigned int
fbuf_len(fbuf_t *fbuf)
{
    return fbuf->len;
}

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
