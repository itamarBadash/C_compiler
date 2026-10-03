#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_type_token(parser *p);
typedef enum declaration_context {
  DECLARATION_FILE,
  DECLARATION_BLOCK,
  DECLARATION_MEMBER,
  DECLARATION_PARAMETER
} declaration_context;

static ast_node *parse_declaration(parser *p, declaration_context context);
static type_info *parse_declarator(parser *p, type_info *base_type, char **out_name,
                                   source_loc *out_name_loc, int allow_abstract);
static void parser_begin(parser *p);
static token clone_token(token t);

static int token_starts_type(parser *p, token tok) {
  switch (tok.type) {
  case TOKEN_INT:
  case TOKEN_FLOAT:
  case TOKEN_CHAR:
  case TOKEN_DOUBLE:
  case TOKEN_VOID:
  case TOKEN_LONG:
  case TOKEN_SHORT:
  case TOKEN_UNSIGNED:
  case TOKEN_SIGNED:
  case TOKEN_STRUCT:
  case TOKEN_UNION:
  case TOKEN_ENUM:
  case TOKEN_CONST:
  case TOKEN_VOLATILE:
  case TOKEN_RESTRICT:
  case TOKEN_COMPLEX:
  case TOKEN_IMAGINARY:
  case TOKEN_BOOL:
    return 1;
  case TOKEN_IDENTIFIER: {
    parser_symbol *sym = parser_lookup_symbol(p, tok.value);
    return sym && sym->kind == PARSER_SYMBOL_TYPEDEF;
  }
  default:
    return 0;
  }
}
static int starts_specifiers(parser *p, token tok) {
  token_type type = tok.type;
  return type == TOKEN_TYPEDEF || type == TOKEN_STATIC || type == TOKEN_EXTERN ||
         type == TOKEN_REGISTER || type == TOKEN_AUTO || type == TOKEN_INLINE ||
         token_starts_type(p, tok);
}

static int is_type_token(parser *p) {
  return starts_specifiers(p, p->current_token);
}

static int starts_declaration(parser *p) {
  return is_type_token(p) &&
         !(p->current_token.type == TOKEN_IDENTIFIER && p->next_token.type == TOKEN_COLON);
}
static token clone_token(token t) {
  token new_t = t;
  if (t.value) {
    new_t.value = strdup(t.value);
  }
  return new_t;
}

static source_loc loc_of(token t) {
  source_loc loc = {t.line, t.column, t.file};
  return loc;
}

static void parser_error_at(parser *p, token at, const char *message) {
  p->had_error++;
  if (at.file)
    fprintf(stderr, "%s:", at.file);
  fprintf(stderr, "%d:%d: error: %s (at '%s')\n", at.line, at.column, message,
          at.value ? at.value : "<eof>");
}

void parser_error(parser *p, const char *message) {
  if (!p)
    return;
  parser_error_at(p, p->current_token, message);
}

static void decode_pieces(parser *p, ast_node *node, const token *pieces, int count) {
  size_t total = 0;
  for (int i = 0; i < count; i++)
    total += pieces[i].value ? strlen(pieces[i].value) : 0;
  node->literal.bytes = malloc(2 * total + 2);
  if (!node->literal.bytes)
    return;
  for (int i = 0; i < count; i++) {
    const char *error =
        decode_literal(pieces[i].value ? pieces[i].value : "", node->literal.is_wide,
                       node->literal.bytes, &node->literal.length);
    if (error)
      parser_error_at(p, pieces[i], error);
  }
  memset(node->literal.bytes + node->literal.length * (node->literal.is_wide ? 2 : 1), 0, 2);
}

static type_info *clone_type_info(type_info *type) {
  if (!type)
    return NULL;
  type_info *new_type = create_type_info(type->kind);
  new_type->prim = type->prim;
  new_type->is_const = type->is_const;
  new_type->is_volatile = type->is_volatile;
  new_type->is_restrict = type->is_restrict;
  new_type->is_complex = type->is_complex;
  new_type->is_imaginary = type->is_imaginary;
  new_type->is_variadic = type->is_variadic;
  new_type->definition = type->definition;

  if (type->tag_name) {
    new_type->tag_name = strdup(type->tag_name);
  }
  new_type->array_size = type->array_size;
  new_type->ptr_to = clone_type_info(type->ptr_to);
  if (type->param_types) {
    new_type->param_count = type->param_count;
    new_type->param_types = malloc(sizeof(type_info *) * type->param_count);
    for (int i = 0; i < type->param_count; i++) {
      new_type->param_types[i] = clone_type_info(type->param_types[i]);
    }
  }
  if (type->param_names) {
    new_type->param_count = type->param_count;
    new_type->param_names = malloc(sizeof(char *) * type->param_count);
    for (int i = 0; i < type->param_count; i++) {
      new_type->param_names[i] = type->param_names[i] ? strdup(type->param_names[i]) : NULL;
    }
  }
  return new_type;
}
typedef struct spec_counts {
  int void_count;
  int bool_count;
  int char_count;
  int short_count;
  int int_count;
  int long_count;
  int float_count;
  int double_count;
  int signed_count;
  int unsigned_count;
  int complex_count;
  int imaginary_count;
} spec_counts;

static int count_arithmetic_specifier(spec_counts *counts, token_type t) {
  switch (t) {
  case TOKEN_VOID:
    counts->void_count++;
    return 1;
  case TOKEN_BOOL:
    counts->bool_count++;
    return 1;
  case TOKEN_CHAR:
    counts->char_count++;
    return 1;
  case TOKEN_SHORT:
    counts->short_count++;
    return 1;
  case TOKEN_INT:
    counts->int_count++;
    return 1;
  case TOKEN_LONG:
    counts->long_count++;
    return 1;
  case TOKEN_FLOAT:
    counts->float_count++;
    return 1;
  case TOKEN_DOUBLE:
    counts->double_count++;
    return 1;
  case TOKEN_SIGNED:
    counts->signed_count++;
    return 1;
  case TOKEN_UNSIGNED:
    counts->unsigned_count++;
    return 1;
  case TOKEN_COMPLEX:
    counts->complex_count++;
    return 1;
  case TOKEN_IMAGINARY:
    counts->imaginary_count++;
    return 1;
  default:
    return 0;
  }
}

static prim_kind resolve_arithmetic_type(const spec_counts *c) {
  int sign_count = c->signed_count + c->unsigned_count;
  int fp_suffix_count = c->complex_count + c->imaginary_count;

  if (c->void_count > 1 || c->bool_count > 1 || c->char_count > 1 || c->short_count > 1 ||
      c->int_count > 1 || c->long_count > 2 || c->float_count > 1 || c->double_count > 1 ||
      sign_count > 1 || fp_suffix_count > 1)
    return PRIM_NONE;

  int integer_words = c->char_count + c->short_count + c->int_count + c->long_count + sign_count;
  int other_words = c->void_count + c->bool_count + c->float_count + c->double_count;

  if (fp_suffix_count && !c->float_count && !c->double_count)
    return PRIM_NONE;
  if (c->void_count)
    return integer_words + other_words == 1 ? PRIM_VOID : PRIM_NONE;
  if (c->bool_count)
    return integer_words + other_words == 1 ? PRIM_BOOL : PRIM_NONE;
  if (c->float_count)
    return integer_words + other_words == 1 ? PRIM_FLOAT : PRIM_NONE;
  if (c->double_count) {
    if (other_words != 1 || integer_words != c->long_count || c->long_count > 1)
      return PRIM_NONE;
    return c->long_count ? PRIM_LDOUBLE : PRIM_DOUBLE;
  }
  if (c->char_count) {
    if (c->short_count || c->int_count || c->long_count)
      return PRIM_NONE;
    if (c->signed_count)
      return PRIM_SCHAR;
    return c->unsigned_count ? PRIM_UCHAR : PRIM_CHAR;
  }
  if (c->short_count) {
    if (c->long_count)
      return PRIM_NONE;
    return c->unsigned_count ? PRIM_USHORT : PRIM_SHORT;
  }
  if (c->long_count == 2)
    return c->unsigned_count ? PRIM_ULLONG : PRIM_LLONG;
  if (c->long_count == 1)
    return c->unsigned_count ? PRIM_ULONG : PRIM_LONG;
  return c->unsigned_count ? PRIM_UINT : PRIM_INT;
}

