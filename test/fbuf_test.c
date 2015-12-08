#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <limits.h>

#include <sys/types.h>
#include <libgen.h>

#include "fbuf.h"
#include "ut.h"

#define TESTFILENAME1        "fbuf_test.txt"
#define TESTFILENAME2        "fbuf_test.out"

#define FBUFMAXLEN 13

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
    ut_vfailure(fmt, args);
    const char *fdata = fbuf_data(fbuf);
    printf("  fbuf: data = %p, len = %u, maxlen = %u, used = %u, "
           " minlen = %u, slowgrowsize = %u, fastgrowsize = %u \n",
           fdata, fbuf->len, fbuf->maxlen, fbuf->used, fbuf->minlen,
           fbuf->slowgrowsize, fbuf->fastgrowsize);
    printf("  contents: ");
    if (fdata)
        printf("\"%s\" (%zub)\n", ascii_escape(fdata, strlen(fdata)), strlen(fdata));
    else
        printf("<empty>\n");

    return 0;
}

static int
fileopcheck(const char *filename, int n)
{
    if (n == -1) {
        ut_failure("%s", strerror(errno));
        return 0;
    } else {
        return 1;
    }
}

static int
validate(fbuf_t *fbuf, const char *data, int maxlen, int flags)
{
    // Flag bits
#   define NO_FLAGS                        0x00
#   define IGNORE_STRLEN_MISMATCH        0x01

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

    return ut_success();
}

