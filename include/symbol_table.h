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

typedef enum {
    SCOPE_FILE,
    SCOPE_FUNCTION,
    SCOPE_BLOCK,
    SCOPE_PROTOTYPE
} scope_kind;

typedef enum {
    LINKAGE_NONE,
    LINKAGE_INTERNAL,
    LINKAGE_EXTERNAL
} linkage_kind;

typedef struct symbol {
    char *name;
    symbol_kind kind;
    type_info *type;
    linkage_kind linkage;
    int is_tentative;
    int is_defined;
    struct symbol *next;
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
} symbol_table;

symbol_table* symbol_table_create(void);
void symbol_table_destroy(symbol_table *table);

void symbol_table_enter_scope(symbol_table *table, scope_kind kind);
void symbol_table_leave_scope(symbol_table *table);

int symbol_table_insert_ordinary(symbol_table *table, const char *name, symbol_kind kind, type_info *type, linkage_kind linkage, int is_tentative, int is_defined);
int symbol_table_insert_tag(symbol_table *table, const char *name, type_info *type);
int symbol_table_insert_label(symbol_table *table, const char *name);

symbol* symbol_table_lookup_ordinary(symbol_table *table, const char *name);
symbol* symbol_table_lookup_tag(symbol_table *table, const char *name);
symbol* symbol_table_lookup_label(symbol_table *table, const char *name);

#endif