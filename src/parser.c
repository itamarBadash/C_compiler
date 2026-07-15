#include "parser.h"
#include <stdlib.h>
#include <string.h>
static int is_type_token(parser *p);
static ast_node* parse_var_decl(parser *p);
ast_node *parse_expression (parser *p);
ast_node *parse_assignment (parser *p);
static int is_type_token(parser *p) {
  token_type type = p->current_token.type;
  if (type == TOKEN_INT || type == TOKEN_FLOAT || type == TOKEN_CHAR ||
      type == TOKEN_DOUBLE || type == TOKEN_VOID || type == TOKEN_LONG ||
      type == TOKEN_SHORT || type == TOKEN_UNSIGNED || type == TOKEN_SIGNED ||
      type == TOKEN_STRUCT || type == TOKEN_UNION || type == TOKEN_ENUM ||
      type == TOKEN_TYPEDEF || type == TOKEN_CONST || type == TOKEN_VOLATILE ||
      type == TOKEN_RESTRICT || type == TOKEN_STATIC || type == TOKEN_EXTERN ||
      type == TOKEN_REGISTER || type == TOKEN_INLINE ||
      type == TOKEN_COMPLEX || type == TOKEN_IMAGINARY ||
      type == TOKEN_BOOL) {
      return 1;
  }
  if (type == TOKEN_IDENTIFIER) {
      symbol *sym = parser_lookup_symbol(p, p->current_token.value);
      if (sym && sym->kind == SYMBOL_TYPEDEF) {
          return 1;
      }
  }
  return 0;
}
static token clone_token (token t)
{
  token new_t = t;
  if (t.value)
  {
    new_t.value = strdup (t.value);
  }
  return new_t;
}
static type_info* clone_type_info(type_info *type) {
  if (!type) return NULL;
  type_info *new_type = create_type_info(type->kind);
  new_type->is_const = type->is_const;
  new_type->is_volatile = type->is_volatile;
  new_type->is_restrict = type->is_restrict;
  new_type->is_long_long = type->is_long_long;
  new_type->is_complex = type->is_complex;
  new_type->is_imaginary = type->is_imaginary;
  new_type->is_variadic = type->is_variadic;
  new_type->is_inline = type->is_inline;
  new_type->storage_class = type->storage_class;
  new_type->base_type = clone_token(type->base_type);
  if (type->tag_name) {
      new_type->tag_name = strdup(type->tag_name);
  }
  new_type->array_size = type->array_size;
  new_type->ptr_to = clone_type_info(type->ptr_to);
  if (type->param_types) {
    new_type->param_count = type->param_count;
    new_type->param_types = malloc(sizeof(type_info*) * type->param_count);
    for (int i = 0; i < type->param_count; i++) {
      new_type->param_types[i] = clone_type_info(type->param_types[i]);
    }
  }
  return new_type;
}

static type_info* parse_type_specifier(parser *p, ast_node **out_def) {
  if (out_def) *out_def = NULL;
  if (!is_type_token(p)) return NULL;
  
  type_info *type = create_type_info(TYPE_PRIMITIVE);
  type->storage_class = 0;
  type->is_const = 0;
  type->is_volatile = 0;
  type->is_restrict = 0;
  type->base_type.type = TOKEN_UNKNOWN;
  type->base_type.value = NULL;
  
  int has_base_type = 0;
  int long_count = 0;

  while (is_type_token(p) && p->current_token.type != TOKEN_EOF) {
    token_type t = p->current_token.type;
    
    if (t == TOKEN_CONST) { type->is_const = 1; parser_advance(p); continue; }
    if (t == TOKEN_VOLATILE) { type->is_volatile = 1; parser_advance(p); continue; }
    if (t == TOKEN_RESTRICT) { type->is_restrict = 1; parser_advance(p); continue; }
    
    if (t == TOKEN_COMPLEX) { type->is_complex = 1; parser_advance(p); continue; }
    if (t == TOKEN_IMAGINARY) { type->is_imaginary = 1; parser_advance(p); continue; }
    if (t == TOKEN_INLINE) { type->is_inline = 1; parser_advance(p); continue; }

    if (t == TOKEN_STATIC || t == TOKEN_EXTERN || t == TOKEN_REGISTER) {
      type->storage_class = t; 
      parser_advance(p);
      continue;
    }
    
    if (t == TOKEN_STRUCT || t == TOKEN_UNION) {
      type->kind = TYPE_STRUCT;
      type->base_type = clone_token(p->current_token);
      parser_advance(p);
      if (p->current_token.type == TOKEN_IDENTIFIER) {
          type->tag_name = strdup(p->current_token.value);
          parser_advance(p);
      }
      if (p->current_token.type == TOKEN_LBRACE && out_def && *out_def == NULL) {
          parser_advance(p);
          ast_node *def = create_ast_node(AST_NODE_TYPE_STRUCT_DEF);
          def->struct_def.tag_name = type->tag_name ? strdup(type->tag_name) : NULL;
          def->struct_def.members = NULL;
          def->struct_def.member_count = 0;
          
          while (p->current_token.type != TOKEN_RBRACE && p->current_token.type != TOKEN_EOF) {
              ast_node *member = parse_var_decl(p);
              if (member) {
                  def->struct_def.members = realloc(def->struct_def.members, sizeof(ast_node*) * (def->struct_def.member_count + 1));
                  def->struct_def.members[def->struct_def.member_count++] = member;
              } else {
                  parser_advance(p);
              }
          }
          if (p->current_token.type == TOKEN_RBRACE) parser_advance(p);
          *out_def = def;
      }
      has_base_type = 1;
      continue;
    }
    
    if (t == TOKEN_ENUM) {
      type->kind = TYPE_ENUM;
      type->base_type = clone_token(p->current_token);
      parser_advance(p);
      if (p->current_token.type == TOKEN_IDENTIFIER) {
          type->tag_name = strdup(p->current_token.value);
          parser_advance(p);
      }
      if (p->current_token.type == TOKEN_LBRACE && out_def && *out_def == NULL) {
          parser_advance(p);
          ast_node *def = create_ast_node(AST_NODE_TYPE_ENUM_DEF);
          def->enum_def.tag_name = type->tag_name ? strdup(type->tag_name) : NULL;
          def->enum_def.enumerators = NULL;
          def->enum_def.values = NULL;
          def->enum_def.enumerator_count = 0;
          
          while (p->current_token.type != TOKEN_RBRACE && p->current_token.type != TOKEN_EOF) {
              if (p->current_token.type == TOKEN_IDENTIFIER) {
                  def->enum_def.enumerators = realloc(def->enum_def.enumerators, sizeof(char*) * (def->enum_def.enumerator_count + 1));
                  def->enum_def.values = realloc(def->enum_def.values, sizeof(ast_node*) * (def->enum_def.enumerator_count + 1));
                  def->enum_def.enumerators[def->enum_def.enumerator_count] = strdup(p->current_token.value);
                  def->enum_def.values[def->enum_def.enumerator_count] = NULL;
                  
                  parser_advance(p);
                  if (p->current_token.type == TOKEN_ASSIGN) {
                      parser_advance(p);
                      ast_node *val = parse_assignment(p);
                      def->enum_def.values[def->enum_def.enumerator_count] = val;
                  }
                  def->enum_def.enumerator_count++;
              }
              if (p->current_token.type == TOKEN_COMMA) parser_advance(p);
              else break;
          }
          if (p->current_token.type == TOKEN_RBRACE) parser_advance(p);
          *out_def = def;
      }
      has_base_type = 1;
      continue;
    }
    
    if (t == TOKEN_IDENTIFIER) {
        if (!has_base_type) {
            symbol *sym = parser_lookup_symbol(p, p->current_token.value);
            if (sym && sym->kind == SYMBOL_TYPEDEF) {
                type->kind = TYPE_TYPEDEF;
                type->tag_name = strdup(p->current_token.value);
                parser_advance(p);
                has_base_type = 1;
                continue;
            }
        }
        break; // Stop parsing types if it's an identifier that is not part of the type specifier (e.g. variable name)
    }
    
    // Primitive types
    if (t == TOKEN_INT || t == TOKEN_FLOAT || t == TOKEN_CHAR || t == TOKEN_DOUBLE || t == TOKEN_VOID || t == TOKEN_LONG || t == TOKEN_SHORT || t == TOKEN_UNSIGNED || t == TOKEN_SIGNED || t == TOKEN_BOOL) {
        if (t == TOKEN_LONG) {
            long_count++;
            if (long_count == 2) {
                type->is_long_long = 1;
            }
        }
        if (!has_base_type) {
            type->base_type = clone_token(p->current_token); // Just keep the first primitive keyword for now
            has_base_type = 1;
        }
        parser_advance(p);
        continue;
    }
    
    // If we reach here, and it's a type token, break to be safe
    break;
  }
  
  if (!has_base_type) {
      type->base_type.type = TOKEN_INT;
      type->base_type.value = strdup("int");
  }
  
  return type;
}

