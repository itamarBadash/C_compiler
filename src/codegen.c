#include "codegen.h"
#include "lexer.h"
#include "sema.h"
#include "target.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct text {
  char *data;
  size_t length;
  size_t capacity;
} text;

typedef struct named_label {
  const char *name;
  const char *label;
} named_label;

typedef struct switch_case {
  ast_node *node;
  const char *label;
} switch_case;

typedef struct switch_context {
  switch_case *cases;
  int count;
} switch_context;

typedef struct codegen {
  text globals;
  text functions;
  text allocas;
  text body;
  char **strings;
  int string_count;
  int string_capacity;
  int failed;
  const char **defined;
  int defined_count;
  symbol **referenced;
  int referenced_count;
  int uses_memset;
  int uses_memcpy;
  int next_string;
  int next_temp;
  int next_name;
  const char *block;
  int terminated;
  const char *break_label;
  const char *continue_label;
  switch_context *current_switch;
  named_label *labels;
  int label_count;
} codegen;

static const char *value(codegen *cg, ast_node *node);
static const char *address_of(codegen *cg, ast_node *node);
static const char *truth(codegen *cg, ast_node *node);
static void discard(codegen *cg, ast_node *node);
static void statement(codegen *cg, ast_node *node);

static char *vformatted(codegen *cg, const char *pattern, va_list args) {
  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, pattern, copy);
  va_end(copy);
  char *out = needed < 0 ? NULL : (char *)malloc((size_t)needed + 1);
  if (!out) {
    cg->failed = 1;
    return NULL;
  }
  vsnprintf(out, (size_t)needed + 1, pattern, args);
  return out;
}

static void append_piece(codegen *cg, text *out, const char *piece) {
  size_t length = strlen(piece);
  if (out->length + length + 1 > out->capacity) {
    size_t capacity = out->capacity ? out->capacity : 256;
    while (capacity < out->length + length + 1)
      capacity *= 2;
    char *grown = (char *)realloc(out->data, capacity);
    if (!grown) {
      cg->failed = 1;
      return;
    }
    out->data = grown;
    out->capacity = capacity;
  }
  memcpy(out->data + out->length, piece, length + 1);
  out->length += length;
}

static void append(codegen *cg, text *out, const char *pattern, ...) {
  va_list args;
  va_start(args, pattern);
  char *piece = vformatted(cg, pattern, args);
  va_end(args);
  if (!piece)
    return;
  append_piece(cg, out, piece);
  free(piece);
}

static void clear(text *t) {
  t->length = 0;
  if (t->data)
    t->data[0] = '\0';
}

static const char *contents(text *t) {
  return t->data ? t->data : "";
}

static const char *formatted(codegen *cg, const char *pattern, ...) {
  va_list args;
  va_start(args, pattern);
  char *out = vformatted(cg, pattern, args);
  va_end(args);
  if (!out)
    return "";
  if (cg->string_count == cg->string_capacity) {
    int capacity = cg->string_capacity ? cg->string_capacity * 2 : 64;
    char **grown = (char **)realloc(cg->strings, sizeof(char *) * (size_t)capacity);
    if (!grown) {
      cg->failed = 1;
      free(out);
      return "";
    }
    cg->strings = grown;
    cg->string_capacity = capacity;
  }
  cg->strings[cg->string_count++] = out;
  return out;
}

static void free_codegen(codegen *cg) {
  for (int i = 0; i < cg->string_count; i++)
    free(cg->strings[i]);
  free(cg->strings);
  free(cg->globals.data);
  free(cg->functions.data);
  free(cg->allocas.data);
  free(cg->body.data);
  free(cg->defined);
  free(cg->referenced);
  free(cg->labels);
}

static type_info *resolved(type_info *type) {
  while (type && type->kind == TYPE_TYPEDEF && type->symbol)
    type = type->symbol->type;
  return type;
}

static int is_volatile(type_info *type) {
  while (type) {
    if (type->is_volatile)
      return 1;
    type = type->kind == TYPE_TYPEDEF && type->symbol ? type->symbol->type : NULL;
  }
  return 0;
}

static prim_kind integer_kind(type_info *type) {
  type_info *t = resolved(type);
  if (t && t->kind == TYPE_ENUM && t->symbol)
    return t->symbol->enum_type;
  if (t && t->kind == TYPE_PRIMITIVE && !t->is_complex && t->prim >= PRIM_BOOL &&
      t->prim <= PRIM_ULLONG)
    return t->prim;
  return PRIM_NONE;
}

static int is_signed(prim_kind prim) {
  switch (prim) {
  case PRIM_CHAR:
  case PRIM_SCHAR:
  case PRIM_SHORT:
  case PRIM_INT:
  case PRIM_LONG:
  case PRIM_LLONG:
    return 1;
  default:
    return 0;
  }
}

static int is_floating(type_info *type) {
  type_info *t = resolved(type);
  return t && t->kind == TYPE_PRIMITIVE && !t->is_complex &&
         (t->prim == PRIM_FLOAT || t->prim == PRIM_DOUBLE || t->prim == PRIM_LDOUBLE);
}

static int is_pointer(type_info *type) {
  type_info *t = resolved(type);
  return t && t->kind == TYPE_POINTER;
}

static int is_scalar(type_info *type) {
  return integer_kind(type) != PRIM_NONE || is_floating(type) || is_pointer(type);
}

static int is_void(type_info *type) {
  type_info *t = resolved(type);
  return t && t->kind == TYPE_PRIMITIVE && t->prim == PRIM_VOID;
}

static long long size_of(type_info *type) {
  return sema_type_layout(type).size;
}

static int alignment_of(type_info *type) {
  return sema_type_layout(type).alignment;
}

static type_info *primitive(prim_kind prim) {
  static type_info types[PRIM_LDOUBLE + 1];
  types[prim].kind = TYPE_PRIMITIVE;
  types[prim].prim = prim;
  types[prim].array_size = -1;
  return &types[prim];
}