static type_info *parse_type_specifier(parser *p, ast_node **out_def, decl_specs *out_specs) {
  if (out_def)
    *out_def = NULL;
  if (out_specs)
    *out_specs = (decl_specs){0};
  if (!is_type_token(p))
    return NULL;

  type_info *type = create_type_info(TYPE_PRIMITIVE);
  spec_counts counts = {0};
  int saw_arithmetic = 0;
  int named_count = 0;

  while (is_type_token(p) && p->current_token.type != TOKEN_EOF) {
    token_type t = p->current_token.type;

    if (t == TOKEN_CONST) {
      type->is_const = 1;
      parser_advance(p);
      continue;
    }
    if (t == TOKEN_VOLATILE) {
      type->is_volatile = 1;
      parser_advance(p);
      continue;
    }
    if (t == TOKEN_RESTRICT) {
      type->is_restrict = 1;
      parser_advance(p);
      continue;
    }
    if (t == TOKEN_INLINE) {
      if (out_specs)
        out_specs->is_inline = 1;
      else
        parser_error(p, "inline is not allowed in a type name");
      parser_advance(p);
      continue;
    }

    if (t == TOKEN_TYPEDEF || t == TOKEN_STATIC || t == TOKEN_EXTERN || t == TOKEN_REGISTER ||
        t == TOKEN_AUTO) {
      if (!out_specs)
        parser_error(p, "a storage class is not allowed in a type name");
      else if (out_specs->storage_class != 0)
        parser_error(p, "more than one storage class in a declaration");
      else
        out_specs->storage_class = t;
      parser_advance(p);
      continue;
    }

    if (count_arithmetic_specifier(&counts, t)) {
      saw_arithmetic = 1;
      parser_advance(p);
      continue;
    }

    if (t == TOKEN_STRUCT || t == TOKEN_UNION) {
      source_loc keyword = loc_of(p->current_token);
      type->kind = t == TOKEN_UNION ? TYPE_UNION : TYPE_STRUCT;
      named_count++;
      parser_advance(p);
      if (p->current_token.type == TOKEN_IDENTIFIER) {
        free(type->tag_name);
        type->tag_name = strdup(p->current_token.value);
        parser_advance(p);
      } else if (p->current_token.type != TOKEN_LBRACE) {
        parser_error(p, "expected a tag name or '{'");
      }
      if (p->current_token.type == TOKEN_LBRACE && out_def && *out_def == NULL) {
        parser_advance(p);
        ast_node *def = create_ast_node(AST_NODE_TYPE_STRUCT_DEF);
        def->loc = keyword;
        def->struct_def.tag_name = type->tag_name ? strdup(type->tag_name) : NULL;
        def->struct_def.members = NULL;
        def->struct_def.member_count = 0;
        def->struct_def.is_union = t == TOKEN_UNION;

        while (p->current_token.type != TOKEN_RBRACE && p->current_token.type != TOKEN_EOF) {
          if (!is_type_token(p))
            parser_error(p, "expected a member declaration");
          ast_node *member = is_type_token(p) ? parse_declaration(p, DECLARATION_MEMBER) : NULL;
          if (member) {
            def->struct_def.members = realloc(
                def->struct_def.members, sizeof(ast_node *) * (def->struct_def.member_count + 1));
            def->struct_def.members[def->struct_def.member_count++] = member;
          } else {
            parser_advance(p);
          }
        }
        if (def->struct_def.member_count == 0)
          parser_error(p, "a struct or union needs at least one member");
        if (p->current_token.type == TOKEN_RBRACE)
          parser_advance(p);
        else
          parser_error(p, "expected '}' to close the member list");
        type->definition = def;
        *out_def = def;
      }
      continue;
    }

    if (t == TOKEN_ENUM) {
      source_loc keyword = loc_of(p->current_token);
      type->kind = TYPE_ENUM;
      named_count++;
      parser_advance(p);
      if (p->current_token.type == TOKEN_IDENTIFIER) {
        free(type->tag_name);
        type->tag_name = strdup(p->current_token.value);
        parser_advance(p);
      } else if (p->current_token.type != TOKEN_LBRACE) {
        parser_error(p, "expected a tag name or '{'");
      }
      if (p->current_token.type == TOKEN_LBRACE && out_def && *out_def == NULL) {
        parser_advance(p);
        ast_node *def = create_ast_node(AST_NODE_TYPE_ENUM_DEF);
        def->loc = keyword;
        def->enum_def.tag_name = type->tag_name ? strdup(type->tag_name) : NULL;
        def->enum_def.enumerators = NULL;
        def->enum_def.values = NULL;
        def->enum_def.enumerator_count = 0;

        while (p->current_token.type != TOKEN_RBRACE && p->current_token.type != TOKEN_EOF) {
          if (p->current_token.type == TOKEN_IDENTIFIER) {
            def->enum_def.enumerators = realloc(
                def->enum_def.enumerators, sizeof(char *) * (def->enum_def.enumerator_count + 1));
            def->enum_def.values = realloc(
                def->enum_def.values, sizeof(ast_node *) * (def->enum_def.enumerator_count + 1));
            def->enum_def.enumerators[def->enum_def.enumerator_count] =
                strdup(p->current_token.value);
            def->enum_def.values[def->enum_def.enumerator_count] = NULL;

            parser_advance(p);
            if (p->current_token.type == TOKEN_ASSIGN) {
              parser_advance(p);
              ast_node *val = parse_assignment(p);
              def->enum_def.values[def->enum_def.enumerator_count] = val;
            }
            parser_define_symbol(p, def->enum_def.enumerators[def->enum_def.enumerator_count],
                                 PARSER_SYMBOL_ORDINARY);
            def->enum_def.enumerator_count++;
          }
          if (p->current_token.type == TOKEN_COMMA)
            parser_advance(p);
          else
            break;
        }
        if (def->enum_def.enumerator_count == 0)
          parser_error(p, "an enum needs at least one enumerator");
        if (p->current_token.type == TOKEN_RBRACE)
          parser_advance(p);
        else
          parser_error(p, "expected '}' to close the enumerator list");
        type->definition = def;
        *out_def = def;
      }
      continue;
    }

    if (t == TOKEN_IDENTIFIER) {
      if (named_count == 0 && !saw_arithmetic) {
        parser_symbol *sym = parser_lookup_symbol(p, p->current_token.value);
        if (sym && sym->kind == PARSER_SYMBOL_TYPEDEF) {
          type->kind = TYPE_TYPEDEF;
          type->tag_name = strdup(p->current_token.value);
          named_count++;
          parser_advance(p);
          continue;
        }
      }
      break;
    }

    break;
  }

  type->is_complex = counts.complex_count > 0;
  type->is_imaginary = counts.imaginary_count > 0;

  if (named_count > 0) {
    if (named_count > 1 || saw_arithmetic)
      parser_error(p, "two or more data types in declaration specifiers");
    return type;
  }
  if (!saw_arithmetic) {
    parser_error(p, "a declaration needs a type specifier");
    type->prim = PRIM_INT;
    return type;
  }
  type->prim = resolve_arithmetic_type(&counts);
  if (type->prim == PRIM_NONE) {
    parser_error(p, "invalid combination of type specifiers");
    type->prim = PRIM_INT;
  }
  return type;
}

static void set_param_definition(type_info *fn, int index, ast_node *definition) {
  if (!fn->param_definitions)
    fn->param_definitions = calloc(fn->param_count, sizeof(ast_node *));
  fn->param_definitions[index] = definition;
}

static void set_param_register(type_info *fn, int index) {
  if (!fn->param_register)
    fn->param_register = calloc(fn->param_count, sizeof(int));
  if (fn->param_register)
    fn->param_register[index] = 1;
}

static void add_param(type_info *fn, char *name, type_info *type, ast_node *definition,
                      int is_register) {
  fn->param_types = realloc(fn->param_types, sizeof(type_info *) * (fn->param_count + 1));
  fn->param_names = realloc(fn->param_names, sizeof(char *) * (fn->param_count + 1));
  fn->param_types[fn->param_count] = type;
  fn->param_names[fn->param_count] = name;
  fn->param_count++;
  if (fn->param_definitions) {
    fn->param_definitions =
        realloc(fn->param_definitions, sizeof(ast_node *) * (size_t)fn->param_count);
    fn->param_definitions[fn->param_count - 1] = NULL;
  }
  if (fn->param_register) {
    fn->param_register = realloc(fn->param_register, sizeof(int) * (size_t)fn->param_count);
    fn->param_register[fn->param_count - 1] = 0;
  }
  if (definition)
    set_param_definition(fn, fn->param_count - 1, definition);
  if (is_register)
    set_param_register(fn, fn->param_count - 1);
}

static void check_declarator(parser *p, type_info *type, int is_parameter, int is_definition) {
  for (type_info *t = type; t; t = t->ptr_to) {
    if (t->kind == TYPE_FUNCTION && !t->has_prototype && t->param_count > 0 &&
        !(is_definition && t == type))
      parser_error(p, "an identifier list is only allowed in a function definition");
    if (t->kind != TYPE_ARRAY)
      continue;
    if ((t->array_static || t->is_const || t->is_volatile || t->is_restrict) &&
        !(is_parameter && t == type))
      parser_error(p, "static and qualifiers in [] belong only to a parameter's outermost array");
    if (t->array_star && !is_parameter)
      parser_error(p, "[*] is only allowed in a function prototype");
  }
}

