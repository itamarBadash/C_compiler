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
  token pending;
  int has_pending;
  cond *conds_at_entry;
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

static const char *pp_current_file(pp *p);
static void pp_error_in(pp *p, const char *file, int line, int column, const char *message);
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
static cond *file_conds(pp *p);

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

static void report_arguments(pp *p, macro *m, token name, const char *problem);
static int collect_args(pp *p, macro *m, token name, arg **out, int *out_count);
static void free_args(arg *args, int n);
static void expand_token_list(pp *p, token *in, int count, token **out, int *out_count);
static void expand_arg(pp *p, arg *a);
static void substitute(pp *p, macro *m, arg *args, int arg_count, int leading_space);
static token make_dynamic_token(pp *p, dynamic_macro kind, token at);
static int try_pragma_operator(pp *p, token t);
static int try_expand(pp *p, token *t);

static void skip_directive_line(pp *p);
static void end_directive(pp *p, const char *extra_message);
static void abort_define(pp *p, token t, char **params, int param_count, token name);
static void do_define(pp *p, int line, int column);
static void do_undef(pp *p, int line, int column);
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
static void handle_directive(pp *p, int line, int column);

static int is_name(token t) {
  return t.type == TOKEN_IDENTIFIER || token_is_keyword(t.type);
}

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

static const char *intern_file_name(const char *name) {
  static struct interned_name {
    char *name;
    struct interned_name *next;
  } *names = NULL;
  for (struct interned_name *it = names; it != NULL; it = it->next) {
    if (strcmp(it->name, name) == 0)
      return it->name;
  }
  struct interned_name *entry = malloc(sizeof(*entry));
  char *copy = strdup(name);
  if (entry == NULL || copy == NULL) {
    free(entry);
    free(copy);
    return NULL;
  }
  entry->name = copy;
  entry->next = names;
  names = entry;
  return copy;
}

static const char *pp_current_file(pp *p) {
  if (p == NULL || p->sources == NULL || p->sources->lex.file == NULL) {
    return "<source>";
  }
  return p->sources->lex.file;
}

static void pp_error_in(pp *p, const char *file, int line, int column, const char *message) {
  if (p == NULL)
    return;
  p->error_count++;
  fprintf(stderr, "%s:%d:%d: error: %s\n", file, line, column, message);
}