static type_info* parse_declarator(parser *p, type_info *base_type, char **out_name) {
  type_info *current_type = base_type;
  
  while (p->current_token.type == TOKEN_STAR) {
    type_info *ptr = create_type_info(TYPE_POINTER);
    parser_advance(p);
    while (p->current_token.type == TOKEN_CONST || p->current_token.type == TOKEN_VOLATILE || p->current_token.type == TOKEN_RESTRICT) {
      if (p->current_token.type == TOKEN_CONST) ptr->is_const = 1;
      if (p->current_token.type == TOKEN_VOLATILE) ptr->is_volatile = 1;
      if (p->current_token.type == TOKEN_RESTRICT) ptr->is_restrict = 1;
      parser_advance(p);
    }
    ptr->ptr_to = current_type;
    current_type = ptr;
  }

  if (p->current_token.type == TOKEN_IDENTIFIER) {
    if (out_name) *out_name = strdup(p->current_token.value);
    parser_advance(p);
  } else {
    if (out_name) *out_name = NULL;
  }

  type_info *arr_head = NULL;
  type_info *arr_tail = NULL;

  while (p->current_token.type == TOKEN_LBRACKET) {
    parser_advance(p);
    type_info *arr = create_type_info(TYPE_ARRAY);
    
    if (p->current_token.type != TOKEN_RBRACKET) {
      arr->array_size_expr = parse_expression(p);
      if (arr->array_size_expr && arr->array_size_expr->type == AST_NODE_TYPE_NUMBER) {
          arr->array_size = atoi(arr->array_size_expr->tok.value);
      }
    }
    
    if (p->current_token.type != TOKEN_RBRACKET) {
      free_type_info(arr);
      return NULL;
    }
    parser_advance(p);
    
    if (!arr_head) {
        arr_head = arr;
        arr_tail = arr;
    } else {
        arr_tail->ptr_to = arr;
        arr_tail = arr;
    }
  }
  
  if (arr_head) {
      arr_tail->ptr_to = current_type;
      current_type = arr_head;
  }

  return current_type;
}

static ast_node *parse_initializer(parser *p) {
    if (p->current_token.type == TOKEN_LBRACE) {
        parser_advance(p);
        ast_node *node = create_ast_node(AST_NODE_TYPE_INIT_LIST);
        node->init_list.items = NULL;
        node->init_list.count = 0;
        
        while (p->current_token.type != TOKEN_RBRACE && p->current_token.type != TOKEN_EOF) {
            char *member_name = NULL;
            ast_node *index = NULL;
            
            if (p->current_token.type == TOKEN_DOT) {
                parser_advance(p);
                if (p->current_token.type == TOKEN_IDENTIFIER) {
                    member_name = strdup(p->current_token.value);
                    parser_advance(p);
                }
                if (p->current_token.type == TOKEN_ASSIGN) parser_advance(p);
            } else if (p->current_token.type == TOKEN_LBRACKET) {
                parser_advance(p);
                index = parse_expression(p);
                if (p->current_token.type == TOKEN_RBRACKET) parser_advance(p);
                if (p->current_token.type == TOKEN_ASSIGN) parser_advance(p);
            }
            
            ast_node *val = parse_initializer(p);
            if (val) {
                node->init_list.items = realloc(node->init_list.items, sizeof(*node->init_list.items) * (node->init_list.count + 1));
                node->init_list.items[node->init_list.count].member_name = member_name;
                node->init_list.items[node->init_list.count].index = index;
                node->init_list.items[node->init_list.count].value = val;
                node->init_list.count++;
            } else {
                if (member_name) free(member_name);
                if (index) free_ast(index);
                parser_advance(p); // Skip errors
            }
            if (p->current_token.type == TOKEN_COMMA) {
                parser_advance(p);
            } else {
                break;
            }
        }
        if (p->current_token.type == TOKEN_RBRACE) {
            parser_advance(p);
        }
        return node;
    }
    return parse_assignment(p);
}

