#include <stdio.h>
#include <math.h>
int main(int argc, char **argv) {
  printf("hello %s %d %.3f\n", argv[0] ? "world" : "", NUM, sqrt(2.0));
  return 3;
}