static void pp_error(pp *p, int line, int column, const char *message) {
  pp_error_in(p, pp_current_file(p), line, column, message);
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
  sf->conds_at_entry = p->conds;
  sf->next = p->sources;
  lexer_init(&sf->lex, sf->spliced);
  sf->lex.file = filename ? intern_file_name(filename) : NULL;
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
  p->error_count += sf->lex.error_count;
  free(sf->spliced);
  free(sf->filename);
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
    token t = {TOKEN_EOF, NULL, 1, 1, 0, NULL, 0, 0};
    return t;
  }
  if (p->sources->has_pending) {
    p->sources->has_pending = 0;
    return p->sources->pending;
  }
  pop_exhausted(p);
  if (p->stack != NULL) {
    if (p->stack->pos >= p->stack->count) {
      token t = {TOKEN_EOF, NULL, 1, 1, 0, NULL, 0, 0};
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

static cond *file_conds(pp *p) {
  return p->sources != NULL ? p->sources->conds_at_entry : NULL;
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
  const char *error;
  number_kind kind = classify_number(s, &error);
  if (kind == NUMBER_INVALID) {
    ev_error(e, error);
    return 0;
  }
  if (kind == NUMBER_FLOATING) {
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

  int has_unsigned_suffix;
  int long_count;
  integer_suffix(end, &has_unsigned_suffix, &long_count);

  int decimal = s[0] != '0';
  if (decimal && !has_unsigned_suffix && raw > (unsigned long long)LLONG_MAX) {
    ev_error(e, "integer constant is too large for #if");
    return 0;
  }
  out->value = (long long)raw;
  out->is_unsigned = has_unsigned_suffix || raw > (unsigned long long)LLONG_MAX;
  return 1;
}

static int ev_char_value(eval *e, const token *t, int wide, long long *out) {
  const char *s = t->value ? t->value : "";
  char *bytes = malloc(2 * strlen(s) + 2);
  if (bytes == NULL) {
    ev_error(e, "out of memory in #if");
    return 0;
  }
  int units = 0;
  const char *error = decode_literal(s, wide, bytes, &units);
  if (error != NULL)
    ev_error(e, error);
  *out = char_constant_value(bytes, units, wide);
  free(bytes);
  return error == NULL;
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
    if (live && !v.is_unsigned && v.value == LLONG_MIN)
      ev_error(e, "integer overflow in #if expression");
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
      if (live && !v.is_unsigned && mul_overflows(v.value, rhs.value))
        ev_error(e, "integer overflow in #if expression");
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
    if (live && !v.is_unsigned &&
        (op == TOKEN_PLUS ? add_overflows(v.value, rhs.value) : sub_overflows(v.value, rhs.value)))
      ev_error(e, "integer overflow in #if expression");
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
      if (!v.is_unsigned && v.value > (LLONG_MAX >> count))
        ev_error(e, "integer overflow in #if expression");
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
      if (i + 3 < count && is_name(toks[i + 2]) && toks[i + 3].type == TOKEN_RPAREN) {
        name_index = i + 2;
        consumed = 4;
      }
    } else if (i + 1 < count && is_name(toks[i + 1])) {
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
    n.file = toks[i].file;
    n.no_expand = 0;
    n.leading_space = toks[i].leading_space;
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
  out.file = (n > 0) ? toks[0].file : NULL;
  out.no_expand = 0;
  out.leading_space = 0;

  size_t cap = 32;
  size_t len = 0;
  char *text = malloc(cap);
  if (text == NULL)
    return out;
  text[0] = '\0';

  for (int i = 0; i < n; i++) {
    char *sp = token_spelling(toks[i]);
    if (sp == NULL)
      continue;

    int gap = i > 0 && toks[i].leading_space;

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
    int literal = toks[i].type == TOKEN_STRING || toks[i].type == TOKEN_CHAR_LITERAL ||
                  toks[i].type == TOKEN_WIDE_STRING || toks[i].type == TOKEN_WIDE_CHAR;
    for (char *c = sp; *c != '\0'; c++) {
      if (literal && (*c == '\\' || *c == '"'))
        text[len++] = '\\';
      text[len++] = *c;
    }
    text[len] = '\0';
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
    first.file = lhs.file;
    first.leading_space = lhs.leading_space;
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

static void report_arguments(pp *p, macro *m, token name, const char *problem) {
  char message[128];
  snprintf(message, sizeof message, "%s '%.64s'", problem, m->name);
  pp_error(p, name.line, name.column, message);
}

static int collect_args(pp *p, macro *m, token name, arg **out, int *out_count) {
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
      report_arguments(p, m, name, "unterminated argument list invoking macro");
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
    report_arguments(p, m, name, "at least one argument is required for '...' in macro");
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
    report_arguments(p, m, name, "wrong number of arguments to macro");
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
    if (try_expand(p, &t))
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

static void substitute(pp *p, macro *m, arg *args, int arg_count, int leading_space) {
  token *out = NULL;
  int out_count = 0;
  int paste = 0;
  int left_empty = 1;

  for (int i = 0; i < m->body_count; i++) {
    if (m->body[i].type == TOKEN_HASH_HASH) {
      paste = 1;
      continue;
    }

    int spaced = m->body[i].leading_space;
    token *piece = &m->body[i];
    int piece_count = 1;
    token stringized = {TOKEN_EOF, NULL, 0, 0, 0, NULL, 0, 0};
    int hashed = -1;
    if (m->is_function_like && m->body[i].type == TOKEN_HASH && i + 1 < m->body_count &&
        is_name(m->body[i + 1]))
      hashed = param_index(m, m->body[i + 1].value);
    int idx = is_name(m->body[i]) ? param_index(m, m->body[i].value) : -1;
    if (hashed >= 0 && hashed < arg_count) {
      stringized = stringize(args[hashed].raw, args[hashed].raw_count);
      piece = &stringized;
      i++;
    } else if (idx >= 0 && idx < arg_count) {
      int raw = paste || (i + 1 < m->body_count && m->body[i + 1].type == TOKEN_HASH_HASH);
      piece = raw ? args[idx].raw : args[idx].expanded;
      piece_count = raw ? args[idx].raw_count : args[idx].expanded_count;
    }

    int first = 0;
    token pasted;
    if (paste && !left_empty && piece_count > 0 &&
        paste_tokens(out[out_count - 1], piece[0], &pasted)) {
      free_token(out[out_count - 1]);
      out[out_count - 1] = pasted;
      first = 1;
    }
    for (int j = first; j < piece_count; j++) {
      token copy = clone_token(piece[j]);
      if (j == 0)
        copy.leading_space = spaced;
      append_token(&out, &out_count, copy);
    }
    left_empty = paste ? left_empty && piece_count == 0 : piece_count == 0;
    paste = 0;
    free_token(stringized);
  }

  if (out_count > 0)
    out[0].leading_space = leading_space;
  push_token_list(p, m->name, out, out_count);
}

static token make_dynamic_token(pp *p, dynamic_macro kind, token at) {
  token out;
  out.value = NULL;
  out.line = at.line;
  out.column = at.column;
  out.at_line_start = at.at_line_start;
  out.file = at.file;
  out.no_expand = 0;
  out.leading_space = at.leading_space;

  if (kind == DYNAMIC_LINE) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", p->invocation_line);
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

static int try_expand(pp *p, token *t) {
  if (!is_name(*t) || t->no_expand) {
    return 0;
  }

  if (try_pragma_operator(p, *t)) {
    return 1;
  }

  macro *m = macro_find(p, t->value);
  if (m == NULL) {
    return 0;
  }
  if (macro_disabled(p, t->value)) {
    t->no_expand = 1;
    return 0;
  }

  if (p->stack == NULL) {
    p->invocation_line = t->line;
  }

  if (m->dynamic != DYNAMIC_NONE) {
    token value = make_dynamic_token(p, m->dynamic, *t);
    free_token(*t);
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
    free_token(*t);
    substitute(p, m, NULL, 0, t->leading_space);
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
  if (!collect_args(p, m, *t, &args, &arg_count)) {
    return 0;
  }

  free_token(*t);
  substitute(p, m, args, arg_count, t->leading_space);
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

static void end_directive(pp *p, const char *extra_message) {
  token t = pp_next(p);
  if (t.type != TOKEN_EOF && !t.at_line_start)
    pp_error(p, t.line, t.column, extra_message);
  pp_unget(p, t);
  skip_directive_line(p);
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

static int param_index_in(char **params, int param_count, const char *name) {
  for (int i = 0; i < param_count; i++) {
    if (strcmp(params[i], name) == 0)
      return i;
  }
  return -1;
}

static const char *replacement_list_problem(char **params, int param_count, int is_function_like,
                                            token *body, int body_count) {
  if (body_count > 0 &&
      (body[0].type == TOKEN_HASH_HASH || body[body_count - 1].type == TOKEN_HASH_HASH))
    return "'##' cannot appear at either end of a macro replacement list";
  for (int i = 0; is_function_like && i < body_count; i++) {
    if (body[i].type == TOKEN_HASH &&
        (i + 1 == body_count || param_index_in(params, param_count, body[i + 1].value) < 0))
      return "'#' is not followed by a macro parameter";
  }
  return NULL;
}

static int space_before(const token *list, int i) {
  const token *previous = &list[i - 1];
  return list[i].line != previous->line ||
         list[i].column != previous->column + (int)strlen(previous->value ? previous->value : "");
}

static int same_definition(macro *m, char **params, int param_count, int is_function_like,
                           token *body, int body_count) {
  if (m->is_function_like != is_function_like || m->param_count != param_count ||
      m->body_count != body_count)
    return 0;
  for (int i = 0; i < param_count; i++) {
    if (strcmp(m->params[i], params[i]) != 0)
      return 0;
  }
  for (int i = 0; i < body_count; i++) {
    const char *a = m->body[i].value ? m->body[i].value : "";
    const char *b = body[i].value ? body[i].value : "";
    if (m->body[i].type != body[i].type || strcmp(a, b) != 0)
      return 0;
    if (i > 0 && space_before(m->body, i) != space_before(body, i))
      return 0;
  }
  return 1;
}

static void do_define(pp *p, int line, int column) {
  token name = pp_next(p);
  char **params = NULL;
  int param_count = 0;
  int is_function_like = 0;
  int is_variadic = 0;

  if (!is_name(name) || name.at_line_start) {
    pp_error(p, line, column, "expected a macro name after #define");
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
    if (t.type == TOKEN_RPAREN && !t.at_line_start) {
      free_token(t);
      t = pp_next(p);
    } else {
      while (1) {
        if (t.at_line_start) {
          pp_error(p, name.line, name.column, "missing ')' in macro parameter list");
          abort_define(p, t, params, param_count, name);
          return;
        }
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
          if (t.type != TOKEN_RPAREN || t.at_line_start) {
            pp_error(p, t.line, t.column, "... must be the last macro parameter");
            abort_define(p, t, params, param_count, name);
            return;
          }
          free_token(t);
          t = pp_next(p);
          break;
        }
        if (is_name(t)) {
          if (strcmp(t.value, "__VA_ARGS__") == 0) {
            pp_error(p, t.line, t.column, "__VA_ARGS__ cannot be used as a parameter name");
            abort_define(p, t, params, param_count, name);
            return;
          }
          if (param_index_in(params, param_count, t.value) >= 0) {
            pp_error(p, t.line, t.column, "duplicate macro parameter");
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
          if (t.type == TOKEN_COMMA && !t.at_line_start) {
            free_token(t);
            t = pp_next(p);
            continue;
          } else if (t.type == TOKEN_RPAREN && !t.at_line_start) {
            free_token(t);
            t = pp_next(p);
            break;
          } else {
            pp_error(p, t.at_line_start ? name.line : t.line,
                     t.at_line_start ? name.column : t.column,
                     "expected ',' or ')' in macro parameter list");
            abort_define(p, t, params, param_count, name);
            return;
          }
        } else {
          pp_error(p, t.line, t.column, "expected a macro parameter name");
          abort_define(p, t, params, param_count, name);
          return;
        }
      }
    }
  }

  if (t.type != TOKEN_EOF && !t.at_line_start && t.column == name.column + (int)strlen(name.value))
    pp_error(p, t.line, t.column, "an object-like macro needs whitespace after its name");

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
  const char *operator_problem =
      replacement_list_problem(params, param_count, is_function_like, body, body_count);
  if (operator_problem != NULL) {
    pp_error(p, name.line, name.column, operator_problem);
    free_params(params, param_count);
    free_tokens(body, body_count);
    free_token(name);
    return;
  }
  macro *old = macro_find(p, name.value);
  if (old != NULL && !same_definition(old, params, param_count, is_function_like, body, body_count))
    pp_error(p, name.line, name.column, "macro redefined with a different definition");
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

static void do_undef(pp *p, int line, int column) {
  token name = pp_next(p);
  if (is_name(name) && !name.at_line_start) {
    if (is_reserved_macro_name(name.value)) {
      pp_error(p, name.line, name.column, "this macro name cannot be undefined");
    } else {
      macro_remove(p, name.value);
    }
    free_token(name);
  } else {
    pp_error(p, line, column, "expected a macro name after #undef");
    pp_unget(p, name);
  }
  end_directive(p, "extra tokens after #undef");
}

static void do_ifdef(pp *p, int line, int column, int negate) {
  token name = pp_next(p);

  if (!is_name(name) || name.at_line_start) {
    pp_error(p, line, column, "expected a macro name after #ifdef");
    pp_unget(p, name);
    cond_push(p, 0, line, column);
    skip_directive_line(p);
    return;
  }

  int defined = (macro_find(p, name.value) != NULL);
  cond_push(p, negate ? !defined : defined, line, column);
  free_token(name);
  end_directive(p, negate ? "extra tokens after #ifndef" : "extra tokens after #ifdef");
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
  if (p->conds == file_conds(p)) {
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
  if (p->conds == file_conds(p)) {
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
  end_directive(p, "extra tokens after #else");
}

static void do_endif(pp *p, int line, int column) {
  if (p->conds == file_conds(p)) {
    pp_error(p, line, column, "#endif without #if");
    skip_directive_line(p);
    return;
  }
  cond_pop(p);
  end_directive(p, "extra tokens after #endif");
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
      const char *name = intern_file_name(expanded[1].value ? expanded[1].value : "");
      p->sources->lex.file = name;
      if (terminator.type != TOKEN_EOF)
        terminator.file = name;
    }
    if (expanded_count > 2) {
      pp_error(p, line, column, "extra tokens after #line");
    }
  }

  if (p->sources != NULL) {
    int old_offset = p->sources->lex.line_offset;
    p->sources->lex.line_offset = (int)value - (line - old_offset + 1);
    if (terminator.type != TOKEN_EOF)
      terminator.line += p->sources->lex.line_offset - old_offset;
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

static char *join_spellings(token *toks, int count) {
  char *name = strdup("");
  size_t len = 0;
  for (int i = 0; name != NULL && i < count; i++) {
    char *spelling = token_spelling(toks[i]);
    if (spelling == NULL)
      continue;
    size_t add = strlen(spelling);
    char *joined = realloc(name, len + add + 1);
    if (joined == NULL)
      free(name);
    else
      memcpy(joined + len, spelling, add + 1);
    name = joined;
    len += add;
    free(spelling);
  }
  return name;
}

static char *read_header_name(pp *p, int *is_system, int line, int column) {
  *is_system = 0;
  token *toks = NULL;
  int count = 0;
  token terminator;
  for (;;) {
    token t = pp_next(p);
    if (t.type == TOKEN_EOF || t.at_line_start) {
      terminator = t;
      break;
    }
    append_token(&toks, &count, t);
  }

  if (count > 0 && toks[0].type != TOKEN_STRING && toks[0].type != TOKEN_LT) {
    token *expanded = NULL;
    int expanded_count = 0;
    p->invocation_line = line;
    expand_token_list(p, toks, count, &expanded, &expanded_count);
    free_tokens(toks, count);
    toks = expanded;
    count = expanded_count;
  }
  pp_unget(p, terminator);

  char *name = NULL;
  int end = 0;
  if (count > 0 && toks[0].type == TOKEN_STRING) {
    name = strdup(toks[0].value ? toks[0].value : "");
    end = 1;
  } else if (count > 0 && toks[0].type == TOKEN_LT) {
    end = 1;
    while (end < count && toks[end].type != TOKEN_GT)
      end++;
    if (end == count) {
      pp_error(p, line, column, "missing > at the end of an #include header name");
      free_tokens(toks, count);
      return NULL;
    }
    *is_system = 1;
    name = join_spellings(toks + 1, end - 1);
    end++;
  } else {
    pp_error(p, line, column, "expected \"FILE\" or <FILE> after #include");
  }
  if (name != NULL && end < count)
    pp_error(p, toks[end].line, toks[end].column, "extra tokens after #include");
  free_tokens(toks, count);
  return name;
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

static void handle_directive(pp *p, int line, int column) {
  if (p == NULL)
    return;

  lexer *lex = &p->sources->lex;
  int vertical_spaces = lex->vertical_spaces;
  int processed = pp_emitting(p);
  const char *file = pp_current_file(p);
  token d = pp_next(p);

  if (d.type == TOKEN_EOF || d.at_line_start) {
    pp_unget(p, d);
    if (processed && lex->vertical_spaces != vertical_spaces)
      pp_error_in(p, file, line, column, "form feed or vertical tab in a preprocessing directive");
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
    do_define(p, d.line, d.column);
  } else if (strcmp(name, "undef") == 0) {
    do_undef(p, d.line, d.column);
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

  if (processed && lex->vertical_spaces != vertical_spaces)
    pp_error_in(p, file, line, column, "form feed or vertical tab in a preprocessing directive");
  free_token(d);
}

static char phase_one_char(const char *s, int *width) {
  static const char trigraphs[] = "=#([/\\)]'^<{!|>}-~";
  *width = 1;
  if (s[0] == '?' && s[1] == '?') {
    for (const char *t = trigraphs; *t; t += 2) {
      if (*t == s[2]) {
        *width = 3;
        return t[1];
      }
    }
  }
  return s[0];
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
    int width;
    char c = phase_one_char(source + i, &width);
    if (c == '\\' && source[i + width] == '\n') {
      i += width + 1;
      pending_newlines++;
    } else if (c == '\\' && source[i + width] == '\r' && source[i + width + 1] == '\n') {
      i += width + 2;
      pending_newlines++;
    } else if (c == '\n') {
      output[j++] = source[i++];
      while (pending_newlines > 0) {
        output[j++] = '\n';
        pending_newlines--;
      }
    } else {
      output[j++] = c;
      i += width;
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
      while (p.conds != file_conds(&p)) {
        pp_error(&p, p.conds->line, p.conds->column, "unterminated #if");
        cond_pop(&p);
      }
      if (p.sources != NULL && p.sources->next != NULL) {
        free_token(t);
        source_pop(&p);
        continue;
      }
      token_buf_push(out, t);
      break;
    }

    if (t.type == TOKEN_HASH && t.at_line_start) {
      free_token(t);
      handle_directive(&p, t.line, t.column);
      continue;
    }

    if (!pp_emitting(&p)) {
      free_token(t);
      continue;
    }

    if (try_expand(&p, &t)) {
      continue;
    }

    if (t.type == TOKEN_UNKNOWN && t.value != NULL) {
      char message[48];
      snprintf(message, sizeof message, "unexpected character '%s' in source", t.value);
      pp_error(&p, t.line, t.column, message);
    } else if (t.type == TOKEN_IDENTIFIER && strcmp(t.value, "__VA_ARGS__") == 0) {
      pp_error(&p, t.line, t.column, "__VA_ARGS__ can only appear in a variadic macro");
    }
    token_buf_push(out, t);
  }

  pp_destroy(&p);
  return p.error_count;
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
