int printf(const char *format, ...);

static int sum(int n, int m, int a[n][m]) {
  int total = 0;
  for (int i = 0; i < n; i++)
    for (int j = 0; j < m; j++)
      total += a[i][j];
  return total;
}

static long long churn(int rounds, int size) {
  long long total = 0;
  for (int r = 0; r < rounds; r++) {
    char block[size];
    block[0] = (char)r;
    block[size - 1] = 1;
    total += block[size - 1];
    if (r % 3 == 0)
      continue;
    {
      int inner[size];
      inner[0] = r;
      total += inner[0] & 1;
    }
  }
  return total;
}

static int jumps(int rounds, int size) {
  int r = 0;
again:
  if (r < rounds) {
    double big[size];
    big[0] = r;
    r++;
    goto again;
  }
  while (1) {
    int scratch[size];
    scratch[0] = 1;
    if (r++ > rounds + 5)
      break;
  }
  return r;
}

int main(void) {
  int n = 3, m = 4;
  int grid[n][m];
  for (int i = 0; i < n; i++)
    for (int j = 0; j < m; j++)
      grid[i][j] = i * 10 + j;
  printf("grid %d %d %d\n", grid[2][3], (int)sizeof grid, (int)sizeof grid[0]);
  printf("sum %d\n", sum(n, m, grid));
  int (*p)[m] = grid;
  p++;
  printf("pointer %d %d\n", (*p)[2], (int)(p - grid));
  typedef int row[m];
  m = 100;
  row r;
  printf("typedef %d\n", (int)sizeof r);
  int k = 2;
  int size = sizeof(int[k++]);
  printf("sizeof %d %d\n", size, k);
  printf("churn %lld\n", churn(200000, 4096));
  printf("jumps %d\n", jumps(200000, 2048));
  struct pair {
    int a, b;
  };
  struct pair pairs[n];
  pairs[2].b = 7;
  printf("pairs %d %d\n", pairs[2].b, (int)sizeof pairs);
  return 0;
}
