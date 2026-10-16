#include <complex.h>

int printf(const char *format, ...);

#define SHOW(label, z)                                                                             \
  do {                                                                                             \
    double _Complex shown = (z);                                                                   \
    printf("%s %g %g\n", label, ((double *)&shown)[0], ((double *)&shown)[1]);                    \
  } while (0)
#define SHOWF(label, z)                                                                            \
  do {                                                                                             \
    float _Complex shown = (z);                                                                    \
    printf("%s %g %g\n", label, (double)((float *)&shown)[0], (double)((float *)&shown)[1]);      \
  } while (0)
#define SHOWL(label, z)                                                                            \
  do {                                                                                             \
    long double _Complex shown = (z);                                                              \
    printf("%s %g %g\n", label, (double)((long double *)&shown)[0],                               \
           (double)((long double *)&shown)[1]);                                                    \
  } while (0)

static double _Complex make(double re, double im) {
  double _Complex z;
  ((double *)&z)[0] = re;
  ((double *)&z)[1] = im;
  return z;
}

static float _Complex makef(float re, float im) {
  float _Complex z;
  ((float *)&z)[0] = re;
  ((float *)&z)[1] = im;
  return z;
}

static long double _Complex makel(long double re, long double im) {
  long double _Complex z;
  ((long double *)&z)[0] = re;
  ((long double *)&z)[1] = im;
  return z;
}

struct sample {
  int tag;
  double _Complex value;
};

static double _Complex global = 1.5 + 2.5 * I;
static float _Complex fglobal = 3.0f - 1.0f * I;
static long double _Complex lglobal = 2.0L + 0.5L * I;
static struct sample samples[2] = {{1, 1.0 - 1.0 * I}, {2, 4.0 * I}};

int main(void) {
  double _Complex a = make(1.0, 2.0), b = make(3.0, -4.0);
  SHOW("add", a + b);
  SHOW("sub", a - b);
  SHOW("mul", a * b);
  SHOW("div", a / b);
  SHOW("neg", -a);
  SHOW("int", a + 2);
  SHOW("scale", 2.0 * a);
  SHOW("real", 7.25);

  float _Complex f = makef(1.5f, 2.0f), g = makef(-0.5f, 4.0f);
  SHOWF("fmul", f * g);
  SHOWF("fdiv", f / g);
  SHOWF("fadd", f + g);
  long double _Complex l = makel(1.25L, -3.0L), m = makel(2.0L, 0.5L);
  SHOWL("lmul", l * m);
  SHOWL("ldiv", l / m);
  SHOW("widen", f);
  SHOWF("narrow", a);
  SHOWL("long", a);

  double re = a;
  int n = b;
  _Bool one = a, zero = 0.0 * a;
  _Bool imaginary_only = make(0.0, 3.0);
  printf("conv %g %d %d %d %d\n", re, n, one, zero, imaginary_only);
  printf("eq %d %d %d %d\n", a == a, a == b, a != b, a == make(1.0, 2.0));
  if (a)
    printf("truth\n");
  if (!(a - a))
    printf("not\n");

  double _Complex c = a;
  c *= b;
  c += 1;
  c /= make(0.0, 2.0);
  c -= 0.5;
  SHOW("compound", c);
  SHOW("global", global);
  SHOWF("fglobal", fglobal);
  SHOWL("lglobal", lglobal);
  SHOW("sample", samples[1].value + samples[0].value);

  double _Complex arr[3] = {1.0, make(0.0, 2.0), make(3.0, 3.0)};
  double _Complex sum = 0;
  for (int i = 0; i < 3; i++)
    sum += arr[i];
  SHOW("sum", sum);
  SHOW("pick", n > 0 ? a : b);
  SHOW("pick2", n < 0 ? a : b);

  double h = __builtin_huge_val();
  SHOW("inf", make(h, 0.0) * make(0.0, 1.0));
  SHOW("zero", make(1.0, 1.0) / make(0.0, 0.0));
  SHOW("big", make(1e300, 1e300) / make(1e-300, 1e300));
  SHOW("unit", 2.0 * I);
  printf("sizes %d %d %d\n", (int)sizeof(float _Complex), (int)sizeof(double _Complex),
         (int)sizeof(long double _Complex));
  return 0;
}
