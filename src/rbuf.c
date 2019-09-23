#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include "rbuf.h"

#define RBUF_DEFAULT_SIZE 4096

struct _rbuf_s {
    u_char *buf;        // the buffer
    int size;           // buffer size
    int used;           // used size
    int rfx;            // read offset
    int wfx;            // write offset
    int mode;           // the ringbuffer mode (blocking/overwrite)
};

rbuf_t *
rbuf_create(int size) {
    rbuf_t *new_rb;
    new_rb = (rbuf_t *)calloc(1, sizeof(rbuf_t));
    if(!new_rb) {
        /* TODO - Error Messaeggs */
        return NULL;
    }
    if(size == 0)
        new_rb->size = RBUF_DEFAULT_SIZE;
    else
        new_rb->size = size;
    new_rb->buf = (u_char *)malloc(new_rb->size);
    if(!new_rb->buf) {
        /* TODO - Error Messaeggs */
        free(new_rb);
        return NULL;
    }
    return new_rb;
}

void
rbuf_set_mode(rbuf_t *rbuf, rbuf_mode_t mode)
{
    rbuf->mode = mode;
}

rbuf_mode_t
rbuf_mode(rbuf_t *rbuf)
{
    return rbuf->mode;
}

void
rbuf_skip(rbuf_t *rb, int size) {
    if(size >= rb->used) { // just empty the ringbuffer
        rb->rfx = rb->wfx;
        rb->used = 0;
    } else {
        rb->used -= size;
        if (size > rb->size-rb->rfx) {
            size -= rb->size-rb->rfx;
            rb->rfx = size;
        } else {
            rb->rfx+=size;
        }
    }
}

int
rbuf_read(rbuf_t *rb, u_char *out, int size) {
    int read_size = size > rb->used ? rb->used : size;
    int to_end = rb->size - rb->rfx;
    if (read_size > to_end) { // check if we need to wrap around
        memcpy(out, &rb->buf[rb->rfx], to_end);
        int start_size = read_size - to_end;
        memcpy(out + to_end, &rb->buf[0], start_size);
        rb->rfx = start_size;
    } else {
        memcpy(out, &rb->buf[rb->rfx], read_size);
        rb->rfx += read_size;
    }
    rb->used -= read_size;
    return read_size;
}

int
rbuf_write(rbuf_t *rb, u_char *in, int size) {

    if(!rb || !in || !size) // safety belt
        return 0;

    int available_size = rb->size - rb->used;
    int to_end = rb->size - rb->wfx;
    int write_size = (size > available_size) ? available_size : size;
    if (write_size < size && rb->mode == RBUF_MODE_OVERWRITE) {
        if (size > rb->size) {
            // the provided buffer is bigger than the
            // ringbuffer itself. Since we are in overwrite mode,
            // only the last chunk will be actually stored.
            write_size = rb->size;
            in = in + (size - write_size);
            rb->rfx = 0;
            rb->wfx = 0;
            memcpy(rb->buf, in, write_size);
            rb->used = write_size;
            // NOTE: we still tell the caller we have written all the data
            // even if the initial part has been thrown away
            return size;
        }
        // we are in overwrite mode, so let's make some space
        // for the new data by advancing the read offset
        int diff = size - write_size;
        rb->rfx += diff;
        write_size += diff;
        if (rb->rfx >= rb->size)
            rb->rfx -= rb->size;
        rb->used -= diff;
    }
    

    if (write_size > to_end) {
        memcpy(&rb->buf[rb->wfx], in, to_end);
        int from_start = write_size - to_end;
        memcpy(&rb->buf[0], in + to_end, from_start);
        rb->wfx = from_start;
    } else {
        memcpy(&rb->buf[rb->wfx], in, write_size);
        rb->wfx += write_size;
    }
    rb->used += write_size;
    return write_size;
}

int
rbuf_used(rbuf_t *rb) {
    return rb->used;
}

int
rbuf_size(rbuf_t *rb) {
    return rb->size;
}

int
rbuf_available(rbuf_t *rb) {
    return rb->size - rb->used;
}

void
rbuf_clear(rbuf_t *rb) {
    rb->rfx = rb->wfx = 0;
    rb->used = 0;
}

void
rbuf_destroy(rbuf_t *rb) {
    free(rb->buf);
    free(rb);
}


