#ifndef _SEMA_H_
#define _SEMA_H_

#include "ast.h"
#include "symbol_table.h"

int sema_check(symbol_table *table, ast_node *program);

#endif