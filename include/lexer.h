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
} lexer;

void lexer_init(lexer *lex, const char *source);
token lexer_next_token(lexer *lex);

#endif //_LEXER_H_