static type_info *promoted_parameter(type_info *type) {
  if (integer_kind(type) != PRIM_NONE && size_of(type) < 4)
    return primitive(PRIM_INT);
  if (is_floating(type) && resolved(type)->prim == PRIM_FLOAT)
    return primitive(PRIM_DOUBLE);
  return type;
}

static const char *llvm_type(codegen *cg, type_info *type) {
  type_info *t = resolved(type);
  if (integer_kind(t) != PRIM_NONE) {
    switch (size_of(t)) {
    case 1:
      return "i8";
    case 2:
      return "i16";
    case 4:
      return "i32";
    default:
      return "i64";
    }
  }
  if (is_pointer(t))
    return "ptr";
  if (t && t->kind == TYPE_ARRAY)
    return formatted(cg, "[%lld x %s]", t->array_size, llvm_type(cg, t->ptr_to));
  if (is_floating(t))
    return t->prim == PRIM_FLOAT ? "float" : t->prim == PRIM_DOUBLE ? "double" : "x86_fp80";
  return "void";
}

static const char *parameter_type(codegen *cg, type_info *type) {
  type_info *t = resolved(type);
  if (t->kind == TYPE_ARRAY || t->kind == TYPE_FUNCTION)
    return "ptr";
  return llvm_type(cg, t);
}

static int plain_name(const char *name) {
  for (; *name; name++) {
    if (!isalnum((unsigned char)*name) && *name != '_' && *name != '.' && *name != '$')
      return 0;
  }
  return 1;
}

static const char *ir_name(codegen *cg, char sigil, const char *name) {
  if (plain_name(name))
    return formatted(cg, "%c%s", sigil, name);
  const char *out = formatted(cg, "%c\"", sigil);
  for (; *name; name++) {
    unsigned char c = (unsigned char)*name;
    if (c < 0x20 || c >= 0x7F || c == '"' || c == '\\')
      out = formatted(cg, "%s\\%02X", out, c);
    else
      out = formatted(cg, "%s%c", out, c);
  }
  return formatted(cg, "%s\"", out);
}

static const char *temp(codegen *cg) {
  return formatted(cg, "%%%d", cg->next_temp++);
}

static const char *new_label(codegen *cg, const char *kind) {
  return formatted(cg, "%s.%d", kind, cg->next_name++);
}

static const char *slot_name(codegen *cg, symbol *sym) {
  return ir_name(cg, '%', formatted(cg, "%s.%d", sym->name, sym->local_slot));
}

static void start_block(codegen *cg, const char *label) {
  if (!cg->terminated)
    append(cg, &cg->body, "  br label %%%s\n", label);
  append(cg, &cg->body, "%s:\n", label);
  cg->block = label;
  cg->terminated = 0;
}

static void emit_line(codegen *cg, const char *line) {
  if (cg->terminated)
    start_block(cg, new_label(cg, "dead"));
  append(cg, &cg->body, "  %s\n", line);
}

static void instruction(codegen *cg, const char *pattern, ...) {
  va_list args;
  va_start(args, pattern);
  char *line = vformatted(cg, pattern, args);
  va_end(args);
  if (!line)
    return;
  emit_line(cg, line);
  free(line);
}

static void terminate(codegen *cg, const char *pattern, ...) {
  va_list args;
  va_start(args, pattern);
  char *line = vformatted(cg, pattern, args);
  va_end(args);
  if (!line)
    return;
  emit_line(cg, line);
  free(line);
  cg->terminated = 1;
}

static const char *compute(codegen *cg, const char *pattern, ...) {
  va_list args;
  va_start(args, pattern);
  char *operation = vformatted(cg, pattern, args);
  va_end(args);
  if (!operation)
    return "poison";
  const char *result = temp(cg);
  instruction(cg, "%s = %s", result, operation);
  free(operation);
  return result;
}

static void branch(codegen *cg, const char *label) {
  terminate(cg, "br label %%%s", label);
}

static void jump(codegen *cg, const char *label) {
  if (!cg->terminated)
    branch(cg, label);
}

static void branch_if(codegen *cg, const char *condition, const char *yes, const char *no) {
  terminate(cg, "br i1 %s, label %%%s, label %%%s", condition, yes, no);
}

static const char *load(codegen *cg, type_info *type, const char *address) {
  return compute(cg, "load %s%s, ptr %s, align %d", is_volatile(type) ? "volatile " : "",
                 llvm_type(cg, type), address, alignment_of(type));
}

static void store(codegen *cg, type_info *type, const char *v, const char *address) {
  instruction(cg, "store %s%s %s, ptr %s, align %d", is_volatile(type) ? "volatile " : "",
              llvm_type(cg, type), v, address, alignment_of(type));
}

static const char *byte_address(codegen *cg, const char *base, long long offset) {
  if (offset == 0)
    return base;
  return compute(cg, "getelementptr inbounds i8, ptr %s, i64 %lld", base, offset);
}

static const char *integer_constant(codegen *cg, type_info *type, unsigned long long bits) {
  int width = (int)size_of(type) * 8;
  if (width < 64) {
    unsigned long long mask = (1ull << width) - 1;
    bits &= mask;
    if ((bits >> (width - 1)) & 1)
      bits |= ~mask;
  }
  return formatted(cg, "%lld", (long long)bits);
}

static const char *float_constant(codegen *cg, type_info *type, long double v) {
  prim_kind prim = resolved(type)->prim;
  if (prim == PRIM_LDOUBLE) {
    unsigned char bytes[sizeof(long double)];
    memcpy(bytes, &v, sizeof bytes);
    unsigned long long mantissa;
    memcpy(&mantissa, bytes, sizeof mantissa);
    return formatted(cg, "0xK%02X%02X%016llX", bytes[9], bytes[8], mantissa);
  }
  double d = prim == PRIM_FLOAT ? (double)(float)v : (double)v;
  unsigned long long bits;
  memcpy(&bits, &d, sizeof bits);
  return formatted(cg, "0x%016llX", bits);
}

