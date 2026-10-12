int printf(const char *format, ...);

int later();
static double half(x) float x; { return x / 2; }
static long double scale(long double x, int n) { while (n-- > 0) x *= 10; return x; }
static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static unsigned short widen(unsigned char c) { return c * 300; }
static _Bool positive(double d) { return d > 0; }

int main(void) {
  int caf\u00e9 = 5;
  printf("ucn %d\n", caf\u00e9 * 2);
  printf("later %d\n", later(4));
  printf("half %f\n", half(5.0f));
  printf("scale %f\n", (double)scale(1.25L, 3));
  printf("lower %c %c\n", lower('Q'), lower('7'));
  printf("widen %u\n", widen(250));
  printf("positive %d %d\n", positive(0.1), positive(-0.0));
  int puts(const char *);
  puts("block-scope declaration");
  printf("escapes [%s] %d %d\n", "tab\there \"q\" \\", '\n', '\377');
  printf("wide %d %d %d\n", (int)sizeof(L"ab"), (int)L'x', (int)sizeof L'x');
  unsigned short *w = (unsigned short *)L"hi";
  printf("wide unit %d\n", w[1]);
  char *p = (char *)0x1000;
  long n = (long)p;
  printf("ptrint %ld %d\n", n, (int)(p + 16 - (char *)0x1000));
  int *ip = (int *)0;
  ip += 3;
  ip -= 1;
  printf("ptr += %d\n", (int)(long)ip);
  printf("not %d %d %d\n", !p, !0.0, !ip);
  printf("bool %d %d\n", (_Bool)p, (_Bool)-0.0);
  long long big = 1;
  int sh = 3;
  printf("shift %lld %d %lld\n", big << sh, 1 << big, -16LL >> 2);
  unsigned char amount = 4;
  sh <<= amount;
  printf("shift= %d\n", sh);
  big <<= sh;
  printf("big %lld\n", big);
  char *q = n > 0 ? p : 0;
  printf("cond %d\n", q == p);
  double dd = 1e300;
  float ff = (float)dd;
  printf("inf %d\n", ff > 1e38f);
  printf("chain %d\n", 1 < 2 < 3);
  int a = 0, b = 0;
  a = b = 7;
  printf("assign %d %d\n", a, b);
  printf("mod %lld %llu\n", -9LL % 4, 9ULL % 4);
  printf("conv %d %d\n", (signed char)200, (unsigned char)-1);
  printf("float int %u\n", (unsigned)4000000000.0);
  printf("int float %f\n", (double)4000000000u);
  return later(1) - 1;
}

int later(int x) { return x * 10 - 9; }
