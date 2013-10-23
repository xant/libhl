/*! \file
 *
 * Framework for displaying tests and test results in a formatted way.
 *
 * General usage:
 * \code
 * #include <stdarg.h>
 * #include <syslog.h>
 *
 * #include "clib.h"
 * #include "testing.h"
 * ...
 * static int
 * validate(args)
 * {
 *     if (check args for error condition) {
 *         t_failure("description of discrepency");
 *     } else if (check args for other error condition) {
 *         t_failure("description of other discrepancy");
 *     } else {
 *         t_success();
 *         return 1;
 *     }
 *     return 0;
 * }
 * ...
 * int
 * main(int argc, char **argv)
 * {
 * ...
 *      openlog(getprogname(), LOG_CONS|LOG_PERROR, LOG_DAEMON);
 *      setlogmask(LOG_UPTO((verbose>LOG_DEBUG? LOG_DEBUG:verbose)));
 *
 *     t_init();
 *
 * ...
 *     t_testing("function1(args1)");
 *     function1(args1);
 *     validate(args2);
 *
 *     t_testing("function2(args3)");
 *     function2(args3);
 *     validate(args4);
 *
 *     t_summary();                // print a summary and validate the test counters
 *
 *     return t_failed;            // return the error count
 * }
 * \endcode
 */

//#include <curses.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h> // for ioctl() on Darwin
#include <string.h>

#include "testing.h"
#include <stdarg.h>

#ifdef DMALLOC
#include "dmalloc.h"
#endif

#define DOUBLE_EPSILON          0x00000000001   //!< epsilon for comparing doubles

static int cols = 0;
static int rows = 0;
static int left_width;
static int right_width;
static int backspace = 0;

int t_tests = 0;        //!< Number of tests run
int t_sections = 0;        //!< Number of sections
int t_succeeded = 0;        //!< Number of succesfull tests
int t_skipped = 0;        //!< Number of skipped tests
int t_tobeimplemented = 0;    //!< Number of tests that are still to be implemented
int t_failed = 0;        //!< Number of failed tests

static char *hex_escape(const char *buf, int len) {
    int i;
    static char *str = NULL;

    str = realloc(str, (len*2)+4);
    strcpy(str, "0x");
    char *p = str+2;
    
    for (i = 0; i < len; i++) {
        sprintf(p, "%02x", buf[i]);
        p+=2;
    }
    return str;
}

/*!
 * \brief Initialise cols and rows variables.
 */
static void
t_windowsize(void)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
        cols = ws.ws_col;
        rows = ws.ws_row;
    } else {
        char *s;
        if ((s = getenv("COLUMNS")) == NULL || (cols = strtol(s, NULL, 0)) == 0
             || (s = getenv("LINES")) == NULL || (rows = strtol(s, NULL, 0)) == 0)
        {
            cols = 80;      // default values as a fallback option
            rows = 25;
        }
    }

    left_width = cols*0.60;
    right_width = cols - left_width;
}

/*!
 * \brief Initialise the lib.
 */
void
t_init()
{
#ifdef __FREEBSD
#if OSRELDATE > 600000
    _malloc_options = "AJX";
#else
    extern const char *malloc_options;
    malloc_options = "AJRX";        // Bomb on malloc failure.
#endif
#endif

    t_windowsize();

#ifndef __linux
    printf("==> Testing %s\n", getprogname());
#else
#endif
}

/*!
 * \brief Insert a section header.
 */
void
t_section(const char *subtitle)
{
    if (t_sections > 0)
    printf("\n");

    t_sections++;
    printf("==> Section %d: %s\n", t_sections, subtitle);
}

/*!
 * \brief announce what we are going to test.
 * \param fmt printf style format string
 * \param ... printf style arguments
 *
 * \note stdout is flushed after writing the message and indenting up to the
 * right column.
 */
void
t_testing(const char *fmt, ...)
{
    va_list args;
    int n = left_width;

    t_tests++;
    printf("% 3d ", t_tests);
    n -= 3;

    va_start(args, fmt);

    n -= vprintf(fmt, args);

    for (; n > 0; n--)
        putchar(' ');

    fflush (stdout);

    va_end(args);
}

/*!
 * \brief Print \c backspace \c \\b characters.
 */
static void
t_backspace(void)
{
    for (; backspace > 0; backspace--)
        printf("\b \b");
}

/*!
 * \brief Print a percentage value, including backspace chars.
 * \param percentage percentage
 */
void
t_percentage(int percentage)
{
    t_backspace();

    backspace = printf("%d%%", percentage);
    fflush(stdout);
}

/*!
 * \brief Announce a succesfull test.
 * \returns 1.
 */
int
t_success(void)
{
    t_backspace();

    printf("ok\n");
    fflush (stdout);

    t_succeeded++;

    return 1;
}

/*!
 * \brief Announce a skipped test.
 */
int
t_skip(void)
{
    t_backspace();

    printf("skipped\n");
    fflush(stdout);

    t_skipped++;

    return 1;
}

/*!
 * \brief Announce a to-be-implemented test.
 */
int
t_tbi(void)
{
    t_backspace();

    printf("to be implemented\n");
    fflush(stdout);

    t_tobeimplemented++;

    return 0;
}

/*!
 * \brief Announce a failed test, including an error message.
 * \param fmt printf style format string
 * \param ... printf style arguments
 * \returns 0.
 */
int
t_failure(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);

    t_vfailure(fmt, args);

    va_end(args);

    return 0;
}

/*!
 * \brief Announce a failed test, including an error message.
 * \param fmt vprintf style format string
 * \param args vprintf style arguments
 * \returns 0.
 */