static const char *number_value(codegen *cg, ast_node *node) {
  const char *spelling = node->tok.value ? node->tok.value : "0";
  type_info *type = node->expr_type;
  if (!is_floating(type))
    return integer_constant(cg, type, strtoull(spelling, NULL, 0));
  prim_kind prim = resolved(type)->prim;
  long double v = prim == PRIM_FLOAT    ? strtof(spelling, NULL)
                  : prim == PRIM_DOUBLE ? strtod(spelling, NULL)
                                        : strtold(spelling, NULL);
  return float_constant(cg, type, v);
}

static const char *string_literal(codegen *cg, ast_node *node) {
  long long unit = size_of(resolved(node->expr_type)->ptr_to);
  long long bytes = ((long long)node->literal.length + 1) * unit;
  const char *name = formatted(cg, "@.str.%d", cg->next_string++);
  append(cg, &cg->globals, "%s = private unnamed_addr constant [%lld x i8] c\"", name, bytes);
  for (long long i = 0; i < bytes; i++) {
    unsigned char c = (unsigned char)node->literal.bytes[i];
    if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\')
      append(cg, &cg->globals, "%c", c);
    else
      append(cg, &cg->globals, "\\%02X", c);
  }
  append(cg, &cg->globals, "\", align %lld\n", unit);
  return name;
}

static const char *truth_of(codegen *cg, const char *v, type_info *type) {
  if (is_floating(type))
    return compute(cg, "fcmp une %s %s, %s", llvm_type(cg, type), v, float_constant(cg, type, 0));
  if (is_pointer(type))
    return compute(cg, "icmp ne ptr %s, null", v);
  return compute(cg, "icmp ne %s %s, 0", llvm_type(cg, type), v);
}

static const char *convert(codegen *cg, const char *v, type_info *from, type_info *to) {
  prim_kind source = integer_kind(from);
  prim_kind target = integer_kind(to);
  const char *from_type = llvm_type(cg, from);
  const char *to_type = llvm_type(cg, to);
  if (target == PRIM_BOOL && source != PRIM_BOOL)
    return compute(cg, "zext i1 %s to i8", truth_of(cg, v, from));
  if (target != PRIM_NONE && source != PRIM_NONE) {
    long long from_size = size_of(from);
    long long to_size = size_of(to);
    if (from_size == to_size)
      return v;
    if (from_size > to_size)
      return compute(cg, "trunc %s %s to %s", from_type, v, to_type);
    return compute(cg, "%s %s %s to %s", is_signed(source) ? "sext" : "zext", from_type, v,
                   to_type);
  }
  if (target != PRIM_NONE && is_floating(from))
    return compute(cg, "%s %s %s to %s", is_signed(target) ? "fptosi" : "fptoui", from_type, v,
                   to_type);
  if (target != PRIM_NONE && is_pointer(from))
    return compute(cg, "ptrtoint ptr %s to %s", v, to_type);
  if (is_floating(to) && source != PRIM_NONE)
    return compute(cg, "%s %s %s to %s", is_signed(source) ? "sitofp" : "uitofp", from_type, v,
                   to_type);
  if (is_floating(to) && is_floating(from)) {
    prim_kind a = resolved(from)->prim;
    prim_kind b = resolved(to)->prim;
    if (a == b)
      return v;
    return compute(cg, "%s %s %s to %s", a < b ? "fpext" : "fptrunc", from_type, v, to_type);
  }
  if (is_pointer(to) && source != PRIM_NONE) {
    if (strcmp(v, "0") == 0)
      return "null";
    if (size_of(from) < 8)
      v = compute(cg, "%s %s %s to i64", is_signed(source) ? "sext" : "zext", from_type, v);
    return compute(cg, "inttoptr i64 %s to ptr", v);
  }
  return v;
}

static void note_reference(codegen *cg, symbol *sym) {
  for (int i = 0; i < cg->referenced_count; i++) {
    if (strcmp(cg->referenced[i]->name, sym->name) == 0)
      return;
  }
  symbol **grown =
      (symbol **)realloc(cg->referenced, sizeof(symbol *) * (size_t)(cg->referenced_count + 1));
  if (!grown) {
    cg->failed = 1;
    return;
  }
  cg->referenced = grown;
  grown[cg->referenced_count++] = sym;
}

static const char *element_address(codegen *cg, type_info *pointer, const char *base,
                                   const char *index) {
  return compute(cg, "getelementptr inbounds %s, ptr %s, i64 %s",
                 llvm_type(cg, resolved(pointer)->ptr_to), base, index);
}

static const char *address_of(codegen *cg, ast_node *node) {
  switch (node->type) {
  case AST_NODE_TYPE_IDENTIFIER:
    if (node->symbol->kind == SYMBOL_FUNC) {
      note_reference(cg, node->symbol);
      return ir_name(cg, '@', node->symbol->name);
    }
    return slot_name(cg, node->symbol);
  case AST_NODE_TYPE_STRING:
    return string_literal(cg, node);
  case AST_NODE_TYPE_UNARY_OP:
    return value(cg, node->unary_op.operand);
  case AST_NODE_TYPE_ARRAY_SUBSCRIPT: {
    ast_node *pointer = node->array_subscript.left;
    ast_node *index = node->array_subscript.index;
    if (!is_pointer(pointer->expr_type)) {
      pointer = node->array_subscript.index;
      index = node->array_subscript.left;
    }
    const char *base = value(cg, pointer);
    const char *offset = value(cg, index);
    return element_address(cg, pointer->expr_type, base, offset);
  }
  default:
    return "poison";
  }
}

static const char *conversion_value(codegen *cg, ast_node *node) {
  ast_node *operand = node->conversion.operand;
  switch (node->conversion.kind) {
  case CONVERSION_LVALUE: {
    const char *address = address_of(cg, operand);
    return load(cg, operand->expr_type, address);
  }
  case CONVERSION_ARRAY_TO_POINTER:
  case CONVERSION_FUNCTION_TO_POINTER:
    return address_of(cg, operand);
  default: {
    const char *v = value(cg, operand);
    return convert(cg, v, operand->expr_type, node->expr_type);
  }
  }
}

