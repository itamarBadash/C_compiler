#ifndef _TOKEN_BUF_H_
#define _TOKEN_BUF_H_
#include "token.h"

typedef struct token_buf {
  token *tokens;
  int count;
  int capacity;
  int pos;
} token_buf;

void token_buf_init(token_buf *tb);
void token_buf_push(token_buf *tb, token t);
token token_buf_next(token_buf *tb);
void token_buf_free(token_buf *tb);
#endif