#include "token_buf.h"
#include "lexer.h"
#include <stdlib.h>
#include <string.h>

void token_buf_init(token_buf *tb) {
  if (tb == NULL)
    return;
  tb->tokens = NULL;
  tb->count = 0;
  tb->capacity = 0;
  tb->pos = 0;
}

void token_buf_push(token_buf *tb, token t) {
  if (tb == NULL)
    return;
  if (tb->count == tb->capacity) {
    int new_capacity = (tb->capacity) ? tb->capacity * 2 : 64;
    token *tmp = realloc(tb->tokens, new_capacity * sizeof(token));
    if (tmp == NULL) {
      free(t.value);
      return;
    }
    tb->tokens = tmp;
    tb->capacity = new_capacity;
  }
  tb->tokens[tb->count++] = t;
}

token token_buf_next(token_buf *tb) {
  if (tb == NULL || tb->pos >= tb->count) {
    token t = {TOKEN_EOF, NULL, 1, 1, 0};
    if (tb != NULL && tb->count > 0) {
      t.line = tb->tokens[tb->count - 1].line;
      t.column = tb->tokens[tb->count - 1].column;
    }
    return t;
  }
  return tb->tokens[tb->pos++];
}

void token_buf_free(token_buf *tb) {
  if (tb == NULL)
    return;
  for (int i = 0; i < tb->count; i++) {
    free(tb->tokens[i].value);
  }
  free(tb->tokens);
  tb->tokens = NULL;
  tb->count = 0;
  tb->capacity = 0;
  tb->pos = 0;
}