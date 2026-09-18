#include "sema.h"
#include "lexer.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct sema {
  symbol_table *table;
  ast_node *program;
  ast_node **gotos;
  int goto_count;
  int error_count;
  type_info *return_type;
  int loops;
  int breakables;
  struct switch_context *current_switch;
  int inline_definition;
} sema;

static void resolve(sema *s, ast_node *node);
static void resolve_type(sema *s, type_info *type, source_loc loc);
static long long initialize_declared(sema *s, type_info *type, ast_node *value, int is_static,
                                     source_loc loc);
static type_info *sized_array(sema *s, type_info *type, long long count);

static void print_location(source_loc loc) {
  if (loc.file)
    fprintf(stderr, "%s:", loc.file);
  fprintf(stderr, "%d:%d: ", loc.line, loc.column);
}

static void report(sema *s, source_loc loc, const char *message, const char *name) {
  print_location(loc);
  fprintf(stderr, "error: %s '%s'\n", message, name);
  s->error_count++;
}

static void report_at(sema *s, source_loc loc, const char *message) {
  print_location(loc);
  fprintf(stderr, "error: %s\n", message);
  s->error_count++;
}

static void note_previous(source_loc loc, const char *name) {
  print_location(loc);
  fprintf(stderr, "note: previous declaration of '%s' is here\n", name);
}

static void note_at(source_loc loc, const char *message) {
  print_location(loc);
  fprintf(stderr, "note: %s\n", message);
}

typedef struct int_value {
  prim_kind type;
  unsigned long long bits;
} int_value;

static int integer_width(prim_kind prim) {
  switch (prim) {
  case PRIM_BOOL:
    return 1;
  case PRIM_CHAR:
  case PRIM_SCHAR:
  case PRIM_UCHAR:
    return 8;
  case PRIM_SHORT:
  case PRIM_USHORT:
    return 16;
  case PRIM_INT:
  case PRIM_UINT:
  case PRIM_LONG:
  case PRIM_ULONG:
    return 32;
  case PRIM_LLONG:
  case PRIM_ULLONG:
    return 64;
  default:
    return 0;
  }
}

static int is_unsigned(prim_kind prim) {
  switch (prim) {
  case PRIM_BOOL:
  case PRIM_UCHAR:
  case PRIM_USHORT:
  case PRIM_UINT:
  case PRIM_ULONG:
  case PRIM_ULLONG:
    return 1;
  default:
    return 0;
  }
}

static int integer_rank(prim_kind prim) {
  switch (prim) {
  case PRIM_BOOL:
    return 1;
  case PRIM_CHAR:
  case PRIM_SCHAR:
  case PRIM_UCHAR:
    return 2;
  case PRIM_SHORT:
  case PRIM_USHORT:
    return 3;
  case PRIM_INT:
  case PRIM_UINT:
    return 4;
  case PRIM_LONG:
  case PRIM_ULONG:
    return 5;
  case PRIM_LLONG:
  case PRIM_ULLONG:
    return 6;
  default:
    return 0;
  }
}

static long long primitive_size(prim_kind prim) {
  switch (prim) {
  case PRIM_BOOL:
  case PRIM_CHAR:
  case PRIM_SCHAR:
  case PRIM_UCHAR:
    return 1;
  case PRIM_SHORT:
  case PRIM_USHORT:
    return 2;
  case PRIM_INT:
  case PRIM_UINT:
  case PRIM_LONG:
  case PRIM_ULONG:
  case PRIM_FLOAT:
    return 4;
  case PRIM_LLONG:
  case PRIM_ULLONG:
  case PRIM_DOUBLE:
    return 8;
  case PRIM_LDOUBLE:
    return 16;
  default:
    return 0;
  }
}

static int_value convert_value(unsigned long long bits, prim_kind type) {
  int_value out;
  out.type = type;
  if (type == PRIM_BOOL) {
    out.bits = bits != 0;
    return out;
  }
  int width = integer_width(type);
  unsigned long long mask = width == 64 ? ~0ull : ((1ull << width) - 1);
  out.bits = bits & mask;
  if (!is_unsigned(type) && (out.bits >> (width - 1)) & 1)
    out.bits |= ~mask;
  return out;
}

static int value_fits(int_value value, prim_kind type) {
  int negative = !is_unsigned(value.type) && (long long)value.bits < 0;
  if (!negative && !is_unsigned(type) && value.bits > (unsigned long long)LLONG_MAX)
    return 0;
  return convert_value(value.bits, type).bits == value.bits;
}

static prim_kind unsigned_version(prim_kind prim) {
  switch (prim) {
  case PRIM_INT:
    return PRIM_UINT;
  case PRIM_LONG:
    return PRIM_ULONG;
  case PRIM_LLONG:
    return PRIM_ULLONG;
  default:
    return prim;
  }
}

static prim_kind promote(prim_kind prim) {
  return integer_rank(prim) < integer_rank(PRIM_INT) ? PRIM_INT : prim;
}

static prim_kind usual_conversions(prim_kind a, prim_kind b) {
  a = promote(a);
  b = promote(b);
  if (a == b)
    return a;
  if (is_unsigned(a) == is_unsigned(b))
    return integer_rank(a) > integer_rank(b) ? a : b;
  prim_kind unsigned_type = is_unsigned(a) ? a : b;
  prim_kind signed_type = is_unsigned(a) ? b : a;
  if (integer_rank(unsigned_type) >= integer_rank(signed_type))
    return unsigned_type;
  if (integer_width(signed_type) > integer_width(unsigned_type))
    return signed_type;
  return unsigned_version(signed_type);
}

typedef enum { CONST_VALUE, CONST_NO_VALUE, CONST_NOT_CONSTANT } const_status;

typedef struct constant {
  const_status status;
  int_value value;
  const char *problem;
  source_loc problem_loc;
} constant;

static constant constant_of(int_value value) {
  constant out = {CONST_VALUE, value, NULL, {0, 0, NULL}};
  return out;
}

static constant unknown_constant(prim_kind type) {
  int_value value = {type, 0};
  constant out = {CONST_NO_VALUE, value, NULL, {0, 0, NULL}};
  return out;
}

static constant failed_constant(prim_kind type, const char *problem, source_loc loc) {
  int_value value = {type, 0};
  constant out = {CONST_NO_VALUE, value, problem, loc};
  return out;
}

static constant non_constant(void) {
  int_value value = {PRIM_INT, 0};
  constant out = {CONST_NOT_CONSTANT, value, NULL, {0, 0, NULL}};
  return out;
}

static void report_problem(sema *s, constant result) {
  if (result.problem)
    report_at(s, result.problem_loc, result.problem);
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static int is_floating_constant(ast_node *number) {
  return classify_number(number->tok.value ? number->tok.value : "", NULL) == NUMBER_FLOATING;
}

static constant evaluate_integer_constant(ast_node *node) {
  const char *text = node->tok.value ? node->tok.value : "";
  if (classify_number(text, NULL) != NUMBER_INTEGER)
    return unknown_constant(PRIM_INT);
  int base = 10;
  const char *p = text;
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    p = text + 2;
  } else if (text[0] == '0') {
    base = 8;
  }

  unsigned long long value = 0;
  int too_large = 0;
  for (; *p; p++) {
    int digit = hex_value(*p);
    if (digit < 0 || digit >= base)
      break;
    if (value > (ULLONG_MAX - (unsigned long long)digit) / (unsigned long long)base)
      too_large = 1;
    value = value * (unsigned long long)base + (unsigned long long)digit;
  }

  int unsigned_suffix;
  int long_suffix;
  integer_suffix(p, &unsigned_suffix, &long_suffix);

  static const prim_kind decimal[] = {PRIM_INT, PRIM_LONG, PRIM_LLONG, PRIM_NONE};
  static const prim_kind decimal_long[] = {PRIM_LONG, PRIM_LLONG, PRIM_NONE};
  static const prim_kind decimal_llong[] = {PRIM_LLONG, PRIM_NONE};
  static const prim_kind other[] = {PRIM_INT,   PRIM_UINT,   PRIM_LONG, PRIM_ULONG,
                                    PRIM_LLONG, PRIM_ULLONG, PRIM_NONE};
  static const prim_kind other_long[] = {PRIM_LONG, PRIM_ULONG, PRIM_LLONG, PRIM_ULLONG, PRIM_NONE};
  static const prim_kind other_llong[] = {PRIM_LLONG, PRIM_ULLONG, PRIM_NONE};
  static const prim_kind unsigned_plain[] = {PRIM_UINT, PRIM_ULONG, PRIM_ULLONG, PRIM_NONE};
  static const prim_kind unsigned_long[] = {PRIM_ULONG, PRIM_ULLONG, PRIM_NONE};
  static const prim_kind unsigned_llong[] = {PRIM_ULLONG, PRIM_NONE};

  const prim_kind *candidates;
  if (unsigned_suffix) {
    candidates = long_suffix == 0   ? unsigned_plain
                 : long_suffix == 1 ? unsigned_long
                                    : unsigned_llong;
  } else if (base == 10) {
    candidates = long_suffix == 0 ? decimal : long_suffix == 1 ? decimal_long : decimal_llong;
  } else {
    candidates = long_suffix == 0 ? other : long_suffix == 1 ? other_long : other_llong;
  }

  int_value magnitude = {PRIM_ULLONG, value};
  for (int i = 0; !too_large && candidates[i] != PRIM_NONE; i++) {
    if (value_fits(magnitude, candidates[i]))
      return constant_of(convert_value(value, candidates[i]));
  }
  return failed_constant(PRIM_ULLONG, "integer constant is too large", node->loc);
}

static constant evaluate_char_constant(ast_node *node) {
  prim_kind type = node->literal.is_wide ? PRIM_USHORT : PRIM_INT;
  if (node->literal.length < 1 || !node->literal.bytes)
    return unknown_constant(type);
  long long value =
      char_constant_value(node->literal.bytes, node->literal.length, node->literal.is_wide);
  return constant_of(convert_value((unsigned long long)value, type));
}

static constant evaluate_floating_cast(ast_node *number, prim_kind target, source_loc loc) {
  double value = strtod(number->tok.value ? number->tok.value : "", NULL);
  if (target == PRIM_BOOL)
    return constant_of(convert_value(value != 0.0, PRIM_BOOL));
  if (!(value < 18446744073709551616.0))
    return failed_constant(target, "overflow in a constant expression", loc);
  int_value truncated = {PRIM_ULLONG, (unsigned long long)value};
  if (!value_fits(truncated, target))
    return failed_constant(target, "overflow in a constant expression", loc);
  return constant_of(convert_value(truncated.bits, target));
}

static type_info *resolved_type(type_info *type) {
  while (type && type->kind == TYPE_TYPEDEF && type->symbol)
    type = type->symbol->type;
  return type;
}

static int is_unresolved(type_info *type) {
  return type && !type->symbol &&
         (type->kind == TYPE_TYPEDEF || type->kind == TYPE_STRUCT || type->kind == TYPE_UNION ||
          type->kind == TYPE_ENUM);
}

static prim_kind integer_type_of(type_info *type) {
  type_info *t = resolved_type(type);
  if (!t)
    return PRIM_NONE;
  if (t->kind == TYPE_PRIMITIVE)
    return integer_width(t->prim) ? t->prim : PRIM_NONE;
  if (t->kind == TYPE_ENUM && t->symbol && t->symbol->is_defined)
    return t->symbol->enum_type;
  return PRIM_NONE;
}

typedef struct type_layout {
  int known;
  long long size;
  int alignment;
} type_layout;

static type_layout layout_of(type_info *type) {
  type_layout out = {0, 0, 1};
  type_info *t = resolved_type(type);
  if (!t || is_unresolved(t))
    return out;
  switch (t->kind) {
  case TYPE_PRIMITIVE:
    out.known = t->prim != PRIM_VOID;
    out.size = primitive_size(t->prim) * (t->is_complex ? 2 : 1);
    out.alignment = (int)primitive_size(t->prim);
    return out;
  case TYPE_POINTER:
  case TYPE_ENUM:
    out.known = 1;
    out.size = t->kind == TYPE_POINTER ? 8 : 4;
    out.alignment = (int)out.size;
    return out;
  case TYPE_ARRAY: {
    type_layout element = layout_of(t->ptr_to);
    if (t->array_size < 0)
      return out;
    element.size *= t->array_size;
    return element;
  }
  case TYPE_STRUCT:
  case TYPE_UNION:
    if (t->symbol->alignment) {
      out.known = 1;
      out.size = t->symbol->size;
      out.alignment = t->symbol->alignment;
    }
    return out;
  default:
    return out;
  }
}

static constant evaluate_sizeof_type(type_info *type) {
  type_info *t = resolved_type(type);
  if (t && t->kind == TYPE_ARRAY) {
    if (t->is_vla)
      return non_constant();
    if (t->array_size < 0)
      return unknown_constant(PRIM_ULLONG);
    constant element = evaluate_sizeof_type(t->ptr_to);
    if (element.status != CONST_VALUE)
      return element;
    return constant_of(
        convert_value(element.value.bits * (unsigned long long)t->array_size, PRIM_ULLONG));
  }
  type_layout layout = layout_of(t);
  if (!layout.known)
    return unknown_constant(PRIM_ULLONG);
  return constant_of(convert_value((unsigned long long)layout.size, PRIM_ULLONG));
}

static constant evaluate(sema *s, ast_node *node, int evaluated);
static constant evaluate_unary(sema *s, ast_node *node, int evaluated);
static constant evaluate_binary(sema *s, ast_node *node, int evaluated);
static constant evaluate_ternary(sema *s, ast_node *node, int evaluated);
static constant evaluate_cast(sema *s, ast_node *node, int evaluated);

static int is_comparison(token_type op) {
  return op == TOKEN_LT || op == TOKEN_GT || op == TOKEN_LTE || op == TOKEN_GTE || op == TOKEN_EQ ||
         op == TOKEN_NEQ;
}

static int propagate(constant *out, constant a, constant b, prim_kind type) {
  if (a.status == CONST_NOT_CONSTANT || b.status == CONST_NOT_CONSTANT) {
    *out = non_constant();
    return 1;
  }
  if (a.status == CONST_NO_VALUE || b.status == CONST_NO_VALUE) {
    *out = a.status == CONST_NO_VALUE ? a : b;
    out->value.type = type;
    return 1;
  }
  return 0;
}

static int add_overflows(long long a, long long b) {
  return (b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b);
}

static int sub_overflows(long long a, long long b) {
  return (b < 0 && a > LLONG_MAX + b) || (b > 0 && a < LLONG_MIN + b);
}

static int mul_overflows(long long a, long long b) {
  if (a == 0 || b == 0)
    return 0;
  if (a > 0)
    return b > 0 ? a > LLONG_MAX / b : b < LLONG_MIN / a;
  return b > 0 ? a < LLONG_MIN / b : b < LLONG_MAX / a;
}

