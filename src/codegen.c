#include "codegen.h"
#include "lexer.h"
#include "sema.h"
#include "target.h"
#include <ctype.h>
#include <math.h>
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
  ast_node **scope;
  int scope_count;
} named_label;

typedef struct switch_case {
  ast_node *node;
  const char *label;
} switch_case;

typedef struct switch_context {
  switch_case *cases;
  int count;
} switch_context;

typedef struct vla_size {
  ast_node *expression;
  const char *slot;
} vla_size;

typedef struct relocation {
  long long offset;
  const char *target;
} relocation;

typedef enum pass_kind { PASS_DIRECT, PASS_PIECES, PASS_POINTER, PASS_MEMORY } pass_kind;

typedef struct passing {
  pass_kind kind;
  const char *pieces[2];
  int piece_count;
  const char *extension;
} passing;

typedef struct function_span {
  symbol *sym;
  size_t start;
  size_t end;
} function_span;

typedef struct function_use {
  symbol *from;
  symbol *to;
} function_use;

typedef struct codegen {
  text globals;
  text functions;
  text allocas;
  text body;
  text declarations;
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
  int uses_stacksave;
  int uses_returns_twice;
  int next_string;
  int next_global;
  const char *function_name;
  const char *function_name_global;
  int next_temp;
  int next_name;
  const char *block;
  int terminated;
  const char *break_label;
  const char *continue_label;
  int break_depth;
  int continue_depth;
  switch_context *current_switch;
  named_label *labels;
  int label_count;
  ast_node **vla_decls;
  const char **vla_saved;
  int vla_count;
  vla_size *sizes;
  int size_count;
  symbol *current_function;
  passing result;
  const char *result_slot;
  function_span *spans;
  int span_count;
  function_use *uses;
  int use_count;
} codegen;

static const char *compound_global(codegen *cg, ast_node *node);
static const char *symbol_address(codegen *cg, symbol *sym);
static const char *address_of(codegen *cg, ast_node *node);
static const char *value(codegen *cg, ast_node *node);
static const char *truth(codegen *cg, ast_node *node);
static void discard(codegen *cg, ast_node *node);
static void initialize_object(codegen *cg, type_info *type, initializer_layout *layout,
                              const char *base);
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

static void free_labels(codegen *cg) {
  for (int i = 0; i < cg->label_count; i++)
    free(cg->labels[i].scope);
  cg->label_count = 0;
}

