#ifndef _LEXER_H_
#define _LEXER_H_
#include "token.h"
typedef struct lexer {
  const char *source;
  char current_char;
  int position;
  int line;
  int column;
  int at_line_start;
  int error_count;
  const char *file;
  int line_offset;
} lexer;

void lexer_init(lexer *lex, const char *source);
token lexer_next_token(lexer *lex);
int token_is_keyword(token_type type);
const char *decode_literal(const char *spelling, int wide, char *out, int *units);
long long char_constant_value(const char *bytes, int units, int wide);
typedef enum number_kind { NUMBER_INVALID, NUMBER_INTEGER, NUMBER_FLOATING } number_kind;
number_kind classify_number(const char *spelling, const char **error);
int integer_suffix(const char *suffix, int *is_unsigned, int *long_count);

#endif //_LEXER_H_
