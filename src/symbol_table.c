#include "symbol_table.h"
#include <stdlib.h>
#include <string.h>

#define INDEX_BUCKETS 1024
#define INDEX_THRESHOLD 64

static symbol *find_in_list(symbol *list, const char *name) {
  for (symbol *curr = list; curr; curr = curr->next) {
    if (strcmp(curr->name, name) == 0)
      return curr;
  }
  return NULL;
}

static symbol **bucket_of(scope *s, const char *name) {
  unsigned long hash = 5381;
  for (const unsigned char *c = (const unsigned char *)name; *c; c++)
    hash = hash * 33 + *c;
  return &s->ordinary_index[hash % INDEX_BUCKETS];
}

static void build_index(scope *s) {
  s->ordinary_index = (symbol **)calloc(INDEX_BUCKETS, sizeof(symbol *));
  if (!s->ordinary_index)
    return;
  for (symbol *sym = s->ordinary_symbols; sym; sym = sym->next) {
    symbol **end = bucket_of(s, sym->name);
    while (*end)
      end = &(*end)->bucket_next;
    sym->bucket_next = NULL;
    *end = sym;
  }
}

static void add_ordinary(scope *s, symbol *sym) {
  sym->next = s->ordinary_symbols;
  s->ordinary_symbols = sym;
  s->ordinary_count++;
  if (s->ordinary_index) {
    symbol **bucket = bucket_of(s, sym->name);
    sym->bucket_next = *bucket;
    *bucket = sym;
  } else if (s->ordinary_count >= INDEX_THRESHOLD) {
    build_index(s);
  }
}

static symbol *find_ordinary(scope *s, const char *name) {
  if (!s->ordinary_index)
    return find_in_list(s->ordinary_symbols, name);
  for (symbol *curr = *bucket_of(s, name); curr; curr = curr->bucket_next) {
    if (strcmp(curr->name, name) == 0)
      return curr;
  }
  return NULL;
}

static symbol *new_symbol(symbol_table *table, const char *name, symbol_kind kind, type_info *type,
                          source_loc loc) {
  symbol *sym = (symbol *)calloc(1, sizeof(symbol));
  if (!sym)
    return NULL;
  if (name) {
    sym->name = strdup(name);
    if (!sym->name) {
      free(sym);
      return NULL;
    }
  }
  sym->kind = kind;
  sym->type = type;
  sym->loc = loc;
  sym->all_next = table->all_symbols;
  table->all_symbols = sym;
  return sym;
}

symbol_table *symbol_table_create(void) {
  return (symbol_table *)calloc(1, sizeof(symbol_table));
}

void symbol_table_destroy(symbol_table *table) {
  if (!table)
    return;
  while (table->current_scope) {
    symbol_table_leave_scope(table);
  }
  symbol *curr = table->all_symbols;
  while (curr) {
    symbol *next = curr->all_next;
    free(curr->name);
    free(curr);
    curr = next;
  }
  free(table);
}

void symbol_table_enter_scope(symbol_table *table, scope_kind kind) {
  if (!table)
    return;
  scope *new_scope = (scope *)calloc(1, sizeof(scope));
  if (!new_scope)
    return;
  new_scope->parent = table->current_scope;
  new_scope->kind = kind;
  table->current_scope = new_scope;
}

void symbol_table_leave_scope(symbol_table *table) {
  if (!table || !table->current_scope)
    return;
  scope *old_scope = table->current_scope;
  table->current_scope = old_scope->parent;
  free(old_scope->ordinary_index);
  free(old_scope);
}

symbol *symbol_table_insert_ordinary(symbol_table *table, const char *name, symbol_kind kind,
                                     type_info *type, source_loc loc) {
  if (!table || !table->current_scope || !name)
    return NULL;
  symbol *sym = new_symbol(table, name, kind, type, loc);
  if (!sym)
    return NULL;
  add_ordinary(table->current_scope, sym);
  return sym;
}

symbol *symbol_table_insert_tag(symbol_table *table, const char *name, type_info *type,
                                source_loc loc) {
  if (!table || !table->current_scope)
    return NULL;
  symbol *sym = new_symbol(table, name, SYMBOL_TAG, type, loc);
  if (!sym)
    return NULL;
  if (name) {
    sym->next = table->current_scope->tag_symbols;
    table->current_scope->tag_symbols = sym;
  }
  return sym;
}

symbol *symbol_table_insert_label(symbol_table *table, const char *name, source_loc loc) {
  if (!table || !name)
    return NULL;
  scope *function_scope = table->current_scope;
  while (function_scope && function_scope->kind != SCOPE_FUNCTION)
    function_scope = function_scope->parent;
  if (!function_scope)
    return NULL;
  symbol *sym = new_symbol(table, name, SYMBOL_LABEL, NULL, loc);
  if (!sym)
    return NULL;
  sym->next = function_scope->label_symbols;
  function_scope->label_symbols = sym;
  return sym;
}

symbol *symbol_table_lookup_ordinary(symbol_table *table, const char *name) {
  if (!table || !name)
    return NULL;
  for (scope *s = table->current_scope; s; s = s->parent) {
    symbol *sym = find_ordinary(s, name);
    if (sym)
      return sym;
  }
  return NULL;
}

symbol *symbol_table_lookup_ordinary_current(symbol_table *table, const char *name) {
  if (!table || !table->current_scope || !name)
    return NULL;
  return find_ordinary(table->current_scope, name);
}

symbol *symbol_table_lookup_tag(symbol_table *table, const char *name) {
  if (!table || !name)
    return NULL;
  for (scope *s = table->current_scope; s; s = s->parent) {
    symbol *sym = find_in_list(s->tag_symbols, name);
    if (sym)
      return sym;
  }
  return NULL;
}

symbol *symbol_table_lookup_tag_current(symbol_table *table, const char *name) {
  if (!table || !table->current_scope || !name)
    return NULL;
  return find_in_list(table->current_scope->tag_symbols, name);
}

symbol *symbol_table_lookup_label(symbol_table *table, const char *name) {
  if (!table || !name)
    return NULL;
  for (scope *s = table->current_scope; s; s = s->parent) {
    if (s->kind == SCOPE_FUNCTION)
      return find_in_list(s->label_symbols, name);
  }
  return NULL;
}