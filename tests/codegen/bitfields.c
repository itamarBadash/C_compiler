int printf(const char *format, ...);

struct flags {
  unsigned ready : 1;
  unsigned mode : 3;
  int delta : 5;
  unsigned char small : 2;
  _Bool on : 1;
  long long wide : 40;
  int : 0;
  short tail : 7;
};

#pragma pack(1)
struct packed {
  char c;
  int b : 30;
  long long straddle : 60;
};
#pragma pack()

static struct flags global_flags = {1, 5, -3, 2, 1, -123456789012LL, 9};

static void show(const char *label, struct flags *f) {
  printf("%s %u %u %d %d %d %lld %d\n", label, f->ready, f->mode, f->delta, f->small, f->on,
         f->wide, f->tail);
}

int main(void) {
  struct flags f = {0};
  show("zero", &f);
  f.ready = 1;
  f.mode = 9;
  f.delta = -16;
  f.small = 7;
  f.on = 5;
  f.wide = 0x7FFFFFFFFFLL;
  f.tail = -64;
  show("set", &f);
  f.delta++;
  f.mode += 3;
  f.wide <<= 1;
  f.small--;
  show("ops", &f);
  int r = (f.delta = 100);
  unsigned m = (f.mode = 12);
  printf("assigned %d %u %d\n", r, m, f.delta);
  int old = f.delta--;
  printf("post %d %d\n", old, f.delta);
  show("global", &global_flags);
  printf("sizes %d %d\n", (int)sizeof(struct flags), (int)sizeof(struct packed));

  struct packed p = {'x', -5, -1234567890123LL};
  printf("packed %c %d %lld\n", p.c, p.b, p.straddle);
  p.straddle += 7;
  p.b = 536870911;
  printf("packed %d %lld %c\n", p.b, p.straddle, p.c);

  struct flags table[2] = {{.mode = 2, .tail = 3}, {.delta = -1}};
  printf("table %u %d %d\n", table[0].mode, table[0].tail, table[1].delta);
  return 0;
}
