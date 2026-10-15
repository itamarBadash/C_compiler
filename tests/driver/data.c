#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int counter;
int bump(int by);

static const char *names[] = {"zero", "one", "two"};

int main(void) {
  errno = 0;
  strtol("99999999999999999999999", NULL, 10);
  fprintf(stdout, "%s %d %d\n", names[bump(2)], counter, errno == ERANGE);
  fputs("to stderr\n", stderr);
  return counter;
}
