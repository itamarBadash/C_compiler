int printf(const char *format, ...);

enum color { RED, GREEN = 5, BLUE };

static int square(int x) { return x * x; }

int add3(a, b, c) char a; short b; float c; { return a + b + (int)c; }

int apply(int (*f)(int), int v) { return f(v); }

long long fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }

int main(void) {
  int i = 7, j = -3;
  unsigned u = 10u;
  long l = -100;
  long long ll = 1234567890123LL;
  unsigned long long ull = 18446744073709551615ULL;
  short s = -2;
  unsigned short us = 65535;
  char c = 'A';
  signed char sc = -128;
  unsigned char uc = 200;
  _Bool b = 42;
  float f = 1.5f;
  double d = 2.25;
  long double ld = 3.125L;

  printf("int %d %d %d %d %d\n", i + j, i - j, i * j, i / j, i % j);
  printf("neg div %d %d\n", -7 / 2, -7 % 2);
  printf("unsigned %u %u %u\n", u / 3, u % 3, u - 11);
  printf("mixed %d\n", j < u);
  printf("long %ld %lld %llu\n", l * 3, ll * 7, ull / 3);
  printf("short %d %d %d\n", s * s, us + 1, (short)(us + 1));
  printf("char %d %d %d %c\n", c + 1, sc - 1, uc * 2, c + 2);
  printf("bool %d %d %d\n", b, (_Bool)0.5, (_Bool)(char *)0);
  printf("shift %d %d %u %lld\n", 1 << 10, -64 >> 3, 0x80000000u >> 31, 1LL << 40);
  printf("bits %d %d %d %d\n", 12 & 10, 12 | 3, 12 ^ 5, ~0);
  printf("cmp %d %d %d %d %d %d\n", i < j, i > j, i <= 7, i >= 8, i == 7, i != 7);
  printf("logic %d %d %d %d\n", i && j, i && 0, 0 || j, 0 || 0);
  printf("not %d %d %d\n", !i, !0, !!d);
  printf("float %f %f %f %f\n", f + d, f * 2, d / 3, -d);
  printf("ld %f %d\n", (double)(ld * 2), ld > 3);
  printf("fconv %d %u %f %f\n", (int)-2.9, (unsigned)3.9, (double)ull, (float)ll);
  printf("fcmp %d %d %d\n", f < d, f == 1.5, d != d);
  printf("sizes %d %d %d %d\n", (int)sizeof(int), (int)sizeof ld, (int)sizeof(long), (int)sizeof "abc");
  printf("enum %d %d %d\n", RED, GREEN, BLUE);
  printf("call %d %d %lld\n", square(9), apply(square, 5), fact(20));
  printf("knr %d\n", add3(1, 2, 3.7f));
  printf("ternary %d %f\n", i > 0 ? 10 : 20, j > 0 ? 1.0 : 2.5);

  i += 5; printf("+= %d\n", i);
  i -= 2; printf("-= %d\n", i);
  i *= 3; printf("*= %d\n", i);
  i /= 4; printf("/= %d\n", i);
  i %= 4; printf("%%= %d\n", i);
  i <<= 3; printf("<<= %d\n", i);
  i >>= 1; printf(">>= %d\n", i);
  i &= 6; printf("&= %d\n", i);
  i |= 9; printf("|= %d\n", i);
  i ^= 3; printf("^= %d\n", i);
  c += 300; printf("char += %d\n", c);
  uc -= 250; printf("uchar -= %d\n", uc);
  f *= 3; printf("float *= %f\n", f);
  d -= 1; printf("double -= %f\n", d);
  s = 3; s <<= 14; printf("short <<= %d\n", s);
  ll >>= 3LL; printf("ll >>= %lld\n", ll);

  int k = 5;
  int k1 = k++;
  int k2 = k;
  int k3 = ++k;
  printf("inc %d %d %d %d\n", k1, k2, k3, k);
  k1 = k--;
  k2 = k;
  k3 = --k;
  printf("dec %d %d %d %d\n", k1, k2, k3, k);
  b = 0; b++; printf("bool++ %d\n", b);
  b--; printf("bool-- %d\n", b);
  b--; printf("bool-- %d\n", b);
  d++; printf("double++ %f\n", d);
  uc = 255; uc++; printf("uchar++ %d\n", uc);
  printf("comma %d\n", (i = 3, i + 1));
  volatile int v = 4;
  v = v * 2;
  printf("volatile %d\n", v);
  return 0;
}