static constant evaluate_arithmetic(token_type op, int_value l, int_value r, source_loc loc) {
  prim_kind type = l.type;
  if (is_comparison(op)) {
    int less = is_unsigned(type) ? l.bits < r.bits : (long long)l.bits < (long long)r.bits;
    int equal = l.bits == r.bits;
    int result;
    switch (op) {
    case TOKEN_LT:
      result = less;
      break;
    case TOKEN_GT:
      result = !less && !equal;
      break;
    case TOKEN_LTE:
      result = less || equal;
      break;
    case TOKEN_GTE:
      result = !less;
      break;
    case TOKEN_EQ:
      result = equal;
      break;
    default:
      result = !equal;
      break;
    }
    return constant_of(convert_value((unsigned long long)result, PRIM_INT));
  }

  if (is_unsigned(type)) {
    unsigned long long a = l.bits;
    unsigned long long b = r.bits;
    unsigned long long result;
    switch (op) {
    case TOKEN_PLUS:
      result = a + b;
      break;
    case TOKEN_MINUS:
      result = a - b;
      break;
    case TOKEN_STAR:
      result = a * b;
      break;
    case TOKEN_SLASH:
    case TOKEN_PERCENT:
      if (b == 0)
        return failed_constant(type, "division by zero in a constant expression", loc);
      result = op == TOKEN_SLASH ? a / b : a % b;
      break;
    case TOKEN_AMPERSAND:
      result = a & b;
      break;
    case TOKEN_PIPE:
      result = a | b;
      break;
    case TOKEN_CARET:
      result = a ^ b;
      break;
    default:
      return non_constant();
    }
    return constant_of(convert_value(result, type));
  }

  long long a = (long long)l.bits;
  long long b = (long long)r.bits;
  long long result;
  switch (op) {
  case TOKEN_PLUS:
    if (add_overflows(a, b))
      return failed_constant(type, "overflow in a constant expression", loc);
    result = a + b;
    break;
  case TOKEN_MINUS:
    if (sub_overflows(a, b))
      return failed_constant(type, "overflow in a constant expression", loc);
    result = a - b;
    break;
  case TOKEN_STAR:
    if (mul_overflows(a, b))
      return failed_constant(type, "overflow in a constant expression", loc);
    result = a * b;
    break;
  case TOKEN_SLASH:
  case TOKEN_PERCENT:
    if (b == 0)
      return failed_constant(type, "division by zero in a constant expression", loc);
    if (a == LLONG_MIN && b == -1)
      return failed_constant(type, "overflow in a constant expression", loc);
    result = op == TOKEN_SLASH ? a / b : a % b;
    break;
  case TOKEN_AMPERSAND:
    result = a & b;
    break;
  case TOKEN_PIPE:
    result = a | b;
    break;
  case TOKEN_CARET:
    result = a ^ b;
    break;
  default:
    return non_constant();
  }
  if (!value_fits((int_value){PRIM_LLONG, (unsigned long long)result}, type))
    return failed_constant(type, "overflow in a constant expression", loc);
  return constant_of(convert_value((unsigned long long)result, type));
}

static constant evaluate_shift(sema *s, ast_node *node, constant a, constant b, int evaluated) {
  (void)s;
  prim_kind type = promote(a.value.type);
  constant propagated;
  if (propagate(&propagated, a, b, type))
    return propagated;
  if (!evaluated)
    return unknown_constant(type);

  int_value l = convert_value(a.value.bits, type);
  unsigned long long count = b.value.bits;
  int negative = !is_unsigned(b.value.type) && (long long)b.value.bits < 0;
  if (negative || count >= (unsigned long long)integer_width(type))
    return failed_constant(type, "shift count is out of range", node->loc);

  if (node->binary_op.op.type == TOKEN_RSHIFT) {
    unsigned long long result =
        is_unsigned(type) ? l.bits >> count : (unsigned long long)((long long)l.bits >> count);
    return constant_of(convert_value(result, type));
  }
  if (is_unsigned(type))
    return constant_of(convert_value(l.bits << count, type));
  if ((long long)l.bits < 0)
    return failed_constant(type, "left shift of a negative value", node->loc);
  if ((long long)l.bits > (LLONG_MAX >> count))
    return failed_constant(type, "overflow in a constant expression", node->loc);
  long long result = (long long)l.bits << count;
  if (!value_fits((int_value){PRIM_LLONG, (unsigned long long)result}, type))
    return failed_constant(type, "overflow in a constant expression", node->loc);
  return constant_of(convert_value((unsigned long long)result, type));
}

static constant evaluate_logical(sema *s, ast_node *node, int evaluated) {
  int is_and = node->binary_op.op.type == TOKEN_AND;
  constant a = evaluate(s, node->binary_op.left, evaluated);
  if (a.status == CONST_NOT_CONSTANT)
    return a;
  int decided = a.status == CONST_VALUE && (is_and ? a.value.bits == 0 : a.value.bits != 0);
  constant b = evaluate(s, node->binary_op.right, evaluated && !decided);
  if (b.status == CONST_NOT_CONSTANT)
    return b;
  if (decided)
    return constant_of(convert_value(is_and ? 0 : 1, PRIM_INT));
  if (a.status != CONST_VALUE) {
    a.value.type = PRIM_INT;
    return a;
  }
  if (b.status != CONST_VALUE) {
    b.value.type = PRIM_INT;
    return b;
  }
  int result =
      is_and ? (a.value.bits != 0 && b.value.bits != 0) : (a.value.bits != 0 || b.value.bits != 0);
  return constant_of(convert_value((unsigned long long)result, PRIM_INT));
}

static constant evaluate_binary(sema *s, ast_node *node, int evaluated) {
  token_type op = node->binary_op.op.type;
  if (op == TOKEN_AND || op == TOKEN_OR)
    return evaluate_logical(s, node, evaluated);
  if (op == TOKEN_COMMA)
    return non_constant();

  constant a = evaluate(s, node->binary_op.left, evaluated);
  constant b = evaluate(s, node->binary_op.right, evaluated);
  if (op == TOKEN_LSHIFT || op == TOKEN_RSHIFT)
    return evaluate_shift(s, node, a, b, evaluated);

  prim_kind type = usual_conversions(a.value.type, b.value.type);
  prim_kind result_type = is_comparison(op) ? PRIM_INT : type;
  constant propagated;
  if (propagate(&propagated, a, b, result_type))
    return propagated;
  if (!evaluated)
    return unknown_constant(result_type);
  return evaluate_arithmetic(op, convert_value(a.value.bits, type),
                             convert_value(b.value.bits, type), node->loc);
}

static constant evaluate_ternary(sema *s, ast_node *node, int evaluated) {
  constant condition = evaluate(s, node->ternary.condition, evaluated);
  if (condition.status == CONST_NOT_CONSTANT)
    return condition;
  int taken = condition.status == CONST_VALUE ? condition.value.bits != 0 : -1;
  constant a = evaluate(s, node->ternary.true_branch, evaluated && taken == 1);
  constant b = evaluate(s, node->ternary.false_branch, evaluated && taken == 0);
  if (a.status == CONST_NOT_CONSTANT)
    return a;
  if (b.status == CONST_NOT_CONSTANT)
    return b;

  prim_kind type = usual_conversions(a.value.type, b.value.type);
  if (condition.status != CONST_VALUE) {
    condition.value.type = type;
    return condition;
  }
  constant chosen = taken ? a : b;
  if (chosen.status != CONST_VALUE) {
    chosen.value.type = type;
    return chosen;
  }
  return constant_of(convert_value(chosen.value.bits, type));
}

static constant evaluate_cast(sema *s, ast_node *node, int evaluated) {
  ast_node *operand = node->cast_expr.operand;
  if (!operand)
    return non_constant();
  if (is_unresolved(resolved_type(node->cast_expr.type)))
    return unknown_constant(PRIM_INT);
  prim_kind target = integer_type_of(node->cast_expr.type);
  if (target == PRIM_NONE)
    return non_constant();
  if (operand->type == AST_NODE_TYPE_NUMBER && is_floating_constant(operand)) {
    if (!evaluated)
      return unknown_constant(target);
    return evaluate_floating_cast(operand, target, node->loc);
  }
  constant value = evaluate(s, operand, evaluated);
  if (value.status != CONST_VALUE) {
    if (value.status == CONST_NO_VALUE)
      value.value.type = target;
    return value;
  }
  return constant_of(convert_value(value.value.bits, target));
}

static type_info *sizeof_operand_type(ast_node *node) {
  ast_node *operand = node->unary_op.operand;
  if (!operand)
    return NULL;
  if (operand->type == AST_NODE_TYPE_CAST && operand->cast_expr.operand == NULL)
    return operand->cast_expr.type;
  return operand->expr_type;
}

static constant evaluate_sizeof(ast_node *node) {
  return evaluate_sizeof_type(sizeof_operand_type(node));
}

static constant evaluate_unary(sema *s, ast_node *node, int evaluated) {
  token_type op = node->unary_op.op.type;
  if (op == TOKEN_SIZEOF)
    return evaluate_sizeof(node);
  if (op != TOKEN_PLUS && op != TOKEN_MINUS && op != TOKEN_TILDE && op != TOKEN_NOT)
    return non_constant();

  constant operand = evaluate(s, node->unary_op.operand, evaluated);
  if (operand.status == CONST_NOT_CONSTANT)
    return operand;
  if (op == TOKEN_NOT) {
    if (operand.status != CONST_VALUE) {
      operand.value.type = PRIM_INT;
      return operand;
    }
    return constant_of(convert_value(operand.value.bits == 0, PRIM_INT));
  }

  prim_kind type = promote(operand.value.type);
  if (operand.status != CONST_VALUE) {
    operand.value.type = type;
    return operand;
  }
  int_value v = convert_value(operand.value.bits, type);
  if (op == TOKEN_PLUS)
    return constant_of(v);
  if (op == TOKEN_TILDE)
    return constant_of(convert_value(~v.bits, type));
  if (is_unsigned(type))
    return constant_of(convert_value(0ull - v.bits, type));
  if ((long long)v.bits == LLONG_MIN)
    return failed_constant(type, "overflow in a constant expression", node->loc);
  unsigned long long negated = (unsigned long long)(-(long long)v.bits);
  if (!value_fits((int_value){PRIM_LLONG, negated}, type))
    return failed_constant(type, "overflow in a constant expression", node->loc);
  return constant_of(convert_value(negated, type));
}

static constant evaluate(sema *s, ast_node *node, int evaluated) {
  if (!node)
    return non_constant();
  switch (node->type) {
  case AST_NODE_TYPE_NUMBER:
    if (is_floating_constant(node))
      return non_constant();
    return evaluate_integer_constant(node);
  case AST_NODE_TYPE_CHAR_LITERAL:
    return evaluate_char_constant(node);
  case AST_NODE_TYPE_IDENTIFIER:
    if (!node->symbol)
      return unknown_constant(PRIM_INT);
    if (node->symbol->kind != SYMBOL_ENUM_CONSTANT)
      return non_constant();
    if (!node->symbol->has_value)
      return unknown_constant(PRIM_INT);
    return constant_of(convert_value((unsigned long long)node->symbol->value, PRIM_INT));
  case AST_NODE_TYPE_UNARY_OP:
    return evaluate_unary(s, node, evaluated);
  case AST_NODE_TYPE_BINARY_OP:
    return evaluate_binary(s, node, evaluated);
  case AST_NODE_TYPE_TERNARY:
    return evaluate_ternary(s, node, evaluated);
  case AST_NODE_TYPE_CAST:
    return evaluate_cast(s, node, evaluated);
  default:
    return non_constant();
  }
}

static int qualifiers_of(type_info *type) {
  return (type->is_const ? 1 : 0) | (type->is_volatile ? 2 : 0) | (type->is_restrict ? 4 : 0);
}

static type_info *strip_typedefs(type_info *type, int *qualifiers) {
  *qualifiers = 0;
  while (type && type->kind == TYPE_TYPEDEF && type->symbol) {
    *qualifiers |= qualifiers_of(type);
    type = type->symbol->type;
  }
  return type;
}

static int is_unknown(type_info *type) {
  return !type || type->kind == TYPE_UNKNOWN || is_unresolved(type);
}

static int compatible_functions(type_info *a, type_info *b);

static int compatible_types(type_info *a, int a_inherited, type_info *b, int b_inherited) {
  int a_qualifiers;
  int b_qualifiers;
  a = strip_typedefs(a, &a_qualifiers);
  b = strip_typedefs(b, &b_qualifiers);
  if (is_unknown(a) || is_unknown(b))
    return 1;
  a_qualifiers |= a_inherited;
  b_qualifiers |= b_inherited;
  if (a->kind == TYPE_ARRAY && b->kind == TYPE_ARRAY) {
    if (a->array_size >= 0 && b->array_size >= 0 && a->array_size != b->array_size)
      return 0;
    return compatible_types(a->ptr_to, a_qualifiers, b->ptr_to, b_qualifiers);
  }
  if ((a_qualifiers | qualifiers_of(a)) != (b_qualifiers | qualifiers_of(b)))
    return 0;
  if (a->kind == TYPE_ENUM && b->kind == TYPE_PRIMITIVE)
    return !b->is_complex && !b->is_imaginary && b->prim == a->symbol->enum_type;
  if (a->kind == TYPE_PRIMITIVE && b->kind == TYPE_ENUM)
    return !a->is_complex && !a->is_imaginary && a->prim == b->symbol->enum_type;
  if (a->kind != b->kind)
    return 0;
  switch (a->kind) {
  case TYPE_PRIMITIVE:
    return a->prim == b->prim && a->is_complex == b->is_complex &&
           a->is_imaginary == b->is_imaginary;
  case TYPE_POINTER:
    return compatible_types(a->ptr_to, 0, b->ptr_to, 0);
  case TYPE_FUNCTION:
    return compatible_functions(a, b);
  default:
    return a->symbol == b->symbol;
  }
}

static type_info *parameter_pointee(type_info *type, int *inherited) {
  if (type->kind == TYPE_ARRAY)
    return type->ptr_to;
  *inherited = 0;
  if (type->kind == TYPE_POINTER)
    return type->ptr_to;
  return type->kind == TYPE_FUNCTION ? type : NULL;
}

static int compatible_unqualified(type_info *a, type_info *b) {
  int ignored;
  type_info *a_type = strip_typedefs(a, &ignored);
  type_info *b_type = strip_typedefs(b, &ignored);
  if (is_unknown(a_type) || is_unknown(b_type) || a_type->kind == TYPE_ARRAY ||
      b_type->kind == TYPE_ARRAY)
    return compatible_types(a, 0, b, 0);
  type_info a_plain = *a_type;
  type_info b_plain = *b_type;
  a_plain.is_const = a_plain.is_volatile = a_plain.is_restrict = 0;
  b_plain.is_const = b_plain.is_volatile = b_plain.is_restrict = 0;
  return compatible_types(&a_plain, 0, &b_plain, 0);
}