static int parse_identifier_list(parser *p, type_info *fn) {
  do {
    if (p->current_token.type != TOKEN_IDENTIFIER) {
      parser_error(p, "expected a parameter name");
      return 0;
    }
    add_param(fn, strdup(p->current_token.value), NULL, NULL, 0);
    parser_advance(p);
    if (p->current_token.type != TOKEN_COMMA)
      break;
    parser_advance(p);
  } while (1);
  if (p->current_token.type != TOKEN_RPAREN) {
    parser_error(p, "expected ')' after parameter list");
    return 0;
  }
  parser_advance(p);
  return 1;
}

static int parse_params(parser *p, type_info *fn) {
  if (p->current_token.type == TOKEN_RPAREN) {
    parser_advance(p);
    return 1;
  }
  if (p->current_token.type == TOKEN_IDENTIFIER && !token_starts_type(p, p->current_token))
    return parse_identifier_list(p, fn);
  fn->has_prototype = 1;

  do {
    if (p->current_token.type == TOKEN_ELLIPSIS) {
      if (fn->param_count == 0)
        parser_error(p, "a named parameter must come before '...'");
      parser_advance(p);
      fn->is_variadic = 1;
      break;
    }

    decl_specs p_specs;
    ast_node *p_def = NULL;
    type_info *p_base = parse_type_specifier(p, &p_def, &p_specs);
    if (!p_base) {
      parser_error(p, "expected a parameter type");
      return 0;
    }
    if ((p_specs.storage_class != 0 && p_specs.storage_class != TOKEN_REGISTER) ||
        p_specs.is_inline)
      parser_error(p, "only register may appear in a parameter declaration");

    char *p_name = NULL;
    type_info *p_type = parse_declarator(p, clone_type_info(p_base), &p_name, NULL, 1);
    free_type_info(p_base);
    if (!p_type) {
      free(p_name);
      free_ast(p_def);
      return 0;
    }
    check_declarator(p, p_type, 1, 0);
    if (p_name)
      parser_define_symbol(p, p_name, PARSER_SYMBOL_ORDINARY);
    add_param(fn, p_name, p_type, p_def, p_specs.storage_class == TOKEN_REGISTER);

    if (p->current_token.type != TOKEN_COMMA)
      break;
    parser_advance(p);
  } while (1);

  if (fn->param_count == 1 && !fn->param_names[0] && !fn->is_variadic &&
      fn->param_types[0]->kind == TYPE_PRIMITIVE && fn->param_types[0]->prim == PRIM_VOID &&
      !fn->param_types[0]->is_const && !fn->param_types[0]->is_volatile &&
      !fn->param_types[0]->is_restrict) {
    free_type_info(fn->param_types[0]);
    free(fn->param_types);
    fn->param_types = NULL;
    free(fn->param_names);
    fn->param_names = NULL;
    free(fn->param_register);
    fn->param_register = NULL;
    fn->param_count = 0;
  }

  if (p->current_token.type != TOKEN_RPAREN) {
    parser_error(p, "expected ')' after parameter list");
    return 0;
  }
  parser_advance(p);
  return 1;
}

static int parse_param_list(parser *p, type_info *fn) {
  parser_enter_scope(p);
  int ok = parse_params(p, fn);
  parser_leave_scope(p);
  return ok;
}

static int starts_parameter_list(parser *p) {
  return p->next_token.type == TOKEN_RPAREN || p->next_token.type == TOKEN_ELLIPSIS ||
         starts_specifiers(p, p->next_token);
}

static type_info *parse_declarator(parser *p, type_info *base_type, char **out_name,
                                   source_loc *out_name_loc, int allow_abstract) {
  type_info *type = base_type;
  if (out_name)
    *out_name = NULL;
  if (out_name_loc)
    *out_name_loc = loc_of(p->current_token);
  while (p->current_token.type == TOKEN_STAR) {
    parser_advance(p);
    type_info *ptr = create_type_info(TYPE_POINTER);
    while (p->current_token.type == TOKEN_CONST || p->current_token.type == TOKEN_VOLATILE ||
           p->current_token.type == TOKEN_RESTRICT) {
      if (p->current_token.type == TOKEN_CONST)
        ptr->is_const = 1;
      if (p->current_token.type == TOKEN_VOLATILE)
        ptr->is_volatile = 1;
      if (p->current_token.type == TOKEN_RESTRICT)
        ptr->is_restrict = 1;
      parser_advance(p);
    }
    ptr->ptr_to = type;
    type = ptr;
  }

  type_info *placeholder = NULL;
  type_info *inner = NULL;

  if (p->current_token.type == TOKEN_LPAREN && (!allow_abstract || !starts_parameter_list(p))) {
    parser_advance(p);
    placeholder = create_type_info(TYPE_UNKNOWN);
    inner = parse_declarator(p, placeholder, out_name, out_name_loc, allow_abstract);
    if (!inner) {
      free_type_info(type);
      return NULL;
    }
    if (p->current_token.type != TOKEN_RPAREN) {
      parser_error(p, "expected ')' closing declarator");
      free_type_info(inner);
      free_type_info(type);
      return NULL;
    }
    parser_advance(p);
  } else if (p->current_token.type == TOKEN_IDENTIFIER) {
    if (out_name)
      *out_name = strdup(p->current_token.value);
    if (out_name_loc)
      *out_name_loc = loc_of(p->current_token);
    parser_advance(p);
  }

  type_info *sfx_head = NULL;
  type_info *sfx_tail = NULL;
  int suffix_failed = 0;

  while (p->current_token.type == TOKEN_LBRACKET || p->current_token.type == TOKEN_LPAREN) {
    type_info *sfx;

    if (p->current_token.type == TOKEN_LBRACKET) {
      parser_advance(p);
      sfx = create_type_info(TYPE_ARRAY);
      while (p->current_token.type == TOKEN_CONST || p->current_token.type == TOKEN_VOLATILE ||
             p->current_token.type == TOKEN_RESTRICT || p->current_token.type == TOKEN_STATIC) {
        token_type qualifier = p->current_token.type;
        if (qualifier == TOKEN_CONST)
          sfx->is_const = 1;
        else if (qualifier == TOKEN_VOLATILE)
          sfx->is_volatile = 1;
        else if (qualifier == TOKEN_RESTRICT)
          sfx->is_restrict = 1;
        else if (sfx->array_static)
          parser_error(p, "static appears twice in an array declarator");
        else
          sfx->array_static = 1;
        parser_advance(p);
      }
      if (p->current_token.type == TOKEN_STAR && p->next_token.type == TOKEN_RBRACKET) {
        sfx->array_star = 1;
        parser_advance(p);
      } else if (p->current_token.type != TOKEN_RBRACKET) {
        sfx->array_size_expr = parse_assignment(p);
      }
      if (sfx->array_static && !sfx->array_size_expr)
        parser_error(p, "static in an array declarator needs a size");
      if (p->current_token.type != TOKEN_RBRACKET) {
        parser_error(p, "expected ']' after array size");
        free_type_info(sfx);
        suffix_failed = 1;
        break;
      }
      parser_advance(p);
    } else {
      parser_advance(p);
      sfx = create_type_info(TYPE_FUNCTION);
      if (!parse_param_list(p, sfx)) {
        free_type_info(sfx);
        suffix_failed = 1;
        break;
      }
    }

    if (!sfx_head) {
      sfx_head = sfx;
      sfx_tail = sfx;
    } else {
      sfx_tail->ptr_to = sfx;
      sfx_tail = sfx;
    }
  }

  if (suffix_failed) {
    if (sfx_head)
      free_type_info(sfx_head);
    free_type_info(type);
    if (inner)
      free_type_info(inner);
    return NULL;
  }

  if (sfx_head) {
    sfx_tail->ptr_to = type;
    type = sfx_head;
  }

  if (placeholder) {
    if (inner == placeholder) {
      free_type_info(placeholder);
      return type;
    }
    type_info *it = inner;
    while (it->ptr_to && it->ptr_to != placeholder)
      it = it->ptr_to;
    if (it->ptr_to != placeholder) {
      parser_error(p, "malformed declarator");
      free_type_info(inner);
      free_type_info(type);
      return NULL;
    }
    it->ptr_to = type;
    free_type_info(placeholder);
    return inner;
  }

  return type;
}

