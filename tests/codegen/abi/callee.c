#include "abi.h"
#include <stdarg.h>

struct s1 next_s1(struct s1 x) {
  x.a += 1;
  return x;
}
struct s2 next_s2(struct s2 x) {
  x.a += 2;
  return x;
}
struct s3 next_s3(struct s3 x) {
  x.a[0] += 1;
  x.a[2] += 3;
  return x;
}
struct s4 next_s4(struct s4 x) {
  x.a *= 2;
  return x;
}
struct s5 next_s5(struct s5 x) {
  x.a[4] += 5;
  return x;
}
struct s6 next_s6(struct s6 x) {
  x.a[2] -= 6;
  return x;
}
struct s7 next_s7(struct s7 x) {
  x.a[6] += 7;
  x.a[0] -= 1;
  return x;
}
struct s8 next_s8(struct s8 x) {
  x.b += x.a;
  return x;
}
struct s12 next_s12(struct s12 x) {
  x.c += x.a + x.b;
  return x;
}
struct s16 next_s16(struct s16 x) {
  x.b -= x.a;
  return x;
}
struct s24 next_s24(struct s24 x) {
  x.c += x.a * x.b;
  return x;
}
struct f1 next_f1(struct f1 x) {
  x.a *= 1.5f;
  return x;
}
struct f2 next_f2(struct f2 x) {
  x.b += x.a;
  return x;
}
struct f3 next_f3(struct f3 x) {
  x.c -= x.a;
  return x;
}
struct f4 next_f4(struct f4 x) {
  x.d += x.c;
  return x;
}
struct d1 next_d1(struct d1 x) {
  x.a /= 4;
  return x;
}
struct d2 next_d2(struct d2 x) {
  x.a += x.b;
  return x;
}
struct d3 next_d3(struct d3 x) {
  x.c += 0.25;
  return x;
}
struct mix next_mix(struct mix x) {
  x.a += 1;
  x.b *= 2;
  return x;
}
struct mix2 next_mix2(struct mix2 x) {
  x.a -= 1;
  x.b *= 3;
  return x;
}
struct fi next_fi(struct fi x) {
  x.a += x.b;
  return x;
}
struct ifl next_ifl(struct ifl x) {
  x.c += (float)x.a + x.b;
  return x;
}
struct cs next_cs(struct cs x) {
  x.c += 1;
  x.s -= 2;
  x.f *= 2;
  x.d += x.f;
  return x;
}
struct ld1 next_ld1(struct ld1 x) {
  x.a *= 3;
  return x;
}
struct ldi next_ldi(struct ldi x) {
  x.a += x.b;
  return x;
}
union u1 next_u1(union u1 x) {
  x.i += 1;
  return x;
}
union u2 next_u2(union u2 x) {
  x.d *= 2;
  return x;
}
union u3 next_u3(union u3 x) {
  x.f[2] += x.f[1];
  return x;
}
struct bits next_bits(struct bits x) {
  x.a += 1;
  x.b -= 100;
  x.c += 2;
  return x;
}
struct nested next_nested(struct nested x) {
  x.inner.b += (float)x.x;
  return x;
}
struct arr next_arr(struct arr x) {
  x.c[9] = x.c[0];
  return x;
}
struct packed next_packed(struct packed x) {
  x.i += x.c;
  return x;
}
struct unnamed next_unnamed(struct unnamed x) {
  x.a *= 4;
  return x;
}
union lu next_lu(union lu x) {
  x.l += 1;
  return x;
}
union ll next_ll(union ll x) {
  x.l *= 2;
  return x;
}
struct aligned next_aligned(struct aligned x) {
  x.b += x.a;
  return x;
}

char next_char(char x) {
  return (char)(x - 1);
}
signed char next_schar(signed char x) {
  return (signed char)(x - 1);
}
unsigned char next_uchar(unsigned char x) {
  return (unsigned char)(x + 1);
}
short next_short(short x) {
  return (short)(x - 1);
}
unsigned short next_ushort(unsigned short x) {
  return (unsigned short)(x + 1);
}
_Bool flip(_Bool x) {
  return !x;
}
float next_float(float x) {
  return x * 0.5f;
}
long double next_ld(long double x) {
  return x * 1.5L;
}
float _Complex next_cf(float _Complex x) {
  return x * 2.0f;
}
double _Complex next_cd(double _Complex x) {
  return x + 1.0i;
}
long double _Complex next_cl(long double _Complex x) {
  return x - 1.0L;
}

