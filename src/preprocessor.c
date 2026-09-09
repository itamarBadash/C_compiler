#include "preprocessor.h"
#include "lexer.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PP_MAX_INCLUDE_DEPTH 64

typedef enum dynamic_macro { DYNAMIC_NONE = 0, DYNAMIC_FILE, DYNAMIC_LINE } dynamic_macro;

typedef struct macro {
  char *name;
  char **params;
  int param_count;
  int is_function_like;
  int is_variadic;
  dynamic_macro dynamic;
  token *body;
  int body_count;
  struct macro *next;
} macro;

typedef struct cond {
  int parent_emitting;
  int taken;
  int emitting;
  int seen_else;
  int line;
  int column;
  struct cond *next;
} cond;

typedef struct arg {
  token *raw;
  int raw_count;
  token *expanded;
  int expanded_count;
} arg;

typedef struct expansion {
  token *tokens;
  int count;
  int pos;
  int is_barrier;
  char *macro_name;
  struct expansion *next;
} expansion;

typedef struct source_file {
  lexer lex;
  char *spliced;
  char *filename;
  char *presumed_name;
  int line_offset;
  token pending;
  int has_pending;
  struct source_file *next;
} source_file;

typedef struct pp {
  source_file *sources;
  macro *macros;
  expansion *stack;
  int error_count;
  int invocation_line;
  cond *conds;
  const char **include_dirs;
  int include_dir_count;
} pp;

typedef struct eval {
  pp *p;
  token *toks;
  int count;
  int pos;
  int line;
  int column;
} eval;

typedef struct ev_value {
  long long value;
  int is_unsigned;
} ev_value;

static token clone_token(token t);
static void free_token(token t);
static void free_tokens(token *toks, int n);
static void append_token(token **list, int *count, token t);

static int pp_current_line(pp *p, int physical_line);
static const char *pp_current_file(pp *p);
static void pp_error(pp *p, int line, int column, const char *message);

static void free_params(char **params, int count);
static void free_expansion(expansion *exp);
static void free_macro(macro *m);

static char *pp_read_file(const char *path);
static int file_exists(const char *path);
static char *join_path(const char *dir, const char *name);
static int source_push(pp *p, const char *text, const char *filename);
static void source_pop(pp *p);

static int pp_init(pp *p, const char *source, const char *filename);
static void pp_destroy(pp *p);

static void pop_exhausted(pp *p);
static token pp_next(pp *p);
static void pp_unget(pp *p, token t);
static void push_token_list(pp *p, const char *disable_name, token *toks, int count);

static macro *macro_find(pp *p, const char *name);
static int macro_disabled(pp *p, const char *name);
static void macro_remove(pp *p, const char *name);
static int param_index(macro *m, const char *name);
static int is_reserved_macro_name(const char *name);
static void predefine(pp *p, const char *name, const char *body_text);
static void predefine_dynamic(pp *p, const char *name, dynamic_macro kind);
static void install_predefined_macros(pp *p);

static int pp_emitting(pp *p);
static void cond_push(pp *p, int condition_true, int line, int column);
static void cond_pop(pp *p);

static token_type ev_peek(eval *e);
static void ev_error(eval *e, const char *message);
static ev_value ev_make(long long value, int is_unsigned);
static int ev_truth(ev_value v);
static void ev_convert(ev_value *a, ev_value *b);
static int ev_number(eval *e, const token *t, ev_value *out);
static int ev_char_value(eval *e, const token *t, int wide, long long *out);
static ev_value ev_primary(eval *e, int live);
static ev_value ev_unary(eval *e, int live);
static ev_value ev_multiplicative(eval *e, int live);
static ev_value ev_additive(eval *e, int live);
static ev_value ev_shift(eval *e, int live);
static ev_value ev_relational(eval *e, int live);
static ev_value ev_equality(eval *e, int live);
static ev_value ev_bit_and(eval *e, int live);
static ev_value ev_bit_xor(eval *e, int live);
static ev_value ev_bit_or(eval *e, int live);
static ev_value ev_logical_and(eval *e, int live);
static ev_value ev_logical_or(eval *e, int live);
static ev_value ev_conditional(eval *e, int live);

static void resolve_defined(pp *p, token *toks, int count, token **out, int *out_count);
static int eval_condition(pp *p, int line, int column);

static char *token_spelling(token t);
static token stringize(token *toks, int n);
static int paste_tokens(token lhs, token rhs, token *out);

static int collect_args(pp *p, macro *m, arg **out, int *out_count);
static void free_args(arg *args, int n);
static void expand_token_list(pp *p, token *in, int count, token **out, int *out_count);
static void expand_arg(pp *p, arg *a);
static void substitute(pp *p, macro *m, arg *args, int arg_count);
static token make_dynamic_token(pp *p, dynamic_macro kind, token at);
static int try_pragma_operator(pp *p, token t);
static int try_expand(pp *p, token t);

static void skip_directive_line(pp *p);
static void abort_define(pp *p, token t, char **params, int param_count, token name);
static void do_define(pp *p);
static void do_undef(pp *p);
static void do_ifdef(pp *p, int line, int column, int negate);
static void do_if(pp *p, int line, int column);
static void do_elif(pp *p, int line, int column);
static void do_else(pp *p, int line, int column);
static void do_endif(pp *p, int line, int column);
static void do_line(pp *p, int line, int column);
static void do_error(pp *p, int line, int column);
static char *read_header_name(pp *p, int *is_system, int line, int column);
static char *resolve_include(pp *p, const char *name, int is_system);
static void do_include(pp *p, int line, int column);
static void handle_directive(pp *p);

static token clone_token(token t) {
  token new_t = t;
  if (t.value) {
    new_t.value = strdup(t.value);
  }
  return new_t;
}

static void free_token(token t) {
  if (t.value) {
    free(t.value);
  }
}

static void free_tokens(token *toks, int n) {
  if (toks == NULL || n == 0)
    return;
  for (int i = 0; i < n; i++) {
    free_token(toks[i]);
  }
  free(toks);
}

static void append_token(token **list, int *count, token t) {
  if ((*count & (*count - 1)) == 0) {
    int capacity = (*count == 0) ? 1 : *count * 2;
    token *tmp = realloc(*list, sizeof(token) * capacity);
    if (tmp == NULL) {
      free_token(t);
      return;
    }
    *list = tmp;
  }
  (*list)[*count] = t;
  (*count)++;
}

static int pp_current_line(pp *p, int physical_line) {
  if (p == NULL || p->sources == NULL) {
    return physical_line;
  }
  return physical_line + p->sources->line_offset;
}

static const char *pp_current_file(pp *p) {
  if (p == NULL || p->sources == NULL) {
    return "<source>";
  }
  if (p->sources->presumed_name != NULL) {
    return p->sources->presumed_name;
  }
  if (p->sources->filename != NULL) {
    return p->sources->filename;
  }
  return "<source>";
}

static void pp_error(pp *p, int line, int column, const char *message) {
  if (p == NULL)
    return;
  p->error_count++;
  fprintf(stderr, "%s:%d:%d: error: %s\n", pp_current_file(p), pp_current_line(p, line), column,
          message);
}

static void free_params(char **params, int count) {
  for (int i = 0; i < count; i++) {
    free(params[i]);
  }
  free(params);
}

static void free_expansion(expansion *exp) {
  if (exp == NULL)
    return;
  for (int i = 0; i < exp->count; i++) {
    free_token(exp->tokens[i]);
  }
  free(exp->tokens);
  free(exp->macro_name);
  free(exp);
}

static void free_macro(macro *m) {
  if (m == NULL)
    return;
  free(m->name);
  free_params(m->params, m->param_count);
  for (int i = 0; i < m->body_count; i++) {
    free_token(m->body[i]);
  }
  free(m->body);
  free(m);
}

