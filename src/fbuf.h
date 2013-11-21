#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#ifndef WIN32
#include <sys/cdefs.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#define FBUF_MAXLEN_NONE	0	//!< No preferred maximum length for fbuf.
#define FBUF_STATIC_INITIALIZER { 0, NULL, 0, 0, 0 }

typedef struct __fbuf {
    unsigned int id;			//!< unique ID for the buffer for reference
    char *data;				//!< buffer
    unsigned int len;			//!< allocated length of buffer
    unsigned int prefmaxlen;		//!< preferred maximum size of buffer
    unsigned int used;			//!< number of bytes used in buffer
} fbuf_t;

fbuf_t *fbuf_create(unsigned int prefmaxlen);
void fbuf_move(fbuf_t *fbufsrc, fbuf_t *fbufdst);
void fbuf_swap(fbuf_t *fbuf1, fbuf_t *fbuf2);
fbuf_t *fbuf_duplicate(fbuf_t *fbufsrc);
unsigned int fbuf_extend(fbuf_t *fbuf, unsigned int newlen);
unsigned int fbuf_shrink(fbuf_t *fbuf);
void fbuf_clear(fbuf_t *fbuf);
void fbuf_destroy(fbuf_t *fbuf);
void fbuf_free(fbuf_t *fbuf);

int fbuf_add_binary(fbuf_t *fbuf, const char *data, int len);
int fbuf_add(fbuf_t *fbuf, const char *data);
int fbuf_add_nl(fbuf_t *fbuf, const char *data);
int fbuf_concat(fbuf_t *fbufdst, fbuf_t *fbufsrc);
int fbuf_copy(fbuf_t *fbufsrc, fbuf_t *fbufdst); 
int fbuf_set(fbuf_t *fbuf, const char *data);
int fbuf_printf(fbuf_t *fbuf, const char *fmt, ...);
int fbuf_fread(fbuf_t *fbuf, FILE *file, unsigned int explen);
int fbuf_read(fbuf_t *fbuf, int fd, unsigned int explen);
int fbuf_write(fbuf_t *fbuf, int fd, unsigned int explen);

int fbuf_remove(fbuf_t *fbuf, unsigned int len);
int fbuf_trim(fbuf_t *fbuf);

char *fbuf_data(fbuf_t *fbuf);
char *fbuf_end(fbuf_t *fbuf);
int fbuf_set_used(fbuf_t *fbuf, unsigned int newused);
unsigned int fbuf_used(fbuf_t *fbuf);
unsigned int fbuf_len(fbuf_t *fbuf);
unsigned int fbuf_set_pref_maxlen(fbuf_t *fbuf, unsigned int newprefmaxlen);
unsigned int fbuf_pref_maxlen(fbuf_t *fbuf);
unsigned int fbuf_set_maxlen(unsigned int maxlen);
unsigned int fbuf_maxlen(void);

#ifdef __cplusplus
}
#endif
