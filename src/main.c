#include "driver.h"
#include "host.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  char *root = host_install_root();
  if (!root) {
    fprintf(stderr, "error: cannot find the compiler's own folder\n");
    return 1;
  }
  int result = driver_run(root, argc, argv);
  free(root);
  return result;
}