int
rbuf_find(rbuf_t *rb, u_char octet) {
    int i;
    int to_read = rbuf_used(rb);
    if (to_read == 0)
        return -1;

    if(rb->wfx > rb->rfx) {
        for (i = rb->rfx; i <= rb->wfx; i++) {
            if(rb->buf[i] == octet)
                return(i-rb->rfx);
        }
    } else {
        for (i = rb->rfx; i < rb->size; i++) {
            if(rb->buf[i] == octet)
                return(i-rb->rfx);
        }
        for (i = 0; i <= rb->wfx; i++) {
            if(rb->buf[i] == octet)
                return((rb->size-rb->rfx)+i);
        }
    }
    return -1;
}

int
rbuf_read_until(rbuf_t *rb, u_char octet, u_char *out, int maxsize)
{
    int i;
    int j = 0;
    int size = rb->used;
    int to_read = size;
    int found = 0;
    for (i = rb->rfx; i < rb->size && (size-to_read) < maxsize; i++, j++) {
        to_read--;
        out[j] = rb->buf[i];
        if(rb->buf[i] == octet)  {
            found = 1;
            break;
        }
    }
    if(!found) {
        for (i = 0; to_read > 0 && (size-to_read) < maxsize; i++, j++) {
            to_read--;
            out[j] = rb->buf[i];
            if(rb->buf[i] == octet) {
                break;
            }
        }
    }
    rbuf_skip(rb, (size - to_read));
    return (size-to_read);
}

static int
rbuf_copy_internal(rbuf_t *src, rbuf_t *dst, int len, int move)
{
    if (!src || !dst || !len)
        return 0;

    int to_copy = rbuf_available(dst);
    if (len < to_copy)
        to_copy = len;

    int available = rbuf_used(src);
    if (available < to_copy)
        to_copy = available;

    int contiguous = (dst->wfx > dst->rfx)
                  ? dst->size - dst->wfx
                  : dst->rfx - dst->wfx;

    if (contiguous >= to_copy) {
        if (move) {
            rbuf_read(src, &dst->buf[dst->wfx], to_copy);
        } else {
            if (src->rfx < src->wfx) {
                memcpy(&dst->buf[dst->wfx], &src->buf[src->rfx], to_copy);
            } else {
                int to_end = src->size - src->rfx;
                memcpy(&dst->buf[dst->wfx], &src->buf[src->rfx], to_end);
                dst->wfx += to_end;
                memcpy(&dst->buf[dst->wfx], &src->buf[0], to_copy - to_end);
            }
        }
        dst->wfx += to_copy;
    } else {
        int remainder = to_copy - contiguous;
        if (move) {
            rbuf_read(src, &dst->buf[dst->wfx], contiguous);
            rbuf_read(src, &dst->buf[0], remainder);
        } else {
            if (src->rfx < src->wfx) {
                memcpy(&dst->buf[dst->wfx], &src->buf[src->rfx], contiguous);
                memcpy(&dst->buf[0], &src->buf[src->rfx + contiguous], remainder);
            } else {
                int to_end = src->size - src->rfx;
                if (to_end > contiguous) {
                    memcpy(&dst->buf[dst->wfx], &src->buf[dst->rfx], contiguous);
                    int diff = to_end - contiguous;
                    if (diff > remainder) {
                        memcpy(&dst->buf[0], &src->buf[dst->rfx + contiguous], remainder);
                    } else {
                        memcpy(&dst->buf[0], &src->buf[dst->rfx + contiguous], diff);
                        memcpy(&dst->buf[diff], &src->buf[0], remainder - diff);
                    }
                } else {
                    memcpy(&dst->buf[dst->wfx], &src->buf[dst->rfx], to_end);
                    int diff = contiguous - to_end;
                    if (diff) {
                        memcpy(&dst->buf[dst->wfx + to_end], &src->buf[0], diff);
                        memcpy(&dst->buf[0], &src->buf[diff], remainder);
                    }
                }
            }
        }
        dst->wfx = remainder;
    }
    dst->used = to_copy;
    return to_copy;
}

int
rbuf_move(rbuf_t *src, rbuf_t *dst, int len)
{
    return rbuf_copy_internal(src, dst, len, 1);
}

int
rbuf_copy(rbuf_t *src, rbuf_t *dst, int len)
{
    return rbuf_copy_internal(src, dst, len, 0);
}

// vim: tabstop=4 shiftwidth=4 expandtab:
/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
