int printf(const char *format, ...);

int counter;
int counter;
static int hidden = 7;
int table[5] = {1, 2, 3};
const char *names[] = {"zero", "one", "two"};
char greeting[] = "hello";
char buffer[8];
double ratio = 2.75;
float half = 0.5f;
long double precise = 1.25L;
int *middle = &table[2];
const char *suffix = "abcdef" + 3;
int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
int (*row)[3] = matrix + 1;
short negative = -5;
unsigned long long big = 0xFFFFFFFFFFFFFFFFULL;
int *nothing = 0;
char *fixed = (char *)64;

struct entry {
  const char *key;
  int value;
  int (*hook)(int);
};

static int twice(int x) {
  return 2 * x;
}
static int thrice(int x) {
  return 3 * x;
}

struct entry entries[] = {{"a", 1, twice}, {"b", 2, thrice}, {0}};
struct entry *last = &entries[1];
int *literal = (int[]){10, 20, 30};
union mixed {
  int i;
  char c[4];
} mixed = {0x01020304};
int later_defined;
extern int defined_after;
int *forward = &defined_after;

int defined_after = 42;

static int next(void) {
  static int calls;
  static int base = 100;
  return base + ++calls;
}

static const char *where(void) {
  return __func__;
}

int main(void) {
  counter += 3;
  printf("counter %d %d\n", counter, hidden);
  printf("table %d %d %d\n", table[0], table[2], table[4]);
  printf("names %s %s\n", names[1], names[2]);
  printf("greeting %s %d\n", greeting, (int)sizeof greeting);
  buffer[0] = 'x';
  printf("buffer %c %d\n", buffer[0], buffer[7]);
  printf("floats %f %f %f\n", ratio, half, (double)precise);
  printf("middle %d suffix %s\n", *middle, suffix);
  printf("row %d\n", (*row)[1]);
  printf("negative %d big %llu\n", negative, big);
  printf("null %d fixed %d\n", nothing == 0, (int)(long long)fixed);
  for (struct entry *e = entries; e->key; e++)
    printf("entry %s %d\n", e->key, e->hook(e->value));
  printf("last %s\n", last->key);
  printf("literal %d %d\n", literal[1], literal[2]);
  printf("mixed %d\n", mixed.c[0]);
  printf("forward %d\n", *forward);
  next();
  next();
  printf("next %d\n", next());
  printf("func %s %s\n", where(), __func__);
  later_defined = 1;
  return 0;
}
