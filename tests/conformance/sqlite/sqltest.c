#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int row(void *unused, int count, char **values, char **names) {
  (void)unused;
  (void)names;
  for (int i = 0; i < count; i++)
    printf("%s%s", i ? "|" : "", values[i] ? values[i] : "NULL");
  printf("\n");
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  FILE *file = fopen(argv[1], "rb");
  if (!file)
    return 3;
  size_t capacity = 1 << 20, length = 0;
  char *sql = malloc(capacity);
  size_t got;
  while ((got = fread(sql + length, 1, capacity - length - 1, file)) > 0) {
    length += got;
    if (length + 1 == capacity)
      sql = realloc(sql, capacity *= 2);
  }
  sql[length] = '\0';
  fclose(file);
  sqlite3 *db;
  if (sqlite3_open(":memory:", &db) != SQLITE_OK)
    return 4;
  char *error = NULL;
  int status = sqlite3_exec(db, sql, row, NULL, &error);
  if (status != SQLITE_OK)
    printf("error: %s\n", error);
  sqlite3_free(error);
  sqlite3_close(db);
  free(sql);
  printf("version %s\n", sqlite3_libversion());
  return status;
}
