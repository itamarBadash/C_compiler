int printf(const char *format, ...);
int puts(const char *s);

static int classify(int n) {
  switch (n) {
  case 0:
    return 100;
  case 1:
  case 2:
    n += 10;
  case 3:
    return n;
  case -5:
    return -50;
  default:
    break;
  case 1000000:
    return 7;
  }
  return -1;
}

static const char *word(unsigned char c) {
  switch (c) {
  case 'a':
    return "alpha";
  case 255:
    return "max";
  }
  return "other";
}

static int gcd(int a, int b) {
  while (b != 0) {
    int t = a % b;
    a = b;
    b = t;
  }
  return a;
}

static int collatz(long long n) {
  int steps = 0;
  do {
    n = n % 2 ? 3 * n + 1 : n / 2;
    steps++;
  } while (n != 1);
  return steps;
}

static int search(int target) {
  int i, j;
  for (i = 0; i < 10; i++) {
    for (j = 0; j < 10; j++) {
      if (i * j == target)
        goto found;
    }
  }
  return -1;
found:
  return i * 100 + j;
}

static int sum_odd(int limit) {
  int total = 0;
  for (int i = 0;; i++) {
    if (i >= limit)
      break;
    if (i % 2 == 0)
      continue;
    total += i;
  }
  return total;
}

static void nothing(void) {
}

int main(void) {
  for (int i = -6; i < 6; i++)
    printf("%d ", classify(i));
  printf("%d %d\n", classify(1000000), classify(999));
  printf("%s %s %s\n", word('a'), word(255), word(3));
  printf("gcd %d %d\n", gcd(84, 36), gcd(17, 5));
  printf("collatz %d\n", collatz(27));
  printf("search %d %d\n", search(42), search(97));
  printf("odd %d\n", sum_odd(10));
  int n = 0;
  while (1) {
    if (++n > 4)
      break;
  }
  printf("n %d\n", n);
  int x = 0;
  if (x)
    puts("never");
  else if (n == 5)
    puts("five");
  else
    puts("never");
  nothing();
  int depth = 0;
  do
    depth++;
  while (depth < 3 && depth != 2);
  printf("depth %d\n", depth);
  {
    int x = 9;
    printf("inner %d\n", x);
  }
  printf("outer %d\n", x);
  int k = 3;
loop:
  if (k-- > 0)
    goto loop;
  printf("k %d\n", k);
  return n == 5 ? 42 : 1;
}
