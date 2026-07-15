#ifndef _PARSER_H_
#define _PARSER_H_
#include "lexer.h"
#include "ast.h"

typedef enum symbol_kind {
    SYMBOL_TYPEDEF,
    SYMBOL_STRUCT_TAG,
    SYMBOL_ENUM_TAG
} symbol_kind;

typedef struct symbol {
    char *name;
    symbol_kind kind;
    struct symbol *next;
} symbol;

typedef struct scope {
    symbol *symbols;
    struct scope *parent;
} scope;

typedef struct parser {
    lexer *lex;
    token current_token;
    token next_token;
    scope *current_scope;
} parser;

void parser_init(parser *p, lexer *lex);
void parser_destroy(parser *p);
void parser_advance(parser *p);

void parser_enter_scope(parser *p);
void parser_leave_scope(parser *p);
void parser_define_symbol(parser *p, const char *name, symbol_kind kind);
symbol* parser_lookup_symbol(parser *p, const char *name);

ast_node* parse_primary(parser *p);
ast_node* parse_postfix(parser *p);
ast_node* parse_unary(parser *p);
ast_node* parse_multiplicative(parser *p);
ast_node* parse_additive(parser *p);
ast_node* parse_shift(parser *p);
ast_node* parse_relational(parser *p);
ast_node* parse_equality(parser *p);
ast_node* parse_bitwise_and(parser *p);
ast_node* parse_bitwise_xor(parser *p);
ast_node* parse_bitwise_or(parser *p);
ast_node* parse_logical_and(parser *p);
ast_node* parse_logical_or(parser *p);
ast_node* parse_ternary(parser *p);
ast_node* parse_assignment(parser *p);
ast_node* parse_expression(parser *p);
ast_node* parse_statement(parser *p);
ast_node* parse_block(parser *p);
ast_node* parse_program(parser *p);


#endif //_PARSER_H_