int
main(int argc, char **argv)
{
    int i, n;
    unsigned int u;
    fbuf_t *fb1, *fb2, *fb3;
    int fd1, fd2;
    char *p;

    ut_init(basename(argv[0]));

    ut_testing("fbuf_create(%d)", FBUFMAXLEN);
    fb1 = fbuf_create(FBUFMAXLEN);
    validate(fb1, NULL, FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_create(%d)", FBUF_MAXLEN_NONE);
    fb2 = fbuf_create(FBUF_MAXLEN_NONE);
    validate(fb2, NULL, FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_add(fb1, \"Hello\")");
    fbuf_add(fb1, "Hello");
    validate(fb1, "Hello", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_add_binary(fb1, \" World\", 2)");
    fbuf_add_binary(fb1, " World", 2);
    validate(fb1, "Hello W", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_add_binary(fb1, \" World\", 0)");
    fbuf_add_binary(fb1, " World", 0);
    validate(fb1, "Hello W", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_add_binary(fb1, \" World\", -1)");
    fbuf_add_binary(fb1, " World", -1);
    validate(fb1, "Hello W", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_add_ln(fb1, \"orld!\")");
    fbuf_add_ln(fb1, "orld!");
    validate(fb1, "Hello World!\n", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_clear(fb1)");
    fbuf_clear(fb1);
    validate(fb1, "", FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_prepend(fb1, \"bla\")");
    fbuf_prepend(fb1, "bla");
    validate(fb1, "bla", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_prepend(fb1, \"Bla \")");
    fbuf_prepend(fb1, "Bla ");
    validate(fb1, "Bla bla", FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_set(fb1, \"Hello \")");
    fbuf_set(fb1, "Hello ");
    validate(fb1, "Hello ", FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_set(fb2, \"world\")");
    fbuf_set(fb2, "world");
    validate(fb2, "world", FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_concat(fb1, fb2)");
    fbuf_concat(fb1, fb2);
    validate(fb1, "Hello world", FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_move(fb1, fb2)");
    fbuf_move(fb1, fb2);
    validate(fb2, "Hello world", FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_swap(fb1, fb2)");
    fbuf_swap(fb1, fb2);
    validate(fb1, "Hello world", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_swap(fb1, fb2) (2)");
    validate(fb2, "", FBUFMAXLEN, NO_FLAGS);

    fbuf_minlen(fb1, 16);
    ut_testing("fbuf_add(fb1, \"<8>\") (honour maxlen)");
    // maxlen is 10, buflen is FBUF_MINLEN (16), and 11 bytes are used, so
    // adding 8 bytes exceeds the current buffer length and exceeds maxlen,
    // so the buffer cannot be extended.
    fbuf_add(fb1, "12345678");
    validate(fb1, "Hello world", FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_data(fb1)");
    p = fbuf_data(fb1);
    if (strcmp(p, "Hello world") != 0)
        ut_failure("'%s' should be 'Hello world'", p);
    else
        ut_success();
    ut_testing("fbuf_end(fb1)");
    if (strcmp(fbuf_end(fb1)-strlen("world"), "world") != 0)
        ut_failure("'%s' should be 'd'", fbuf_end(fb1)-strlen("world"));
    else
        ut_success();

    // fbuf2 now has the maxlen from fb1 , we need to reset it
    fbuf_maxlen(fb2, 0);

    ut_testing("fbuf_printf(fb2, \"%%s\", \"hello\")");
    fbuf_printf(fb2, "%s", "hello");
    validate(fb2, "hello", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_printf(fb2, \"%%s%%c\", \"World\", '!')");
    fbuf_printf(fb2, " %s%c", "World", '!');
    validate(fb2, "hello World!", FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_remove(fb2, 6)");
    fbuf_remove(fb2, 6);
    validate(fb2, "World!", FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_copy(fb2, fb1)");
    fbuf_copy(fb2, fb1);
    validate(fb1, "World!", FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_duplicate(fb1, fb2)");
    fb3 = fbuf_duplicate(fb1);
    validate(fb3, "World!", FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_free(fb3)");
    fbuf_free(fb3);
    ut_success();

    ut_testing("fbuf_clear(fb1)");
    fbuf_clear(fb1);
    validate(fb1, NULL, FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_trim(fb2 == \"World!\")");
    fbuf_trim(fb2);
    validate(fb2, "World!", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_trim(fb2 == \"\")");
    fbuf_clear(fb2);
    fbuf_trim(fb2);
    validate(fb2, "", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_trim(fb2 == \" \\t Hello world!\")");
    fbuf_set(fb2, " \t Hello world!");
    fbuf_trim(fb2);
    validate(fb2, "Hello world!", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_trim(fb2 == \"   \")");
    fbuf_set(fb2, "   ");
    fbuf_trim(fb2);
    validate(fb2, "", FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_rtrim(fb2 == \"World!\")");
    fbuf_set(fb2, "World!");
    fbuf_rtrim(fb2);
    validate(fb2, "World!", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_rtrim(fb2 == \"\")");
    fbuf_clear(fb2);
    fbuf_rtrim(fb2);
    validate(fb2, "", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_rtrim(fb2 == \"Hello world! \\t \")");
    fbuf_set(fb2, "Hello world! \t ");
    fbuf_rtrim(fb2);
    validate(fb2, "Hello world!", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_rtrim(fb2 == \"   \")");
    fbuf_set(fb2, "   ");
    fbuf_rtrim(fb2);
    validate(fb2, "", FBUFMAXLEN, NO_FLAGS);

    fd1 = open(TESTFILENAME1, O_RDONLY);
    if (fd1 == -1)
        err(1, "open(%s, O_RDONLY)", TESTFILENAME1);
    fd2 = open(TESTFILENAME2, O_RDWR|O_CREAT|O_TRUNC, 0644);
    if (fd2 == -1)
        err(1, "open(%s, O_RDONLY)", TESTFILENAME2);

    ut_testing("fbuf_read(fb2, fd1, 5)");
    fbuf_clear(fb2);
    fileopcheck(TESTFILENAME1, fbuf_read(fb2, fd1, 5));
        validate(fb2, "First", 0, NO_FLAGS);

    ut_testing("fbuf_write(fb2, fd1, 100)");
    fbuf_set(fb2, "Hello world!");
    fbuf_write(fb2, fd2, 100);
    lseek(fd2, 0, SEEK_SET);
    fbuf_read(fb2, fd2, 100);
    validate(fb2, "Hello world!", 0, NO_FLAGS);

    lseek(fd1, 0, SEEK_SET);
    fbuf_clear(fb1);
    fbuf_read_ln(fb1, fd1);
    ut_testing("fbuf_read_ln(fb1, fd1)");
    validate(fb1, "First line", 0, NO_FLAGS);

    fbuf_clear(fb1);
    fbuf_read_ln(fb1, fd1);
    ut_testing("fbuf_read_ln(fb1, fd1)");
    validate(fb1, "Second line", 0, NO_FLAGS);

    lseek(fd1, 0, SEEK_SET);

    FILE *f1 = fdopen(fd1, "r");
    fbuf_clear(fb1);
    fbuf_fread_ln(fb1, f1);
    ut_testing("fbuf_fread_ln(fb1, fd1)");
    validate(fb1, "First line", 0, NO_FLAGS);

    fbuf_clear(fb1);
    fbuf_fread_ln(fb1, f1);
    ut_testing("fbuf_fread_ln(fb1, fd1)");
    validate(fb1, "Second line", 0, NO_FLAGS);

    fclose(f1);

    ut_testing("fbuf_printf(fb2, \"%%d\") (100000x)");
    fbuf_clear(fb2);
    for (i = 0; i < 10000; i++)
        if (fbuf_printf(fb2, "%d", i) == -1)
            break;
    if (i < 10000) 
        ut_failure("fbuf_printf() failed: maxlen = %d", fbuf_maxlen(fb2, UINT_MAX));
    else if (fb2->used != 38890)
        ut_failure("length %d, should be 38890 ('%s')", fb2->used, fbuf_data(fb2));
    else
        ut_success();

    ut_testing("fbuf_extend() after fbuf_maxlen(fb2, 10)");
    fbuf_maxlen(fb2, 10);
    n = fbuf_extend(fb2, fbuf_len(fb2)+1);
    if (n != 0)
        ut_failure("fbuf_extend() returned %d instead of 0", n);
    else
        ut_success();

    fbuf_maxlen(fb2, 1000000);
    ut_testing("fbuf_shrink(fb2)");
    u = fbuf_len(fb2);
    fbuf_set(fb2, "Hello world!");
    if (fbuf_shrink(fb2) == u)
        ut_failure("buffer still %d bytes", u);
    else
        validate(fb2, NULL, FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_shrink(fb2) after clear");
    u = fbuf_len(fb2);
    fbuf_clear(fb2);
    if (fbuf_shrink(fb2) == 0)
        validate(fb2, NULL, FBUFMAXLEN, NO_FLAGS);
    else
        ut_failure("buffer still %d bytes", u);

    ut_testing("fbuf_add(fb2, \"LMOPQRSTVWXYZ\") after fbuf_maxlen(fb2, 5)");
    fbuf_set(fb2, "ABCDEFGHIJK");
    fbuf_shrink(fb2);
    fbuf_maxlen(fb2, 5);
    n = fbuf_add(fb2, "LMOPQRSTVWXYZ");
    if (n != -1)
        ut_failure("added while it should not have");
    else
        validate(fb2, "ABCDE", FBUFMAXLEN, NO_FLAGS);
    
    ut_testing("fbuf_printf(fb2, \"%%d%%s\", 1, \"LMOPQRSTVWXYZ\") after fbuf_maxlen(fb2, 5)");
    fbuf_set(fb2, "ABCDE");
    fbuf_shrink(fb2);
    fbuf_maxlen(fb2, 5);
    n = fbuf_printf(fb2, "%d%s", 1, "KLMOPQRSTVWXYZ");
    if (n != -1)
        ut_failure("added while it should not have");
    else
        validate(fb2, "ABCDE", FBUFMAXLEN, NO_FLAGS);

    ut_testing("fbuf_remove(fb2, 2) uses fbuf->skip");
    fbuf_remove(fb2, 2);
    if (memcmp(fbuf_data(fb2), "CDE", 3) == 0 && fb2->skip == 2)
        ut_success();
    else
        ut_failure("skip is not set or buffer is not equal to 'CDE'");

    ut_testing("fbuf_remove(fb2, 1) resets the skip offset because past half of the buffer");
    fbuf_remove(fb2, 1);
    if (memcmp(fbuf_data(fb2), "DE", 2) == 0 && fb2->skip == 0)
        ut_success();
    else
        ut_failure("skip is has not been set back to 0 or buffer is not equal to 'DE'");



    ut_testing("fbuf_destroy(fb1)");
    fbuf_destroy(fb1);
    validate(fb1, "", FBUFMAXLEN, NO_FLAGS);
    ut_testing("fbuf_free(...)");
    fbuf_free(fb1);
    fbuf_free(fb2);
    ut_success();

    ut_summary();

    return ut_failed;
}