static int compatible_parameters(type_info *a, type_info *b) {
  int a_qualifiers;
  int b_qualifiers;
  type_info *a_type = strip_typedefs(a, &a_qualifiers);
  type_info *b_type = strip_typedefs(b, &b_qualifiers);
  type_info *a_pointee = parameter_pointee(a_type, &a_qualifiers);
  type_info *b_pointee = parameter_pointee(b_type, &b_qualifiers);
  if (a_pointee && b_pointee)
    return compatible_types(a_pointee, a_qualifiers, b_pointee, b_qualifiers);
  return compatible_unqualified(a_type, b_type);
}

static int survives_promotion(type_info *type) {
  type_info *t = resolved_type(type);
  if (!t || t->kind != TYPE_PRIMITIVE || t->is_complex || t->is_imaginary)
    return 1;
  if (t->prim == PRIM_FLOAT)
    return 0;
  return integer_rank(t->prim) == 0 || integer_rank(t->prim) >= integer_rank(PRIM_INT);
}

static int compatible_with_old_parameter(type_info *parameter, type_info *old) {
  if (survives_promotion(old))
    return compatible_parameters(parameter, old);
  type_info promoted = {0};
  promoted.kind = TYPE_PRIMITIVE;
  promoted.prim = resolved_type(old)->prim == PRIM_FLOAT ? PRIM_DOUBLE : PRIM_INT;
  return compatible_parameters(parameter, &promoted);
}

static int compatible_functions(type_info *a, type_info *b) {
  if (!compatible_types(a->ptr_to, 0, b->ptr_to, 0))
    return 0;
  if (!a->has_prototype && !b->has_prototype)
    return 1;
  if (a->has_prototype && b->has_prototype) {
    if (a->param_count != b->param_count || a->is_variadic != b->is_variadic)
      return 0;
    for (int i = 0; i < a->param_count; i++) {
      if (!compatible_parameters(a->param_types[i], b->param_types[i]))
        return 0;
    }
    return 1;
  }
  type_info *prototype = a->has_prototype ? a : b;
  type_info *old = a->has_prototype ? b : a;
  if (prototype->is_variadic)
    return 0;
  if (old->param_count == 0) {
    for (int i = 0; i < prototype->param_count; i++) {
      if (!survives_promotion(prototype->param_types[i]))
        return 0;
    }
    return 1;
  }
  if (old->param_count != prototype->param_count)
    return 0;
  for (int i = 0; i < prototype->param_count; i++) {
    if (!compatible_with_old_parameter(prototype->param_types[i], old->param_types[i]))
      return 0;
  }
  return 1;
}

static int is_incomplete(type_info *type) {
  type_info *t = resolved_type(type);
  if (is_unknown(t))
    return 0;
  switch (t->kind) {
  case TYPE_PRIMITIVE:
    return t->prim == PRIM_VOID;
  case TYPE_ARRAY:
    return t->array_size < 0 && !t->array_size_expr;
  case TYPE_STRUCT:
  case TYPE_UNION:
    return !t->symbol->is_defined;
  default:
    return 0;
  }
}

static int is_variably_modified(type_info *type) {
  for (; type; type = type->ptr_to) {
    if (type->kind == TYPE_TYPEDEF)
      return type->symbol && is_variably_modified(type->symbol->type);
    if (type->kind == TYPE_ARRAY && type->is_vla)
      return 1;
  }
  return 0;
}

static type_info *copy_type(sema *s, const type_info *from) {
  type_info **grown =
      (type_info **)realloc(s->program->program.derived_types,
                            sizeof(type_info *) * (size_t)(s->program->program.derived_count + 1));
  if (!grown)
    return NULL;
  s->program->program.derived_types = grown;
  type_info *type = (type_info *)malloc(sizeof(type_info));
  if (!type)
    return NULL;
  *type = *from;
  grown[s->program->program.derived_count++] = type;
  return type;
}

static type_info *pointer_to(sema *s, type_info *target) {
  type_info pointer = {0};
  pointer.kind = TYPE_POINTER;
  pointer.array_size = -1;
  pointer.ptr_to = target;
  return copy_type(s, &pointer);
}

static type_info *qualified(sema *s, type_info *type, int qualifiers) {
  int inherited;
  type_info *t = strip_typedefs(type, &inherited);
  if (is_unknown(t))
    return t;
  if (t->kind == TYPE_ARRAY) {
    type_info *element = qualified(s, t->ptr_to, inherited | qualifiers);
    if (element == t->ptr_to)
      return t;
    type_info *copy = copy_type(s, t);
    if (copy)
      copy->ptr_to = element;
    return copy;
  }
  int wanted = inherited | qualifiers_of(t) | qualifiers;
  if (wanted == qualifiers_of(t))
    return t;
  type_info *copy = copy_type(s, t);
  if (copy) {
    copy->is_const = (wanted & 1) != 0;
    copy->is_volatile = (wanted & 2) != 0;
    copy->is_restrict = (wanted & 4) != 0;
  }
  return copy;
}

static type_info *unqualified(sema *s, type_info *type) {
  type_info *t = resolved_type(type);
  if (is_unknown(t) || qualifiers_of(t) == 0)
    return t;
  type_info *copy = copy_type(s, t);
  if (copy)
    copy->is_const = copy->is_volatile = copy->is_restrict = 0;
  return copy;
}

static type_info *adjusted_parameter_type(sema *s, type_info *type) {
  int inherited;
  type_info *t = strip_typedefs(type, &inherited);
  if (is_unknown(t))
    return type;
  if (t->kind == TYPE_FUNCTION)
    return pointer_to(s, t);
  if (t->kind != TYPE_ARRAY)
    return type;
  type_info *pointer = pointer_to(s, qualified(s, t->ptr_to, inherited));
  if (pointer) {
    pointer->is_const = t->is_const;
    pointer->is_volatile = t->is_volatile;
    pointer->is_restrict = t->is_restrict;
  }
  return pointer;
}

static linkage_kind linkage_of(sema *s, const char *name, symbol_kind kind, token_type storage) {
  if (kind != SYMBOL_VAR && kind != SYMBOL_FUNC)
    return LINKAGE_NONE;
  int at_file_scope = s->table->current_scope->kind == SCOPE_FILE;
  if (storage == TOKEN_STATIC && at_file_scope)
    return LINKAGE_INTERNAL;
  if (storage == TOKEN_EXTERN || kind == SYMBOL_FUNC) {
    symbol *prior = symbol_table_lookup_ordinary(s->table, name);
    if (prior && prior->linkage != LINKAGE_NONE)
      return prior->linkage;
    return LINKAGE_EXTERNAL;
  }
  return at_file_scope ? LINKAGE_EXTERNAL : LINKAGE_NONE;
}

static void check_storage_class(sema *s, const char *name, symbol_kind kind, token_type storage,
                                source_loc loc) {
  int at_file_scope = s->table->current_scope->kind == SCOPE_FILE;
  if (at_file_scope && (storage == TOKEN_AUTO || storage == TOKEN_REGISTER))
    report(s, loc, "invalid storage class at file scope for", name);
  else if (!at_file_scope && kind == SYMBOL_FUNC && storage != 0 && storage != TOKEN_EXTERN)
    report(s, loc, "invalid storage class for block-scope function", name);
}

static void check_variably_modified(sema *s, ast_node *decl, linkage_kind linkage) {
  const char *name = decl->var_decl.var_name;
  type_info *type = resolved_type(decl->var_decl.type);
  if (!is_variably_modified(decl->var_decl.type))
    return;
  if (s->table->current_scope->kind == SCOPE_FILE)
    report(s, decl->loc, "variably modified type at file scope for", name);
  else if (linkage != LINKAGE_NONE)
    report(s, decl->loc, "variably modified type with linkage for", name);
  else if (decl->var_decl.specs.storage_class == TOKEN_STATIC && type->kind == TYPE_ARRAY &&
           type->is_vla)
    report(s, decl->loc, "variable length array with static storage duration for", name);
}

static symbol *linked_symbol(sema *s, const char *name) {
  for (symbol *sym = s->table->all_symbols; sym; sym = sym->all_next) {
    if (sym->linkage != LINKAGE_NONE && strcmp(sym->name, name) == 0)
      return sym;
  }
  return NULL;
}

static int check_redeclaration(sema *s, symbol *prior, const char *name, symbol_kind kind,
                               type_info *type, linkage_kind linkage, source_loc loc) {
  const char *problem = NULL;
  if (prior->kind != kind)
    problem = "conflicting kind of symbol for";
  else if (prior->linkage != linkage)
    problem = "conflicting linkage for";
  else if (!compatible_types(prior->type, 0, type, 0))
    problem = "conflicting types for";
  if (!problem)
    return 1;
  report(s, loc, problem, name);
  note_previous(prior->loc, name);
  return 0;
}

static int completes(type_info *prior, type_info *type) {
  prior = resolved_type(prior);
  type = resolved_type(type);
  if (is_unknown(prior) || is_unknown(type))
    return 0;
  if (prior->kind == TYPE_ARRAY)
    return prior->array_size < 0 && type->array_size >= 0;
  return prior->kind == TYPE_FUNCTION && !prior->has_prototype && type->has_prototype;
}

static symbol *declare(sema *s, const char *name, symbol_kind kind, type_info *type,
                       linkage_kind linkage, source_loc loc) {
  symbol *prior = symbol_table_lookup_ordinary_current(s->table, name);
  if (prior && prior->linkage != LINKAGE_NONE && linkage != LINKAGE_NONE) {
    if (check_redeclaration(s, prior, name, kind, type, linkage, loc) &&
        completes(prior->type, type))
      prior->type = type;
    return prior;
  }
  if (prior) {
    report(s, loc, "redeclaration of", name);
    note_previous(prior->loc, name);
  } else if (linkage != LINKAGE_NONE) {
    symbol *linked = linked_symbol(s, name);
    if (linked)
      check_redeclaration(s, linked, name, kind, type, linkage, loc);
  }
  symbol *sym = symbol_table_insert_ordinary(s->table, name, kind, type, loc);
  if (sym)
    sym->linkage = linkage;
  return sym;
}

static void record_definition(sema *s, symbol *sym, ast_node *definition, const char *name) {
  if (sym->is_defined) {
    report(s, definition->loc, "redefinition of", name);
    note_previous(sym->definition->loc, name);
    return;
  }
  sym->is_defined = 1;
  sym->definition = definition;
}

static void record_tentative_definition(sema *s, symbol *sym, ast_node *decl) {
  if (sym->linkage == LINKAGE_INTERNAL && is_incomplete(decl->var_decl.type))
    report(s, decl->loc, "incomplete type for tentative definition with internal linkage",
           decl->var_decl.var_name);
  sym->is_tentative = 1;
  if (!sym->definition)
    sym->definition = decl;
}

static void check_tentative_definitions(sema *s, ast_node *program) {
  for (int i = 0; i < program->program.count; i++) {
    ast_node *item = program->program.declarations[i];
    int count = item->type == AST_NODE_TYPE_DECL_GROUP ? item->block.count : 1;
    for (int j = 0; j < count; j++) {
      ast_node *decl = item->type == AST_NODE_TYPE_DECL_GROUP ? item->block.statements[j] : item;
      symbol *sym = decl->symbol;
      if (!sym || sym->definition != decl || sym->is_defined || sym->linkage != LINKAGE_EXTERNAL)
        continue;
      if (is_incomplete(sym->type) && resolved_type(sym->type)->kind != TYPE_ARRAY)
        report(s, decl->loc, "incomplete type for tentative definition", sym->name);
    }
  }
}

static int is_unqualified_void(type_info *type) {
  int qualifiers;
  type_info *t = strip_typedefs(type, &qualifiers);
  return t && t->kind == TYPE_PRIMITIVE && t->prim == PRIM_VOID &&
         (qualifiers | qualifiers_of(t)) == 0;
}

static void remove_void_parameter(sema *s, type_info *fn) {
  if (fn->param_count != 1 || fn->param_names[0] || fn->is_variadic)
    return;
  type_info *param = fn->param_types[0];
  if (param->kind != TYPE_TYPEDEF || qualifiers_of(param))
    return;
  symbol *sym = symbol_table_lookup_ordinary(s->table, param->tag_name);
  if (!sym || sym->kind != SYMBOL_TYPEDEF || !is_unqualified_void(sym->type))
    return;
  free_type_info(fn->param_types[0]);
  free(fn->param_types);
  free(fn->param_names);
  free(fn->param_definitions);
  fn->param_types = NULL;
  fn->param_names = NULL;
  fn->param_definitions = NULL;
  fn->param_count = 0;
}

static void declare_parameters(sema *s, type_info *fn, source_loc loc, symbol **out) {
  remove_void_parameter(s, fn);
  for (int i = 0; i < fn->param_count; i++) {
    if (fn->param_definitions)
      resolve(s, fn->param_definitions[i]);
    resolve_type(s, fn->param_types[i], loc);
    if (!fn->param_names[i])
      continue;
    symbol *sym = declare(s, fn->param_names[i], SYMBOL_VAR,
                          adjusted_parameter_type(s, fn->param_types[i]), LINKAGE_NONE, loc);
    if (out)
      out[i] = sym;
  }
}

static void check_definition_parameters(sema *s, ast_node *fn) {
  type_info *type = fn->function_def.type;
  for (int i = 0; i < type->param_count; i++) {
    const char *name = type->param_names[i];
    if (!name) {
      report_at(s, fn->loc, "a parameter in a function definition needs a name");
      return;
    }
    if (is_incomplete(type->param_types[i]) &&
        resolved_type(type->param_types[i])->kind != TYPE_ARRAY)
      report(s, fn->loc, "incomplete type for parameter", name);
  }
}

static symbol *new_tag(sema *s, const char *name, type_kind kind, source_loc loc) {
  symbol *sym = symbol_table_insert_tag(s->table, name, NULL, loc);
  if (sym)
    sym->tag_kind = kind;
  return sym;
}

static symbol *tag_in_current_scope(sema *s, const char *name, type_kind kind, source_loc loc) {
  symbol *prior = name ? symbol_table_lookup_tag_current(s->table, name) : NULL;
  if (prior && prior->tag_kind == kind)
    return prior;
  if (prior) {
    report(s, loc, "wrong kind of tag", name);
    note_previous(prior->loc, name);
  }
  return new_tag(s, name, kind, loc);
}

static symbol *define_tag(sema *s, ast_node *def, const char *name, type_kind kind) {
  symbol *sym = tag_in_current_scope(s, name, kind, def->loc);
  if (sym && sym->definition) {
    report(s, def->loc, "redefinition of", name);
    note_previous(sym->definition->loc, name);
    sym = new_tag(s, name, kind, def->loc);
  }
  if (sym)
    sym->definition = def;
  return sym;
}

static void resolve_tag_reference(sema *s, type_info *type, source_loc loc) {
  if (type->definition) {
    type->symbol = type->definition->symbol;
    return;
  }
  if (!type->tag_name)
    return;
  symbol *prior = symbol_table_lookup_tag(s->table, type->tag_name);
  if (prior && prior->tag_kind != type->kind) {
    report(s, loc, "wrong kind of tag", type->tag_name);
    note_previous(prior->loc, type->tag_name);
  } else if (type->kind == TYPE_ENUM && (!prior || !prior->is_defined)) {
    report(s, loc, "use of incomplete enum", type->tag_name);
  } else {
    type->symbol = prior ? prior : new_tag(s, type->tag_name, type->kind, loc);
  }
}