static ast_node *parse_initializer(parser *p) {
  if (p->current_token.type == TOKEN_LBRACE) {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    ast_node *node = create_ast_node(AST_NODE_TYPE_INIT_LIST);
    node->loc = loc;
    node->init_list.items = NULL;
    node->init_list.count = 0;
    if (p->current_token.type == TOKEN_RBRACE)
      parser_error(p, "an initializer list needs at least one initializer");

    while (p->current_token.type != TOKEN_RBRACE && p->current_token.type != TOKEN_EOF) {
      char *member_name = NULL;
      ast_node *index = NULL;

      char **chain_names = NULL;
      ast_node **chain_indices = NULL;
      int chain_count = 0;
      int errors_before_designators = p->had_error;

      while (p->current_token.type == TOKEN_DOT || p->current_token.type == TOKEN_LBRACKET) {
        char *this_name = NULL;
        ast_node *this_index = NULL;

        if (p->current_token.type == TOKEN_DOT) {
          parser_advance(p);
          if (p->current_token.type != TOKEN_IDENTIFIER) {
            parser_error(p, "expected a member name after '.' in an initializer");
            break;
          }
          this_name = strdup(p->current_token.value);
          parser_advance(p);
        } else {
          parser_advance(p);
          this_index = parse_expression(p);
          if (p->current_token.type == TOKEN_RBRACKET) {
            parser_advance(p);
          } else {
            parser_error(p, "expected ']' after an array designator");
          }
        }

        char **tmp_names = realloc(chain_names, sizeof(char *) * (chain_count + 1));
        ast_node **tmp_indices = realloc(chain_indices, sizeof(ast_node *) * (chain_count + 1));
        if (!tmp_names || !tmp_indices) {
          free(tmp_names ? tmp_names : chain_names);
          free(tmp_indices ? tmp_indices : chain_indices);
          chain_names = NULL;
          chain_indices = NULL;
          chain_count = 0;
          free(this_name);
          free_ast(this_index);
          break;
        }
        chain_names = tmp_names;
        chain_indices = tmp_indices;
        chain_names[chain_count] = this_name;
        chain_indices[chain_count] = this_index;
        chain_count++;
      }

      if (chain_count > 0 && p->current_token.type == TOKEN_ASSIGN)
        parser_advance(p);
      else if (chain_count > 0 && p->had_error == errors_before_designators)
        parser_error(p, "expected '=' after a designator");

      ast_node *val = parse_initializer(p);

      if (val) {
        for (int k = chain_count - 1; k >= 1; k--) {
          ast_node *wrap = create_ast_node(AST_NODE_TYPE_INIT_LIST);
          wrap->loc = node->loc;
          wrap->init_list.items = malloc(sizeof(*wrap->init_list.items));
          if (!wrap->init_list.items) {
            free_ast(wrap);
            break;
          }
          wrap->init_list.items[0].member_name = chain_names[k];
          wrap->init_list.items[0].index = chain_indices[k];
          wrap->init_list.items[0].value = val;
          wrap->init_list.count = 1;
          wrap->init_list.is_designation = 1;
          val = wrap;
        }
        if (chain_count > 0) {
          member_name = chain_names[0];
          index = chain_indices[0];
        }
      } else {
        for (int k = 0; k < chain_count; k++) {
          free(chain_names[k]);
          free_ast(chain_indices[k]);
        }
      }
      free(chain_names);
      free(chain_indices);

      if (val) {
        node->init_list.items = realloc(node->init_list.items, sizeof(*node->init_list.items) *
                                                                   (node->init_list.count + 1));
        node->init_list.items[node->init_list.count].member_name = member_name;
        node->init_list.items[node->init_list.count].index = index;
        node->init_list.items[node->init_list.count].value = val;
        node->init_list.count++;
      } else {
        if (member_name)
          free(member_name);
        if (index)
          free_ast(index);
        if (p->current_token.type != TOKEN_RBRACE)
          parser_advance(p);
      }
      if (p->current_token.type == TOKEN_COMMA) {
        parser_advance(p);
      } else {
        break;
      }
    }
    if (p->current_token.type == TOKEN_RBRACE)
      parser_advance(p);
    else
      parser_error(p, "expected '}' to close the initializer list");
    return node;
  }
  return parse_assignment(p);
}

static void append_item(ast_node *group, ast_node *item) {
  group->block.statements =
      realloc(group->block.statements, sizeof(ast_node *) * (group->block.count + 1));
  group->block.statements[group->block.count++] = item;
}

static ast_node *finish_group(ast_node *group) {
  if (group->block.count != 1)
    return group;
  ast_node *single = group->block.statements[0];
  free(group->block.statements);
  free(group);
  return single;
}

static ast_node *declaration_without_declarators(parser *p, type_info *base_type, ast_node *group,
                                                 source_loc start, declaration_context context) {
  int declares_tags = context == DECLARATION_FILE || context == DECLARATION_BLOCK;
  ast_node *definition = group->block.count == 1 ? group->block.statements[0] : NULL;
  int untagged_record = definition && definition->type == AST_NODE_TYPE_STRUCT_DEF &&
                        !definition->struct_def.tag_name;
  if (declares_tags && definition && !untagged_record)
    return finish_group(group);
  if (declares_tags && base_type->tag_name &&
      (base_type->kind == TYPE_STRUCT || base_type->kind == TYPE_UNION)) {
    free_ast(group);
    ast_node *forward = create_ast_node(AST_NODE_TYPE_STRUCT_DEF);
    forward->loc = start;
    forward->struct_def.tag_name = strdup(base_type->tag_name);
    forward->struct_def.is_forward = 1;
    forward->struct_def.is_union = base_type->kind == TYPE_UNION;
    return forward;
  }
  parser_error(p, "declaration does not declare anything");
  return finish_group(group);
}

static int parameter_index(type_info *fn, const char *name) {
  for (int i = 0; i < fn->param_count; i++) {
    if (name && fn->param_names[i] && strcmp(fn->param_names[i], name) == 0)
      return i;
  }
  return -1;
}

static int parse_parameter_declarations(parser *p, type_info *fn) {
  while (is_type_token(p)) {
    ast_node *decl = parse_declaration(p, DECLARATION_PARAMETER);
    if (!decl)
      return 0;
    int is_group = decl->type == AST_NODE_TYPE_DECL_GROUP;
    ast_node **items = is_group ? decl->block.statements : &decl;
    int count = is_group ? decl->block.count : 1;
    ast_node *definition = NULL;
    for (int i = 0; i < count; i++) {
      ast_node *item = items[i];
      if (item->type != AST_NODE_TYPE_VAR_DECL) {
        definition = item;
        items[i] = NULL;
        continue;
      }
      int index = parameter_index(fn, item->var_decl.var_name);
      if (index < 0) {
        parser_error(p, "this declaration names no parameter of the function");
        continue;
      }
      if (fn->param_types[index]) {
        parser_error(p, "a parameter is declared twice");
        continue;
      }
      fn->param_types[index] = item->var_decl.type;
      item->var_decl.type = NULL;
      if (item->var_decl.specs.storage_class == TOKEN_REGISTER)
        set_param_register(fn, index);
      if (definition) {
        set_param_definition(fn, index, definition);
        definition = NULL;
      }
    }
    free_ast(definition);
    free_ast(decl);
  }
  for (int i = 0; i < fn->param_count; i++) {
    if (!fn->param_types[i]) {
      parser_error(p, "a parameter in the identifier list is not declared");
      fn->param_types[i] = create_type_info(TYPE_PRIMITIVE);
      fn->param_types[i]->prim = PRIM_INT;
    }
  }
  return 1;
}

static ast_node *parse_function_definition(parser *p, char *name, source_loc name_loc,
                                           type_info *type, decl_specs specs) {
  parser_enter_scope(p);
  for (int i = 0; i < type->param_count; i++) {
    if (type->param_names[i])
      parser_define_symbol(p, type->param_names[i], PARSER_SYMBOL_ORDINARY);
  }
  int declared = type->has_prototype || parse_parameter_declarations(p, type);
  ast_node *body = declared ? parse_block(p) : NULL;
  parser_leave_scope(p);
  if (!body) {
    free(name);
    free_type_info(type);
    return NULL;
  }
  ast_node *node = create_ast_node(AST_NODE_TYPE_FUNCTION_DEF);
  node->loc = name_loc;
  node->function_def.name = name;
  node->function_def.type = type;
  node->function_def.body = body;
  node->function_def.specs = specs;
  return node;
}