static void free_codegen(codegen *cg) {
  for (int i = 0; i < cg->string_count; i++)
    free(cg->strings[i]);
  free(cg->strings);
  free(cg->globals.data);
  free(cg->functions.data);
  free(cg->allocas.data);
  free(cg->body.data);
  free(cg->declarations.data);
  free(cg->defined);
  free(cg->referenced);
  free_labels(cg);
  free(cg->labels);
  free(cg->vla_decls);
  free(cg->vla_saved);
  free(cg->sizes);
  free(cg->spans);
  free(cg->uses);
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

static int is_complex(type_info *type) {
  type_info *t = resolved(type);
  return t && t->kind == TYPE_PRIMITIVE && t->is_complex;
}

static int is_pointer(type_info *type) {
  type_info *t = resolved(type);
  return t && t->kind == TYPE_POINTER;
}

static int is_scalar(type_info *type) {
  return integer_kind(type) != PRIM_NONE || is_floating(type) || is_pointer(type);
}

static int is_aggregate(type_info *type) {
  type_info *t = resolved(type);
  return t && (t->kind == TYPE_STRUCT || t->kind == TYPE_UNION);
}

static int is_void(type_info *type) {
  type_info *t = resolved(type);
  return t && t->kind == TYPE_PRIMITIVE && t->prim == PRIM_VOID;
}

static int is_variably_sized(type_info *type) {
  type_info *t = resolved(type);
  return t && t->kind == TYPE_ARRAY && (t->is_vla || is_variably_sized(t->ptr_to));
}

static long long size_of(type_info *type) {
  return sema_type_layout(type).size;
}

static int alignment_of(type_info *type) {
  return sema_type_layout(type).alignment;
}

static int element_alignment(type_info *type) {
  type_info *t = resolved(type);
  while (t && t->kind == TYPE_ARRAY)
    t = resolved(t->ptr_to);
  return alignment_of(t);
}

static long long static_size(type_info *type) {
  type_info *t = resolved(type);
  if (t->kind == TYPE_ARRAY && t->array_size < 0)
    return size_of(t->ptr_to);
  return size_of(t);
}

static int object_alignment(type_info *type) {
  type_layout layout = sema_type_layout(type);
  int alignment = layout.known ? layout.alignment : element_alignment(type);
  type_info *t = resolved(type);
  if (t->kind == TYPE_ARRAY && static_size(type) >= 16 && alignment < 16)
    return 16;
  return alignment;
}

static type_info *primitive(prim_kind prim) {
  static type_info types[PRIM_LDOUBLE + 1];
  types[prim].kind = TYPE_PRIMITIVE;
  types[prim].prim = prim;
  types[prim].array_size = -1;
  return &types[prim];
}

static type_info *complex_part(type_info *type) {
  return primitive(resolved(type)->prim);
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
  if (is_aggregate(t))
    return formatted(cg, "[%lld x i8]", size_of(t));
  if (is_complex(t)) {
    const char *part = llvm_type(cg, complex_part(t));
    return formatted(cg, "{ %s, %s }", part, part);
  }
  if (is_floating(t))
    return t->prim == PRIM_FLOAT ? "float" : t->prim == PRIM_DOUBLE ? "double" : "x86_fp80";
  return "void";
}

static const char *value_type(codegen *cg, type_info *type) {
  return is_aggregate(type) ? "ptr" : llvm_type(cg, type);
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

static const char *global_name(codegen *cg, symbol *sym) {
  return ir_name(cg, '@', sym->asm_label ? sym->asm_label : sym->name);
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

static void declare_once(codegen *cg, const char *line) {
  const char *wanted = formatted(cg, "%s\n", line);
  if (!strstr(contents(&cg->declarations), wanted))
    append(cg, &cg->declarations, "%s", wanted);
}

static const char *joined(codegen *cg, const char *list, const char *separator, const char *item) {
  if (!*item)
    return list;
  return formatted(cg, "%s%s%s", list, *list ? separator : "", item);
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

static void copy_memory(codegen *cg, const char *to, int to_alignment, const char *from,
                        int from_alignment, const char *bytes, int is_volatile_copy) {
  cg->uses_memcpy = 1;
  instruction(cg,
              "call void @llvm.memcpy.p0.p0.i64(ptr align %d %s, ptr align %d %s, i64 %s, i1 %s)",
              to_alignment, to, from_alignment, from, bytes, is_volatile_copy ? "true" : "false");
}

static const char *load(codegen *cg, type_info *type, const char *address) {
  if (is_aggregate(type))
    return address;
  return compute(cg, "load %s%s, ptr %s, align %d", is_volatile(type) ? "volatile " : "",
                 llvm_type(cg, type), address, alignment_of(type));
}

static void store(codegen *cg, type_info *type, const char *v, const char *address) {
  if (is_aggregate(type)) {
    copy_memory(cg, address, alignment_of(type), v, alignment_of(type),
                formatted(cg, "%lld", size_of(type)), is_volatile(type));
    return;
  }
  instruction(cg, "store %s%s %s, ptr %s, align %d", is_volatile(type) ? "volatile " : "",
              llvm_type(cg, type), v, address, alignment_of(type));
}

static const char *byte_address(codegen *cg, const char *base, long long offset) {
  if (offset == 0)
    return base;
  return compute(cg, "getelementptr inbounds i8, ptr %s, i64 %lld", base, offset);
}

static const char *bits_constant(codegen *cg, unsigned long long low, unsigned long long high,
                                 int width) {
  if (width > 64) {
    if (width < 128)
      high &= (1ull << (width - 64)) - 1;
    return formatted(cg, "u0x%016llX%016llX", high, low);
  }
  if (width < 64) {
    unsigned long long mask = (1ull << width) - 1;
    low &= mask;
    if ((low >> (width - 1)) & 1)
      low |= ~mask;
  }
  return formatted(cg, "%lld", (long long)low);
}

static const char *integer_constant(codegen *cg, type_info *type, unsigned long long bits) {
  return bits_constant(cg, bits, 0, (int)size_of(type) * 8);
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
  if (!is_floating(type) && !is_complex(type))
    return integer_constant(cg, type, strtoull(spelling, NULL, 0));
  prim_kind prim = resolved(type)->prim;
  long double v = prim == PRIM_FLOAT    ? strtof(spelling, NULL)
                  : prim == PRIM_DOUBLE ? strtod(spelling, NULL)
                                        : strtold(spelling, NULL);
  if (!is_complex(type))
    return float_constant(cg, type, v);
  type_info *part = complex_part(type);
  const char *part_type = llvm_type(cg, part);
  return formatted(cg, "{ %s %s, %s %s }", part_type, float_constant(cg, part, 0), part_type,
                   float_constant(cg, part, v));
}

static const char *byte_string(codegen *cg, const unsigned char *bytes, long long count) {
  const char *out = "c\"";
  for (long long i = 0; i < count; i++) {
    unsigned char c = bytes[i];
    if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\')
      out = formatted(cg, "%s%c", out, c);
    else
      out = formatted(cg, "%s\\%02X", out, c);
  }
  return formatted(cg, "%s\"", out);
}

static const char *string_literal(codegen *cg, ast_node *node) {
  long long unit = size_of(resolved(node->expr_type)->ptr_to);
  long long bytes = ((long long)node->literal.length + 1) * unit;
  const char *name = formatted(cg, "@.str.%d", cg->next_string++);
  append(cg, &cg->globals, "%s = private unnamed_addr constant [%lld x i8] %s, align %lld\n", name,
         bytes, byte_string(cg, (const unsigned char *)node->literal.bytes, bytes), unit);
  return name;
}

static const char *part_of(codegen *cg, type_info *type, const char *v, int index) {
  return compute(cg, "extractvalue %s %s, %d", llvm_type(cg, type), v, index);
}

static const char *make_complex(codegen *cg, type_info *type, const char *re, const char *im) {
  const char *pair = llvm_type(cg, type);
  const char *part = llvm_type(cg, complex_part(type));
  const char *half = compute(cg, "insertvalue %s poison, %s %s, 0", pair, part, re);
  return compute(cg, "insertvalue %s %s, %s %s, 1", pair, half, part, im);
}

static const char *truth_of(codegen *cg, const char *v, type_info *type) {
  if (is_complex(type)) {
    const char *re = truth_of(cg, part_of(cg, type, v, 0), complex_part(type));
    const char *im = truth_of(cg, part_of(cg, type, v, 1), complex_part(type));
    return compute(cg, "or i1 %s, %s", re, im);
  }
  if (is_floating(type))
    return compute(cg, "fcmp une %s %s, %s", llvm_type(cg, type), v, float_constant(cg, type, 0));
  if (is_pointer(type))
    return compute(cg, "icmp ne ptr %s, null", v);
  return compute(cg, "icmp ne %s %s, 0", llvm_type(cg, type), v);
}

static const char *convert(codegen *cg, const char *v, type_info *from, type_info *to) {
  if (is_complex(to) && !is_complex(from)) {
    type_info *part = complex_part(to);
    const char *re = convert(cg, v, from, part);
    return make_complex(cg, to, re, float_constant(cg, part, 0));
  }
  if (is_complex(from) && !is_complex(to) && integer_kind(to) != PRIM_BOOL) {
    const char *re = part_of(cg, from, v, 0);
    return convert(cg, re, complex_part(from), to);
  }
  if (is_complex(from) && is_complex(to)) {
    if (resolved(from)->prim == resolved(to)->prim)
      return v;
    const char *re = part_of(cg, from, v, 0);
    re = convert(cg, re, complex_part(from), complex_part(to));
    const char *im = part_of(cg, from, v, 1);
    im = convert(cg, im, complex_part(from), complex_part(to));
    return make_complex(cg, to, re, im);
  }
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

static const char *resize(codegen *cg, const char *v, int from_bits, type_info *to) {
  int to_bits = (int)size_of(to) * 8;
  if (from_bits == to_bits)
    return v;
  if (from_bits > to_bits)
    return compute(cg, "trunc i%d %s to i%d", from_bits, v, to_bits);
  return compute(cg, "%s i%d %s to i%d", is_signed(integer_kind(to)) ? "sext" : "zext", from_bits,
                 v, to_bits);
}

static const char *narrowed(codegen *cg, const char *v, type_info *type, int width) {
  int bits = (int)size_of(type) * 8;
  if (width == bits)
    return v;
  const char *shifted = compute(cg, "shl i%d %s, %d", bits, v, bits - width);
  return compute(cg, "%s i%d %s, %d", is_signed(integer_kind(type)) ? "ashr" : "lshr", bits,
                 shifted, bits - width);
}

static const char *read_bits(codegen *cg, type_info *type, const char *address, int offset,
                             int width) {
  int unit = (offset + width + 7) / 8 * 8;
  const char *v = compute(cg, "load %si%d, ptr %s, align 1", is_volatile(type) ? "volatile " : "",
                          unit, address);
  if (unit > offset + width)
    v = compute(cg, "shl i%d %s, %d", unit, v, unit - offset - width);
  if (unit > width)
    v = compute(cg, "%s i%d %s, %d", is_signed(integer_kind(type)) ? "ashr" : "lshr", unit, v,
                unit - width);
  return resize(cg, v, unit, type);
}

static const char *write_bits(codegen *cg, type_info *type, const char *v, const char *address,
                              int offset, int width) {
  int unit = (offset + width + 7) / 8 * 8;
  int bits = (int)size_of(type) * 8;
  const char *wide = v;
  if (unit > bits)
    wide = compute(cg, "zext i%d %s to i%d", bits, v, unit);
  else if (unit < bits)
    wide = compute(cg, "trunc i%d %s to i%d", bits, v, unit);
  const char *stored = wide;
  if (width < unit) {
    unsigned long long field[2] = {0, 0};
    for (int i = 0; i < width; i++)
      field[i / 64] |= 1ull << (i % 64);
    unsigned long long kept[2] = {~0ull, ~0ull};
    for (int i = offset; i < offset + width; i++)
      kept[i / 64] &= ~(1ull << (i % 64));
    const char *old = compute(cg, "load %si%d, ptr %s, align 1",
                              is_volatile(type) ? "volatile " : "", unit, address);
    const char *masked =
        compute(cg, "and i%d %s, %s", unit, wide, bits_constant(cg, field[0], field[1], unit));
    if (offset)
      masked = compute(cg, "shl i%d %s, %d", unit, masked, offset);
    const char *cleared =
        compute(cg, "and i%d %s, %s", unit, old, bits_constant(cg, kept[0], kept[1], unit));
    stored = compute(cg, "or i%d %s, %s", unit, cleared, masked);
  }
  instruction(cg, "store %si%d %s, ptr %s, align 1", is_volatile(type) ? "volatile " : "", unit,
              stored, address);
  return narrowed(cg, v, type, width);
}

static ast_node *bitfield_member(ast_node *node) {
  if (node->type != AST_NODE_TYPE_MEMBER_ACCESS)
    return NULL;
  ast_node *member = node->member_access.member;
  return member->var_decl.bitfield_width ? member : NULL;
}

static const char *read_object(codegen *cg, ast_node *node, const char *address) {
  ast_node *field = bitfield_member(node);
  if (field)
    return read_bits(cg, node->expr_type, address, field->var_decl.bit_offset,
                     field->var_decl.bit_width);
  return load(cg, node->expr_type, address);
}

static const char *write_object(codegen *cg, ast_node *node, const char *v, const char *address) {
  ast_node *field = bitfield_member(node);
  if (field)
    return write_bits(cg, node->expr_type, v, address, field->var_decl.bit_offset,
                      field->var_decl.bit_width);
  store(cg, node->expr_type, v, address);
  return v;
}

static const char *size_slot(codegen *cg, ast_node *expression) {
  for (int i = 0; i < cg->size_count; i++) {
    if (cg->sizes[i].expression == expression)
      return cg->sizes[i].slot;
  }
  vla_size *grown = (vla_size *)realloc(cg->sizes, sizeof(vla_size) * (size_t)(cg->size_count + 1));
  if (!grown) {
    cg->failed = 1;
    return "poison";
  }
  cg->sizes = grown;
  const char *slot = formatted(cg, "%%size.%d", cg->next_name++);
  append(cg, &cg->allocas, "  %s = alloca i64, align 8\n", slot);
  grown[cg->size_count].expression = expression;
  grown[cg->size_count++].slot = slot;
  return slot;
}

static void evaluate_sizes(codegen *cg, type_info *type) {
  for (type_info *t = type; t && t->kind != TYPE_TYPEDEF; t = t->ptr_to) {
    if (t->kind == TYPE_ARRAY && t->is_vla) {
      const char *v = value(cg, t->array_size_expr);
      instruction(cg, "store i64 %s, ptr %s, align 8", v, size_slot(cg, t->array_size_expr));
    }
  }
}

static const char *runtime_size(codegen *cg, type_info *type) {
  type_info *t = resolved(type);
  if (!is_variably_sized(t))
    return formatted(cg, "%lld", size_of(t));
  const char *element = runtime_size(cg, t->ptr_to);
  const char *count =
      t->is_vla ? compute(cg, "load i64, ptr %s, align 8", size_slot(cg, t->array_size_expr))
                : formatted(cg, "%lld", t->array_size);
  return compute(cg, "mul i64 %s, %s", count, element);
}

static void push_vla(codegen *cg, ast_node *decl, const char *saved) {
  ast_node **decls =
      (ast_node **)realloc(cg->vla_decls, sizeof(ast_node *) * (size_t)(cg->vla_count + 1));
  if (decls)
    cg->vla_decls = decls;
  const char **saves =
      (const char **)realloc(cg->vla_saved, sizeof(const char *) * (size_t)(cg->vla_count + 1));
  if (saves)
    cg->vla_saved = saves;
  if (!decls || !saves) {
    cg->failed = 1;
    return;
  }
  decls[cg->vla_count] = decl;
  saves[cg->vla_count++] = saved;
}

static void restore_to(codegen *cg, int depth) {
  if (cg->vla_count > depth)
    instruction(cg, "call void @llvm.stackrestore.p0(ptr %s)", cg->vla_saved[depth]);
}

static void leave_scope(codegen *cg, int depth) {
  if (!cg->terminated)
    restore_to(cg, depth);
  cg->vla_count = depth;
}

static void note_reference(codegen *cg, symbol *sym) {
  const char *name = global_name(cg, sym);
  for (int i = 0; i < cg->referenced_count; i++) {
    if (strcmp(global_name(cg, cg->referenced[i]), name) == 0)
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

static void set_bits(unsigned char *bytes, long long position, int width, unsigned long long bits) {
  for (int i = 0; i < width; i++) {
    if ((bits >> i) & 1)
      bytes[(position + i) / 8] |= (unsigned char)(1u << ((position + i) % 8));
  }
}

static void write_floating(unsigned char *bytes, type_info *type, long double v) {
  prim_kind prim = resolved(type)->prim;
  if (prim == PRIM_FLOAT) {
    float f = (float)v;
    memcpy(bytes, &f, sizeof f);
  } else if (prim == PRIM_DOUBLE) {
    double d = (double)v;
    memcpy(bytes, &d, sizeof d);
  } else {
    memcpy(bytes, &v, 10);
  }
}

static const char *address_target(codegen *cg, constant_value *c) {
  const char *base;
  if (c->symbol)
    base = symbol_address(cg, c->symbol);
  else if (c->literal->type == AST_NODE_TYPE_STRING)
    base = string_literal(cg, c->literal);
  else
    base = compound_global(cg, c->literal);
  if (c->offset == 0)
    return formatted(cg, "ptr %s", base);
  return formatted(cg, "ptr getelementptr (i8, ptr %s, i64 %lld)", base, c->offset);
}

static void fill_entry(codegen *cg, unsigned char *bytes, initializer_entry *entry,
                       relocation *relocations, int *relocation_count) {
  constant_value *c = &entry->constant;
  unsigned char *at = bytes + entry->offset;
  if (c->kind == CONSTANT_INTEGER && entry->bit_width) {
    set_bits(bytes, entry->offset * 8 + entry->bit_offset, entry->bit_width, c->bits);
  } else if (c->kind == CONSTANT_INTEGER) {
    for (long long i = 0; i < size_of(entry->type); i++)
      at[i] = (unsigned char)(c->bits >> (8 * i));
  } else if (c->kind == CONSTANT_FLOATING && is_complex(entry->type)) {
    type_info *part = complex_part(entry->type);
    write_floating(at, part, c->real);
    write_floating(at + size_of(part), part, c->imag);
  } else if (c->kind == CONSTANT_FLOATING) {
    write_floating(at, entry->type, c->real);
  } else if (c->kind == CONSTANT_ADDRESS && (c->symbol || c->literal)) {
    relocations[*relocation_count].offset = entry->offset;
    relocations[(*relocation_count)++].target = address_target(cg, c);
  } else if (c->kind == CONSTANT_ADDRESS) {
    for (int i = 0; i < 8; i++)
      at[i] = (unsigned char)((unsigned long long)c->offset >> (8 * i));
  } else if (entry->value->type == AST_NODE_TYPE_STRING) {
    ast_node *string = entry->value;
    long long count =
        ((long long)string->literal.length + 1) * size_of(resolved(string->expr_type)->ptr_to);
    type_layout array = sema_type_layout(entry->type);
    if (array.known && array.size < count)
      count = array.size;
    memcpy(at, string->literal.bytes, (size_t)count);
  }
}

static const char *static_initializer(codegen *cg, type_info *type, initializer_layout *layout) {
  long long size = static_size(type);
  int count = layout ? layout->count : 0;
  unsigned char *bytes = (unsigned char *)calloc((size_t)size + 1, 1);
  relocation *relocations = (relocation *)calloc((size_t)count + 1, sizeof(relocation));
  if (!bytes || !relocations) {
    free(bytes);
    free(relocations);
    cg->failed = 1;
    return "";
  }
  int relocation_count = 0;
  for (int i = 0; i < count; i++)
    fill_entry(cg, bytes, &layout->entries[i], relocations, &relocation_count);
  for (int i = 1; i < relocation_count; i++) {
    for (int j = i; j > 0 && relocations[j - 1].offset > relocations[j].offset; j--) {
      relocation swap = relocations[j];
      relocations[j] = relocations[j - 1];
      relocations[j - 1] = swap;
    }
  }
  const char *out;
  if (relocation_count == 0) {
    long long nonzero = 0;
    while (nonzero < size && bytes[nonzero] == 0)
      nonzero++;
    out = nonzero == size ? formatted(cg, "[%lld x i8] zeroinitializer", size)
                          : formatted(cg, "[%lld x i8] %s", size, byte_string(cg, bytes, size));
  } else {
    const char *types = "";
    const char *values = "";
    long long at = 0;
    for (int i = 0; i <= relocation_count; i++) {
      long long end = i < relocation_count ? relocations[i].offset : size;
      if (end > at) {
        types = formatted(cg, "%s%s[%lld x i8]", types, *types ? ", " : "", end - at);
        values = formatted(cg, "%s%s[%lld x i8] %s", values, *values ? ", " : "", end - at,
                           byte_string(cg, bytes + at, end - at));
      }
      if (i < relocation_count) {
        types = formatted(cg, "%s%sptr", types, *types ? ", " : "");
        values = formatted(cg, "%s%s%s", values, *values ? ", " : "", relocations[i].target);
        at = end + 8;
      }
    }
    out = formatted(cg, "<{ %s }> <{ %s }>", types, values);
  }
  free(bytes);
  free(relocations);
  return out;
}

static void define_global(codegen *cg, const char *name, const char *linkage, type_info *type,
                          initializer_layout *layout) {
  const char *initializer = static_initializer(cg, type, layout);
  append(cg, &cg->globals, "%s = %sglobal %s, align %d\n", name, linkage, initializer,
         object_alignment(type));
}

static const char *compound_global(codegen *cg, ast_node *node) {
  const char *name = formatted(cg, "@.compound.%d", cg->next_global++);
  define_global(cg, name, "internal ", node->expr_type, &node->compound_literal.init_layout);
  return name;
}

static int is_function_name(symbol *sym) {
  return strcmp(sym->name, "__func__") == 0 || strcmp(sym->name, "__FUNCTION__") == 0 ||
         strcmp(sym->name, "__PRETTY_FUNCTION__") == 0;
}

static const char *function_name_global(codegen *cg) {
  if (cg->function_name_global)
    return cg->function_name_global;
  const char *name = formatted(cg, "@__func__.%d", cg->next_global++);
  long long length = (long long)strlen(cg->function_name) + 1;
  append(cg, &cg->globals, "%s = private unnamed_addr constant [%lld x i8] %s, align 1\n", name,
         length, byte_string(cg, (const unsigned char *)cg->function_name, length));
  cg->function_name_global = name;
  return name;
}

static void note_use(codegen *cg, symbol *sym) {
  if (sym->kind != SYMBOL_FUNC || sym->linkage != LINKAGE_INTERNAL)
    return;
  function_use *grown =
      (function_use *)realloc(cg->uses, sizeof(function_use) * (size_t)(cg->use_count + 1));
  if (!grown) {
    cg->failed = 1;
    return;
  }
  cg->uses = grown;
  grown[cg->use_count].from = cg->current_function;
  grown[cg->use_count++].to = sym;
}

static const char *symbol_address(codegen *cg, symbol *sym) {
  if (sym->kind == SYMBOL_FUNC || sym->linkage != LINKAGE_NONE) {
    note_use(cg, sym);
    note_reference(cg, sym);
    return global_name(cg, sym);
  }
  if (!sym->has_static_storage)
    return slot_name(cg, sym);
  if (is_function_name(sym))
    return function_name_global(cg);
  return ir_name(cg, '@', formatted(cg, "%s.%s.%d", cg->function_name, sym->name, sym->local_slot));
}

static const char *element_address(codegen *cg, type_info *pointer, const char *base,
                                   const char *index) {
  type_info *element = resolved(pointer)->ptr_to;
  if (is_variably_sized(element)) {
    const char *bytes = compute(cg, "mul i64 %s, %s", index, runtime_size(cg, element));
    return compute(cg, "getelementptr inbounds i8, ptr %s, i64 %s", base, bytes);
  }
  return compute(cg, "getelementptr inbounds %s, ptr %s, i64 %s", llvm_type(cg, element), base,
                 index);
}

static const char *compound_object(codegen *cg, ast_node *node) {
  type_info *type = node->expr_type;
  const char *slot = formatted(cg, "%%compound.%d", cg->next_name++);
  append(cg, &cg->allocas, "  %s = alloca %s, align %d\n", slot, llvm_type(cg, type),
         object_alignment(type));
  initialize_object(cg, type, &node->compound_literal.init_layout, slot);
  return slot;
}

static const char *address_of(codegen *cg, ast_node *node) {
  switch (node->type) {
  case AST_NODE_TYPE_IDENTIFIER:
    return symbol_address(cg, node->symbol);
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
  case AST_NODE_TYPE_MEMBER_ACCESS: {
    ast_node *left = node->member_access.left;
    const char *base =
        node->member_access.is_pointer || !left->is_lvalue ? value(cg, left) : address_of(cg, left);
    return byte_address(cg, base, node->member_access.member->var_decl.offset);
  }
  case AST_NODE_TYPE_COMPOUND_LITERAL:
    return compound_object(cg, node);
  case AST_NODE_TYPE_BUILTIN:
    return address_of(cg, node->builtin.chosen);
  default:
    return "poison";
  }
}

static const char *conversion_value(codegen *cg, ast_node *node) {
  ast_node *operand = node->conversion.operand;
  switch (node->conversion.kind) {
  case CONVERSION_LVALUE: {
    const char *address = address_of(cg, operand);
    return read_object(cg, operand, address);
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

static const char *sizeof_value(codegen *cg, ast_node *node) {
  ast_node *operand = node->unary_op.operand;
  type_info *type = sizeof_operand(node);
  if (!is_variably_sized(type))
    return integer_constant(cg, node->expr_type, (unsigned long long)size_of(type));
  if (operand->type == AST_NODE_TYPE_CAST && !operand->cast_expr.operand)
    evaluate_sizes(cg, type);
  else
    discard(cg, operand);
  return runtime_size(cg, type);
}

static const char *complex_call(codegen *cg, const char *operation, type_info *type, const char *a,
                                const char *b) {
  type_info *part = complex_part(type);
  prim_kind prim = part->prim;
  const char *name = formatted(cg, "@__%s%cc3", operation,
                               prim == PRIM_FLOAT    ? 's'
                               : prim == PRIM_DOUBLE ? 'd'
                                                     : 'x');
  const char *pair = llvm_type(cg, type);
  const char *t = llvm_type(cg, part);
  const char *parts[4];
  parts[0] = part_of(cg, type, a, 0);
  parts[1] = part_of(cg, type, a, 1);
  parts[2] = part_of(cg, type, b, 0);
  parts[3] = part_of(cg, type, b, 1);
  int windows = target_current()->windows_abi;
  if (windows && prim != PRIM_FLOAT) {
    const char *result = formatted(cg, "%%complex.%d", cg->next_name++);
    append(cg, &cg->allocas, "  %s = alloca %s, align %d\n", result, pair, alignment_of(part));
    const char *signature = "ptr";
    const char *arguments = formatted(cg, "ptr sret(%s) %s", pair, result);
    for (int i = 0; i < 4; i++) {
      const char *argument = parts[i];
      const char *kind = t;
      if (prim == PRIM_LDOUBLE) {
        argument = formatted(cg, "%%complex.%d", cg->next_name++);
        append(cg, &cg->allocas, "  %s = alloca x86_fp80, align 16\n", argument);
        instruction(cg, "store x86_fp80 %s, ptr %s, align 16", parts[i], argument);
        kind = "ptr";
      }
      signature = joined(cg, signature, ", ", kind);
      arguments = joined(cg, arguments, ", ", formatted(cg, "%s %s", kind, argument));
    }
    declare_once(cg, formatted(cg, "declare void %s(%s)", name, signature));
    instruction(cg, "call void %s(%s)", name, arguments);
    return compute(cg, "load %s, ptr %s, align %d", pair, result, alignment_of(part));
  }
  const char *returned = prim != PRIM_FLOAT ? pair : windows ? "i64" : "<2 x float>";
  declare_once(cg, formatted(cg, "declare %s %s(%s, %s, %s, %s)", returned, name, t, t, t, t));
  const char *r = compute(cg, "call %s %s(%s %s, %s %s, %s %s, %s %s)", returned, name, t, parts[0],
                          t, parts[1], t, parts[2], t, parts[3]);
  if (prim != PRIM_FLOAT)
    return r;
  if (windows)
    r = compute(cg, "bitcast i64 %s to <2 x float>", r);
  const char *re = compute(cg, "extractelement <2 x float> %s, i32 0", r);
  const char *im = compute(cg, "extractelement <2 x float> %s, i32 1", r);
  return make_complex(cg, type, re, im);
}

static const char *complex_arithmetic(codegen *cg, token_type op, type_info *type, const char *a,
                                      const char *b) {
  if (op == TOKEN_STAR || op == TOKEN_SLASH)
    return complex_call(cg, op == TOKEN_STAR ? "mul" : "div", type, a, b);
  const char *name = op == TOKEN_PLUS ? "fadd" : "fsub";
  const char *t = llvm_type(cg, complex_part(type));
  const char *ar = part_of(cg, type, a, 0);
  const char *ai = part_of(cg, type, a, 1);
  const char *br = part_of(cg, type, b, 0);
  const char *bi = part_of(cg, type, b, 1);
  const char *re = compute(cg, "%s %s %s, %s", name, t, ar, br);
  const char *im = compute(cg, "%s %s %s, %s", name, t, ai, bi);
  return make_complex(cg, type, re, im);
}

static const char *complex_negate(codegen *cg, type_info *type, const char *v) {
  const char *t = llvm_type(cg, complex_part(type));
  const char *re = part_of(cg, type, v, 0);
  const char *im = part_of(cg, type, v, 1);
  re = compute(cg, "fneg %s %s", t, re);
  im = compute(cg, "fneg %s %s", t, im);
  return make_complex(cg, type, re, im);
}

static const char *arithmetic(codegen *cg, token_type op, type_info *type, const char *a,
                              const char *b, type_info *b_type) {
  if (is_complex(type))
    return complex_arithmetic(cg, op, type, a, b);
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
  if (is_complex(type)) {
    const char *t = llvm_type(cg, complex_part(type));
    const char *ar = part_of(cg, type, a, 0);
    const char *ai = part_of(cg, type, a, 1);
    const char *br = part_of(cg, type, b, 0);
    const char *bi = part_of(cg, type, b, 1);
    const char *re = compute(cg, "fcmp %s %s %s, %s", floating[index], t, ar, br);
    const char *im = compute(cg, "fcmp %s %s %s, %s", floating[index], t, ai, bi);
    return compute(cg, "%s i1 %s, %s", index == 4 ? "and" : "or", re, im);
  }
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
    type_info *element = resolved(left)->ptr_to;
    if (is_variably_sized(element))
      return compute(cg, "sdiv exact i64 %s, %s", bytes, runtime_size(cg, element));
    long long size = size_of(element);
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
  const char *old = read_object(cg, operand, address);
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
  updated = write_object(cg, operand, updated, address);
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
    if (is_complex(type))
      return complex_negate(cg, type, v);
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
    return sizeof_value(cg, node);
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
    return write_object(cg, left, v, address);
  }
  token_type op = operator_of_assignment(node->assignment.op.type);
  type_info *computation = node->assignment.computation_type;
  const char *address = address_of(cg, left);
  const char *old = read_object(cg, left, address);
  const char *b = value(cg, right);
  const char *result;
  if (is_pointer(computation)) {
    result = pointer_arithmetic(cg, op, type, right->expr_type, old, b);
  } else {
    const char *a = convert(cg, old, type, computation);
    result = arithmetic(cg, op, computation, a, b, right->expr_type);
    result = convert(cg, result, computation, type);
  }
  return write_object(cg, left, result, address);
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
  return compute(cg, "phi %s [ %s, %%%s ], [ %s, %%%s ]", value_type(cg, node->expr_type), a, a_end,
                 b, b_end);
}

static const char *cast_value(codegen *cg, ast_node *node) {
  ast_node *operand = node->cast_expr.operand;
  evaluate_sizes(cg, node->cast_expr.type);
  if (is_void(node->expr_type)) {
    discard(cg, operand);
    return "";
  }
  const char *v = value(cg, operand);
  return convert(cg, v, operand->expr_type, node->expr_type);
}

enum { CLASS_NONE, CLASS_INTEGER, CLASS_SSE, CLASS_X87, CLASS_X87UP, CLASS_MEMORY };

static int merge_class(int a, int b) {
  if (a == b || b == CLASS_NONE)
    return a;
  if (a == CLASS_NONE)
    return b;
  if (a == CLASS_MEMORY || b == CLASS_MEMORY)
    return CLASS_MEMORY;
  if (a == CLASS_INTEGER || b == CLASS_INTEGER)
    return CLASS_INTEGER;
  if (a == CLASS_X87 || a == CLASS_X87UP || b == CLASS_X87 || b == CLASS_X87UP)
    return CLASS_MEMORY;
  return CLASS_SSE;
}

static void classify_eightbytes(type_info *type, long long offset, int classes[2]);

static void classify_member(ast_node *member, long long offset, int classes[2]) {
  if (member->type != AST_NODE_TYPE_VAR_DECL)
    return;
  if (!member->var_decl.bitfield_width) {
    classify_eightbytes(member->var_decl.type, offset + member->var_decl.offset, classes);
    return;
  }
  if (!member->var_decl.bit_width)
    return;
  long long first = (offset + member->var_decl.offset) * 8 + member->var_decl.bit_offset;
  long long last = first + member->var_decl.bit_width - 1;
  for (long long i = first / 64; i <= last / 64 && i < 2; i++)
    classes[i] = merge_class(classes[i], CLASS_INTEGER);
}

static void classify_eightbytes(type_info *type, long long offset, int classes[2]) {
  type_info *t = resolved(type);
  if (t->kind == TYPE_ARRAY) {
    long long element = size_of(t->ptr_to);
    for (long long i = 0; i < t->array_size && element > 0; i++)
      classify_eightbytes(t->ptr_to, offset + i * element, classes);
    return;
  }
  if (offset % alignment_of(t)) {
    classes[0] = classes[1] = CLASS_MEMORY;
    return;
  }
  if (is_aggregate(t)) {
    ast_node *def = t->symbol->definition;
    for (int i = 0; i < def->struct_def.member_count; i++) {
      ast_node *item = def->struct_def.members[i];
      int group = item->type == AST_NODE_TYPE_DECL_GROUP;
      for (int j = 0; j < (group ? item->block.count : 1); j++)
        classify_member(group ? item->block.statements[j] : item, offset, classes);
    }
    return;
  }
  if (is_complex(t)) {
    type_info *part = complex_part(t);
    classify_eightbytes(part, offset, classes);
    classify_eightbytes(part, offset + size_of(part), classes);
    return;
  }
  int index = (int)(offset / 8);
  if (is_floating(t) && t->prim == PRIM_LDOUBLE) {
    classes[index] = merge_class(classes[index], CLASS_X87);
    if (index == 0)
      classes[1] = merge_class(classes[1], CLASS_X87UP);
    return;
  }
  classes[index] = merge_class(classes[index], is_floating(t) ? CLASS_SSE : CLASS_INTEGER);
}

static int classify_system_v(type_info *type, int classes[2]) {
  classes[0] = classes[1] = CLASS_NONE;
  if (size_of(type) > 16)
    return 0;
  classify_eightbytes(type, 0, classes);
  if (classes[0] == CLASS_MEMORY || classes[1] == CLASS_MEMORY)
    return 0;
  return (classes[0] == CLASS_X87) == (classes[1] == CLASS_X87UP);
}

static passing pieces_of(codegen *cg, type_info *type, int classes[2]) {
  passing p = {PASS_PIECES, {NULL, NULL}, 0, ""};
  long long size = size_of(type);
  for (int i = 0; i < 2 && classes[i] != CLASS_X87UP; i++) {
    long long bytes = size - 8 * i < 8 ? size - 8 * i : 8;
    if (classes[i] == CLASS_NONE || bytes <= 0)
      continue;
    if (classes[i] == CLASS_X87)
      p.pieces[p.piece_count++] = "x86_fp80";
    else if (classes[i] == CLASS_SSE)
      p.pieces[p.piece_count++] = bytes <= 4 ? "float" : "double";
    else
      p.pieces[p.piece_count++] = formatted(cg, "i%lld", bytes * 8);
  }
  return p;
}

static const char *extension_of(type_info *type) {
  prim_kind prim = integer_kind(type);
  if (prim == PRIM_BOOL)
    return "zeroext";
  if (target_current()->windows_abi)
    return "";
  if (prim == PRIM_CHAR || prim == PRIM_SCHAR || prim == PRIM_SHORT)
    return "signext";
  if (prim == PRIM_UCHAR || prim == PRIM_USHORT)
    return "zeroext";
  return "";
}

static const char *with_extension(codegen *cg, const char *type, const char *extension,
                                  int result) {
  if (!*extension)
    return type;
  if (result)
    return formatted(cg, "%s %s", extension, type);
  return formatted(cg, "%s %s", type, extension);
}

static int is_long_double(type_info *type) {
  type_info *t = resolved(type);
  return is_floating(t) && t->prim == PRIM_LDOUBLE;
}

static passing pass_windows(codegen *cg, type_info *type) {
  passing p = {PASS_DIRECT, {NULL, NULL}, 0, extension_of(type)};
  if (!is_aggregate(type) && !is_complex(type) && !is_long_double(type))
    return p;
  long long size = size_of(type);
  if (size == 1 || size == 2 || size == 4 || size == 8) {
    p.kind = PASS_PIECES;
    p.pieces[p.piece_count++] = formatted(cg, "i%lld", size * 8);
    return p;
  }
  p.kind = PASS_POINTER;
  return p;
}

static passing pass_argument(codegen *cg, type_info *type, int *ints, int *sses) {
  passing p = {PASS_DIRECT, {NULL, NULL}, 0, extension_of(type)};
  if (target_current()->windows_abi)
    return pass_windows(cg, type);
  if (!is_aggregate(type) && !is_complex(type)) {
    int *registers = is_floating(type) ? sses : ints;
    if (!is_long_double(type) && *registers > 0)
      (*registers)--;
    return p;
  }
  int classes[2];
  if (!classify_system_v(type, classes) || classes[0] == CLASS_X87) {
    p.kind = PASS_MEMORY;
    return p;
  }
  passing pieces = pieces_of(cg, type, classes);
  int need_int = 0;
  int need_sse = 0;
  for (int i = 0; i < pieces.piece_count; i++) {
    if (pieces.pieces[i][0] == 'i')
      need_int++;
    else
      need_sse++;
  }
  if (need_int > *ints || need_sse > *sses) {
    p.kind = PASS_MEMORY;
    return p;
  }
  *ints -= need_int;
  *sses -= need_sse;
  return pieces;
}

static passing pass_result(codegen *cg, type_info *type) {
  passing p = {PASS_DIRECT, {NULL, NULL}, 0, extension_of(type)};
  if (target_current()->windows_abi)
    return pass_windows(cg, type);
  if (!is_aggregate(type) && !is_complex(type))
    return p;
  if (is_complex(type) && is_long_double(complex_part(type)))
    return p;
  int classes[2];
  if (!classify_system_v(type, classes)) {
    p.kind = PASS_POINTER;
    return p;
  }
  return pieces_of(cg, type, classes);
}

static const char *piece_type(codegen *cg, passing p) {
  if (p.piece_count == 1)
    return p.pieces[0];
  return formatted(cg, "{ %s, %s }", p.pieces[0], p.pieces[1]);
}

static const char *result_type(codegen *cg, type_info *type, passing r) {
  if (r.kind == PASS_POINTER)
    return "void";
  if (r.kind == PASS_PIECES)
    return piece_type(cg, r);
  return llvm_type(cg, type);
}

static const char *memory_parameter(codegen *cg, type_info *type, const char *attribute) {
  int alignment = alignment_of(type);
  if (strcmp(attribute, "byval") == 0 && alignment < 8)
    alignment = 8;
  return formatted(cg, "ptr %s(%s) align %d", attribute, llvm_type(cg, type), alignment);
}

static const char *lowered_parameter(codegen *cg, type_info *type, passing p, int *next) {
  const char *out = "";
  int count = p.kind == PASS_PIECES ? p.piece_count : 1;
  for (int i = 0; i < count; i++) {
    const char *piece = p.kind == PASS_PIECES    ? p.pieces[i]
                        : p.kind == PASS_POINTER ? "ptr"
                        : p.kind == PASS_MEMORY
                            ? memory_parameter(cg, type, "byval")
                            : with_extension(cg, parameter_type(cg, type), p.extension, 0);
    if (next)
      piece = formatted(cg, "%s %%%d", piece, (*next)++);
    out = joined(cg, out, ", ", piece);
  }
  return out;
}

static const char *temporary(codegen *cg, type_info *type) {
  const char *slot = formatted(cg, "%%tmp.%d", cg->next_name++);
  append(cg, &cg->allocas, "  %s = alloca %s, align %d\n", slot, llvm_type(cg, type),
         alignment_of(type));
  return slot;
}

static const char *in_memory(codegen *cg, type_info *type, const char *v) {
  if (is_aggregate(type))
    return v;
  const char *slot = temporary(cg, type);
  store(cg, type, v, slot);
  return slot;
}

static int piece_alignment(type_info *type, int index) {
  int alignment = alignment_of(type);
  return index && alignment > 8 ? 8 : alignment;
}

static const char *load_piece(codegen *cg, type_info *type, passing p, const char *address,
                              int index) {
  const char *at = byte_address(cg, address, 8 * index);
  return compute(cg, "load %s, ptr %s, align %d", p.pieces[index], at,
                 piece_alignment(type, index));
}

static void store_piece(codegen *cg, type_info *type, passing p, const char *v, const char *address,
                        int index) {
  const char *at = byte_address(cg, address, 8 * index);
  instruction(cg, "store %s %s, ptr %s, align %d", p.pieces[index], v, at,
              piece_alignment(type, index));
}

static const char *emit_call(codegen *cg, ast_node *callable, ast_node **args, int count) {
  type_info *fn = resolved(resolved(callable->expr_type)->ptr_to);
  type_info *result = fn->ptr_to;
  const char *callee = value(cg, callable);
  passing r = pass_result(cg, result);
  int ints = 6;
  int sses = 8;
  const char *signature = "";
  const char *arguments = "";
  const char *slot = NULL;
  if (r.kind == PASS_POINTER) {
    slot = temporary(cg, result);
    signature = "ptr";
    arguments = formatted(cg, "%s %s", memory_parameter(cg, result, "sret"), slot);
    ints--;
  }
  for (int i = 0; i < count; i++) {
    type_info *type = args[i]->expr_type;
    const char *v = value(cg, args[i]);
    passing p = pass_argument(cg, type, &ints, &sses);
    const char *types = "";
    const char *values = "";
    if (p.kind == PASS_DIRECT) {
      types = llvm_type(cg, type);
      values = formatted(cg, "%s %s", with_extension(cg, types, p.extension, 0), v);
    } else if (p.kind == PASS_PIECES) {
      const char *address = in_memory(cg, type, v);
      for (int k = 0; k < p.piece_count; k++) {
        const char *piece = load_piece(cg, type, p, address, k);
        types = joined(cg, types, ", ", p.pieces[k]);
        values = joined(cg, values, ", ", formatted(cg, "%s %s", p.pieces[k], piece));
      }
    } else if (p.kind == PASS_POINTER) {
      const char *copy = temporary(cg, type);
      store(cg, type, v, copy);
      types = "ptr";
      values = formatted(cg, "ptr %s", copy);
    } else {
      types = "ptr";
      values = formatted(cg, "%s %s", memory_parameter(cg, type, "byval"), in_memory(cg, type, v));
    }
    arguments = joined(cg, arguments, ", ", values);
    if (!fn->has_prototype || i < fn->param_count)
      signature = joined(cg, signature, ", ", types);
  }
  if (fn->has_prototype && fn->is_variadic)
    signature = joined(cg, signature, ", ", "...");
  ast_node *target = callable;
  while (target->type == AST_NODE_TYPE_CONVERSION)
    target = target->conversion.operand;
  const char *attributes = "";
  if (target->type == AST_NODE_TYPE_IDENTIFIER && target->symbol->returns_twice) {
    attributes = " #0";
    cg->uses_returns_twice = 1;
  }
  const char *returned = result_type(cg, result, r);
  const char *call =
      formatted(cg, "call %s (%s) %s(%s)%s", with_extension(cg, returned, r.extension, 1),
                signature, callee, arguments, attributes);
  if (r.kind == PASS_POINTER) {
    instruction(cg, "%s", call);
    return load(cg, result, slot);
  }
  if (is_void(result)) {
    instruction(cg, "%s", call);
    return "";
  }
  const char *v = compute(cg, "%s", call);
  if (r.kind == PASS_DIRECT)
    return v;
  slot = temporary(cg, result);
  if (r.piece_count == 1) {
    store_piece(cg, result, r, v, slot, 0);
  } else {
    for (int k = 0; k < r.piece_count; k++) {
      const char *piece = compute(cg, "extractvalue %s %s, %d", returned, v, k);
      store_piece(cg, result, r, piece, slot, k);
    }
  }
  return load(cg, result, slot);
}

static const char *call_value(codegen *cg, ast_node *node) {
  return emit_call(cg, node->function_call.callable, node->function_call.arguments,
                   node->function_call.arg_count);
}

static const char *offsetof_value(codegen *cg, ast_node *node) {
  const char *type = llvm_type(cg, node->expr_type);
  const char *total =
      integer_constant(cg, node->expr_type, (unsigned long long)node->builtin.value);
  for (int i = 0; i < node->builtin.step_count; i++) {
    if (!node->builtin.steps[i].scale)
      continue;
    const char *index = value(cg, node->builtin.steps[i].index);
    const char *bytes = compute(cg, "mul %s %s, %lld", type, index, node->builtin.steps[i].scale);
    total = compute(cg, "add %s %s, %s", type, total, bytes);
  }
  return total;
}

static const char *sign_bit(codegen *cg, ast_node *argument) {
  prim_kind prim = resolved(argument->expr_type)->prim;
  const char *v = value(cg, argument);
  if (prim == PRIM_FLOAT) {
    const char *bits = compute(cg, "bitcast float %s to i32", v);
    return compute(cg, "and i32 %s, -2147483648", bits);
  }
  if (prim == PRIM_DOUBLE) {
    const char *bits = compute(cg, "bitcast double %s to i64", v);
    const char *sign = compute(cg, "lshr i64 %s, 63", bits);
    return compute(cg, "trunc i64 %s to i32", sign);
  }
  const char *bits = compute(cg, "bitcast x86_fp80 %s to i80", v);
  const char *sign = compute(cg, "lshr i80 %s, 79", bits);
  const char *narrow = compute(cg, "trunc i80 %s to i32", sign);
  return compute(cg, "shl i32 %s, 9", narrow);
}

static const char *va_list_address(codegen *cg, ast_node *list) {
  if (target_current()->windows_abi)
    return address_of(cg, list);
  return value(cg, list);
}

static const char *va_arg_windows(codegen *cg, type_info *type, const char *list) {
  const char *current = compute(cg, "load ptr, ptr %s, align 8", list);
  const char *next = compute(cg, "getelementptr inbounds i8, ptr %s, i64 8", current);
  instruction(cg, "store ptr %s, ptr %s, align 8", next, list);
  if (pass_windows(cg, type).kind == PASS_POINTER)
    current = compute(cg, "load ptr, ptr %s, align 8", current);
  return load(cg, type, current);
}

static const char *va_arg_registers(codegen *cg, type_info *type, passing p, const char *list,
                                    const char *gp, const char *fp, int need_int, int need_sse) {
  const char *save_slot = byte_address(cg, list, 16);
  const char *save = compute(cg, "load ptr, ptr %s, align 8", save_slot);
  const char *address;
  if (p.kind == PASS_DIRECT || p.piece_count == 1 || need_sse == 0) {
    address = compute(cg, "getelementptr i8, ptr %s, i32 %s", save, need_int ? gp : fp);
  } else {
    address = temporary(cg, type);
    int ints = 0;
    int sses = 0;
    for (int k = 0; k < p.piece_count; k++) {
      int integer = p.pieces[k][0] == 'i';
      const char *base = compute(cg, "getelementptr i8, ptr %s, i32 %s", save, integer ? gp : fp);
      const char *source = byte_address(cg, base, integer ? 8 * ints++ : 16 * sses++);
      const char *piece = compute(cg, "load %s, ptr %s, align 8", p.pieces[k], source);
      store_piece(cg, type, p, piece, address, k);
    }
  }
  if (need_int) {
    const char *moved = compute(cg, "add i32 %s, %d", gp, 8 * need_int);
    instruction(cg, "store i32 %s, ptr %s, align 4", moved, list);
  }
  if (need_sse) {
    const char *moved = compute(cg, "add i32 %s, %d", fp, 16 * need_sse);
    instruction(cg, "store i32 %s, ptr %s, align 4", moved, byte_address(cg, list, 4));
  }
  return address;
}

static const char *va_arg_memory(codegen *cg, type_info *type, const char *list) {
  const char *overflow_slot = byte_address(cg, list, 8);
  const char *area = compute(cg, "load ptr, ptr %s, align 8", overflow_slot);
  if (alignment_of(type) > 8) {
    const char *bits = compute(cg, "ptrtoint ptr %s to i64", area);
    const char *raised = compute(cg, "add i64 %s, 15", bits);
    const char *aligned = compute(cg, "and i64 %s, -16", raised);
    area = compute(cg, "inttoptr i64 %s to ptr", aligned);
  }
  long long size = (size_of(type) + 7) / 8 * 8;
  const char *next = compute(cg, "getelementptr i8, ptr %s, i64 %lld", area, size);
  instruction(cg, "store ptr %s, ptr %s, align 8", next, overflow_slot);
  return area;
}

static const char *va_arg_system_v(codegen *cg, type_info *type, const char *list) {
  int ints = 6;
  int sses = 8;
  passing p = pass_argument(cg, type, &ints, &sses);
  int need_int = 6 - ints;
  int need_sse = 8 - sses;
  if (p.kind == PASS_MEMORY || (need_int == 0 && need_sse == 0))
    return load(cg, type, va_arg_memory(cg, type, list));
  const char *gp = NULL;
  const char *fp = NULL;
  const char *fits = NULL;
  if (need_int) {
    gp = compute(cg, "load i32, ptr %s, align 4", list);
    fits = compute(cg, "icmp ule i32 %s, %d", gp, 48 - 8 * need_int);
  }
  if (need_sse) {
    fp = compute(cg, "load i32, ptr %s, align 4", byte_address(cg, list, 4));
    const char *room = compute(cg, "icmp ule i32 %s, %d", fp, 176 - 16 * need_sse);
    fits = fits ? compute(cg, "and i1 %s, %s", fits, room) : room;
  }
  const char *in_registers = new_label(cg, "va.reg");
  const char *in_memory_label = new_label(cg, "va.mem");
  const char *end = new_label(cg, "va.end");
  branch_if(cg, fits, in_registers, in_memory_label);
  start_block(cg, in_registers);
  const char *register_address = va_arg_registers(cg, type, p, list, gp, fp, need_int, need_sse);
  const char *register_block = cg->block;
  branch(cg, end);
  start_block(cg, in_memory_label);
  const char *memory_address = va_arg_memory(cg, type, list);
  const char *memory_block = cg->block;
  branch(cg, end);
  start_block(cg, end);
  const char *address = compute(cg, "phi ptr [ %s, %%%s ], [ %s, %%%s ]", register_address,
                                register_block, memory_address, memory_block);
  return load(cg, type, address);
}

static const char *va_builtin(codegen *cg, ast_node *node) {
  ast_node **args = node->builtin.args;
  switch (node->builtin.kind) {
  case BUILTIN_VA_START: {
    const char *list = va_list_address(cg, args[0]);
    declare_once(cg, "declare void @llvm.va_start.p0(ptr)");
    instruction(cg, "call void @llvm.va_start.p0(ptr %s)", list);
    return "";
  }
  case BUILTIN_VA_END: {
    const char *list = va_list_address(cg, args[0]);
    declare_once(cg, "declare void @llvm.va_end.p0(ptr)");
    instruction(cg, "call void @llvm.va_end.p0(ptr %s)", list);
    return "";
  }
  case BUILTIN_VA_COPY: {
    const char *to = va_list_address(cg, args[0]);
    const char *from = value(cg, args[1]);
    if (target_current()->windows_abi) {
      instruction(cg, "store ptr %s, ptr %s, align 8", from, to);
      return "";
    }
    declare_once(cg, "declare void @llvm.va_copy.p0(ptr, ptr)");
    instruction(cg, "call void @llvm.va_copy.p0(ptr %s, ptr %s)", to, from);
    return "";
  }
  default: {
    const char *list = va_list_address(cg, args[0]);
    if (target_current()->windows_abi)
      return va_arg_windows(cg, node->expr_type, list);
    return va_arg_system_v(cg, node->expr_type, list);
  }
  }
}

static const char *sync_builtin(codegen *cg, ast_node *node) {
  static const char *const operations[] = {"add", "sub", "or", "and", "xor", "nand"};
  builtin_kind kind = node->builtin.kind;
  ast_node **args = node->builtin.args;
  if (kind == BUILTIN_SYNC_SYNCHRONIZE) {
    instruction(cg, "fence seq_cst");
    return "";
  }
  type_info *target = resolved(resolved(args[0]->expr_type)->ptr_to);
  const char *type = llvm_type(cg, target);
  const char *address = value(cg, args[0]);
  if (kind == BUILTIN_SYNC_LOCK_RELEASE) {
    instruction(cg, "store atomic %s %s, ptr %s release, align %lld", type,
                is_pointer(target) ? "null" : "0", address, size_of(target));
    return "";
  }
  const char *operand = value(cg, args[1]);
  if (kind <= BUILTIN_SYNC_NAND_AND_FETCH) {
    int index = (int)(kind - BUILTIN_SYNC_FETCH_AND_ADD) % 6;
    const char *old = compute(cg, "atomicrmw %s ptr %s, %s %s seq_cst", operations[index], address,
                              type, operand);
    if (kind < BUILTIN_SYNC_ADD_AND_FETCH)
      return old;
    if (kind == BUILTIN_SYNC_NAND_AND_FETCH)
      return compute(cg, "xor %s %s, -1", type, compute(cg, "and %s %s, %s", type, old, operand));
    return compute(cg, "%s %s %s, %s", operations[index], type, old, operand);
  }
  if (kind == BUILTIN_SYNC_LOCK_TEST_AND_SET)
    return compute(cg, "atomicrmw xchg ptr %s, %s %s acquire", address, type, operand);
  const char *replacement = value(cg, args[2]);
  const char *pair = compute(cg, "cmpxchg ptr %s, %s %s, %s %s seq_cst seq_cst", address, type,
                             operand, type, replacement);
  if (kind == BUILTIN_SYNC_VAL_COMPARE_AND_SWAP)
    return compute(cg, "extractvalue { %s, i1 } %s, 0", type, pair);
  return compute(cg, "zext i1 %s to i8", compute(cg, "extractvalue { %s, i1 } %s, 1", type, pair));
}

static const char *builtin_value(codegen *cg, ast_node *node) {
  static const char *const orders[] = {"ogt", "oge", "olt", "ole", "one", "uno"};
  ast_node **args = node->builtin.args;
  type_info *type = node->expr_type;
  switch (node->builtin.kind) {
  case BUILTIN_OFFSETOF:
    return offsetof_value(cg, node);
  case BUILTIN_TYPES_COMPATIBLE_P:
    return integer_constant(cg, type, (unsigned long long)node->builtin.value);
  case BUILTIN_CHOOSE_EXPR:
    if (is_void(type)) {
      discard(cg, node->builtin.chosen);
      return "";
    }
    return value(cg, node->builtin.chosen);
  case BUILTIN_HUGE_VAL:
  case BUILTIN_HUGE_VALF:
  case BUILTIN_HUGE_VALL:
  case BUILTIN_INFF:
    return float_constant(cg, type, INFINITY);
  case BUILTIN_NANF:
    discard(cg, args[0]);
    return float_constant(cg, type, NAN);
  case BUILTIN_ISGREATER:
  case BUILTIN_ISGREATEREQUAL:
  case BUILTIN_ISLESS:
  case BUILTIN_ISLESSEQUAL:
  case BUILTIN_ISLESSGREATER:
  case BUILTIN_ISUNORDERED: {
    const char *a = value(cg, args[0]);
    const char *b = value(cg, args[1]);
    const char *result =
        compute(cg, "fcmp %s %s %s, %s", orders[node->builtin.kind - BUILTIN_ISGREATER],
                llvm_type(cg, args[0]->expr_type), a, b);
    return compute(cg, "zext i1 %s to %s", result, llvm_type(cg, type));
  }
  case BUILTIN_SIGNBIT:
  case BUILTIN_SIGNBITF:
  case BUILTIN_SIGNBITL:
    return sign_bit(cg, args[0]);
  case BUILTIN_LLABS: {
    const char *v = value(cg, args[0]);
    const char *negative = compute(cg, "icmp slt i64 %s, 0", v);
    const char *negated = compute(cg, "sub i64 0, %s", v);
    return compute(cg, "select i1 %s, i64 %s, i64 %s", negative, negated, v);
  }
  case BUILTIN_VA_START:
  case BUILTIN_VA_END:
  case BUILTIN_VA_COPY:
  case BUILTIN_VA_ARG:
    return va_builtin(cg, node);
  case BUILTIN_TRAP:
    declare_once(cg, "declare void @llvm.trap()");
    instruction(cg, "call void @llvm.trap()");
    terminate(cg, "unreachable");
    return "";
  case BUILTIN_UNREACHABLE:
    terminate(cg, "unreachable");
    return "";
  case BUILTIN_TGMATH: {
    int functions = (int)node->builtin.value;
    return emit_call(cg, node->builtin.chosen, args + functions,
                     node->builtin.arg_count - functions);
  }
  case BUILTIN_SYNC_FETCH_AND_ADD:
  case BUILTIN_SYNC_FETCH_AND_SUB:
  case BUILTIN_SYNC_FETCH_AND_OR:
  case BUILTIN_SYNC_FETCH_AND_AND:
  case BUILTIN_SYNC_FETCH_AND_XOR:
  case BUILTIN_SYNC_FETCH_AND_NAND:
  case BUILTIN_SYNC_ADD_AND_FETCH:
  case BUILTIN_SYNC_SUB_AND_FETCH:
  case BUILTIN_SYNC_OR_AND_FETCH:
  case BUILTIN_SYNC_AND_AND_FETCH:
  case BUILTIN_SYNC_XOR_AND_FETCH:
  case BUILTIN_SYNC_NAND_AND_FETCH:
  case BUILTIN_SYNC_BOOL_COMPARE_AND_SWAP:
  case BUILTIN_SYNC_VAL_COMPARE_AND_SWAP:
  case BUILTIN_SYNC_LOCK_TEST_AND_SET:
  case BUILTIN_SYNC_LOCK_RELEASE:
  case BUILTIN_SYNC_SYNCHRONIZE:
    return sync_builtin(cg, node);
  case BUILTIN_UMULH:
  case BUILTIN_MULH: {
    const char *widen = node->builtin.kind == BUILTIN_UMULH ? "zext" : "sext";
    const char *a = compute(cg, "%s i64 %s to i128", widen, value(cg, args[0]));
    const char *b = compute(cg, "%s i64 %s to i128", widen, value(cg, args[1]));
    const char *product = compute(cg, "mul i128 %s, %s", a, b);
    return compute(cg, "trunc i128 %s to i64", compute(cg, "lshr i128 %s, 64", product));
  }
  case BUILTIN_IA32_SFENCE:
    declare_once(cg, "declare void @llvm.x86.sse.sfence()");
    instruction(cg, "call void @llvm.x86.sse.sfence()");
    return "";
  default:
    return "poison";
  }
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
  case AST_NODE_TYPE_MEMBER_ACCESS: {
    const char *address = address_of(cg, node);
    return read_object(cg, node, address);
  }
  case AST_NODE_TYPE_BUILTIN:
    return builtin_value(cg, node);
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
  copy_memory(cg, address, alignment_of(entry->type), source, (int)unit,
              formatted(cg, "%lld", bytes), 0);
}

static void initialize_object(codegen *cg, type_info *type, initializer_layout *layout,
                              const char *base) {
  if (!is_scalar(type)) {
    cg->uses_memset = 1;
    instruction(cg, "call void @llvm.memset.p0.i64(ptr align %d %s, i8 0, i64 %lld, i1 false)",
                alignment_of(type), base, size_of(type));
  }
  for (int i = 0; i < layout->count; i++) {
    initializer_entry *entry = &layout->entries[i];
    const char *address = byte_address(cg, base, entry->offset);
    if (entry->value->type == AST_NODE_TYPE_STRING) {
      copy_string(cg, entry, address);
      continue;
    }
    const char *v = value(cg, entry->value);
    if (entry->bit_width)
      write_bits(cg, entry->type, v, address, entry->bit_offset, entry->bit_width);
    else
      store(cg, entry->type, v, address);
  }
}

static int is_vla_object(ast_node *decl) {
  symbol *sym = decl->symbol;
  return sym && sym->kind == SYMBOL_VAR && !sym->has_static_storage && is_variably_sized(sym->type);
}

static void local_declaration(codegen *cg, ast_node *decl) {
  symbol *sym = decl->symbol;
  if (!sym)
    return;
  if (sym->kind == SYMBOL_TYPEDEF)
    evaluate_sizes(cg, decl->var_decl.type);
  if (sym->kind != SYMBOL_VAR || sym->linkage != LINKAGE_NONE)
    return;
  if (sym->has_static_storage) {
    sym->local_slot = cg->next_global++;
    define_global(cg, symbol_address(cg, sym), "internal ", sym->type,
                  decl->var_decl.init_value ? &decl->var_decl.init_layout : NULL);
    return;
  }
  evaluate_sizes(cg, decl->var_decl.type);
  sym->local_slot = cg->next_name++;
  const char *slot = slot_name(cg, sym);
  if (is_vla_object(decl)) {
    const char *size = runtime_size(cg, sym->type);
    const char *saved = compute(cg, "call ptr @llvm.stacksave.p0()");
    cg->uses_stacksave = 1;
    push_vla(cg, decl, saved);
    instruction(cg, "%s = alloca i8, i64 %s, align %d", slot, size, element_alignment(sym->type));
    return;
  }
  append(cg, &cg->allocas, "  %s = alloca %s, align %d\n", slot, llvm_type(cg, sym->type),
         object_alignment(sym->type));
  if (decl->var_decl.init_value)
    initialize_object(cg, sym->type, &decl->var_decl.init_layout, slot);
}

static void loop_body(codegen *cg, ast_node *body, const char *break_label,
                      const char *continue_label) {
  const char *outer_break = cg->break_label;
  const char *outer_continue = cg->continue_label;
  int outer_break_depth = cg->break_depth;
  int outer_continue_depth = cg->continue_depth;
  cg->break_label = break_label;
  cg->continue_label = continue_label;
  cg->break_depth = cg->vla_count;
  cg->continue_depth = cg->vla_count;
  statement(cg, body);
  cg->break_label = outer_break;
  cg->continue_label = outer_continue;
  cg->break_depth = outer_break_depth;
  cg->continue_depth = outer_continue_depth;
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
  int depth = cg->vla_count;
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
  leave_scope(cg, depth);
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
  const char *outer_break = cg->break_label;
  int outer_break_depth = cg->break_depth;
  cg->current_switch = &context;
  cg->break_label = end;
  cg->break_depth = cg->vla_count;
  statement(cg, node->switch_stmt.body);
  cg->current_switch = outer;
  cg->break_label = outer_break;
  cg->break_depth = outer_break_depth;
  free(context.cases);
  start_block(cg, end);
}

static int label_index(codegen *cg, const char *name) {
  for (int i = 0; i < cg->label_count; i++) {
    if (strcmp(cg->labels[i].name, name) == 0)
      return i;
  }
  named_label *grown =
      (named_label *)realloc(cg->labels, sizeof(named_label) * (size_t)(cg->label_count + 1));
  if (!grown) {
    cg->failed = 1;
    return -1;
  }
  cg->labels = grown;
  grown[cg->label_count].name = name;
  grown[cg->label_count].label = NULL;
  grown[cg->label_count].scope = NULL;
  grown[cg->label_count].scope_count = 0;
  return cg->label_count++;
}

static const char *user_label(codegen *cg, const char *name) {
  int i = label_index(cg, name);
  if (i < 0)
    return "";
  if (!cg->labels[i].label)
    cg->labels[i].label = new_label(cg, "label");
  return cg->labels[i].label;
}

static void scan_labels(codegen *cg, ast_node *node) {
  if (!node)
    return;
  int depth = cg->vla_count;
  switch (node->type) {
  case AST_NODE_TYPE_BLOCK:
  case AST_NODE_TYPE_DECL_GROUP:
    for (int i = 0; i < node->block.count; i++)
      scan_labels(cg, node->block.statements[i]);
    if (node->type == AST_NODE_TYPE_BLOCK)
      cg->vla_count = depth;
    break;
  case AST_NODE_TYPE_VAR_DECL:
    if (is_vla_object(node))
      push_vla(cg, node, NULL);
    break;
  case AST_NODE_TYPE_IF:
    scan_labels(cg, node->if_stmt.then_branch);
    scan_labels(cg, node->if_stmt.else_branch);
    break;
  case AST_NODE_TYPE_WHILE:
    scan_labels(cg, node->while_stmt.body);
    break;
  case AST_NODE_TYPE_DO_WHILE:
    scan_labels(cg, node->do_while_stmt.body);
    break;
  case AST_NODE_TYPE_FOR:
    scan_labels(cg, node->for_stmt.init);
    scan_labels(cg, node->for_stmt.body);
    cg->vla_count = depth;
    break;
  case AST_NODE_TYPE_SWITCH:
    scan_labels(cg, node->switch_stmt.body);
    break;
  case AST_NODE_TYPE_CASE:
    scan_labels(cg, node->case_stmt.body);
    break;
  case AST_NODE_TYPE_DEFAULT:
    scan_labels(cg, node->default_stmt.body);
    break;
  case AST_NODE_TYPE_LABEL: {
    int i = label_index(cg, node->label_stmt.label_name);
    if (i >= 0 && cg->vla_count > 0) {
      cg->labels[i].scope = (ast_node **)malloc(sizeof(ast_node *) * (size_t)cg->vla_count);
      if (cg->labels[i].scope) {
        memcpy(cg->labels[i].scope, cg->vla_decls, sizeof(ast_node *) * (size_t)cg->vla_count);
        cg->labels[i].scope_count = cg->vla_count;
      }
    }
    scan_labels(cg, node->label_stmt.statement);
    break;
  }
  default:
    break;
  }
}

static void goto_label(codegen *cg, const char *name) {
  const char *label = user_label(cg, name);
  named_label *target = &cg->labels[label_index(cg, name)];
  int common = 0;
  while (common < cg->vla_count && common < target->scope_count &&
         cg->vla_decls[common] == target->scope[common])
    common++;
  restore_to(cg, common);
  branch(cg, label);
}

static void return_statement(codegen *cg, ast_node *node) {
  ast_node *result = node->return_stmt.return_value;
  if (!result) {
    terminate(cg, "ret void");
    return;
  }
  type_info *type = result->expr_type;
  const char *v = value(cg, result);
  passing r = cg->result;
  if (r.kind == PASS_POINTER) {
    store(cg, type, v, cg->result_slot);
    terminate(cg, "ret void");
    return;
  }
  if (r.kind == PASS_DIRECT) {
    terminate(cg, "ret %s %s", llvm_type(cg, type), v);
    return;
  }
  const char *address = in_memory(cg, type, v);
  const char *returned = piece_type(cg, r);
  const char *out = load_piece(cg, type, r, address, 0);
  if (r.piece_count == 2) {
    const char *second = load_piece(cg, type, r, address, 1);
    const char *half = compute(cg, "insertvalue %s poison, %s %s, 0", returned, r.pieces[0], out);
    out = compute(cg, "insertvalue %s %s, %s %s, 1", returned, half, r.pieces[1], second);
  }
  terminate(cg, "ret %s %s", returned, out);
}

static const char *asm_template(codegen *cg, ast_node *node) {
  const char *in = node->asm_stmt.template_text->literal.bytes;
  int length = node->asm_stmt.template_text->literal.length;
  const char *out = "";
  for (int i = 0; i < length; i++) {
    char c = in[i];
    char next = i + 1 < length ? in[i + 1] : '\0';
    char after = i + 2 < length ? in[i + 2] : '\0';
    if (c == '$') {
      out = formatted(cg, "%s$$", out);
    } else if (c != '%' || node->asm_stmt.is_basic) {
      out = formatted(cg, "%s%c", out, c);
    } else if (next == '%') {
      out = formatted(cg, "%s%%", out);
      i++;
    } else if (next == '=') {
      out = formatted(cg, "%s${:uid}", out);
      i++;
    } else if (isdigit((unsigned char)next)) {
      out = formatted(cg, "%s$%c", out, next);
      for (i++; i + 1 < length && isdigit((unsigned char)in[i + 1]); i++)
        out = formatted(cg, "%s%c", out, in[i + 1]);
    } else if (isalpha((unsigned char)next) && isdigit((unsigned char)after)) {
      out = formatted(cg, "%s${%c", out, after);
      for (i += 2; i + 1 < length && isdigit((unsigned char)in[i + 1]); i++)
        out = formatted(cg, "%s%c", out, in[i + 1]);
      out = formatted(cg, "%s:%c}", out, next);
    } else {
      out = formatted(cg, "%s%%", out);
    }
  }
  return byte_string(cg, (const unsigned char *)out, (long long)strlen(out)) + 1;
}

static const char *asm_constraint(codegen *cg, const char *text) {
  const char *out = "";
  for (; *text; text++) {
    switch (*text) {
    case 'g':
      out = formatted(cg, "%simr", out);
      break;
    case 'a':
      out = formatted(cg, "%s{ax}", out);
      break;
    case 'b':
      out = formatted(cg, "%s{bx}", out);
      break;
    case 'c':
      out = formatted(cg, "%s{cx}", out);
      break;
    case 'd':
      out = formatted(cg, "%s{dx}", out);
      break;
    case 'S':
      out = formatted(cg, "%s{si}", out);
      break;
    case 'D':
      out = formatted(cg, "%s{di}", out);
      break;
    default:
      out = formatted(cg, "%s%c", out, *text);
      break;
    }
  }
  return out;
}

static const char *asm_input(codegen *cg, ast_node *operand) {
  type_info *t = resolved(operand->expr_type);
  if (t->kind == TYPE_ARRAY || t->kind == TYPE_FUNCTION)
    return address_of(cg, operand);
  if (!operand->is_lvalue)
    return value(cg, operand);
  const char *address = address_of(cg, operand);
  return read_object(cg, operand, address);
}

static void asm_statement(codegen *cg, ast_node *node) {
  int outputs = node->asm_stmt.output_count;
  const char **addresses = (const char **)calloc((size_t)outputs + 1, sizeof(const char *));
  if (!addresses) {
    cg->failed = 1;
    return;
  }
  const char *constraints = "";
  const char *arguments = "";
  const char *results = "";
  const char *tied = "";
  const char *tied_arguments = "";
  int result_count = 0;
  for (int i = 0; i < outputs; i++) {
    ast_node *operand = node->asm_stmt.operands[i];
    const char *text = node->asm_stmt.constraints[i]->literal.bytes;
    int in_out = *text == '+';
    const char *early = "";
    text++;
    if (*text == '&') {
      early = "&";
      text++;
    }
    const char *type = llvm_type(cg, operand->expr_type);
    const char *address = address_of(cg, operand);
    if (strcmp(text, "m") == 0) {
      const char *pointer = formatted(cg, "ptr elementtype(%s) %s", type, address);
      constraints = joined(cg, constraints, ",", formatted(cg, "=%s*m", early));
      arguments = joined(cg, arguments, ", ", pointer);
      if (in_out) {
        tied = joined(cg, tied, ",", "*m");
        tied_arguments = joined(cg, tied_arguments, ", ", pointer);
      }
      continue;
    }
    addresses[i] = address;
    constraints =
        joined(cg, constraints, ",", formatted(cg, "=%s%s", early, asm_constraint(cg, text)));
    results = joined(cg, results, ", ", type);
    result_count++;
    if (in_out) {
      const char *v = read_object(cg, operand, address);
      tied = joined(cg, tied, ",", formatted(cg, "%d", i));
      tied_arguments = joined(cg, tied_arguments, ", ", formatted(cg, "%s %s", type, v));
    }
  }
  for (int i = outputs; i < node->asm_stmt.operand_count; i++) {
    ast_node *operand = node->asm_stmt.operands[i];
    const char *text = node->asm_stmt.constraints[i]->literal.bytes;
    if (strcmp(text, "m") == 0) {
      const char *address = address_of(cg, operand);
      constraints = joined(cg, constraints, ",", "*m");
      arguments = joined(
          cg, arguments, ", ",
          formatted(cg, "ptr elementtype(%s) %s", llvm_type(cg, operand->expr_type), address));
      continue;
    }
    const char *v = asm_input(cg, operand);
    constraints = joined(cg, constraints, ",", asm_constraint(cg, text));
    arguments = joined(cg, arguments, ", ",
                       formatted(cg, "%s %s", parameter_type(cg, operand->expr_type), v));
  }
  constraints = joined(cg, constraints, ",", tied);
  arguments = joined(cg, arguments, ", ", tied_arguments);
  for (int i = 0; i < node->asm_stmt.clobber_count; i++) {
    const char *name = node->asm_stmt.clobbers[i]->literal.bytes;
    if (*name == '%')
      name++;
    constraints = joined(cg, constraints, ",", formatted(cg, "~{%s}", name));
  }
  constraints = joined(cg, constraints, ",", "~{dirflag},~{fpsr},~{flags}");
  const char *returned = result_count == 0   ? "void"
                         : result_count == 1 ? results
                                             : formatted(cg, "{ %s }", results);
  const char *call = formatted(cg, "call %s asm %s%s, \"%s\"(%s)", returned,
                               node->asm_stmt.is_volatile || outputs == 0 ? "sideeffect " : "",
                               asm_template(cg, node), constraints, arguments);
  if (result_count == 0) {
    instruction(cg, "%s", call);
    free(addresses);
    return;
  }
  const char *r = compute(cg, "%s", call);
  int k = 0;
  for (int i = 0; i < outputs; i++) {
    if (!addresses[i])
      continue;
    const char *v = result_count == 1 ? r : compute(cg, "extractvalue %s %s, %d", returned, r, k);
    write_object(cg, node->asm_stmt.operands[i], v, addresses[i]);
    k++;
  }
  free(addresses);
}

static void statement(codegen *cg, ast_node *node) {
  if (!node)
    return;
  switch (node->type) {
  case AST_NODE_TYPE_BLOCK: {
    int depth = cg->vla_count;
    for (int i = 0; i < node->block.count; i++)
      statement(cg, node->block.statements[i]);
    leave_scope(cg, depth);
    break;
  }
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
    restore_to(cg, cg->break_depth);
    branch(cg, cg->break_label);
    break;
  case AST_NODE_TYPE_CONTINUE:
    restore_to(cg, cg->continue_depth);
    branch(cg, cg->continue_label);
    break;
  case AST_NODE_TYPE_GOTO:
    goto_label(cg, node->goto_stmt.label_name);
    break;
  case AST_NODE_TYPE_LABEL:
    start_block(cg, user_label(cg, node->label_stmt.label_name));
    statement(cg, node->label_stmt.statement);
    break;
  case AST_NODE_TYPE_RETURN:
    return_statement(cg, node);
    break;
  case AST_NODE_TYPE_ASM:
    asm_statement(cg, node);
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
  passing *passings = (passing *)calloc((size_t)count + 1, sizeof(passing));
  if (!passings) {
    cg->failed = 1;
    return;
  }
  clear(&cg->allocas);
  clear(&cg->body);
  cg->current_function = fn->symbol;
  cg->result = pass_result(cg, result);
  cg->result_slot = cg->result.kind == PASS_POINTER ? "%0" : NULL;
  int ints = cg->result.kind == PASS_POINTER ? 5 : 6;
  int sses = 8;
  int lowered = cg->result.kind == PASS_POINTER;
  for (int i = 0; i < count; i++) {
    symbol *sym = fn->function_def.param_symbols[i];
    type_info *passed = type->has_prototype ? sym->type : promoted_parameter(sym->type);
    passings[i] = pass_argument(cg, passed, &ints, &sses);
    lowered += passings[i].kind == PASS_PIECES ? passings[i].piece_count : 1;
  }
  cg->next_temp = lowered;
  cg->next_name = 0;
  cg->block = "entry";
  cg->terminated = 0;
  free_labels(cg);
  cg->vla_count = 0;
  cg->size_count = 0;
  cg->function_name = fn->function_def.name;
  cg->function_name_global = NULL;
  scan_labels(cg, fn->function_def.body);
  cg->vla_count = 0;
  const char *parameters =
      cg->result_slot ? formatted(cg, "%s %%0", memory_parameter(cg, result, "sret")) : "";
  int next = cg->result_slot != NULL;
  for (int i = 0; i < count; i++) {
    symbol *sym = fn->function_def.param_symbols[i];
    type_info *passed = type->has_prototype ? sym->type : promoted_parameter(sym->type);
    passing p = passings[i];
    int first = next;
    parameters = joined(cg, parameters, ", ", lowered_parameter(cg, passed, p, &next));
    sym->local_slot = cg->next_name++;
    const char *slot = slot_name(cg, sym);
    append(cg, &cg->allocas, "  %s = alloca %s, align %d\n", slot, llvm_type(cg, sym->type),
           alignment_of(sym->type));
    if (p.kind == PASS_DIRECT) {
      const char *incoming = convert(cg, formatted(cg, "%%%d", first), passed, sym->type);
      store(cg, sym->type, incoming, slot);
    } else if (p.kind == PASS_PIECES) {
      for (int k = 0; k < p.piece_count; k++)
        store_piece(cg, sym->type, p, formatted(cg, "%%%d", first + k), slot, k);
    } else {
      const char *incoming = load(cg, sym->type, formatted(cg, "%%%d", first));
      store(cg, sym->type, incoming, slot);
    }
  }
  free(passings);
  for (int i = 0; i < count; i++)
    evaluate_sizes(cg, type->param_types[i]);
  if (type->is_variadic)
    parameters = joined(cg, parameters, ", ", "...");
  statement(cg, fn->function_def.body);
  if (!cg->terminated && (is_void(result) || cg->result.kind == PASS_POINTER))
    terminate(cg, "ret void");
  else if (!cg->terminated && is_main(fn))
    terminate(cg, "ret i32 0");
  else if (!cg->terminated)
    terminate(cg, "ret %s poison", result_type(cg, result, cg->result));
  function_span span = {fn->symbol, cg->functions.length, 0};
  append(cg, &cg->functions, "define %s%s %s(%s) {\nentry:\n%s%s}\n\n",
         fn->symbol->linkage == LINKAGE_INTERNAL ? "internal " : "",
         with_extension(cg, result_type(cg, result, cg->result), cg->result.extension, 1),
         global_name(cg, fn->symbol), parameters, contents(&cg->allocas), contents(&cg->body));
  span.end = cg->functions.length;
  cg->current_function = NULL;
  function_span *grown =
      (function_span *)realloc(cg->spans, sizeof(function_span) * (size_t)(cg->span_count + 1));
  if (!grown) {
    cg->failed = 1;
    return;
  }
  cg->spans = grown;
  grown[cg->span_count++] = span;
}

static int is_defined_here(codegen *cg, const char *name) {
  for (int i = 0; i < cg->defined_count; i++) {
    if (strcmp(cg->defined[i], name) == 0)
      return 1;
  }
  return 0;
}

static void declare_symbol(codegen *cg, text *out, symbol *sym) {
  type_info *type = resolved(sym->type);
  const char *import = sym->dllimport ? "dllimport " : "";
  if (sym->kind != SYMBOL_FUNC) {
    append(cg, out, "%s = external %sglobal i8\n", global_name(cg, sym), import);
    return;
  }
  passing r = pass_result(cg, type->ptr_to);
  int ints = r.kind == PASS_POINTER ? 5 : 6;
  int sses = 8;
  const char *parameters = r.kind == PASS_POINTER ? memory_parameter(cg, type->ptr_to, "sret") : "";
  for (int i = 0; type->has_prototype && i < type->param_count; i++) {
    passing p = pass_argument(cg, type->param_types[i], &ints, &sses);
    parameters = joined(cg, parameters, ", ", lowered_parameter(cg, type->param_types[i], p, NULL));
  }
  if (!type->has_prototype || type->is_variadic)
    parameters = joined(cg, parameters, ", ", "...");
  const char *attributes = "";
  if (sym->returns_twice) {
    attributes = " #0";
    cg->uses_returns_twice = 1;
  }
  append(cg, out, "declare %s%s %s(%s)%s\n", import,
         with_extension(cg, result_type(cg, type->ptr_to, r), r.extension, 1), global_name(cg, sym),
         parameters, attributes);
}

static int is_emitted_function(ast_node *item) {
  return item->type == AST_NODE_TYPE_FUNCTION_DEF && !item->function_def.is_inline_definition;
}

static int is_global_definition(ast_node *decl) {
  symbol *sym = decl->symbol;
  return decl->type == AST_NODE_TYPE_VAR_DECL && sym && sym->kind == SYMBOL_VAR &&
         sym->definition == decl;
}

static void collect_definitions(codegen *cg, ast_node *item) {
  if (item->type == AST_NODE_TYPE_DECL_GROUP) {
    for (int i = 0; i < item->block.count; i++)
      collect_definitions(cg, item->block.statements[i]);
  } else if (is_emitted_function(item) || is_global_definition(item)) {
    const char **grown =
        (const char **)realloc(cg->defined, sizeof(const char *) * (size_t)(cg->defined_count + 1));
    if (!grown) {
      cg->failed = 1;
      return;
    }
    cg->defined = grown;
    grown[cg->defined_count++] = global_name(cg, item->symbol);
  }
}

static void file_scope_item(codegen *cg, ast_node *item) {
  if (item->type == AST_NODE_TYPE_DECL_GROUP) {
    for (int i = 0; i < item->block.count; i++)
      file_scope_item(cg, item->block.statements[i]);
  } else if (is_emitted_function(item)) {
    function_definition(cg, item);
  } else if (is_global_definition(item)) {
    symbol *sym = item->symbol;
    define_global(cg, global_name(cg, sym), sym->linkage == LINKAGE_INTERNAL ? "internal " : "",
                  sym->type, item->var_decl.init_value ? &item->var_decl.init_layout : NULL);
  }
}

static void append_reachable(codegen *cg, text *out) {
  int *kept = (int *)calloc((size_t)cg->span_count + 1, sizeof(int));
  if (!kept) {
    cg->failed = 1;
    return;
  }
  for (int i = 0; i < cg->span_count; i++)
    kept[i] = cg->spans[i].sym->linkage != LINKAGE_INTERNAL;
  for (int changed = 1; changed;) {
    changed = 0;
    for (int u = 0; u < cg->use_count; u++) {
      int from = cg->uses[u].from == NULL;
      for (int i = 0; !from && i < cg->span_count; i++)
        from = kept[i] && cg->spans[i].sym == cg->uses[u].from;
      for (int i = 0; from && i < cg->span_count; i++) {
        if (!kept[i] && cg->spans[i].sym == cg->uses[u].to) {
          kept[i] = 1;
          changed = 1;
        }
      }
    }
  }
  for (int i = 0; i < cg->span_count; i++) {
    if (kept[i])
      append(cg, out, "%.*s", (int)(cg->spans[i].end - cg->spans[i].start),
             cg->functions.data + cg->spans[i].start);
  }
  free(kept);
}

char *codegen_program(ast_node *program) {
  codegen cg;
  memset(&cg, 0, sizeof cg);
  ast_node **items = program->program.declarations;
  int count = program->program.count;
  for (int i = 0; i < count; i++)
    collect_definitions(&cg, items[i]);
  for (int i = 0; i < count; i++)
    file_scope_item(&cg, items[i]);
  text out = {NULL, 0, 0};
  const target *t = target_current();
  append(&cg, &out, "target datalayout = \"%s\"\ntarget triple = \"%s\"\n\n", t->data_layout,
         t->triple);
  if (cg.globals.length)
    append(&cg, &out, "%s\n", contents(&cg.globals));
  append_reachable(&cg, &out);
  for (int i = 0; i < cg.referenced_count; i++) {
    if (!is_defined_here(&cg, global_name(&cg, cg.referenced[i])))
      declare_symbol(&cg, &out, cg.referenced[i]);
  }
  if (cg.uses_memset)
    append(&cg, &out, "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)\n");
  if (cg.uses_memcpy)
    append(&cg, &out, "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n");
  if (cg.uses_stacksave)
    append(&cg, &out,
           "declare ptr @llvm.stacksave.p0()\ndeclare void @llvm.stackrestore.p0(ptr)\n");
  append(&cg, &out, "%s", contents(&cg.declarations));
  if (cg.uses_returns_twice)
    append(&cg, &out, "\nattributes #0 = { returns_twice }\n");
  int failed = cg.failed;
  free_codegen(&cg);
  if (failed) {
    free(out.data);
    return NULL;
  }
  return out.data;
}