static ast_node* parse_var_decl(parser *p) {
  int is_typedef = 0;
  if (p->current_token.type == TOKEN_TYPEDEF) {
    is_typedef = 1;
    parser_advance(p);
  }

  ast_node *def_node = NULL;
  type_info *base_type = parse_type_specifier(p, &def_node);
  if (!base_type) return NULL;

  ast_node *block = create_ast_node(AST_NODE_TYPE_BLOCK);
  block->block.statements = NULL;
  block->block.count = 0;

  do {
    char *name = NULL;
    type_info *decl_type = parse_declarator(p, clone_type_info(base_type), &name);
    
    if (!name) {
      free_type_info(decl_type);
      if (def_node && p->current_token.type == TOKEN_SEMICOLON) {
          parser_advance(p);
          free_type_info(base_type);
          free_ast(block);
          return def_node;
      }
      break;
    }

    if (is_typedef) {
      parser_define_symbol(p, name, SYMBOL_TYPEDEF);
    }

    ast_node *init_expr = NULL;
    if (p->current_token.type == TOKEN_ASSIGN) {
      parser_advance(p);
      init_expr = parse_initializer(p);
    }

    ast_node *decl = create_ast_node(AST_NODE_TYPE_VAR_DECL);
    decl->var_decl.type = decl_type;
    decl->var_decl.var_name = name;
    decl->var_decl.init_value = init_expr;
    decl->var_decl.is_typedef = is_typedef;

    block->block.statements = realloc(block->block.statements, sizeof(ast_node*) * (block->block.count + 1));
    block->block.statements[block->block.count++] = decl;

    if (p->current_token.type == TOKEN_COMMA) {
      parser_advance(p);
    } else {
      break;
    }
  } while (1);

  if (p->current_token.type != TOKEN_SEMICOLON) {
    free_type_info(base_type);
    free_ast(block);
    if (def_node) free_ast(def_node);
    return NULL;
  }
  parser_advance(p);
  free_type_info(base_type);

  if (def_node) {
      ast_node **new_stmts = malloc(sizeof(ast_node*) * (block->block.count + 1));
      new_stmts[0] = def_node;
      for (int i = 0; i < block->block.count; i++) new_stmts[i + 1] = block->block.statements[i];
      free(block->block.statements);
      block->block.statements = new_stmts;
      block->block.count++;
  }

  if (block->block.count == 1) {
    ast_node *single_decl = block->block.statements[0];
    free(block->block.statements);
    free(block); 
    return single_decl;
  }
  return block;
}

