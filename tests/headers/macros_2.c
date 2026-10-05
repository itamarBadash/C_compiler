#include <assert.h>
#include <complex.h>
#include <ctype.h>
#include <errno.h>
#include <fenv.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include <float.h>
#include <iso646.h>
void probe2(void) {
  void *p = NULL;
  FILE *e = stderr;
  void (*h)(int) = SIG_DFL;
  h = SIG_IGN;
  h = SIG_ERR;
  long v = WEOF + WCHAR_MIN + math_errhandling + MATH_ERRNO + MATH_ERREXCEPT + SEEK_SET + SEEK_CUR +
           SEEK_END + TMP_MAX + L_tmpnam + FOPEN_MAX + FILENAME_MAX + _IOFBF + _IOLBF + _IONBF +
           CHAR_BIT + SCHAR_MIN + LLONG_MIN + ULLONG_MAX + MB_LEN_MAX + INTPTR_MAX + SIZE_MAX +
           PTRDIFF_MIN + SIG_ATOMIC_MIN + WINT_MIN + INTMAX_MAX + INT_FAST8_MAX + INT_LEAST64_MIN +
           EDOM + EILSEQ + ERANGE + FE_DIVBYZERO + FE_INEXACT + FE_INVALID + FE_OVERFLOW +
           FE_UNDERFLOW + FE_TONEAREST + FE_DOWNWARD + FE_UPWARD + FE_TOWARDZERO + SIGABRT + SIGFPE +
           SIGILL + SIGSEGV + SIGTERM + LC_COLLATE + LC_CTYPE + LC_MONETARY + LC_NUMERIC + LC_TIME +
           EXIT_FAILURE + true + false + __bool_true_false_are_defined + INT8_C(1) + UINT64_C(1);
  const fenv_t *env = FE_DFL_ENV;
  const char *f = PRId64 PRIuMAX SCNx32 PRIxPTR;
  double d = isblank(1) + iscntrl(1) + isgraph(1) + islower(1) + isprint(1) + ispunct(1) + isspace(1) +
             isupper(1) + isxdigit(1) + toupper(1) + isalnum(1);
  d += iswalnum(1) + iswblank(1) + iswcntrl(1) + iswdigit(1) + iswgraph(1) + iswlower(1) +
       iswprint(1) + iswpunct(1) + iswspace(1) + iswupper(1) + iswxdigit(1) + towlower(1) + towupper(1);
  d += fabs(d) + sqrt(d) + fmax(d, d) + isnan(d) + isinf(1.0f) + isfinite(1.0L) + isnormal(1.0f);
  (void)p, (void)e, (void)v, (void)env, (void)f, (void)d;
}
static double stat_h = HUGE_VAL;
static float stat_i = INFINITY;
static float stat_n = NAN;
