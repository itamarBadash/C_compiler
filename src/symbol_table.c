#include "symbol_table.h"
#include <stdlib.h>
#include <string.h>

symbol_table *symbol_table_create (void)
{
  symbol_table *table = (symbol_table *)malloc(sizeof(symbol_table));
  if (!table) return NULL;
  table->current_scope = NULL;
  return table;
}

void symbol_table_destroy (symbol_table *table)
{
  if (!table) return;
  while (table->current_scope) {
    symbol_table_leave_scope(table);
  }
  free(table);
}

void symbol_table_leave_scope (symbol_table *table)
{
  if (!table || !table->current_scope) return;
  scope *old_scope = table->current_scope;
  table->current_scope = old_scope->parent;

  symbol *curr = old_scope->ordinary_symbols;
  while (curr) {
    symbol *next = curr->next;
    free(curr->name);
    free(curr);
    curr = next;
  }

  curr = old_scope->tag_symbols;
  while (curr) {
    symbol *next = curr->next;
    free(curr->name);
    free(curr);
    curr = next;
  }

  curr = old_scope->label_symbols;
  while (curr) {
    symbol *next = curr->next;
    free(curr->name);
    free(curr);
    curr = next;
  }

  free(old_scope);
}

void symbol_table_enter_scope (symbol_table *table, scope_kind kind)
{
  if (!table) return;
  scope *new_scope = (scope *)malloc(sizeof(scope));
  if (!new_scope) return;
  new_scope->parent = table->current_scope;
  new_scope->kind = kind;
  new_scope->ordinary_symbols = NULL;
  new_scope->tag_symbols = NULL;
  new_scope->label_symbols = NULL;
  table->current_scope = new_scope;
}

int symbol_table_insert_ordinary (symbol_table *table, const char *name, symbol_kind kind, type_info *type, linkage_kind linkage, int is_tentative, int is_defined)
{
  if (!table || !table->current_scope || !name) return -1;

  symbol *curr = table->current_scope->ordinary_symbols;
  while (curr) {
    if (strcmp(curr->name, name) == 0) {
      if (table->current_scope->kind == SCOPE_FILE) {
          if (curr->is_defined && is_defined) {
              return -1; // Redefinition error
          }
          if (is_defined) {
              curr->is_defined = 1;
              curr->is_tentative = 0;
          }
          return 0; // Merged
      }
      return -1;
    }
    curr = curr->next;
  }

  symbol *new_symbol = (symbol *)malloc(sizeof(symbol));
  if (!new_symbol) return -1;
  new_symbol->name = strdup(name);
  new_symbol->kind = kind;
  new_symbol->type = type;
  new_symbol->linkage = linkage;
  new_symbol->is_tentative = is_tentative;
  new_symbol->is_defined = is_defined;
  new_symbol->next = table->current_scope->ordinary_symbols;
  table->current_scope->ordinary_symbols = new_symbol;

  return 0;
}

int symbol_table_insert_tag(symbol_table *table, const char *name, type_info *type)
{
  if (!table || !table->current_scope || !name) return -1;

  symbol *curr = table->current_scope->tag_symbols;
  while (curr) {
    if (strcmp(curr->name, name) == 0) {
      return -1;
    }
    curr = curr->next;
  }

  symbol *sym = (symbol*)malloc(sizeof(symbol));
  if (!sym) return -1;
  sym->name = strdup(name);
  sym->kind = SYMBOL_TAG;
  sym->type = type;
  sym->next = table->current_scope->tag_symbols;
  table->current_scope->tag_symbols = sym;

  return 0;
}

symbol *symbol_table_lookup_ordinary (symbol_table *table, const char *name)
{
  if (!table || !table->current_scope || !name) return NULL;

  scope *scope_iter = table->current_scope;
  while (scope_iter) {
    symbol *curr = scope_iter->ordinary_symbols;
    while (curr) {
      if (strcmp(curr->name, name) == 0) {
        return curr;
      }
      curr = curr->next;
    }
    scope_iter = scope_iter->parent;
  }

  return NULL;
}

symbol *symbol_table_lookup_tag (symbol_table *table, const char *name)
{
  if (!table || !table->current_scope || !name) return NULL;

  scope *scope_iter = table->current_scope;
  while (scope_iter) {
    symbol *curr = scope_iter->tag_symbols;
    while (curr) {
      if (strcmp(curr->name, name) == 0) {
        return curr;
      }
      curr = curr->next;
    }
    scope_iter = scope_iter->parent;
  }

  return NULL;
}

int symbol_table_insert_label(symbol_table *table, const char *name)
{
  if (!table || !table->current_scope || !name) return -1;

  scope *scope_iter = table->current_scope;
  while (scope_iter && scope_iter->kind != SCOPE_FUNCTION) {
    scope_iter = scope_iter->parent;
  }

  if (!scope_iter) {
    return -1; // Not in a function
  }

  symbol *curr = scope_iter->label_symbols;
  while (curr) {
    if (strcmp(curr->name, name) == 0) {
      return -1; // Label already defined
    }
    curr = curr->next;
  }

  symbol *sym = (symbol*)malloc(sizeof(symbol));
  if (!sym) return -1;
  sym->name = strdup(name);
  sym->kind = SYMBOL_LABEL;
  sym->type = NULL;
  sym->linkage = LINKAGE_NONE;
  sym->is_tentative = 0;
  sym->is_defined = 1;
  sym->next = scope_iter->label_symbols;
  scope_iter->label_symbols = sym;

  return 0;
}

symbol* symbol_table_lookup_label(symbol_table *table, const char *name)
{
  if (!table || !table->current_scope || !name) return NULL;

  scope *scope_iter = table->current_scope;
  while (scope_iter && scope_iter->kind != SCOPE_FUNCTION) {
    scope_iter = scope_iter->parent;
  }

  if (!scope_iter) return NULL;

  symbol *curr = scope_iter->label_symbols;
  while (curr) {
    if (strcmp(curr->name, name) == 0) {
      return curr;
    }
    curr = curr->next;
  }

  return NULL;
}