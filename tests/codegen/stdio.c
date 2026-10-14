#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct entry {
  const char *name;
  int score;
  double weight;
};

static int by_score(const void *a, const void *b) {
  const struct entry *x = a;
  const struct entry *y = b;
  return (x->score > y->score) - (x->score < y->score);
}

static struct entry make(const char *name, int score, double weight) {
  struct entry e;
  e.name = name;
  e.score = score;
  e.weight = weight;
  return e;
}

static int report(char *out, size_t size, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int written = vsnprintf(out, size, format, ap);
  va_end(ap);
  return written;
}

int main(void) {
  struct entry table[4];
  table[0] = make("delta", 40, 1.5);
  table[1] = make("alpha", 10, 2.25);
  table[2] = make("gamma", 30, 0.5);
  table[3] = make("beta", 20, 4.0);
  qsort(table, 4, sizeof table[0], by_score);
  for (int i = 0; i < 4; i++)
    printf("%-6s %3d %6.2f\n", table[i].name, table[i].score, table[i].weight);

  char buffer[64];
  int n = sprintf(buffer, "%d-%s-%c-%x-%o", 42, "text", 'z', 255, 8);
  printf("sprintf %d [%s]\n", n, buffer);
  n = report(buffer, 8, "%s %d %.3f", "clipped", 12345, 3.14159);
  printf("vsnprintf %d [%s]\n", n, buffer);
  printf("long double %.3Lf %Lg\n", 2.5L, 1.0L / 3);
  printf("long long %lld %llu\n", -1234567890123LL, 18446744073709551615ULL);
  printf("width [%5d] [%-5d] [%05d] [%+d]\n", 42, 42, 42, 42);

  char *copy = malloc(16);
  strcpy(copy, "heap");
  strcat(copy, "-copy");
  printf("%s %d %d\n", copy, (int)strlen(copy), strcmp(copy, "heap"));
  free(copy);
  printf("%ld %d %d\n", strtol("-1234", NULL, 10), atoi("99"), abs(-7));
  fputs("fputs line\n", stdout);
  puts("puts line");
  putchar('!');
  putchar('\n');
  fflush(stdout);
  return 0;
}
