#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <sys/types.h>

#include "fbuf.h"
#include "testing.h"

#define TESTFILENAME1	"test/fbuf_test.txt"
#define TESTFILENAME2	"test/fbuf_test.out"

#define FBUFMAXLEN 10

const unsigned char *
ascii_escape(const unsigned char *buf, int buflen)
{
    int i;

    static fbuf_t fbuf = FBUF_STATIC_INITIALIZER;

    fbuf_clear(&fbuf);

    for (i = 0; i < buflen; i++) {
	switch(buf[i]) {
	    case '\0':
		fbuf_add(&fbuf, "\\0");
		break;
            case '\b':
                fbuf_add(&fbuf, "\\b");
                break;
            case '\t':
                fbuf_add(&fbuf, "\\t");
                break;
            case '\n':
                fbuf_add(&fbuf, "\\n");
                break;
            case '\r':
                fbuf_add(&fbuf, "\\r");
                break;
            case '\\':
                fbuf_add(&fbuf, "\\\\");
                break;
            case ';':
                fbuf_add(&fbuf, "\\73");
                break;
            default:
                if(buf[i] <= 0x1f || buf[i] >= 0x7f) 
                    fbuf_printf(&fbuf, "\\%03o", buf[i]);
                else 
                    fbuf_printf(&fbuf, "%c", buf[i]);
        }
    }

    return fbuf_data(&fbuf);
}


static int
failure(fbuf_t *fbuf, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    t_vfailure(fmt, args);
    const char *fdata = fbuf_data(fbuf);
    printf("  fbuf: data = %p, len = %d, prefmaxlen = %d, used = %d\n",
	   fdata, fbuf->len, fbuf->prefmaxlen, fbuf->used);
    printf("  contents: ");
    if (fdata)
	printf("\"%s\" (%lub)\n", ascii_escape(fdata, strlen(fdata)), strlen(fdata));
    else
	printf("<empty>\n");

    return 0;
}

static int
fileopcheck(const char *filename, int n)
{
    if (n == -1) {
	t_failure("%s", strerror(errno));
	return 0;
    } else {
	return 1;
    }
}

static int
validate(fbuf_t *fbuf, const char *data, int prefmaxlen, int flags)
{
    // Flag bits
#   define NO_FLAGS			0x00
#   define IGNORE_STRLEN_MISMATCH	0x01

    if (fbuf->used > 0 && fbuf->used >= fbuf->len) {
	return failure(fbuf, "used > len (%d > %d)", fbuf->used, fbuf->len);
    } else if (data != NULL) {
        const char *fdata = fbuf_data(fbuf);
	if (strlen(data) == 0) {
	    if (fdata != NULL && strlen(fdata) != 0)
		return failure(fbuf, "buffer not empty: '%s'",
			       ascii_escape(fdata, strlen(fdata)));
	} else if (fdata == NULL || strlen(fdata) == 0) {
	    return failure(fbuf, "buffer empty, should be '%s'",
			   ascii_escape(data, strlen(data)));
	} else if (strcmp(fdata, data) != 0) {
	    char *buf1 = strdup(ascii_escape(fdata, strlen(fdata)));
	    char *buf2 = strdup(ascii_escape(data, strlen(data)));
	    failure(fbuf, "buffer is '%s' (%d), should be '%s' (%d)",
		    buf1, strlen(fdata),
		    buf2, strlen(data));
	    free(buf1);
	    free(buf2);
	    return 0;
	} else if (!(flags&IGNORE_STRLEN_MISMATCH) && strlen(fdata) != fbuf->used) {
	    return failure(fbuf, "strlen vs. used mismatch (%d vs. %d)",
			   strlen(fdata), fbuf->used);
	}
    }

    return t_success();
}

