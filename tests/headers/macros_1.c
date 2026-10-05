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
struct probe_s { int a; double b; };
jmp_buf probe_jb;
int probe(int n, ...) {
  va_list ap, aq;
  va_start(ap, n);
  va_copy(aq, ap);
  int i = va_arg(ap, int);
  va_end(aq);
  va_end(ap);
  size_t off = offsetof(struct probe_s, b);
  double h = HUGE_VAL;
  float hf = HUGE_VALF;
  long double hl = HUGE_VALL;
  float inf = INFINITY;
  float nan = NAN;
  double x = 1.0, y = 2.0;
  int c = fpclassify(x) + isfinite(x) + isinf(x) + isnan(x) + isnormal(x) + signbit(x);
  c += isgreater(x, y) + isgreaterequal(x, y) + isless(x, y) + islessequal(x, y) +
       islessgreater(x, y) + isunordered(x, y);
  c += fpclassify(1.0f) + signbit(1.0L) + isnan(1.0f) + isinf(1.0L);
  double _Complex z = I + _Complex_I;
  complex double w = z;
  assert(n);
  if (setjmp(probe_jb))
    c++;
  errno = 0;
  c += isalpha(n) + tolower(n) + isdigit(n) + getc(stdin) + putc(n, stdout) + getchar() + putchar(n);
  c += getwc(stdin) + putwc(L'a', stdout) + iswalpha(n);
  c += FLT_EVAL_METHOD + math_errhandling + FP_ILOGB0 + FP_ILOGBNAN + FP_NAN + FP_INFINITE + FP_ZERO +
       FP_SUBNORMAL + FP_NORMAL + EOF + BUFSIZ + RAND_MAX + MB_CUR_MAX + CLOCKS_PER_SEC + SIG_ATOMIC_MAX +
       WCHAR_MAX + WINT_MAX + INT64_C(1) + UINTMAX_C(2) + EXIT_SUCCESS + SIGINT + LC_ALL + FE_ALL_EXCEPT;
  double_t dt = x;
  float_t ft = (float)y;
  return (int)(off + h + hf + hl + inf + nan + c + i + dt + ft);
}
