#include <setjmp.h>

int printf(const char *format, ...);

static jmp_buf env;
static int depth;

static void dive(int level) {
  depth++;
  if (level == 3)
    longjmp(env, 3);
  dive(level + 1);
  printf("never\n");
}

static int guarded(int value) {
  jmp_buf local;
  volatile int attempts = 0;
  if (setjmp(local) == 0) {
    attempts++;
    if (value > 0)
      longjmp(local, 1);
    return -1;
  }
  return attempts * value;
}

int main(void) {
  volatile int count = 0;
  switch (setjmp(env)) {
  case 0:
    count++;
    printf("first %d\n", count);
    dive(1);
    break;
  case 3:
    count++;
    printf("from dive %d %d\n", count, depth);
    longjmp(env, 7);
  case 7:
    count++;
    printf("from main %d\n", count);
    break;
  default:
    printf("unexpected\n");
  }
  printf("guarded %d %d\n", guarded(5), guarded(0));
  if (!setjmp(env))
    printf("not zero\n");
  return 0;
}