static void resolve_typedef_name(sema *s, type_info *type, source_loc loc) {
  symbol *sym = symbol_table_lookup_ordinary(s->table, type->tag_name);
  if (!sym) {
    report(s, loc, "unknown type name", type->tag_name);
  } else if (sym->kind != SYMBOL_TYPEDEF) {
    report(s, loc, "expected a type name, found", type->tag_name);
    note_previous(sym->loc, sym->name);
  } else {
    type->symbol = sym;
  }
}

static void evaluate_array_size(sema *s, type_info *type, source_loc loc) {
  type_info *size_type = type->array_size_expr->expr_type;
  if (size_type && integer_type_of(size_type) == PRIM_NONE) {
    report_at(s, loc, "an array size needs an integer type");
    return;
  }
  constant size = evaluate(s, type->array_size_expr, 1);
  report_problem(s, size);
  if (size.status == CONST_NOT_CONSTANT) {
    type->is_vla = 1;
    return;
  }
  if (size.status != CONST_VALUE)
    return;
  if (!is_unsigned(size.value.type) && (long long)size.value.bits <= 0) {
    report_at(s, loc, "array size must be greater than zero");
    return;
  }
  if (size.value.bits == 0) {
    report_at(s, loc, "array size must be greater than zero");
    return;
  }
  if (size.value.bits > (unsigned long long)LLONG_MAX) {
    report_at(s, loc, "array is too large");
    return;
  }
  type->array_size = (long long)size.value.bits;
}

static int restrict_allowed(type_info *type) {
  if (type->kind == TYPE_ARRAY)
    return 1;
  type_info *t = resolved_type(type);
  while (t && t->kind == TYPE_ARRAY)
    t = resolved_type(t->ptr_to);
  if (is_unknown(t))
    return 1;
  if (t->kind != TYPE_POINTER)
    return 0;
  type_info *target = resolved_type(t->ptr_to);
  return is_unknown(target) || target->kind != TYPE_FUNCTION;
}

static void check_function_result(sema *s, type_info *function, source_loc loc) {
  type_info *result = resolved_type(function->ptr_to);
  if (!is_unknown(result) && (result->kind == TYPE_ARRAY || result->kind == TYPE_FUNCTION))
    report_at(s, loc, "a function cannot return an array or a function");
}

static void check_declarator(sema *s, type_info *type, source_loc loc) {
  for (type_info *t = type; t; t = t->ptr_to) {
    if (t->is_restrict && !restrict_allowed(t))
      report_at(s, loc, "restrict needs a pointer to an object or incomplete type");
    if (t->kind == TYPE_FUNCTION) {
      check_function_result(s, t, loc);
    } else if (t->kind == TYPE_ARRAY) {
      type_info *element = resolved_type(t->ptr_to);
      if (is_unknown(element))
        continue;
      if (element->kind == TYPE_FUNCTION)
        report_at(s, loc, "an array cannot have elements of function type");
      else if (is_incomplete(element))
        report_at(s, loc, "an array needs a complete element type");
      else if ((element->kind == TYPE_STRUCT || element->kind == TYPE_UNION) &&
               element->symbol->has_flexible_member)
        report_at(s, loc, "an array cannot have elements with a flexible array member");
    }
  }
}

static void resolve_type(sema *s, type_info *type, source_loc loc) {
  type_info *declarator = type;
  for (; type; type = type->ptr_to) {
    resolve(s, type->array_size_expr);
    if (type->kind == TYPE_ARRAY && type->array_size_expr)
      evaluate_array_size(s, type, loc);
    if (type->array_star)
      type->is_vla = 1;
    switch (type->kind) {
    case TYPE_TYPEDEF:
      resolve_typedef_name(s, type, loc);
      break;
    case TYPE_STRUCT:
    case TYPE_UNION:
    case TYPE_ENUM:
      resolve_tag_reference(s, type, loc);
      break;
    case TYPE_FUNCTION:
      symbol_table_enter_scope(s->table, SCOPE_PROTOTYPE);
      declare_parameters(s, type, loc, NULL);
      symbol_table_leave_scope(s->table);
      break;
    default:
      break;
    }
  }
  check_declarator(s, declarator, loc);
}

static void declare_label(sema *s, ast_node *label) {
  const char *name = label->label_stmt.label_name;
  symbol *prior = symbol_table_lookup_label(s->table, name);
  if (prior) {
    report(s, label->loc, "duplicate label", name);
    note_previous(prior->loc, name);
  }
  label->symbol = symbol_table_insert_label(s->table, name, label->loc);
}

static void record_goto(sema *s, ast_node *jump) {
  ast_node **grown = (ast_node **)realloc(s->gotos, sizeof(ast_node *) * (s->goto_count + 1));
  if (!grown)
    return;
  s->gotos = grown;
  s->gotos[s->goto_count++] = jump;
}

static void resolve_gotos(sema *s) {
  for (int i = 0; i < s->goto_count; i++) {
    ast_node *jump = s->gotos[i];
    jump->symbol = symbol_table_lookup_label(s->table, jump->goto_stmt.label_name);
    if (!jump->symbol)
      report(s, jump->loc, "use of undeclared label", jump->goto_stmt.label_name);
  }
  s->goto_count = 0;
}

static void resolve_items(sema *s, ast_node *list) {
  for (int i = 0; i < list->block.count; i++)
    resolve(s, list->block.statements[i]);
}

static void check_for_declaration(sema *s, ast_node *init) {
  if (!init || (init->type != AST_NODE_TYPE_VAR_DECL && init->type != AST_NODE_TYPE_DECL_GROUP &&
                init->type != AST_NODE_TYPE_STRUCT_DEF && init->type != AST_NODE_TYPE_ENUM_DEF))
    return;
  int count = init->type == AST_NODE_TYPE_DECL_GROUP ? init->block.count : 1;
  for (int i = 0; i < count; i++) {
    ast_node *item = init->type == AST_NODE_TYPE_DECL_GROUP ? init->block.statements[i] : init;
    if (item->type != AST_NODE_TYPE_VAR_DECL) {
      report_at(s, item->loc, "a for loop declaration may only declare automatic objects");
      continue;
    }
    token_type storage = item->var_decl.specs.storage_class;
    if ((storage != 0 && storage != TOKEN_AUTO && storage != TOKEN_REGISTER) ||
        (item->symbol && item->symbol->kind != SYMBOL_VAR))
      report(s, item->loc, "a for loop declaration may only declare automatic objects, not",
             item->var_decl.var_name);
  }
}

static void resolve_in_block(sema *s, ast_node *node) {
  symbol_table_enter_scope(s->table, SCOPE_BLOCK);
  resolve(s, node);
  symbol_table_leave_scope(s->table);
}

static int is_modifiable_type(type_info *type) {
  int qualifiers;
  type_info *t = strip_typedefs(type, &qualifiers);
  while (t && t->kind == TYPE_ARRAY) {
    int element_qualifiers;
    t = strip_typedefs(t->ptr_to, &element_qualifiers);
    qualifiers |= element_qualifiers;
  }
  return !t || !((qualifiers | qualifiers_of(t)) & 1);
}

static void resolve_var_decl(sema *s, ast_node *decl) {
  resolve_type(s, decl->var_decl.type, decl->loc);
  const char *name = decl->var_decl.var_name;
  ast_node *init = decl->var_decl.init_value;
  if (!name)
    return;
  token_type storage = decl->var_decl.specs.storage_class;
  symbol_kind kind = SYMBOL_VAR;
  if (storage == TOKEN_TYPEDEF)
    kind = SYMBOL_TYPEDEF;
  else if (resolved_type(decl->var_decl.type)->kind == TYPE_FUNCTION)
    kind = SYMBOL_FUNC;
  check_storage_class(s, name, kind, storage, decl->loc);
  if (decl->var_decl.specs.is_inline && kind != SYMBOL_FUNC &&
      !is_unresolved(resolved_type(decl->var_decl.type)))
    report(s, decl->loc, "inline can only declare a function, not", name);
  linkage_kind linkage = linkage_of(s, name, kind, storage);
  decl->symbol = declare(s, name, kind, decl->var_decl.type, linkage, decl->loc);
  symbol *sym = decl->symbol;
  int at_file_scope = s->table->current_scope->kind == SCOPE_FILE;
  int is_object = kind == SYMBOL_VAR && sym && sym->kind == SYMBOL_VAR;
  if (is_object && storage == TOKEN_REGISTER)
    sym->is_register = 1;
  if (is_object && (at_file_scope || storage == TOKEN_STATIC || storage == TOKEN_EXTERN))
    sym->has_static_storage = 1;
  if (kind == SYMBOL_FUNC && sym && sym->kind == SYMBOL_FUNC &&
      (!decl->var_decl.specs.is_inline || storage == TOKEN_EXTERN))
    sym->external_declaration = 1;
  check_variably_modified(s, decl, linkage);
  if (s->inline_definition && kind == SYMBOL_VAR && storage == TOKEN_STATIC &&
      is_modifiable_type(decl->var_decl.type))
    report(s, decl->loc,
           "an inline definition with external linkage cannot define modifiable static object",
           name);
  int linked_in_block = linkage != LINKAGE_NONE && !at_file_scope;
  if (kind == SYMBOL_VAR && linked_in_block && init)
    report(s, decl->loc, "cannot initialize block-scope declaration with linkage", name);
  if (kind == SYMBOL_FUNC && init)
    report(s, decl->loc, "cannot initialize function", name);
  if (at_file_scope && is_object) {
    if (init)
      record_definition(s, sym, decl, name);
    else if (storage == 0 || storage == TOKEN_STATIC)
      record_tentative_definition(s, sym, decl);
  }
  resolve(s, init);
  if (is_object && init && !linked_in_block) {
    long long count =
        initialize_declared(s, decl->var_decl.type, init, sym->has_static_storage, decl->loc);
    type_info *sized = sized_array(s, decl->var_decl.type, count);
    type_info *current = resolved_type(sym->type);
    if (current && current->kind == TYPE_ARRAY && current->array_size < 0 && !current->is_vla)
      sym->type = sized;
  }
  if (is_object && linkage == LINKAGE_NONE && !init && is_incomplete(sym->type))
    report(s, decl->loc, "incomplete type for object", name);
}

static void resolve_enum(sema *s, ast_node *def) {
  def->symbol = define_tag(s, def, def->enum_def.tag_name, TYPE_ENUM);
  long long next = 0;
  int next_known = 1;
  int has_negative = 0;

  for (int i = 0; i < def->enum_def.enumerator_count; i++) {
    ast_node *expr = def->enum_def.values[i];
    const char *name = def->enum_def.enumerators[i];
    long long value = 0;
    int known = 0;

    resolve(s, expr);
    if (expr) {
      constant result = evaluate(s, expr, 1);
      report_problem(s, result);
      if (result.status == CONST_NOT_CONSTANT) {
        report(s, expr->loc, "enumerator value is not an integer constant expression", name);
      } else if (result.status == CONST_VALUE) {
        if (!value_fits(result.value, PRIM_INT)) {
          report(s, expr->loc, "enumerator value is not representable as an int", name);
        } else {
          value = (long long)convert_value(result.value.bits, PRIM_INT).bits;
          known = 1;
        }
      }
    } else if (next_known) {
      if (!value_fits((int_value){PRIM_LLONG, (unsigned long long)next}, PRIM_INT)) {
        report(s, def->loc, "overflow in enumeration values", name);
      } else {
        value = next;
        known = 1;
      }
    }

    symbol *sym = declare(s, name, SYMBOL_ENUM_CONSTANT, NULL, LINKAGE_NONE, def->loc);
    if (sym && known) {
      sym->has_value = 1;
      sym->value = value;
    }
    if (known && value < 0)
      has_negative = 1;
    next_known = known;
    next = known ? value + 1 : 0;
  }

  if (def->symbol) {
    def->symbol->enum_type = has_negative ? PRIM_INT : PRIM_UINT;
    def->symbol->is_defined = 1;
  }
}

static void resolve_member(sema *s, ast_node *member) {
  if (member->type == AST_NODE_TYPE_DECL_GROUP) {
    for (int i = 0; i < member->block.count; i++)
      resolve_member(s, member->block.statements[i]);
  } else if (member->type == AST_NODE_TYPE_VAR_DECL) {
    resolve_type(s, member->var_decl.type, member->loc);
    if (member->var_decl.var_name && is_variably_modified(member->var_decl.type))
      report(s, member->loc, "variably modified type for member", member->var_decl.var_name);
    resolve(s, member->var_decl.bitfield_width);
  } else {
    resolve(s, member);
  }
}

typedef struct record_layout {
  int is_union;
  long long end;
  long long size;
  int alignment;
  int unit_size;
  long long unit_offset;
  int unit_bits;
  int has_flexible_member;
} record_layout;

static long long align_to(long long offset, int alignment) {
  return (offset + alignment - 1) / alignment * alignment;
}

static void place(record_layout *layout, ast_node *member, long long size, int alignment) {
  long long offset = layout->is_union ? 0 : align_to(layout->end, alignment);
  member->var_decl.offset = offset;
  layout->end = offset + size;
  if (layout->end > layout->size)
    layout->size = layout->end;
  if (alignment > layout->alignment)
    layout->alignment = alignment;
}

static void report_bitfield(sema *s, ast_node *member, const char *problem) {
  print_location(member->loc);
  if (member->var_decl.var_name)
    fprintf(stderr, "error: %s for bit-field '%s'\n", problem, member->var_decl.var_name);
  else
    fprintf(stderr, "error: %s for an unnamed bit-field\n", problem);
  s->error_count++;
}

static int lay_out_bitfield(sema *s, record_layout *layout, ast_node *member) {
  if (is_unknown(resolved_type(member->var_decl.type)))
    return 0;
  prim_kind prim = integer_type_of(member->var_decl.type);
  if (prim == PRIM_NONE) {
    report_bitfield(s, member, "invalid type");
    return 0;
  }
  constant width = evaluate(s, member->var_decl.bitfield_width, 1);
  report_problem(s, width);
  if (width.status == CONST_NOT_CONSTANT) {
    report_bitfield(s, member, "width is not an integer constant expression");
    return 0;
  }
  if (width.status != CONST_VALUE)
    return 0;
  int unit = (int)primitive_size(prim);
  if (!is_unsigned(width.value.type) && (long long)width.value.bits < 0) {
    report_bitfield(s, member, "negative width");
    return 0;
  }
  if (width.value.bits > (unsigned long long)unit * 8) {
    report_bitfield(s, member, "width exceeds its type");
    return 0;
  }
  int bits = (int)width.value.bits;
  member->var_decl.bit_width = bits;
  if (bits == 0) {
    if (member->var_decl.var_name) {
      report_bitfield(s, member, "zero width");
      return 0;
    }
    if (layout->unit_size && !layout->is_union) {
      layout->end = align_to(layout->end, unit);
      if (unit > layout->alignment)
        layout->alignment = unit;
    }
    layout->unit_size = 0;
    return 1;
  }
  if (!layout->is_union && layout->unit_size == unit && layout->unit_bits + bits <= unit * 8) {
    member->var_decl.offset = layout->unit_offset;
    member->var_decl.bit_offset = layout->unit_bits;
    layout->unit_bits += bits;
    return 1;
  }
  place(layout, member, unit, unit);
  layout->unit_size = unit;
  layout->unit_offset = member->var_decl.offset;
  layout->unit_bits = bits;
  return 1;
}

