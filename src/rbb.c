#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include "rbb.h"

#define RBB_DEFAULT_SIZE 4096

struct __rbb_s {
    u_char *buf;        // the buffer
    int size;           // buffer size
    int rfx;            // read offset
    int wfx;            // write offset
};


rbb_t *
rbb_create(int size) {
    rbb_t *new_rb;
    new_rb = (rbb_t *)calloc(1, sizeof(rbb_t));
    if(!new_rb) {
        /* TODO - Error Messaeggs */
        return NULL;
    }
    if(size == 0) 
        new_rb->size = RBB_DEFAULT_SIZE+1;
    else
        new_rb->size = size+1;
    new_rb->buf = (u_char *)malloc(new_rb->size);
    if(!new_rb->buf) {
        /* TODO - Error Messaeggs */
        free(new_rb);
        return NULL;
    }
    return new_rb;
}

void
rbb_skip(rbb_t *rb, int size) {
    if(size >= rb->size) { // just empty the ringbuffer
        rb->rfx = rb->wfx;
    } else {
        if (size > rb->size-rb->rfx) {
            size -= rb->size-rb->rfx;
            rb->rfx = size;
        } else {
            rb->rfx+=size;
        }
    }
}

int
rbb_read(rbb_t *rb, u_char *out, int size) {
    int read_size = rbb_len(rb); // never read more than available data
    int to_end = rb->size - rb->rfx;
    
    // requested size is less than stored data, return only what has been requested
    if(read_size > size)  
        read_size = size;

    if(read_size > 0) {
        // if the write pointer is beyond the read pointer or the requested read_size is 
        // smaller than the number of octets between the read pointer and the end of the buffer,
        // than we can safely copy all the octets in a single shot
        if(rb->wfx > rb->rfx || to_end >= read_size) {
            memcpy(out, &rb->buf[rb->rfx], read_size);
            rb->rfx += read_size;
        }
        else { // otherwise we have to wrap around the buffer and copy octest in two times
            memcpy(out, &rb->buf[rb->rfx], to_end);
            memcpy(out+to_end, &rb->buf[0], read_size - to_end);
            rb->rfx = read_size - to_end;
        }
    }

    return read_size;
}

int
rbb_write(rbb_t *rb, u_char *in, int size) {
    int write_size = rb->size - rbb_len(rb) - 1; // don't write more than available size

    if(!rb || !in || !size) // safety belt
        return 0;
    // if requested size fits the available space, use that
    if(write_size > size)  
        write_size = size;

    if(rb->wfx >= rb->rfx) { // write pointer is ahead
        if(write_size <= rb->size - rb->wfx) {
            memcpy(&rb->buf[rb->wfx], in, write_size);
            rb->wfx+=write_size;
        } else { // and we have to wrap around the buffer 
            int to_end = rb->size - rb->wfx;
            memcpy(&rb->buf[rb->wfx], in, to_end);
            memcpy(rb->buf, in+to_end, write_size - to_end);
            rb->wfx = write_size - to_end;
        }
    } else { // read pointer is ahead we can safely memcpy the entire chunk
        memcpy(&rb->buf[rb->wfx], in, write_size);
        rb->wfx+=write_size;
    }
    return write_size;
}

int
rbb_len(rbb_t *rb) {
    if(rb->wfx == rb->rfx)
        return 0;
    if(rb->wfx < rb->rfx) 
        return rb->wfx+(rb->size-rb->rfx);
    return rb->wfx-rb->rfx;
}

void
rbb_clear(rbb_t *rb) {
    rb->rfx = rb->wfx = 0;
}

void
rbb_destroy(rbb_t *rb) {
    if(rb->buf)
        free(rb->buf);
    free(rb);
}


int
rbb_find(rbb_t *rb, u_char octet) {
    int i;
    int to_read = rbb_len(rb);
    if (to_read == 0)
        return -1;

    if(rb->wfx > rb->rfx) {
        for (i = rb->rfx; i < rb->wfx; i++) {
            if(rb->buf[i] == octet)
                return(i-rb->rfx);
        }
    } else {
        for (i = rb->rfx; i < rb->size; i++) {
            if(rb->buf[i] == octet)
                return(i-rb->rfx);
        }
        for (i = 0; i < rb->wfx; i++) {
            if(rb->buf[i] == octet)
                return((rb->size-rb->rfx)+i);
        }
    }
    return -1;
}

int
rbb_read_until(rbb_t *rb, u_char octet, u_char *out, int maxsize)
{
    int i;
    int size = rbb_len(rb);
    int to_read = size;
    int found = 0;
    for (i = rb->rfx; i < rb->size; i++) {
        to_read--;
        if(rb->buf[i] == octet)  {
            found = 1;
            break;
        } else if ((size-to_read) == maxsize) {
            break;
        } else {
            out[i] = rb->buf[i];
        }
    }
    if(!found) {
        for (i = 0; to_read > 0 && (size-to_read) < maxsize; i++) {
            to_read--;
            if(rb->buf[i] == octet) {
                found = 1;
                break;
            }
            else {
                out[i] = rb->buf[i];
            }
            
        }
    }
    rbb_skip(rb, (size - to_read));
    return (size-to_read);
}

