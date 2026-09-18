#ifndef _PARSER_H_
#define _PARSER_H_
#include "ast.h"
#include "lexer.h"
#include "token_buf.h"

typedef enum parser_symbol_kind {
  PARSER_SYMBOL_TYPEDEF,
  PARSER_SYMBOL_ORDINARY
} parser_symbol_kind;

typedef struct parser_symbol {
  char *name;
  parser_symbol_kind kind;
  struct parser_symbol *next;
} parser_symbol;

typedef struct parser_scope {
  parser_symbol *symbols;
  struct parser_scope *parent;
} parser_scope;

typedef struct parser {
  token_buf tokens;
  token current_token;
  token next_token;
  parser_scope *current_scope;
  int had_error;
} parser;

void parser_init(parser *p, lexer *lex);
void parser_init_from_buf(parser *p, token_buf *tb);
void parser_destroy(parser *p);
void parser_advance(parser *p);

void parser_enter_scope(parser *p);
void parser_leave_scope(parser *p);
void parser_define_symbol(parser *p, const char *name, parser_symbol_kind kind);
parser_symbol *parser_lookup_symbol(parser *p, const char *name);
void parser_error(parser *p, const char *message);

ast_node *parse_primary(parser *p);
ast_node *parse_postfix(parser *p);
ast_node *parse_unary(parser *p);
ast_node *parse_multiplicative(parser *p);
ast_node *parse_additive(parser *p);
ast_node *parse_shift(parser *p);
ast_node *parse_relational(parser *p);
ast_node *parse_equality(parser *p);
ast_node *parse_bitwise_and(parser *p);
ast_node *parse_bitwise_xor(parser *p);
ast_node *parse_bitwise_or(parser *p);
ast_node *parse_logical_and(parser *p);
ast_node *parse_logical_or(parser *p);
ast_node *parse_ternary(parser *p);
ast_node *parse_assignment(parser *p);
ast_node *parse_expression(parser *p);
ast_node *parse_statement(parser *p);
ast_node *parse_block(parser *p);
ast_node *parse_program(parser *p);

#endif //_PARSER_H_