static type_info *element_type(type_info *type) {
  type_info *t = resolved_type(type);
  while (t && t->kind == TYPE_ARRAY)
    t = resolved_type(t->ptr_to);
  return t;
}

static int lay_out_field(sema *s, record_layout *layout, ast_node *member, int is_last,
                         int named_before) {
  const char *name = member->var_decl.var_name;
  type_info *type = member->var_decl.type;
  type_info *t = resolved_type(type);
  type_info *element = element_type(type);
  layout->unit_size = 0;
  if (is_unknown(element))
    return 0;
  if (t->kind == TYPE_FUNCTION) {
    report(s, member->loc, "function type for member", name);
    return 0;
  }
  if (is_incomplete(element)) {
    if (t->kind != TYPE_ARRAY)
      report(s, member->loc, "incomplete type for member", name);
    return 0;
  }
  if ((element->kind == TYPE_STRUCT || element->kind == TYPE_UNION) &&
      element->symbol->has_flexible_member) {
    if (t->kind == TYPE_ARRAY)
      return 0;
    if (!layout->is_union) {
      report(s, member->loc, "structure with a flexible array member used for member", name);
      return 0;
    }
    layout->has_flexible_member = 1;
  }
  if (is_incomplete(t)) {
    const char *problem = NULL;
    if (layout->is_union)
      problem = "flexible array member in a union";
    else if (!is_last)
      problem = "flexible array member must be the last member, not";
    else if (!named_before)
      problem = "flexible array member without another named member";
    if (problem) {
      report(s, member->loc, problem, name);
      return 0;
    }
    layout->has_flexible_member = 1;
    type_layout flexible = layout_of(element);
    place(layout, member, 0, flexible.alignment);
    return flexible.known;
  }
  type_layout field = layout_of(type);
  if (!field.known)
    return 0;
  place(layout, member, field.size, field.alignment);
  return 1;
}

static int collect_members(ast_node *def, ast_node **out) {
  int count = 0;
  for (int i = 0; i < def->struct_def.member_count; i++) {
    ast_node *item = def->struct_def.members[i];
    int group = item->type == AST_NODE_TYPE_DECL_GROUP;
    for (int j = 0; j < (group ? item->block.count : 1); j++) {
      ast_node *member = group ? item->block.statements[j] : item;
      if (member->type != AST_NODE_TYPE_VAR_DECL)
        continue;
      if (out)
        out[count] = member;
      count++;
    }
  }
  return count;
}

static void lay_out_record(sema *s, ast_node *def) {
  int count = collect_members(def, NULL);
  ast_node **members = (ast_node **)malloc(sizeof(ast_node *) * (size_t)(count + 1));
  if (!members)
    return;
  collect_members(def, members);
  record_layout layout = {0};
  layout.is_union = def->struct_def.is_union;
  layout.alignment = 1;
  int known = 1;
  int named = 0;
  for (int i = 0; i < count; i++) {
    ast_node *member = members[i];
    const char *name = member->var_decl.var_name;
    for (int j = 0; name && j < i; j++) {
      if (members[j]->var_decl.var_name && strcmp(members[j]->var_decl.var_name, name) == 0) {
        report(s, member->loc, "duplicate member", name);
        note_previous(members[j]->loc, name);
        break;
      }
    }
    int placed = member->var_decl.bitfield_width
                     ? lay_out_bitfield(s, &layout, member)
                     : lay_out_field(s, &layout, member, i == count - 1, named);
    known = known && placed;
    if (name)
      named++;
  }
  if (named == 0)
    report_at(s, def->loc, "a structure or union needs a named member");
  if (def->symbol) {
    def->symbol->has_flexible_member = layout.has_flexible_member;
    if (known && named > 0) {
      def->symbol->alignment = layout.alignment;
      def->symbol->size = align_to(layout.size, layout.alignment);
    }
  }
  free(members);
}

static void resolve_struct(sema *s, ast_node *def) {
  type_kind kind = def->struct_def.is_union ? TYPE_UNION : TYPE_STRUCT;
  if (def->struct_def.is_forward) {
    def->symbol = tag_in_current_scope(s, def->struct_def.tag_name, kind, def->loc);
    return;
  }
  def->symbol = define_tag(s, def, def->struct_def.tag_name, kind);
  for (int i = 0; i < def->struct_def.member_count; i++)
    resolve_member(s, def->struct_def.members[i]);
  lay_out_record(s, def);
  if (def->symbol)
    def->symbol->is_defined = 1;
}

static void resolve_function(sema *s, ast_node *fn) {
  const char *name = fn->function_def.name;
  type_info *type = fn->function_def.type;
  resolve_type(s, type->ptr_to, fn->loc);
  check_storage_class(s, name, SYMBOL_FUNC, fn->function_def.specs.storage_class, fn->loc);
  for (int i = 0; i < type->param_count; i++) {
    for (type_info *t = type->param_types[i]; t; t = t->ptr_to) {
      if (t->kind == TYPE_ARRAY && t->array_star) {
        report(s, fn->loc, "[*] is only allowed in a function prototype, not in the definition of",
               name);
        break;
      }
    }
  }
  check_function_result(s, type, fn->loc);
  if (resolved_type(type->ptr_to)->prim != PRIM_VOID && is_incomplete(type->ptr_to))
    report(s, fn->loc, "incomplete return type in the definition of", name);
  linkage_kind linkage = linkage_of(s, name, SYMBOL_FUNC, fn->function_def.specs.storage_class);
  remove_void_parameter(s, type);
  fn->symbol = declare(s, name, SYMBOL_FUNC, type, linkage, fn->loc);
  if (fn->symbol && fn->symbol->kind == SYMBOL_FUNC) {
    record_definition(s, fn->symbol, fn, name);
    if (!fn->function_def.specs.is_inline || fn->function_def.specs.storage_class == TOKEN_EXTERN)
      fn->symbol->external_declaration = 1;
    s->inline_definition = linkage == LINKAGE_EXTERNAL && !fn->symbol->external_declaration;
  }

  symbol_table_enter_scope(s->table, SCOPE_FUNCTION);
  if (type->param_count > 0)
    fn->function_def.param_symbols = (symbol **)calloc(type->param_count, sizeof(symbol *));
  declare_parameters(s, type, fn->loc, fn->function_def.param_symbols);
  check_definition_parameters(s, fn);
  type_info *name_type = create_type_info(TYPE_ARRAY);
  name_type->array_size = (long long)strlen(name) + 1;
  name_type->ptr_to = create_type_info(TYPE_PRIMITIVE);
  name_type->ptr_to->prim = PRIM_CHAR;
  name_type->ptr_to->is_const = 1;
  fn->function_def.name_type = name_type;
  symbol *func =
      declare(s, "__func__", SYMBOL_VAR, name_type, LINKAGE_NONE, fn->function_def.body->loc);
  if (func)
    func->has_static_storage = 1;
  s->return_type = type->ptr_to;
  resolve_items(s, fn->function_def.body);
  resolve_gotos(s);
  s->inline_definition = 0;
  symbol_table_leave_scope(s->table);
}

static type_info *arithmetic_type(prim_kind prim, int is_complex) {
  static type_info types[PRIM_LDOUBLE + 1][2];
  type_info *type = &types[prim][is_complex ? 1 : 0];
  type->kind = TYPE_PRIMITIVE;
  type->prim = prim;
  type->is_complex = is_complex ? 1 : 0;
  type->array_size = -1;
  return type;
}

static int is_arithmetic(type_info *type) {
  type_info *t = resolved_type(type);
  if (integer_type_of(t) != PRIM_NONE)
    return 1;
  return t && t->kind == TYPE_PRIMITIVE &&
         (t->prim == PRIM_FLOAT || t->prim == PRIM_DOUBLE || t->prim == PRIM_LDOUBLE);
}

static int is_integer(type_info *type) {
  return integer_type_of(type) != PRIM_NONE;
}

static int is_real(type_info *type) {
  return is_arithmetic(type) && !resolved_type(type)->is_complex;
}

static int is_pointer(type_info *type) {
  type_info *t = resolved_type(type);
  return t && t->kind == TYPE_POINTER;
}

static int is_scalar(type_info *type) {
  return is_arithmetic(type) || is_pointer(type);
}

static int is_void(type_info *type) {
  type_info *t = resolved_type(type);
  return t && t->kind == TYPE_PRIMITIVE && t->prim == PRIM_VOID;
}

static type_info *pointee(type_info *pointer) {
  return resolved_type(pointer)->ptr_to;
}

static int points_to_function(type_info *pointer) {
  type_info *t = resolved_type(pointee(pointer));
  return t && t->kind == TYPE_FUNCTION;
}

static int points_to_object(type_info *pointer) {
  return is_pointer(pointer) && !points_to_function(pointer) && !is_incomplete(pointee(pointer));
}

static int pointee_qualifiers(type_info *pointer) {
  int qualifiers;
  type_info *t = strip_typedefs(pointee(pointer), &qualifiers);
  return t ? qualifiers | qualifiers_of(t) : qualifiers;
}

static type_info *value_type(sema *s, ast_node *expr) {
  int inherited;
  type_info *t = strip_typedefs(expr ? expr->expr_type : NULL, &inherited);
  if (is_unknown(t))
    return NULL;
  if (t->kind == TYPE_ARRAY)
    return pointer_to(s, qualified(s, t->ptr_to, inherited));
  if (t->kind == TYPE_FUNCTION)
    return pointer_to(s, t);
  return unqualified(s, t);
}

static prim_kind real_kind(type_info *type) {
  prim_kind integer = integer_type_of(type);
  return integer != PRIM_NONE ? integer : resolved_type(type)->prim;
}

static type_info *promoted(type_info *type) {
  prim_kind integer = integer_type_of(type);
  if (integer != PRIM_NONE)
    return arithmetic_type(promote(integer), 0);
  return arithmetic_type(resolved_type(type)->prim, resolved_type(type)->is_complex);
}

static type_info *arithmetic_result(type_info *a, type_info *b) {
  prim_kind x = real_kind(a);
  prim_kind y = real_kind(b);
  int is_complex = resolved_type(a)->is_complex || resolved_type(b)->is_complex;
  if (x == PRIM_LDOUBLE || y == PRIM_LDOUBLE)
    return arithmetic_type(PRIM_LDOUBLE, is_complex);
  if (x == PRIM_DOUBLE || y == PRIM_DOUBLE)
    return arithmetic_type(PRIM_DOUBLE, is_complex);
  if (x == PRIM_FLOAT || y == PRIM_FLOAT)
    return arithmetic_type(PRIM_FLOAT, is_complex);
  return arithmetic_type(usual_conversions(x, y), 0);
}

static int is_null_pointer_constant(sema *s, ast_node *node) {
  if (node && node->type == AST_NODE_TYPE_CAST && node->cast_expr.operand) {
    type_info *target = resolved_type(node->cast_expr.type);
    if (is_pointer(target) && is_void(pointee(target)) && pointee_qualifiers(target) == 0)
      node = node->cast_expr.operand;
  }
  if (!node)
    return 0;
  constant value = evaluate(s, node, 1);
  return value.status == CONST_VALUE && value.value.bits == 0;
}

static type_info *number_type(ast_node *node) {
  const char *text = node->tok.value ? node->tok.value : "";
  number_kind kind = classify_number(text, NULL);
  if (kind == NUMBER_INTEGER)
    return arithmetic_type(evaluate_integer_constant(node).value.type, 0);
  if (kind != NUMBER_FLOATING)
    return NULL;
  char suffix = text[strlen(text) - 1];
  if (suffix == 'f' || suffix == 'F')
    return arithmetic_type(PRIM_FLOAT, 0);
  if (suffix == 'l' || suffix == 'L')
    return arithmetic_type(PRIM_LDOUBLE, 0);
  return arithmetic_type(PRIM_DOUBLE, 0);
}

static type_info *string_type(sema *s, ast_node *node) {
  type_info array = {0};
  array.kind = TYPE_ARRAY;
  array.array_size = (long long)node->literal.length + 1;
  array.ptr_to = arithmetic_type(node->literal.is_wide ? PRIM_USHORT : PRIM_CHAR, 0);
  node->is_lvalue = 1;
  return copy_type(s, &array);
}

static type_info *identifier_type(ast_node *node) {
  if (!node->symbol)
    return NULL;
  if (node->symbol->kind == SYMBOL_ENUM_CONSTANT)
    return arithmetic_type(PRIM_INT, 0);
  node->is_lvalue = node->symbol->kind == SYMBOL_VAR;
  if (node->symbol->kind == SYMBOL_VAR || node->symbol->kind == SYMBOL_FUNC)
    return node->symbol->type;
  return NULL;
}

static int is_bitfield(ast_node *expr) {
  return expr && expr->type == AST_NODE_TYPE_MEMBER_ACCESS && expr->member_access.member &&
         expr->member_access.member->var_decl.bitfield_width;
}

static int has_const_member(type_info *record) {
  ast_node *def = record->symbol->definition;
  for (int i = 0; def && i < def->struct_def.member_count; i++) {
    ast_node *item = def->struct_def.members[i];
    int group = item->type == AST_NODE_TYPE_DECL_GROUP;
    for (int j = 0; j < (group ? item->block.count : 1); j++) {
      ast_node *member = group ? item->block.statements[j] : item;
      if (member->type != AST_NODE_TYPE_VAR_DECL)
        continue;
      int qualifiers;
      type_info *t = strip_typedefs(member->var_decl.type, &qualifiers);
      while (t->kind == TYPE_ARRAY) {
        int element_qualifiers;
        t = strip_typedefs(t->ptr_to, &element_qualifiers);
        qualifiers |= element_qualifiers;
      }
      if (is_unknown(t))
        continue;
      if ((qualifiers | qualifiers_of(t)) & 1)
        return 1;
      if ((t->kind == TYPE_STRUCT || t->kind == TYPE_UNION) && has_const_member(t))
        return 1;
    }
  }
  return 0;
}

static int is_modifiable_lvalue(ast_node *expr) {
  if (!expr->is_lvalue)
    return 0;
  int qualifiers;
  type_info *t = strip_typedefs(expr->expr_type, &qualifiers);
  if (t->kind == TYPE_ARRAY || is_incomplete(t) || ((qualifiers | qualifiers_of(t)) & 1))
    return 0;
  return !((t->kind == TYPE_STRUCT || t->kind == TYPE_UNION) && has_const_member(t));
}