static ast_node *parse_declaration(parser *p, declaration_context context) {
  source_loc start = loc_of(p->current_token);
  ast_node *def_node = NULL;
  decl_specs specs;
  type_info *base_type = parse_type_specifier(p, &def_node, &specs);
  if (!base_type)
    return NULL;
  if (context == DECLARATION_MEMBER && (specs.storage_class != 0 || specs.is_inline))
    parser_error(p, "a member cannot have a storage class or be inline");
  if (context == DECLARATION_PARAMETER &&
      ((specs.storage_class != 0 && specs.storage_class != TOKEN_REGISTER) || specs.is_inline))
    parser_error(p, "only register may appear in a parameter declaration");

  ast_node *group = create_ast_node(AST_NODE_TYPE_DECL_GROUP);
  group->loc = start;
  if (def_node)
    append_item(group, def_node);
  int first = group->block.count;

  do {
    type_info *base_copy = clone_type_info(base_type);
    char *name = NULL;
    source_loc name_loc;
    type_info *type = parse_declarator(p, base_copy, &name, &name_loc, 0);
    int failed = !type;
    ast_node *width = NULL;
    if (!failed && context == DECLARATION_MEMBER && p->current_token.type == TOKEN_COLON) {
      parser_advance(p);
      width = parse_ternary(p);
      failed = !width;
    }
    if (failed) {
      free(name);
      free_type_info(type);
      free_type_info(base_type);
      free_ast(group);
      return NULL;
    }

    if (!name && !width) {
      int bare = type == base_copy && group->block.count == first &&
                 p->current_token.type == TOKEN_SEMICOLON;
      free_type_info(type);
      if (!bare) {
        parser_error(p, "expected a name in this declaration");
        free_type_info(base_type);
        free_ast(group);
        return NULL;
      }
      ast_node *result = declaration_without_declarators(p, base_type, group, start, context);
      parser_advance(p);
      free_type_info(base_type);
      return result;
    }

    if (name && context != DECLARATION_MEMBER)
      parser_define_symbol(p, name,
                           specs.storage_class == TOKEN_TYPEDEF ? PARSER_SYMBOL_TYPEDEF
                                                                : PARSER_SYMBOL_ORDINARY);

    int identifier_list =
        type->kind == TYPE_FUNCTION && !type->has_prototype && type->param_count > 0;
    int definition =
        context == DECLARATION_FILE && group->block.count == first && type->kind == TYPE_FUNCTION &&
        specs.storage_class != TOKEN_TYPEDEF &&
        (p->current_token.type == TOKEN_LBRACE || (identifier_list && is_type_token(p)));
    check_declarator(p, type, 0, definition);
    if (definition) {
      free_type_info(base_type);
      ast_node *function = parse_function_definition(p, name, name_loc, type, specs);
      if (!function) {
        free_ast(group);
        return NULL;
      }
      append_item(group, function);
      return finish_group(group);
    }

    ast_node *init = NULL;
    if (p->current_token.type == TOKEN_ASSIGN) {
      int allowed = (context == DECLARATION_FILE || context == DECLARATION_BLOCK) &&
                    specs.storage_class != TOKEN_TYPEDEF;
      if (context == DECLARATION_MEMBER)
        parser_error(p, "a member cannot have an initializer");
      else if (context == DECLARATION_PARAMETER)
        parser_error(p, "a parameter cannot have an initializer");
      else if (!allowed)
        parser_error(p, "a typedef cannot have an initializer");
      parser_advance(p);
      init = parse_initializer(p);
      if (!allowed) {
        free_ast(init);
        init = NULL;
      }
    }

    ast_node *decl = create_ast_node(AST_NODE_TYPE_VAR_DECL);
    decl->loc = name_loc;
    decl->var_decl.type = type;
    decl->var_decl.var_name = name;
    decl->var_decl.init_value = init;
    decl->var_decl.bitfield_width = width;
    decl->var_decl.specs = specs;
    append_item(group, decl);

    if (p->current_token.type != TOKEN_COMMA)
      break;
    parser_advance(p);
  } while (1);

  free_type_info(base_type);
  if (p->current_token.type != TOKEN_SEMICOLON) {
    parser_error(p, "expected ';' after declaration");
    free_ast(group);
    return NULL;
  }
  parser_advance(p);
  return finish_group(group);
}
void parser_enter_scope(parser *p) {
  parser_scope *new_scope = (parser_scope *)calloc(1, sizeof(parser_scope));
  new_scope->parent = p->current_scope;
  p->current_scope = new_scope;
}

void parser_leave_scope(parser *p) {
  if (!p->current_scope)
    return;
  parser_scope *old = p->current_scope;
  p->current_scope = old->parent;

  parser_symbol *curr = old->symbols;
  while (curr) {
    parser_symbol *next = curr->next;
    free(curr->name);
    free(curr);
    curr = next;
  }
  free(old);
}

void parser_define_symbol(parser *p, const char *name, parser_symbol_kind kind) {
  if (!p->current_scope)
    return;
  parser_symbol *sym = (parser_symbol *)calloc(1, sizeof(parser_symbol));
  sym->name = strdup(name);
  sym->kind = kind;
  sym->next = p->current_scope->symbols;
  p->current_scope->symbols = sym;
}