static char *pp_read_file(const char *path) {
  if (path == NULL)
    return NULL;
  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long size = ftell(f);
  if (size < 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  char *text = malloc((size_t)size + 1);
  if (text == NULL) {
    fclose(f);
    return NULL;
  }
  size_t got = fread(text, 1, (size_t)size, f);
  text[got] = '\0';
  fclose(f);
  return text;
}

static int file_exists(const char *path) {
  if (path == NULL)
    return 0;
  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return 0;
  fclose(f);
  return 1;
}

static char *join_path(const char *dir, const char *name) {
  size_t dir_len = strlen(dir);
  size_t name_len = strlen(name);
  char *out = malloc(dir_len + name_len + 2);
  if (out == NULL)
    return NULL;
  memcpy(out, dir, dir_len);
  size_t at = dir_len;
  if (dir_len > 0 && dir[dir_len - 1] != '/' && dir[dir_len - 1] != '\\') {
    out[at++] = '/';
  }
  memcpy(out + at, name, name_len + 1);
  return out;
}

static int source_push(pp *p, const char *text, const char *filename) {
  if (p == NULL || text == NULL)
    return 0;
  char *spliced = pp_splice_lines(text);
  if (spliced == NULL)
    return 0;
  source_file *sf = calloc(1, sizeof(source_file));
  if (sf == NULL) {
    free(spliced);
    return 0;
  }
  sf->spliced = spliced;
  sf->filename = filename ? strdup(filename) : NULL;
  sf->has_pending = 0;
  sf->next = p->sources;
  lexer_init(&sf->lex, sf->spliced);
  p->sources = sf;
  return 1;
}

static void source_pop(pp *p) {
  if (p == NULL || p->sources == NULL)
    return;
  source_file *sf = p->sources;
  p->sources = sf->next;
  if (sf->has_pending) {
    free_token(sf->pending);
  }
  free(sf->spliced);
  free(sf->filename);
  free(sf->presumed_name);
  free(sf);
}

static int pp_init(pp *p, const char *source, const char *filename) {
  *p = (pp){0};
  if (!source_push(p, source, filename)) {
    return 0;
  }
  install_predefined_macros(p);
  return 1;
}

static void pp_destroy(pp *p) {
  if (p == NULL)
    return;
  while (p->sources != NULL) {
    source_pop(p);
  }
  while (p->stack != NULL) {
    expansion *exp = p->stack;
    p->stack = exp->next;
    free_expansion(exp);
  }
  while (p->conds != NULL) {
    cond_pop(p);
  }
  while (p->macros != NULL) {
    macro *m = p->macros;
    p->macros = m->next;
    free_macro(m);
  }
}

static void pop_exhausted(pp *p) {
  if (p == NULL)
    return;
  while (p->stack != NULL && p->stack->pos >= p->stack->count && !p->stack->is_barrier) {
    expansion *exp = p->stack;
    p->stack = exp->next;
    free_expansion(exp);
  }
}

static token pp_next(pp *p) {
  if (p == NULL || p->sources == NULL) {
    token t = {TOKEN_EOF, NULL, 1, 1, 0};
    return t;
  }
  if (p->sources->has_pending) {
    p->sources->has_pending = 0;
    return p->sources->pending;
  }
  pop_exhausted(p);
  if (p->stack != NULL) {
    if (p->stack->pos >= p->stack->count) {
      token t = {TOKEN_EOF, NULL, 1, 1, 0};
      return t;
    }
    return clone_token(p->stack->tokens[p->stack->pos++]);
  }
  return lexer_next_token(&p->sources->lex);
}

static void pp_unget(pp *p, token t) {
  if (p == NULL || p->sources == NULL)
    return;
  assert(!p->sources->has_pending);
  p->sources->pending = t;
  p->sources->has_pending = 1;
}

static void push_token_list(pp *p, const char *disable_name, token *toks, int count) {
  expansion *exp = malloc(sizeof(expansion));
  if (exp == NULL) {
    free_tokens(toks, count);
    return;
  }
  exp->tokens = toks;
  exp->count = count;
  exp->pos = 0;
  exp->is_barrier = 0;
  exp->macro_name = strdup(disable_name);
  exp->next = p->stack;
  p->stack = exp;
}

static macro *macro_find(pp *p, const char *name) {
  if (p == NULL || name == NULL)
    return NULL;
  macro *m = p->macros;
  while (m != NULL) {
    if (strcmp(m->name, name) == 0) {
      return m;
    }
    m = m->next;
  }
  return NULL;
}

static int macro_disabled(pp *p, const char *name) {
  if (p == NULL || name == NULL)
    return 0;
  expansion *exp = p->stack;
  while (exp != NULL) {
    if (strcmp(exp->macro_name, name) == 0) {
      return 1;
    }
    exp = exp->next;
  }
  return 0;
}

static void macro_remove(pp *p, const char *name) {
  if (p == NULL || name == NULL)
    return;
  macro **prev = &p->macros;
  macro *m = p->macros;
  while (m != NULL) {
    if (strcmp(m->name, name) == 0) {
      *prev = m->next;
      free_macro(m);
      return;
    }
    prev = &m->next;
    m = m->next;
  }
}

static int param_index(macro *m, const char *name) {
  if (m == NULL || name == NULL || !m->is_function_like)
    return -1;
  for (int i = 0; i < m->param_count; i++) {
    if (strcmp(m->params[i], name) == 0) {
      return i;
    }
  }
  return -1;
}

static int is_reserved_macro_name(const char *name) {
  static const char *reserved[] = {"defined",          "__FILE__",        "__LINE__",
                                   "__DATE__",         "__TIME__",        "__STDC__",
                                   "__STDC_VERSION__", "__STDC_HOSTED__", "__VA_ARGS__"};
  if (name == NULL) {
    return 0;
  }
  for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
    if (strcmp(name, reserved[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

static void predefine(pp *p, const char *name, const char *body_text) {
  lexer lex;
  lexer_init(&lex, body_text);

  token *body = NULL;
  int body_count = 0;
  for (;;) {
    token t = lexer_next_token(&lex);
    if (t.type == TOKEN_EOF) {
      free_token(t);
      break;
    }
    append_token(&body, &body_count, t);
  }

  macro *m = calloc(1, sizeof(macro));
  if (m == NULL) {
    free_tokens(body, body_count);
    return;
  }
  macro_remove(p, name);
  m->name = strdup(name);
  m->body = body;
  m->body_count = body_count;
  m->next = p->macros;
  p->macros = m;
}

static void predefine_dynamic(pp *p, const char *name, dynamic_macro kind) {
  predefine(p, name, "0");
  macro *m = macro_find(p, name);
  if (m != NULL) {
    m->dynamic = kind;
  }
}

static void install_predefined_macros(pp *p) {
  predefine(p, "__STDC__", "1");
  predefine(p, "__STDC_VERSION__", "199901L");
  predefine(p, "__STDC_HOSTED__", "1");
  predefine_dynamic(p, "__FILE__", DYNAMIC_FILE);
  predefine_dynamic(p, "__LINE__", DYNAMIC_LINE);

  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  if (lt == NULL) {
    return;
  }

  char formatted[32];
  char quoted[64];

  if (strftime(formatted, sizeof(formatted), "%b %d %Y", lt) > 0) {
    if (formatted[4] == '0') {
      formatted[4] = ' ';
    }
    snprintf(quoted, sizeof(quoted), "\"%s\"", formatted);
    predefine(p, "__DATE__", quoted);
  }

  if (strftime(formatted, sizeof(formatted), "%H:%M:%S", lt) > 0) {
    snprintf(quoted, sizeof(quoted), "\"%s\"", formatted);
    predefine(p, "__TIME__", quoted);
  }
}

static int pp_emitting(pp *p) {
  if (p == NULL || p->conds == NULL)
    return 1;
  return p->conds->emitting;
}

static void cond_push(pp *p, int condition_true, int line, int column) {
  if (p == NULL)
    return;
  cond *c = malloc(sizeof(cond));
  if (c == NULL)
    return;
  c->parent_emitting = pp_emitting(p);
  c->taken = c->parent_emitting && condition_true;
  c->emitting = c->taken;
  c->seen_else = 0;
  c->line = line;
  c->column = column;
  c->next = p->conds;
  p->conds = c;
}

static void cond_pop(pp *p) {
  if (p == NULL || p->conds == NULL)
    return;
  cond *c = p->conds;
  p->conds = c->next;
  free(c);
}

static token_type ev_peek(eval *e) {
  return (e->pos < e->count) ? e->toks[e->pos].type : TOKEN_EOF;
}

static void ev_error(eval *e, const char *message) {
  if (e->pos < e->count) {
    pp_error(e->p, e->toks[e->pos].line, e->toks[e->pos].column, message);
  } else {
    pp_error(e->p, e->line, e->column, message);
  }
}

static ev_value ev_make(long long value, int is_unsigned) {
  ev_value out;
  out.value = value;
  out.is_unsigned = is_unsigned;
  return out;
}

static int ev_truth(ev_value v) {
  return v.value != 0;
}

static void ev_convert(ev_value *a, ev_value *b) {
  if (a->is_unsigned || b->is_unsigned) {
    a->is_unsigned = 1;
    b->is_unsigned = 1;
  }
}

static int ev_number(eval *e, const token *t, ev_value *out) {
  const char *s = t->value ? t->value : "";
  int hex = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'));
  if (strchr(s, '.') != NULL || (hex && (strchr(s, 'p') != NULL || strchr(s, 'P') != NULL)) ||
      (!hex && (strchr(s, 'e') != NULL || strchr(s, 'E') != NULL))) {
    ev_error(e, "floating point numbers are not supported in preprocessor expressions");
    return 0;
  }

  errno = 0;
  char *end;
  unsigned long long raw = strtoull(s, &end, 0);
  if (errno == ERANGE) {
    ev_error(e, "integer constant is too large for #if");
    return 0;
  }

  int has_unsigned_suffix = 0;
  while (*end != '\0') {
    if (*end == 'u' || *end == 'U') {
      has_unsigned_suffix = 1;
      end++;
    } else if (*end == 'l' || *end == 'L') {
      end++;
    } else {
      ev_error(e, "invalid integer constant in #if");
      return 0;
    }
  }

  out->value = (long long)raw;
  out->is_unsigned = has_unsigned_suffix || raw > (unsigned long long)LLONG_MAX;
  return 1;
}

static int ev_char_value(eval *e, const token *t, int wide, long long *out) {
  const char *s = t->value ? t->value : "";
  if (s[0] == '\0') {
    ev_error(e, "empty character constant in #if");
    return 0;
  }

  if (s[0] != '\\') {
    *out = wide ? (long long)(unsigned char)s[0] : (long long)(signed char)s[0];
    return 1;
  }

  long long simple = -1;
  switch (s[1]) {
  case 'n':
    simple = '\n';
    break;
  case 't':
    simple = '\t';
    break;
  case 'r':
    simple = '\r';
    break;
  case 'a':
    simple = '\a';
    break;
  case 'b':
    simple = '\b';
    break;
  case 'f':
    simple = '\f';
    break;
  case 'v':
    simple = '\v';
    break;
  case '\\':
    simple = '\\';
    break;
  case '\'':
    simple = '\'';
    break;
  case '"':
    simple = '"';
    break;
  case '?':
    simple = '?';
    break;
  default:
    break;
  }
  if (simple >= 0) {
    *out = simple;
    return 1;
  }

  if (s[1] >= '0' && s[1] <= '7') {
    int value = 0;
    int digits = 0;
    for (int i = 1; digits < 3 && s[i] >= '0' && s[i] <= '7'; i++, digits++) {
      value = value * 8 + (s[i] - '0');
    }
    *out = wide ? (long long)value : (long long)(signed char)value;
    return 1;
  }

  if (s[1] == 'x') {
    int value = 0;
    int digits = 0;
    for (int i = 2; isxdigit((unsigned char)s[i]); i++, digits++) {
      int d = isdigit((unsigned char)s[i]) ? s[i] - '0' : tolower((unsigned char)s[i]) - 'a' + 10;
      value = value * 16 + d;
    }
    if (digits == 0) {
      ev_error(e, "\\x used with no following hex digits in #if");
      return 0;
    }
    *out = wide ? (long long)value : (long long)(signed char)value;
    return 1;
  }

  ev_error(e, "unknown escape sequence in #if character constant");
  return 0;
}

static ev_value ev_primary(eval *e, int live) {
  switch (ev_peek(e)) {
  case TOKEN_NUMBER: {
    ev_value v = ev_make(0, 0);
    ev_number(e, &e->toks[e->pos], &v);
    e->pos++;
    return v;
  }
  case TOKEN_LPAREN: {
    e->pos++;
    ev_value v = ev_conditional(e, live);
    if (ev_peek(e) != TOKEN_RPAREN) {
      ev_error(e, "expected ) in #if expression");
      return v;
    }
    e->pos++;
    return v;
  }
  case TOKEN_EOF:
    ev_error(e, "unexpected end of #if expression");
    return ev_make(0, 0);
  case TOKEN_STRING:
  case TOKEN_WIDE_STRING:
    ev_error(e, "string literals are not permitted in #if");
    e->pos++;
    return ev_make(0, 0);
  case TOKEN_CHAR_LITERAL:
  case TOKEN_WIDE_CHAR: {
    long long value = 0;
    int wide = (ev_peek(e) == TOKEN_WIDE_CHAR);
    ev_char_value(e, &e->toks[e->pos], wide, &value);
    e->pos++;
    return ev_make(value, 0);
  }
  default: {
    const char *s = e->toks[e->pos].value ? e->toks[e->pos].value : "";
    if (isalpha((unsigned char)s[0]) || s[0] == '_') {
      e->pos++;
      return ev_make(0, 0);
    }
    ev_error(e, "unexpected token in #if expression");
    e->pos++;
    return ev_make(0, 0);
  }
  }
}

static ev_value ev_unary(eval *e, int live) {
  switch (ev_peek(e)) {
  case TOKEN_PLUS:
    e->pos++;
    return ev_unary(e, live);
  case TOKEN_MINUS: {
    e->pos++;
    ev_value v = ev_unary(e, live);
    v.value = (long long)(0ULL - (unsigned long long)v.value);
    return v;
  }
  case TOKEN_TILDE: {
    e->pos++;
    ev_value v = ev_unary(e, live);
    v.value = ~v.value;
    return v;
  }
  case TOKEN_NOT: {
    e->pos++;
    ev_value v = ev_unary(e, live);
    return ev_make(!ev_truth(v), 0);
  }
  default:
    return ev_primary(e, live);
  }
}

static ev_value ev_multiplicative(eval *e, int live) {
  ev_value v = ev_unary(e, live);
  for (;;) {
    token_type op = ev_peek(e);
    if (op != TOKEN_STAR && op != TOKEN_SLASH && op != TOKEN_PERCENT) {
      return v;
    }
    e->pos++;
    ev_value rhs = ev_unary(e, live);
    ev_convert(&v, &rhs);

    if (op == TOKEN_STAR) {
      v.value = (long long)((unsigned long long)v.value * (unsigned long long)rhs.value);
      continue;
    }
    if (!live) {
      v.value = 0;
      continue;
    }
    if (rhs.value == 0) {
      ev_error(e, op == TOKEN_SLASH ? "division by zero in #if expression"
                                    : "modulo by zero in #if expression");
      v.value = 0;
      continue;
    }
    if (v.is_unsigned) {
      unsigned long long a = (unsigned long long)v.value;
      unsigned long long b = (unsigned long long)rhs.value;
      v.value = (long long)(op == TOKEN_SLASH ? a / b : a % b);
      continue;
    }
    if (v.value == LLONG_MIN && rhs.value == -1) {
      ev_error(e, "integer overflow in #if expression");
      v.value = 0;
      continue;
    }
    v.value = (op == TOKEN_SLASH) ? v.value / rhs.value : v.value % rhs.value;
  }
}

static ev_value ev_additive(eval *e, int live) {
  ev_value v = ev_multiplicative(e, live);
  for (;;) {
    token_type op = ev_peek(e);
    if (op != TOKEN_PLUS && op != TOKEN_MINUS) {
      return v;
    }
    e->pos++;
    ev_value rhs = ev_multiplicative(e, live);
    ev_convert(&v, &rhs);
    unsigned long long a = (unsigned long long)v.value;
    unsigned long long b = (unsigned long long)rhs.value;
    v.value = (long long)(op == TOKEN_PLUS ? a + b : a - b);
  }
}

static ev_value ev_shift(eval *e, int live) {
  ev_value v = ev_additive(e, live);
  for (;;) {
    token_type op = ev_peek(e);
    if (op != TOKEN_LSHIFT && op != TOKEN_RSHIFT) {
      return v;
    }
    e->pos++;
    ev_value rhs = ev_additive(e, live);
    if (!live) {
      v.value = 0;
      continue;
    }
    int out_of_range = rhs.is_unsigned ? ((unsigned long long)rhs.value >= 64)
                                       : (rhs.value < 0 || rhs.value >= 64);
    if (out_of_range) {
      ev_error(e, "shift count out of range in #if");
      v.value = 0;
      continue;
    }
    int count = (int)rhs.value;
    if (op == TOKEN_LSHIFT) {
      v.value = (long long)((unsigned long long)v.value << count);
    } else if (v.is_unsigned) {
      v.value = (long long)((unsigned long long)v.value >> count);
    } else {
      v.value = v.value >> count;
    }
  }
}

static ev_value ev_relational(eval *e, int live) {
  ev_value v = ev_shift(e, live);
  for (;;) {
    token_type op = ev_peek(e);
    if (op != TOKEN_LT && op != TOKEN_GT && op != TOKEN_LTE && op != TOKEN_GTE) {
      return v;
    }
    e->pos++;
    ev_value rhs = ev_shift(e, live);
    ev_convert(&v, &rhs);

    int result;
    if (v.is_unsigned) {
      unsigned long long a = (unsigned long long)v.value;
      unsigned long long b = (unsigned long long)rhs.value;
      result = (op == TOKEN_LT)    ? (a < b)
               : (op == TOKEN_GT)  ? (a > b)
               : (op == TOKEN_LTE) ? (a <= b)
                                   : (a >= b);
    } else {
      long long a = v.value;
      long long b = rhs.value;
      result = (op == TOKEN_LT)    ? (a < b)
               : (op == TOKEN_GT)  ? (a > b)
               : (op == TOKEN_LTE) ? (a <= b)
                                   : (a >= b);
    }
    v = ev_make(result, 0);
  }
}

static ev_value ev_equality(eval *e, int live) {
  ev_value v = ev_relational(e, live);
  for (;;) {
    token_type op = ev_peek(e);
    if (op != TOKEN_EQ && op != TOKEN_NEQ) {
      return v;
    }
    e->pos++;
    ev_value rhs = ev_relational(e, live);
    int result = (op == TOKEN_EQ) ? (v.value == rhs.value) : (v.value != rhs.value);
    v = ev_make(result, 0);
  }
}

static ev_value ev_bit_and(eval *e, int live) {
  ev_value v = ev_equality(e, live);
  while (ev_peek(e) == TOKEN_AMPERSAND) {
    e->pos++;
    ev_value rhs = ev_equality(e, live);
    ev_convert(&v, &rhs);
    v.value = v.value & rhs.value;
  }
  return v;
}

static ev_value ev_bit_xor(eval *e, int live) {
  ev_value v = ev_bit_and(e, live);
  while (ev_peek(e) == TOKEN_CARET) {
    e->pos++;
    ev_value rhs = ev_bit_and(e, live);
    ev_convert(&v, &rhs);
    v.value = v.value ^ rhs.value;
  }
  return v;
}

static ev_value ev_bit_or(eval *e, int live) {
  ev_value v = ev_bit_xor(e, live);
  while (ev_peek(e) == TOKEN_PIPE) {
    e->pos++;
    ev_value rhs = ev_bit_xor(e, live);
    ev_convert(&v, &rhs);
    v.value = v.value | rhs.value;
  }
  return v;
}

static ev_value ev_logical_and(eval *e, int live) {
  ev_value v = ev_bit_or(e, live);
  while (ev_peek(e) == TOKEN_AND) {
    e->pos++;
    ev_value rhs = ev_bit_or(e, live && ev_truth(v));
    v = ev_make(ev_truth(v) && ev_truth(rhs), 0);
  }
  return v;
}

static ev_value ev_logical_or(eval *e, int live) {
  ev_value v = ev_logical_and(e, live);
  while (ev_peek(e) == TOKEN_OR) {
    e->pos++;
    ev_value rhs = ev_logical_and(e, live && !ev_truth(v));
    v = ev_make(ev_truth(v) || ev_truth(rhs), 0);
  }
  return v;
}

static ev_value ev_conditional(eval *e, int live) {
  ev_value cond_value = ev_logical_or(e, live);
  if (ev_peek(e) != TOKEN_QUESTION) {
    return cond_value;
  }
  e->pos++;
  ev_value a = ev_conditional(e, live && ev_truth(cond_value));
  if (ev_peek(e) != TOKEN_COLON) {
    ev_error(e, "expected : in #if expression");
    return ev_make(0, 0);
  }
  e->pos++;
  ev_value b = ev_conditional(e, live && !ev_truth(cond_value));
  ev_convert(&a, &b);
  return ev_truth(cond_value) ? a : b;
}

static void resolve_defined(pp *p, token *toks, int count, token **out, int *out_count) {
  *out = NULL;
  *out_count = 0;

  int i = 0;
  while (i < count) {
    if (toks[i].type != TOKEN_IDENTIFIER || toks[i].value == NULL ||
        strcmp(toks[i].value, "defined") != 0) {
      append_token(out, out_count, clone_token(toks[i]));
      i++;
      continue;
    }

    int name_index = -1;
    int consumed = 0;

    if (i + 1 < count && toks[i + 1].type == TOKEN_LPAREN) {
      if (i + 3 < count && toks[i + 2].type == TOKEN_IDENTIFIER &&
          toks[i + 3].type == TOKEN_RPAREN) {
        name_index = i + 2;
        consumed = 4;
      }
    } else if (i + 1 < count && toks[i + 1].type == TOKEN_IDENTIFIER) {
      name_index = i + 1;
      consumed = 2;
    }

    if (name_index < 0) {
      pp_error(p, toks[i].line, toks[i].column, "operator defined requires an identifier");
      i++;
      continue;
    }

    token n;
    n.type = TOKEN_NUMBER;
    n.value = strdup(macro_find(p, toks[name_index].value) != NULL ? "1" : "0");
    n.line = toks[i].line;
    n.column = toks[i].column;
    n.at_line_start = 0;
    append_token(out, out_count, n);
    i += consumed;
  }
}

static int eval_condition(pp *p, int line, int column) {
  token *raw = NULL;
  int raw_count = 0;
  token terminator;

  for (;;) {
    token t = pp_next(p);
    if (t.type == TOKEN_EOF || t.at_line_start) {
      terminator = t;
      break;
    }
    append_token(&raw, &raw_count, t);
  }

  if (raw_count == 0) {
    pp_error(p, line, column, "#if with no expression");
    pp_unget(p, terminator);
    return 0;
  }

  token *resolved = NULL;
  int resolved_count = 0;
  resolve_defined(p, raw, raw_count, &resolved, &resolved_count);
  free_tokens(raw, raw_count);

  p->invocation_line = line;
  token *expanded = NULL;
  int expanded_count = 0;
  expand_token_list(p, resolved, resolved_count, &expanded, &expanded_count);

  eval e;
  e.p = p;
  e.toks = expanded;
  e.count = expanded_count;
  e.pos = 0;
  e.line = line;
  e.column = column;

  ev_value value = ev_conditional(&e, 1);
  if (e.pos < e.count) {
    ev_error(&e, "extra tokens after #if expression");
  }

  free_tokens(expanded, expanded_count);
  pp_unget(p, terminator);
  return ev_truth(value);
}

static char *token_spelling(token t) {
  const char *v = t.value ? t.value : "";
  size_t n = strlen(v);

  if (t.type == TOKEN_STRING || t.type == TOKEN_CHAR_LITERAL || t.type == TOKEN_WIDE_STRING ||
      t.type == TOKEN_WIDE_CHAR) {
    int wide = (t.type == TOKEN_WIDE_STRING || t.type == TOKEN_WIDE_CHAR);
    char q = (t.type == TOKEN_STRING || t.type == TOKEN_WIDE_STRING) ? '"' : '\'';
    char *out = malloc(n + 4);
    if (out == NULL)
      return NULL;
    size_t at = 0;
    if (wide)
      out[at++] = 'L';
    out[at++] = q;
    memcpy(out + at, v, n);
    at += n;
    out[at++] = q;
    out[at] = '\0';
    return out;
  }

  char *out = malloc(n + 1);
  if (out == NULL)
    return NULL;
  memcpy(out, v, n + 1);
  return out;
}

static token stringize(token *toks, int n) {
  token out;
  out.type = TOKEN_STRING;
  out.value = NULL;
  out.line = (n > 0) ? toks[0].line : 0;
  out.column = (n > 0) ? toks[0].column : 0;
  out.at_line_start = 0;

  size_t cap = 32;
  size_t len = 0;
  char *text = malloc(cap);
  if (text == NULL)
    return out;
  text[0] = '\0';

  int prev_line = 0;
  int prev_end_col = 0;

  for (int i = 0; i < n; i++) {
    char *sp = token_spelling(toks[i]);
    if (sp == NULL)
      continue;

    int gap = (i > 0) && (toks[i].line != prev_line || toks[i].column != prev_end_col);

    size_t need = len + 1 + strlen(sp) * 2 + 1;
    if (need > cap) {
      while (cap < need)
        cap *= 2;
      char *tmp = realloc(text, cap);
      if (tmp == NULL) {
        free(sp);
        break;
      }
      text = tmp;
    }

    if (gap)
      text[len++] = ' ';
    for (char *c = sp; *c != '\0'; c++) {
      if (*c == '\\' || *c == '"')
        text[len++] = '\\';
      text[len++] = *c;
    }
    text[len] = '\0';

    prev_line = toks[i].line;
    prev_end_col = toks[i].column + (int)strlen(sp);
    free(sp);
  }

  out.value = text;
  return out;
}

static int paste_tokens(token lhs, token rhs, token *out) {
  if (lhs.type == TOKEN_EOF || rhs.type == TOKEN_EOF) {
    return 0;
  }
  char *a = token_spelling(lhs);
  char *b = token_spelling(rhs);
  if (a == NULL || b == NULL) {
    free(a);
    free(b);
    return 0;
  }

  char *combined = malloc(strlen(a) + strlen(b) + 1);
  if (!combined) {
    free(a);
    free(b);
    return 0;
  }

  strcpy(combined, a);
  strcat(combined, b);

  lexer lex;
  lexer_init(&lex, combined);
  token first = lexer_next_token(&lex);
  token second = lexer_next_token(&lex);

  if (first.type != TOKEN_EOF && second.type == TOKEN_EOF) {
    first.line = lhs.line;
    first.column = lhs.column;
    first.at_line_start = lhs.at_line_start;
    *out = first;
    free_token(second);
    free(a);
    free(b);
    free(combined);
    return 1;
  } else {
    free_token(first);
    free_token(second);
    free(a);
    free(b);
    free(combined);
    return 0;
  }
}

static int collect_args(pp *p, macro *m, arg **out, int *out_count) {
  arg *args = malloc(sizeof(arg));
  if (args == NULL)
    return 0;
  args[0].raw = NULL;
  args[0].raw_count = 0;
  args[0].expanded = NULL;
  args[0].expanded_count = 0;

  int count = 1;
  int depth = 1;

  while (1) {
    token t = pp_next(p);

    if (t.type == TOKEN_EOF) {
      free_token(t);
      free_args(args, count);
      return 0;
    }

    if (t.type == TOKEN_LPAREN) {
      depth++;
      append_token(&args[count - 1].raw, &args[count - 1].raw_count, t);
      continue;
    }

    if (t.type == TOKEN_RPAREN) {
      depth--;
      if (depth == 0) {
        free_token(t);
        break;
      }
      append_token(&args[count - 1].raw, &args[count - 1].raw_count, t);
      continue;
    }

    if (t.type == TOKEN_COMMA && depth == 1 && !(m->is_variadic && count >= m->param_count)) {
      free_token(t);
      arg *tmp = realloc(args, sizeof(arg) * (count + 1));
      if (tmp == NULL) {
        free_args(args, count);
        return 0;
      }
      args = tmp;
      args[count].raw = NULL;
      args[count].raw_count = 0;
      args[count].expanded = NULL;
      args[count].expanded_count = 0;
      count++;
      continue;
    }

    append_token(&args[count - 1].raw, &args[count - 1].raw_count, t);
  }

  if (m->param_count == 0 && count == 1 && args[0].raw_count == 0) {
    free_args(args, count);
    args = NULL;
    count = 0;
  }

  if (m->is_variadic && count == m->param_count - 1) {
    arg *tmp = realloc(args, sizeof(arg) * (count + 1));
    if (tmp == NULL) {
      free_args(args, count);
      return 0;
    }
    args = tmp;
    args[count].raw = NULL;
    args[count].raw_count = 0;
    args[count].expanded = NULL;
    args[count].expanded_count = 0;
    count++;
  }

  if (count != m->param_count) {
    free_args(args, count);
    return 0;
  }

  for (int i = 0; i < count; i++) {
    expand_arg(p, &args[i]);
  }

  *out = args;
  *out_count = count;
  return 1;
}

static void free_args(arg *args, int n) {
  if (args == NULL)
    return;
  for (int i = 0; i < n; i++) {
    free_tokens(args[i].raw, args[i].raw_count);
    free_tokens(args[i].expanded, args[i].expanded_count);
  }
  free(args);
}

static void expand_token_list(pp *p, token *in, int count, token **out, int *out_count) {
  if (p == NULL || out == NULL || out_count == NULL)
    return;
  *out = NULL;
  *out_count = 0;
  if (in == NULL || count == 0)
    return;

  token *copy = malloc(sizeof(token) * count);
  if (copy == NULL)
    return;
  for (int i = 0; i < count; i++) {
    copy[i] = clone_token(in[i]);
  }
  push_token_list(p, "", copy, count);
  if (p->stack == NULL)
    return;

  expansion *mine = p->stack;
  mine->is_barrier = 1;

  while (1) {
    token t = pp_next(p);
    if (t.type == TOKEN_EOF) {
      free_token(t);
      break;
    }
    if (try_expand(p, t))
      continue;
    append_token(out, out_count, t);
  }

  if (p->sources->has_pending && p->sources->pending.type == TOKEN_EOF) {
    free_token(p->sources->pending);
    p->sources->has_pending = 0;
  }

  while (p->stack != NULL && p->stack != mine) {
    expansion *exp = p->stack;
    p->stack = exp->next;
    free_expansion(exp);
  }
  if (p->stack == mine) {
    p->stack = mine->next;
    free_expansion(mine);
  }
}

static void expand_arg(pp *p, arg *a) {
  if (p == NULL || a == NULL)
    return;
  expand_token_list(p, a->raw, a->raw_count, &a->expanded, &a->expanded_count);
}

static void substitute(pp *p, macro *m, arg *args, int arg_count) {
  token *out = NULL;
  int out_count = 0;

  for (int i = 0; i < m->body_count; i++) {
    token bt = m->body[i];

    if (m->is_function_like && bt.type == TOKEN_HASH && i + 1 < m->body_count &&
        m->body[i + 1].type == TOKEN_IDENTIFIER) {
      int pidx = param_index(m, m->body[i + 1].value);
      if (pidx >= 0 && pidx < arg_count) {
        append_token(&out, &out_count, stringize(args[pidx].raw, args[pidx].raw_count));
        i++;
        continue;
      }
    }

    int idx = -1;
    if (bt.type == TOKEN_IDENTIFIER) {
      idx = param_index(m, bt.value);
    }

    if (idx >= 0 && idx < arg_count) {

      int use_raw = 0;
      if (i > 0 && m->body[i - 1].type == TOKEN_HASH_HASH) {
        use_raw = 1;
      }
      if (i + 1 < m->body_count && m->body[i + 1].type == TOKEN_HASH_HASH) {
        use_raw = 1;
      }

      token *src = use_raw ? args[idx].raw : args[idx].expanded;
      int n = use_raw ? args[idx].raw_count : args[idx].expanded_count;
      for (int j = 0; j < n; j++) {
        append_token(&out, &out_count, clone_token(src[j]));
      }
    } else {
      append_token(&out, &out_count, clone_token(bt));
    }
  }
  token *final = NULL;
  int final_count = 0;

  for (int i = 0; i < out_count; i++) {
    if (out[i].type == TOKEN_HASH_HASH) {
      if (i + 1 < out_count && out[i + 1].type == TOKEN_HASH_HASH) {
        free_token(out[i]);
        continue;
      }

      int have_left = (final_count > 0);
      int have_right = (i + 1 < out_count);

      if (!have_right) {
        free_token(out[i]);
      } else if (!have_left) {
        free_token(out[i]);
        append_token(&final, &final_count, out[i + 1]);
        i++;
      } else {
        token left = final[final_count - 1];
        final_count--;
        token right = out[i + 1];
        token pasted;

        if (paste_tokens(left, right, &pasted)) {
          free_token(left);
          free_token(right);
          append_token(&final, &final_count, pasted);
        } else {
          append_token(&final, &final_count, left);
          append_token(&final, &final_count, right);
        }
        free_token(out[i]);
        i++;
      }
    } else {
      append_token(&final, &final_count, out[i]);
    }
  }

  free(out);
  push_token_list(p, m->name, final, final_count);
}

static token make_dynamic_token(pp *p, dynamic_macro kind, token at) {
  token out;
  out.value = NULL;
  out.line = at.line;
  out.column = at.column;
  out.at_line_start = at.at_line_start;

  if (kind == DYNAMIC_LINE) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", pp_current_line(p, p->invocation_line));
    out.type = TOKEN_NUMBER;
    out.value = strdup(buffer);
    return out;
  }

  const char *file = pp_current_file(p);
  out.type = TOKEN_STRING;

  size_t length = strlen(file);
  char *escaped = malloc(length * 2 + 1);
  if (escaped == NULL) {
    out.value = strdup("");
    return out;
  }
  size_t at_out = 0;
  for (size_t i = 0; i < length; i++) {
    if (file[i] == '\\' || file[i] == '"') {
      escaped[at_out++] = '\\';
    }
    escaped[at_out++] = file[i];
  }
  escaped[at_out] = '\0';
  out.value = escaped;
  return out;
}

static int try_pragma_operator(pp *p, token t) {
  if (t.value == NULL || strcmp(t.value, "_Pragma") != 0) {
    return 0;
  }

  token open = pp_next(p);
  if (open.type != TOKEN_LPAREN) {
    pp_unget(p, open);
    return 0;
  }
  free_token(open);

  token text = pp_next(p);
  if (text.type != TOKEN_STRING && text.type != TOKEN_WIDE_STRING) {
    pp_error(p, t.line, t.column, "_Pragma requires a string literal");
    pp_unget(p, text);
    free_token(t);
    return 1;
  }
  free_token(text);

  token close = pp_next(p);
  if (close.type != TOKEN_RPAREN) {
    pp_error(p, t.line, t.column, "expected ) after _Pragma");
    pp_unget(p, close);
    free_token(t);
    return 1;
  }
  free_token(close);
  free_token(t);
  return 1;
}

static int try_expand(pp *p, token t) {
  if (t.type != TOKEN_IDENTIFIER) {
    return 0;
  }

  if (try_pragma_operator(p, t)) {
    return 1;
  }

  macro *m = macro_find(p, t.value);
  if (m == NULL || macro_disabled(p, t.value)) {
    return 0;
  }

  if (p->stack == NULL) {
    p->invocation_line = t.line;
  }

  if (m->dynamic != DYNAMIC_NONE) {
    token value = make_dynamic_token(p, m->dynamic, t);
    free_token(t);
    token *list = malloc(sizeof(token));
    if (list == NULL) {
      free_token(value);
      return 1;
    }
    list[0] = value;
    push_token_list(p, m->name, list, 1);
    return 1;
  }

  if (!m->is_function_like) {
    free_token(t);
    substitute(p, m, NULL, 0);
    return 1;
  }

  token nxt = pp_next(p);
  if (nxt.type != TOKEN_LPAREN) {
    pp_unget(p, nxt);
    return 0;
  }
  free_token(nxt);

  arg *args = NULL;
  int arg_count = 0;
  if (!collect_args(p, m, &args, &arg_count)) {
    return 0;
  }

  free_token(t);
  substitute(p, m, args, arg_count);
  free_args(args, arg_count);
  return 1;
}

static void skip_directive_line(pp *p) {
  if (p == NULL)
    return;
  while (1) {
    token t = pp_next(p);
    if (t.type == TOKEN_EOF || t.at_line_start) {
      pp_unget(p, t);
      return;
    }
    free_token(t);
  }
}

static void abort_define(pp *p, token t, char **params, int param_count, token name) {
  if (t.type == TOKEN_EOF || t.at_line_start) {
    pp_unget(p, t);
  } else {
    free_token(t);
    skip_directive_line(p);
  }
  free_params(params, param_count);
  free_token(name);
}

static void do_define(pp *p) {
  token name = pp_next(p);
  char **params = NULL;
  int param_count = 0;
  int is_function_like = 0;
  int is_variadic = 0;

  if (name.type != TOKEN_IDENTIFIER || name.at_line_start) {
    pp_unget(p, name);
    skip_directive_line(p);
    return;
  }

  if (is_reserved_macro_name(name.value)) {
    pp_error(p, name.line, name.column, "this macro name cannot be redefined");
    free_token(name);
    skip_directive_line(p);
    return;
  }

  token t = pp_next(p);

  if (t.type == TOKEN_LPAREN && t.line == name.line &&
      t.column == name.column + (int)strlen(name.value)) {
    free_token(t);
    is_function_like = 1;
    t = pp_next(p);
    if (t.type == TOKEN_RPAREN) {
      free_token(t);
      t = pp_next(p);
    } else {
      while (1) {
        if (t.type == TOKEN_ELLIPSIS) {
          char **tmp = realloc(params, sizeof(char *) * (param_count + 1));
          if (tmp == NULL) {
            abort_define(p, t, params, param_count, name);
            return;
          }
          params = tmp;
          params[param_count++] = strdup("__VA_ARGS__");
          is_variadic = 1;
          free_token(t);
          t = pp_next(p);
          if (t.type != TOKEN_RPAREN) {
            pp_error(p, t.line, t.column, "... must be the last macro parameter");
            abort_define(p, t, params, param_count, name);
            return;
          }
          free_token(t);
          t = pp_next(p);
          break;
        }
        if (t.type == TOKEN_IDENTIFIER) {
          if (strcmp(t.value, "__VA_ARGS__") == 0) {
            pp_error(p, t.line, t.column, "__VA_ARGS__ cannot be used as a parameter name");
            abort_define(p, t, params, param_count, name);
            return;
          }
          char **tmp = realloc(params, sizeof(char *) * (param_count + 1));
          if (tmp == NULL) {
            abort_define(p, t, params, param_count, name);
            return;
          }
          params = tmp;
          params[param_count++] = strdup(t.value);
          free_token(t);
          t = pp_next(p);
          if (t.type == TOKEN_COMMA) {
            free_token(t);
            t = pp_next(p);
            continue;
          } else if (t.type == TOKEN_RPAREN) {
            free_token(t);
            t = pp_next(p);
            break;
          } else {
            abort_define(p, t, params, param_count, name);
            return;
          }
        } else {
          abort_define(p, t, params, param_count, name);
          return;
        }
      }
    }
  }

  token *body = NULL;
  int body_count = 0;
  while (t.type != TOKEN_EOF && !t.at_line_start) {
    token *tmp = realloc(body, sizeof(token) * (body_count + 1));
    if (tmp == NULL) {
      free_token(t);
    } else {
      body = tmp;
      body[body_count++] = t;
    }
    t = pp_next(p);
  }
  pp_unget(p, t);
  if (!is_variadic) {
    for (int i = 0; i < body_count; i++) {
      if (body[i].type == TOKEN_IDENTIFIER && body[i].value != NULL &&
          strcmp(body[i].value, "__VA_ARGS__") == 0) {
        pp_error(p, body[i].line, body[i].column,
                 "__VA_ARGS__ can only appear in a variadic macro");
        break;
      }
    }
  }
  macro_remove(p, name.value);

  macro *m = calloc(1, sizeof(macro));
  if (m == NULL) {
    free_params(params, param_count);
    for (int i = 0; i < body_count; i++) {
      free_token(body[i]);
    }
    free(body);
    free_token(name);
    return;
  }
  m->params = params;
  m->param_count = param_count;
  m->is_function_like = is_function_like;
  m->is_variadic = is_variadic;
  m->name = strdup(name.value);
  m->body = body;
  m->body_count = body_count;
  m->next = p->macros;
  p->macros = m;
  free_token(name);
}

static void do_undef(pp *p) {
  token name = pp_next(p);
  if (name.type == TOKEN_IDENTIFIER && !name.at_line_start) {
    if (is_reserved_macro_name(name.value)) {
      pp_error(p, name.line, name.column, "this macro name cannot be undefined");
    } else {
      macro_remove(p, name.value);
    }
    free_token(name);
  } else {
    pp_unget(p, name);
  }
  skip_directive_line(p);
}

static void do_ifdef(pp *p, int line, int column, int negate) {
  token name = pp_next(p);

  if (name.type != TOKEN_IDENTIFIER || name.at_line_start) {
    pp_error(p, line, column, "expected a macro name after #ifdef");
    pp_unget(p, name);
    cond_push(p, 0, line, column);
    skip_directive_line(p);
    return;
  }

  int defined = (macro_find(p, name.value) != NULL);
  cond_push(p, negate ? !defined : defined, line, column);
  free_token(name);
  skip_directive_line(p);
}

static void do_if(pp *p, int line, int column) {
  int value = 0;
  if (pp_emitting(p)) {
    value = eval_condition(p, line, column);
  } else {
    skip_directive_line(p);
  }
  cond_push(p, value, line, column);
}

static void do_elif(pp *p, int line, int column) {
  if (p->conds == NULL) {
    pp_error(p, line, column, "#elif without #if");
    skip_directive_line(p);
    return;
  }

  if (p->conds->seen_else) {
    pp_error(p, line, column, "#elif after #else");
    p->conds->emitting = 0;
    skip_directive_line(p);
    return;
  }

  cond *c = p->conds;

  if (c->taken || !c->parent_emitting) {
    c->emitting = 0;
    skip_directive_line(p);
    return;
  }

  int value = eval_condition(p, line, column);
  c->emitting = value;
  if (value) {
    c->taken = 1;
  }
}

static void do_else(pp *p, int line, int column) {
  if (p->conds == NULL) {
    pp_error(p, line, column, "#else without #if");
    skip_directive_line(p);
    return;
  }

  if (p->conds->seen_else) {
    pp_error(p, line, column, "#else after #else");
    p->conds->emitting = 0;
    skip_directive_line(p);
    return;
  }

  cond *c = p->conds;
  c->seen_else = 1;
  c->emitting = c->parent_emitting && !c->taken;
  if (c->emitting) {
    c->taken = 1;
  }
  skip_directive_line(p);
}

static void do_endif(pp *p, int line, int column) {
  if (p->conds == NULL) {
    pp_error(p, line, column, "#endif without #if");
    skip_directive_line(p);
    return;
  }
  cond_pop(p);
  skip_directive_line(p);
}

static void do_line(pp *p, int line, int column) {
  token *raw = NULL;
  int raw_count = 0;
  token terminator;

  for (;;) {
    token t = pp_next(p);
    if (t.type == TOKEN_EOF || t.at_line_start) {
      terminator = t;
      break;
    }
    append_token(&raw, &raw_count, t);
  }

  p->invocation_line = line;
  token *expanded = NULL;
  int expanded_count = 0;
  expand_token_list(p, raw, raw_count, &expanded, &expanded_count);
  free_tokens(raw, raw_count);

  if (expanded_count == 0 || expanded[0].type != TOKEN_NUMBER) {
    pp_error(p, line, column, "#line requires a line number");
    free_tokens(expanded, expanded_count);
    pp_unget(p, terminator);
    return;
  }

  const char *digits = expanded[0].value ? expanded[0].value : "";
  for (const char *c = digits; *c != '\0'; c++) {
    if (!isdigit((unsigned char)*c)) {
      pp_error(p, line, column, "#line requires a plain decimal line number");
      free_tokens(expanded, expanded_count);
      pp_unget(p, terminator);
      return;
    }
  }

  long value = strtol(digits, NULL, 10);
  if (value < 1) {
    pp_error(p, line, column, "#line requires a positive line number");
    free_tokens(expanded, expanded_count);
    pp_unget(p, terminator);
    return;
  }

  if (expanded_count > 1) {
    if (expanded[1].type != TOKEN_STRING) {
      pp_error(p, line, column, "#line file name must be a string literal");
    } else if (p->sources != NULL) {
      free(p->sources->presumed_name);
      p->sources->presumed_name = strdup(expanded[1].value ? expanded[1].value : "");
    }
    if (expanded_count > 2) {
      pp_error(p, line, column, "extra tokens after #line");
    }
  }

  if (p->sources != NULL) {
    p->sources->line_offset = (int)value - (line + 1);
  }

  free_tokens(expanded, expanded_count);
  pp_unget(p, terminator);
}

static void do_error(pp *p, int line, int column) {
  char *message = strdup("#error");
  if (message == NULL) {
    skip_directive_line(p);
    return;
  }
  size_t length = strlen(message);

  for (;;) {
    token t = pp_next(p);
    if (t.type == TOKEN_EOF || t.at_line_start) {
      pp_unget(p, t);
      break;
    }
    char *spelling = token_spelling(t);
    free_token(t);
    if (spelling == NULL) {
      continue;
    }
    size_t add = strlen(spelling);
    char *joined = realloc(message, length + add + 2);
    if (joined == NULL) {
      free(spelling);
      break;
    }
    message = joined;
    message[length++] = ' ';
    memcpy(message + length, spelling, add + 1);
    length += add;
    free(spelling);
  }

  pp_error(p, line, column, message);
  free(message);
}

static char *read_header_name(pp *p, int *is_system, int line, int column) {
  *is_system = 0;
  token t = pp_next(p);

  if (t.type == TOKEN_STRING && !t.at_line_start) {
    char *name = strdup(t.value ? t.value : "");
    free_token(t);
    return name;
  }

  if (t.type == TOKEN_LT && !t.at_line_start) {
    free_token(t);
    *is_system = 1;
    char *name = strdup("");
    if (name == NULL)
      return NULL;
    size_t len = 0;
    while (1) {
      token part = pp_next(p);
      if (part.type == TOKEN_GT && !part.at_line_start) {
        free_token(part);
        return name;
      }
      if (part.type == TOKEN_EOF || part.at_line_start) {
        pp_unget(p, part);
        pp_error(p, line, column, "missing > at the end of an #include header name");
        free(name);
        return NULL;
      }
      char *spelling = token_spelling(part);
      free_token(part);
      if (spelling == NULL)
        continue;
      size_t add = strlen(spelling);
      char *joined = realloc(name, len + add + 1);
      if (joined == NULL) {
        free(spelling);
        free(name);
        return NULL;
      }
      name = joined;
      memcpy(name + len, spelling, add + 1);
      len += add;
      free(spelling);
    }
  }

  pp_unget(p, t);
  pp_error(p, line, column, "expected \"FILE\" or <FILE> after #include");
  return NULL;
}

static char *resolve_include(pp *p, const char *name, int is_system) {
  if (!is_system && p->sources != NULL && p->sources->filename != NULL) {
    const char *base = p->sources->filename;
    const char *slash = NULL;
    for (const char *c = base; *c != '\0'; c++) {
      if (*c == '/' || *c == '\\') {
        slash = c;
      }
    }
    if (slash != NULL) {
      size_t dir_len = (size_t)(slash - base);
      char *dir = malloc(dir_len + 1);
      if (dir != NULL) {
        memcpy(dir, base, dir_len);
        dir[dir_len] = '\0';
        char *candidate = join_path(dir, name);
        free(dir);
        if (candidate != NULL && file_exists(candidate)) {
          return candidate;
        }
        free(candidate);
      }
    }
  }

  for (int i = 0; i < p->include_dir_count; i++) {
    char *candidate = join_path(p->include_dirs[i], name);
    if (candidate != NULL && file_exists(candidate)) {
      return candidate;
    }
    free(candidate);
  }

  if (!is_system && file_exists(name)) {
    return strdup(name);
  }
  return NULL;
}

static void do_include(pp *p, int line, int column) {
  int is_system = 0;
  char *name = read_header_name(p, &is_system, line, column);
  skip_directive_line(p);

  if (name == NULL) {
    return;
  }

  int depth = 0;
  for (source_file *sf = p->sources; sf != NULL; sf = sf->next) {
    depth++;
  }
  if (depth >= PP_MAX_INCLUDE_DEPTH) {
    pp_error(p, line, column, "#include nested too deeply");
    free(name);
    return;
  }

  char *path = resolve_include(p, name, is_system);
  if (path == NULL) {
    pp_error(p, line, column, "cannot find the file named by #include");
    free(name);
    return;
  }

  char *text = pp_read_file(path);
  if (text == NULL) {
    pp_error(p, line, column, "cannot read the file named by #include");
    free(path);
    free(name);
    return;
  }

  if (!source_push(p, text, path)) {
    pp_error(p, line, column, "out of memory opening an #include");
  }

  free(text);
  free(path);
  free(name);
}

static void handle_directive(pp *p) {
  if (p == NULL)
    return;

  token d = pp_next(p);

  if (d.type == TOKEN_EOF || d.at_line_start) {
    pp_unget(p, d);
    return;
  }

  const char *name = d.value ? d.value : "";

  if (strcmp(name, "ifdef") == 0) {
    do_ifdef(p, d.line, d.column, 0);
  } else if (strcmp(name, "ifndef") == 0) {
    do_ifdef(p, d.line, d.column, 1);
  } else if (strcmp(name, "if") == 0) {
    do_if(p, d.line, d.column);
  } else if (strcmp(name, "elif") == 0) {
    do_elif(p, d.line, d.column);
  } else if (strcmp(name, "else") == 0) {
    do_else(p, d.line, d.column);
  } else if (strcmp(name, "endif") == 0) {
    do_endif(p, d.line, d.column);
  } else if (!pp_emitting(p)) {
    skip_directive_line(p);
  } else if (strcmp(name, "include") == 0) {
    do_include(p, d.line, d.column);
  } else if (strcmp(name, "define") == 0) {
    do_define(p);
  } else if (strcmp(name, "undef") == 0) {
    do_undef(p);
  } else if (strcmp(name, "line") == 0) {
    do_line(p, d.line, d.column);
  } else if (strcmp(name, "error") == 0) {
    do_error(p, d.line, d.column);
  } else if (strcmp(name, "pragma") == 0) {
    skip_directive_line(p);
  } else {
    pp_error(p, d.line, d.column, "invalid preprocessing directive");
    skip_directive_line(p);
  }

  free_token(d);
}

char *pp_splice_lines(const char *source) {
  if (source == NULL) {
    return NULL;
  }

  char *output = malloc(strlen(source) + 1);
  if (output == NULL) {
    return NULL;
  }
  int i = 0, j = 0;
  int pending_newlines = 0;
  while (source[i] != '\0') {
    if (source[i] == '\\' && source[i + 1] == '\n') {
      i += 2;
      pending_newlines++;
    } else if (source[i] == '\\' && source[i + 1] == '\r' && source[i + 2] == '\n') {
      i += 3;
      pending_newlines++;
    } else if (source[i] == '\n') {
      output[j++] = source[i++];
      while (pending_newlines > 0) {
        output[j++] = '\n';
        pending_newlines--;
      }
    } else {
      output[j++] = source[i++];
    }
  }
  while (pending_newlines > 0) {
    output[j++] = '\n';
    pending_newlines--;
  }
  output[j] = '\0';
  return output;
}

int pp_run_ex(token_buf *out, const char *source, const char *filename, const char **include_dirs,
              int include_dir_count) {
  if (out == NULL)
    return -1;
  token_buf_init(out);
  if (source == NULL)
    return -1;

  pp p;
  if (!pp_init(&p, source, filename)) {
    return -1;
  }
  p.include_dirs = include_dirs;
  p.include_dir_count = include_dir_count;
  while (1) {
    token t = pp_next(&p);

    if (t.type == TOKEN_EOF) {
      if (p.sources != NULL && p.sources->next != NULL) {
        free_token(t);
        source_pop(&p);
        continue;
      }
      for (cond *c = p.conds; c != NULL; c = c->next) {
        pp_error(&p, c->line, c->column, "unterminated #if");
      }
      token_buf_push(out, t);
      break;
    }

    if (t.type == TOKEN_HASH && t.at_line_start) {
      free_token(t);
      handle_directive(&p);
      continue;
    }

    if (!pp_emitting(&p)) {
      free_token(t);
      continue;
    }

    if (try_expand(&p, t)) {
      continue;
    }

    token_buf_push(out, t);
  }

  int errors = p.error_count;
  pp_destroy(&p);
  return errors;
}

int pp_run(token_buf *out, const char *source) {
  return pp_run_ex(out, source, NULL, NULL, 0);
}

int pp_run_file(token_buf *out, const char *path, const char **include_dirs,
                int include_dir_count) {
  char *text = pp_read_file(path);
  int result = pp_run_ex(out, text, path, include_dirs, include_dir_count);
  free(text);
  return result;
}
