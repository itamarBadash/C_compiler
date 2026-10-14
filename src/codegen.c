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
  int uses_stacksave;
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
  free(cg->defined);
  free(cg->referenced);
  free_labels(cg);
  free(cg->labels);
  free(cg->vla_decls);
  free(cg->vla_saved);
  free(cg->sizes);
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
  if (!is_floating(type))
    return integer_constant(cg, type, strtoull(spelling, NULL, 0));
  prim_kind prim = resolved(type)->prim;
  long double v = prim == PRIM_FLOAT    ? strtof(spelling, NULL)
                  : prim == PRIM_DOUBLE ? strtod(spelling, NULL)
                                        : strtold(spelling, NULL);
  return float_constant(cg, type, v);
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

static const char *symbol_address(codegen *cg, symbol *sym) {
  if (sym->kind == SYMBOL_FUNC || sym->linkage != LINKAGE_NONE) {
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
  case AST_NODE_TYPE_MEMBER_ACCESS: {
    const char *address = address_of(cg, node);
    return read_object(cg, node, address);
  }
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
  const char *v = value(cg, result);
  terminate(cg, "ret %s %s", llvm_type(cg, result->expr_type), v);
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
  free_labels(cg);
  cg->vla_count = 0;
  cg->size_count = 0;
  cg->function_name = fn->function_def.name;
  cg->function_name_global = NULL;
  scan_labels(cg, fn->function_def.body);
  cg->vla_count = 0;
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
  for (int i = 0; i < count; i++)
    evaluate_sizes(cg, type->param_types[i]);
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
         global_name(cg, fn->symbol), parameters, contents(&cg->allocas), contents(&cg->body));
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
  const char *parameters = type->has_prototype ? "" : "...";
  for (int i = 0; type->has_prototype && i < type->param_count; i++)
    parameters = formatted(cg, "%s%s%s", parameters, i ? ", " : "",
                           parameter_type(cg, type->param_types[i]));
  if (type->has_prototype && type->is_variadic)
    parameters = formatted(cg, "%s%s...", parameters, type->param_count ? ", " : "");
  append(cg, out, "declare %s%s %s(%s)\n", import, llvm_type(cg, type->ptr_to),
         global_name(cg, sym), parameters);
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
  append(&cg, &out, "%s", contents(&cg.functions));
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
  int failed = cg.failed;
  free_codegen(&cg);
  if (failed) {
    free(out.data);
    return NULL;
  }
  return out.data;
}
