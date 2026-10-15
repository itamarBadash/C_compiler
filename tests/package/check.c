#include <complex.h>
#include <math.h>
#include <stdio.h>

int main(void) {
  double complex z = (1.0 + 2.0 * I) * (3.0 - I);
  printf("%.1f%+.1fi %.4f\n", creal(z), cimag(z), sqrt(2.0));
  return 5;
}