static ast_node* parse_top_level_declaration(parser *p) {
  int is_typedef = 0;
  if (p->current_token.type == TOKEN_TYPEDEF) {
    is_typedef = 1;
    parser_advance(p);
  }

  ast_node *def_node = NULL;
  type_info *base_type = parse_type_specifier(p, &def_node);
  if (!base_type) return NULL;

  char *name = NULL;
  type_info *decl_type = parse_declarator(p, clone_type_info(base_type), &name);

  if (!name) {
    free_type_info(decl_type);
    free_type_info(base_type);
    if (def_node && p->current_token.type == TOKEN_SEMICOLON) {
        parser_advance(p);
        return def_node;
    }
    if (def_node) free_ast(def_node);
    return NULL;
  }

  if (is_typedef) {
    parser_define_symbol(p, name, SYMBOL_TYPEDEF);
  }

  // Is it a function definition?
  if (p->current_token.type == TOKEN_LPAREN) {
    parser_advance(p);

    char **params = NULL;
    type_info **param_types = NULL;
    int param_count = 0;

    if (p->current_token.type != TOKEN_RPAREN) {
      do {
        type_info *p_base_type = parse_type_specifier(p, NULL);
        if (!p_base_type) break;

        char *p_name = NULL;
        type_info *p_decl_type = parse_declarator(p, clone_type_info(p_base_type), &p_name);
        free_type_info(p_base_type);

        if (p_decl_type) {
          params = realloc(params, sizeof(char*) * (param_count + 1));
          param_types = realloc(param_types, sizeof(type_info*) * (param_count + 1));
          params[param_count] = p_name; // Could be NULL for anonymous parameters
          param_types[param_count] = p_decl_type;
          param_count++;
        } else {
          if (p_name) free(p_name);
          break;
        }

        if (p->current_token.type == TOKEN_COMMA) {
            parser_advance(p);
            if (p->current_token.type == TOKEN_ELLIPSIS) {
                parser_advance(p);
                decl_type->is_variadic = 1;
                break;
            }
        } else {
            break;
        }
      } while (1);
    }

    if (p->current_token.type != TOKEN_RPAREN) {
      free_type_info(base_type);
      free_type_info(decl_type);
      free(name);
      for(int i = 0; i < param_count; i++) { free(params[i]); free_type_info(param_types[i]); }
      free(params); free(param_types);
      return NULL;
    }
    parser_advance(p);

    if (p->current_token.type == TOKEN_SEMICOLON) {
      parser_advance(p);
      // For now, prototype parsed as function def with no body
      ast_node *node = create_ast_node(AST_NODE_TYPE_FUNCTION_DEF);
      node->function_def.name = name;
      node->function_def.return_type = decl_type;
      node->function_def.parameters = params;
      node->function_def.param_types = param_types;
      node->function_def.param_count = param_count;
      node->function_def.body = NULL;
      free_type_info(base_type);
      
      if (def_node) {
          ast_node *b = create_ast_node(AST_NODE_TYPE_BLOCK);
          b->block.count = 2;
          b->block.statements = malloc(sizeof(ast_node*) * 2);
          b->block.statements[0] = def_node;
          b->block.statements[1] = node;
          return b;
      }
      return node;
    }

    ast_node *body = parse_block(p);
    if (!body) {
      free_type_info(base_type);
      free_type_info(decl_type);
      free(name);
      for(int i = 0; i < param_count; i++) { free(params[i]); free_type_info(param_types[i]); }
      free(params); free(param_types);
      if (def_node) free_ast(def_node);
      return NULL;
    }

    ast_node *node = create_ast_node(AST_NODE_TYPE_FUNCTION_DEF);
    node->function_def.name = name;
    node->function_def.return_type = decl_type;
    node->function_def.parameters = params;
    node->function_def.param_types = param_types;
    node->function_def.param_count = param_count;
    node->function_def.body = body;

    free_type_info(base_type);
    
    if (def_node) {
        ast_node *b = create_ast_node(AST_NODE_TYPE_BLOCK);
        b->block.count = 2;
        b->block.statements = malloc(sizeof(ast_node*) * 2);
        b->block.statements[0] = def_node;
        b->block.statements[1] = node;
        return b;
    }
    return node;
  } else {
    // Variable declaration(s)
    ast_node *block = create_ast_node(AST_NODE_TYPE_BLOCK);
    block->block.statements = NULL;
    block->block.count = 0;

    ast_node *init_expr = NULL;
    if(p->current_token.type == TOKEN_ASSIGN) {
      parser_advance(p);
      init_expr = parse_initializer(p);
    }

    ast_node *decl = create_ast_node(AST_NODE_TYPE_VAR_DECL);
    decl->var_decl.type = decl_type;
    decl->var_decl.var_name = name;
    decl->var_decl.init_value = init_expr;
    decl->var_decl.is_typedef = is_typedef;
    
    block->block.statements = realloc(block->block.statements, sizeof(ast_node*) * (block->block.count + 1));
    block->block.statements[block->block.count++] = decl;

    while (p->current_token.type == TOKEN_COMMA) {
      parser_advance(p);
      char *next_name = NULL;
      type_info *next_decl_type = parse_declarator(p, clone_type_info(base_type), &next_name);
      if (!next_name) {
        free_type_info(next_decl_type);
        break;
      }
      ast_node *next_init_expr = NULL;
      if (p->current_token.type == TOKEN_ASSIGN) {
        parser_advance(p);
        next_init_expr = parse_initializer(p);
      }
      ast_node *next_decl = create_ast_node(AST_NODE_TYPE_VAR_DECL);
      next_decl->var_decl.type = next_decl_type;
      next_decl->var_decl.var_name = next_name;
      next_decl->var_decl.init_value = next_init_expr;
      next_decl->var_decl.is_typedef = is_typedef;
      
      block->block.statements = realloc(block->block.statements, sizeof(ast_node*) * (block->block.count + 1));
      block->block.statements[block->block.count++] = next_decl;
    }

    if(p->current_token.type != TOKEN_SEMICOLON) {
      free_type_info(base_type);
      free_ast(block);
      if (def_node) free_ast(def_node);
      return NULL;
    }
    parser_advance(p);
    free_type_info(base_type);

    if (def_node) {
        ast_node **new_stmts = malloc(sizeof(ast_node*) * (block->block.count + 1));
        new_stmts[0] = def_node;
        for (int i = 0; i < block->block.count; i++) new_stmts[i + 1] = block->block.statements[i];
        free(block->block.statements);
        block->block.statements = new_stmts;
        block->block.count++;
    }

    if (block->block.count == 1) {
      ast_node *single_decl = block->block.statements[0];
      free(block->block.statements);
      free(block); 
      return single_decl;
    }
    return block;
  }
}

void parser_enter_scope(parser *p) {
  scope *new_scope = (scope*)calloc(1, sizeof(scope));
  new_scope->parent = p->current_scope;
  p->current_scope = new_scope;
}

void parser_leave_scope(parser *p) {
  if (!p->current_scope) return;
  scope *old = p->current_scope;
  p->current_scope = old->parent;
  
  symbol *curr = old->symbols;
  while (curr) {
    symbol *next = curr->next;
    free(curr->name);
    free(curr);
    curr = next;
  }
  free(old);
}

void parser_define_symbol(parser *p, const char *name, symbol_kind kind) {
  if (!p->current_scope) return;
  symbol *sym = (symbol*)calloc(1, sizeof(symbol));
  sym->name = strdup(name);
  sym->kind = kind;
  sym->next = p->current_scope->symbols;
  p->current_scope->symbols = sym;
}

symbol* parser_lookup_symbol(parser *p, const char *name) {
  scope *curr_scope = p->current_scope;
  while (curr_scope) {
    symbol *curr_sym = curr_scope->symbols;
    while (curr_sym) {
      if (strcmp(curr_sym->name, name) == 0) {
        return curr_sym;
      }
      curr_sym = curr_sym->next;
    }
    curr_scope = curr_scope->parent;
  }
  return NULL;
}

void parser_destroy (parser *p)
{
  if (!p) return;
  if (p->current_token.value) free (p->current_token.value);
  if (p->next_token.value) free (p->next_token.value);
  while (p->current_scope) {
    parser_leave_scope(p);
  }
}

void parser_advance (parser *p)
{
  if (!p || !p->lex) return;
  if (p->current_token.value) free (p->current_token.value);
  p->current_token = p->next_token;
  p->next_token = lexer_next_token (p->lex);
}
void parser_init (parser *p, lexer *lex)
{
  if (!p || !lex) return;
  p->lex = lex;
  p->current_scope = NULL;
  parser_enter_scope(p);
  p->current_token = lexer_next_token (lex);
  p->next_token = lexer_next_token (lex);
}

