struct s1 {
  char a;
};
struct s2 {
  short a;
};
struct s3 {
  char a[3];
};
struct s4 {
  int a;
};
struct s5 {
  char a[5];
};
struct s6 {
  short a[3];
};
struct s7 {
  char a[7];
};
struct s8 {
  int a, b;
};
struct s12 {
  int a, b, c;
};
struct s16 {
  long long a, b;
};
struct s24 {
  long long a, b, c;
};
struct f1 {
  float a;
};
struct f2 {
  float a, b;
};
struct f3 {
  float a, b, c;
};
struct f4 {
  float a, b, c, d;
};
struct d1 {
  double a;
};
struct d2 {
  double a, b;
};
struct d3 {
  double a, b, c;
};
struct mix {
  int a;
  double b;
};
struct mix2 {
  double a;
  int b;
};
struct fi {
  float a;
  int b;
};
struct ifl {
  int a;
  float b, c;
};
struct cs {
  char c;
  short s;
  float f;
  double d;
};
struct ld1 {
  long double a;
};
struct ldi {
  long double a;
  int b;
};
union u1 {
  int i;
  float f;
};
union u2 {
  double d;
  long long l;
};
union u3 {
  float f[3];
  int i;
};
struct bits {
  unsigned a : 3;
  int b : 9;
  unsigned c : 4;
};
struct nested {
  struct f2 inner;
  int x;
};
struct arr {
  char c[10];
};
#pragma pack(1)
struct packed {
  char c;
  int i;
};
#pragma pack()
struct unnamed {
  float a;
  int : 32;
};
union lu {
  long double l;
  double d[2];
};
union ll {
  long double l;
  long long x;
};
struct __attribute__((aligned(16))) aligned {
  long long a, b;
};

struct s1 next_s1(struct s1 x);
struct s2 next_s2(struct s2 x);
struct s3 next_s3(struct s3 x);
struct s4 next_s4(struct s4 x);
struct s5 next_s5(struct s5 x);
struct s6 next_s6(struct s6 x);
struct s7 next_s7(struct s7 x);
struct s8 next_s8(struct s8 x);
struct s12 next_s12(struct s12 x);
struct s16 next_s16(struct s16 x);
struct s24 next_s24(struct s24 x);
struct f1 next_f1(struct f1 x);
struct f2 next_f2(struct f2 x);
struct f3 next_f3(struct f3 x);
struct f4 next_f4(struct f4 x);
struct d1 next_d1(struct d1 x);
struct d2 next_d2(struct d2 x);
struct d3 next_d3(struct d3 x);
struct mix next_mix(struct mix x);
struct mix2 next_mix2(struct mix2 x);
struct fi next_fi(struct fi x);
struct ifl next_ifl(struct ifl x);
struct cs next_cs(struct cs x);
struct ld1 next_ld1(struct ld1 x);
struct ldi next_ldi(struct ldi x);
union u1 next_u1(union u1 x);
union u2 next_u2(union u2 x);
union u3 next_u3(union u3 x);
struct bits next_bits(struct bits x);
struct nested next_nested(struct nested x);
struct arr next_arr(struct arr x);
struct packed next_packed(struct packed x);
struct unnamed next_unnamed(struct unnamed x);
union lu next_lu(union lu x);
union ll next_ll(union ll x);
struct aligned next_aligned(struct aligned x);

char next_char(char x);
signed char next_schar(signed char x);
unsigned char next_uchar(unsigned char x);
short next_short(short x);
unsigned short next_ushort(unsigned short x);
_Bool flip(_Bool x);
float next_float(float x);
long double next_ld(long double x);
float _Complex next_cf(float _Complex x);
double _Complex next_cd(double _Complex x);
long double _Complex next_cl(long double _Complex x);

double many(int a, double b, struct mix c, int d, struct d2 e, int f, int g, struct s16 h, double i,
            double j, double k, double l, double m, double n, struct f2 o, int p, long double q,
            struct s3 r, struct d2 s, struct s12 t, char u, float v);

long long four(struct s16 a, struct s16 b, struct s16 c, struct s16 d);
double five(struct d2 a, struct d2 b, struct d2 c, struct d2 d, struct d2 e);
struct s24 hidden_spill(int a, int b, int c, int d, struct s16 f);

double sum_format(const char *format, ...);
double sum_twice(const char *format, ...);
double caller_format(const char *format, ...);

double call_back(struct mix (*f)(struct mix, struct d2, long double, struct s24), struct mix m,
                 struct d2 d);
double call_variadic(void);
