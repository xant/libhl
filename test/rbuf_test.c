#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>
#include "rbuf.h"
#include "testing.h"

struct __rbuf_s {
    u_char *buf;        // the buffer
    int size;           // buffer size
    int rfx;            // read offset
    int wfx;            // write offset
};


int
main (int argc, char **argv)
{
    u_char buffer1[16] = "0123456789ABCDEF";
    u_char buffer2[16] = "xxxxxxxxxxxxxxxx";
    u_char test[32];
    rbuf_t *rb;

    t_init();

    t_testing("rbuf_create(24)");
    rb = rbuf_create(24);
    if(rb)
        t_success();
    else
        t_failure("Cant' create a new rbuf handler");

    t_testing("rbuf_write(rb, buffer1, 16)");
    t_validate_int(rbuf_write(rb, buffer1, 16), 16);

    t_testing("rbuf_read(rb, test, 16)");
    rbuf_read(rb, test, 16);
    t_validate_buffer(test, 16, buffer1, 16);

    memset(test, 0, sizeof(test));

    // data is wrapped around the ringbuffer
    t_testing("rbuf_write(rb, buffer2 16)");
    t_validate_int(rbuf_write(rb, buffer2, 16), 16);
    t_testing("(rb->rfx > rb->wfx)");
    t_validate_int((rb->rfx > rb->wfx), 1);
    t_testing("rbuf_read(rb, test, 16)");
    rbuf_read(rb, test, 16);
    t_validate_buffer(test, 16, buffer2, 16);

    t_testing("rbuf_write(rb, buffer1, 16);");
    t_validate_int( rbuf_write(rb, buffer1, 16), 16);
    t_testing("rbuf_write(rb, buffer2, 16);");
    t_validate_int( rbuf_write(rb, buffer2, 16), 8);


    //rbuf_data_dump(rb);
    // actual layout should be "0123456789ABCDEFxxxxxxx"
    t_testing("rbuf_find(rb, 'x')");
    t_validate_int(rbuf_find(rb, 'x'), 16);

    t_testing("rbuf_clear(rb)");
    rbuf_clear(rb);
    // len must be 0 and both rfx and wfx at the beginning of the buffer (so either 0)
    t_validate_int(rbuf_len(rb)|rb->rfx|rb->wfx, 0);
    // fill the buffer so that it looks like '0123456789ABCDEFxxxxxxxx'
    rbuf_write(rb, buffer1, 16);
    rbuf_write(rb, buffer2, 16);
    t_testing("rbuf_read_until(rb, 'x', test, 24)");
    t_validate_int(rbuf_read_until(rb, 'x', test, rb->size), 17); // 16bytes + first 'x' occurence
    t_testing("rbuf_find(rb, 'x') == 0");
    t_validate_int(rbuf_find(rb, 'x'), 0);
    // now check if rbuf_read_until is able to wrap around the buffer
    rbuf_write(rb, buffer1, 16);
    t_testing("rb->rfx > rb->wfx");
    t_validate_int((rb->rfx > rb->wfx), 1);
    t_testing("rbuf_read_until(rb, 'E', test, 24)"); 
    t_validate_int(rbuf_read_until(rb, 'E', test, rb->size), 22); 
    // first byte now has to be 'F' (which follows the 'E' we looked for)
    t_testing("rbuf_find(rb, 'F') == 0"); 
    t_validate_int(rbuf_find(rb, 'F'), 0);
    t_testing("rb->rfx < rb->wfx");
    t_validate_int((rb->rfx < rb->wfx), 1);
    
    rbuf_clear(rb);
    t_testing("rbuf_set_mode(rb, RBUF_MODE_OVERWRITE)");
    rbuf_set_mode(rb, RBUF_MODE_OVERWRITE);
    t_validate_int(rbuf_mode(rb), RBUF_MODE_OVERWRITE);

    char bigbuffer[32] = "0123456789ABCDEFxxxxxxxxxxxxxxxx";

    t_testing("rbuf_write(rb, \"0123456789ABCDEFxxxxxxxxxxxxxxxx\", 32)");
    t_validate_int( rbuf_write(rb, bigbuffer, 32), 32);
    t_testing("rbuf_find(rb, '8') == 0");
    t_validate_int(rbuf_find(rb, '8'), 0);
    t_testing("rbuf_len(rb) == 24");
    t_validate_int(rbuf_len(rb), 24);
    
    t_testing("rbuf_write(rb, \"XX\", 2)");
    t_validate_int(rbuf_write(rb, "XX", 2), 2);

    t_testing("rbuf_find(rb, 'A') == 0");
    t_validate_int(rbuf_find(rb, 'A'), 0);
    t_testing("rbuf_find(rb, 'X') == 22");
    t_validate_int(rbuf_find(rb, 'X'), 22);



    t_summary();

    return t_failed;
}
