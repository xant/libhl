#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <libgen.h>

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>
#include <ut.h>
#include "rbuf.h"

struct _rbuf_s {
    u_char *buf;        // the buffer
    int size;           // buffer size
    int available;      // buffer size
    int used;           // used size
    int rfx;            // read offset
    int wfx;            // write offset
    int mode;           // the ringbuffer mode (blocking/overwrite)
};

int
main (int argc, char **argv)
{
    u_char buffer1[16] = "0123456789ABCDEF";
    u_char buffer2[16] = "xxxxxxxxxxxxxxxx";
    u_char test[32];
    rbuf_t *rb;

    ut_init(basename(argv[0]));

    ut_testing("rbuf_create(24)");
    rb = rbuf_create(24);
    if(rb)
        ut_success();
    else
        ut_failure("Cant' create a new rbuf handler");

    ut_testing("rbuf_write(rb, buffer1, 16)");
    ut_validate_int(rbuf_write(rb, buffer1, 16), 16);

    ut_testing("rbuf_read(rb, test, 16)");
    rbuf_read(rb, test, 16);
    ut_validate_buffer(test, 16, buffer1, 16);

    memset(test, 0, sizeof(test));

    // data is wrapped around the ringbuffer
    ut_testing("rbuf_write(rb, buffer2 16)");
    ut_validate_int(rbuf_write(rb, buffer2, 16), 16);
    ut_testing("(rb->rfx > rb->wfx)");
    ut_validate_int((rb->rfx > rb->wfx), 1);
    ut_testing("rbuf_read(rb, test, 16)");
    rbuf_read(rb, test, 16);
    ut_validate_buffer(test, 16, buffer2, 16);

    ut_testing("rbuf_write(rb, buffer1, 16);");
    ut_validate_int( rbuf_write(rb, buffer1, 16), 16);
    ut_testing("rbuf_write(rb, buffer2, 16);");
    ut_validate_int( rbuf_write(rb, buffer2, 16), 8);


    //rbuf_data_dump(rb);
    // actual layout should be "0123456789ABCDEFxxxxxxx"
    ut_testing("rbuf_find(rb, 'x')");
    ut_validate_int(rbuf_find(rb, 'x'), 16);

    ut_testing("rbuf_clear(rb)");
    rbuf_clear(rb);
    // len must be 0 and both rfx and wfx at the beginning of the buffer (so either 0)
    ut_validate_int(rbuf_used(rb)|rb->rfx|rb->wfx, 0);
    // fill the buffer so that it looks like '0123456789ABCDEFxxxxxxxx'
    rbuf_write(rb, buffer1, 16);
    rbuf_write(rb, buffer2, 16);
    ut_testing("rbuf_read_until(rb, 'x', test, 24)");
    ut_validate_int(rbuf_read_until(rb, 'x', test, rb->size), 17); // 16bytes + first 'x' occurence
    ut_testing("rbuf_find(rb, 'x') == 0");
    ut_validate_int(rbuf_find(rb, 'x'), 0);
    // now check if rbuf_read_until is able to wrap around the buffer
    rbuf_write(rb, buffer1, 16);
    ut_testing("rb->rfx > rb->wfx");
    ut_validate_int((rb->rfx > rb->wfx), 1);

    ut_testing("rbuf_available(rb) == rbuf_size(rb) - rbuf_used(rb)");
    ut_validate_int(rbuf_available(rb), rbuf_size(rb) - rbuf_used(rb));

    ut_testing("rbuf_read_until(rb, 'E', test, 24)");
    ut_validate_int(rbuf_read_until(rb, 'E', test, rb->size), 22);
    // first byte now has to be 'F' (which follows the 'E' we looked for)
    ut_testing("rbuf_find(rb, 'F') == 0");
    ut_validate_int(rbuf_find(rb, 'F'), 0);
    ut_testing("rb->rfx < rb->wfx");
    ut_validate_int((rb->rfx < rb->wfx), 1);

    rbuf_clear(rb);
    ut_testing("rbuf_set_mode(rb, RBUF_MODE_OVERWRITE)");
    rbuf_set_mode(rb, RBUF_MODE_OVERWRITE);
    ut_validate_int(rbuf_mode(rb), RBUF_MODE_OVERWRITE);

    char bigbuffer[32] = "0123456789ABCDEFxxxxxxxxxxxxxxxx";

    ut_testing("rbuf_write(rb, \"0123456789ABCDEFxxxxxxxxxxxxxxxx\", 32)");
    ut_validate_int( rbuf_write(rb, bigbuffer, 32), 32);
    ut_testing("rbuf_find(rb, '8') == 0");
    ut_validate_int(rbuf_find(rb, '8'), 0);
    ut_testing("rbuf_used(rb) == 24");
    ut_validate_int(rbuf_used(rb), 24);


    ut_testing("rbuf_copy(rb, copy, 24)");
    rbuf_t *copy = rbuf_create(24);
    rbuf_copy(rb, copy, rbuf_used(rb));
    ut_validate_buffer(copy->buf, 24, rb->buf, 24);

    rbuf_t *move = rbuf_create(24);
    ut_testing("rbuf_move(copy, move, 24)");
    rbuf_move(copy, move, rbuf_used(copy));
    if (memcmp(move->buf, rb->buf, 24) == 0) {
        if (rbuf_used(copy) == 0) {
            ut_success();
        } else {
            ut_failure("rbuf_used(copy) != 0");
        }
    } else {
        ut_failure("move != rb");
    }

    rbuf_destroy(copy);
    rbuf_destroy(move);

    ut_testing("rbuf_available(rb) == rbuf_size(rb) - rbuf_used(rb) [ again ]");
    ut_validate_int(rbuf_available(rb), rbuf_size(rb) - rbuf_used(rb));

    ut_testing("rbuf_write(rb, \"XX\", 2)");
    ut_validate_int(rbuf_write(rb, "XX", 2), 2);

    ut_testing("rbuf_find(rb, 'A') == 0");
    ut_validate_int(rbuf_find(rb, 'A'), 0);
    ut_testing("rbuf_find(rb, 'X') == 22");
    ut_validate_int(rbuf_find(rb, 'X'), 22);


    rbuf_destroy(rb);

    ut_summary();

    return ut_failed;
}
