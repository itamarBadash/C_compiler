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

struct sample {
  int tag;
  double _Complex value;
};

static double _Complex global = 1.5 + 2.5i;
static float _Complex fglobal = 3.0f - 1.0iF;
static long double _Complex lglobal = 2.0L + 0.5iL;
static struct sample samples[2] = {{1, 1.0 - 1.0i}, {2, 4.0i}};

int main(void) {
  double _Complex a = 1.0 + 2.0i, b = 3.0 - 4.0i;
  SHOW("add", a + b);
  SHOW("sub", a - b);
  SHOW("mul", a * b);
  SHOW("div", a / b);
  SHOW("neg", -a);
  SHOW("int", a + 2);
  SHOW("scale", 2.0 * a);
  SHOW("real", 7.25);

  float _Complex f = 1.5f + 2.0iF, g = -0.5f + 4.0iF;
  SHOWF("fmul", f * g);
  SHOWF("fdiv", f / g);
  SHOWF("fadd", f + g);
  long double _Complex l = 1.25L - 3.0iL, m = 2.0L + 0.5iL;
  SHOWL("lmul", l * m);
  SHOWL("ldiv", l / m);
  SHOW("widen", f);
  SHOWF("narrow", a);
  SHOWL("long", a);

  double re = a;
  int n = b;
  _Bool one = a, zero = 0.0 * a;
  _Bool imaginary_only = 3.0i;
  printf("conv %g %d %d %d %d\n", re, n, one, zero, imaginary_only);
  printf("eq %d %d %d %d\n", a == a, a == b, a != b, a == 1.0 + 2.0i);
  if (a)
    printf("truth\n");
  if (!(a - a))
    printf("not\n");

  double _Complex c = a;
  c *= b;
  c += 1;
  c /= 2.0i;
  c -= 0.5;
  SHOW("compound", c);
  SHOW("global", global);
  SHOWF("fglobal", fglobal);
  SHOWL("lglobal", lglobal);
  SHOW("sample", samples[1].value + samples[0].value);

  double _Complex arr[3] = {1.0, 2.0i, 3.0 + 3.0i};
  double _Complex sum = 0;
  for (int i = 0; i < 3; i++)
    sum += arr[i];
  SHOW("sum", sum);
  SHOW("pick", n > 0 ? a : b);
  SHOW("pick2", n < 0 ? a : b);

  double h = __builtin_huge_val();
  SHOW("inf", (h + 0.0i) * 1.0i);
  SHOW("zero", (1.0 + 1.0i) / (0.0 + 0.0i));
  SHOW("big", (1e300 + 1e300i) / (1e-300 + 1e300i));
  printf("sizes %d %d %d\n", (int)sizeof(float _Complex), (int)sizeof(double _Complex),
         (int)sizeof(long double _Complex));
  return 0;
}