static type_info *sizeof_operand(ast_node *node) {
  ast_node *operand = node->unary_op.operand;
  if (operand->type == AST_NODE_TYPE_CAST && !operand->cast_expr.operand)
    return operand->cast_expr.type;
  return operand->expr_type;
}

static const char *arithmetic(codegen *cg, token_type op, type_info *type, const char *a,
                              const char *b, type_info *b_type) {
  int floating = is_floating(type);
  int is_signed_type = is_signed(integer_kind(type));
  const char *name;
  switch (op) {
  case TOKEN_PLUS:
    name = floating ? "fadd" : "add";
    break;
  case TOKEN_MINUS:
    name = floating ? "fsub" : "sub";
    break;
  case TOKEN_STAR:
    name = floating ? "fmul" : "mul";
    break;
  case TOKEN_SLASH:
    name = floating ? "fdiv" : is_signed_type ? "sdiv" : "udiv";
    break;
  case TOKEN_PERCENT:
    name = is_signed_type ? "srem" : "urem";
    break;
  case TOKEN_AMPERSAND:
    name = "and";
    break;
  case TOKEN_PIPE:
    name = "or";
    break;
  case TOKEN_CARET:
    name = "xor";
    break;
  default:
    name = op == TOKEN_LSHIFT ? "shl" : is_signed_type ? "ashr" : "lshr";
    if (size_of(b_type) > size_of(type))
      b = compute(cg, "trunc %s %s to %s", llvm_type(cg, b_type), b, llvm_type(cg, type));
    else if (size_of(b_type) < size_of(type))
      b = compute(cg, "zext %s %s to %s", llvm_type(cg, b_type), b, llvm_type(cg, type));
    break;
  }
  return compute(cg, "%s %s %s, %s", name, llvm_type(cg, type), a, b);
}

static int is_comparison(token_type op) {
  return op == TOKEN_LT || op == TOKEN_GT || op == TOKEN_LTE || op == TOKEN_GTE || op == TOKEN_EQ ||
         op == TOKEN_NEQ;
}

static const char *compare(codegen *cg, token_type op, type_info *type, const char *a,
                           const char *b) {
  static const char *const floating[] = {"olt", "ogt", "ole", "oge", "oeq", "une"};
  static const char *const signed_order[] = {"slt", "sgt", "sle", "sge", "eq", "ne"};
  static const char *const unsigned_order[] = {"ult", "ugt", "ule", "uge", "eq", "ne"};
  int index = op == TOKEN_LT    ? 0
              : op == TOKEN_GT  ? 1
              : op == TOKEN_LTE ? 2
              : op == TOKEN_GTE ? 3
              : op == TOKEN_EQ  ? 4
                                : 5;
  if (is_floating(type))
    return compute(cg, "fcmp %s %s %s, %s", floating[index], llvm_type(cg, type), a, b);
  const char *const *order = is_signed(integer_kind(type)) ? signed_order : unsigned_order;
  return compute(cg, "icmp %s %s %s, %s", order[index], llvm_type(cg, type), a, b);
}

static const char *pointer_arithmetic(codegen *cg, token_type op, type_info *left, type_info *right,
                                      const char *a, const char *b) {
  if (is_pointer(left) && is_pointer(right)) {
    const char *x = compute(cg, "ptrtoint ptr %s to i64", a);
    const char *y = compute(cg, "ptrtoint ptr %s to i64", b);
    const char *bytes = compute(cg, "sub i64 %s, %s", x, y);
    long long size = size_of(resolved(left)->ptr_to);
    if (size == 1)
      return bytes;
    return compute(cg, "sdiv exact i64 %s, %lld", bytes, size);
  }
  int pointer_left = is_pointer(left);
  const char *index = pointer_left ? b : a;
  if (op == TOKEN_MINUS)
    index = compute(cg, "sub i64 0, %s", index);
  return element_address(cg, pointer_left ? left : right, pointer_left ? a : b, index);
}

static const char *logical_value(codegen *cg, ast_node *node) {
  int is_and = node->binary_op.op.type == TOKEN_AND;
  const char *rhs = new_label(cg, is_and ? "and.rhs" : "or.rhs");
  const char *end = new_label(cg, is_and ? "and.end" : "or.end");
  const char *a = truth(cg, node->binary_op.left);
  const char *from = cg->block;
  if (is_and)
    branch_if(cg, a, rhs, end);
  else
    branch_if(cg, a, end, rhs);
  start_block(cg, rhs);
  const char *b = truth(cg, node->binary_op.right);
  const char *rhs_end = cg->block;
  branch(cg, end);
  start_block(cg, end);
  const char *result =
      compute(cg, "phi i1 [ %s, %%%s ], [ %s, %%%s ]", is_and ? "false" : "true", from, b, rhs_end);
  return compute(cg, "zext i1 %s to %s", result, llvm_type(cg, node->expr_type));
}

static const char *binary_value(codegen *cg, ast_node *node) {
  token_type op = node->binary_op.op.type;
  ast_node *left = node->binary_op.left;
  ast_node *right = node->binary_op.right;
  if (op == TOKEN_COMMA) {
    discard(cg, left);
    return value(cg, right);
  }
  if (op == TOKEN_AND || op == TOKEN_OR)
    return logical_value(cg, node);
  const char *a = value(cg, left);
  const char *b = value(cg, right);
  if (is_comparison(op)) {
    const char *result = compare(cg, op, left->expr_type, a, b);
    return compute(cg, "zext i1 %s to %s", result, llvm_type(cg, node->expr_type));
  }
  if (is_pointer(left->expr_type) || is_pointer(right->expr_type))
    return pointer_arithmetic(cg, op, left->expr_type, right->expr_type, a, b);
  return arithmetic(cg, op, node->expr_type, a, b, right->expr_type);
}

