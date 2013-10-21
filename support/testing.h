#include <stdarg.h>

extern int t_tests;
extern int t_skipped;
extern int t_tobeimplemented;
extern int t_failed;

void t_init(void);
void t_section(const char *subtitle);
void t_testing(const char *fmt, ...);
void t_percentage(int percentage);
int t_success(void);
int t_skip(void);
int t_tbi(void);
int t_failure(const char *fmt, ...);
int t_vfailure(const char *fmt, va_list args);
void t_fatal(int err, const char *fmt, ...);
void t_vfatal(int err, const char *fmt, va_list args);
int t_rc(int rc1, int rc2);
int t_result(int r, const char *fmt, ...);
int t_vresult(int r, const char *fmt, va_list args);
void t_summary(void);
int t_validate_buffer(const char *result, int result_len, const char *orig, int orig_len);
int t_validate_string(const char *result, const char *orig);
int t_validate_double(double result, double orig);
int t_validate_int(int result, int orig);