double many(int a, double b, struct mix c, int d, struct d2 e, int f, int g, struct s16 h, double i,
            double j, double k, double l, double m, double n, struct f2 o, int p, long double q,
            struct s3 r, struct d2 s, struct s12 t, char u, float v) {
  return a + 2 * b + 3 * (c.a + c.b) + 4 * d + 5 * (e.a + e.b) + 6 * f + 7 * g + 8 * (h.a + h.b) +
         9 * i + 10 * j + 11 * k + 12 * l + 13 * m + 14 * n + 15 * (o.a + o.b) + 16 * p +
         17 * (double)q + 18 * (r.a[0] + r.a[1] + r.a[2]) + 19 * (s.a + s.b) +
         20 * (t.a + t.b + t.c) + 21 * u + 22 * v;
}

long long four(struct s16 a, struct s16 b, struct s16 c, struct s16 d) {
  return a.a + 2 * b.b + 3 * c.a + 4 * d.b;
}

double five(struct d2 a, struct d2 b, struct d2 c, struct d2 d, struct d2 e) {
  return a.a + 2 * b.b + 3 * c.a + 4 * d.b + 5 * e.a + 6 * e.b;
}

struct s24 hidden_spill(int a, int b, int c, int d, struct s16 f) {
  struct s24 r;
  r.a = a + b + c;
  r.b = d;
  r.c = f.a * 10 + f.b;
  return r;
}

static double sum_list(const char *format, va_list ap) {
  double total = 0;
  for (int index = 1; *format; format++, index++) {
    double v = 0;
    switch (*format) {
    case 'i':
      v = va_arg(ap, int);
      break;
    case 'l':
      v = (double)va_arg(ap, long long);
      break;
    case 'd':
      v = va_arg(ap, double);
      break;
    case 'L':
      v = (double)va_arg(ap, long double);
      break;
    case 'p':
      v = *va_arg(ap, int *);
      break;
    case '3': {
      struct s3 x = va_arg(ap, struct s3);
      v = x.a[0] + x.a[1] + x.a[2];
      break;
    }
    case 'x': {
      struct s12 x = va_arg(ap, struct s12);
      v = x.a + x.b + x.c;
      break;
    }
    case 'X': {
      struct s24 x = va_arg(ap, struct s24);
      v = (double)(x.a + x.b + x.c);
      break;
    }
    case 'm': {
      struct mix x = va_arg(ap, struct mix);
      v = x.a + x.b;
      break;
    }
    case 'M': {
      struct mix2 x = va_arg(ap, struct mix2);
      v = x.a + x.b;
      break;
    }
    case 'f': {
      struct f2 x = va_arg(ap, struct f2);
      v = x.a + x.b;
      break;
    }
    case 'F': {
      struct f3 x = va_arg(ap, struct f3);
      v = x.a + x.b + x.c;
      break;
    }
    case 'D': {
      struct d2 x = va_arg(ap, struct d2);
      v = x.a + x.b;
      break;
    }
    case 'h': {
      struct ld1 x = va_arg(ap, struct ld1);
      v = (double)x.a;
      break;
    }
    case 'c': {
      double _Complex x = va_arg(ap, double _Complex);
      v = ((double *)&x)[0] + ((double *)&x)[1];
      break;
    }
    case 'k': {
      float _Complex x = va_arg(ap, float _Complex);
      v = ((float *)&x)[0] + ((float *)&x)[1];
      break;
    }
    }
    total += index * v;
  }
  return total;
}

double sum_format(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  double total = sum_list(format, ap);
  va_end(ap);
  return total;
}

double sum_twice(const char *format, ...) {
  va_list ap, again;
  va_start(ap, format);
  va_copy(again, ap);
  double first = sum_list(format, ap);
  double second = sum_list(format, again);
  va_end(again);
  va_end(ap);
  return first * 1000 + second;
}

double call_back(struct mix (*f)(struct mix, struct d2, long double, struct s24), struct mix m,
                 struct d2 d) {
  struct s24 big = {1, 2, 3};
  struct mix r = f(m, d, 0.5L, big);
  return r.a * 100 + r.b;
}

double call_variadic(void) {
  struct mix m = {5, 0.5};
  struct s24 big = {10, 20, 30};
  struct f2 f = {1.5f, 2.5f};
  struct ld1 h = {7.25L};
  return caller_format("imXfhLd", 3, m, big, f, h, 4.5L, 9.75);
}