static const char *increment(codegen *cg, ast_node *node) {
  ast_node *operand = node->unary_op.operand;
  type_info *type = operand->expr_type;
  int up = node->unary_op.op.type == TOKEN_PLUS_PLUS;
  const char *address = address_of(cg, operand);
  const char *old = load(cg, type, address);
  const char *updated;
  if (is_pointer(type)) {
    updated = element_address(cg, type, old, up ? "1" : "-1");
  } else {
    type_info *computation = node->unary_op.computation_type;
    const char *wide = convert(cg, old, type, computation);
    const char *one = is_floating(computation) ? float_constant(cg, computation, 1) : "1";
    updated = arithmetic(cg, up ? TOKEN_PLUS : TOKEN_MINUS, computation, wide, one, computation);
    updated = convert(cg, updated, computation, type);
  }
  store(cg, type, updated, address);
  return node->unary_op.is_postfix ? old : updated;
}

static const char *unary_value(codegen *cg, ast_node *node) {
  ast_node *operand = node->unary_op.operand;
  type_info *type = node->expr_type;
  switch (node->unary_op.op.type) {
  case TOKEN_PLUS:
    return value(cg, operand);
  case TOKEN_MINUS: {
    const char *v = value(cg, operand);
    if (is_floating(type))
      return compute(cg, "fneg %s %s", llvm_type(cg, type), v);
    return compute(cg, "sub %s 0, %s", llvm_type(cg, type), v);
  }
  case TOKEN_TILDE: {
    const char *v = value(cg, operand);
    return compute(cg, "xor %s %s, -1", llvm_type(cg, type), v);
  }
  case TOKEN_NOT: {
    const char *t = truth(cg, operand);
    const char *inverted = compute(cg, "xor i1 %s, true", t);
    return compute(cg, "zext i1 %s to %s", inverted, llvm_type(cg, type));
  }
  case TOKEN_STAR:
    value(cg, operand);
    return "";
  case TOKEN_AMPERSAND:
    return address_of(cg, operand);
  case TOKEN_SIZEOF:
    return integer_constant(cg, type, (unsigned long long)size_of(sizeof_operand(node)));
  default:
    return increment(cg, node);
  }
}

static token_type operator_of_assignment(token_type op) {
  switch (op) {
  case TOKEN_PLUS_ASSIGN:
    return TOKEN_PLUS;
  case TOKEN_MINUS_ASSIGN:
    return TOKEN_MINUS;
  case TOKEN_STAR_ASSIGN:
    return TOKEN_STAR;
  case TOKEN_SLASH_ASSIGN:
    return TOKEN_SLASH;
  case TOKEN_PERCENT_ASSIGN:
    return TOKEN_PERCENT;
  case TOKEN_LSHIFT_ASSIGN:
    return TOKEN_LSHIFT;
  case TOKEN_RSHIFT_ASSIGN:
    return TOKEN_RSHIFT;
  case TOKEN_AMPERSAND_ASSIGN:
    return TOKEN_AMPERSAND;
  case TOKEN_CARET_ASSIGN:
    return TOKEN_CARET;
  default:
    return TOKEN_PIPE;
  }
}

static const char *assignment_value(codegen *cg, ast_node *node) {
  ast_node *left = node->assignment.left;
  ast_node *right = node->assignment.right;
  type_info *type = left->expr_type;
  if (node->assignment.op.type == TOKEN_ASSIGN) {
    const char *v = value(cg, right);
    const char *address = address_of(cg, left);
    store(cg, type, v, address);
    return v;
  }
  token_type op = operator_of_assignment(node->assignment.op.type);
  type_info *computation = node->assignment.computation_type;
  const char *address = address_of(cg, left);
  const char *old = load(cg, type, address);
  const char *b = value(cg, right);
  const char *result;
  if (is_pointer(computation)) {
    result = pointer_arithmetic(cg, op, type, right->expr_type, old, b);
  } else {
    const char *a = convert(cg, old, type, computation);
    result = arithmetic(cg, op, computation, a, b, right->expr_type);
    result = convert(cg, result, computation, type);
  }
  store(cg, type, result, address);
  return result;
}

static const char *conditional_value(codegen *cg, ast_node *node) {
  const char *yes = new_label(cg, "cond.true");
  const char *no = new_label(cg, "cond.false");
  const char *end = new_label(cg, "cond.end");
  int has_value = !is_void(node->expr_type);
  branch_if(cg, truth(cg, node->ternary.condition), yes, no);
  start_block(cg, yes);
  const char *a = has_value ? value(cg, node->ternary.true_branch) : "";
  if (!has_value)
    discard(cg, node->ternary.true_branch);
  const char *a_end = cg->block;
  branch(cg, end);
  start_block(cg, no);
  const char *b = has_value ? value(cg, node->ternary.false_branch) : "";
  if (!has_value)
    discard(cg, node->ternary.false_branch);
  const char *b_end = cg->block;
  branch(cg, end);
  start_block(cg, end);
  if (!has_value)
    return "";
  return compute(cg, "phi %s [ %s, %%%s ], [ %s, %%%s ]", llvm_type(cg, node->expr_type), a, a_end,
                 b, b_end);
}

static const char *cast_value(codegen *cg, ast_node *node) {
  ast_node *operand = node->cast_expr.operand;
  if (is_void(node->expr_type)) {
    discard(cg, operand);
    return "";
  }
  const char *v = value(cg, operand);
  return convert(cg, v, operand->expr_type, node->expr_type);
}

