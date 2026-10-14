#include "abi.h"
#include <stdarg.h>

int printf(const char *format, ...);

static struct mix callback(struct mix m, struct d2 d, long double half, struct s24 big) {
  m.a = m.a * 10 + (int)big.c;
  m.b = m.b + d.a * d.b + (double)half;
  return m;
}

double caller_format(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  double total = 0;
  for (int index = 1; *format; format++, index++) {
    double v = 0;
    switch (*format) {
    case 'i':
      v = va_arg(ap, int);
      break;
    case 'd':
      v = va_arg(ap, double);
      break;
    case 'L':
      v = (double)va_arg(ap, long double);
      break;
    case 'm': {
      struct mix x = va_arg(ap, struct mix);
      v = x.a + x.b;
      break;
    }
    case 'X': {
      struct s24 x = va_arg(ap, struct s24);
      v = (double)(x.a + x.b + x.c);
      break;
    }
    case 'f': {
      struct f2 x = va_arg(ap, struct f2);
      v = x.a + x.b;
      break;
    }
    case 'h': {
      struct ld1 x = va_arg(ap, struct ld1);
      v = (double)x.a;
      break;
    }
    }
    total += index * v;
  }
  va_end(ap);
  return total;
}

int main(void) {
  struct s1 a1 = {10};
  struct s2 a2 = {-300};
  struct s3 a3 = {{1, 2, 3}};
  struct s4 a4 = {21};
  struct s5 a5 = {{1, 2, 3, 4, 5}};
  struct s6 a6 = {{100, 200, 300}};
  struct s7 a7 = {{1, 2, 3, 4, 5, 6, 7}};
  struct s8 a8 = {3, 4};
  struct s12 a12 = {1, 2, 3};
  struct s16 a16 = {100000000000LL, 3};
  struct s24 a24 = {2, 3, 4};
  struct f1 b1 = {2.5f};
  struct f2 b2 = {1.25f, 2.5f};
  struct f3 b3 = {1, 2, 3};
  struct f4 b4 = {1, 2, 3, 4};
  struct d1 c1 = {9};
  struct d2 c2 = {1.5, 2.25};
  struct d3 c3 = {1, 2, 3};
  struct mix m = {7, 1.75};
  struct mix2 m2 = {3.5, 4};
  struct fi fi = {1.5f, 2};
  struct ifl ifl = {3, 0.5f, 0.25f};
  struct cs cs = {'a', 300, 1.25f, 2.5};
  struct ld1 l1 = {1.25L};
  struct ldi li = {2.5L, 3};
  union u1 v1;
  union u2 v2;
  union u3 v3;
  struct bits bf = {5, -77, 9};
  struct nested ne = {{1.5f, 2.5f}, 4};
  struct arr ar = {{'q', 'r', 's'}};
  struct packed pk = {'A', 1000};
  v1.i = 41;
  v2.d = 1.125;
  v3.f[0] = 1;
  v3.f[1] = 2;
  v3.f[2] = 3;

  a1 = next_s1(a1);
  a2 = next_s2(a2);
  a3 = next_s3(a3);
  a4 = next_s4(a4);
  a5 = next_s5(a5);
  a6 = next_s6(a6);
  a7 = next_s7(a7);
  a8 = next_s8(a8);
  a12 = next_s12(a12);
  a16 = next_s16(a16);
  a24 = next_s24(a24);
  printf("ints %d %d %d %d %d %d %d %d %d %d %d %lld %lld %lld\n", a1.a, a2.a, a3.a[0], a3.a[2],
         a4.a, a5.a[4], a6.a[2], a7.a[0], a7.a[6], a8.b, a12.c, a16.a, a16.b, a24.c);
  b1 = next_f1(b1);
  b2 = next_f2(b2);
  b3 = next_f3(b3);
  b4 = next_f4(b4);
  c1 = next_d1(c1);
  c2 = next_d2(c2);
  c3 = next_d3(c3);
  printf("floats %g %g %g %g %g %g %g\n", b1.a, b2.b, b3.c, b4.d, c1.a, c2.a, c3.c);
  m = next_mix(m);
  m2 = next_mix2(m2);
  fi = next_fi(fi);
  ifl = next_ifl(ifl);
  cs = next_cs(cs);
  printf("mixed %d %g %g %d %g %g %d %d %g %g\n", m.a, m.b, m2.a, m2.b, fi.a, ifl.c, cs.c, cs.s,
         cs.f, cs.d);
  l1 = next_ld1(l1);
  li = next_ldi(li);
  v1 = next_u1(v1);
  v2 = next_u2(v2);
  v3 = next_u3(v3);
  bf = next_bits(bf);
  ne = next_nested(ne);
  ar = next_arr(ar);
  pk = next_packed(pk);
  printf("other %g %g %d %g %g %u %d %u %g %c %d\n", (double)l1.a, (double)li.a, v1.i, v2.d,
         v3.f[2], bf.a, bf.b, bf.c, ne.inner.b, ar.c[9], pk.i);
  printf("member %d %g\n", next_s12(a12).b, next_mix(m).b);
  struct unnamed un = {1.5f};
  union lu lu;
  union ll ll;
  struct aligned al = {5, 6};
  lu.l = 2.5L;
  ll.l = 3.25L;
  un = next_unnamed(un);
  lu = next_lu(lu);
  ll = next_ll(ll);
  al = next_aligned(al);
  printf("unions %g %g %g %lld\n", un.a, (double)lu.l, (double)ll.l, al.b);

  printf("small %d %d %d %d %d %d %d\n", next_char(-5), next_schar(-128 + 1), next_uchar(254),
         next_short(-32767), next_ushort(65534), flip(0), flip(1));
  float _Complex zf = 1.5f + 2.0iF;
  double _Complex zd = 3.0 + 4.0i;
  long double _Complex zl = 5.0L + 6.0iL;
  zf = next_cf(zf);
  zd = next_cd(zd);
  zl = next_cl(zl);
  printf("scalars %g %g %g %g %g %g %g %g\n", next_float(3.0f), (double)next_ld(2.0L),
         ((float *)&zf)[0], ((float *)&zf)[1], ((double *)&zd)[0], ((double *)&zd)[1],
         (double)((long double *)&zl)[0], (double)((long double *)&zl)[1]);

  struct d2 e = {0.5, 0.25};
  struct s16 h = {1, 2};
  struct f2 o = {0.5f, 1.5f};
  struct s3 r = {{1, 2, 3}};
  struct d2 s = {2, 4};
  struct s12 t = {1, 1, 1};
  printf("many %g\n",
         many(1, 2, m, 3, e, 4, 5, h, 6, 7, 8, 9, 10, 11, o, 12, 13.5L, r, s, t, 'z', 0.5f));

  struct s16 q1 = {1, 2}, q2 = {3, 4}, q3 = {5, 6}, q4 = {7, 8};
  struct d2 w1 = {1, 2}, w2 = {3, 4}, w3 = {5, 6}, w4 = {7, 8}, w5 = {9, 10};
  struct s24 hs = hidden_spill(1, 2, 3, 4, q4);
  printf("registers %lld %g %lld %lld %lld\n", four(q1, q2, q3, q4), five(w1, w2, w3, w4, w5), hs.a,
         hs.b, hs.c);

  int seven = 7;
  printf("variadic %g\n", sum_format("ildLp3xXmMfFDhck", 1, 2LL, 3.5, 4.25L, &seven, a3, a12, a24,
                                     m, m2, b2, b3, c2, l1, zd, zf));
  printf("spill %g\n", sum_format("ddddddddddiiiiiiiimDf", 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0,
                                  9.0, 10.0, 1, 2, 3, 4, 5, 6, 7, 8, m, c2, b2));
  printf("twice %g\n", sum_twice("imd", 4, m, 0.5));
  printf("callback %g\n", call_back(callback, m, c2));
  printf("call variadic %g\n", call_variadic());
  return 0;
}