int
t_vfailure(const char *fmt, va_list args)
{
    t_backspace();

    printf("FAILED ");
    vprintf(fmt, args);
    putchar('\n');
    fflush(stdout);

    t_failed++;

    return 0;
}

/*!
 * \brief Announce a fatal error, including an error message.
 * \param err error code to pass to exit()
 * \param fmt printf style format string
 * \param ... printf style arguments
 * \returns never.
 */
void
t_fatal(int err, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);

    t_vfatal(err, fmt, args);
}

/*!
 * \brief Announce a fatal error, including an error message.
 * \param err error code to pass to exit()
 * \param fmt vprintf style format string
 * \param args vprintf style arguments
 * \returns never.
 */
void
t_vfatal(int err, const char *fmt, va_list args)
{
    t_backspace();

    printf("FATAL: ");
    vprintf(fmt, args);
    putchar('\n');
    fflush (stdout);

    exit(err);
}

/*!
 * \brief Call t_success or t_vfailure depending on comparison of rc1 and rc2.
 * \param rc1 return value 1
 * \param rc2 return value 2
 * \returns rc1.
 *
 * Does printf("returned %d, should be %d", rc1, rc2) on failure.
 *
 * Example:
 * \code
 *     t_rc(strlen(""), 0);
 * \endcode
 */
int
t_rc(int rc1, int rc2)
{
    t_result(rc1 == rc2, "returned %d, should be %d", rc1, rc2);

    return rc1;
}

/*!
 * \brief Call t_success or t_vfailure depending on the boolean r.
 * \param r boolean result, indicating success if true or failure otherwise
 * \param fmt printf style format string
 * \param ... printf style arguments
 * \returns r.
 */
int
t_result(int r, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    t_vresult(r, fmt, args);
    va_end(args);

    return r;
}

/*!
 * \brief Call t_success or t_vfailure depending on the boolean r.
 * \param r boolean result, indicating success if true or failure otherwise
 * \param fmt vprintf style format string
 * \param args vprintf style arguments
 * \returns r.
 */
int
t_vresult(int r, const char *fmt, va_list args)
{
    if (r)
        t_success();
    else
        t_vfailure(fmt, args);

    return r;
}

/*!
 * \brief Display a summary of the test.
 */
void
t_summary(void)
{
    int r = t_succeeded+t_failed+t_skipped+t_tobeimplemented;

    if (t_tests == 0)
        t_fatal(99, "no tests");

    if (r != t_tests)
        t_fatal(99, "number of tests (%d) does not match number of results (%d)", t_tests, r);

    printf("==> Summary: %d tests, %d succeeded", t_tests, t_succeeded);

    if (t_failed)
        printf(", %d failed", t_failed);

    if (t_skipped)
        printf(", %d skipped", t_skipped);

    if (t_tobeimplemented)
        printf(", %d to be implemented", t_tobeimplemented);

    printf(".\n");
}

/*!
 * \brief Compare two buffers.
 * \param result result buffer
 * \param result_len length of result buffer
 * \param orig original buffer
 * \param orig_len length of original buffer
 * \returns 1 on success, 0 otherwise.
 */
int
t_validate_buffer(const char *result, int result_len, const char *orig, int orig_len)
{
    if (orig == NULL && result != NULL) {
        return t_failure("'%s' should be NULL", hex_escape(result, result_len));
    } else if (result == NULL && orig != NULL) {
        return t_failure("should not be NULL");
    } else if (result == NULL && result_len != 0) {
        return t_failure("NULL but len is not 0");
    } else if (result_len != orig_len) {
        return t_failure("result len == %d but should be %d", result_len, orig_len);
    } else if (result == NULL && orig == NULL) {
        return t_success();
    } else if (memcmp(result, orig, orig_len) != 0) {
        char *buf1 = strdup(hex_escape(result, result_len));
        char *buf2 = strdup(hex_escape(orig, orig_len));
        t_failure("'%s' should be '%s'", buf1, buf2);
        free(buf1);
        free(buf2);
        return 0;
    } else {
        return t_success();
    }
}

/*!
 * \brief Compare two strings.
 * \param result result string
 * \param orig original string
 * \returns 1 on success, 0 otherwise.
 */
int
t_validate_string(const char *result, const char *orig)
{
    if (orig == NULL && result != NULL) {
        return t_failure("should be NULL");
    } else if (result == NULL && orig != NULL) {
        return t_failure("should not be NULL");
    } else if (result == NULL && orig == NULL) {
        return t_success();
    } else if (strcmp(result, orig) != 0) {
        char *result2 = strdup(result);        // is result from *_escape functions in escape_test.c, so strdup it first.
        t_failure("'%s' should be '%s'", result2, orig);
        free(result2);
        return 0;
    } else {
        return t_success();
    }
}

/*!
 * \brief Compare two doubles.
 * \param result result double
 * \param orig original double
 * \returns 1 on success, 0 otherwise.
 * \note Compares the doubles with DOUBLE_EPSILON precision.
 */
int
t_validate_double(double result, double orig)
{
    if (fabs(result - orig) > DOUBLE_EPSILON) {
        return t_failure("%f should be %f", result, orig);
    } else {
        return t_success();
    }
}

/*!
 * \brief Compare two integers.
 * \param result result integer
 * \param orig original integer
 * \returns 1 on success, 0 otherwise.
 */
int
t_validate_int(int result, int orig)
{
    if (result != orig) { 
        return t_failure("%d should be %d", result, orig);
    } else {
        return t_success();
    }
}