static const char *call_value(codegen *cg, ast_node *node) {
  ast_node *callable = node->function_call.callable;
  type_info *fn = resolved(resolved(callable->expr_type)->ptr_to);
  const char *callee = value(cg, callable);
  const char *signature = "";
  const char *arguments = "";
  for (int i = 0; i < node->function_call.arg_count; i++) {
    ast_node *argument = node->function_call.arguments[i];
    const char *v = value(cg, argument);
    const char *type = llvm_type(cg, argument->expr_type);
    arguments = formatted(cg, "%s%s%s %s", arguments, i ? ", " : "", type, v);
    if (!fn->has_prototype || i < fn->param_count)
      signature = formatted(cg, "%s%s%s", signature, i ? ", " : "", type);
  }
  if (fn->has_prototype && fn->is_variadic)
    signature = formatted(cg, "%s%s...", signature, fn->param_count ? ", " : "");
  const char *result = llvm_type(cg, fn->ptr_to);
  if (is_void(fn->ptr_to)) {
    instruction(cg, "call void (%s) %s(%s)", signature, callee, arguments);
    return "";
  }
  return compute(cg, "call %s (%s) %s(%s)", result, signature, callee, arguments);
}

static const char *value(codegen *cg, ast_node *node) {
  switch (node->type) {
  case AST_NODE_TYPE_NUMBER:
    return number_value(cg, node);
  case AST_NODE_TYPE_CHAR_LITERAL:
    return integer_constant(cg, node->expr_type,
                            (unsigned long long)char_constant_value(
                                node->literal.bytes, node->literal.length, node->literal.is_wide));
  case AST_NODE_TYPE_IDENTIFIER:
    return integer_constant(cg, node->expr_type, (unsigned long long)node->symbol->value);
  case AST_NODE_TYPE_CONVERSION:
    return conversion_value(cg, node);
  case AST_NODE_TYPE_UNARY_OP:
    return unary_value(cg, node);
  case AST_NODE_TYPE_BINARY_OP:
    return binary_value(cg, node);
  case AST_NODE_TYPE_ASSIGNMENT:
    return assignment_value(cg, node);
  case AST_NODE_TYPE_TERNARY:
    return conditional_value(cg, node);
  case AST_NODE_TYPE_CAST:
    return cast_value(cg, node);
  case AST_NODE_TYPE_FUNCTION_CALL:
    return call_value(cg, node);
  default:
    return "poison";
  }
}

static const char *truth(codegen *cg, ast_node *node) {
  if (node->type == AST_NODE_TYPE_CONVERSION && node->conversion.kind == CONVERSION_VALUE) {
    ast_node *operand = node->conversion.operand;
    const char *v = value(cg, operand);
    return truth_of(cg, v, operand->expr_type);
  }
  const char *v = value(cg, node);
  return truth_of(cg, v, node->expr_type);
}

static void discard(codegen *cg, ast_node *node) {
  if (!node)
    return;
  type_info *t = resolved(node->expr_type);
  if (node->is_lvalue || (t && (t->kind == TYPE_ARRAY || t->kind == TYPE_FUNCTION)))
    address_of(cg, node);
  else
    value(cg, node);
}

static void copy_string(codegen *cg, initializer_entry *entry, const char *address) {
  ast_node *string = entry->value;
  long long unit = size_of(resolved(string->expr_type)->ptr_to);
  long long bytes = ((long long)string->literal.length + 1) * unit;
  type_layout array = sema_type_layout(entry->type);
  if (array.known && array.size < bytes)
    bytes = array.size;
  const char *source = string_literal(cg, string);
  cg->uses_memcpy = 1;
  instruction(cg,
              "call void @llvm.memcpy.p0.p0.i64(ptr align %d %s, ptr align %lld %s, i64 %lld, "
              "i1 false)",
              alignment_of(entry->type), address, unit, source, bytes);
}

static void initialize(codegen *cg, ast_node *decl, symbol *sym, const char *slot) {
  initializer_layout *layout = &decl->var_decl.init_layout;
  if (!is_scalar(sym->type)) {
    cg->uses_memset = 1;
    instruction(cg, "call void @llvm.memset.p0.i64(ptr align %d %s, i8 0, i64 %lld, i1 false)",
                alignment_of(sym->type), slot, size_of(sym->type));
  }
  for (int i = 0; i < layout->count; i++) {
    initializer_entry *entry = &layout->entries[i];
    const char *address = byte_address(cg, slot, entry->offset);
    if (entry->value->type == AST_NODE_TYPE_STRING) {
      copy_string(cg, entry, address);
    } else {
      const char *v = value(cg, entry->value);
      store(cg, entry->type, v, address);
    }
  }
}

static void local_declaration(codegen *cg, ast_node *decl) {
  symbol *sym = decl->symbol;
  if (!sym || sym->kind != SYMBOL_VAR || sym->has_static_storage)
    return;
  sym->local_slot = cg->next_name++;
  const char *slot = slot_name(cg, sym);
  append(cg, &cg->allocas, "  %s = alloca %s, align %d\n", slot, llvm_type(cg, sym->type),
         alignment_of(sym->type));
  if (decl->var_decl.init_value)
    initialize(cg, decl, sym, slot);
}

static void loop_body(codegen *cg, ast_node *body, const char *break_label,
                      const char *continue_label) {
  const char *outer_break = cg->break_label;
  const char *outer_continue = cg->continue_label;
  cg->break_label = break_label;
  cg->continue_label = continue_label;
  statement(cg, body);
  cg->break_label = outer_break;
  cg->continue_label = outer_continue;
}

static void if_statement(codegen *cg, ast_node *node) {
  const char *then_label = new_label(cg, "if.then");
  const char *else_label = node->if_stmt.else_branch ? new_label(cg, "if.else") : NULL;
  const char *end = new_label(cg, "if.end");
  branch_if(cg, truth(cg, node->if_stmt.condition), then_label, else_label ? else_label : end);
  start_block(cg, then_label);
  statement(cg, node->if_stmt.then_branch);
  jump(cg, end);
  if (else_label) {
    start_block(cg, else_label);
    statement(cg, node->if_stmt.else_branch);
    jump(cg, end);
  }
  start_block(cg, end);
}

static void while_statement(codegen *cg, ast_node *node) {
  const char *condition = new_label(cg, "while.cond");
  const char *body = new_label(cg, "while.body");
  const char *end = new_label(cg, "while.end");
  start_block(cg, condition);
  branch_if(cg, truth(cg, node->while_stmt.condition), body, end);
  start_block(cg, body);
  loop_body(cg, node->while_stmt.body, end, condition);
  jump(cg, condition);
  start_block(cg, end);
}

