#ifndef _SYMBOL_TABLE_H_
#define _SYMBOL_TABLE_H_

#include "ast.h"

typedef enum {
  SYMBOL_VAR,
  SYMBOL_FUNC,
  SYMBOL_TYPEDEF,
  SYMBOL_ENUM_CONSTANT,
  SYMBOL_TAG,
  SYMBOL_LABEL
} symbol_kind;

typedef enum { SCOPE_FILE, SCOPE_FUNCTION, SCOPE_BLOCK, SCOPE_PROTOTYPE } scope_kind;

typedef enum { LINKAGE_NONE, LINKAGE_INTERNAL, LINKAGE_EXTERNAL } linkage_kind;

typedef struct symbol {
  char *name;
  symbol_kind kind;
  type_info *type;
  linkage_kind linkage;
  int is_tentative;
  int is_defined;
  type_kind tag_kind;
  prim_kind enum_type;
  ast_node *definition;
  int has_value;
  long long value;
  long long size;
  int alignment;
  int has_flexible_member;
  int is_register;
  int has_static_storage;
  int external_declaration;
  int is_used;
  int unsupported_mode;
  source_loc loc;
  struct symbol *next;
  struct symbol *all_next;
} symbol;

typedef struct scope {
  struct scope *parent;
  scope_kind kind;
  symbol *ordinary_symbols;
  symbol *tag_symbols;
  symbol *label_symbols;
} scope;

typedef struct symbol_table {
  scope *current_scope;
  symbol *all_symbols;
} symbol_table;

symbol_table *symbol_table_create(void);
void symbol_table_destroy(symbol_table *table);

void symbol_table_enter_scope(symbol_table *table, scope_kind kind);
void symbol_table_leave_scope(symbol_table *table);

symbol *symbol_table_insert_ordinary(symbol_table *table, const char *name, symbol_kind kind,
                                     type_info *type, source_loc loc);
symbol *symbol_table_insert_tag(symbol_table *table, const char *name, type_info *type,
                                source_loc loc);
symbol *symbol_table_insert_label(symbol_table *table, const char *name, source_loc loc);

symbol *symbol_table_lookup_ordinary(symbol_table *table, const char *name);
symbol *symbol_table_lookup_ordinary_current(symbol_table *table, const char *name);
symbol *symbol_table_lookup_tag(symbol_table *table, const char *name);
symbol *symbol_table_lookup_tag_current(symbol_table *table, const char *name);
symbol *symbol_table_lookup_label(symbol_table *table, const char *name);

#endif