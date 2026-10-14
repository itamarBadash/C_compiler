#include <tgmath.h>

int printf(const char *format, ...);

struct record {
  int a;
  char b[10];
  struct {
    int x[4];
  } inner[3];
};

static int fixed = __builtin_offsetof(struct record, inner[1].x[2]);

int main(void) {
  int i = 2;
  printf("offsetof %d %d %d %d\n", (int)__builtin_offsetof(struct record, b[3]), fixed,
         (int)__builtin_offsetof(struct record, inner[i].x[i + 1]),
         (int)__builtin_offsetof(struct record, inner[i].x[1]));
  printf("compatible %d %d %d\n", __builtin_types_compatible_p(int, const int),
         __builtin_types_compatible_p(int, long), __builtin_types_compatible_p(int[3], int[]));

  int x = 5;
  double y = 2.5;
  printf("choose %g %g\n", (double)__builtin_choose_expr(1, x, y),
         (double)__builtin_choose_expr(0, x, y));
  __builtin_choose_expr(1, x, y) = 9;
  __builtin_choose_expr(0, x, y) += 1;
  int *where = &__builtin_choose_expr(1, x, y);
  printf("lvalue %d %g %d\n", x, y, *where);

  printf("inf %g %g %g %g\n", __builtin_huge_val(), (double)__builtin_huge_valf(),
         (double)__builtin_huge_vall(), (double)__builtin_inff());
  volatile float nan = __builtin_nanf("");
  printf("nan %d\n", nan != nan);

  volatile double one = 1.0, two = 2.0, nothing = nan;
  printf("order %d %d %d %d %d %d\n", __builtin_isgreater(two, one), __builtin_isgreaterequal(one, one),
         __builtin_isless(two, one), __builtin_islessequal(one, two),
         __builtin_islessgreater(one, two), __builtin_isunordered(one, two));
  printf("nan order %d %d %d %d %d %d\n", __builtin_isgreater(nothing, one),
         __builtin_isgreaterequal(nothing, one), __builtin_isless(nothing, one),
         __builtin_islessequal(one, nothing), __builtin_islessgreater(nothing, one),
         __builtin_isunordered(nothing, one));
  printf("mixed %d\n", __builtin_isless(1, 2.5f));

  volatile float nf = -1.0f, pf = 1.0f, zf = -0.0f;
  volatile double nd = -2.0, pd = 2.0;
  volatile long double nl = -3.0L, pl = 3.0L;
  printf("signbit %d %d %d %d %d %d %d %d\n", __builtin_signbitf(nf), __builtin_signbitf(pf),
         __builtin_signbitf(zf), __builtin_signbit(nd), __builtin_signbit(pd),
         __builtin_signbitl(nl), __builtin_signbitl(pl), __builtin_signbit(nf));
  volatile long long big = -5;
  printf("llabs %lld %lld\n", __builtin_llabs(big), __builtin_llabs(7));

  printf("tgmath %g %g %g %g\n", (double)sqrt(2.0f), sqrt(2), (double)fabs(-3.5f), pow(2, 10));
  printf("float sqrt %d\n", (int)sizeof(sqrt(2.0f)));

  if (i == 100)
    __builtin_trap();
  if (i == 101)
    __builtin_unreachable();
  printf("done\n");
  return 0;
}