static void do_statement(codegen *cg, ast_node *node) {
  const char *body = new_label(cg, "do.body");
  const char *condition = new_label(cg, "do.cond");
  const char *end = new_label(cg, "do.end");
  start_block(cg, body);
  loop_body(cg, node->do_while_stmt.body, end, condition);
  start_block(cg, condition);
  branch_if(cg, truth(cg, node->do_while_stmt.condition), body, end);
  start_block(cg, end);
}

static void for_statement(codegen *cg, ast_node *node) {
  statement(cg, node->for_stmt.init);
  const char *condition = new_label(cg, "for.cond");
  const char *body = new_label(cg, "for.body");
  const char *step = new_label(cg, "for.inc");
  const char *end = new_label(cg, "for.end");
  start_block(cg, condition);
  if (node->for_stmt.condition)
    branch_if(cg, truth(cg, node->for_stmt.condition), body, end);
  start_block(cg, body);
  loop_body(cg, node->for_stmt.body, end, step);
  start_block(cg, step);
  discard(cg, node->for_stmt.increment);
  jump(cg, condition);
  start_block(cg, end);
}

static void add_case(codegen *cg, switch_context *context, ast_node *node, const char *kind) {
  switch_case *grown =
      (switch_case *)realloc(context->cases, sizeof(switch_case) * (size_t)(context->count + 1));
  if (!grown) {
    cg->failed = 1;
    return;
  }
  context->cases = grown;
  grown[context->count].node = node;
  grown[context->count].label = new_label(cg, kind);
  context->count++;
}

static void collect_cases(codegen *cg, switch_context *context, ast_node *node) {
  if (!node)
    return;
  switch (node->type) {
  case AST_NODE_TYPE_BLOCK:
  case AST_NODE_TYPE_DECL_GROUP:
    for (int i = 0; i < node->block.count; i++)
      collect_cases(cg, context, node->block.statements[i]);
    break;
  case AST_NODE_TYPE_IF:
    collect_cases(cg, context, node->if_stmt.then_branch);
    collect_cases(cg, context, node->if_stmt.else_branch);
    break;
  case AST_NODE_TYPE_WHILE:
    collect_cases(cg, context, node->while_stmt.body);
    break;
  case AST_NODE_TYPE_DO_WHILE:
    collect_cases(cg, context, node->do_while_stmt.body);
    break;
  case AST_NODE_TYPE_FOR:
    collect_cases(cg, context, node->for_stmt.body);
    break;
  case AST_NODE_TYPE_LABEL:
    collect_cases(cg, context, node->label_stmt.statement);
    break;
  case AST_NODE_TYPE_CASE:
    add_case(cg, context, node, "switch.case");
    collect_cases(cg, context, node->case_stmt.body);
    break;
  case AST_NODE_TYPE_DEFAULT:
    add_case(cg, context, node, "switch.default");
    collect_cases(cg, context, node->default_stmt.body);
    break;
  default:
    break;
  }
}

static const char *case_label(codegen *cg, ast_node *node) {
  switch_context *context = cg->current_switch;
  int i = 0;
  while (context->cases[i].node != node)
    i++;
  return context->cases[i].label;
}

static void switch_statement(codegen *cg, ast_node *node) {
  ast_node *condition = node->switch_stmt.condition;
  type_info *type = condition->expr_type;
  const char *v = value(cg, condition);
  switch_context context = {NULL, 0};
  collect_cases(cg, &context, node->switch_stmt.body);
  const char *end = new_label(cg, "switch.end");
  const char *otherwise = end;
  const char *table = "";
  for (int i = 0; i < context.count; i++) {
    ast_node *label = context.cases[i].node;
    if (label->type == AST_NODE_TYPE_DEFAULT) {
      otherwise = context.cases[i].label;
      continue;
    }
    table =
        formatted(cg, "%s %s %s, label %%%s", table, llvm_type(cg, type),
                  integer_constant(cg, type, label->case_stmt.label_value), context.cases[i].label);
  }
  terminate(cg, "switch %s %s, label %%%s [%s ]", llvm_type(cg, type), v, otherwise, table);
  switch_context *outer = cg->current_switch;
  cg->current_switch = &context;
  loop_body(cg, node->switch_stmt.body, end, cg->continue_label);
  cg->current_switch = outer;
  free(context.cases);
  start_block(cg, end);
}

static const char *user_label(codegen *cg, const char *name) {
  for (int i = 0; i < cg->label_count; i++) {
    if (strcmp(cg->labels[i].name, name) == 0)
      return cg->labels[i].label;
  }
  named_label *grown =
      (named_label *)realloc(cg->labels, sizeof(named_label) * (size_t)(cg->label_count + 1));
  if (!grown) {
    cg->failed = 1;
    return "";
  }
  cg->labels = grown;
  grown[cg->label_count].name = name;
  grown[cg->label_count].label = new_label(cg, "label");
  return grown[cg->label_count++].label;
}

static void return_statement(codegen *cg, ast_node *node) {
  ast_node *result = node->return_stmt.return_value;
  if (!result) {
    terminate(cg, "ret void");
    return;
  }
  const char *v = value(cg, result);
  terminate(cg, "ret %s %s", llvm_type(cg, result->expr_type), v);
}

