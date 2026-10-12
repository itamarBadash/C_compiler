int printf(const char *format, ...);

static int sum(const int *a, int n) {
  int total = 0;
  for (int i = 0; i < n; i++)
    total += a[i];
  return total;
}

static void fill(int grid[][4], int rows) {
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < 4; c++)
      grid[r][c] = r * 10 + c;
}

static int length(const char *s) {
  const char *p = s;
  while (*p)
    p++;
  return (int)(p - s);
}

static void swap(int *a, int *b) {
  int t = *a;
  *a = *b;
  *b = t;
}

static void reverse(char *s) {
  char *end = s + length(s) - 1;
  while (s < end) {
    char t = *s;
    *s++ = *end;
    *end-- = t;
  }
}

static int twice(int x) { return 2 * x; }
static int thrice(int x) { return 3 * x; }

int main(void) {
  int a[5] = {1, 2, 3};
  int b[] = {10, 20, 30, 40};
  int grid[3][4];
  char word[16] = "hello";
  char exact[3] = "abc";
  char text[] = "racecar!";
  double ds[3] = {0.5, [2] = 2.5};
  long long big[2] = {1, -1};
  int *p = a;
  int x = 1, y = 2;
  int (*ops[2])(int) = {twice, thrice};
  int (*op)(int) = &thrice;

  printf("a %d %d %d %d %d sum %d\n", a[0], a[1], a[2], a[3], a[4], sum(a, 5));
  printf("b %d %d\n", sum(b, 4), (int)(sizeof b / sizeof b[0]));
  fill(grid, 3);
  printf("grid %d %d %d\n", grid[0][3], grid[2][1], grid[1][0]);
  printf("word %s %d %c\n", word, length(word), word[5] + '0');
  printf("exact %c%c%c\n", exact[0], exact[1], exact[2]);
  reverse(text);
  printf("text %s\n", text);
  printf("ds %f %f %f\n", ds[0], ds[1], ds[2]);
  printf("big %lld %lld\n", big[0], big[1]);
  p += 2;
  printf("p %d %d %d\n", *p, p[-1], *(p + 1));
  printf("diff %d %d\n", (int)(p - a), (int)(&a[4] - p));
  printf("cmp %d %d %d\n", p > a, p == &a[2], p != a);
  swap(&x, &y);
  printf("swap %d %d\n", x, y);
  int *q = 0;
  printf("null %d %d\n", q == 0, !q);
  q = &x;
  *q += 5;
  printf("deref %d\n", x);
  printf("ops %d %d %d %d\n", ops[0](7), ops[1](7), op(4), (*op)(5));
  char *s = "literal";
  printf("s %c %s\n", s[3], s + 4);
  printf("idx %c\n", 2["xyz"]);
  const char *names[] = {"zero", "one", "two"};
  printf("names %s %s\n", names[1], names[2]);
  int m[2][3] = {{1, 2, 3}, {4, 5, 6}};
  int (*row)[3] = m + 1;
  printf("row %d %d\n", (*row)[2], row[0][0]);
  unsigned char bytes[4] = {1, 2, 3, 4};
  unsigned char *bp = bytes;
  bp++;
  printf("bytes %d %d\n", *bp, bp[2]);
  void *vp = &x;
  int *ip = vp;
  printf("void %d\n", *ip);
  long addr = (long)(char *)0 + 8;
  printf("addr %ld\n", addr);
  return 0;
}
