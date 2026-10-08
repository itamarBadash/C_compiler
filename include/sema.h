#ifndef _SEMA_H_
#define _SEMA_H_

#include "ast.h"
#include "symbol_table.h"

typedef struct type_layout {
  int known;
  long long size;
  int alignment;
} type_layout;

int sema_check(symbol_table *table, ast_node *program);
type_layout sema_type_layout(type_info *type);

#endif