static void statement(codegen *cg, ast_node *node) {
  if (!node)
    return;
  switch (node->type) {
  case AST_NODE_TYPE_BLOCK:
  case AST_NODE_TYPE_DECL_GROUP:
    for (int i = 0; i < node->block.count; i++)
      statement(cg, node->block.statements[i]);
    break;
  case AST_NODE_TYPE_VAR_DECL:
    local_declaration(cg, node);
    break;
  case AST_NODE_TYPE_IF:
    if_statement(cg, node);
    break;
  case AST_NODE_TYPE_WHILE:
    while_statement(cg, node);
    break;
  case AST_NODE_TYPE_DO_WHILE:
    do_statement(cg, node);
    break;
  case AST_NODE_TYPE_FOR:
    for_statement(cg, node);
    break;
  case AST_NODE_TYPE_SWITCH:
    switch_statement(cg, node);
    break;
  case AST_NODE_TYPE_CASE:
    start_block(cg, case_label(cg, node));
    statement(cg, node->case_stmt.body);
    break;
  case AST_NODE_TYPE_DEFAULT:
    start_block(cg, case_label(cg, node));
    statement(cg, node->default_stmt.body);
    break;
  case AST_NODE_TYPE_BREAK:
    branch(cg, cg->break_label);
    break;
  case AST_NODE_TYPE_CONTINUE:
    branch(cg, cg->continue_label);
    break;
  case AST_NODE_TYPE_GOTO:
    branch(cg, user_label(cg, node->goto_stmt.label_name));
    break;
  case AST_NODE_TYPE_LABEL:
    start_block(cg, user_label(cg, node->label_stmt.label_name));
    statement(cg, node->label_stmt.statement);
    break;
  case AST_NODE_TYPE_RETURN:
    return_statement(cg, node);
    break;
  case AST_NODE_TYPE_EMPTY:
  case AST_NODE_TYPE_STRUCT_DEF:
  case AST_NODE_TYPE_ENUM_DEF:
    break;
  default:
    discard(cg, node);
    break;
  }
}

static int is_main(ast_node *fn) {
  return strcmp(fn->function_def.name, "main") == 0 && fn->symbol->linkage == LINKAGE_EXTERNAL;
}

static void function_definition(codegen *cg, ast_node *fn) {
  type_info *type = fn->function_def.type;
  type_info *result = type->ptr_to;
  int count = type->param_count;
  clear(&cg->allocas);
  clear(&cg->body);
  cg->next_temp = count;
  cg->next_name = 0;
  cg->block = "entry";
  cg->terminated = 0;
  cg->label_count = 0;
  const char *parameters = "";
  for (int i = 0; i < count; i++) {
    symbol *sym = fn->function_def.param_symbols[i];
    type_info *passed = type->has_prototype ? sym->type : promoted_parameter(sym->type);
    parameters = formatted(cg, "%s%s%s %%%d", parameters, i ? ", " : "", llvm_type(cg, passed), i);
    sym->local_slot = cg->next_name++;
    const char *slot = slot_name(cg, sym);
    append(cg, &cg->allocas, "  %s = alloca %s, align %d\n", slot, llvm_type(cg, sym->type),
           alignment_of(sym->type));
    const char *incoming = convert(cg, formatted(cg, "%%%d", i), passed, sym->type);
    store(cg, sym->type, incoming, slot);
  }
  if (type->is_variadic)
    parameters = formatted(cg, "%s%s...", parameters, count ? ", " : "");
  statement(cg, fn->function_def.body);
  if (!cg->terminated && is_void(result))
    terminate(cg, "ret void");
  else if (!cg->terminated && is_main(fn))
    terminate(cg, "ret i32 0");
  else if (!cg->terminated)
    terminate(cg, "ret %s poison", llvm_type(cg, result));
  append(cg, &cg->functions, "define %s%s %s(%s) {\nentry:\n%s%s}\n\n",
         fn->symbol->linkage == LINKAGE_INTERNAL ? "internal " : "", llvm_type(cg, result),
         ir_name(cg, '@', fn->function_def.name), parameters, contents(&cg->allocas),
         contents(&cg->body));
}

static int is_defined_here(codegen *cg, const char *name) {
  for (int i = 0; i < cg->defined_count; i++) {
    if (strcmp(cg->defined[i], name) == 0)
      return 1;
  }
  return 0;
}

static void declare_function(codegen *cg, text *out, symbol *sym) {
  type_info *type = resolved(sym->type);
  const char *parameters = type->has_prototype ? "" : "...";
  for (int i = 0; type->has_prototype && i < type->param_count; i++)
    parameters = formatted(cg, "%s%s%s", parameters, i ? ", " : "",
                           parameter_type(cg, type->param_types[i]));
  if (type->has_prototype && type->is_variadic)
    parameters = formatted(cg, "%s%s...", parameters, type->param_count ? ", " : "");
  append(cg, out, "declare %s %s(%s)\n", llvm_type(cg, type->ptr_to), ir_name(cg, '@', sym->name),
         parameters);
}

char *codegen_program(ast_node *program) {
  codegen cg;
  memset(&cg, 0, sizeof cg);
  ast_node **items = program->program.declarations;
  int count = program->program.count;
  cg.defined = (const char **)calloc((size_t)count + 1, sizeof(const char *));
  if (!cg.defined)
    return NULL;
  for (int i = 0; i < count; i++) {
    if (items[i]->type == AST_NODE_TYPE_FUNCTION_DEF)
      cg.defined[cg.defined_count++] = items[i]->function_def.name;
  }
  for (int i = 0; i < count; i++) {
    if (items[i]->type == AST_NODE_TYPE_FUNCTION_DEF)
      function_definition(&cg, items[i]);
  }
  text out = {NULL, 0, 0};
  const target *t = target_current();
  append(&cg, &out, "target datalayout = \"%s\"\ntarget triple = \"%s\"\n\n", t->data_layout,
         t->triple);
  if (cg.globals.length)
    append(&cg, &out, "%s\n", contents(&cg.globals));
  append(&cg, &out, "%s", contents(&cg.functions));
  for (int i = 0; i < cg.referenced_count; i++) {
    if (!is_defined_here(&cg, cg.referenced[i]->name))
      declare_function(&cg, &out, cg.referenced[i]);
  }
  if (cg.uses_memset)
    append(&cg, &out, "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)\n");
  if (cg.uses_memcpy)
    append(&cg, &out, "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n");
  int failed = cg.failed;
  free_codegen(&cg);
  if (failed) {
    free(out.data);
    return NULL;
  }
  return out.data;
}