parser_symbol *parser_lookup_symbol(parser *p, const char *name) {
  parser_scope *curr_scope = p->current_scope;
  while (curr_scope) {
    parser_symbol *curr_sym = curr_scope->symbols;
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

void parser_destroy(parser *p) {
  if (!p)
    return;
  token_buf_free(&p->tokens);
  while (p->current_scope) {
    parser_leave_scope(p);
  }
}

void parser_advance(parser *p) {
  if (!p)
    return;
  p->current_token = p->next_token;
  p->next_token = token_buf_next(&p->tokens);
}

static void parser_begin(parser *p) {
  p->current_scope = NULL;
  p->had_error = 0;
  parser_enter_scope(p);
  p->current_token = token_buf_next(&p->tokens);
  p->next_token = token_buf_next(&p->tokens);
}

void parser_init(parser *p, lexer *lex) {
  if (!p || !lex)
    return;
  token_buf_init(&p->tokens);
  token t;
  do {
    t = lexer_next_token(lex);
    token_buf_push(&p->tokens, t);
  } while (t.type != TOKEN_EOF);
  parser_begin(p);
}

void parser_init_from_buf(parser *p, token_buf *tb) {
  if (!p || !tb)
    return;
  p->tokens = *tb;
  p->tokens.pos = 0;
  token_buf_init(tb);
  parser_begin(p);
}

ast_node *parse_shift(parser *p) {
  ast_node *left = parse_additive(p);
  while (p->current_token.type == TOKEN_LSHIFT || p->current_token.type == TOKEN_RSHIFT) {
    token op = clone_token(p->current_token);
    parser_advance(p);
    ast_node *right = parse_additive(p);

    ast_node *new_node = create_ast_node(AST_NODE_TYPE_BINARY_OP);
    new_node->loc = loc_of(op);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_relational(parser *p) {
  ast_node *left = parse_shift(p);
  while (p->current_token.type == TOKEN_LT || p->current_token.type == TOKEN_GT ||
         p->current_token.type == TOKEN_LTE || p->current_token.type == TOKEN_GTE) {

    token op = clone_token(p->current_token);
    parser_advance(p);

    ast_node *right = parse_shift(p);

    ast_node *new_node = create_ast_node(AST_NODE_TYPE_BINARY_OP);
    new_node->loc = loc_of(op);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_equality(parser *p) {
  ast_node *left = parse_relational(p);
  while (p->current_token.type == TOKEN_EQ || p->current_token.type == TOKEN_NEQ) {

    token op = clone_token(p->current_token);
    parser_advance(p);

    ast_node *right = parse_relational(p);

    ast_node *new_node = create_ast_node(AST_NODE_TYPE_BINARY_OP);
    new_node->loc = loc_of(op);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_primary(parser *p) {
  if (!p)
    return NULL;
  switch (p->current_token.type) {
  case TOKEN_NUMBER: {
    ast_node *node = create_ast_node(AST_NODE_TYPE_NUMBER);
    node->tok = clone_token(p->current_token);
    node->loc = loc_of(node->tok);
    const char *error;
    if (classify_number(node->tok.value ? node->tok.value : "", &error) == NUMBER_INVALID)
      parser_error(p, error);
    parser_advance(p);
    return node;
  }
  case TOKEN_IDENTIFIER: {
    ast_node *node = create_ast_node(AST_NODE_TYPE_IDENTIFIER);
    node->tok = clone_token(p->current_token);
    node->loc = loc_of(node->tok);
    parser_advance(p);
    return node;
  }
  case TOKEN_STRING:
  case TOKEN_WIDE_STRING: {
    ast_node *node = create_ast_node(AST_NODE_TYPE_STRING);
    node->tok = clone_token(p->current_token);
    node->loc = loc_of(node->tok);
    token *pieces = malloc(sizeof(token));
    int count = 0;
    if (pieces)
      pieces[count++] = p->current_token;
    node->literal.is_wide = p->current_token.type == TOKEN_WIDE_STRING;
    parser_advance(p);
    while (pieces &&
           (p->current_token.type == TOKEN_STRING || p->current_token.type == TOKEN_WIDE_STRING)) {
      token *grown = realloc(pieces, (count + 1) * sizeof(token));
      if (!grown)
        break;
      pieces = grown;
      pieces[count++] = p->current_token;
      if (p->current_token.type == TOKEN_WIDE_STRING)
        node->literal.is_wide = 1;
      size_t have = node->tok.value ? strlen(node->tok.value) : 0;
      size_t add = p->current_token.value ? strlen(p->current_token.value) : 0;
      char *joined = malloc(have + add + 1);
      if (!joined)
        break;
      if (node->tok.value)
        memcpy(joined, node->tok.value, have);
      if (p->current_token.value)
        memcpy(joined + have, p->current_token.value, add);
      joined[have + add] = '\0';
      free(node->tok.value);
      node->tok.value = joined;
      parser_advance(p);
    }
    decode_pieces(p, node, pieces, count);
    free(pieces);
    return node;
  }
  case TOKEN_CHAR_LITERAL:
  case TOKEN_WIDE_CHAR: {
    ast_node *node = create_ast_node(AST_NODE_TYPE_CHAR_LITERAL);
    node->tok = clone_token(p->current_token);
    node->loc = loc_of(node->tok);
    node->literal.is_wide = p->current_token.type == TOKEN_WIDE_CHAR;
    decode_pieces(p, node, &p->current_token, 1);
    parser_advance(p);
    return node;
  }
  case TOKEN_LPAREN: {
    parser_advance(p);
    ast_node *node = parse_expression(p);
    if (p->current_token.type != TOKEN_RPAREN) {
      parser_error(p, "expected ')' to close parenthesised expression");
      free_ast(node);
      return NULL;
    }
    parser_advance(p);
    return node;
  }
  default:
    parser_error(p, "expected an expression");
    return NULL;
  }
}
static ast_node *parse_postfix_operators(parser *p, ast_node *left) {
  while (p->current_token.type == TOKEN_LPAREN || p->current_token.type == TOKEN_LBRACKET ||
         p->current_token.type == TOKEN_DOT || p->current_token.type == TOKEN_ARROW ||
         p->current_token.type == TOKEN_PLUS_PLUS || p->current_token.type == TOKEN_MINUS_MINUS) {

    if (p->current_token.type == TOKEN_LPAREN) {
      source_loc loc = loc_of(p->current_token);
      parser_advance(p);
      ast_node *node = create_ast_node(AST_NODE_TYPE_FUNCTION_CALL);
      node->loc = loc;
      node->function_call.callable = left;
      node->function_call.arguments = NULL;
      node->function_call.arg_count = 0;

      if (p->current_token.type != TOKEN_RPAREN) {
        do {
          ast_node *arg = parse_assignment(p);
          if (arg) {
            node->function_call.arguments =
                realloc(node->function_call.arguments,
                        sizeof(ast_node *) * (node->function_call.arg_count + 1));
            node->function_call.arguments[node->function_call.arg_count++] = arg;
          }
          if (p->current_token.type == TOKEN_COMMA) {
            parser_advance(p);
          } else {
            break;
          }
        } while (1);
      }
      if (p->current_token.type == TOKEN_RPAREN)
        parser_advance(p);
      else
        parser_error(p, "expected ')' after the arguments");
      left = node;
    } else if (p->current_token.type == TOKEN_LBRACKET) {
      source_loc loc = loc_of(p->current_token);
      parser_advance(p);
      ast_node *node = create_ast_node(AST_NODE_TYPE_ARRAY_SUBSCRIPT);
      node->loc = loc;
      node->array_subscript.left = left;
      node->array_subscript.index = parse_expression(p);
      if (p->current_token.type == TOKEN_RBRACKET)
        parser_advance(p);
      else
        parser_error(p, "expected ']' after the subscript");
      left = node;
    } else if (p->current_token.type == TOKEN_DOT || p->current_token.type == TOKEN_ARROW) {
      int is_pointer = (p->current_token.type == TOKEN_ARROW) ? 1 : 0;
      source_loc loc = loc_of(p->current_token);
      parser_advance(p);

      ast_node *node = create_ast_node(AST_NODE_TYPE_MEMBER_ACCESS);
      node->loc = loc;
      node->member_access.left = left;
      node->member_access.is_pointer = is_pointer;

      if (p->current_token.type == TOKEN_IDENTIFIER) {
        node->member_access.member_name = strdup(p->current_token.value);
        parser_advance(p);
      } else {
        parser_error(p, "expected a member name");
        node->member_access.member_name = strdup("unknown");
      }
      left = node;
    } else {
      token op = clone_token(p->current_token);
      parser_advance(p);
      ast_node *node = create_ast_node(AST_NODE_TYPE_UNARY_OP);
      node->loc = loc_of(op);
      node->unary_op.op = op;
      node->unary_op.operand = left;
      node->unary_op.is_postfix = 1;
      left = node;
    }
  }
  return left;
}

ast_node *parse_postfix(parser *p) {
  if (!p)
    return NULL;
  return parse_postfix_operators(p, parse_primary(p));
}
ast_node *parse_multiplicative(parser *p) {
  ast_node *left = parse_unary(p);

  while (p->current_token.type == TOKEN_STAR || p->current_token.type == TOKEN_SLASH ||
         p->current_token.type == TOKEN_PERCENT) {

    token op = clone_token(p->current_token);
    parser_advance(p);

    ast_node *right = parse_unary(p);

    ast_node *new_node = create_ast_node(AST_NODE_TYPE_BINARY_OP);
    new_node->loc = loc_of(op);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }

  return left;
}
ast_node *parse_additive(parser *p) {
  ast_node *left = parse_multiplicative(p);

  while (p->current_token.type == TOKEN_PLUS || p->current_token.type == TOKEN_MINUS) {

    token op = clone_token(p->current_token);
    parser_advance(p);

    ast_node *right = parse_multiplicative(p);

    ast_node *new_node = create_ast_node(AST_NODE_TYPE_BINARY_OP);
    new_node->loc = loc_of(op);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }

  return left;
}
ast_node *parse_ternary(parser *p) {
  ast_node *cond = parse_logical_or(p);
  if (p && p->current_token.type == TOKEN_QUESTION) {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    ast_node *true_expr = parse_expression(p);
    if (p->current_token.type == TOKEN_COLON) {
      parser_advance(p);
    }
    ast_node *false_expr = parse_ternary(p);

    ast_node *node = create_ast_node(AST_NODE_TYPE_TERNARY);
    node->loc = loc;
    node->ternary.condition = cond;
    node->ternary.true_branch = true_expr;
    node->ternary.false_branch = false_expr;
    return node;
  }
  return cond;
}
ast_node *parse_assignment(parser *p) {
  ast_node *left = parse_ternary(p);
  if (p->current_token.type == TOKEN_ASSIGN || p->current_token.type == TOKEN_PLUS_ASSIGN ||
      p->current_token.type == TOKEN_MINUS_ASSIGN || p->current_token.type == TOKEN_STAR_ASSIGN ||
      p->current_token.type == TOKEN_SLASH_ASSIGN ||
      p->current_token.type == TOKEN_PERCENT_ASSIGN ||
      p->current_token.type == TOKEN_LSHIFT_ASSIGN ||
      p->current_token.type == TOKEN_RSHIFT_ASSIGN ||
      p->current_token.type == TOKEN_AMPERSAND_ASSIGN ||
      p->current_token.type == TOKEN_CARET_ASSIGN || p->current_token.type == TOKEN_PIPE_ASSIGN) {
    token op = clone_token(p->current_token);
    parser_advance(p);
    ast_node *right = parse_assignment(p);

    ast_node *node = create_ast_node(AST_NODE_TYPE_ASSIGNMENT);
    node->loc = loc_of(op);
    node->assignment.op = op;
    node->assignment.left = left;
    node->assignment.right = right;
    return node;
  }
  return left;
}
ast_node *parse_expression(parser *p) {
  ast_node *left = parse_assignment(p);
  while (p && p->current_token.type == TOKEN_COMMA) {
    token op = clone_token(p->current_token);
    parser_advance(p);
    ast_node *right = parse_assignment(p);

    ast_node *node = create_ast_node(AST_NODE_TYPE_BINARY_OP);
    node->loc = loc_of(op);
    node->binary_op.op = op;
    node->binary_op.left = left;
    node->binary_op.right = right;

    left = node;
  }
  return left;
}
ast_node *parse_block(parser *p) {
  if (p->current_token.type != TOKEN_LBRACE) {
    parser_error(p, "expected '{' to open a block");
    return NULL;
  }
  source_loc loc = loc_of(p->current_token);
  parser_advance(p);

  ast_node *node = create_ast_node(AST_NODE_TYPE_BLOCK);
  node->loc = loc;
  node->block.statements = NULL;
  node->block.count = 0;

  parser_enter_scope(p);

  while (p->current_token.type != TOKEN_RBRACE && p->current_token.type != TOKEN_EOF) {
    ast_node *stmt =
        starts_declaration(p) ? parse_declaration(p, DECLARATION_BLOCK) : parse_statement(p);
    if (stmt) {
      node->block.statements =
          realloc(node->block.statements, sizeof(ast_node *) * (node->block.count + 1));
      node->block.statements[node->block.count++] = stmt;
    } else {
      parser_leave_scope(p);
      free_ast(node);
      return NULL;
    }
  }

  parser_leave_scope(p);

  if (p->current_token.type != TOKEN_RBRACE) {
    parser_error(p, "expected '}' to close a block");
    free_ast(node);
    return NULL;
  }

  parser_advance(p);
  return node;
}
static ast_node *parse_substatement(parser *p) {
  parser_enter_scope(p);
  ast_node *node = parse_statement(p);
  parser_leave_scope(p);
  return node;
}

static ast_node *parse_unscoped_statement(parser *p);

ast_node *parse_statement(parser *p) {
  if (!p)
    return NULL;
  if (starts_declaration(p)) {
    parser_error(p, "a declaration is not a statement");
    return NULL;
  }
  token_type type = p->current_token.type;
  if (type != TOKEN_IF && type != TOKEN_WHILE && type != TOKEN_DO && type != TOKEN_FOR &&
      type != TOKEN_SWITCH)
    return parse_unscoped_statement(p);
  parser_enter_scope(p);
  ast_node *node = parse_unscoped_statement(p);
  parser_leave_scope(p);
  return node;
}

static ast_node *parse_unscoped_statement(parser *p) {
  switch (p->current_token.type) {
  case TOKEN_SEMICOLON: {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    ast_node *node = create_ast_node(AST_NODE_TYPE_EMPTY);
    node->loc = loc;
    return node;
  }
  case TOKEN_RETURN: {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    ast_node *node = create_ast_node(AST_NODE_TYPE_RETURN);
    node->loc = loc;
    if (p->current_token.type != TOKEN_SEMICOLON) {
      node->return_stmt.return_value = parse_expression(p);
    }
    if (p->current_token.type != TOKEN_SEMICOLON) {
      parser_error(p, "expected ';' after return statement");
      free_ast(node);
      return NULL;
    }
    parser_advance(p);
    return node;
  }
  case TOKEN_LBRACE:
    return parse_block(p);
  case TOKEN_IF: {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    if (p->current_token.type != TOKEN_LPAREN) {
      parser_error(p, "expected '(' after 'if'");
      return NULL;
    }
    parser_advance(p);
    ast_node *condition = parse_expression(p);
    if (p->current_token.type != TOKEN_RPAREN) {
      parser_error(p, "expected ')' after condition");
      return NULL;
    }
    parser_advance(p);
    ast_node *then_branch = parse_substatement(p);
    ast_node *else_branch = NULL;
    if (p->current_token.type == TOKEN_ELSE) {
      parser_advance(p);
      else_branch = parse_substatement(p);
    }

    ast_node *node = create_ast_node(AST_NODE_TYPE_IF);
    node->loc = loc;
    node->if_stmt.condition = condition;
    node->if_stmt.then_branch = then_branch;
    node->if_stmt.else_branch = else_branch;
    return node;
  }
  case TOKEN_WHILE: {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    if (p->current_token.type != TOKEN_LPAREN) {
      parser_error(p, "expected '(' after 'while'");
      return NULL;
    }
    parser_advance(p);
    ast_node *condition = parse_expression(p);
    if (p->current_token.type != TOKEN_RPAREN) {
      parser_error(p, "expected ')' after condition");
      return NULL;
    }
    parser_advance(p);
    ast_node *body = parse_substatement(p);

    ast_node *node = create_ast_node(AST_NODE_TYPE_WHILE);
    node->loc = loc;
    node->while_stmt.condition = condition;
    node->while_stmt.body = body;
    return node;
  }

  case TOKEN_FOR: {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    if (p->current_token.type != TOKEN_LPAREN) {
      parser_error(p, "expected '(' after 'for'");
      return NULL;
    }
    parser_advance(p);

    ast_node *init = NULL;
    if (is_type_token(p)) {
      init = parse_declaration(p, DECLARATION_BLOCK);
    } else {
      if (p->current_token.type != TOKEN_SEMICOLON) {
        init = parse_expression(p);
      }
      if (p->current_token.type != TOKEN_SEMICOLON) {
        parser_error(p, "expected ';' after the initialiser in 'for'");
        return NULL;
      }
      parser_advance(p);
    }

    ast_node *condition = NULL;
    if (p->current_token.type != TOKEN_SEMICOLON) {
      condition = parse_expression(p);
    }
    if (p->current_token.type != TOKEN_SEMICOLON) {
      parser_error(p, "expected ';' after the condition in 'for'");
      return NULL;
    }
    parser_advance(p);

    ast_node *increment = NULL;
    if (p->current_token.type != TOKEN_RPAREN) {
      increment = parse_expression(p);
    }
    if (p->current_token.type != TOKEN_RPAREN) {
      parser_error(p, "expected ')' after the increment in 'for'");
      return NULL;
    }
    parser_advance(p);

    ast_node *body = parse_substatement(p);

    ast_node *node = create_ast_node(AST_NODE_TYPE_FOR);
    node->loc = loc;
    node->for_stmt.init = init;
    node->for_stmt.condition = condition;
    node->for_stmt.increment = increment;
    node->for_stmt.body = body;
    return node;
  }

  case TOKEN_DO: {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    ast_node *body = parse_substatement(p);
    if (p->current_token.type != TOKEN_WHILE) {
      parser_error(p, "expected 'while' after the body of 'do'");
      return NULL;
    }
    parser_advance(p);
    if (p->current_token.type != TOKEN_LPAREN) {
      parser_error(p, "expected '(' after 'while'");
      return NULL;
    }
    parser_advance(p);
    ast_node *condition = parse_expression(p);
    if (p->current_token.type != TOKEN_RPAREN) {
      parser_error(p, "expected ')' after condition");
      return NULL;
    }
    parser_advance(p);
    if (p->current_token.type != TOKEN_SEMICOLON) {
      parser_error(p, "expected ';' after 'do ... while (...)'");
      return NULL;
    }
    parser_advance(p);

    ast_node *node = create_ast_node(AST_NODE_TYPE_DO_WHILE);
    node->loc = loc;
    node->do_while_stmt.body = body;
    node->do_while_stmt.condition = condition;
    return node;
  }

  case TOKEN_SWITCH: {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    if (p->current_token.type != TOKEN_LPAREN) {
      parser_error(p, "expected '(' after 'switch'");
      return NULL;
    }
    parser_advance(p);
    ast_node *condition = parse_expression(p);
    if (p->current_token.type != TOKEN_RPAREN) {
      parser_error(p, "expected ')' after the switch expression");
      return NULL;
    }
    parser_advance(p);
    ast_node *body = parse_substatement(p);

    ast_node *node = create_ast_node(AST_NODE_TYPE_SWITCH);
    node->loc = loc;
    node->switch_stmt.condition = condition;
    node->switch_stmt.body = body;
    return node;
  }

  case TOKEN_CASE: {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    ast_node *value = parse_expression(p);
    if (p->current_token.type != TOKEN_COLON) {
      parser_error(p, "expected ':' after the case label");
      free_ast(value);
      return NULL;
    }
    parser_advance(p);
    ast_node *body = parse_statement(p);

    ast_node *node = create_ast_node(AST_NODE_TYPE_CASE);
    node->loc = loc;
    node->case_stmt.value = value;
    node->case_stmt.body = body;
    return node;
  }

  case TOKEN_DEFAULT: {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    if (p->current_token.type != TOKEN_COLON) {
      parser_error(p, "expected ':' after 'default'");
      return NULL;
    }
    parser_advance(p);
    ast_node *body = parse_statement(p);

    ast_node *node = create_ast_node(AST_NODE_TYPE_DEFAULT);
    node->loc = loc;
    node->default_stmt.body = body;
    return node;
  }

  case TOKEN_GOTO: {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    if (p->current_token.type != TOKEN_IDENTIFIER) {
      parser_error(p, "expected a label name after 'goto'");
      return NULL;
    }
    ast_node *node = create_ast_node(AST_NODE_TYPE_GOTO);
    node->loc = loc;
    node->goto_stmt.label_name = strdup(p->current_token.value);
    parser_advance(p);
    if (p->current_token.type != TOKEN_SEMICOLON) {
      parser_error(p, "expected ';' after 'goto'");
      free_ast(node);
      return NULL;
    }
    parser_advance(p);
    return node;
  }

  case TOKEN_BREAK: {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    if (p->current_token.type != TOKEN_SEMICOLON) {
      parser_error(p, "expected ';' after 'break'");
      return NULL;
    }
    parser_advance(p);
    ast_node *node = create_ast_node(AST_NODE_TYPE_BREAK);
    node->loc = loc;
    return node;
  }

  case TOKEN_CONTINUE: {
    source_loc loc = loc_of(p->current_token);
    parser_advance(p);
    if (p->current_token.type != TOKEN_SEMICOLON) {
      parser_error(p, "expected ';' after 'continue'");
      return NULL;
    }
    parser_advance(p);
    ast_node *node = create_ast_node(AST_NODE_TYPE_CONTINUE);
    node->loc = loc;
    return node;
  }

  default: {
    if (p->current_token.type == TOKEN_IDENTIFIER && p->next_token.type == TOKEN_COLON) {
      ast_node *node = create_ast_node(AST_NODE_TYPE_LABEL);
      node->loc = loc_of(p->current_token);
      node->label_stmt.label_name = strdup(p->current_token.value);
      parser_advance(p);
      parser_advance(p);
      node->label_stmt.statement = parse_statement(p);
      return node;
    }

    ast_node *node = parse_expression(p);
    if (p->current_token.type != TOKEN_SEMICOLON) {
      parser_error(p, "expected ';' after expression statement");
      free_ast(node);
      return NULL;
    }
    parser_advance(p);
    return node;
  }
  }
}
ast_node *parse_bitwise_and(parser *p) {
  ast_node *left = parse_equality(p);
  while (p->current_token.type == TOKEN_AMPERSAND) {
    token op = clone_token(p->current_token);
    parser_advance(p);
    ast_node *right = parse_equality(p);

    ast_node *new_node = create_ast_node(AST_NODE_TYPE_BINARY_OP);
    new_node->loc = loc_of(op);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_bitwise_xor(parser *p) {
  ast_node *left = parse_bitwise_and(p);
  while (p->current_token.type == TOKEN_CARET) {
    token op = clone_token(p->current_token);
    parser_advance(p);
    ast_node *right = parse_bitwise_and(p);

    ast_node *new_node = create_ast_node(AST_NODE_TYPE_BINARY_OP);
    new_node->loc = loc_of(op);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_bitwise_or(parser *p) {
  ast_node *left = parse_bitwise_xor(p);
  while (p->current_token.type == TOKEN_PIPE) {
    token op = clone_token(p->current_token);
    parser_advance(p);
    ast_node *right = parse_bitwise_xor(p);

    ast_node *new_node = create_ast_node(AST_NODE_TYPE_BINARY_OP);
    new_node->loc = loc_of(op);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_logical_and(parser *p) {
  ast_node *left = parse_bitwise_or(p);
  while (p->current_token.type == TOKEN_AND) {
    token op = clone_token(p->current_token);
    parser_advance(p);
    ast_node *right = parse_bitwise_or(p);

    ast_node *new_node = create_ast_node(AST_NODE_TYPE_BINARY_OP);
    new_node->loc = loc_of(op);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
ast_node *parse_logical_or(parser *p) {
  ast_node *left = parse_logical_and(p);
  while (p->current_token.type == TOKEN_OR) {
    token op = clone_token(p->current_token);
    parser_advance(p);
    ast_node *right = parse_logical_and(p);

    ast_node *new_node = create_ast_node(AST_NODE_TYPE_BINARY_OP);
    new_node->loc = loc_of(op);
    new_node->binary_op.op = op;
    new_node->binary_op.left = left;
    new_node->binary_op.right = right;

    left = new_node;
  }
  return left;
}
static type_info *parse_type_name(parser *p, ast_node **definition) {
  type_info *base = parse_type_specifier(p, definition, NULL);
  if (!base)
    return NULL;
  type_info *decl = parse_declarator(p, clone_type_info(base), NULL, NULL, 1);
  free_type_info(base);
  if (decl)
    check_declarator(p, decl, 0, 0);
  return decl;
}

static type_info *parse_parenthesized_type_name(parser *p, ast_node **definition) {
  parser_advance(p);
  type_info *type = parse_type_name(p, definition);
  if (type && p->current_token.type == TOKEN_RPAREN) {
    parser_advance(p);
    return type;
  }
  if (type)
    parser_error(p, "expected ')' after a type name");
  free_type_info(type);
  free_ast(*definition);
  *definition = NULL;
  return NULL;
}

static ast_node *parse_compound_literal(parser *p, source_loc paren, type_info *type,
                                        ast_node *definition) {
  ast_node *node = create_ast_node(AST_NODE_TYPE_COMPOUND_LITERAL);
  node->loc = paren;
  node->compound_literal.type = type;
  node->compound_literal.definition = definition;
  node->compound_literal.init_list = parse_initializer(p);
  return parse_postfix_operators(p, node);
}

ast_node *parse_unary(parser *p) {
  if (p->current_token.type == TOKEN_LPAREN && token_starts_type(p, p->next_token)) {
    source_loc paren = loc_of(p->current_token);
    ast_node *definition = NULL;
    type_info *type = parse_parenthesized_type_name(p, &definition);
    if (!type)
      return NULL;
    if (p->current_token.type == TOKEN_LBRACE)
      return parse_compound_literal(p, paren, type, definition);
    ast_node *node = create_ast_node(AST_NODE_TYPE_CAST);
    node->loc = paren;
    node->cast_expr.type = type;
    node->cast_expr.definition = definition;
    node->cast_expr.operand = parse_unary(p);
    return node;
  }

  if (p->current_token.type == TOKEN_SIZEOF) {
    token op = clone_token(p->current_token);
    parser_advance(p);
    ast_node *operand;
    if (p->current_token.type == TOKEN_LPAREN && token_starts_type(p, p->next_token)) {
      source_loc paren = loc_of(p->current_token);
      ast_node *definition = NULL;
      type_info *type = parse_parenthesized_type_name(p, &definition);
      if (!type) {
        free(op.value);
        return NULL;
      }
      if (p->current_token.type == TOKEN_LBRACE) {
        operand = parse_compound_literal(p, paren, type, definition);
      } else {
        operand = create_ast_node(AST_NODE_TYPE_CAST);
        operand->loc = paren;
        operand->cast_expr.type = type;
        operand->cast_expr.definition = definition;
      }
    } else {
      operand = parse_unary(p);
    }
    ast_node *node = create_ast_node(AST_NODE_TYPE_UNARY_OP);
    node->loc = loc_of(op);
    node->unary_op.op = op;
    node->unary_op.operand = operand;
    node->unary_op.is_postfix = 0;
    return node;
  }

  if (p->current_token.type == TOKEN_NOT || p->current_token.type == TOKEN_MINUS ||
      p->current_token.type == TOKEN_TILDE || p->current_token.type == TOKEN_AMPERSAND ||
      p->current_token.type == TOKEN_STAR || p->current_token.type == TOKEN_PLUS_PLUS ||
      p->current_token.type == TOKEN_MINUS_MINUS || p->current_token.type == TOKEN_PLUS) {
    token op = clone_token(p->current_token);
    parser_advance(p);
    ast_node *operand = parse_unary(p);

    ast_node *node = create_ast_node(AST_NODE_TYPE_UNARY_OP);
    node->loc = loc_of(op);
    node->unary_op.op = op;
    node->unary_op.operand = operand;
    node->unary_op.is_postfix = 0;

    return node;
  } else {
    return parse_postfix(p);
  }
}

static void parser_recover_top_level(parser *p) {
  int depth = 0;
  while (p->current_token.type != TOKEN_EOF) {
    if (p->current_token.type == TOKEN_LBRACE) {
      depth++;
    } else if (p->current_token.type == TOKEN_RBRACE) {
      if (depth > 0)
        depth--;
      if (depth == 0) {
        parser_advance(p);
        return;
      }
    } else if (p->current_token.type == TOKEN_SEMICOLON && depth == 0) {
      parser_advance(p);
      return;
    }
    parser_advance(p);
  }
}

ast_node *parse_program(parser *p) {
  if (!p)
    return NULL;

  ast_node *prog_node = create_ast_node(AST_NODE_TYPE_PROGRAM);
  prog_node->loc = loc_of(p->current_token);
  prog_node->program.declarations = NULL;
  prog_node->program.count = 0;

  int stray_reported = 0;

  while (p->current_token.type != TOKEN_EOF) {
    if (is_type_token(p)) {
      stray_reported = 0;
      ast_node *decl = parse_declaration(p, DECLARATION_FILE);
      if (decl) {
        ast_node **temp = realloc(prog_node->program.declarations,
                                  sizeof(ast_node *) * (prog_node->program.count + 1));
        if (temp) {
          prog_node->program.declarations = temp;
          prog_node->program.declarations[prog_node->program.count++] = decl;
        }
      } else {
        parser_recover_top_level(p);
      }
    } else {
      if (!stray_reported) {
        parser_error(p, "expected a declaration at file scope");
        stray_reported = 1;
      }
      parser_advance(p);
    }
  }
  if (prog_node->program.count == 0 && p->had_error == 0)
    parser_error(p, "a translation unit needs at least one declaration");
  return prog_node;
}