static type_info *address_type(sema *s, ast_node *node) {
  ast_node *operand = node->unary_op.operand;
  type_info *t = resolved_type(operand ? operand->expr_type : NULL);
  if (is_unknown(t))
    return NULL;
  int designates = t->kind == TYPE_FUNCTION || (operand->type == AST_NODE_TYPE_UNARY_OP &&
                                                operand->unary_op.op.type == TOKEN_STAR);
  if (!designates && !operand->is_lvalue) {
    report_at(s, node->loc, "the operand of '&' must be an lvalue or a function designator");
    return NULL;
  }
  if (is_bitfield(operand)) {
    report_at(s, node->loc, "cannot take the address of a bit-field");
    return NULL;
  }
  if (operand->type == AST_NODE_TYPE_IDENTIFIER && operand->symbol &&
      operand->symbol->is_register) {
    report(s, node->loc, "cannot take the address of register object", operand->tok.value);
    return NULL;
  }
  return pointer_to(s, operand->expr_type);
}

static type_info *indirection_type(sema *s, ast_node *node) {
  type_info *pointer = value_type(s, node->unary_op.operand);
  if (!pointer)
    return NULL;
  if (!is_pointer(pointer)) {
    report(s, node->loc, "invalid operand to unary", node->unary_op.op.value);
    return NULL;
  }
  node->is_lvalue = !points_to_function(pointer) && !is_void(pointee(pointer));
  return pointee(pointer);
}

static type_info *increment_type(sema *s, ast_node *node) {
  ast_node *operand = node->unary_op.operand;
  type_info *type = value_type(s, operand);
  if (!type)
    return NULL;
  if (!is_real(type) && !points_to_object(type)) {
    report(s, node->loc, "invalid operand to unary", node->unary_op.op.value);
    return NULL;
  }
  if (!is_modifiable_lvalue(operand)) {
    report(s, node->loc, "not a modifiable lvalue as the operand of", node->unary_op.op.value);
    return NULL;
  }
  return type;
}

static type_info *sizeof_type(sema *s, ast_node *node) {
  type_info *t = resolved_type(sizeof_operand_type(node));
  if (is_bitfield(node->unary_op.operand))
    report_at(s, node->unary_op.operand->loc, "invalid application of sizeof to a bit-field");
  else if (!is_unknown(t)) {
    if (t->kind == TYPE_FUNCTION)
      report_at(s, node->unary_op.operand->loc, "invalid application of sizeof to a function type");
    else if (is_incomplete(t))
      report_at(s, node->unary_op.operand->loc,
                "invalid application of sizeof to an incomplete type");
  }
  return arithmetic_type(PRIM_ULLONG, 0);
}

static type_info *unary_type(sema *s, ast_node *node) {
  token_type op = node->unary_op.op.type;
  if (op == TOKEN_SIZEOF)
    return sizeof_type(s, node);
  if (op == TOKEN_AMPERSAND)
    return address_type(s, node);
  if (op == TOKEN_STAR)
    return indirection_type(s, node);
  if (op == TOKEN_PLUS_PLUS || op == TOKEN_MINUS_MINUS)
    return increment_type(s, node);
  if (op != TOKEN_PLUS && op != TOKEN_MINUS && op != TOKEN_TILDE && op != TOKEN_NOT)
    return NULL;
  type_info *operand = value_type(s, node->unary_op.operand);
  if (!operand)
    return NULL;
  int valid = op == TOKEN_NOT     ? is_scalar(operand)
              : op == TOKEN_TILDE ? is_integer(operand)
                                  : is_arithmetic(operand);
  if (!valid) {
    report(s, node->loc, "invalid operand to unary", node->unary_op.op.value);
    return NULL;
  }
  return op == TOKEN_NOT ? arithmetic_type(PRIM_INT, 0) : promoted(operand);
}

static int comparable_pointers(type_info *a, type_info *b) {
  return is_pointer(a) && is_pointer(b) && compatible_unqualified(pointee(a), pointee(b));
}

static int void_pointer_pair(type_info *a, type_info *b) {
  return is_pointer(a) && is_pointer(b) &&
         ((is_void(pointee(a)) && !points_to_function(b)) ||
          (is_void(pointee(b)) && !points_to_function(a)));
}

static type_info *binary_result(sema *s, token_type op, ast_node *left, ast_node *right,
                                type_info *a, type_info *b) {
  switch (op) {
  case TOKEN_STAR:
  case TOKEN_SLASH:
    return is_arithmetic(a) && is_arithmetic(b) ? arithmetic_result(a, b) : NULL;
  case TOKEN_PERCENT:
  case TOKEN_AMPERSAND:
  case TOKEN_PIPE:
  case TOKEN_CARET:
    return is_integer(a) && is_integer(b) ? arithmetic_result(a, b) : NULL;
  case TOKEN_LSHIFT:
  case TOKEN_RSHIFT:
    return is_integer(a) && is_integer(b) ? promoted(a) : NULL;
  case TOKEN_PLUS:
    if (is_arithmetic(a) && is_arithmetic(b))
      return arithmetic_result(a, b);
    if (points_to_object(a) && is_integer(b))
      return a;
    return is_integer(a) && points_to_object(b) ? b : NULL;
  case TOKEN_MINUS:
    if (is_arithmetic(a) && is_arithmetic(b))
      return arithmetic_result(a, b);
    if (points_to_object(a) && is_integer(b))
      return a;
    return points_to_object(a) && points_to_object(b) && comparable_pointers(a, b)
               ? arithmetic_type(PRIM_LLONG, 0)
               : NULL;
  case TOKEN_LT:
  case TOKEN_GT:
  case TOKEN_LTE:
  case TOKEN_GTE:
    if (is_real(a) && is_real(b))
      return arithmetic_type(PRIM_INT, 0);
    return comparable_pointers(a, b) && !points_to_function(a) &&
                   is_incomplete(pointee(a)) == is_incomplete(pointee(b))
               ? arithmetic_type(PRIM_INT, 0)
               : NULL;
  case TOKEN_EQ:
  case TOKEN_NEQ:
    if ((is_arithmetic(a) && is_arithmetic(b)) || comparable_pointers(a, b) ||
        void_pointer_pair(a, b) || (is_pointer(a) && is_null_pointer_constant(s, right)) ||
        (is_pointer(b) && is_null_pointer_constant(s, left)))
      return arithmetic_type(PRIM_INT, 0);
    return NULL;
  case TOKEN_AND:
  case TOKEN_OR:
    return is_scalar(a) && is_scalar(b) ? arithmetic_type(PRIM_INT, 0) : NULL;
  default:
    return NULL;
  }
}

static type_info *binary_type(sema *s, ast_node *node) {
  if (node->binary_op.op.type == TOKEN_COMMA)
    return value_type(s, node->binary_op.right);
  type_info *a = value_type(s, node->binary_op.left);
  type_info *b = value_type(s, node->binary_op.right);
  if (!a || !b)
    return NULL;
  type_info *result =
      binary_result(s, node->binary_op.op.type, node->binary_op.left, node->binary_op.right, a, b);
  if (!result)
    report(s, node->loc, "invalid operands to binary", node->binary_op.op.value);
  return result;
}

static type_info *conditional_type(sema *s, ast_node *node) {
  type_info *condition = value_type(s, node->ternary.condition);
  type_info *a = value_type(s, node->ternary.true_branch);
  type_info *b = value_type(s, node->ternary.false_branch);
  if (!condition || !a || !b)
    return NULL;
  if (!is_scalar(condition)) {
    report_at(s, node->loc, "the condition of '?:' needs a scalar type");
    return NULL;
  }
  type_info *ta = resolved_type(a);
  if (is_arithmetic(a) && is_arithmetic(b))
    return arithmetic_result(a, b);
  if ((ta->kind == TYPE_STRUCT || ta->kind == TYPE_UNION) && compatible_types(a, 0, b, 0))
    return a;
  if (is_void(a) && is_void(b))
    return a;
  if (is_pointer(a) && is_null_pointer_constant(s, node->ternary.false_branch))
    return a;
  if (is_pointer(b) && is_null_pointer_constant(s, node->ternary.true_branch))
    return b;
  if (comparable_pointers(a, b) || (void_pointer_pair(a, b) && is_void(pointee(a))))
    return pointer_to(s, qualified(s, pointee(a), pointee_qualifiers(b)));
  if (void_pointer_pair(a, b))
    return pointer_to(s, qualified(s, pointee(b), pointee_qualifiers(a)));
  report_at(s, node->loc, "type mismatch in conditional expression");
  return NULL;
}

static type_info *cast_type(sema *s, ast_node *node) {
  if (!node->cast_expr.operand)
    return NULL;
  type_info *target = resolved_type(node->cast_expr.type);
  if (is_unknown(target))
    return NULL;
  if (is_void(target))
    return unqualified(s, target);
  type_info *operand = value_type(s, node->cast_expr.operand);
  if (!is_scalar(target)) {
    report_at(s, node->loc, "a cast needs a scalar or void type");
    return NULL;
  }
  if (operand && !is_scalar(operand)) {
    report_at(s, node->loc, "a cast needs a scalar operand");
    return NULL;
  }
  if (operand && ((is_pointer(target) && !is_integer(operand) && !is_pointer(operand)) ||
                  (is_pointer(operand) && !is_integer(target) && !is_pointer(target)))) {
    report_at(s, node->loc, "cannot cast between a pointer and a floating type");
    return NULL;
  }
  return unqualified(s, target);
}

static ast_node *member_named(type_info *record, const char *name) {
  ast_node *def = record->symbol->definition;
  for (int i = 0; i < def->struct_def.member_count; i++) {
    ast_node *item = def->struct_def.members[i];
    int group = item->type == AST_NODE_TYPE_DECL_GROUP;
    for (int j = 0; j < (group ? item->block.count : 1); j++) {
      ast_node *member = group ? item->block.statements[j] : item;
      if (member->type == AST_NODE_TYPE_VAR_DECL && member->var_decl.var_name &&
          strcmp(member->var_decl.var_name, name) == 0)
        return member;
    }
  }
  return NULL;
}

static type_info *member_type(sema *s, ast_node *node) {
  ast_node *left = node->member_access.left;
  type_info *record = left ? left->expr_type : NULL;
  if (node->member_access.is_pointer) {
    type_info *pointer = value_type(s, left);
    if (!pointer)
      return NULL;
    if (!is_pointer(pointer)) {
      report_at(s, node->loc, "the left operand of '->' must be a pointer to a structure or union");
      return NULL;
    }
    record = pointee(pointer);
  }
  int qualifiers;
  type_info *t = strip_typedefs(record, &qualifiers);
  if (is_unknown(t))
    return NULL;
  if (t->kind != TYPE_STRUCT && t->kind != TYPE_UNION) {
    report_at(s, node->loc,
              node->member_access.is_pointer
                  ? "the left operand of '->' must be a pointer to a structure or union"
                  : "the left operand of '.' must be a structure or union");
    return NULL;
  }
  if (!t->symbol->is_defined) {
    report_at(s, node->loc, "member access into an incomplete structure or union");
    return NULL;
  }
  ast_node *member = member_named(t, node->member_access.member_name);
  if (!member) {
    report(s, node->loc, "no member named", node->member_access.member_name);
    return NULL;
  }
  node->member_access.member = member;
  node->is_lvalue = node->member_access.is_pointer || left->is_lvalue;
  return qualified(s, member->var_decl.type, qualifiers | qualifiers_of(t));
}

static type_info *subscript_type(sema *s, ast_node *node) {
  type_info *a = value_type(s, node->array_subscript.left);
  type_info *b = value_type(s, node->array_subscript.index);
  if (!a || !b)
    return NULL;
  type_info *pointer = is_pointer(a) ? a : b;
  type_info *index = is_pointer(a) ? b : a;
  if (!points_to_object(pointer) || !is_integer(index)) {
    report_at(s, node->loc, "a subscript needs a pointer to an object and an integer");
    return NULL;
  }
  node->is_lvalue = 1;
  return pointee(pointer);
}

static const char *conversion_problem(sema *s, type_info *target, ast_node *source) {
  type_info *from = value_type(s, source);
  type_info *t = resolved_type(target);
  if ((is_arithmetic(target) && is_arithmetic(from)) ||
      (t->prim == PRIM_BOOL && is_pointer(from)) ||
      (is_pointer(target) && is_null_pointer_constant(s, source)))
    return NULL;
  if ((t->kind == TYPE_STRUCT || t->kind == TYPE_UNION) && compatible_types(target, 0, from, 0))
    return NULL;
  if (comparable_pointers(target, from) || void_pointer_pair(target, from))
    return (pointee_qualifiers(from) & ~pointee_qualifiers(target)) ? "qualifiers discarded" : NULL;
  return "incompatible types";
}

static void report_conversion(sema *s, source_loc loc, const char *problem, const char *context) {
  char message[96];
  snprintf(message, sizeof message, "%s in %s", problem, context);
  report_at(s, loc, message);
}