ast_node *parse_shift (parser *p)
{
  ast_node *left = parse_additive (p);
  while (p->current_token.type == TOKEN_LSHIFT ||
         p->current_token.type == TOKEN_RSHIFT)
  {
    token op = clone_token (p->current_token);
    parser_advance (p);
    ast_node *right = parse_additive (p);

    ast_node *new_node = create_ast_node (AST_NODE_TYPE_BINARY_OP);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_relational (parser *p)
{
  ast_node *left = parse_shift (p);
  while (p->current_token.type == TOKEN_LT ||
         p->current_token.type == TOKEN_GT ||
         p->current_token.type == TOKEN_LTE ||
         p->current_token.type == TOKEN_GTE)
  {

    token op = clone_token (p->current_token);
    parser_advance (p);

    ast_node *right = parse_shift (p);

    ast_node *new_node = create_ast_node (AST_NODE_TYPE_BINARY_OP);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_equality (parser *p)
{
  ast_node *left = parse_relational (p);
  while (p->current_token.type == TOKEN_EQ ||
         p->current_token.type == TOKEN_NEQ)
  {

    token op = clone_token (p->current_token);
    parser_advance (p);

    ast_node *right = parse_relational (p);

    ast_node *new_node = create_ast_node (AST_NODE_TYPE_BINARY_OP);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_primary (parser *p)
{
  if (!p) return NULL;
  switch (p->current_token.type)
  {
    case TOKEN_NUMBER:
    {
      ast_node *node = create_ast_node (AST_NODE_TYPE_NUMBER);
      node->tok = clone_token (p->current_token);
      parser_advance (p);
      return node;
    }
    case TOKEN_IDENTIFIER:
    {
      ast_node *node = create_ast_node (AST_NODE_TYPE_IDENTIFIER);
      node->tok = clone_token (p->current_token);
      parser_advance (p);
      return node;
    }
    case TOKEN_STRING:
    {
      ast_node *node = create_ast_node (AST_NODE_TYPE_STRING);
      node->tok = clone_token (p->current_token);
      parser_advance (p);
      return node;
    }
    case TOKEN_CHAR_LITERAL:
    {
      ast_node *node = create_ast_node (AST_NODE_TYPE_CHAR_LITERAL);
      node->tok = clone_token (p->current_token);
      parser_advance (p);
      return node;
    }
    case TOKEN_LPAREN:
    {
      parser_advance (p);
      ast_node *node = parse_expression (p);
      if (p->current_token.type != TOKEN_RPAREN)
      {
        // Handle error: expected closing parenthesis
        return NULL;
      }
      parser_advance (p);
      return node;
    }
    default:
      return NULL;
  }
}
ast_node *parse_postfix (parser *p)
{
  ast_node *left = parse_primary (p);
  while (p && (p->current_token.type == TOKEN_LPAREN ||
               p->current_token.type == TOKEN_LBRACKET ||
               p->current_token.type == TOKEN_DOT ||
               p->current_token.type == TOKEN_ARROW ||
               p->current_token.type == TOKEN_PLUS_PLUS ||
               p->current_token.type == TOKEN_MINUS_MINUS))
  {

    if (p->current_token.type == TOKEN_LPAREN)
    {
      parser_advance (p);
      ast_node *node = create_ast_node (AST_NODE_TYPE_FUNCTION_CALL);
      node->function_call.callable = left;
      node->function_call.arguments = NULL;
      node->function_call.arg_count = 0;

      if (p->current_token.type != TOKEN_RPAREN)
      {
        do
        {
          ast_node *arg = parse_assignment (p);
          if (arg)
          {
            node->function_call.arguments = realloc (node->function_call.arguments,
                                                     sizeof (ast_node *)
                                                     * (node->function_call.arg_count
                                                        + 1));
            node->function_call.arguments[node->function_call.arg_count++] = arg;
          }
          if (p->current_token.type == TOKEN_COMMA)
          {
            parser_advance (p);
          }
          else
          {
            break;
          }
        }
        while (1);
      }
      if (p->current_token.type == TOKEN_RPAREN)
      {
        parser_advance (p);
      }
      left = node;
    }
    else if (p->current_token.type == TOKEN_LBRACKET)
    {
      parser_advance (p);
      ast_node *node = create_ast_node (AST_NODE_TYPE_ARRAY_SUBSCRIPT);
      node->array_subscript.left = left;
      node->array_subscript.index = parse_expression (p);
      if (p->current_token.type == TOKEN_RBRACKET)
      {
        parser_advance (p);
      }
      left = node;
    }
    else if (p->current_token.type == TOKEN_DOT
             || p->current_token.type == TOKEN_ARROW)
    {
      int is_pointer = (p->current_token.type == TOKEN_ARROW) ? 1 : 0;
      parser_advance (p);

      ast_node *node = create_ast_node (AST_NODE_TYPE_MEMBER_ACCESS);
      node->member_access.left = left;
      node->member_access.is_pointer = is_pointer;

      if (p->current_token.type == TOKEN_IDENTIFIER)
      {
        node->member_access.member_name = strdup (p->current_token.value);
        parser_advance (p);
      }
      else
      {
        node->member_access.member_name = strdup ("unknown");
      }
      left = node;
    }
    else if (p->current_token.type == TOKEN_PLUS_PLUS
             || p->current_token.type == TOKEN_MINUS_MINUS)
    {
      token op = clone_token (p->current_token);
      parser_advance (p);
      ast_node *node = create_ast_node (AST_NODE_TYPE_UNARY_OP);
      node->unary_op.op = op;
      node->unary_op.operand = left;
      node->unary_op.is_postfix = 1;
      left = node;
    }
    else
    {
      break; // Should not reach here based on while condition, but safe
    }
  }
  return left;
}
ast_node *parse_multiplicative (parser *p)
{
  ast_node *left = parse_unary (p);

  while (p->current_token.type == TOKEN_STAR ||
         p->current_token.type == TOKEN_SLASH ||
         p->current_token.type == TOKEN_PERCENT)
  {

    token op = clone_token (p->current_token);
    parser_advance (p);

    ast_node *right = parse_unary (p);

    ast_node *new_node = create_ast_node (AST_NODE_TYPE_BINARY_OP);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }

  return left;
}
ast_node *parse_additive (parser *p)
{
  ast_node *left = parse_multiplicative (p);

  while (p->current_token.type == TOKEN_PLUS ||
         p->current_token.type == TOKEN_MINUS)
  {

    token op = clone_token (p->current_token);
    parser_advance (p);

    ast_node *right = parse_multiplicative (p);

    ast_node *new_node = create_ast_node (AST_NODE_TYPE_BINARY_OP);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }

  return left;
}
ast_node *parse_ternary (parser *p)
{
  ast_node *cond = parse_logical_or (p);
  if (p && p->current_token.type == TOKEN_QUESTION)
  {
    parser_advance (p);
    ast_node *true_expr = parse_expression (p);
    if (p->current_token.type == TOKEN_COLON)
    {
      parser_advance (p);
    }
    ast_node *false_expr = parse_ternary (p);

    ast_node *node = create_ast_node (AST_NODE_TYPE_TERNARY);
    node->ternary.condition = cond;
    node->ternary.true_branch = true_expr;
    node->ternary.false_branch = false_expr;
    return node;
  }
  return cond;
}
ast_node *parse_assignment (parser *p)
{
  ast_node *left = parse_ternary (p);
  if (p->current_token.type == TOKEN_ASSIGN ||
      p->current_token.type == TOKEN_PLUS_ASSIGN ||
      p->current_token.type == TOKEN_MINUS_ASSIGN ||
      p->current_token.type == TOKEN_STAR_ASSIGN ||
      p->current_token.type == TOKEN_SLASH_ASSIGN ||
      p->current_token.type == TOKEN_PERCENT_ASSIGN ||
      p->current_token.type == TOKEN_LSHIFT_ASSIGN ||
      p->current_token.type == TOKEN_RSHIFT_ASSIGN ||
      p->current_token.type == TOKEN_AMPERSAND_ASSIGN ||
      p->current_token.type == TOKEN_CARET_ASSIGN ||
      p->current_token.type == TOKEN_PIPE_ASSIGN)
  {
    token op = clone_token (p->current_token);
    parser_advance (p);
    ast_node *right = parse_assignment (p); // Right-associative

    ast_node *node = create_ast_node (AST_NODE_TYPE_ASSIGNMENT);
    node->assignment.op = op;
    node->assignment.left = left;
    node->assignment.right = right;
    return node;
  }
  return left;
}
ast_node *parse_expression (parser *p)
{
  ast_node *left = parse_assignment (p);
  while (p && p->current_token.type == TOKEN_COMMA)
  {
    token op = clone_token (p->current_token);
    parser_advance (p);
    ast_node *right = parse_assignment (p);

    ast_node *node = create_ast_node (AST_NODE_TYPE_BINARY_OP);
    node->binary_op.op = op;
    node->binary_op.left = left;
    node->binary_op.right = right;

    left = node;
  }
  return left;
}
ast_node *parse_block (parser *p)
{
  if (p->current_token.type != TOKEN_LBRACE)
  {
    // Handle error: expected opening brace
    return NULL;
  }
  parser_advance (p);

  ast_node *node = create_ast_node (AST_NODE_TYPE_BLOCK);
  node->block.statements = NULL;
  node->block.count = 0;

  parser_enter_scope(p);

  while (p->current_token.type != TOKEN_RBRACE
         && p->current_token.type != TOKEN_EOF)
  {
    ast_node *stmt = parse_statement (p);
    if (stmt)
    {
      node->block.statements = realloc (node->block.statements,
                                        sizeof (ast_node *)
                                        * (node->block.count + 1));
      node->block.statements[node->block.count++] = stmt;
    }
    else
    {
      // Handle error: failed to parse statement
      parser_leave_scope(p);
      free_ast (node);
      return NULL;
    }
  }

  parser_leave_scope(p);

  if (p->current_token.type != TOKEN_RBRACE)
  {
    // Handle error: expected closing brace
    free_ast (node);
    return NULL;
  }

  parser_advance (p);
  return node;
}
ast_node *parse_statement (parser *p)
{
  if (!p) return NULL;

  if (is_type_token(p) || p->current_token.type == TOKEN_TYPEDEF) {
    return parse_var_decl(p);
  }

  switch (p->current_token.type)
  {
    case TOKEN_RETURN:
    {
      parser_advance (p);
      ast_node *node = create_ast_node (AST_NODE_TYPE_RETURN);
      node->return_stmt.return_value = parse_expression (p);
      if (p->current_token.type != TOKEN_SEMICOLON)
      {
        // Handle error: expected semicolon
        return NULL;
      }
      parser_advance (p);
      return node;
    }
    case TOKEN_LBRACE:
      return parse_block (p);
    case TOKEN_IF:
    {
      parser_advance (p);
      if (p->current_token.type != TOKEN_LPAREN)
      {
        // Handle error: expected '(' after 'if'
        return NULL;
      }
      parser_advance (p);
      ast_node *condition = parse_expression (p);
      if (p->current_token.type != TOKEN_RPAREN)
      {
        // Handle error: expected ')' after condition
        return NULL;
      }
      parser_advance (p);
      ast_node *then_branch = parse_statement (p);
      ast_node *else_branch = NULL;
      if (p->current_token.type == TOKEN_ELSE)
      {
        parser_advance (p);
        else_branch = parse_statement (p);
      }

      ast_node *node = create_ast_node (AST_NODE_TYPE_IF);
      node->if_stmt.condition = condition;
      node->if_stmt.then_branch = then_branch;
      node->if_stmt.else_branch = else_branch;
      return node;
    }
    case TOKEN_WHILE:
    {
      parser_advance (p);
      if (p->current_token.type != TOKEN_LPAREN)
      {
        // Handle error: expected '(' after 'while'
        return NULL;
      }
      parser_advance (p);
      ast_node *condition = parse_expression (p);
      if (p->current_token.type != TOKEN_RPAREN)
      {
        // Handle error: expected ')' after condition
        return NULL;
      }
      parser_advance (p);
      ast_node *body = parse_statement (p);

      ast_node *node = create_ast_node (AST_NODE_TYPE_WHILE);
      node->while_stmt.condition = condition;
      node->while_stmt.body = body;
      return node;
    }

    case TOKEN_FOR:
    {
      parser_advance (p);
      if (p->current_token.type != TOKEN_LPAREN)
      {
        // Handle error: expected '(' after 'for'
        return NULL;
      }
      parser_advance (p);

      // 1. אתחול (אופציונלי)
      ast_node *init = NULL;
      if (is_type_token(p)) {
        init = parse_var_decl(p); // Consumes the semicolon
      } else {
        if (p->current_token.type != TOKEN_SEMICOLON)
        {
          init = parse_expression (p);
        }
        if (p->current_token.type != TOKEN_SEMICOLON)
        {
          // Handle error: expected ';' after init
          return NULL;
        }
        parser_advance (p);
      }

      // 2. תנאי (אופציונלי)
      ast_node *condition = NULL;
      if (p->current_token.type != TOKEN_SEMICOLON)
      {
        condition = parse_expression (p);
      }
      if (p->current_token.type != TOKEN_SEMICOLON)
      {
        // Handle error: expected ';' after condition
        return NULL;
      }
      parser_advance (p);

      // 3. קידום (אופציונלי)
      ast_node *increment = NULL;
      if (p->current_token.type != TOKEN_RPAREN)
      {
        increment = parse_expression (p);
      }
      if (p->current_token.type != TOKEN_RPAREN)
      {
        // Handle error: expected ')' after increment
        return NULL;
      }
      parser_advance (p);

      // 4. גוף הלולאה
      ast_node *body = parse_statement (p);

      ast_node *node = create_ast_node (AST_NODE_TYPE_FOR);
      node->for_stmt.init = init;
      node->for_stmt.condition = condition;
      node->for_stmt.increment = increment;
      node->for_stmt.body = body;
      return node;
    }

    case TOKEN_DO:
    {
      parser_advance (p);
      ast_node *body = parse_statement (p);
      if (p->current_token.type != TOKEN_WHILE) return NULL;
      parser_advance (p);
      if (p->current_token.type != TOKEN_LPAREN) return NULL;
      parser_advance (p);
      ast_node *condition = parse_expression (p);
      if (p->current_token.type != TOKEN_RPAREN) return NULL;
      parser_advance (p);
      if (p->current_token.type != TOKEN_SEMICOLON) return NULL;
      parser_advance (p);
      
      ast_node *node = create_ast_node (AST_NODE_TYPE_DO_WHILE);
      node->do_while_stmt.body = body;
      node->do_while_stmt.condition = condition;
      return node;
    }

    case TOKEN_SWITCH:
    {
      parser_advance (p);
      if (p->current_token.type != TOKEN_LPAREN) return NULL;
      parser_advance (p);
      ast_node *condition = parse_expression (p);
      if (p->current_token.type != TOKEN_RPAREN) return NULL;
      parser_advance (p);
      ast_node *body = parse_statement (p);
      
      ast_node *node = create_ast_node (AST_NODE_TYPE_SWITCH);
      node->switch_stmt.condition = condition;
      node->switch_stmt.body = body;
      return node;
    }

    case TOKEN_CASE:
    {
      parser_advance (p);
      ast_node *value = parse_expression (p);
      if (p->current_token.type != TOKEN_COLON) return NULL;
      parser_advance (p);
      ast_node *body = parse_statement (p);
      
      ast_node *node = create_ast_node (AST_NODE_TYPE_CASE);
      node->case_stmt.value = value;
      node->case_stmt.body = body;
      return node;
    }

    case TOKEN_DEFAULT:
    {
      parser_advance (p);
      if (p->current_token.type != TOKEN_COLON) return NULL;
      parser_advance (p);
      ast_node *body = parse_statement (p);
      
      ast_node *node = create_ast_node (AST_NODE_TYPE_DEFAULT);
      node->default_stmt.body = body;
      return node;
    }

    case TOKEN_GOTO:
    {
      parser_advance (p);
      if (p->current_token.type != TOKEN_IDENTIFIER) return NULL;
      ast_node *node = create_ast_node(AST_NODE_TYPE_GOTO);
      node->goto_stmt.label_name = strdup(p->current_token.value);
      parser_advance (p);
      if (p->current_token.type != TOKEN_SEMICOLON) {
          free_ast(node);
          return NULL;
      }
      parser_advance (p);
      return node;
    }

    case TOKEN_BREAK:
    {
      parser_advance (p);
      if (p->current_token.type != TOKEN_SEMICOLON) return NULL;
      parser_advance (p);
      return create_ast_node (AST_NODE_TYPE_BREAK);
    }

    case TOKEN_CONTINUE:
    {
      parser_advance (p);
      if (p->current_token.type != TOKEN_SEMICOLON) return NULL;
      parser_advance (p);
      return create_ast_node (AST_NODE_TYPE_CONTINUE);
    }

    default:
    {
      if (p->current_token.type == TOKEN_IDENTIFIER && p->next_token.type == TOKEN_COLON) {
          ast_node *node = create_ast_node(AST_NODE_TYPE_LABEL);
          node->label_stmt.label_name = strdup(p->current_token.value);
          parser_advance(p); // Consume identifier
          parser_advance(p); // Consume colon
          node->label_stmt.statement = parse_statement(p);
          return node;
      }

      ast_node *node = parse_expression (p);
      if (p->current_token.type != TOKEN_SEMICOLON)
      {
        // Handle error: expected semicolon
        return NULL;
      }
      parser_advance (p);
      return node;
    }
  }
}
ast_node *parse_bitwise_and (parser *p)
{
  ast_node *left = parse_equality (p);
  while (p->current_token.type == TOKEN_AMPERSAND)
  {
    token op = clone_token (p->current_token);
    parser_advance (p);
    ast_node *right = parse_equality (p);

    ast_node *new_node = create_ast_node (AST_NODE_TYPE_BINARY_OP);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_bitwise_xor (parser *p)
{
  ast_node *left = parse_bitwise_and (p);
  while (p->current_token.type == TOKEN_CARET)
  {
    token op = clone_token (p->current_token);
    parser_advance (p);
    ast_node *right = parse_bitwise_and (p);

    ast_node *new_node = create_ast_node (AST_NODE_TYPE_BINARY_OP);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_bitwise_or (parser *p)
{
  ast_node *left = parse_bitwise_xor (p);
  while (p->current_token.type == TOKEN_PIPE)
  {
    token op = clone_token (p->current_token);
    parser_advance (p);
    ast_node *right = parse_bitwise_xor (p);

    ast_node *new_node = create_ast_node (AST_NODE_TYPE_BINARY_OP);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_logical_and (parser *p)
{
  ast_node *left = parse_bitwise_or (p);
  while (p->current_token.type == TOKEN_AND)
  {
    token op = clone_token (p->current_token);
    parser_advance (p);
    ast_node *right = parse_bitwise_or (p);

    ast_node *new_node = create_ast_node (AST_NODE_TYPE_BINARY_OP);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_logical_or (parser *p)
{
  ast_node *left = parse_logical_and (p);
  while (p->current_token.type == TOKEN_OR)
  {
    token op = clone_token (p->current_token);
    parser_advance (p);
    ast_node *right = parse_logical_and (p);

    ast_node *new_node = create_ast_node (AST_NODE_TYPE_BINARY_OP);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
static type_info* parse_type_name(parser *p) {
    type_info *base = parse_type_specifier(p, NULL);
    if (!base) return NULL;
    type_info *decl = parse_declarator(p, clone_type_info(base), NULL);
    free_type_info(base);
    return decl;
}

ast_node *parse_unary (parser *p)
{
  if (p->current_token.type == TOKEN_LPAREN) {
    int next_is_type = 0;
    token_type t = p->next_token.type;
    if (t == TOKEN_INT || t == TOKEN_FLOAT || t == TOKEN_CHAR || t == TOKEN_DOUBLE || t == TOKEN_VOID || t == TOKEN_LONG || t == TOKEN_SHORT || t == TOKEN_UNSIGNED || t == TOKEN_SIGNED || t == TOKEN_STRUCT || t == TOKEN_ENUM) {
        next_is_type = 1;
    } else if (t == TOKEN_IDENTIFIER) {
        symbol *sym = parser_lookup_symbol(p, p->next_token.value);
        if (sym && sym->kind == SYMBOL_TYPEDEF) next_is_type = 1;
    }

    if (next_is_type) {
        parser_advance(p); // Consume '('
        type_info *cast_type = parse_type_name(p);
        if (p->current_token.type == TOKEN_RPAREN) {
            parser_advance(p); // Consume ')'
            
            if (p->current_token.type == TOKEN_LBRACE) {
                ast_node *init_list = parse_initializer(p);
                ast_node *node = create_ast_node(AST_NODE_TYPE_COMPOUND_LITERAL);
                node->compound_literal.type = cast_type;
                node->compound_literal.init_list = init_list;
                return node;
            }

            ast_node *operand = parse_unary(p);
            ast_node *node = create_ast_node(AST_NODE_TYPE_CAST);
            node->cast_expr.type = cast_type;
            node->cast_expr.operand = operand;
            return node;
        } else {
            if (cast_type) free_type_info(cast_type);
        }
    }
  }

  if (p->current_token.type == TOKEN_SIZEOF) {
    token op = clone_token(p->current_token);
    parser_advance(p);

    if (p->current_token.type == TOKEN_LPAREN) {
        int next_is_type = 0;
        token_type t = p->next_token.type;
        if (t == TOKEN_INT || t == TOKEN_FLOAT || t == TOKEN_CHAR || t == TOKEN_DOUBLE || t == TOKEN_VOID || t == TOKEN_LONG || t == TOKEN_SHORT || t == TOKEN_UNSIGNED || t == TOKEN_SIGNED || t == TOKEN_STRUCT || t == TOKEN_ENUM) {
            next_is_type = 1;
        } else if (t == TOKEN_IDENTIFIER) {
            symbol *sym = parser_lookup_symbol(p, p->next_token.value);
            if (sym && sym->kind == SYMBOL_TYPEDEF) next_is_type = 1;
        }

        if (next_is_type) {
            parser_advance(p); // '('
            type_info *sizeof_type = parse_type_name(p);
            if (p->current_token.type == TOKEN_RPAREN) {
                parser_advance(p); // ')'
                ast_node *node = create_ast_node(AST_NODE_TYPE_UNARY_OP);
                node->unary_op.op = op;
                ast_node *dummy_cast = create_ast_node(AST_NODE_TYPE_CAST);
                dummy_cast->cast_expr.type = sizeof_type;
                dummy_cast->cast_expr.operand = NULL;
                node->unary_op.operand = dummy_cast;
                node->unary_op.is_postfix = 0;
                return node;
            }
        }
    }

    ast_node *operand = parse_unary(p);
    ast_node *node = create_ast_node(AST_NODE_TYPE_UNARY_OP);
    node->unary_op.op = op;
    node->unary_op.operand = operand;
    node->unary_op.is_postfix = 0;
    return node;
  }

  if (p->current_token.type == TOKEN_NOT ||
      p->current_token.type == TOKEN_MINUS ||
      p->current_token.type == TOKEN_TILDE ||
      p->current_token.type == TOKEN_AMPERSAND ||
      p->current_token.type == TOKEN_STAR ||
      p->current_token.type == TOKEN_PLUS_PLUS ||
      p->current_token.type == TOKEN_MINUS_MINUS ||
      p->current_token.type == TOKEN_PLUS)
  {
    token op = clone_token (p->current_token);
    parser_advance (p);
    ast_node *operand = parse_unary (p);

    ast_node *node = create_ast_node (AST_NODE_TYPE_UNARY_OP);
    node->unary_op.op = op;
    node->unary_op.operand = operand;
    node->unary_op.is_postfix = 0;

    return node;
  }
  else
  {
    return parse_postfix (p);
  }
}
ast_node *parse_program(parser *p) {
  if(!p) return NULL;

  ast_node *prog_node = create_ast_node(AST_NODE_TYPE_PROGRAM);
  prog_node->program.declarations = NULL;
  prog_node->program.count = 0;

  while (p->current_token.type != TOKEN_EOF) {
    if (is_type_token(p) || p->current_token.type == TOKEN_TYPEDEF) {
      ast_node *decl = parse_top_level_declaration(p);
      if (decl) {
        ast_node **temp = realloc(prog_node->program.declarations,
                                  sizeof(ast_node*) * (prog_node->program.count + 1));
        if (temp) {
          prog_node->program.declarations = temp;
          prog_node->program.declarations[prog_node->program.count++] = decl;
        }
      } else {
        parser_advance(p);
      }
    } else {
      parser_advance(p);
    }
  }
  return prog_node;
}