int
main(int argc, char **argv)
{
    int i, n;
    unsigned int u;
    fbuf_t *d1, *d2, *d3;
    int fd1, fd2;
    char *p;

    t_init();

    t_testing("fbuf_create(%d)", FBUFMAXLEN);
    d1 = fbuf_create(FBUFMAXLEN);
    validate(d1, NULL, FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_create(%d)", FBUF_MAXLEN_NONE);
    d2 = fbuf_create(FBUF_MAXLEN_NONE);
    validate(d2, NULL, FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_add(d1, \"Hello\")");
    fbuf_add(d1, "Hello");
    validate(d1, "Hello", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_add_binary(d1, \" World\", 2)");
    fbuf_add_binary(d1, " World", 2);
    validate(d1, "Hello W", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_add_binary(d1, \" World\", 0)");
    fbuf_add_binary(d1, " World", 0);
    validate(d1, "Hello W", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_add_binary(d1, \" World\", -1)");
    fbuf_add_binary(d1, " World", -1);
    validate(d1, "Hello W", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_add_nl(d1, \"orld!\")");
    fbuf_add_nl(d1, "orld!");
    validate(d1, "Hello World!\n", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_clear(d1)");
    fbuf_clear(d1);
    validate(d1, "", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_prepend(d1, \"bla\")");
    fbuf_prepend(d1, "bla");
    validate(d1, "bla", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_prepend(d1, \"Bla \")");
    fbuf_prepend(d1, "Bla ");
    validate(d1, "Bla bla", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_set(d1, \"Hello \")");
    fbuf_set(d1, "Hello ");
    validate(d1, "Hello ", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_set(d2, \"world\")");
    fbuf_set(d2, "world");
    validate(d2, "world", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_concat(d1, d2)");
    fbuf_concat(d1, d2);
    validate(d1, "Hello world", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_move(d1, d2)");
    fbuf_move(d1, d2);
    validate(d2, "Hello world", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_swap(d1, d2)");
    fbuf_swap(d1, d2);
    validate(d1, "Hello world", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_swap(d1, d2) (2)");
    validate(d2, "", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_add(d1, \"<8>\") (honour prefmaxlen)");
    // prefmaxlen is 10, buflen is FBUF_MINLEN (16), and 11 bytes are used, so
    // adding 8 bytes exceeds the current buffer length and exceeds prefmaxlen,
    // so the buffer cannot be extended.
    fbuf_add(d1, "12345678");
    validate(d1, "Hello world", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_data(d1)");
    p = fbuf_data(d1);
    if (strcmp(p, "Hello world") != 0)
	t_failure("'%s' should be 'Hello world'", p);
    else
	t_success();
    t_testing("fbuf_end(d1)");
    if (strcmp(fbuf_end(d1)-strlen("world"), "world") != 0)
	t_failure("'%s' should be 'd'", fbuf_end(d1)-strlen("world"));
    else
	t_success();

    t_testing("fbuf_printf(d2, \"%%s\", \"hello\")");
    fbuf_printf(d2, "%s", "hello");
    validate(d2, "hello", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_printf(d2, \"%%s%%c\", \"World\", '!')");
    fbuf_printf(d2, " %s%c", "World", '!');
    validate(d2, "hello World!", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_remove(d2, 6)");
    fbuf_remove(d2, 6);
    validate(d2, "World!", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_copy(d2, d1)");
    fbuf_copy(d2, d1);
    validate(d1, "World!", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_duplicate(d1, d2)");
    d3 = fbuf_duplicate(d1);
    validate(d3, "World!", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_free(d3)");
    fbuf_free(d3);
    t_success();

    t_testing("fbuf_clear(d1)");
    fbuf_clear(d1);
    validate(d1, NULL, FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_trim(d2 == \"World!\")");
    fbuf_trim(d2);
    validate(d2, "World!", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_trim(d2 == \"\")");
    fbuf_clear(d2);
    fbuf_trim(d2);
    validate(d2, "", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_trim(d2 == \" \\t Hello world!\")");
    fbuf_set(d2, " \t Hello world!");
    fbuf_trim(d2);
    validate(d2, "Hello world!", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_trim(d2 == \"   \")");
    fbuf_set(d2, "   ");
    fbuf_trim(d2);
    validate(d2, "", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_rtrim(d2 == \"World!\")");
    fbuf_set(d2, "World!");
    fbuf_rtrim(d2);
    validate(d2, "World!", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_rtrim(d2 == \"\")");
    fbuf_clear(d2);
    fbuf_rtrim(d2);
    validate(d2, "", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_rtrim(d2 == \"Hello world! \\t \")");
    fbuf_set(d2, "Hello world! \t ");
    fbuf_rtrim(d2);
    validate(d2, "Hello world!", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_rtrim(d2 == \"   \")");
    fbuf_set(d2, "   ");
    fbuf_rtrim(d2);
    validate(d2, "", FBUFMAXLEN, NO_FLAGS);

    fd1 = open(TESTFILENAME1, O_RDONLY);
    if (fd1 == -1)
	err(1, "open(%s, O_RDONLY)", TESTFILENAME1);
    fd2 = open(TESTFILENAME2, O_RDWR|O_CREAT|O_TRUNC, 0644);
    if (fd2 == -1)
	err(1, "open(%s, O_RDONLY)", TESTFILENAME2);

    t_testing("fbuf_read(d2, fd1, 5)");
    fbuf_clear(d2);
    fileopcheck(TESTFILENAME1, fbuf_read(d2, fd1, 5));
	validate(d2, "First", 0, NO_FLAGS);

    t_testing("fbuf_write(d2, fd1, 100)");
    fbuf_set(d2, "Hello world!");
    fbuf_write(d2, fd2, 100);
    lseek(fd2, 0, SEEK_SET);
    fbuf_read(d2, fd2, 100);
    validate(d2, "Hello world!", 0, NO_FLAGS);

    t_testing("fbuf_printf(d2, \"%%d\") (100000x)");
    fbuf_clear(d2);
    for (i = 0; i < 10000; i++)
	if (fbuf_printf(d2, "%d", i) == -1)
	    break;
    if (i < 10000) 
	t_failure("fbuf_printf() failed: prefmaxlen = %d", fbuf_prefmaxlen(d2));
    else if (d2->used != 38890)
	t_failure("length %d, should be 38890 ('%s')", d2->used, fbuf_data(d2));
    else
	t_success();

    t_testing("fbuf_extend() after fbuf_set_prefmaxlen(d2, 10)");
    fbuf_set_prefmaxlen(d2, 10);
    n = fbuf_extend(d2, fbuf_len(d2)+1);
    if (n != 0)
	t_failure("fbuf_extend() returned %d instead of -1", n);
    else
	t_success();

    fbuf_set_prefmaxlen(d2, 1000000);
    fbuf_set_maxlen(10000);
    t_testing("fbuf_extend() after fbuf_maxlen(10000)");
    n = fbuf_extend(d2, fbuf_len(d2)+1);
    if (n != 0)
	t_failure("fbuf_extend() returned %d instead of -1", n);
    else
	t_success();

    t_testing("fbuf_shrink(d2)");
    u = fbuf_len(d2);
    fbuf_set(d2, "Hello world!");
    if (fbuf_shrink(d2) == u)
	t_failure("buffer still %d bytes", u);
    else
	validate(d2, NULL, FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_shrink(d2) after clear");
    u = fbuf_len(d2);
    fbuf_clear(d2);
    if (fbuf_shrink(d2) == 0)
	validate(d2, NULL, FBUFMAXLEN, NO_FLAGS);
    else
	t_failure("buffer still %d bytes", u);

    t_testing("fbuf_add(d2, \"LMOPQRSTVWXYZ\") after fbuf_set_prefmaxlen(d2, 5)");
    fbuf_set(d2, "ABCDEFGHIJK");
    fbuf_shrink(d2);
    fbuf_set_prefmaxlen(d2, 5);
    n = fbuf_add(d2, "LMOPQRSTVWXYZ");
    if (n != -1)
	t_failure("added while it should not have");
    else
	validate(d2, "ABCDEFGHIJK", FBUFMAXLEN, NO_FLAGS);
    
    t_testing("fbuf_printf(d2, \"%%d%%s\", 1, \"LMOPQRSTVWXYZ\") after fbuf_set_prefmaxlen(d2, 5)");
    fbuf_set(d2, "ABCDEFGHIJK");
    fbuf_shrink(d2);
    fbuf_set_prefmaxlen(d2, 5);
    n = fbuf_printf(d2, "%d%s", 1, "KLMOPQRSTVWXYZ");
    if (n != -1)
	t_failure("added while it should not have");
    else
	validate(d2, "ABCDEFGHIJK", FBUFMAXLEN, NO_FLAGS);

    t_testing("fbuf_destroy(d1)");
    fbuf_destroy(d1);
    validate(d1, "", FBUFMAXLEN, NO_FLAGS);
    t_testing("fbuf_free(...)");
    fbuf_free(d1);
    fbuf_free(d2);
    t_success();

    t_summary();

    return t_failed;
}
