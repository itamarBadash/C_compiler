int printf(const char *format, ...);

static int count(int n, ...) { return n; }

static void say(const char *s) { printf("say %s\n", s); }

static int bump(int *p) { return ++*p; }

int main(int argc, char **argv) {
  int (*pf)(const char *, ...) = printf;
  pf("pf %d %s\n", count(3, 1.0, "x"), argc > 0 ? "args" : "none");
  int x = 0;
  x ? say("yes") : say("no");
  (void)bump(&x);
  (void)x;
  void *vp = &x;
  *(int *)vp;
  x;
  printf("x %d %d\n", x, argv[0] != 0);
  int i = 0;
  for (;;) {
    if (i == 3)
      break;
    i++;
  }
  for (i = 0; i < 2; i++)
    ;
  printf("i %d\n", i);
}