static token_type operator_of_assignment(token_type op) {
  switch (op) {
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

static type_info *assignment_type(sema *s, ast_node *node) {
  ast_node *left = node->assignment.left;
  ast_node *right = node->assignment.right;
  token_type op = node->assignment.op.type;
  type_info *target = value_type(s, left);
  if (!target)
    return NULL;
  if (!is_modifiable_lvalue(left)) {
    report(s, node->loc, "not a modifiable lvalue on the left of", node->assignment.op.value);
    return NULL;
  }
  type_info *from = value_type(s, right);
  if (!from)
    return NULL;
  if (op == TOKEN_ASSIGN) {
    const char *problem = conversion_problem(s, target, right);
    if (problem) {
      report_conversion(s, node->loc, problem, "assignment");
      return NULL;
    }
    return target;
  }
  int valid = op == TOKEN_PLUS_ASSIGN || op == TOKEN_MINUS_ASSIGN
                  ? (is_arithmetic(target) && is_arithmetic(from)) ||
                        (points_to_object(target) && is_integer(from))
                  : binary_result(s, operator_of_assignment(op), left, right, target, from) != NULL;
  if (!valid) {
    report(s, node->loc, "invalid operands to binary", node->assignment.op.value);
    return NULL;
  }
  return target;
}

static type_info *compound_literal_type(sema *s, ast_node *node) {
  type_info *type = node->compound_literal.type;
  type_info *t = resolved_type(type);
  if (is_unknown(t))
    return NULL;
  if (t->kind == TYPE_ARRAY && t->is_vla) {
    report_at(s, node->loc, "a compound literal cannot have a variable length array type");
    return NULL;
  }
  if (t->kind == TYPE_FUNCTION || (t->kind != TYPE_ARRAY && is_incomplete(t))) {
    report_at(s, node->loc, "a compound literal needs an object type");
    return NULL;
  }
  node->is_lvalue = 1;
  int is_static = s->table->current_scope->kind == SCOPE_FILE;
  long long count =
      initialize_declared(s, type, node->compound_literal.init_list, is_static, node->loc);
  return sized_array(s, type, count);
}

static int check_argument(sema *s, type_info *fn, int index, ast_node *argument) {
  type_info *value = value_type(s, argument);
  if (!value)
    return 1;
  if (fn->has_prototype && index < fn->param_count) {
    type_info *parameter = unqualified(s, adjusted_parameter_type(s, fn->param_types[index]));
    const char *problem = is_unknown(parameter) ? NULL : conversion_problem(s, parameter, argument);
    if (problem)
      report_conversion(s, argument->loc, problem, "argument");
    return problem == NULL;
  }
  if (is_incomplete(value)) {
    report_at(s, argument->loc, "an argument needs a complete object type");
    return 0;
  }
  return 1;
}

static type_info *call_type(sema *s, ast_node *node) {
  type_info *callee = value_type(s, node->function_call.callable);
  if (!callee)
    return NULL;
  type_info *fn = is_pointer(callee) ? resolved_type(pointee(callee)) : NULL;
  if (!fn || fn->kind != TYPE_FUNCTION) {
    report_at(s, node->loc, "the called expression is not a function");
    return NULL;
  }
  int valid = 1;
  if (!is_void(fn->ptr_to) && is_incomplete(fn->ptr_to)) {
    report_at(s, node->loc, "a called function must return void or a complete object type");
    valid = 0;
  }
  int count = node->function_call.arg_count;
  if (fn->has_prototype && count < fn->param_count) {
    report_at(s, node->loc, "too few arguments in call");
    valid = 0;
  } else if (fn->has_prototype && count > fn->param_count && !fn->is_variadic) {
    report_at(s, node->loc, "too many arguments in call");
    valid = 0;
  }
  for (int i = 0; i < count; i++) {
    if (!check_argument(s, fn, i, node->function_call.arguments[i]))
      valid = 0;
  }
  return valid ? unqualified(s, fn->ptr_to) : NULL;
}

typedef enum {
  NOT_CONSTANT,
  ARITHMETIC_CONSTANT,
  ADDRESS_CONSTANT,
  UNKNOWN_CONSTANT
} constant_class;

static int is_static_object(sema *s, ast_node *expr);

static constant_class classify_constant(sema *s, ast_node *expr) {
  if (!expr || !expr->expr_type)
    return UNKNOWN_CONSTANT;
  type_info *type = resolved_type(expr->expr_type);
  switch (expr->type) {
  case AST_NODE_TYPE_NUMBER:
  case AST_NODE_TYPE_CHAR_LITERAL:
    return ARITHMETIC_CONSTANT;
  case AST_NODE_TYPE_STRING:
    return ADDRESS_CONSTANT;
  case AST_NODE_TYPE_IDENTIFIER:
    if (expr->symbol->kind == SYMBOL_ENUM_CONSTANT)
      return ARITHMETIC_CONSTANT;
    if (expr->symbol->kind == SYMBOL_FUNC)
      return ADDRESS_CONSTANT;
    return type->kind == TYPE_ARRAY && is_static_object(s, expr) ? ADDRESS_CONSTANT : NOT_CONSTANT;
  case AST_NODE_TYPE_UNARY_OP: {
    token_type op = expr->unary_op.op.type;
    ast_node *operand = expr->unary_op.operand;
    if (op == TOKEN_SIZEOF)
      return evaluate_sizeof(expr).status == CONST_NOT_CONSTANT ? NOT_CONSTANT
                                                                : ARITHMETIC_CONSTANT;
    if (op == TOKEN_AMPERSAND) {
      type_info *target = resolved_type(operand->expr_type);
      if (target->kind == TYPE_FUNCTION)
        return operand->type == AST_NODE_TYPE_IDENTIFIER ||
                       classify_constant(s, operand->unary_op.operand) == ADDRESS_CONSTANT
                   ? ADDRESS_CONSTANT
                   : NOT_CONSTANT;
      return is_static_object(s, operand) ? ADDRESS_CONSTANT : NOT_CONSTANT;
    }
    if (op == TOKEN_STAR)
      return (type->kind == TYPE_ARRAY || type->kind == TYPE_FUNCTION) &&
                     classify_constant(s, operand) == ADDRESS_CONSTANT
                 ? ADDRESS_CONSTANT
                 : NOT_CONSTANT;
    if (op == TOKEN_PLUS || op == TOKEN_MINUS || op == TOKEN_TILDE || op == TOKEN_NOT) {
      constant_class value = classify_constant(s, operand);
      return value == ADDRESS_CONSTANT ? NOT_CONSTANT : value;
    }
    return NOT_CONSTANT;
  }
  case AST_NODE_TYPE_CAST: {
    constant_class operand = classify_constant(s, expr->cast_expr.operand);
    if (operand == UNKNOWN_CONSTANT)
      return UNKNOWN_CONSTANT;
    if (type->kind == TYPE_POINTER)
      return operand == NOT_CONSTANT ? NOT_CONSTANT : ADDRESS_CONSTANT;
    return operand == ARITHMETIC_CONSTANT ? ARITHMETIC_CONSTANT : NOT_CONSTANT;
  }
  case AST_NODE_TYPE_BINARY_OP: {
    token_type op = expr->binary_op.op.type;
    if (op == TOKEN_COMMA)
      return NOT_CONSTANT;
    constant_class a = classify_constant(s, expr->binary_op.left);
    constant_class b = classify_constant(s, expr->binary_op.right);
    if (a == UNKNOWN_CONSTANT || b == UNKNOWN_CONSTANT)
      return UNKNOWN_CONSTANT;
    if (a == ARITHMETIC_CONSTANT && b == ARITHMETIC_CONSTANT)
      return ARITHMETIC_CONSTANT;
    if (op == TOKEN_PLUS && ((a == ADDRESS_CONSTANT && b == ARITHMETIC_CONSTANT) ||
                             (a == ARITHMETIC_CONSTANT && b == ADDRESS_CONSTANT)))
      return ADDRESS_CONSTANT;
    return op == TOKEN_MINUS && a == ADDRESS_CONSTANT && b == ARITHMETIC_CONSTANT ? ADDRESS_CONSTANT
                                                                                  : NOT_CONSTANT;
  }
  case AST_NODE_TYPE_TERNARY: {
    constant_class condition = classify_constant(s, expr->ternary.condition);
    constant_class a = classify_constant(s, expr->ternary.true_branch);
    constant_class b = classify_constant(s, expr->ternary.false_branch);
    if (condition == UNKNOWN_CONSTANT)
      return UNKNOWN_CONSTANT;
    if (condition != ARITHMETIC_CONSTANT || a == NOT_CONSTANT || b == NOT_CONSTANT)
      return NOT_CONSTANT;
    return type->kind == TYPE_POINTER ? ADDRESS_CONSTANT : ARITHMETIC_CONSTANT;
  }
  case AST_NODE_TYPE_MEMBER_ACCESS:
  case AST_NODE_TYPE_ARRAY_SUBSCRIPT:
  case AST_NODE_TYPE_COMPOUND_LITERAL:
    return type->kind == TYPE_ARRAY && is_static_object(s, expr) ? ADDRESS_CONSTANT : NOT_CONSTANT;
  default:
    return NOT_CONSTANT;
  }
}

static int is_static_object(sema *s, ast_node *expr) {
  switch (expr->type) {
  case AST_NODE_TYPE_IDENTIFIER:
    return expr->symbol && expr->symbol->kind == SYMBOL_VAR && expr->symbol->has_static_storage;
  case AST_NODE_TYPE_STRING:
    return 1;
  case AST_NODE_TYPE_COMPOUND_LITERAL:
    return s->table->current_scope->kind == SCOPE_FILE;
  case AST_NODE_TYPE_UNARY_OP:
    return expr->unary_op.op.type == TOKEN_STAR &&
           classify_constant(s, expr->unary_op.operand) == ADDRESS_CONSTANT;
  case AST_NODE_TYPE_ARRAY_SUBSCRIPT: {
    constant_class a = classify_constant(s, expr->array_subscript.left);
    constant_class b = classify_constant(s, expr->array_subscript.index);
    return (a == ADDRESS_CONSTANT && b == ARITHMETIC_CONSTANT) ||
           (a == ARITHMETIC_CONSTANT && b == ADDRESS_CONSTANT);
  }
  case AST_NODE_TYPE_MEMBER_ACCESS:
    return expr->member_access.is_pointer
               ? classify_constant(s, expr->member_access.left) == ADDRESS_CONSTANT
               : is_static_object(s, expr->member_access.left);
  default:
    return 0;
  }
}

typedef struct init_frame {
  type_info *type;
  long long position;
  long long length;
  long long highest;
  ast_node **members;
} init_frame;

typedef struct initializer {
  sema *s;
  int is_static;
  init_frame *frames;
  int depth;
} initializer;

static int is_aggregate(type_info *t) {
  return t && (t->kind == TYPE_ARRAY || t->kind == TYPE_STRUCT || t->kind == TYPE_UNION);
}

static int is_string_array(type_info *t) {
  type_info *element = t && t->kind == TYPE_ARRAY ? resolved_type(t->ptr_to) : NULL;
  return element && element->kind == TYPE_PRIMITIVE && !element->is_complex &&
         (element->prim == PRIM_CHAR || element->prim == PRIM_SCHAR ||
          element->prim == PRIM_UCHAR || element->prim == PRIM_USHORT);
}

static int push_frame(initializer *init, type_info *type) {
  type_info *t = resolved_type(type);
  init_frame *frames =
      (init_frame *)realloc(init->frames, sizeof(init_frame) * (size_t)(init->depth + 1));
  if (!frames)
    return 0;
  init->frames = frames;
  if (init->depth > 0 && frames[init->depth - 1].position >= frames[init->depth - 1].highest)
    frames[init->depth - 1].highest = frames[init->depth - 1].position + 1;
  init_frame frame = {t, 0, t->array_size, 0, NULL};
  if (t->kind != TYPE_ARRAY) {
    ast_node *def = t->symbol->definition;
    int count = collect_members(def, NULL);
    frame.members = (ast_node **)malloc(sizeof(ast_node *) * (size_t)(count + 1));
    if (!frame.members)
      return 0;
    collect_members(def, frame.members);
    frame.length = 0;
    for (int i = 0; i < count; i++) {
      ast_node *member = frame.members[i];
      if (member->var_decl.var_name && !is_incomplete(member->var_decl.type))
        frame.members[frame.length++] = member;
    }
  }
  frames[init->depth++] = frame;
  return 1;
}

static void pop_frames(initializer *init, int depth) {
  while (init->depth > depth)
    free(init->frames[--init->depth].members);
}

static void advance(init_frame *frame) {
  if (frame->type->kind == TYPE_UNION)
    frame->position = frame->length;
  else
    frame->position++;
  if (frame->position > frame->highest)
    frame->highest = frame->position;
}

static int exhausted(init_frame *frame) {
  return frame->length >= 0 && frame->position >= frame->length;
}

static type_info *current_element(init_frame *frame) {
  if (frame->type->kind == TYPE_ARRAY)
    return frame->type->ptr_to;
  return frame->members[frame->position]->var_decl.type;
}

static long long initialize_object(initializer *init, type_info *type, ast_node *value);

static void initialize_leaf(initializer *init, type_info *type, ast_node *value) {
  if (!value_type(init->s, value))
    return;
  const char *problem = conversion_problem(init->s, unqualified(init->s, type), value);
  if (problem)
    report_conversion(init->s, value->loc, problem, "initialization");
  else if (init->is_static && classify_constant(init->s, value) == NOT_CONSTANT)
    report_at(init->s, value->loc,
              "an initializer for an object with static storage must be constant");
}

static long long initialize_string(initializer *init, type_info *array, ast_node *string) {
  int wide = resolved_type(array->ptr_to)->prim == PRIM_USHORT;
  if (wide != string->literal.is_wide) {
    report_conversion(init->s, string->loc, "incompatible types", "initialization");
    return -1;
  }
  if (array->array_size >= 0 && string->literal.length > array->array_size)
    report_at(init->s, string->loc, "initializer string is too long for its array");
  return (long long)string->literal.length + 1;
}

static int designate(initializer *init, ast_node *list, const char *name, ast_node *index) {
  init_frame *frame = &init->frames[init->depth - 1];
  if (index) {
    if (frame->type->kind != TYPE_ARRAY) {
      report_at(init->s, list->loc, "an array designator needs an array");
      return 0;
    }
    constant value = evaluate(init->s, index, 1);
    report_problem(init->s, value);
    if (value.status == CONST_NOT_CONSTANT) {
      report_at(init->s, index->loc, "an array designator needs an integer constant expression");
      return 0;
    }
    if (value.status != CONST_VALUE)
      return 0;
    if (!is_unsigned(value.value.type) && (long long)value.value.bits < 0) {
      report_at(init->s, index->loc, "an array designator cannot be negative");
      return 0;
    }
    if (frame->length >= 0 && value.value.bits >= (unsigned long long)frame->length) {
      report_at(init->s, index->loc, "an array designator is past the end of the array");
      return 0;
    }
    frame->position = (long long)value.value.bits;
    return 1;
  }
  if (frame->type->kind == TYPE_ARRAY) {
    report_at(init->s, list->loc, "a member designator needs a structure or union");
    return 0;
  }
  for (long long i = 0; i < frame->length; i++) {
    if (strcmp(frame->members[i]->var_decl.var_name, name) == 0) {
      frame->position = i;
      return 1;
    }
  }
  report(init->s, list->loc, "no member named", name);
  return 0;
}

static ast_node *follow_designation(initializer *init, ast_node *list, int item) {
  const char *name = list->init_list.items[item].member_name;
  ast_node *index = list->init_list.items[item].index;
  ast_node *value = list->init_list.items[item].value;
  if (!designate(init, list, name, index))
    return NULL;
  while (value->type == AST_NODE_TYPE_INIT_LIST && value->init_list.is_designation) {
    type_info *element = resolved_type(current_element(&init->frames[init->depth - 1]));
    if (is_unknown(element))
      return NULL;
    if (!is_aggregate(element)) {
      report_at(init->s, list->loc,
                "a designator needs a structure, union or array to select from");
      return NULL;
    }
    if (!push_frame(init, element) || !designate(init, list, value->init_list.items[0].member_name,
                                                 value->init_list.items[0].index))
      return NULL;
    value = value->init_list.items[0].value;
  }
  return value;
}

static void place_value(initializer *init, int base, ast_node *value, int *excess) {
  for (;;) {
    init_frame *frame = &init->frames[init->depth - 1];
    if (exhausted(frame)) {
      if (init->depth - 1 > base) {
        pop_frames(init, init->depth - 1);
        advance(&init->frames[init->depth - 1]);
        continue;
      }
      if (!*excess)
        report_at(init->s, value->loc, "excess elements in initializer");
      *excess = 1;
      return;
    }
    type_info *element = current_element(frame);
    type_info *t = resolved_type(element);
    int braced = value->type == AST_NODE_TYPE_INIT_LIST;
    int whole = is_string_array(t) && value->type == AST_NODE_TYPE_STRING;
    if (!braced && !whole && is_aggregate(t) && t->kind != TYPE_ARRAY) {
      type_info *from = value_type(init->s, value);
      whole = !from || compatible_types(unqualified(init->s, t), 0, from, 0);
    }
    if (braced || whole || !is_aggregate(t)) {
      initialize_object(init, element, value);
      advance(&init->frames[init->depth - 1]);
      return;
    }
    if (!push_frame(init, t))
      return;
  }
}

static long long initialize_list(initializer *init, type_info *type, ast_node *list) {
  int base = init->depth;
  if (!push_frame(init, type))
    return -1;
  int excess = 0;
  for (int i = 0; i < list->init_list.count; i++) {
    ast_node *value = list->init_list.items[i].value;
    if (list->init_list.items[i].member_name || list->init_list.items[i].index) {
      pop_frames(init, base + 1);
      value = follow_designation(init, list, i);
      if (!value) {
        pop_frames(init, base + 1);
        continue;
      }
    }
    place_value(init, base, value, &excess);
  }
  long long count = init->frames[base].highest;
  pop_frames(init, base);
  return count;
}

static long long initialize_object(initializer *init, type_info *type, ast_node *value) {
  type_info *t = resolved_type(type);
  if (is_unknown(t))
    return -1;
  if (value->type == AST_NODE_TYPE_INIT_LIST) {
    ast_node *first = value->init_list.items[0].value;
    int plain = !value->init_list.items[0].member_name && !value->init_list.items[0].index;
    if (is_string_array(t) && value->init_list.count == 1 && plain &&
        first->type == AST_NODE_TYPE_STRING)
      return initialize_string(init, t, first);
    if (is_aggregate(t))
      return initialize_list(init, t, value);
    if (!plain)
      report_at(init->s, value->loc, "a designator cannot select from a scalar");
    else
      initialize_object(init, type, first);
    if (value->init_list.count > 1)
      report_at(init->s, value->init_list.items[1].value->loc, "excess elements in initializer");
    return -1;
  }
  if (is_string_array(t) && value->type == AST_NODE_TYPE_STRING)
    return initialize_string(init, t, value);
  if (t->kind == TYPE_ARRAY) {
    report_at(init->s, value->loc, "an array needs a brace-enclosed initializer");
    return -1;
  }
  initialize_leaf(init, type, value);
  return -1;
}

static long long initialize_declared(sema *s, type_info *type, ast_node *value, int is_static,
                                     source_loc loc) {
  type_info *t = resolved_type(type);
  if (!value)
    return -1;
  if (t->kind == TYPE_ARRAY && t->is_vla) {
    report_at(s, loc, "a variable length array cannot be initialized");
    return -1;
  }
  if (t->kind != TYPE_ARRAY && is_incomplete(t)) {
    report_at(s, loc, "an object with incomplete type cannot be initialized");
    return -1;
  }
  initializer init = {s, is_static, NULL, 0};
  long long count = initialize_object(&init, type, value);
  pop_frames(&init, 0);
  free(init.frames);
  return count;
}

static type_info *sized_array(sema *s, type_info *type, long long count) {
  type_info *t = resolved_type(type);
  if (!t || t->kind != TYPE_ARRAY || t->array_size_expr)
    return type;
  if (type->kind == TYPE_ARRAY) {
    type->array_size = count;
    return type;
  }
  type_info *copy = copy_type(s, t);
  if (!copy)
    return type;
  copy->array_size = count;
  return copy;
}

static void type_expression(sema *s, ast_node *node) {
  switch (node->type) {
  case AST_NODE_TYPE_NUMBER:
    node->expr_type = number_type(node);
    break;
  case AST_NODE_TYPE_CHAR_LITERAL:
    node->expr_type = arithmetic_type(node->literal.is_wide ? PRIM_USHORT : PRIM_INT, 0);
    break;
  case AST_NODE_TYPE_STRING:
    node->expr_type = string_type(s, node);
    break;
  case AST_NODE_TYPE_IDENTIFIER:
    node->expr_type = identifier_type(node);
    break;
  case AST_NODE_TYPE_UNARY_OP:
    node->expr_type = unary_type(s, node);
    break;
  case AST_NODE_TYPE_BINARY_OP:
    node->expr_type = binary_type(s, node);
    break;
  case AST_NODE_TYPE_TERNARY:
    node->expr_type = conditional_type(s, node);
    break;
  case AST_NODE_TYPE_CAST:
    node->expr_type = cast_type(s, node);
    break;
  case AST_NODE_TYPE_MEMBER_ACCESS:
    node->expr_type = member_type(s, node);
    break;
  case AST_NODE_TYPE_ARRAY_SUBSCRIPT:
    node->expr_type = subscript_type(s, node);
    break;
  case AST_NODE_TYPE_ASSIGNMENT:
    node->expr_type = assignment_type(s, node);
    break;
  case AST_NODE_TYPE_COMPOUND_LITERAL:
    node->expr_type = compound_literal_type(s, node);
    break;
  case AST_NODE_TYPE_FUNCTION_CALL:
    node->expr_type = call_type(s, node);
    break;
  default:
    break;
  }
}

typedef struct switch_context {
  prim_kind type;
  unsigned long long *values;
  source_loc *locations;
  int count;
  ast_node *default_label;
  struct switch_context *outer;
} switch_context;

static void check_condition(sema *s, ast_node *statement, ast_node *condition,
                            const char *message) {
  type_info *type = value_type(s, condition);
  if (type && !is_scalar(type))
    report_at(s, statement->loc, message);
}

static void resolve_loop_body(sema *s, ast_node *body) {
  s->loops++;
  s->breakables++;
  resolve_in_block(s, body);
  s->loops--;
  s->breakables--;
}

static void resolve_switch(sema *s, ast_node *node) {
  symbol_table_enter_scope(s->table, SCOPE_BLOCK);
  resolve(s, node->switch_stmt.condition);
  type_info *type = value_type(s, node->switch_stmt.condition);
  switch_context context = {0};
  context.type = PRIM_NONE;
  if (type && !is_integer(type))
    report_at(s, node->loc, "the controlling expression of 'switch' needs an integer type");
  else if (type)
    context.type = promote(integer_type_of(type));
  context.outer = s->current_switch;
  s->current_switch = &context;
  s->breakables++;
  resolve_in_block(s, node->switch_stmt.body);
  s->breakables--;
  s->current_switch = context.outer;
  free(context.values);
  free(context.locations);
  symbol_table_leave_scope(s->table);
}

static void record_case(sema *s, switch_context *context, ast_node *node) {
  ast_node *value = node->case_stmt.value;
  constant result = evaluate(s, value, 1);
  report_problem(s, result);
  if (result.status == CONST_NOT_CONSTANT) {
    report_at(s, node->loc, "a case label needs an integer constant expression");
    return;
  }
  if (result.status != CONST_VALUE || context->type == PRIM_NONE)
    return;
  unsigned long long bits = convert_value(result.value.bits, context->type).bits;
  for (int i = 0; i < context->count; i++) {
    if (context->values[i] == bits) {
      report_at(s, node->loc, "duplicate case value");
      note_at(context->locations[i], "previous case is here");
      return;
    }
  }
  unsigned long long *values = (unsigned long long *)realloc(
      context->values, sizeof(unsigned long long) * (size_t)(context->count + 1));
  if (!values)
    return;
  context->values = values;
  source_loc *locations =
      (source_loc *)realloc(context->locations, sizeof(source_loc) * (size_t)(context->count + 1));
  if (!locations)
    return;
  context->locations = locations;
  values[context->count] = bits;
  locations[context->count] = node->loc;
  context->count++;
}

static void resolve_case(sema *s, ast_node *node) {
  resolve(s, node->case_stmt.value);
  if (!s->current_switch)
    report_at(s, node->loc, "a case label must be inside a switch");
  else if (node->case_stmt.value)
    record_case(s, s->current_switch, node);
  resolve(s, node->case_stmt.body);
}

static void resolve_default(sema *s, ast_node *node) {
  switch_context *context = s->current_switch;
  if (!context) {
    report_at(s, node->loc, "a default label must be inside a switch");
  } else if (context->default_label) {
    report_at(s, node->loc, "duplicate default label");
    note_at(context->default_label->loc, "previous default label is here");
  } else {
    context->default_label = node;
  }
  resolve(s, node->default_stmt.body);
}

static void resolve_return(sema *s, ast_node *node) {
  ast_node *value = node->return_stmt.return_value;
  resolve(s, value);
  type_info *expected = s->return_type;
  if (is_unknown(expected))
    return;
  if (is_void(expected)) {
    if (value)
      report_at(s, node->loc, "a function returning void cannot return a value");
    return;
  }
  if (!value) {
    report_at(s, node->loc, "a function returning a value needs a return value");
    return;
  }
  if (!value_type(s, value))
    return;
  const char *problem = conversion_problem(s, unqualified(s, expected), value);
  if (problem)
    report_conversion(s, value->loc, problem, "return");
}

static void resolve(sema *s, ast_node *node) {
  if (!node)
    return;
  switch (node->type) {
  case AST_NODE_TYPE_IDENTIFIER:
    node->symbol = symbol_table_lookup_ordinary(s->table, node->tok.value);
    if (!node->symbol)
      report(s, node->loc, "use of undeclared identifier", node->tok.value);
    else if (s->inline_definition && node->symbol->linkage == LINKAGE_INTERNAL)
      report(
          s, node->loc,
          "an inline definition with external linkage cannot refer to internal linkage identifier",
          node->tok.value);
    break;
  case AST_NODE_TYPE_VAR_DECL:
    resolve_var_decl(s, node);
    break;
  case AST_NODE_TYPE_FUNCTION_DEF:
    resolve_function(s, node);
    break;
  case AST_NODE_TYPE_ENUM_DEF:
    resolve_enum(s, node);
    break;
  case AST_NODE_TYPE_STRUCT_DEF:
    resolve_struct(s, node);
    break;
  case AST_NODE_TYPE_DECL_GROUP:
    resolve_items(s, node);
    break;
  case AST_NODE_TYPE_BLOCK:
    symbol_table_enter_scope(s->table, SCOPE_BLOCK);
    resolve_items(s, node);
    symbol_table_leave_scope(s->table);
    break;
  case AST_NODE_TYPE_IF:
    symbol_table_enter_scope(s->table, SCOPE_BLOCK);
    resolve(s, node->if_stmt.condition);
    check_condition(s, node, node->if_stmt.condition, "the condition of 'if' needs a scalar type");
    resolve_in_block(s, node->if_stmt.then_branch);
    resolve_in_block(s, node->if_stmt.else_branch);
    symbol_table_leave_scope(s->table);
    break;
  case AST_NODE_TYPE_WHILE:
    symbol_table_enter_scope(s->table, SCOPE_BLOCK);
    resolve(s, node->while_stmt.condition);
    check_condition(s, node, node->while_stmt.condition,
                    "the condition of 'while' needs a scalar type");
    resolve_loop_body(s, node->while_stmt.body);
    symbol_table_leave_scope(s->table);
    break;
  case AST_NODE_TYPE_DO_WHILE:
    symbol_table_enter_scope(s->table, SCOPE_BLOCK);
    resolve_loop_body(s, node->do_while_stmt.body);
    resolve(s, node->do_while_stmt.condition);
    check_condition(s, node, node->do_while_stmt.condition,
                    "the condition of 'do' needs a scalar type");
    symbol_table_leave_scope(s->table);
    break;
  case AST_NODE_TYPE_FOR:
    symbol_table_enter_scope(s->table, SCOPE_BLOCK);
    resolve(s, node->for_stmt.init);
    check_for_declaration(s, node->for_stmt.init);
    resolve(s, node->for_stmt.condition);
    check_condition(s, node, node->for_stmt.condition,
                    "the condition of 'for' needs a scalar type");
    resolve(s, node->for_stmt.increment);
    resolve_loop_body(s, node->for_stmt.body);
    symbol_table_leave_scope(s->table);
    break;
  case AST_NODE_TYPE_SWITCH:
    resolve_switch(s, node);
    break;
  case AST_NODE_TYPE_CASE:
    resolve_case(s, node);
    break;
  case AST_NODE_TYPE_DEFAULT:
    resolve_default(s, node);
    break;
  case AST_NODE_TYPE_BREAK:
    if (!s->breakables)
      report_at(s, node->loc, "a break statement must be inside a loop or switch");
    break;
  case AST_NODE_TYPE_CONTINUE:
    if (!s->loops)
      report_at(s, node->loc, "a continue statement must be inside a loop");
    break;
  case AST_NODE_TYPE_LABEL:
    declare_label(s, node);
    resolve(s, node->label_stmt.statement);
    break;
  case AST_NODE_TYPE_GOTO:
    record_goto(s, node);
    break;
  case AST_NODE_TYPE_RETURN:
    resolve_return(s, node);
    break;
  case AST_NODE_TYPE_BINARY_OP:
    resolve(s, node->binary_op.left);
    resolve(s, node->binary_op.right);
    break;
  case AST_NODE_TYPE_ASSIGNMENT:
    resolve(s, node->assignment.left);
    resolve(s, node->assignment.right);
    break;
  case AST_NODE_TYPE_UNARY_OP:
    resolve(s, node->unary_op.operand);
    break;
  case AST_NODE_TYPE_TERNARY:
    resolve(s, node->ternary.condition);
    resolve(s, node->ternary.true_branch);
    resolve(s, node->ternary.false_branch);
    break;
  case AST_NODE_TYPE_FUNCTION_CALL:
    resolve(s, node->function_call.callable);
    for (int i = 0; i < node->function_call.arg_count; i++)
      resolve(s, node->function_call.arguments[i]);
    break;
  case AST_NODE_TYPE_ARRAY_SUBSCRIPT:
    resolve(s, node->array_subscript.left);
    resolve(s, node->array_subscript.index);
    break;
  case AST_NODE_TYPE_MEMBER_ACCESS:
    resolve(s, node->member_access.left);
    break;
  case AST_NODE_TYPE_CAST:
    resolve(s, node->cast_expr.definition);
    resolve_type(s, node->cast_expr.type, node->loc);
    resolve(s, node->cast_expr.operand);
    break;
  case AST_NODE_TYPE_COMPOUND_LITERAL:
    resolve(s, node->compound_literal.definition);
    resolve_type(s, node->compound_literal.type, node->loc);
    resolve(s, node->compound_literal.init_list);
    break;
  case AST_NODE_TYPE_INIT_LIST:
    for (int i = 0; i < node->init_list.count; i++) {
      resolve(s, node->init_list.items[i].index);
      resolve(s, node->init_list.items[i].value);
    }
    break;
  default:
    break;
  }
  type_expression(s, node);
}

int sema_check(symbol_table *table, ast_node *program) {
  if (!table || !program || program->type != AST_NODE_TYPE_PROGRAM)
    return -1;
  sema s = {.table = table, .program = program};
  symbol_table_enter_scope(table, SCOPE_FILE);
  for (int i = 0; i < program->program.count; i++)
    resolve(&s, program->program.declarations[i]);
  check_tentative_definitions(&s, program);
  symbol_table_leave_scope(table);
  free(s.gotos);
  return s.error_count;
}