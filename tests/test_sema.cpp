#include "conversion_walk.h"
#include "target_guard.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

extern "C" {
#include "parser.h"
#include "preprocessor.h"
#include "sema.h"
}

static void collect(ast_node *node, std::vector<ast_node *> &out);

static void collect_type(type_info *type, std::vector<ast_node *> &out) {
  for (; type != nullptr; type = type->ptr_to) {
    collect(type->array_size_expr, out);
    for (int i = 0; i < type->param_count; i++) {
      if (type->param_definitions != nullptr)
        collect(type->param_definitions[i], out);
      collect_type(type->param_types[i], out);
    }
  }
}

static void collect(ast_node *node, std::vector<ast_node *> &out) {
  if (node == nullptr)
    return;
  out.push_back(node);
  switch (node->type) {
  case AST_NODE_TYPE_PROGRAM:
    for (int i = 0; i < node->program.count; i++)
      collect(node->program.declarations[i], out);
    break;
  case AST_NODE_TYPE_BLOCK:
  case AST_NODE_TYPE_DECL_GROUP:
    for (int i = 0; i < node->block.count; i++)
      collect(node->block.statements[i], out);
    break;
  case AST_NODE_TYPE_BINARY_OP:
    collect(node->binary_op.left, out);
    collect(node->binary_op.right, out);
    break;
  case AST_NODE_TYPE_UNARY_OP:
    collect(node->unary_op.operand, out);
    break;
  case AST_NODE_TYPE_ASSIGNMENT:
    collect(node->assignment.left, out);
    collect(node->assignment.right, out);
    break;
  case AST_NODE_TYPE_TERNARY:
    collect(node->ternary.condition, out);
    collect(node->ternary.true_branch, out);
    collect(node->ternary.false_branch, out);
    break;
  case AST_NODE_TYPE_IF:
    collect(node->if_stmt.condition, out);
    collect(node->if_stmt.then_branch, out);
    collect(node->if_stmt.else_branch, out);
    break;
  case AST_NODE_TYPE_WHILE:
    collect(node->while_stmt.condition, out);
    collect(node->while_stmt.body, out);
    break;
  case AST_NODE_TYPE_FOR:
    collect(node->for_stmt.init, out);
    collect(node->for_stmt.condition, out);
    collect(node->for_stmt.increment, out);
    collect(node->for_stmt.body, out);
    break;
  case AST_NODE_TYPE_FUNCTION_CALL:
    collect(node->function_call.callable, out);
    for (int i = 0; i < node->function_call.arg_count; i++)
      collect(node->function_call.arguments[i], out);
    break;
  case AST_NODE_TYPE_FUNCTION_DEF:
    collect_type(node->function_def.type->ptr_to, out);
    for (int i = 0; i < node->function_def.type->param_count; i++)
      collect_type(node->function_def.type->param_types[i], out);
    collect(node->function_def.body, out);
    break;
  case AST_NODE_TYPE_RETURN:
    collect(node->return_stmt.return_value, out);
    break;
  case AST_NODE_TYPE_ARRAY_SUBSCRIPT:
    collect(node->array_subscript.left, out);
    collect(node->array_subscript.index, out);
    break;
  case AST_NODE_TYPE_MEMBER_ACCESS:
    collect(node->member_access.left, out);
    break;
  case AST_NODE_TYPE_VAR_DECL:
    collect_type(node->var_decl.type, out);
    collect(node->var_decl.bitfield_width, out);
    collect(node->var_decl.init_value, out);
    break;
  case AST_NODE_TYPE_DO_WHILE:
    collect(node->do_while_stmt.body, out);
    collect(node->do_while_stmt.condition, out);
    break;
  case AST_NODE_TYPE_SWITCH:
    collect(node->switch_stmt.condition, out);
    collect(node->switch_stmt.body, out);
    break;
  case AST_NODE_TYPE_CASE:
    collect(node->case_stmt.value, out);
    collect(node->case_stmt.body, out);
    break;
  case AST_NODE_TYPE_DEFAULT:
    collect(node->default_stmt.body, out);
    break;
  case AST_NODE_TYPE_STRUCT_DEF:
    for (int i = 0; i < node->struct_def.member_count; i++)
      collect(node->struct_def.members[i], out);
    break;
  case AST_NODE_TYPE_ENUM_DEF:
    if (node->enum_def.values != nullptr) {
      for (int i = 0; i < node->enum_def.enumerator_count; i++)
        collect(node->enum_def.values[i], out);
    }
    break;
  case AST_NODE_TYPE_CAST:
    collect(node->cast_expr.definition, out);
    collect_type(node->cast_expr.type, out);
    collect(node->cast_expr.operand, out);
    break;
  case AST_NODE_TYPE_INIT_LIST:
    for (int i = 0; i < node->init_list.count; i++) {
      collect(node->init_list.items[i].index, out);
      collect(node->init_list.items[i].value, out);
    }
    break;
  case AST_NODE_TYPE_COMPOUND_LITERAL:
    collect(node->compound_literal.definition, out);
    collect_type(node->compound_literal.type, out);
    collect(node->compound_literal.init_list, out);
    break;
  case AST_NODE_TYPE_LABEL:
    collect(node->label_stmt.statement, out);
    break;
  case AST_NODE_TYPE_CONVERSION:
    collect(node->conversion.operand, out);
    break;
  default:
    break;
  }
}

struct Checked {
  ast_node *program = nullptr;
  symbol_table *table = nullptr;
  int parse_errors = -1;
  int errors = -2;
  std::string diagnostics;
  std::vector<ast_node *> nodes;

  Checked() = default;
  Checked(const Checked &) = delete;
  Checked &operator=(const Checked &) = delete;
  ~Checked() {
    symbol_table_destroy(table);
    free_ast(program);
  }
};

static void check(Checked &c, const char *source) {
  lexer lex;
  lexer_init(&lex, source);
  parser p;
  parser_init(&p, &lex);
  c.program = parse_program(&p);
  c.parse_errors = p.had_error;
  parser_destroy(&p);
  c.table = symbol_table_create();
  testing::internal::CaptureStderr();
  c.errors = sema_check(c.table, c.program);
  c.diagnostics = testing::internal::GetCapturedStderr();
  collect(c.program, c.nodes);
}

static void check_preprocessed(Checked &c, const char *source) {
  token_buf tb;
  testing::internal::CaptureStderr();
  int pp_errors = pp_run(&tb, source);
  std::string pp_diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(pp_errors, 0) << pp_diagnostics;
  parser p;
  parser_init_from_buf(&p, &tb);
  c.program = parse_program(&p);
  c.parse_errors = p.had_error;
  parser_destroy(&p);
  token_buf_free(&tb);
  c.table = symbol_table_create();
  testing::internal::CaptureStderr();
  c.errors = sema_check(c.table, c.program);
  c.diagnostics = testing::internal::GetCapturedStderr();
  collect(c.program, c.nodes);
}

static std::vector<ast_node *> nodes_of(const Checked &c, ast_node_type type) {
  std::vector<ast_node *> out;
  for (ast_node *n : c.nodes) {
    if (n->type == type)
      out.push_back(n);
  }
  return out;
}

static std::vector<ast_node *> uses(const Checked &c, const char *name) {
  std::vector<ast_node *> out;
  for (ast_node *n : nodes_of(c, AST_NODE_TYPE_IDENTIFIER)) {
    if (std::strcmp(n->tok.value, name) == 0)
      out.push_back(n);
  }
  return out;
}

static std::vector<ast_node *> declarations(const Checked &c, const char *name) {
  std::vector<ast_node *> out;
  for (ast_node *n : c.nodes) {
    const char *declared = nullptr;
    if (n->type == AST_NODE_TYPE_VAR_DECL)
      declared = n->var_decl.var_name;
    else if (n->type == AST_NODE_TYPE_FUNCTION_DEF)
      declared = n->function_def.name;
    if (declared != nullptr && std::strcmp(declared, name) == 0)
      out.push_back(n);
  }
  return out;
}

static std::string where(const char *src, const char *needle, int nth = 1) {
  const char *from = src;
  const char *hit = nullptr;
  for (int i = 0; i < nth; i++) {
    hit = std::strstr(from, needle);
    if (hit == nullptr)
      return "<needle not found>";
    from = hit + 1;
  }
  int line = 1;
  int column = 1;
  for (const char *q = src; q < hit; q++) {
    if (*q == '\n') {
      line++;
      column = 1;
    } else {
      column++;
    }
  }
  return std::to_string(line) + ":" + std::to_string(column);
}

static std::string error_at(const char *src, const char *needle, int nth, const std::string &text) {
  return where(src, needle, nth) + ": error: " + text + "\n";
}

static std::string note_at(const char *src, const char *needle, int nth, const std::string &name) {
  return where(src, needle, nth) + ": note: previous declaration of '" + name + "' is here\n";
}

static void collect_type_nodes(type_info *type, std::vector<type_info *> &out) {
  for (; type != nullptr; type = type->ptr_to) {
    out.push_back(type);
    for (int i = 0; i < type->param_count; i++)
      collect_type_nodes(type->param_types[i], out);
  }
}

static std::vector<type_info *> types_in(const Checked &c) {
  std::vector<type_info *> out;
  for (ast_node *n : c.nodes) {
    if (n->type == AST_NODE_TYPE_VAR_DECL) {
      collect_type_nodes(n->var_decl.type, out);
    } else if (n->type == AST_NODE_TYPE_FUNCTION_DEF) {
      collect_type_nodes(n->function_def.type->ptr_to, out);
      for (int i = 0; i < n->function_def.type->param_count; i++)
        collect_type_nodes(n->function_def.type->param_types[i], out);
    } else if (n->type == AST_NODE_TYPE_CAST) {
      collect_type_nodes(n->cast_expr.type, out);
    } else if (n->type == AST_NODE_TYPE_COMPOUND_LITERAL) {
      collect_type_nodes(n->compound_literal.type, out);
    }
  }
  return out;
}

static std::vector<type_info *> named_types(const Checked &c, type_kind kind, const char *name) {
  std::vector<type_info *> out;
  for (type_info *t : types_in(c)) {
    if (t->kind == kind && t->tag_name != nullptr && std::strcmp(t->tag_name, name) == 0)
      out.push_back(t);
  }
  return out;
}

static std::vector<ast_node *> tag_definitions(const Checked &c, const char *name) {
  std::vector<ast_node *> out;
  for (ast_node *n : c.nodes) {
    if (n->type != AST_NODE_TYPE_STRUCT_DEF && n->type != AST_NODE_TYPE_ENUM_DEF)
      continue;
    const char *tag =
        n->type == AST_NODE_TYPE_STRUCT_DEF ? n->struct_def.tag_name : n->enum_def.tag_name;
    bool same = name == nullptr ? tag == nullptr : tag != nullptr && std::strcmp(tag, name) == 0;
    if (same)
      out.push_back(n);
  }
  return out;
}

static type_info *base_of(type_info *type) {
  while (type != nullptr && type->ptr_to != nullptr)
    type = type->ptr_to;
  return type;
}

static type_info *type_of(const Checked &c, const char *name) {
  std::vector<ast_node *> decls = declarations(c, name);
  if (decls.size() != 1 || decls[0]->type != AST_NODE_TYPE_VAR_DECL)
    return nullptr;
  return decls[0]->var_decl.type;
}

TEST(SemaTest, UncheckableArgumentsReturnMinusOne) {
  symbol_table *table = symbol_table_create();
  ast_node *program = create_ast_node(AST_NODE_TYPE_PROGRAM);
  ast_node *number = create_ast_node(AST_NODE_TYPE_NUMBER);

  EXPECT_EQ(sema_check(nullptr, program), -1);
  EXPECT_EQ(sema_check(table, nullptr), -1);
  EXPECT_EQ(sema_check(table, number), -1);
  EXPECT_EQ(table->current_scope, nullptr);
  EXPECT_EQ(table->all_symbols, nullptr);
  EXPECT_EQ(sema_check(table, program), 0) << "an empty translation unit is valid";

  symbol_table_destroy(table);
  free_ast(number);
  free_ast(program);
}

TEST(SemaTest, ACleanProgramHasNoErrorsPrintsNothingAndLeavesNoScopeOpen) {
  Checked c;
  check(c, "int g;\n"
           "int add(int a, int b) { return a + b + g; }\n"
           "int main(void) { int x = add(1, 2); return x; }\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);
  EXPECT_EQ(c.diagnostics, "");
  EXPECT_EQ(c.table->current_scope, nullptr);
  EXPECT_NE(c.table->all_symbols, nullptr) << "symbols outlive the file scope";
}

static const char *every_name_source =
    "enum color { RED, GREEN = RED + 1 };\n"
    "typedef int handler(int code);\n"
    "typedef struct { int w; } box;\n"
    "enum { LOW, HIGH } level;\n"
    "union number { int i; float f; } num;\n"
    "union number *np = &num;\n"
    "handler *on_event;\n"
    "box crate;\n"
    "struct point { int x; int y[GREEN]; int flags : GREEN; };\n"
    "static int table[3] = { [RED] = 1, 2, 3 };\n"
    "int counter, *cursor = &counter;\n"
    "int (*callback)(int k, int arr[k]);\n"
    "int sum(int a, int b);\n"
    "int sum(int a, int b) { return a + b; }\n"
    "int apply(int n, int vla[n]) { return vla[0] + n; }\n"
    "int main(void) {\n"
    "  struct point pt;\n"
    "  int i, *ptr = &i;\n"
    "  i = sizeof(int) + sizeof i + sizeof(int[GREEN]);\n"
    "  ptr = (int *)ptr;\n"
    "  pt = (struct point){ 1, { GREEN } };\n"
    "  pt.x = ptr[0] + table[RED];\n"
    "  for (int k = 0; k < 3; k++) { if (k == 1) continue; else break; }\n"
    "  while (i) i--;\n"
    "  do { i++; } while (i < 2);\n"
    "  switch (i) { case RED: i = 2; break; default: i = GREEN; }\n"
    "  goto done;\n"
    "done:\n"
    "  i = sum(i, 1), i++;\n"
    "  return i ? counter : *cursor;\n"
    "}\n";

TEST(SemaTest, EveryNameInAValidProgramIsResolved) {
  Checked c;
  check(c, every_name_source);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);
  EXPECT_EQ(c.diagnostics, "");

  std::vector<ast_node *> members;
  for (ast_node *def : nodes_of(c, AST_NODE_TYPE_STRUCT_DEF)) {
    for (int i = 0; i < def->struct_def.member_count; i++) {
      ast_node *member = def->struct_def.members[i];
      if (member->type != AST_NODE_TYPE_DECL_GROUP) {
        members.push_back(member);
        continue;
      }
      for (int k = 0; k < member->block.count; k++)
        members.push_back(member->block.statements[k]);
    }
  }

  int identifiers = 0;
  for (ast_node *n : c.nodes) {
    switch (n->type) {
    case AST_NODE_TYPE_IDENTIFIER:
      identifiers++;
      ASSERT_NE(n->symbol, nullptr)
          << n->tok.value << " at " << n->loc.line << ":" << n->loc.column;
      EXPECT_STREQ(n->symbol->name, n->tok.value);
      break;
    case AST_NODE_TYPE_VAR_DECL:
      if (std::find(members.begin(), members.end(), n) != members.end()) {
        EXPECT_EQ(n->symbol, nullptr) << "member " << n->var_decl.var_name;
        break;
      }
      ASSERT_NE(n->symbol, nullptr) << n->var_decl.var_name;
      EXPECT_STREQ(n->symbol->name, n->var_decl.var_name);
      if (n->var_decl.type->kind == TYPE_FUNCTION &&
          n->var_decl.specs.storage_class != TOKEN_TYPEDEF)
        EXPECT_EQ(n->symbol->kind, SYMBOL_FUNC) << n->var_decl.var_name;
      break;
    case AST_NODE_TYPE_FUNCTION_DEF:
      ASSERT_NE(n->symbol, nullptr) << n->function_def.name;
      EXPECT_STREQ(n->symbol->name, n->function_def.name);
      EXPECT_NE(n->symbol->type, nullptr) << n->function_def.name;
      EXPECT_NE(n->function_def.body, nullptr) << n->function_def.name;
      if (n->function_def.body == nullptr || n->function_def.type->param_count == 0) {
        EXPECT_EQ(n->function_def.param_symbols, nullptr) << n->function_def.name;
        break;
      }
      ASSERT_NE(n->function_def.param_symbols, nullptr) << n->function_def.name;
      for (int i = 0; i < n->function_def.type->param_count; i++) {
        ASSERT_NE(n->function_def.param_symbols[i], nullptr);
        EXPECT_STREQ(n->function_def.param_symbols[i]->name, n->function_def.type->param_names[i]);
      }
      break;
    case AST_NODE_TYPE_LABEL:
    case AST_NODE_TYPE_GOTO:
      ASSERT_NE(n->symbol, nullptr);
      EXPECT_EQ(n->symbol->kind, SYMBOL_LABEL);
      break;
    case AST_NODE_TYPE_STRUCT_DEF:
    case AST_NODE_TYPE_ENUM_DEF:
      ASSERT_NE(n->symbol, nullptr) << "definition node type " << n->type;
      EXPECT_EQ(n->symbol->kind, SYMBOL_TAG);
      EXPECT_EQ(n->symbol->definition, n);
      EXPECT_EQ(n->symbol->is_defined, 1);
      break;
    default:
      EXPECT_EQ(n->symbol, nullptr) << "node type " << n->type;
      break;
    }
  }
  EXPECT_GE(identifiers, 35) << "the walk must reach every construct that holds a name";

  int named = 0;
  for (type_info *t : types_in(c)) {
    if (t->kind != TYPE_TYPEDEF && t->kind != TYPE_STRUCT && t->kind != TYPE_UNION &&
        t->kind != TYPE_ENUM) {
      EXPECT_EQ(t->symbol, nullptr) << "type kind " << t->kind;
      continue;
    }
    named++;
    ASSERT_NE(t->symbol, nullptr) << "type kind " << t->kind << " "
                                  << (t->tag_name ? t->tag_name : "<anonymous>");
    EXPECT_EQ(t->symbol->kind, t->kind == TYPE_TYPEDEF ? SYMBOL_TYPEDEF : SYMBOL_TAG);
  }
  EXPECT_GE(named, 8) << "the walk must reach every type that names a typedef or a tag";
}

TEST(SemaTest, ANamedTagResolvesToItsDefinitionEverywhereItIsUsed) {
  Checked c;
  check(c, "struct point { int x; };\n"
           "struct point origin;\n"
           "struct point *make(struct point *from);\n"
           "int main(void) {\n"
           "  struct point *p = (struct point *)0;\n"
           "  struct point q = (struct point){ 1 };\n"
           "  return sizeof(struct point) + p->x + q.x;\n"
           "}\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);
  EXPECT_EQ(c.diagnostics, "");

  std::vector<ast_node *> defs = tag_definitions(c, "point");
  ASSERT_EQ(defs.size(), 1u);
  symbol *point = defs[0]->symbol;
  ASSERT_NE(point, nullptr);
  EXPECT_STREQ(point->name, "point");
  EXPECT_EQ(point->kind, SYMBOL_TAG);
  EXPECT_EQ(point->tag_kind, TYPE_STRUCT);
  EXPECT_EQ(point->definition, defs[0]);
  EXPECT_EQ(point->is_defined, 1);

  std::vector<type_info *> references = named_types(c, TYPE_STRUCT, "point");
  EXPECT_EQ(references.size(), 8u)
      << "origin, make's return and parameter, p, the cast, q, the compound literal, sizeof";
  for (type_info *t : references)
    EXPECT_EQ(t->symbol, point);
}

TEST(SemaTest, EveryDeclaratorOfAnAnonymousDefinitionSharesOneTag) {
  Checked c;
  check(c, "typedef struct { int x; } point, *point_ptr;\n"
           "struct { int y; } a, b;\n"
           "point p;\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);

  std::vector<ast_node *> anonymous = tag_definitions(c, nullptr);
  ASSERT_EQ(anonymous.size(), 2u);
  symbol *first = anonymous[0]->symbol;
  symbol *second = anonymous[1]->symbol;
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first, second) << "every anonymous definition is its own type";
  EXPECT_EQ(first->name, nullptr);
  EXPECT_EQ(first->tag_kind, TYPE_STRUCT);
  EXPECT_EQ(first->definition, anonymous[0]);
  EXPECT_EQ(first->is_defined, 1);

  type_info *point = type_of(c, "point");
  type_info *point_ptr = type_of(c, "point_ptr");
  type_info *a = type_of(c, "a");
  type_info *b = type_of(c, "b");
  type_info *p = type_of(c, "p");
  ASSERT_NE(point, nullptr);
  ASSERT_NE(point_ptr, nullptr);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(point->symbol, first);
  EXPECT_EQ(base_of(point_ptr)->symbol, first);
  EXPECT_EQ(a->symbol, second);
  EXPECT_EQ(b->symbol, second);
  ASSERT_EQ(p->kind, TYPE_TYPEDEF);
  EXPECT_EQ(p->symbol, declarations(c, "point")[0]->symbol);
}

TEST(SemaTest, ASelfReferentialMemberPointsAtItsOwnStruct) {
  Checked c;
  check(c, "struct node { int value; struct node *next; };\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);

  std::vector<ast_node *> defs = tag_definitions(c, "node");
  ASSERT_EQ(defs.size(), 1u);
  ASSERT_NE(defs[0]->symbol, nullptr);
  EXPECT_EQ(defs[0]->symbol->loc.line, defs[0]->loc.line);
  EXPECT_EQ(defs[0]->symbol->loc.column, defs[0]->loc.column);
  type_info *next = type_of(c, "next");
  ASSERT_NE(next, nullptr);
  EXPECT_EQ(base_of(next)->symbol, defs[0]->symbol);
}

TEST(SemaTest, AForwardDeclarationAndItsDefinitionShareOneTag) {
  Checked c;
  check(c, "struct S;\n"
           "struct S *early;\n"
           "struct S { int a; };\n"
           "struct S;\n"
           "struct S late;\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);
  EXPECT_EQ(c.diagnostics, "");

  std::vector<ast_node *> defs = tag_definitions(c, "S");
  ASSERT_EQ(defs.size(), 3u);
  symbol *s = defs[0]->symbol;
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(defs[1]->symbol, s);
  EXPECT_EQ(defs[2]->symbol, s);
  EXPECT_EQ(s->loc.line, 1) << "a tag is located at its first declaration";
  EXPECT_EQ(s->definition, defs[1]);
  EXPECT_EQ(s->is_defined, 1);

  type_info *early = type_of(c, "early");
  type_info *late = type_of(c, "late");
  ASSERT_NE(early, nullptr);
  ASSERT_NE(late, nullptr);
  EXPECT_EQ(base_of(early)->symbol, s);
  EXPECT_EQ(late->symbol, s);
}

TEST(SemaTest, AReferenceWithNothingVisibleDeclaresTheTagWhereItAppears) {
  Checked c;
  check(c, "void f(void) {\n"
           "  struct S *p;\n"
           "  struct S { int a; } *q;\n"
           "}\n"
           "struct S *outside;\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);

  std::vector<ast_node *> defs = tag_definitions(c, "S");
  ASSERT_EQ(defs.size(), 1u);
  symbol *inner = defs[0]->symbol;
  ASSERT_NE(inner, nullptr);
  EXPECT_EQ(base_of(type_of(c, "p"))->symbol, inner) << "the definition completed p's tag";
  EXPECT_EQ(base_of(type_of(c, "q"))->symbol, inner);

  symbol *outside = base_of(type_of(c, "outside"))->symbol;
  ASSERT_NE(outside, nullptr);
  EXPECT_NE(outside, inner) << "the block's tag ended with the block";
  EXPECT_EQ(outside->tag_kind, TYPE_STRUCT);
  EXPECT_EQ(outside->is_defined, 0);
}

TEST(SemaTest, AReferenceUsesTheVisibleTagButAnInnerDefinitionIsANewType) {
  Checked c;
  check(c, "struct S { int a; };\n"
           "void f(void) {\n"
           "  struct S *p;\n"
           "  struct S { int b; } *q;\n"
           "}\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);
  EXPECT_EQ(c.diagnostics, "");

  std::vector<ast_node *> defs = tag_definitions(c, "S");
  ASSERT_EQ(defs.size(), 2u);
  ASSERT_NE(defs[0]->symbol, nullptr);
  ASSERT_NE(defs[1]->symbol, nullptr);
  EXPECT_NE(defs[0]->symbol, defs[1]->symbol);
  EXPECT_EQ(base_of(type_of(c, "p"))->symbol, defs[0]->symbol);
  EXPECT_EQ(base_of(type_of(c, "q"))->symbol, defs[1]->symbol);
}

TEST(SemaTest, ABlockScopeForwardDeclarationHidesTheOuterTag) {
  Checked c;
  check(c, "struct S { int a; };\n"
           "void f(void) {\n"
           "  struct S;\n"
           "  struct S *p;\n"
           "}\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);

  std::vector<ast_node *> defs = tag_definitions(c, "S");
  ASSERT_EQ(defs.size(), 2u);
  ast_node *forward = defs[1];
  EXPECT_EQ(forward->struct_def.is_forward, 1);
  ASSERT_NE(defs[0]->symbol, nullptr);
  ASSERT_NE(forward->symbol, nullptr);
  EXPECT_NE(forward->symbol, defs[0]->symbol);
  EXPECT_EQ(forward->symbol->is_defined, 0);
  EXPECT_EQ(forward->symbol->definition, nullptr);
  EXPECT_EQ(base_of(type_of(c, "p"))->symbol, forward->symbol);
}

TEST(SemaTest, PrototypeAndDefinitionParametersScopeTheirTagsDifferently) {
  Checked c;
  check(c, "void g(struct T *t);\n"
           "struct T *after;\n"
           "int h(struct U *u) { struct U *inside = u; return 0; }\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);

  std::vector<ast_node *> g = declarations(c, "g");
  std::vector<ast_node *> h = declarations(c, "h");
  ASSERT_EQ(g.size(), 1u);
  ASSERT_EQ(h.size(), 1u);
  symbol *in_prototype = base_of(g[0]->var_decl.type->param_types[0])->symbol;
  symbol *after = base_of(type_of(c, "after"))->symbol;
  ASSERT_NE(in_prototype, nullptr);
  ASSERT_NE(after, nullptr);
  EXPECT_NE(in_prototype, after) << "a tag first seen in a prototype ends with it";

  symbol *in_parameters = base_of(h[0]->function_def.type->param_types[0])->symbol;
  ASSERT_NE(in_parameters, nullptr);
  EXPECT_EQ(base_of(type_of(c, "inside"))->symbol, in_parameters)
      << "a definition's parameters share the body's scope";
}

TEST(SemaTest, ATypedefUseResolvesToTheVisibleTypedef) {
  Checked c;
  check(c, "typedef int T;\n"
           "T outer;\n"
           "void f(void) {\n"
           "  typedef char T;\n"
           "  T inner;\n"
           "}\n"
           "typedef struct S S;\n"
           "S *both;\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);

  std::vector<ast_node *> typedefs = declarations(c, "T");
  ASSERT_EQ(typedefs.size(), 2u);
  ASSERT_NE(typedefs[0]->symbol, nullptr);
  ASSERT_NE(typedefs[1]->symbol, nullptr);
  EXPECT_EQ(type_of(c, "outer")->symbol, typedefs[0]->symbol);
  EXPECT_EQ(type_of(c, "inner")->symbol, typedefs[1]->symbol);

  std::vector<ast_node *> s = declarations(c, "S");
  ASSERT_EQ(s.size(), 1u);
  symbol *typedef_s = s[0]->symbol;
  symbol *tag_s = s[0]->var_decl.type->symbol;
  ASSERT_NE(typedef_s, nullptr);
  ASSERT_NE(tag_s, nullptr);
  EXPECT_EQ(typedef_s->kind, SYMBOL_TYPEDEF);
  EXPECT_EQ(tag_s->kind, SYMBOL_TAG);
  EXPECT_EQ(base_of(type_of(c, "both"))->symbol, typedef_s);
}

TEST(SemaTest, AParameterThatHidesATypedefIsAnObject) {
  const char *src = "typedef int T;\n"
                    "void f(int T) { T = 1; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0) << c.diagnostics;
  std::vector<ast_node *> t_uses = uses(c, "T");
  ASSERT_EQ(t_uses.size(), 1u);
  ASSERT_NE(t_uses[0]->symbol, nullptr);
  EXPECT_EQ(t_uses[0]->symbol->kind, SYMBOL_VAR);
}

TEST(SemaTest, ATypedefNameFromARejectedDeclarationIsReportedNotTrusted) {
  const char *src = "int T;\n"
                    "typedef int T, ;\n"
                    "typedef int U, ;\n"
                    "T x;\n"
                    "U y;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 2)
      << "the parser saw both typedef names, then dropped both declarations";
  EXPECT_EQ(c.errors, 2);
  EXPECT_EQ(c.diagnostics, error_at(src, "x;", 1, "expected a type name, found 'T'") +
                               note_at(src, "T;", 1, "T") +
                               error_at(src, "y;", 1, "unknown type name 'U'"));
  ASSERT_NE(type_of(c, "x"), nullptr);
  EXPECT_EQ(type_of(c, "x")->symbol, nullptr);
}

TEST(SemaTest, TagRedefinitionIsReportedAtTheLaterDefinition) {
  const char *src = "struct S;\n"
                    "struct S { int a; };\n"
                    "struct S { int b; };\n"
                    "struct N { struct N { int c; } inner; };\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 2);
  EXPECT_EQ(c.diagnostics, error_at(src, "struct S { int b", 1, "redefinition of 'S'") +
                               note_at(src, "struct S { int a", 1, "S") +
                               error_at(src, "struct N {", 2, "redefinition of 'N'") +
                               note_at(src, "struct N {", 1, "N"));

  std::vector<ast_node *> defs = tag_definitions(c, "S");
  ASSERT_EQ(defs.size(), 3u);
  ASSERT_NE(defs[2]->symbol, nullptr);
  EXPECT_NE(defs[2]->symbol, defs[1]->symbol) << "the redefinition gets a tag of its own";
  EXPECT_EQ(defs[1]->symbol->definition, defs[1]);
}

TEST(SemaTest, ATagUsedWithTheWrongKeywordIsReported) {
  const char *src = "struct S;\n"
                    "union S { int a; };\n"
                    "union U;\n"
                    "struct U;\n"
                    "enum E { A };\n"
                    "struct E *p;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 3);
  EXPECT_EQ(c.diagnostics, error_at(src, "union S", 1, "wrong kind of tag 'S'") +
                               note_at(src, "struct S;", 1, "S") +
                               error_at(src, "struct U;", 1, "wrong kind of tag 'U'") +
                               note_at(src, "union U;", 1, "U") +
                               error_at(src, "p;", 1, "wrong kind of tag 'E'") +
                               note_at(src, "enum E", 1, "E"));
  EXPECT_EQ(base_of(type_of(c, "p"))->symbol, nullptr);
}

TEST(SemaTest, AnEnumMustBeCompleteWhereItIsNamed) {
  const char *src = "enum E *early;\n"
                    "enum E { A, B = sizeof(enum E) };\n"
                    "enum E late;\n"
                    "enum Missing *m;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 3);
  EXPECT_EQ(c.diagnostics, error_at(src, "early", 1, "use of incomplete enum 'E'") +
                               error_at(src, "(enum E)", 1, "use of incomplete enum 'E'") +
                               error_at(src, "m;", 1, "use of incomplete enum 'Missing'"));

  std::vector<ast_node *> defs = tag_definitions(c, "E");
  ASSERT_EQ(defs.size(), 1u);
  ASSERT_NE(defs[0]->symbol, nullptr);
  EXPECT_EQ(defs[0]->symbol->tag_kind, TYPE_ENUM);
  EXPECT_EQ(type_of(c, "late")->symbol, defs[0]->symbol);
}

TEST(SemaTest, AUseLinksToTheSymbolItsDeclarationRecorded) {
  const char *src = "int g;\n"
                    "void f(void) {\n"
                    "  int x;\n"
                    "  x = g;\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);

  std::vector<ast_node *> x_decls = declarations(c, "x");
  std::vector<ast_node *> x_uses = uses(c, "x");
  ASSERT_EQ(x_decls.size(), 1u);
  ASSERT_EQ(x_uses.size(), 1u);
  symbol *x = x_decls[0]->symbol;
  ASSERT_NE(x, nullptr);
  EXPECT_EQ(x_uses[0]->symbol, x);
  EXPECT_EQ(x->kind, SYMBOL_VAR);
  EXPECT_EQ(x->linkage, LINKAGE_NONE);
  EXPECT_EQ(x->type, x_decls[0]->var_decl.type);
  EXPECT_EQ(where(src, "x;"), std::to_string(x->loc.line) + ":" + std::to_string(x->loc.column));

  std::vector<ast_node *> g_decls = declarations(c, "g");
  std::vector<ast_node *> g_uses = uses(c, "g");
  ASSERT_EQ(g_decls.size(), 1u);
  ASSERT_EQ(g_uses.size(), 1u);
  ASSERT_NE(g_decls[0]->symbol, nullptr);
  EXPECT_EQ(g_uses[0]->symbol, g_decls[0]->symbol);
  EXPECT_EQ(g_decls[0]->symbol->linkage, LINKAGE_EXTERNAL);
}

TEST(SemaTest, ParametersAreRecordedInOrderAndAFunctionIsVisibleInItsOwnBody) {
  Checked c;
  check(c, "int fact(int a, int b) { return b ? a * fact(a, b - 1) : a; }\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);

  std::vector<ast_node *> fns = declarations(c, "fact");
  ASSERT_EQ(fns.size(), 1u);
  ast_node *fn = fns[0];
  ASSERT_NE(fn->symbol, nullptr);
  EXPECT_EQ(fn->symbol->kind, SYMBOL_FUNC);
  EXPECT_EQ(fn->symbol->type, fn->function_def.type);
  ASSERT_NE(fn->function_def.param_symbols, nullptr);
  symbol *a = fn->function_def.param_symbols[0];
  symbol *b = fn->function_def.param_symbols[1];
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_STREQ(a->name, "a");
  EXPECT_STREQ(b->name, "b");
  EXPECT_EQ(a->kind, SYMBOL_VAR);
  EXPECT_EQ(a->linkage, LINKAGE_NONE);
  EXPECT_EQ(a->type, fn->function_def.type->param_types[0]);
  EXPECT_EQ(b->type, fn->function_def.type->param_types[1]);
  EXPECT_EQ(a->loc.line, fn->loc.line);
  EXPECT_EQ(a->loc.column, fn->loc.column);

  std::vector<ast_node *> a_uses = uses(c, "a");
  std::vector<ast_node *> b_uses = uses(c, "b");
  ASSERT_EQ(a_uses.size(), 3u);
  ASSERT_EQ(b_uses.size(), 2u);
  for (ast_node *use : a_uses)
    EXPECT_EQ(use->symbol, a);
  for (ast_node *use : b_uses)
    EXPECT_EQ(use->symbol, b);

  std::vector<ast_node *> calls = uses(c, "fact");
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0]->symbol, fn->symbol);
}

TEST(SemaTest, EnumerationConstantsResolveToTheirOwnSymbols) {
  Checked c;
  check(c, "enum { A, B = A + 1 };\n"
           "int x = B;\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);

  std::vector<ast_node *> defs = nodes_of(c, AST_NODE_TYPE_ENUM_DEF);
  std::vector<ast_node *> a_uses = uses(c, "A");
  std::vector<ast_node *> b_uses = uses(c, "B");
  ASSERT_EQ(defs.size(), 1u);
  ASSERT_EQ(a_uses.size(), 1u);
  ASSERT_EQ(b_uses.size(), 1u);
  symbol *a = a_uses[0]->symbol;
  symbol *b = b_uses[0]->symbol;
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_NE(a, b);
  EXPECT_STREQ(a->name, "A");
  EXPECT_STREQ(b->name, "B");
  EXPECT_EQ(a->kind, SYMBOL_ENUM_CONSTANT);
  EXPECT_EQ(b->kind, SYMBOL_ENUM_CONSTANT);
  EXPECT_EQ(a->linkage, LINKAGE_NONE);
  EXPECT_EQ(a->type, nullptr);
  EXPECT_EQ(a->loc.line, defs[0]->loc.line);
  EXPECT_EQ(a->loc.column, defs[0]->loc.column);
}

TEST(SemaTest, AnInnerDeclarationHidesAnOuterOneUntilItsBlockEnds) {
  Checked c;
  check(c, "int x;\n"
           "void f(void) {\n"
           "  x = 1;\n"
           "  { int x; x = 2; }\n"
           "  x = 3;\n"
           "}\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);
  EXPECT_EQ(c.diagnostics, "");

  std::vector<ast_node *> decls = declarations(c, "x");
  std::vector<ast_node *> x_uses = uses(c, "x");
  ASSERT_EQ(decls.size(), 2u);
  ASSERT_EQ(x_uses.size(), 3u);
  ASSERT_NE(decls[0]->symbol, nullptr);
  ASSERT_NE(decls[1]->symbol, nullptr);
  EXPECT_NE(decls[0]->symbol, decls[1]->symbol);
  EXPECT_EQ(x_uses[0]->symbol, decls[0]->symbol);
  EXPECT_EQ(x_uses[1]->symbol, decls[1]->symbol);
  EXPECT_EQ(x_uses[2]->symbol, decls[0]->symbol);
}

TEST(SemaTest, DeclarationGroupsDoNotOpenAScope) {
  Checked c;
  check(c, "int a, b;\n"
           "struct pair { int first; } make(void);\n"
           "void f(void) {\n"
           "  int c, d;\n"
           "  c = a + b + d;\n"
           "  make();\n"
           "}\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);
  EXPECT_EQ(c.diagnostics, "");
}

TEST(SemaTest, AForVariableBelongsToTheLoopAndItsBodyIsAnInnerBlock) {
  const char *src = "void f(void) {\n"
                    "  for (int i = 0; i < 3; i++) { int i; i = 1; }\n"
                    "  i = 0;\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 1);
  EXPECT_EQ(c.diagnostics, error_at(src, "i = 0;\n}", 1, "use of undeclared identifier 'i'"));

  std::vector<ast_node *> decls = declarations(c, "i");
  std::vector<ast_node *> i_uses = uses(c, "i");
  ASSERT_EQ(decls.size(), 2u);
  ASSERT_EQ(i_uses.size(), 4u);
  EXPECT_EQ(i_uses[0]->symbol, decls[0]->symbol);
  EXPECT_EQ(i_uses[1]->symbol, decls[0]->symbol);
  EXPECT_EQ(i_uses[2]->symbol, decls[1]->symbol);
  EXPECT_EQ(i_uses[3]->symbol, nullptr);
}

TEST(SemaTest, EverySubstatementIsABlockOfItsOwn) {
  const char *src = "void f(int c) {\n"
                    "  if (c) c = sizeof(enum { Y1 = 1 }); c = Y1;\n"
                    "  if (c) ; else c = sizeof(enum { Y2 = 1 }); c = Y2;\n"
                    "  while (c) c = sizeof(enum { Y3 = 1 }); c = Y3;\n"
                    "  do c = sizeof(enum { Y4 = 1 }); while (Y4);\n"
                    "  switch (c) c = sizeof(enum { Y5 = 1 }); c = Y5;\n"
                    "  for (int j = 0; j < 1; j++) c = sizeof(enum { j = 1 });\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 5);
  EXPECT_EQ(c.diagnostics, error_at(src, "Y1;", 1, "use of undeclared identifier 'Y1'") +
                               error_at(src, "Y2;", 1, "use of undeclared identifier 'Y2'") +
                               error_at(src, "Y3;", 1, "use of undeclared identifier 'Y3'") +
                               error_at(src, "Y4);", 1, "use of undeclared identifier 'Y4'") +
                               error_at(src, "Y5;", 1, "use of undeclared identifier 'Y5'"));
}

TEST(SemaTest, ASelectionOrIterationStatementIsABlockOfItsOwn) {
  const char *src = "void g(int c) {\n"
                    "  if (sizeof(enum { A = 1 })) c = A; else c = A;\n"
                    "  c = A;\n"
                    "  while (sizeof(enum { B = 1 }) > c) c = B;\n"
                    "  c = B;\n"
                    "  switch (sizeof(enum { D = 1 })) { case D: c = D; }\n"
                    "  c = D;\n"
                    "  for (; sizeof(enum { E = 1 }) > c; c += E) c = E;\n"
                    "  c = E;\n"
                    "  do c = 0; while (sizeof(enum { F = 1 }) > F);\n"
                    "  c = F;\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 5);
  EXPECT_EQ(c.diagnostics, error_at(src, "A;\n", 2, "use of undeclared identifier 'A'") +
                               error_at(src, "B;\n", 2, "use of undeclared identifier 'B'") +
                               error_at(src, "D;\n", 1, "use of undeclared identifier 'D'") +
                               error_at(src, "E;\n", 2, "use of undeclared identifier 'E'") +
                               error_at(src, "F;\n", 1, "use of undeclared identifier 'F'"));
}

TEST(SemaTest, AParameterSharesTheFunctionBodysOutermostBlock) {
  const char *src = "int f(int a) {\n"
                    "  int a;\n"
                    "  { int a; }\n"
                    "  return a;\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 1);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "a;", 1, "redeclaration of 'a'") + note_at(src, "f(", 1, "a"));

  std::vector<ast_node *> fns = declarations(c, "f");
  std::vector<ast_node *> decls = declarations(c, "a");
  std::vector<ast_node *> a_uses = uses(c, "a");
  ASSERT_EQ(fns.size(), 1u);
  ASSERT_EQ(decls.size(), 2u);
  ASSERT_EQ(a_uses.size(), 1u);
  ASSERT_NE(fns[0]->function_def.param_symbols, nullptr);
  EXPECT_NE(decls[0]->symbol, fns[0]->function_def.param_symbols[0]);
  EXPECT_EQ(a_uses[0]->symbol, decls[0]->symbol);
}

TEST(SemaTest, PrototypeParametersAreScopedToTheirDeclarator) {
  const char *src = "void g(int n, int a[n]);\n"
                    "void h(int a, int a);\n"
                    "int (*fp)(int m, int q[m]);\n"
                    "int use(void) { return n + m; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 3);
  EXPECT_EQ(c.diagnostics, error_at(src, "h(", 1, "redeclaration of 'a'") +
                               note_at(src, "h(", 1, "a") +
                               error_at(src, "n + m", 1, "use of undeclared identifier 'n'") +
                               error_at(src, "m; }", 1, "use of undeclared identifier 'm'"));

  std::vector<ast_node *> n_uses = uses(c, "n");
  std::vector<ast_node *> m_uses = uses(c, "m");
  ASSERT_EQ(n_uses.size(), 2u);
  ASSERT_EQ(m_uses.size(), 2u);
  ASSERT_NE(n_uses[0]->symbol, nullptr);
  EXPECT_STREQ(n_uses[0]->symbol->name, "n");
  ASSERT_NE(m_uses[0]->symbol, nullptr);
  EXPECT_STREQ(m_uses[0]->symbol->name, "m");
  EXPECT_EQ(n_uses[1]->symbol, nullptr);
  EXPECT_EQ(m_uses[1]->symbol, nullptr);
}

TEST(SemaTest, AScopeBeginsAfterTheDeclaratorAndBeforeTheInitializer) {
  Checked c;
  check(c, "int a;\n"
           "void f(void) {\n"
           "  int x = x;\n"
           "  int a[sizeof a];\n"
           "}\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);

  std::vector<ast_node *> x_decls = declarations(c, "x");
  std::vector<ast_node *> x_uses = uses(c, "x");
  ASSERT_EQ(x_decls.size(), 1u);
  ASSERT_EQ(x_uses.size(), 1u);
  ASSERT_NE(x_decls[0]->symbol, nullptr);
  EXPECT_EQ(x_uses[0]->symbol, x_decls[0]->symbol);

  std::vector<ast_node *> a_decls = declarations(c, "a");
  std::vector<ast_node *> a_uses = uses(c, "a");
  ASSERT_EQ(a_decls.size(), 2u);
  ASSERT_EQ(a_uses.size(), 1u);
  ASSERT_NE(a_decls[0]->symbol, nullptr);
  EXPECT_EQ(a_uses[0]->symbol, a_decls[0]->symbol) << "the size is part of the local's declarator";
}

TEST(SemaTest, AnEnumerationConstantIsInScopeOnlyAfterItsEnumerator) {
  const char *src = "enum { A = A };\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 1);
  EXPECT_EQ(c.diagnostics, error_at(src, "A };", 1, "use of undeclared identifier 'A'"));
}

TEST(SemaTest, MembersAreNotOrdinaryNamesButWhatTheyContainIsResolved) {
  const char *src = "struct S {\n"
                    "  enum { M = 2 } e;\n"
                    "  int a[M], b : M;\n"
                    "};\n"
                    "struct S v = { .e = M };\n"
                    "int use(struct S s) { return s.a[0] + v.b + M; }\n"
                    "int n = a;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 1);
  EXPECT_EQ(c.diagnostics, error_at(src, "a;", 1, "use of undeclared identifier 'a'"));

  std::vector<ast_node *> m_uses = uses(c, "M");
  ASSERT_EQ(m_uses.size(), 4u);
  for (ast_node *use : m_uses) {
    ASSERT_NE(use->symbol, nullptr) << "M at " << use->loc.line << ":" << use->loc.column;
    EXPECT_EQ(use->symbol->kind, SYMBOL_ENUM_CONSTANT);
  }
  for (const char *member : {"e", "a", "b"}) {
    for (ast_node *decl : declarations(c, member))
      EXPECT_EQ(decl->symbol, nullptr) << member;
  }
}

TEST(SemaTest, FileScopeDeclarationsWithLinkageShareOneSymbol) {
  Checked c;
  check(c, "int x;\n"
           "extern int x;\n"
           "int x;\n"
           "int f(void);\n"
           "int f(void) { return x; }\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);
  EXPECT_EQ(c.diagnostics, "");

  std::vector<ast_node *> x_decls = declarations(c, "x");
  ASSERT_EQ(x_decls.size(), 3u);
  symbol *x = x_decls[0]->symbol;
  ASSERT_NE(x, nullptr);
  EXPECT_EQ(x_decls[1]->symbol, x);
  EXPECT_EQ(x_decls[2]->symbol, x);
  EXPECT_EQ(x->kind, SYMBOL_VAR);
  EXPECT_EQ(x->linkage, LINKAGE_EXTERNAL);
  std::vector<ast_node *> x_uses = uses(c, "x");
  ASSERT_EQ(x_uses.size(), 1u);
  EXPECT_EQ(x_uses[0]->symbol, x);

  std::vector<ast_node *> f_decls = declarations(c, "f");
  ASSERT_EQ(f_decls.size(), 2u);
  ASSERT_NE(f_decls[0]->symbol, nullptr);
  EXPECT_EQ(f_decls[1]->symbol, f_decls[0]->symbol);
  EXPECT_EQ(f_decls[0]->symbol->kind, SYMBOL_FUNC);
  EXPECT_EQ(f_decls[0]->symbol->linkage, LINKAGE_EXTERNAL);
}

TEST(SemaTest, StaticGivesInternalLinkageAndALaterDeclarationInheritsIt) {
  Checked c;
  check(c, "static int s;\n"
           "static int helper(void);\n"
           "void f(void) {\n"
           "  extern int s;\n"
           "  s = 1;\n"
           "}\n"
           "int helper(void) { return 0; }\n"
           "extern int e;\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);

  std::vector<ast_node *> s_decls = declarations(c, "s");
  ASSERT_EQ(s_decls.size(), 2u);
  ASSERT_NE(s_decls[0]->symbol, nullptr);
  ASSERT_NE(s_decls[1]->symbol, nullptr);
  EXPECT_EQ(s_decls[0]->symbol->linkage, LINKAGE_INTERNAL);
  EXPECT_NE(s_decls[1]->symbol, s_decls[0]->symbol)
      << "a block-scope declaration is its own symbol";
  EXPECT_EQ(s_decls[1]->symbol->linkage, LINKAGE_INTERNAL);
  std::vector<ast_node *> s_uses = uses(c, "s");
  ASSERT_EQ(s_uses.size(), 1u);
  EXPECT_EQ(s_uses[0]->symbol, s_decls[1]->symbol);

  std::vector<ast_node *> helpers = declarations(c, "helper");
  ASSERT_EQ(helpers.size(), 2u);
  ASSERT_NE(helpers[0]->symbol, nullptr);
  EXPECT_EQ(helpers[1]->symbol, helpers[0]->symbol);
  EXPECT_EQ(helpers[0]->symbol->linkage, LINKAGE_INTERNAL);

  std::vector<ast_node *> e_decls = declarations(c, "e");
  ASSERT_EQ(e_decls.size(), 1u);
  ASSERT_NE(e_decls[0]->symbol, nullptr);
  EXPECT_EQ(e_decls[0]->symbol->linkage, LINKAGE_EXTERNAL);
}

TEST(SemaTest, BlockScopeLinkageAndKinds) {
  Checked c;
  check(c, "void f(int p) {\n"
           "  int local;\n"
           "  static int kept;\n"
           "  extern int shared;\n"
           "  int g(int);\n"
           "  typedef int T;\n"
           "  { extern int local; }\n"
           "}\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);

  std::vector<ast_node *> fns = declarations(c, "f");
  ASSERT_EQ(fns.size(), 1u);
  ASSERT_NE(fns[0]->function_def.param_symbols, nullptr);
  ASSERT_NE(fns[0]->function_def.param_symbols[0], nullptr);
  EXPECT_EQ(fns[0]->function_def.param_symbols[0]->linkage, LINKAGE_NONE);

  auto only = [&](const char *name, size_t index) -> symbol * {
    std::vector<ast_node *> decls = declarations(c, name);
    return index < decls.size() ? decls[index]->symbol : nullptr;
  };
  symbol *local = only("local", 0);
  symbol *kept = only("kept", 0);
  symbol *shared = only("shared", 0);
  symbol *g = only("g", 0);
  symbol *t = only("T", 0);
  symbol *inner_local = only("local", 1);
  ASSERT_NE(local, nullptr);
  ASSERT_NE(kept, nullptr);
  ASSERT_NE(shared, nullptr);
  ASSERT_NE(g, nullptr);
  ASSERT_NE(t, nullptr);
  ASSERT_NE(inner_local, nullptr);

  EXPECT_EQ(local->kind, SYMBOL_VAR);
  EXPECT_EQ(local->linkage, LINKAGE_NONE);
  EXPECT_EQ(kept->linkage, LINKAGE_NONE);
  EXPECT_EQ(shared->linkage, LINKAGE_EXTERNAL);
  EXPECT_EQ(g->kind, SYMBOL_FUNC);
  EXPECT_EQ(g->linkage, LINKAGE_EXTERNAL);
  EXPECT_EQ(t->kind, SYMBOL_TYPEDEF);
  EXPECT_EQ(t->linkage, LINKAGE_NONE);
  EXPECT_NE(inner_local, local);
  EXPECT_EQ(inner_local->linkage, LINKAGE_EXTERNAL)
      << "the visible earlier declaration has no linkage, so extern gives external";
}

TEST(SemaTest, ASecondDeclarationWithoutLinkageInOneScopeIsAnError) {
  const char *src = "void f(void) {\n"
                    "  int y;\n"
                    "  int y;\n"
                    "  y = 1;\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 1);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "y;", 2, "redeclaration of 'y'") + note_at(src, "y;", 1, "y"));

  std::vector<ast_node *> decls = declarations(c, "y");
  std::vector<ast_node *> y_uses = uses(c, "y");
  ASSERT_EQ(decls.size(), 2u);
  ASSERT_EQ(y_uses.size(), 1u);
  ASSERT_NE(decls[1]->symbol, nullptr);
  EXPECT_NE(decls[1]->symbol, decls[0]->symbol);
  EXPECT_EQ(y_uses[0]->symbol, decls[1]->symbol) << "a use links to the nearest declaration";
}

TEST(SemaTest, TypedefNamesHaveNoLinkageSoTheyCannotBeRedeclared) {
  const char *src = "typedef int T;\n"
                    "typedef int T;\n"
                    "int x;\n"
                    "typedef int x;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 2);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "T;", 2, "redeclaration of 'T'") + note_at(src, "T;", 1, "T") +
                error_at(src, "x;", 2, "redeclaration of 'x'") + note_at(src, "x;", 1, "x"));
}

TEST(SemaTest, GotoResolvesForwardAndBackward) {
  Checked c;
  check(c, "void f(void) {\n"
           "  goto end;\n"
           "start:\n"
           "  { goto start; }\n"
           "end:\n"
           "  return;\n"
           "}\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);
  EXPECT_EQ(c.diagnostics, "");

  std::vector<ast_node *> labels = nodes_of(c, AST_NODE_TYPE_LABEL);
  std::vector<ast_node *> gotos = nodes_of(c, AST_NODE_TYPE_GOTO);
  ASSERT_EQ(labels.size(), 2u);
  ASSERT_EQ(gotos.size(), 2u);
  ASSERT_NE(labels[0]->symbol, nullptr);
  ASSERT_NE(labels[1]->symbol, nullptr);
  EXPECT_EQ(gotos[0]->symbol, labels[1]->symbol) << "forward";
  EXPECT_EQ(gotos[1]->symbol, labels[0]->symbol) << "backward, from a nested block";
}

TEST(SemaTest, DuplicateAndMissingLabelsAreReported) {
  const char *src = "void f(void) {\n"
                    "  again: ;\n"
                    "  { again: ; }\n"
                    "  goto nowhere;\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 2);
  EXPECT_EQ(c.diagnostics, error_at(src, "again", 2, "duplicate label 'again'") +
                               note_at(src, "again", 1, "again") +
                               error_at(src, "goto", 1, "use of undeclared label 'nowhere'"));
  std::vector<ast_node *> gotos = nodes_of(c, AST_NODE_TYPE_GOTO);
  ASSERT_EQ(gotos.size(), 1u);
  EXPECT_EQ(gotos[0]->symbol, nullptr);
}

TEST(SemaTest, LabelsDoNotCarryFromOneFunctionToTheNext) {
  const char *src = "void f(void) { goto out; }\n"
                    "void g(void) { out: ; }\n"
                    "void h(void) { out: ; goto out; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 1);
  EXPECT_EQ(c.diagnostics, error_at(src, "goto", 1, "use of undeclared label 'out'"));

  std::vector<ast_node *> labels = nodes_of(c, AST_NODE_TYPE_LABEL);
  std::vector<ast_node *> gotos = nodes_of(c, AST_NODE_TYPE_GOTO);
  ASSERT_EQ(labels.size(), 2u);
  ASSERT_EQ(gotos.size(), 2u);
  EXPECT_EQ(gotos[0]->symbol, nullptr) << "f's goto must not be resolved by a later function";
  ASSERT_NE(labels[1]->symbol, nullptr);
  EXPECT_EQ(gotos[1]->symbol, labels[1]->symbol);
}

TEST(SemaTest, CallingAnUndeclaredFunctionIsAnError) {
  const char *src = "int main(void) { return missing(1); }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 1);
  EXPECT_EQ(c.diagnostics, error_at(src, "missing", 1, "use of undeclared identifier 'missing'"));
}

TEST(SemaTest, ATypedefNameIsNotAnExpression) {
  const char *src = "typedef int T;\n"
                    "int a = T;\n"
                    "void g(int);\n"
                    "void f(void) { g(T); }\n"
                    "int h(void) { return T + 1; }\n"
                    "int s = sizeof T;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 4);
  const std::string message = "expected an expression, found type name 'T'";
  const std::string declared = note_at(src, "T;", 1, "T");
  EXPECT_EQ(c.diagnostics, error_at(src, "T;", 2, message) + declared +
                               error_at(src, "T)", 1, message) + declared +
                               error_at(src, "T +", 1, message) + declared +
                               error_at(src, "T;", 3, message) + declared);
  int uses = 0;
  for (ast_node *n : nodes_of(c, AST_NODE_TYPE_IDENTIFIER)) {
    if (std::strcmp(n->tok.value, "T") == 0) {
      EXPECT_EQ(n->symbol, nullptr) << "a type name used as a value resolves to nothing";
      uses++;
    }
  }
  EXPECT_EQ(uses, 4);
}

static symbol *find_symbol(const Checked &c, const char *name, symbol_kind kind) {
  for (symbol *sym = c.table->all_symbols; sym != nullptr; sym = sym->all_next) {
    if (sym->kind == kind && sym->name != nullptr && std::strcmp(sym->name, name) == 0)
      return sym;
  }
  return nullptr;
}

struct Evaluated {
  int parse_errors;
  int errors;
  bool has_value;
  long long value;
  std::string diagnostics;
};

static Evaluated evaluate_in(const std::string &declarations, const std::string &expression) {
  Checked c;
  std::string source = declarations + "enum { V = " + expression + " };\n";
  check(c, source.c_str());
  Evaluated out;
  out.parse_errors = c.parse_errors;
  out.errors = c.errors;
  symbol *v = find_symbol(c, "V", SYMBOL_ENUM_CONSTANT);
  out.has_value = v != nullptr && v->has_value;
  out.value = v != nullptr ? v->value : 0;
  out.diagnostics = c.diagnostics;
  return out;
}

struct ValueCase {
  const char *expression;
  long long value;
};

static void expect_values(const std::string &declarations, const std::vector<ValueCase> &cases) {
  for (const ValueCase &test : cases) {
    Evaluated result = evaluate_in(declarations, test.expression);
    EXPECT_EQ(result.parse_errors, 0) << test.expression;
    EXPECT_EQ(result.errors, 0) << test.expression << "\n" << result.diagnostics;
    EXPECT_TRUE(result.has_value) << test.expression;
    EXPECT_EQ(result.value, test.value) << test.expression;
  }
}

struct ErrorCase {
  const char *expression;
  const char *message;
};

static void expect_one_error(const std::string &declarations, const std::vector<ErrorCase> &cases) {
  for (const ErrorCase &test : cases) {
    Evaluated result = evaluate_in(declarations, test.expression);
    EXPECT_EQ(result.parse_errors, 0) << test.expression;
    EXPECT_EQ(result.errors, 1) << test.expression << "\n" << result.diagnostics;
    EXPECT_NE(result.diagnostics.find(test.message), std::string::npos) << test.expression << "\n"
                                                                        << result.diagnostics;
    EXPECT_FALSE(result.has_value) << test.expression;
  }
}

TEST(ConstantExpressionTest, ConversionsFollowTheTarget) {
  expect_values("", {
                        {"-1 < 0u", 0},
                        {"-1L < 1U", 0},
                        {"-1LL < 0UL", 1},
                        {"(long long)-1 < 1u", 1},
                        {"(unsigned char)255 + 1", 256},
                        {"(unsigned short)65535 + 1", 65536},
                        {"(char)255", -1},
                        {"(signed char)200 < 0", 1},
                        {"(unsigned)-1 > 0", 1},
                        {"(_Bool)2", 1},
                        {"(_Bool)0", 0},
                        {"(1 ? 1 : 0u) - 2 > 0", 1},
                    });
}

TEST(ConstantExpressionTest, IntegerConstantsTakeTheFirstTypeThatFits) {
  expect_values("", {
                        {"0", 0},
                        {"123", 123},
                        {"0x10", 16},
                        {"0X1f", 31},
                        {"010", 8},
                        {"2147483647", 2147483647},
                        {"2147483648 - 2147483649 < 0", 1},
                        {"0x80000000 - 0x80000001 < 0", 0},
                        {"0x80000000L - 0x80000001L < 0", 0},
                        {"4294967295 + 1 == 0", 0},
                        {"0xFFFFFFFF + 1 == 0", 1},
                        {"0x8000000000000000 > 0", 1},
                        {"1u - 2 > 0", 1},
                        {"1U - 2 > 0", 1},
                        {"1ul - 2 > 0", 1},
                        {"1LU - 2 > 0", 1},
                        {"1ull - 2 > 0", 1},
                        {"1LLU - 2 > 0", 1},
                        {"1l - 2 < 0", 1},
                        {"1LL - 2 < 0", 1},
                    });
}

TEST(TargetTypeTest, ConstantsFollowTheTargetsSizes) {
  const struct {
    const char *expression;
    long long on_windows;
    long long on_linux;
  } rows[] = {
      {"sizeof(long)", 4, 8},
      {"sizeof(unsigned long)", 4, 8},
      {"sizeof(struct { char c; long l; })", 8, 16},
      {"-1L < 1U", 0, 1},
      {"-1LL < 0UL", 1, 0},
      {"0x80000000L - 0x80000001L < 0", 0, 1},
      {"(long)4294967296 > 0", 0, 1},
      {"(unsigned long)-1 > 4294967295", 0, 1},
      {"sizeof L'a'", 2, 4},
      {"L'\\xffff'", 65535, 65535},
      {"L'\\U0001F600'", 0xDE00, 0x1F600},
      {"L'ab'", 98, 98},
      {"sizeof(L\"ab\")", 6, 12},
      {"sizeof(L\"\\U0001F600\")", 6, 8},
      {"sizeof(sizeof 0)", 8, 8},
  };
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    for (const auto &row : rows) {
      long long expected = kind == TARGET_LINUX_X64 ? row.on_linux : row.on_windows;
      SCOPED_TRACE(std::string(kind == TARGET_LINUX_X64 ? "linux: " : "windows: ") +
                   row.expression);
      Evaluated result = evaluate_in("", row.expression);
      EXPECT_EQ(result.parse_errors, 0);
      EXPECT_EQ(result.errors, 0) << result.diagnostics;
      EXPECT_TRUE(result.has_value);
      EXPECT_EQ(result.value, expected);
    }
  }
}

TEST(TargetTypeTest, AWideConstantOnLinuxIsASignedInt) {
  TargetGuard guard(TARGET_LINUX_X64);
  expect_values("", {{"L'\\xffffffff'", -1}, {"L'\\x80000000' < 0", 1}, {"L'\\x7fffffff' > 0", 1}});
  Evaluated too_wide = evaluate_in("", "L'\\x100000000'");
  EXPECT_EQ(too_wide.parse_errors, 1) << "the decoder reports an escape past 32 bits";
  EXPECT_EQ(too_wide.errors, 0) << too_wide.diagnostics;
}

static prim_kind prim_of(ast_node *expression) {
  type_info *type = expression->expr_type;
  if (type != nullptr && type->kind == TYPE_ARRAY)
    type = type->ptr_to;
  return type != nullptr ? type->prim : PRIM_NONE;
}

TEST(TargetTypeTest, SizeofPointerDifferencesAndWideCharactersTakeTheTargetsTypes) {
  const char *src = "char *p, *q;\n"
                    "void f(void) { sizeof 0; p - q; L'a'; L\"ab\"; 1L + 1u; 2147483648; }\n";
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    bool on_linux = kind == TARGET_LINUX_X64;
    SCOPED_TRACE(on_linux ? "linux" : "windows");
    Checked c;
    check(c, src);
    ASSERT_EQ(c.parse_errors, 0);
    ASSERT_EQ(c.errors, 0) << c.diagnostics;
    std::vector<ast_node *> unary = nodes_of(c, AST_NODE_TYPE_UNARY_OP);
    std::vector<ast_node *> binary = nodes_of(c, AST_NODE_TYPE_BINARY_OP);
    std::vector<ast_node *> chars = nodes_of(c, AST_NODE_TYPE_CHAR_LITERAL);
    std::vector<ast_node *> strings = nodes_of(c, AST_NODE_TYPE_STRING);
    std::vector<ast_node *> numbers = nodes_of(c, AST_NODE_TYPE_NUMBER);
    ASSERT_EQ(unary.size(), 1u);
    ASSERT_EQ(binary.size(), 2u);
    ASSERT_EQ(chars.size(), 1u);
    ASSERT_EQ(strings.size(), 1u);
    EXPECT_EQ(prim_of(unary[0]), on_linux ? PRIM_ULONG : PRIM_ULLONG) << "size_t";
    EXPECT_EQ(prim_of(binary[0]), on_linux ? PRIM_LONG : PRIM_LLONG) << "ptrdiff_t";
    EXPECT_EQ(prim_of(chars[0]), on_linux ? PRIM_INT : PRIM_USHORT) << "wchar_t";
    EXPECT_EQ(prim_of(strings[0]), on_linux ? PRIM_INT : PRIM_USHORT) << "an array of wchar_t";
    EXPECT_EQ(prim_of(binary[1]), on_linux ? PRIM_LONG : PRIM_ULONG)
        << "long holds every unsigned int only where it is wider";
    EXPECT_EQ(prim_of(numbers.back()), on_linux ? PRIM_LONG : PRIM_LLONG)
        << "2147483648 takes the first of int, long, long long that holds it";
  }
}

TEST(TargetTypeTest, AWideStringInitializesAnArrayOfTheTargetsWcharT) {
  const char *src = "int wi[] = L\"ab\";\n"
                    "unsigned short wu[] = L\"ab\";\n";
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    bool on_linux = kind == TARGET_LINUX_X64;
    SCOPED_TRACE(on_linux ? "linux" : "windows");
    Checked c;
    check(c, src);
    ASSERT_EQ(c.parse_errors, 0);
    EXPECT_EQ(c.diagnostics, error_at(src, "L\"ab\"", on_linux ? 2 : 1,
                                      "an array needs a brace-enclosed initializer"));
    std::vector<ast_node *> accepted = declarations(c, on_linux ? "wi" : "wu");
    ASSERT_EQ(accepted.size(), 1u);
    EXPECT_EQ(accepted[0]->var_decl.type->array_size, 3) << "two characters and the terminator";
  }
}

TEST(AsmTest, OperandsAreCheckedLikeAnyExpression) {
  const char *src = "const int k = 1;\n"
                    "void f(int code) {\n"
                    "  int out;\n"
                    "  __asm__(\"\" : \"=r\"(out) : \"r\"(code), \"r\"(1), \"r\"(k));\n"
                    "  __asm__(\"\" : \"=r\"(code + 1));\n"
                    "  __asm__(\"\" : \"=m\"(k));\n"
                    "  __asm__(\"\" : : \"r\"(missing));\n"
                    "  __asm__(\"\" : \"=r\"(gone));\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string output = "an asm output must be a modifiable lvalue";
  EXPECT_EQ(c.diagnostics,
            error_at(src, "+ 1", 1, output) + error_at(src, "k));", 2, output) +
                error_at(src, "missing", 1, "use of undeclared identifier 'missing'") +
                error_at(src, "gone", 1, "use of undeclared identifier 'gone'"))
      << "an input need not be modifiable, and an untyped output is reported once";
  std::vector<ast_node *> defs = nodes_of(c, AST_NODE_TYPE_FUNCTION_DEF);
  ASSERT_EQ(defs.size(), 1u);
  ast_node *first = defs[0]->function_def.body->block.statements[1];
  ASSERT_EQ(first->type, AST_NODE_TYPE_ASM);
  ASSERT_EQ(first->asm_stmt.operand_count, 4);
  for (int i = 0; i < 4; i++) {
    SCOPED_TRACE(i);
    EXPECT_NE(first->asm_stmt.operands[i]->expr_type, nullptr) << "every operand is typed";
  }
  EXPECT_EQ(first->asm_stmt.operands[0]->symbol,
            defs[0]->function_def.body->block.statements[0]->symbol)
      << "out resolves to the local";
  EXPECT_EQ(first->asm_stmt.operands[1]->symbol, defs[0]->function_def.param_symbols[0])
      << "code resolves to the parameter";
}

TEST(TargetTypeTest, ALongBitFieldIsAsWideAsTheTargetsLong) {
  const char *src = "struct S { long x : 40; };\n";
  {
    Checked c;
    check(c, src);
    ASSERT_EQ(c.parse_errors, 0);
    EXPECT_EQ(c.diagnostics, error_at(src, "x", 1, "width exceeds its type for bit-field 'x'"));
  }
  TargetGuard guard(TARGET_LINUX_X64);
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics, "");
}

TEST(ConstantExpressionTest, BadIntegerConstantsAreReportedOnce) {
  expect_one_error("", {
                           {"18446744073709551616", "integer constant is too large"},
                           {"18446744073709551615", "integer constant is too large"},
                           {"9223372036854775808", "integer constant is too large"},
                       });
}

TEST(ConstantExpressionTest, AMisspelledNumberIsReportedOnceByTheParser) {
  for (const char *number :
       {"1uu", "1lll", "1lL", "12abc", "08", "0x", "1f", "1e", "1.2.3", "0x1.8"}) {
    Evaluated result = evaluate_in("", number);
    EXPECT_EQ(result.parse_errors, 1) << number;
    EXPECT_EQ(result.errors, 0) << number << "\n" << result.diagnostics;
    EXPECT_FALSE(result.has_value) << number;
  }
}

TEST(ConstantExpressionTest, CharacterConstantsAreIntExceptWideOnes) {
  expect_values("", {
                        {"'a'", 97},
                        {"'\\xff'", -1},
                        {"'\\0'", 0},
                        {"'\\n'", 10},
                        {"L'\\xff'", 255},
                        {"L'a' - 98 < 0", 1},
                        {"'ab'", 24930},
                        {"'abcde'", 1650680933},
                        {"'\\xff\\xff\\xff\\xff'", -1},
                        {"'\\377\\377'", 65535},
                        {"'\\x80\\0'", 32768},
                        {"'\\u00e9'", 50089},
                        {"'\\u20ac'", 14844588},
                        {"L'ab'", 98},
                        {"L'\\xffff'", 65535},
                        {"L'\\x8000' > 0", 1},
                        {"L'\\u20ac'", 8364},
                        {"L'\\U0001F600'", 56832},
                    });
}

TEST(ConstantExpressionTest, AMalformedCharacterConstantAddsNoSecondError) {
  Evaluated result = evaluate_in("", "'\\x'");
  EXPECT_EQ(result.parse_errors, 1);
  EXPECT_EQ(result.errors, 0) << result.diagnostics;
  EXPECT_FALSE(result.has_value);
}

TEST(ConstantExpressionTest, FloatingConstantsCountOnlyAsCastOperands) {
  expect_values("", {
                        {"(int)2.9", 2},
                        {"(int)2.9f", 2},
                        {"-(int)2.9", -2},
                        {"(_Bool)0.5", 1},
                        {"(unsigned char)255.0", 255},
                        {"(long long)1e9", 1000000000},
                        {"(_Bool)0.0", 0},
                        {"(unsigned long long)1e19 > 0", 1},
                    });
  expect_one_error("",
                   {
                       {"(int)1e30", "overflow in a constant expression"},
                       {"(char)300.0", "overflow in a constant expression"},
                       {"(long long)1e19", "overflow in a constant expression"},
                       {"1.5", "enumerator value is not an integer constant expression"},
                       {"(int)(2.5 + 1)", "enumerator value is not an integer constant expression"},
                   });
}

TEST(ConstantExpressionTest, OperatorsFollowC) {
  expect_values("", {
                        {"7 / 2", 3},
                        {"-7 / 2", -3},
                        {"7 % 3", 1},
                        {"-7 % 3", -1},
                        {"1 << 4", 16},
                        {"-16 >> 2", -4},
                        {"-1 >> 31", -1},
                        {"1u << 31 >> 31", 1},
                        {"0xF0 & 0x3C", 0x30},
                        {"0xF0 | 0x0F", 0xFF},
                        {"0xFF ^ 0x0F", 0xF0},
                        {"~0", -1},
                        {"!0", 1},
                        {"!5", 0},
                        {"-(-3)", 3},
                        {"+5", 5},
                        {"3 < 4", 1},
                        {"4 <= 4", 1},
                        {"5 > 4", 1},
                        {"4 >= 5", 0},
                        {"4 == 4", 1},
                        {"4 != 4", 0},
                        {"1 && 2", 1},
                        {"0 && 2", 0},
                        {"0 || 0", 0},
                        {"0 || 3", 1},
                        {"2 ? 10 : 20", 10},
                        {"0 ? 10 : 20", 20},
                        {"4294967295u + 1 == 0", 1},
                        {"0u - 1 == 4294967295u", 1},
                        {"-2147483647 - 1 < 0", 1},
                        {"65535 * 32768 > 0", 1},
                    });
}

TEST(ConstantExpressionTest, UnevaluatedOperandsAreNeverEvaluated) {
  expect_values("", {
                        {"0 && 1 / 0", 0},
                        {"1 || 1 / 0", 1},
                        {"1 ? 5 : 1 / 0", 5},
                        {"0 ? 1 / 0 : 6", 6},
                        {"0 && (1 << 40)", 0},
                    });
  expect_one_error("", {
                           {"1 && 1 / 0", "division by zero in a constant expression"},
                           {"0 || 1 / 0", "division by zero in a constant expression"},
                           {"0 ? 5 : 1 / 0", "division by zero in a constant expression"},
                       });
}

TEST(ConstantExpressionTest, EvaluationErrorsAreReportedOnce) {
  expect_one_error("",
                   {
                       {"1 / 0", "division by zero in a constant expression"},
                       {"1 % 0", "division by zero in a constant expression"},
                       {"1u / 0", "division by zero in a constant expression"},
                       {"2147483647 + 1", "overflow in a constant expression"},
                       {"-2147483647 - 2", "overflow in a constant expression"},
                       {"65536 * 65536", "overflow in a constant expression"},
                       {"9223372036854775807LL + 1", "overflow in a constant expression"},
                       {"(-9223372036854775807LL - 1) - 1", "overflow in a constant expression"},
                       {"3037000500LL * 3037000500LL", "overflow in a constant expression"},
                       {"(-2147483647 - 1) / -1", "overflow in a constant expression"},
                       {"(-9223372036854775807LL - 1) / -1", "overflow in a constant expression"},
                       {"-(-2147483647 - 1)", "overflow in a constant expression"},
                       {"1 << 31", "overflow in a constant expression"},
                       {"1 << 32", "shift count is out of range"},
                       {"1 << -1", "shift count is out of range"},
                       {"1u >> 32", "shift count is out of range"},
                       {"-1 << 1", "left shift of a negative value"},
                   });
}

TEST(ConstantExpressionTest, CommasAndNonConstantsAreNotConstantExpressions) {
  expect_one_error("int x;\n",
                   {
                       {"(1, 2)", "enumerator value is not an integer constant expression"},
                       {"0 && (1, 2)", "enumerator value is not an integer constant expression"},
                       {"(char *)1", "enumerator value is not an integer constant expression"},
                       {"x", "enumerator value is not an integer constant expression"},
                       {"x + 1", "enumerator value is not an integer constant expression"},
                       {"(int)&x", "enumerator value is not an integer constant expression"},
                       {"\"s\"[0]", "enumerator value is not an integer constant expression"},
                       {"missing + 1", "use of undeclared identifier 'missing'"},
                   });
}

TEST(ConstantExpressionTest, ATypeThatFailedToResolveAddsNoSecondError) {
  const char *src = "int T;\n"
                    "typedef int T, ;\n"
                    "void f(void) { enum { W = (T)1 }; }\n"
                    "enum { X = (enum Missing)1, Y = sizeof(enum Missing) };\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 1);
  EXPECT_EQ(c.errors, 3);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "(T)", 1, "expected a type name, found 'T'") +
                note_at(src, "T;", 1, "T") +
                error_at(src, "(enum Missing)", 1, "use of incomplete enum 'Missing'") +
                error_at(src, "(enum Missing)", 2, "use of incomplete enum 'Missing'"));
  symbol *y = find_symbol(c, "Y", SYMBOL_ENUM_CONSTANT);
  ASSERT_NE(y, nullptr);
  EXPECT_EQ(y->has_value, 0) << "a type that failed to resolve has no size";
}

TEST(ConstantExpressionTest, SizeofSizesScalarsPointersEnumsAndArrays) {
  expect_values("enum E { A };\ntypedef long T;\ntypedef int A3[3];\n",
                {
                    {"sizeof(char)", 1},
                    {"sizeof(short)", 2},
                    {"sizeof(int)", 4},
                    {"sizeof(long)", 4},
                    {"sizeof(long long)", 8},
                    {"sizeof(float)", 4},
                    {"sizeof(double)", 8},
                    {"sizeof(long double)", 16},
                    {"sizeof(_Bool)", 1},
                    {"sizeof(char *)", 8},
                    {"sizeof(int (*)(void))", 8},
                    {"sizeof(int[4])", 16},
                    {"sizeof(int[2][3])", 24},
                    {"sizeof(enum E)", 4},
                    {"sizeof(T)", 4},
                    {"sizeof(A3)", 12},
                    {"sizeof(int) - 5 > 0", 1},
                });
  expect_one_error(
      "struct Incomplete;\ntypedef int F(void);\n",
      {
          {"sizeof(void)", "invalid application of sizeof to an incomplete type"},
          {"sizeof(struct Incomplete)", "invalid application of sizeof to an incomplete type"},
          {"sizeof(int[])", "invalid application of sizeof to an incomplete type"},
          {"sizeof(F)", "invalid application of sizeof to a function type"},
      });
}

TEST(ConstantExpressionTest, SizeofOfARecordIsItsLayoutAndOfAnExpressionIsItsType) {
  expect_values("struct S { int a; };\n", {{"sizeof(struct S)", 4}, {"sizeof(struct S[2])", 8}});

  Evaluated broken = evaluate_in("struct B { int a; void v; };\n", "sizeof(struct B)");
  EXPECT_EQ(broken.errors, 1) << broken.diagnostics;
  EXPECT_FALSE(broken.has_value) << "a record with a bad member has no layout and adds no error";

  expect_values("int x;\nint a[4];\n", {{"sizeof x", 4}, {"sizeof a", 16}, {"sizeof(a + 0)", 8}});
}

TEST(EnumerationTest, ValuesCountUpAndTheTypeFollowsTheSign) {
  Checked c;
  check(c, "enum up { A, B, C = 10, D };\n"
           "enum down { N = -1, O };\n"
           "enum { UP_IS_UNSIGNED = (enum up)-1 > 0, DOWN_IS_SIGNED = (enum down)-1 < 0 };\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0) << c.diagnostics;

  const std::vector<ValueCase> expected = {{"A", 0},
                                           {"B", 1},
                                           {"C", 10},
                                           {"D", 11},
                                           {"N", -1},
                                           {"O", 0},
                                           {"UP_IS_UNSIGNED", 1},
                                           {"DOWN_IS_SIGNED", 1}};
  for (const ValueCase &test : expected) {
    symbol *sym = find_symbol(c, test.expression, SYMBOL_ENUM_CONSTANT);
    ASSERT_NE(sym, nullptr) << test.expression;
    EXPECT_EQ(sym->has_value, 1) << test.expression;
    EXPECT_EQ(sym->value, test.value) << test.expression;
  }
  symbol *up = find_symbol(c, "up", SYMBOL_TAG);
  symbol *down = find_symbol(c, "down", SYMBOL_TAG);
  ASSERT_NE(up, nullptr);
  ASSERT_NE(down, nullptr);
  EXPECT_EQ(up->enum_type, PRIM_UINT);
  EXPECT_EQ(down->enum_type, PRIM_INT);
}

TEST(EnumerationTest, ValuesMustFitInAnInt) {
  const char *src = "enum { P = 2147483647 };\n"
                    "enum { Q = 2147483648 };\n"
                    "enum { R = 2147483647, S };\n"
                    "int x;\n"
                    "enum { U = x };\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 3);
  EXPECT_EQ(
      c.diagnostics,
      error_at(src, "2147483648", 1, "enumerator value is not representable as an int 'Q'") +
          error_at(src, "enum { R", 1, "overflow in enumeration values 'S'") +
          error_at(src, "x }", 1, "enumerator value is not an integer constant expression 'U'"));

  symbol *p = find_symbol(c, "P", SYMBOL_ENUM_CONSTANT);
  symbol *q = find_symbol(c, "Q", SYMBOL_ENUM_CONSTANT);
  symbol *r = find_symbol(c, "R", SYMBOL_ENUM_CONSTANT);
  symbol *s = find_symbol(c, "S", SYMBOL_ENUM_CONSTANT);
  ASSERT_NE(p, nullptr);
  ASSERT_NE(q, nullptr);
  ASSERT_NE(r, nullptr);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(p->value, 2147483647);
  EXPECT_EQ(q->has_value, 0);
  EXPECT_EQ(r->has_value, 1);
  EXPECT_EQ(s->has_value, 0);
}

TEST(EnumerationTest, AnUnsignedValueAboveTheSignedRangeDoesNotFitAnInt) {
  expect_one_error(
      "", {
              {"18446744073709551615ull", "enumerator value is not representable as an int"},
              {"0x8000000000000000ull", "enumerator value is not representable as an int"},
          });
}

TEST(EnumerationTest, OneBadEnumeratorProducesOneError) {
  const char *src = "enum { T1 = 1 / 0, T2, T3 = 5 };\n"
                    "int arr[T2];\n"
                    "int ok[T3];\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 1);
  EXPECT_EQ(c.diagnostics, error_at(src, "/", 1, "division by zero in a constant expression"));

  symbol *t1 = find_symbol(c, "T1", SYMBOL_ENUM_CONSTANT);
  symbol *t2 = find_symbol(c, "T2", SYMBOL_ENUM_CONSTANT);
  symbol *t3 = find_symbol(c, "T3", SYMBOL_ENUM_CONSTANT);
  ASSERT_NE(t1, nullptr);
  ASSERT_NE(t2, nullptr);
  ASSERT_NE(t3, nullptr);
  EXPECT_EQ(t1->has_value, 0);
  EXPECT_EQ(t2->has_value, 0);
  EXPECT_EQ(t3->value, 5);
  ASSERT_NE(type_of(c, "arr"), nullptr);
  ASSERT_NE(type_of(c, "ok"), nullptr);
  EXPECT_EQ(type_of(c, "arr")->array_size, -1);
  EXPECT_EQ(type_of(c, "arr")->is_vla, 0);
  EXPECT_EQ(type_of(c, "ok")->array_size, 5);
}

TEST(ArraySizeTest, ConstantSizesAreEvaluatedAndTheRestClassified) {
  Checked c;
  check(c, "int a[0x10];\n"
           "int b[010];\n"
           "int c[2 + 3];\n"
           "enum { N = 4 };\n"
           "int d[N];\n"
           "int e[];\n"
           "char big[sizeof(int) * 3];\n"
           "struct S { int x; };\n"
           "typedef int A3[3];\n"
           "void f(int n) {\n"
           "  int v[n];\n"
           "  char w[sizeof(struct S)];\n"
           "  int q[n + 1 / 0];\n"
           "  int m[3][n];\n"
           "}\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0);
  EXPECT_EQ(c.diagnostics, "") << "a division by zero inside a VLA size is not a constant error";

  const std::vector<ValueCase> constant_sizes = {{"a", 16}, {"b", 8},    {"c", 5},
                                                 {"d", 4},  {"big", 12}, {"A3", 3}};
  for (const ValueCase &test : constant_sizes) {
    type_info *type = type_of(c, test.expression);
    ASSERT_NE(type, nullptr) << test.expression;
    EXPECT_EQ(type->array_size, test.value) << test.expression;
    EXPECT_EQ(type->is_vla, 0) << test.expression;
  }

  type_info *e = type_of(c, "e");
  type_info *v = type_of(c, "v");
  type_info *w = type_of(c, "w");
  type_info *q = type_of(c, "q");
  type_info *m = type_of(c, "m");
  ASSERT_NE(e, nullptr);
  ASSERT_NE(v, nullptr);
  ASSERT_NE(w, nullptr);
  ASSERT_NE(q, nullptr);
  ASSERT_NE(m, nullptr);
  EXPECT_EQ(e->array_size, -1);
  EXPECT_EQ(e->is_vla, 0) << "no size given";
  EXPECT_EQ(v->array_size, -1);
  EXPECT_EQ(v->is_vla, 1);
  EXPECT_EQ(w->array_size, 4);
  EXPECT_EQ(w->is_vla, 0);
  EXPECT_EQ(q->is_vla, 1);
  EXPECT_EQ(m->array_size, 3);
  EXPECT_EQ(m->is_vla, 0);
  ASSERT_NE(m->ptr_to, nullptr);
  EXPECT_EQ(m->ptr_to->is_vla, 1);
}

TEST(ArraySizeTest, NonPositiveAndHugeSizesAreRejected) {
  const char *src = "int z[0];\n"
                    "int u[0u];\n"
                    "int y[-1];\n"
                    "char huge[0xFFFFFFFFFFFFFFFFull];\n"
                    "int ok[1];\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 4);
  EXPECT_EQ(c.diagnostics, error_at(src, "z[", 1, "array size must be greater than zero") +
                               error_at(src, "u[", 1, "array size must be greater than zero") +
                               error_at(src, "y[", 1, "array size must be greater than zero") +
                               error_at(src, "huge", 1, "array is too large"));
  EXPECT_EQ(type_of(c, "z")->array_size, -1);
  EXPECT_EQ(type_of(c, "u")->array_size, -1);
  EXPECT_EQ(type_of(c, "y")->array_size, -1);
  EXPECT_EQ(type_of(c, "huge")->array_size, -1);
  EXPECT_EQ(type_of(c, "ok")->array_size, 1);
}

TEST(FunctionDefinitionTest, AFunctionSymbolHasTheFunctionsType) {
  Checked c;
  check(c, "typedef int count;\n"
           "struct S;\n"
           "count f(struct S *p, int n);\n"
           "count f(struct S *p, int n) { return n; }\n"
           "struct S *g(void) { return 0; }\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0) << c.diagnostics;

  std::vector<ast_node *> f = declarations(c, "f");
  ASSERT_EQ(f.size(), 2u);
  ASSERT_EQ(f[0]->type, AST_NODE_TYPE_VAR_DECL) << "the prototype is a declaration";
  ASSERT_EQ(f[1]->type, AST_NODE_TYPE_FUNCTION_DEF);
  ASSERT_NE(f[0]->symbol, nullptr);
  EXPECT_EQ(f[1]->symbol, f[0]->symbol);
  EXPECT_EQ(f[0]->symbol->kind, SYMBOL_FUNC);
  EXPECT_EQ(f[0]->symbol->type, f[0]->var_decl.type)
      << "the first declaration's type, until Step 11";

  type_info *definition = f[1]->function_def.type;
  ASSERT_NE(definition, nullptr);
  ASSERT_NE(definition->ptr_to, nullptr);
  std::vector<ast_node *> count = declarations(c, "count");
  ASSERT_EQ(count.size(), 1u);
  EXPECT_EQ(definition->ptr_to->symbol, count[0]->symbol) << "the return type is resolved";

  symbol **params = f[1]->function_def.param_symbols;
  ASSERT_NE(params, nullptr);
  ASSERT_NE(params[0], nullptr);
  ASSERT_NE(params[1], nullptr);
  EXPECT_STREQ(params[0]->name, "p");
  EXPECT_STREQ(params[1]->name, "n");
  EXPECT_EQ(params[0]->type, definition->param_types[0]);

  std::vector<ast_node *> g = declarations(c, "g");
  ASSERT_EQ(g.size(), 1u);
  ASSERT_NE(g[0]->symbol, nullptr);
  EXPECT_EQ(g[0]->symbol->type, g[0]->function_def.type);
  type_info *returned = base_of(g[0]->function_def.type->ptr_to);
  ASSERT_NE(returned, nullptr);
  EXPECT_NE(returned->symbol, nullptr) << "a tag in the return type is resolved";
}

TEST(FunctionDefinitionTest, ADefinitionsParametersAreResolvedOnce) {
  const char *src = "int f(int a[0]) { return 0; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 1);
  EXPECT_EQ(c.diagnostics, error_at(src, "f(", 1, "array size must be greater than zero"));
}

TEST(FunctionDefinitionTest, ATypedefThatNamesAFunctionTypeDeclaresAFunction) {
  Checked c;
  check(c, "typedef int F(void);\n"
           "static F k;\n"
           "void g(void) { F h; }\n");
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0) << c.diagnostics;

  std::vector<ast_node *> k = declarations(c, "k");
  std::vector<ast_node *> h = declarations(c, "h");
  ASSERT_EQ(k.size(), 1u);
  ASSERT_EQ(h.size(), 1u);
  ASSERT_NE(k[0]->symbol, nullptr);
  ASSERT_NE(h[0]->symbol, nullptr);
  EXPECT_EQ(k[0]->symbol->kind, SYMBOL_FUNC);
  EXPECT_EQ(k[0]->symbol->linkage, LINKAGE_INTERNAL);
  EXPECT_EQ(h[0]->symbol->kind, SYMBOL_FUNC);
  EXPECT_EQ(h[0]->symbol->linkage, LINKAGE_EXTERNAL)
      << "a block-scope function declaration has linkage";
}

TEST(FunctionDefinitionTest, AnUnresolvedTypedefStaysAnObject) {
  Checked c;
  check(c, "typedef int U, ;\n"
           "void g(void) {\n"
           "  U y;\n"
           "}\n");
  ASSERT_EQ(c.parse_errors, 1);
  EXPECT_EQ(c.errors, 1) << c.diagnostics;
  std::vector<ast_node *> y = declarations(c, "y");
  ASSERT_EQ(y.size(), 1u);
  ASSERT_NE(y[0]->symbol, nullptr);
  EXPECT_EQ(y[0]->symbol->kind, SYMBOL_VAR);
}

TEST(FunctionDefinitionTest, AFunctionCannotBeInitialized) {
  const char *src = "int f(void) = 1;\n"
                    "typedef int F(void);\n"
                    "F g = 0;\n"
                    "int ok = 1;\n"
                    "int h(void) = missing;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 4);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "f(", 1, "cannot initialize function 'f'") +
                error_at(src, "g =", 1, "cannot initialize function 'g'") +
                error_at(src, "h(", 1, "cannot initialize function 'h'") +
                error_at(src, "missing", 1, "use of undeclared identifier 'missing'"));
}

TEST(SemaTest, StorageClassesMustSuitTheScope) {
  const char *src = "auto int a;\n"
                    "register int b;\n"
                    "register int f(void) { return 0; }\n"
                    "static int s;\n"
                    "extern int e;\n"
                    "void g(void) {\n"
                    "  auto int c;\n"
                    "  register int d;\n"
                    "  static int h(void);\n"
                    "  extern int i(void);\n"
                    "  int j(void);\n"
                    "  static int k;\n"
                    "  auto int m(void);\n"
                    "  typedef int n(void);\n"
                    "}\n"
                    "static int p(void) { return 0; }\n"
                    "extern int q(void) { return 0; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 5);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "a;", 1, "invalid storage class at file scope for 'a'") +
                error_at(src, "b;", 1, "invalid storage class at file scope for 'b'") +
                error_at(src, "f(", 1, "invalid storage class at file scope for 'f'") +
                error_at(src, "h(", 1, "invalid storage class for block-scope function 'h'") +
                error_at(src, "m(", 1, "invalid storage class for block-scope function 'm'"));
}

TEST(DefinitionScopeTest, TagsDefinedInParametersAndTypeNamesBelongToTheRightScope) {
  const char *src = "void f(struct Q { int y; } q);\n"
                    "struct Q *after;\n"
                    "int g(struct R { int y; } r) { struct R copy = r; return copy.y; }\n"
                    "void h(void) { long n = sizeof(struct C { int a; }); struct C c; }\n"
                    "void k(void) { int y = ((struct E { int y; }){4}).y; struct E e; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0) << c.diagnostics;

  std::vector<type_info *> q = named_types(c, TYPE_STRUCT, "Q");
  ASSERT_EQ(q.size(), 2u);
  ASSERT_NE(q[0]->symbol, nullptr);
  ASSERT_NE(q[1]->symbol, nullptr);
  EXPECT_NE(q[0]->symbol, q[1]->symbol) << "a prototype's tag ends with the prototype";
  EXPECT_EQ(q[1]->symbol->is_defined, 0);

  std::vector<type_info *> r = named_types(c, TYPE_STRUCT, "R");
  ASSERT_EQ(r.size(), 2u);
  ASSERT_NE(r[0]->symbol, nullptr);
  EXPECT_EQ(r[0]->symbol, r[1]->symbol) << "a definition's parameter tag is visible in its body";
  EXPECT_EQ(r[1]->symbol->is_defined, 1);

  std::vector<type_info *> cs = named_types(c, TYPE_STRUCT, "C");
  ASSERT_EQ(cs.size(), 2u);
  ASSERT_NE(cs[0]->symbol, nullptr);
  EXPECT_EQ(cs[0]->symbol, cs[1]->symbol) << "a tag defined in sizeof belongs to the block";
  EXPECT_EQ(cs[1]->symbol->is_defined, 1);

  std::vector<type_info *> es = named_types(c, TYPE_STRUCT, "E");
  ASSERT_EQ(es.size(), 2u);
  ASSERT_NE(es[0]->symbol, nullptr);
  EXPECT_EQ(es[0]->symbol, es[1]->symbol)
      << "a tag defined in a compound literal belongs to the block";
  EXPECT_EQ(es[1]->symbol->is_defined, 1);
}

TEST(DefinitionScopeTest, EnumerationConstantsFromATypeNameHaveValues) {
  expect_values("", {
                        {"sizeof(enum { K = 3 }) + K", 7},
                        {"(enum { L = 5 })0 + L", 5},
                    });
}

TEST(DefinitionScopeTest, AnOldStyleDefinitionsParametersAreDeclared) {
  const char *src = "int f(a, b) int a; char *b; { return a + *b; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0) << c.diagnostics;
  std::vector<ast_node *> a_uses = uses(c, "a");
  std::vector<ast_node *> b_uses = uses(c, "b");
  ASSERT_EQ(a_uses.size(), 1u);
  ASSERT_EQ(b_uses.size(), 1u);
  ASSERT_NE(a_uses[0]->symbol, nullptr);
  ASSERT_NE(b_uses[0]->symbol, nullptr);
  ASSERT_NE(a_uses[0]->symbol->type, nullptr);
  EXPECT_EQ(a_uses[0]->symbol->type->prim, PRIM_INT);
  ASSERT_NE(b_uses[0]->symbol->type, nullptr);
  EXPECT_EQ(b_uses[0]->symbol->type->kind, TYPE_POINTER);
}

TEST(DefinitionScopeTest, AStarArrayIsOnlyForPrototypes) {
  const char *src = "void f(int a[*]) {}\n"
                    "void g(int a[*]);\n"
                    "void h(void (*p)(int a[*])) {}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 1);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "f(", 1,
                     "[*] is only allowed in a function prototype, not in the definition of 'f'"));
}

TEST(ForLoopDeclarationTest, OnlyAutomaticObjectsMayBeDeclared) {
  const char *src = "void f(void) {\n"
                    "  for (static int i = 0; ;) break;\n"
                    "  for (extern int j; ;) break;\n"
                    "  for (typedef int T; ;) break;\n"
                    "  for (int g(void); ;) break;\n"
                    "  for (enum { E } e = E; ;) break;\n"
                    "  for (struct S { int x; } s; ;) break;\n"
                    "  for (auto int a = 0, b; ;) break;\n"
                    "  for (register int r = 0; ;) break;\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 6);
  const std::string message = "a for loop declaration may only declare automatic objects";
  EXPECT_EQ(c.diagnostics, error_at(src, "i =", 1, message + ", not 'i'") +
                               error_at(src, "j;", 1, message + ", not 'j'") +
                               error_at(src, "T;", 1, message + ", not 'T'") +
                               error_at(src, "g(", 1, message + ", not 'g'") +
                               error_at(src, "enum {", 1, message) +
                               error_at(src, "struct S", 1, message));
}

TEST(SemaTest, DiagnosticsNameTheFileWhenTheTokensHaveOne) {
  lexer lex;
  lexer_init(&lex, "void f(void) {\n  int y;\n  int y;\n  z = 1;\n}\n");
  lex.file = "unit.c";
  lex.line_offset = 4;
  parser p;
  parser_init(&p, &lex);
  ast_node *program = parse_program(&p);
  EXPECT_EQ(p.had_error, 0);
  parser_destroy(&p);
  symbol_table *table = symbol_table_create();
  testing::internal::CaptureStderr();
  int errors = sema_check(table, program);
  std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(errors, 2);
  EXPECT_EQ(diagnostics, "unit.c:7:7: error: redeclaration of 'y'\n"
                         "unit.c:6:7: note: previous declaration of 'y' is here\n"
                         "unit.c:8:3: error: use of undeclared identifier 'z'\n");
  symbol_table_destroy(table);
  free_ast(program);
}

TEST(SemaTest, InlineOnlyDeclaresFunctions) {
  const char *src = "inline int x;\n"
                    "inline int f(void);\n"
                    "typedef int F(void);\n"
                    "inline F g;\n"
                    "inline typedef int H(void);\n"
                    "void k(void) { inline int y; }\n"
                    "typedef int U, ;\n"
                    "inline U m;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 1);
  EXPECT_EQ(c.errors, 4) << c.diagnostics;
  const std::string message = "inline can only declare a function, not";
  EXPECT_EQ(c.diagnostics, error_at(src, "x;", 1, message + " 'x'") +
                               error_at(src, "H(", 1, message + " 'H'") +
                               error_at(src, "y;", 1, message + " 'y'") +
                               error_at(src, "m;", 1, "unknown type name 'U'"))
      << "a type that failed to resolve adds no second error";
}

TEST(SemaTest, EveryFunctionBodyDeclaresItsOwnFunc) {
  const char *src = "const char *f(void) { return __func__; }\n"
                    "const char *gg(void) { return __func__; }\n"
                    "const char *s = __func__;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 1);
  EXPECT_EQ(c.diagnostics, error_at(src, "__func__", 3, "use of undeclared identifier '__func__'"));
  std::vector<ast_node *> func = uses(c, "__func__");
  ASSERT_EQ(func.size(), 3u);
  ASSERT_NE(func[0]->symbol, nullptr);
  ASSERT_NE(func[1]->symbol, nullptr);
  EXPECT_NE(func[0]->symbol, func[1]->symbol);
  EXPECT_EQ(func[2]->symbol, nullptr);
  const struct {
    symbol *declared;
    long long size;
  } expected[] = {{func[0]->symbol, 2}, {func[1]->symbol, 3}};
  for (const auto &test : expected) {
    EXPECT_EQ(test.declared->kind, SYMBOL_VAR);
    EXPECT_EQ(test.declared->linkage, LINKAGE_NONE);
    ASSERT_NE(test.declared->type, nullptr);
    EXPECT_EQ(test.declared->type->kind, TYPE_ARRAY);
    EXPECT_EQ(test.declared->type->array_size, test.size) << "the name and its terminator";
    ASSERT_NE(test.declared->type->ptr_to, nullptr);
    EXPECT_EQ(test.declared->type->ptr_to->prim, PRIM_CHAR);
    EXPECT_EQ(test.declared->type->ptr_to->is_const, 1);
  }
}

TEST(SemaTest, ABlockScopeDeclarationWithLinkageCannotBeInitialized) {
  const char *src = "extern int e = 1;\n"
                    "void f(void) {\n"
                    "  extern int a = 1;\n"
                    "  static int b = 2;\n"
                    "  int c = 3;\n"
                    "  extern int d;\n"
                    "  extern int g(void) = 0;\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 2);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "a =", 1, "cannot initialize block-scope declaration with linkage 'a'") +
                error_at(src, "g(", 1, "cannot initialize function 'g'"))
      << "a function initializer is reported once, as a function";
}

TEST(CompatibilityTest, RedeclarationsMustHaveCompatibleTypes) {
  const struct {
    const char *source;
    int conflicts;
  } cases[] = {
      {"char c; char c;", 0},
      {"char c; signed char c;", 1},
      {"double _Complex z; long double _Complex z;", 1},
      {"double _Complex z; double z;", 1},
      {"int *p; const int *p;", 1},
      {"int *p; int *const p;", 1},
      {"volatile int v; int v;", 1},
      {"int *restrict r; int *r;", 1},
      {"const int *p; int const *p;", 0},
      {"int a[3]; int a[4];", 1},
      {"int a[3]; int a[];", 0},
      {"int a[]; int a[3];", 0},
      {"int a[3]; long a[3];", 1},
      {"typedef int A[3]; const A x; extern const int x[3];", 0},
      {"typedef int A[3]; const A x; extern int x[3];", 1},
      {"typedef const int CI; CI x; extern const int x;", 0},
      {"typedef const int CI; CI x; extern int x;", 1},
      {"enum E { R }; enum E e; extern unsigned int e;", 0},
      {"enum E { R }; enum E e; extern int e;", 1},
      {"enum E { R = -1 }; enum E e; extern int e;", 0},
      {"extern unsigned int e; enum E { R } e;", 0},
      {"extern int e; enum E { R } e;", 1},
      {"enum E { R }; enum F { S }; enum E e; extern enum F e;", 1},
      {"struct S { int a; }; extern struct S s; extern struct S s;", 0},
      {"struct S { int a; }; struct T { int a; }; extern struct S s; extern struct T s;", 1},
      {"int f(int a[3]); int f(int *a);", 0},
      {"int f(int a[const 3]); int f(int *const a);", 0},
      {"int f(int a[]); int f(const int *a);", 1},
      {"int f(const int a); int f(int a);", 0},
      {"int f(int a); int f(const int a);", 0},
      {"int f(long a); int f(int a);", 1},
      {"int f(int *a); int f(int a);", 1},
      {"int f(int a); int f(int *a);", 1},
      {"int f(int g(void)); int f(int (*g)(void));", 0},
      {"int f(int **p); int f(int *const *p);", 1},
      {"typedef int A[3]; int f(const A a); int f(const int *a);", 0},
      {"typedef int A[3]; int f(const A a); int f(int *a);", 1},
      {"typedef int *IP; int f(const IP p); int f(int *p);", 0},
      {"int f(); int f();", 0},
      {"int f(); int f(int, double);", 0},
      {"int f(); int f(long, int *);", 0},
      {"int f(); int f(int, float);", 1},
      {"int f(); int f(char);", 1},
      {"int f(); int f(short);", 1},
      {"int f(); int f(_Bool);", 1},
      {"int f(); int f(float _Complex);", 0},
      {"int f(int, ...); int f();", 1},
      {"int f(); int f(int, ...);", 1},
      {"int f(void); int f();", 0},
      {"int f(a) char a; { return a; } int f(char);", 1},
      {"int f(a) char a; { return a; } int f(int);", 0},
      {"int f(a) float a; { return 0; } int f(double);", 0},
      {"int f(a) float a; { return 0; } int f(float);", 1},
      {"int f(a) long a; { return 0; } int f(long);", 0},
      {"int f(a, b) int a, b; { return a; } int f(int);", 1},
      {"int f(int); long f(int);", 1},
      {"const int f(void); int f(void);", 1},
      {"int f(int, int); int f(int);", 1},
      {"int f(int); int f(int, int);", 1},
      {"int f(int); int f(int, ...);", 1},
      {"int f(int (*)(int)); int f(int (*)());", 0},
      {"int f(int (*)(char)); int f(int (*)());", 1},
      {"int (*fp)(int); int (*fp)(long);", 1},
  };
  for (const auto &test : cases) {
    SCOPED_TRACE(test.source);
    Checked c;
    check(c, test.source);
    ASSERT_EQ(c.parse_errors, 0);
    EXPECT_EQ(c.errors, test.conflicts) << c.diagnostics;
    EXPECT_EQ(c.diagnostics.find("conflicting types for") != std::string::npos, test.conflicts == 1)
        << c.diagnostics;
  }
}

TEST(CompatibilityTest, ADefinitionIsComparedWithItsParametersResolved) {
  const struct {
    const char *source;
    int conflicts;
  } cases[] = {
      {"typedef long L; int f(int); int f(L x) { return 0; }", 1},
      {"typedef int I; int f(int); int f(I x) { return x; }", 0},
      {"struct A; struct B; int f(struct A *); int f(struct B *x) { return 0; }", 1},
      {"struct A; int f(struct A *); int f(struct A *x) { return 0; }", 0},
      {"int f(struct A *); int f(struct A *x) { return 0; }", 1},
      {"enum E { X }; enum F { Y = -1 }; int f(enum F); int f(enum E x) { return 0; }", 1},
      {"typedef long L; int f(int); int f(a) L a; { return 0; }", 1},
      {"typedef int I; int f(int); int f(a) I a; { return a; }", 0},
      {"struct A; struct B; int f(struct A *); int f(a) struct B *a; { return 0; }", 1},
      {"struct A; struct B; int (*f(struct A *))(void); int (*f(struct B *x))(void) { return 0; }",
       1},
  };
  for (const auto &test : cases) {
    SCOPED_TRACE(test.source);
    Checked c;
    check(c, test.source);
    ASSERT_EQ(c.parse_errors, 0);
    EXPECT_EQ(c.errors, test.conflicts) << c.diagnostics;
    EXPECT_EQ(c.diagnostics.find("conflicting types for") != std::string::npos, test.conflicts == 1)
        << c.diagnostics;
  }
}

TEST(CompatibilityTest, ADefinitionsNameIsDeclaredInTheEnclosingScopeAfterItsParameters) {
  const char *src = "struct A;\n"
                    "int f(struct A *);\n"
                    "int f(struct A *a) { return f(a); }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0) << c.diagnostics;
  std::vector<ast_node *> prototypes = nodes_of(c, AST_NODE_TYPE_VAR_DECL);
  std::vector<ast_node *> definitions = nodes_of(c, AST_NODE_TYPE_FUNCTION_DEF);
  ASSERT_EQ(prototypes.size(), 1u);
  ASSERT_EQ(definitions.size(), 1u);
  symbol *f = prototypes[0]->symbol;
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(definitions[0]->symbol, f) << "the definition joins the file-scope declaration";
  EXPECT_TRUE(f->is_defined);
  int uses = 0;
  for (ast_node *n : nodes_of(c, AST_NODE_TYPE_IDENTIFIER)) {
    if (std::strcmp(n->tok.value, "f") == 0) {
      EXPECT_EQ(n->symbol, f);
      uses++;
    }
  }
  EXPECT_EQ(uses, 1);
}

TEST(CompatibilityTest, AnInlineDefinitionThatConflictsKeepsItsKind) {
  const char *src = "static int hidden;\n"
                    "typedef int k;\n"
                    "inline int k(void) { return hidden; }\n"
                    "int v;\n"
                    "inline int v(void) { return hidden; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "k(void)", 1, "redeclaration of 'k'") + note_at(src, "k;", 1, "k") +
                error_at(src, "hidden; }", 1,
                         "an inline definition with external linkage cannot refer to internal "
                         "linkage identifier 'hidden'") +
                error_at(src, "v(void)", 1, "conflicting kind of symbol for 'v'") +
                note_at(src, "v;", 1, "v"))
      << "a definition replacing a typedef is a new inline function; one replacing an object is "
         "not a function at all";
}

TEST(CompatibilityTest, ATypeThatFailedToResolveConflictsWithNothing) {
  const char *src = "typedef int U, ;\n"
                    "U x;\n"
                    "extern int x;\n"
                    "extern double x;\n"
                    "int g(U);\n"
                    "int g(int *);\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 1);
  EXPECT_EQ(c.errors, 2) << c.diagnostics;
  EXPECT_EQ(c.diagnostics, error_at(src, "x;", 1, "unknown type name 'U'") +
                               error_at(src, "g(U", 1, "unknown type name 'U'"));
}

TEST(CompatibilityTest, AnIdentifierListLeftByAParseErrorConflictsWithNothing) {
  const char *src = "int g(U);\n"
                    "int g(int *);\n"
                    "int k(int);\n"
                    "int k(V);\n"
                    "int (*p)(int *) = g;\n";
  Checked c;
  check(c, src);
  EXPECT_EQ(c.parse_errors, 2) << "one for each identifier list";
  EXPECT_EQ(c.errors, 0) << c.diagnostics;
}

TEST(RedeclarationTest, KindAndLinkageMustAgree) {
  const char *src = "double q;\n"
                    "int x;\n"
                    "int x(void);\n"
                    "static int y;\n"
                    "int y;\n"
                    "int z;\n"
                    "static int z;\n"
                    "static int w;\n"
                    "extern int w;\n"
                    "static int f(void);\n"
                    "int f(void);\n"
                    "void g(void) { extern int h; }\n"
                    "int h(void);\n"
                    "static int k;\n"
                    "void m(void) { int k; { extern int k; } }\n"
                    "void n(void) { int q; extern int q; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 6) << c.diagnostics;
  EXPECT_EQ(c.diagnostics,
            error_at(src, "x(", 1, "conflicting kind of symbol for 'x'") +
                note_at(src, "x;", 1, "x") + error_at(src, "y;", 2, "conflicting linkage for 'y'") +
                note_at(src, "y;", 1, "y") + error_at(src, "z;", 2, "conflicting linkage for 'z'") +
                note_at(src, "z;", 1, "z") +
                error_at(src, "h(", 1, "conflicting kind of symbol for 'h'") +
                note_at(src, "h;", 1, "h") + error_at(src, "k;", 3, "conflicting linkage for 'k'") +
                note_at(src, "k;", 1, "k") + error_at(src, "q;", 3, "redeclaration of 'q'") +
                note_at(src, "q;", 2, "q"))
      << "a declaration hiding a local is checked once, against that local";
}

TEST(RedeclarationTest, DeclarationsInDifferentScopesMustAgree) {
  const char *src = "void g(void) { extern int h; extern double h; }\n"
                    "void k(void) { extern int m; }\n"
                    "double m;\n"
                    "int n(int);\n"
                    "void p(void) { int n(long); }\n"
                    "void r(void) { extern int s; }\n"
                    "int s;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 3) << c.diagnostics;
  EXPECT_EQ(c.diagnostics,
            error_at(src, "h;", 2, "conflicting types for 'h'") + note_at(src, "h;", 1, "h") +
                error_at(src, "m;", 2, "conflicting types for 'm'") + note_at(src, "m;", 1, "m") +
                error_at(src, "n(long", 1, "conflicting types for 'n'") +
                note_at(src, "n(int", 1, "n"));
}

TEST(RedeclarationTest, ACompatibleRedeclarationCompletesTheSymbolsType) {
  const char *src = "int a[];\n"
                    "int a[4];\n"
                    "extern int b[4];\n"
                    "int b[];\n"
                    "int f();\n"
                    "int f(int);\n"
                    "int g(int);\n"
                    "int g();\n"
                    "int c[3];\n"
                    "int c[5];\n"
                    "int d[];\n"
                    "long d[4];\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 2) << c.diagnostics;
  const struct {
    const char *name;
    int kept;
  } expected[] = {{"a", 1}, {"b", 0}, {"f", 1}, {"g", 0}, {"c", 0}, {"d", 0}};
  for (const auto &test : expected) {
    SCOPED_TRACE(test.name);
    std::vector<ast_node *> decls = declarations(c, test.name);
    ASSERT_EQ(decls.size(), 2u);
    ASSERT_NE(decls[0]->symbol, nullptr);
    EXPECT_EQ(decls[0]->symbol, decls[1]->symbol);
    EXPECT_EQ(decls[0]->symbol->type, decls[test.kept]->var_decl.type);
  }
  EXPECT_EQ(declarations(c, "a")[0]->symbol->type->array_size, 4);
  EXPECT_EQ(declarations(c, "f")[0]->symbol->type->has_prototype, 1);
}

TEST(DefinitionTest, EachObjectAndFunctionIsDefinedOnce) {
  const char *src = "int x;\n"
                    "int x = 1;\n"
                    "int x;\n"
                    "int x = 2;\n"
                    "int f(void) { return 0; }\n"
                    "int f(void) { return 1; }\n"
                    "extern int e = 1;\n"
                    "int e = 2;\n"
                    "int y(void) { return 0; }\n"
                    "int y = 1;\n"
                    "int z = 1;\n"
                    "int z(void) { return 0; }\n"
                    "int u = 1;\n"
                    "int u(void) = 2;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 7) << c.diagnostics;
  EXPECT_EQ(c.diagnostics,
            error_at(src, "x = 2", 1, "redefinition of 'x'") + note_at(src, "x = 1", 1, "x") +
                error_at(src, "f(void) { return 1", 1, "redefinition of 'f'") +
                note_at(src, "f(void) { return 0", 1, "f") +
                error_at(src, "e = 2", 1, "redefinition of 'e'") + note_at(src, "e = 1", 1, "e") +
                error_at(src, "y = 1", 1, "conflicting kind of symbol for 'y'") +
                note_at(src, "y(", 1, "y") +
                error_at(src, "z(", 1, "conflicting kind of symbol for 'z'") +
                note_at(src, "z = 1", 1, "z") +
                error_at(src, "u(", 1, "conflicting kind of symbol for 'u'") +
                note_at(src, "u = 1", 1, "u") +
                error_at(src, "u(", 1, "cannot initialize function 'u'"))
      << "a declaration of the wrong kind is not also a redefinition";
  std::vector<ast_node *> x = declarations(c, "x");
  ASSERT_EQ(x.size(), 4u);
  ASSERT_NE(x[0]->symbol, nullptr);
  EXPECT_EQ(x[0]->symbol->is_defined, 1);
  EXPECT_EQ(x[0]->symbol->is_tentative, 1);
  EXPECT_EQ(x[0]->symbol->definition, x[1]);
  std::vector<ast_node *> f = declarations(c, "f");
  ASSERT_EQ(f.size(), 2u);
  EXPECT_EQ(f[0]->symbol->definition, f[0]);
}

TEST(DefinitionTest, TentativeDefinitionsMustEndUpComplete) {
  const char *src = "struct S a;\n"
                    "struct S a;\n"
                    "struct T b;\n"
                    "struct T { int m; };\n"
                    "static int sized[sizeof(struct T)];\n"
                    "int c[];\n"
                    "extern struct U d;\n"
                    "static int e[];\n"
                    "static struct V f;\n"
                    "struct S g;\n"
                    "void v;\n"
                    "struct S i = {0};\n"
                    "struct S j, k;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 8) << c.diagnostics;
  EXPECT_EQ(
      c.diagnostics,
      error_at(src, "e[", 1, "incomplete type for tentative definition with internal linkage 'e'") +
          error_at(src, "f;", 1,
                   "incomplete type for tentative definition with internal linkage 'f'") +
          error_at(src, "i =", 1, "an object with incomplete type cannot be initialized") +
          error_at(src, "a;", 1, "incomplete type for tentative definition 'a'") +
          error_at(src, "g;", 1, "incomplete type for tentative definition 'g'") +
          error_at(src, "v;", 1, "incomplete type for tentative definition 'v'") +
          error_at(src, "j,", 1, "incomplete type for tentative definition 'j'") +
          error_at(src, "k;", 1, "incomplete type for tentative definition 'k'"))
      << "internal linkage is checked at the declaration, external at the end, in source order";
  std::vector<ast_node *> a = declarations(c, "a");
  ASSERT_EQ(a.size(), 2u);
  ASSERT_NE(a[0]->symbol, nullptr);
  EXPECT_EQ(a[0]->symbol->is_tentative, 1);
  EXPECT_EQ(a[0]->symbol->is_defined, 0);
  EXPECT_EQ(a[0]->symbol->definition, a[0]);
  std::vector<ast_node *> d = declarations(c, "d");
  ASSERT_EQ(d.size(), 1u);
  ASSERT_NE(d[0]->symbol, nullptr);
  EXPECT_EQ(d[0]->symbol->is_tentative, 0);
}

TEST(VariablyModifiedTest, OnlyBlockScopeNamesWithoutLinkage) {
  const char *src = "int n;\n"
                    "int a[n];\n"
                    "typedef int T[n];\n"
                    "int (*p)[n];\n"
                    "static int s[n];\n"
                    "void g(int m) {\n"
                    "  int ok[m];\n"
                    "  static int bad[m];\n"
                    "  static int (*fine)[m];\n"
                    "  static int (*pointers[2])[m];\n"
                    "  extern int ext[m];\n"
                    "  typedef int L[m];\n"
                    "  L local;\n"
                    "  static L also_bad;\n"
                    "  struct S { int member[m]; };\n"
                    "}\n"
                    "int f(int k, int arr[k]);\n"
                    "int f(int k, int arr[*]);\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 8) << c.diagnostics;
  EXPECT_EQ(
      c.diagnostics,
      error_at(src, "a[", 1, "variably modified type at file scope for 'a'") +
          error_at(src, "T[", 1, "variably modified type at file scope for 'T'") +
          error_at(src, "p)", 1, "variably modified type at file scope for 'p'") +
          error_at(src, "s[", 1, "variably modified type at file scope for 's'") +
          error_at(src, "bad[", 1, "variable length array with static storage duration for 'bad'") +
          error_at(src, "ext[", 1, "variably modified type with linkage for 'ext'") +
          error_at(src, "also_bad", 1,
                   "variable length array with static storage duration for 'also_bad'") +
          error_at(src, "member[", 1, "variably modified type for member 'member'"));
}

TEST(ParameterListTest, ATypedefOfVoidAloneMeansNoParameters) {
  const char *src = "typedef void V;\n"
                    "typedef const void CV;\n"
                    "typedef const V CV2;\n"
                    "int a(V);\n"
                    "int a(void);\n"
                    "int b(void);\n"
                    "int b(V) { return 0; }\n"
                    "int c(const V);\n"
                    "int d(CV);\n"
                    "int e(V, ...);\n"
                    "int g(V x);\n"
                    "int k(V, V);\n"
                    "int m(V *);\n"
                    "int n(CV2);\n"
                    "int (*fp)(V);\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0) << c.diagnostics;
  const struct {
    const char *name;
    int count;
  } expected[] = {{"a", 0}, {"c", 1}, {"d", 1}, {"e", 1}, {"g", 1}, {"k", 2}, {"m", 1}, {"n", 1}};
  for (const auto &test : expected) {
    SCOPED_TRACE(test.name);
    std::vector<ast_node *> decls = declarations(c, test.name);
    ASSERT_FALSE(decls.empty());
    ASSERT_EQ(decls[0]->type, AST_NODE_TYPE_VAR_DECL);
    EXPECT_EQ(decls[0]->var_decl.type->param_count, test.count);
    EXPECT_EQ(decls[0]->var_decl.type->has_prototype, 1);
  }
  std::vector<ast_node *> b = declarations(c, "b");
  ASSERT_EQ(b.size(), 2u);
  ASSERT_EQ(b[1]->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_EQ(b[1]->function_def.type->param_count, 0);
  EXPECT_EQ(b[1]->function_def.param_symbols, nullptr);
  std::vector<ast_node *> fp = declarations(c, "fp");
  ASSERT_EQ(fp.size(), 1u);
  ASSERT_NE(fp[0]->var_decl.type->ptr_to, nullptr);
  EXPECT_EQ(fp[0]->var_decl.type->ptr_to->param_count, 0);
}

TEST(ParameterListTest, DefinitionParametersNeedNamesAndCompleteTypes) {
  const char *src = "typedef void V;\n"
                    "struct S;\n"
                    "int a(V x) { return 0; }\n"
                    "int b(void x) { return 0; }\n"
                    "int c(struct S s) { return 0; }\n"
                    "int d(struct S *s) { return 0; }\n"
                    "int e(int arr[]) { return 0; }\n"
                    "int f(int) { return 0; }\n"
                    "int g(int p, char) { return p; }\n"
                    "int h(void, int q) { return q; }\n"
                    "int i(const void) { return 0; }\n"
                    "int j(struct S s, void t) { return 0; }\n"
                    "int l(int, int) { return 0; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 10) << c.diagnostics;
  const std::string unnamed = "a parameter in a function definition needs a name";
  EXPECT_EQ(c.diagnostics, error_at(src, "a(", 1, "incomplete type for parameter 'x'") +
                               error_at(src, "b(", 1, "incomplete type for parameter 'x'") +
                               error_at(src, "c(", 1, "incomplete type for parameter 's'") +
                               error_at(src, "f(", 1, unnamed) + error_at(src, "g(", 1, unnamed) +
                               error_at(src, "h(", 1, unnamed) + error_at(src, "i(", 1, unnamed) +
                               error_at(src, "j(", 1, "incomplete type for parameter 's'") +
                               error_at(src, "j(", 1, "incomplete type for parameter 't'") +
                               error_at(src, "l(", 1, unnamed))
      << "one unnamed parameter is one error for the list";
}

static symbol *record_symbol(const Checked &c, const char *tag) {
  for (ast_node *n : nodes_of(c, AST_NODE_TYPE_STRUCT_DEF)) {
    if (n->struct_def.tag_name != nullptr && std::strcmp(n->struct_def.tag_name, tag) == 0)
      return n->symbol;
  }
  return nullptr;
}

struct MemberAt {
  const char *name;
  long long offset;
  int bit_offset;
  int bit_width;
};

struct RecordLayout {
  const char *source;
  long long size;
  int alignment;
  std::vector<MemberAt> members;
};

static void expect_layouts(const std::vector<RecordLayout> &cases, bool preprocessed = false) {
  for (const RecordLayout &test : cases) {
    SCOPED_TRACE(test.source);
    Checked c;
    if (preprocessed)
      check_preprocessed(c, test.source);
    else
      check(c, test.source);
    ASSERT_EQ(c.parse_errors, 0);
    ASSERT_EQ(c.errors, 0) << c.diagnostics;
    symbol *record = record_symbol(c, "R");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->size, test.size);
    EXPECT_EQ(record->alignment, test.alignment);
    for (const MemberAt &expected : test.members) {
      SCOPED_TRACE(expected.name);
      std::vector<ast_node *> member = declarations(c, expected.name);
      ASSERT_FALSE(member.empty());
      ast_node *last = member.back();
      EXPECT_EQ(last->var_decl.offset, expected.offset);
      EXPECT_EQ(last->var_decl.bit_offset, expected.bit_offset);
      EXPECT_EQ(last->var_decl.bit_width, expected.bit_width);
    }
  }
}

TEST(LayoutTest, RecordsFollowThePlatformLayout) {
  expect_layouts({
      {"struct R { char c; int i; };\n", 8, 4, {{"c", 0, 0, 0}, {"i", 4, 0, 0}}},
      {"struct R { int i; char c; };\n", 8, 4, {{"i", 0, 0, 0}, {"c", 4, 0, 0}}},
      {"struct R { char c; long double d; };\n", 32, 16, {{"c", 0, 0, 0}, {"d", 16, 0, 0}}},
      {"struct R { char c; double _Complex z; };\n", 24, 8, {{"c", 0, 0, 0}, {"z", 8, 0, 0}}},
      {"struct R { char c; int *p; };\n", 16, 8, {{"c", 0, 0, 0}, {"p", 8, 0, 0}}},
      {"enum E { X };\nstruct R { char c; enum E e; };\n", 8, 4, {{"c", 0, 0, 0}, {"e", 4, 0, 0}}},
      {"struct P { char c; double d; };\nstruct R { char c; struct P p; };\n",
       24,
       8,
       {{"c", 0, 0, 0}, {"p", 8, 0, 0}}},
      {"struct P { char c; short s; };\nstruct R { char c; struct P p[3]; char d; };\n",
       16,
       2,
       {{"c", 0, 0, 0}, {"p", 2, 0, 0}, {"d", 14, 0, 0}}},
      {"union R { char c; int i; double d; };\n",
       8,
       8,
       {{"c", 0, 0, 0}, {"i", 0, 0, 0}, {"d", 0, 0, 0}}},
      {"union U { char c[5]; int i; };\nstruct R { char c; union U u; };\n",
       12,
       4,
       {{"c", 0, 0, 0}, {"u", 4, 0, 0}}},
      {"struct R { int n; char data[]; };\n", 4, 4, {{"n", 0, 0, 0}, {"data", 4, 0, 0}}},
      {"struct R { char c; int data[]; };\n", 4, 4, {{"c", 0, 0, 0}, {"data", 4, 0, 0}}},
      {"struct R { char a; int b : 3; char c; int d : 5; };\n",
       16,
       4,
       {{"a", 0, 0, 0}, {"b", 4, 0, 3}, {"c", 8, 0, 0}, {"d", 12, 0, 5}}},
      {"struct R { int a : 3; unsigned b : 5; };\n", 4, 4, {{"a", 0, 0, 3}, {"b", 0, 3, 5}}},
      {"struct R { _Bool a : 1; int b : 3; };\n", 8, 4, {{"a", 0, 0, 1}, {"b", 4, 0, 3}}},
      {"struct R { int a : 30; int b : 3; };\n", 8, 4, {{"a", 0, 0, 30}, {"b", 4, 0, 3}}},
      {"struct R { int a : 29; int b : 3; };\n", 4, 4, {{"a", 0, 0, 29}, {"b", 0, 29, 3}}},
      {"struct R { int a : 3; char c; int b : 3; };\n",
       12,
       4,
       {{"a", 0, 0, 3}, {"c", 4, 0, 0}, {"b", 8, 0, 3}}},
      {"struct R { _Bool a : 1; int : 0; char b; };\n", 8, 4, {{"a", 0, 0, 1}, {"b", 4, 0, 0}}},
      {"struct R { _Bool a : 1; int : 0; _Bool b : 1; };\n",
       8,
       4,
       {{"a", 0, 0, 1}, {"b", 4, 0, 1}}},
      {"struct R { char a; int : 0; char b; };\n", 2, 1, {{"a", 0, 0, 0}, {"b", 1, 0, 0}}},
      {"union R { _Bool a : 1; int : 0; };\n", 1, 1, {{"a", 0, 0, 1}}},
      {"union R { char c; int a : 3; };\n", 4, 4, {{"c", 0, 0, 0}, {"a", 0, 0, 3}}},
      {"union R { int a : 3; int b : 5; };\n", 4, 4, {{"a", 0, 0, 3}, {"b", 0, 0, 5}}},
      {"struct R { char a : 3; char b : 5; char c : 1; };\n",
       2,
       1,
       {{"a", 0, 0, 3}, {"b", 0, 3, 5}, {"c", 1, 0, 1}}},
      {"struct R { long long a : 40; int b : 3; };\n", 16, 8, {{"a", 0, 0, 40}, {"b", 8, 0, 3}}},
      {"enum E { Y };\nstruct R { enum E a : 2; unsigned b : 2; };\n",
       4,
       4,
       {{"a", 0, 0, 2}, {"b", 0, 2, 2}}},
      {"struct R { char c; int a : 3; int b : 4; };\n",
       8,
       4,
       {{"c", 0, 0, 0}, {"a", 4, 0, 3}, {"b", 4, 3, 4}}},
      {"struct R { struct In { char x; } in; int i; };\n", 8, 4, {{"in", 0, 0, 0}, {"i", 4, 0, 0}}},
      {"struct R { char a, b; int c; };\n", 8, 4, {{"a", 0, 0, 0}, {"b", 1, 0, 0}, {"c", 4, 0, 0}}},
  });
}

TEST(LayoutTest, RecordsOnLinuxFollowTheSystemVLayout) {
  TargetGuard guard(TARGET_LINUX_X64);
  expect_layouts({
      {"struct R { char c; long l; };\n", 16, 8, {{"c", 0, 0, 0}, {"l", 8, 0, 0}}},
      {"struct R { char a : 4; int b : 4; };\n", 4, 4, {{"a", 0, 0, 4}, {"b", 0, 4, 4}}},
      {"struct R { char a; int b : 3; char c; int d : 5; };\n",
       4,
       4,
       {{"a", 0, 0, 0}, {"b", 0, 8, 3}, {"c", 2, 0, 0}, {"d", 0, 24, 5}}},
      {"struct R { int a : 3; unsigned b : 5; };\n", 4, 4, {{"a", 0, 0, 3}, {"b", 0, 3, 5}}},
      {"struct R { _Bool a : 1; int b : 3; };\n", 4, 4, {{"a", 0, 0, 1}, {"b", 0, 1, 3}}},
      {"struct R { int a : 30; int b : 3; };\n", 8, 4, {{"a", 0, 0, 30}, {"b", 4, 0, 3}}},
      {"struct R { int a : 29; int b : 3; };\n", 4, 4, {{"a", 0, 0, 29}, {"b", 0, 29, 3}}},
      {"struct R { int a : 3; char c; int b : 3; };\n",
       4,
       4,
       {{"a", 0, 0, 3}, {"c", 1, 0, 0}, {"b", 0, 16, 3}}},
      {"struct R { char a : 7; char b : 2; };\n", 2, 1, {{"a", 0, 0, 7}, {"b", 1, 0, 2}}},
      {"struct R { short a : 10; char b : 7; };\n", 4, 2, {{"a", 0, 0, 10}, {"b", 2, 0, 7}}},
      {"struct R { char c; long long a : 40; };\n", 8, 8, {{"c", 0, 0, 0}, {"a", 0, 8, 40}}},
      {"struct R { long a : 40; int b : 3; };\n", 8, 8, {{"a", 0, 0, 40}, {"b", 4, 8, 3}}},
      {"struct R { char c; int : 4; };\n", 2, 1, {{"c", 0, 0, 0}}},
      {"struct R { char a; int : 0; char b; };\n", 5, 1, {{"a", 0, 0, 0}, {"b", 4, 0, 0}}},
      {"struct R { _Bool a : 1; int : 0; _Bool b : 1; };\n",
       5,
       1,
       {{"a", 0, 0, 1}, {"b", 4, 0, 1}}},
      {"struct R { char a : 3; char c; };\n", 2, 1, {{"a", 0, 0, 3}, {"c", 1, 0, 0}}},
      {"struct R { char a : 3; char b : 5; char c : 1; };\n",
       2,
       1,
       {{"a", 0, 0, 3}, {"b", 0, 3, 5}, {"c", 1, 0, 1}}},
      {"struct R { char c; int a : 3; int b : 4; };\n",
       4,
       4,
       {{"c", 0, 0, 0}, {"a", 0, 8, 3}, {"b", 0, 11, 4}}},
      {"union R { char c; int a : 3; };\n", 4, 4, {{"c", 0, 0, 0}, {"a", 0, 0, 3}}},
      {"union R { int a : 3; int b : 5; };\n", 4, 4, {{"a", 0, 0, 3}, {"b", 0, 0, 5}}},
      {"union R { char c[3]; int : 20; };\n", 3, 1, {{"c", 0, 0, 0}}},
      {"union R { _Bool a : 1; int : 0; };\n", 1, 1, {{"a", 0, 0, 1}}},
      {"enum E { Y };\nstruct R { enum E a : 2; unsigned b : 2; };\n",
       4,
       4,
       {{"a", 0, 0, 2}, {"b", 0, 2, 2}}},
  });
}

TEST(LayoutTest, SizeofReadsTheLayout) {
  expect_values("struct R { char a; int b : 3; char c; int d : 5; };\n"
                "union U { char c[5]; int i; };\n",
                {{"sizeof(struct R)", 16},
                 {"sizeof(union U)", 8},
                 {"sizeof(struct R[3])", 48},
                 {"sizeof(double _Complex)", 16},
                 {"sizeof(float _Complex)", 8},
                 {"sizeof(long double _Complex)", 32}});
}

TEST(LayoutTest, MemberListsFollowTheConstraints) {
  const char *src = "struct T;\n"
                    "struct A { int a; int a; };\n"
                    "struct B { struct T t; };\n"
                    "struct C { struct C self; };\n"
                    "struct D { int f(void); };\n"
                    "struct E { char data[]; int n; };\n"
                    "struct F { char data[]; };\n"
                    "union G { int n; char data[]; };\n"
                    "struct H { int n; char data[]; };\n"
                    "struct I { struct H h; };\n"
                    "union J { struct H h; int x; };\n"
                    "struct K { union J j; };\n"
                    "union L { struct H h[2]; };\n"
                    "struct M { int : 3; };\n"
                    "struct N { void v; };\n"
                    "struct O { struct T a[2]; };\n"
                    "struct P { int n; struct T *p; char tail[]; };\n"
                    "struct Q { char a[1 / 0]; };\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 14) << c.diagnostics;
  EXPECT_EQ(
      c.diagnostics,
      error_at(src, "a; }", 1, "duplicate member 'a'") + note_at(src, "a; int a", 1, "a") +
          error_at(src, "t; }", 1, "incomplete type for member 't'") +
          error_at(src, "self", 1, "incomplete type for member 'self'") +
          error_at(src, "f(void)", 1, "function type for member 'f'") +
          error_at(src, "data[]; int", 1,
                   "flexible array member must be the last member, not 'data'") +
          error_at(src, "data[]; }", 1,
                   "flexible array member without another named member 'data'") +
          error_at(src, "data[]; }", 2, "flexible array member in a union 'data'") +
          error_at(src, "h; }", 1, "structure with a flexible array member used for member 'h'") +
          error_at(src, "j; }", 1, "structure with a flexible array member used for member 'j'") +
          error_at(src, "h[2]", 1, "an array cannot have elements with a flexible array member") +
          error_at(src, "struct M", 1, "a structure or union needs a named member") +
          error_at(src, "v; }", 1, "incomplete type for member 'v'") +
          error_at(src, "a[2]", 1, "an array needs a complete element type") +
          error_at(src, "/ 0", 1, "division by zero in a constant expression"));
  const struct {
    const char *tag;
    int has_layout;
    int has_flexible_member;
  } expected[] = {{"A", 1, 0}, {"B", 0, 0}, {"C", 0, 0}, {"E", 0, 0}, {"F", 0, 0}, {"H", 1, 1},
                  {"I", 0, 0}, {"J", 1, 1}, {"K", 0, 0}, {"M", 0, 0}, {"P", 1, 1}, {"Q", 0, 0}};
  for (const auto &test : expected) {
    SCOPED_TRACE(test.tag);
    symbol *record = record_symbol(c, test.tag);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->alignment != 0, test.has_layout != 0);
    EXPECT_EQ(record->has_flexible_member, test.has_flexible_member);
  }
}

TEST(LayoutTest, BitFieldsFollowTheConstraints) {
  const char *src = "int n;\n"
                    "typedef unsigned U;\n"
                    "enum E { X };\n"
                    "struct S {\n"
                    "  double d : 3;\n"
                    "  int *p : 2;\n"
                    "  double : 7;\n"
                    "  int w : n;\n"
                    "  int div : 1 / 0;\n"
                    "  int neg : -1;\n"
                    "  int wide : 33;\n"
                    "  _Bool b8 : 8;\n"
                    "  _Bool b9 : 9;\n"
                    "  char ch : 9;\n"
                    "  int zero : 0;\n"
                    "  const U q : 3;\n"
                    "  signed int si : 4;\n"
                    "  enum E e : 2;\n"
                    "  long long ll : 64;\n"
                    "};\n"
                    "struct Z { char z : 8; unsigned : 32; };\n";
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    bool on_linux = kind == TARGET_LINUX_X64;
    SCOPED_TRACE(on_linux ? "linux" : "windows");
    Checked c;
    check(c, src);
    ASSERT_EQ(c.parse_errors, 0);
    EXPECT_EQ(c.errors, 10) << c.diagnostics;
    EXPECT_EQ(c.diagnostics,
              error_at(src, "d : 3", 1, "invalid type for bit-field 'd'") +
                  error_at(src, "p : 2", 1, "invalid type for bit-field 'p'") +
                  error_at(src, ": 7", 1, "invalid type for an unnamed bit-field") +
                  error_at(src, "w : n", 1,
                           "width is not an integer constant expression for bit-field 'w'") +
                  error_at(src, "/ 0", 1, "division by zero in a constant expression") +
                  error_at(src, "neg", 1, "negative width for bit-field 'neg'") +
                  error_at(src, "wide", 1, "width exceeds its type for bit-field 'wide'") +
                  error_at(src, "b9", 1, "width exceeds its type for bit-field 'b9'") +
                  error_at(src, "ch :", 1, "width exceeds its type for bit-field 'ch'") +
                  error_at(src, "zero", 1, "zero width for bit-field 'zero'"))
        << "the constraints are C99's and the same on every target";
    symbol *s = record_symbol(c, "S");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->alignment, 0) << "a record with a bad bit-field has no layout";
    symbol *z = record_symbol(c, "Z");
    ASSERT_NE(z, nullptr);
    EXPECT_EQ(z->size, 8);
    EXPECT_EQ(z->alignment, on_linux ? 1 : 4) << "an unnamed bit-field aligns nothing on Linux";
  }
}

TEST(LayoutTest, AMemberTypeThatFailedToResolveAddsNoError) {
  const char *src = "typedef int V, ;\n"
                    "struct S { V a : 3; V b; int c; };\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 1);
  EXPECT_EQ(c.errors, 2) << c.diagnostics;
  EXPECT_EQ(c.diagnostics, error_at(src, "a :", 1, "unknown type name 'V'") +
                               error_at(src, "b;", 1, "unknown type name 'V'"));
  symbol *s = record_symbol(c, "S");
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->alignment, 0);
}

static std::string describe(type_info *type) {
  if (type == nullptr)
    return "<none>";
  static const char *const prims[] = {"<none>",      "void",
                                      "_Bool",       "char",
                                      "signed char", "unsigned char",
                                      "short",       "unsigned short",
                                      "int",         "unsigned int",
                                      "long",        "unsigned long",
                                      "long long",   "unsigned long long",
                                      "float",       "double",
                                      "long double"};
  std::string qualifiers = std::string(type->is_const ? "const " : "") +
                           (type->is_volatile ? "volatile " : "") +
                           (type->is_restrict ? "restrict " : "");
  switch (type->kind) {
  case TYPE_PRIMITIVE:
    return qualifiers + prims[type->prim] + (type->is_complex ? " _Complex" : "");
  case TYPE_POINTER: {
    std::string trailing = qualifiers.empty() ? "" : qualifiers.substr(0, qualifiers.size() - 1);
    return describe(type->ptr_to) + " *" + trailing;
  }
  case TYPE_ARRAY:
    return describe(type->ptr_to) + "[" +
           (type->array_size >= 0 ? std::to_string(type->array_size) : "") + "]";
  case TYPE_FUNCTION:
    return describe(type->ptr_to) + "()";
  case TYPE_STRUCT:
  case TYPE_UNION:
  case TYPE_ENUM:
  case TYPE_TYPEDEF:
    return qualifiers + (type->tag_name != nullptr ? type->tag_name : "<anonymous>");
  default:
    return "<unknown>";
  }
}

static ast_node *probe_expression(const Checked &c) {
  std::vector<ast_node *> probe = declarations(c, "probe");
  if (probe.size() != 1 || probe[0]->type != AST_NODE_TYPE_FUNCTION_DEF)
    return nullptr;
  ast_node *body = probe[0]->function_def.body;
  return body->block.count > 0 ? body->block.statements[body->block.count - 1] : nullptr;
}

struct TypeCase {
  const char *declarations;
  const char *expression;
  const char *type;
};

static void expect_types(const std::vector<TypeCase> &cases, bool preprocessed = false) {
  for (const TypeCase &test : cases) {
    std::string source =
        std::string(test.declarations) + "void probe(void) { " + test.expression + "; }\n";
    SCOPED_TRACE(source);
    Checked c;
    if (preprocessed)
      check_preprocessed(c, source.c_str());
    else
      check(c, source.c_str());
    ASSERT_EQ(c.parse_errors, 0);
    EXPECT_EQ(c.errors, 0) << c.diagnostics;
    ast_node *expression = probe_expression(c);
    ASSERT_NE(expression, nullptr);
    EXPECT_EQ(describe(expression->expr_type), test.type);
  }
}

TEST(ExpressionTypeTest, ConstantsAndNamesHaveTheirTypes) {
  expect_types({
      {"", "1", "int"},
      {"", "2147483648", "long long"},
      {"", "0xFFFFFFFF", "unsigned int"},
      {"", "1ul", "unsigned long"},
      {"", "1.0", "double"},
      {"", "1.0f", "float"},
      {"", "1.0L", "long double"},
      {"", "0x1p3", "double"},
      {"", "'a'", "int"},
      {"", "L'a'", "unsigned short"},
      {"", "\"abc\"", "char[4]"},
      {"", "L\"ab\"", "unsigned short[3]"},
      {"", "\"\"", "char[1]"},
      {"enum E { A };\n", "A", "int"},
      {"const long x;\n", "x", "const long"},
      {"int a[4];\n", "a", "int[4]"},
      {"int f(void);\n", "f", "int()"},
      {"typedef unsigned U;\nU u;\n", "u", "U"},
  });
}

TEST(ExpressionTypeTest, OperatorsGiveTheirResultTypes) {
  expect_types({
      {"char c;\n", "+c", "int"},
      {"unsigned short h;\n", "-h", "int"},
      {"unsigned u;\n", "~u", "unsigned int"},
      {"float f;\n", "-f", "float"},
      {"double d;\n", "!d", "int"},
      {"enum E { A } e;\n", "-e", "unsigned int"},
      {"char c;\n", "c + c", "int"},
      {"unsigned u; long l;\n", "u * l", "unsigned long"},
      {"int i; float f;\n", "i / f", "float"},
      {"char c; float f;\n", "c + f", "float"},
      {"float f; double d;\n", "f - d", "double"},
      {"double d; long double e;\n", "d * e", "long double"},
      {"float f; double _Complex z;\n", "f + z", "double _Complex"},
      {"int i; float _Complex z;\n", "i * z", "float _Complex"},
      {"enum E { A } e; double d;\n", "e + d", "double"},
      {"long long l; unsigned u;\n", "l % u", "long long"},
      {"char c; long long l;\n", "c << l", "int"},
      {"unsigned long u; int i;\n", "u >> i", "unsigned long"},
      {"int i; unsigned u;\n", "i & u", "unsigned int"},
      {"double d;\n", "d < 1", "int"},
      {"int *p;\n", "p == 0", "int"},
      {"int *p;\n", "p && 1.0", "int"},
      {"int *p;\n", "!p", "int"},
      {"int *p;\n", "p - 1", "int *"},
      {"typedef int A[2];\nconst A x;\n", "x + 1", "const int *"},
      {"int i;\n", "(int *)i", "int *"},
      {"const int *p;\n", "p + 1", "const int *"},
      {"int *const p;\n", "p + 1", "int *"},
      {"int *p;\n", "1 + p", "int *"},
      {"int a[4];\n", "a + 1", "int *"},
      {"int *p; const int *q;\n", "p - q", "long long"},
      {"int *p;\n", "p < p", "int"},
      {"int i;\n", "(1, i)", "int"},
      {"int a[4];\n", "(1, a)", "int *"},
      {"int f(void);\n", "(1, f)", "int() *"},
      {"const int x;\n", "(0, x)", "int"},
      {"int c;\n", "c ? 1 : 2.0", "double"},
      {"int c; struct S { int a; } s, t;\n", "c ? s : t", "S"},
      {"int c; void g(void);\n", "c ? (void)0 : (void)1", "void"},
      {"int c; int *p;\n", "c ? p : 0", "int *"},
      {"int c; int *p;\n", "c ? (void *)0 : p", "int *"},
      {"int c; int *p; const int *q;\n", "c ? p : q", "const int *"},
      {"int c; int *p; volatile int *q;\n", "c ? q : p", "volatile int *"},
      {"int c; int *p; void *v;\n", "c ? p : v", "void *"},
      {"int c; const int *p; void *v;\n", "c ? p : v", "const void *"},
      {"int c; typedef const int CI; CI *p; int *q;\n", "c ? q : p", "const int *"},
      {"int c; typedef int I; const I *p; int *q;\n", "c ? q : p", "const int *"},
      {"typedef int I;\nconst I x[2];\n", "x + 1", "const int *"},
      {"int c; const int *p; void *v;\n", "c ? v : p", "const void *"},
      {"int c; int *p; const void *v;\n", "c ? p : v", "const void *"},
      {"double d;\n", "(const int)d", "int"},
      {"int *p;\n", "(long long)p", "long long"},
      {"struct S { int a; } s;\n", "(void)s", "void"},
      {"int i;\n", "sizeof i", "unsigned long long"},
      {"", "sizeof(int)", "unsigned long long"},
  });
}

TEST(ExpressionTypeTest, ParametersHaveTheirAdjustedTypes) {
  const char *src =
      "typedef int A[3];\n"
      "typedef int I;\n"
      "int probe(int a[10], int b[const], const A c, int g(void), int n, const I d[2]) {\n"
      "  return 0;\n"
      "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  std::vector<ast_node *> probe = declarations(c, "probe");
  ASSERT_EQ(probe.size(), 1u);
  ASSERT_NE(probe[0]->function_def.param_symbols, nullptr);
  const char *const expected[] = {"int *",   "int *const", "const int *",
                                  "int() *", "int",        "const int *"};
  for (int i = 0; i < 6; i++) {
    SCOPED_TRACE(i);
    ASSERT_NE(probe[0]->function_def.param_symbols[i], nullptr);
    EXPECT_EQ(describe(probe[0]->function_def.param_symbols[i]->type), expected[i]);
  }
  EXPECT_EQ(describe(probe[0]->function_def.type->param_types[0]), "int[10]")
      << "the function type keeps the written parameter type";
  expect_values("void f(int a[10]) { enum { S = sizeof a }; }\n", {});
  Checked sized;
  check(sized, "void f(int a[], char b[sizeof a == 8 ? 1 : -1]);\n"
               "void g(int a[]) { char b[sizeof a == 8 ? 1 : -1]; }\n");
  EXPECT_EQ(sized.errors, 0) << sized.diagnostics;
}

TEST(ExpressionTypeTest, SizeofOfAnExpressionIsAConstant) {
  expect_values("int x;\nint a[4];\nshort s;\n", {{"sizeof x", 4},
                                                  {"sizeof a", 16},
                                                  {"sizeof(a + 0)", 8},
                                                  {"sizeof \"abc\"", 4},
                                                  {"sizeof L\"abc\"", 8},
                                                  {"sizeof 'a'", 4},
                                                  {"sizeof(s + s)", 4},
                                                  {"sizeof 1.0L", 16},
                                                  {"sizeof(1 ? 2 : 3.0)", 8},
                                                  {"sizeof(-s)", 4}});
}

TEST(ExpressionTypeTest, OperandsFollowTheConstraints) {
  const struct {
    const char *declarations;
    const char *expression;
    const char *needle;
    const char *message;
  } cases[] = {
      {"int *p;\n", "-p", "-p", "invalid operand to unary '-'"},
      {"int *p;\n", "+p", "+p", "invalid operand to unary '+'"},
      {"double d;\n", "~d", "~d", "invalid operand to unary '~'"},
      {"struct S { int a; } s;\n", "!s", "!s", "invalid operand to unary '!'"},
      {"struct S { int a; } s;\n", "s + 1", "+ 1", "invalid operands to binary '+'"},
      {"int *p;\n", "p + p", "+ p", "invalid operands to binary '+'"},
      {"int *p;\n", "p * 2", "* 2", "invalid operands to binary '*'"},
      {"double d; int *p;\n", "d / p", "/ p", "invalid operands to binary '/'"},
      {"double d;\n", "d % 2", "% 2", "invalid operands to binary '%'"},
      {"double d;\n", "2 & d", "& d", "invalid operands to binary '&'"},
      {"double d;\n", "d << 1", "<< 1", "invalid operands to binary '<<'"},
      {"double d;\n", "1 >> d", ">> d", "invalid operands to binary '>>'"},
      {"int *p;\n", "1 | p", "| p", "invalid operands to binary '|'"},
      {"int *p;\n", "p ^ 1", "^ 1", "invalid operands to binary '^'"},
      {"int *p; double d;\n", "p + d", "+ d", "invalid operands to binary '+'"},
      {"void *v;\n", "v + 1", "+ 1", "invalid operands to binary '+'"},
      {"void *v;\n", "1 + v", "+ v", "invalid operands to binary '+'"},
      {"int (*fp)(void);\n", "fp - 1", "- 1", "invalid operands to binary '-'"},
      {"struct T *t;\n", "t + 1", "+ 1", "invalid operands to binary '+'"},
      {"int *p; long *q;\n", "p - q", "- q", "invalid operands to binary '-'"},
      {"void *v;\n", "v - v", "- v", "invalid operands to binary '-'"},
      {"int *p;\n", "1 - p", "- p", "invalid operands to binary '-'"},
      {"int *p; double d;\n", "p < d", "< d", "invalid operands to binary '<'"},
      {"int *p; long *q;\n", "p > q", "> q", "invalid operands to binary '>'"},
      {"int (*f)(void);\n", "f <= f", "<= f", "invalid operands to binary '<='"},
      {"int (*a)[]; int (*b)[3];\n", "a >= b", ">= b", "invalid operands to binary '>='"},
      {"double _Complex z;\n", "z < 1", "< 1", "invalid operands to binary '<'"},
      {"int *p;\n", "p == 1", "== 1", "invalid operands to binary '=='"},
      {"int *p;\n", "1 != p", "!= p", "invalid operands to binary '!='"},
      {"int *p;\n", "p == 0.0", "== 0.0", "invalid operands to binary '=='"},
      {"int (*f)(void);\n", "f == (const void *)0", "==", "invalid operands to binary '=='"},
      {"int (*f)(void);\n", "f == (void *)(void *)0", "==", "invalid operands to binary '=='"},
      {"int *p; long *q;\n", "p == q", "== q", "invalid operands to binary '=='"},
      {"typedef int A[3]; const A *x; int (*y)[3];\n", "x == y", "== y",
       "invalid operands to binary '=='"},
      {"int (*f)(void); void *v;\n", "f == v", "== v", "invalid operands to binary '=='"},
      {"int (*f)(void); void *v;\n", "v != f", "!= f", "invalid operands to binary '!='"},
      {"struct S { int a; } s;\n", "s == s", "== s", "invalid operands to binary '=='"},
      {"struct S { int a; } s;\n", "s && 1", "&& 1", "invalid operands to binary '&&'"},
      {"struct S { int a; } s;\n", "1 || s", "|| s", "invalid operands to binary '||'"},
      {"struct S { int a; } s;\n", "s ? 1 : 2", "? 1", "the condition of '?:' needs a scalar type"},
      {"int c; int *p; long *q;\n", "c ? p : q", "? p", "type mismatch in conditional expression"},
      {"int c; int *p;\n", "c ? p : 1", "? p", "type mismatch in conditional expression"},
      {"int c; int *p;\n", "c ? 1.0 : p", "? 1.0", "type mismatch in conditional expression"},
      {"int c; struct S { int a; } s; struct T { int a; } t;\n", "c ? s : t", "? s",
       "type mismatch in conditional expression"},
      {"int c; struct S { int a; } s;\n", "c ? s : 1", "? s",
       "type mismatch in conditional expression"},
      {"int c; int (*f)(void); void *v;\n", "c ? f : v", "? f",
       "type mismatch in conditional expression"},
      {"int c; int (*f)(void); void *v;\n", "c ? v : f", "? v",
       "type mismatch in conditional expression"},
      {"struct S { int a; } s;\n", "(int)s", "(int)", "a cast needs a scalar operand"},
      {"int i;\n", "(struct S { int a; })i", "(struct", "a cast needs a scalar or void type"},
      {"int i;\n", "(int[2])i", "(int[2])", "a cast needs a scalar or void type"},
      {"double d;\n", "(int *)d", "(int *)", "cannot cast between a pointer and a floating type"},
      {"int *p;\n", "(float)p", "(float)", "cannot cast between a pointer and a floating type"},
      {"int f(void);\n", "sizeof f", "f;", "invalid application of sizeof to a function type"},
      {"struct T;\n", "sizeof(struct T)", "(struct",
       "invalid application of sizeof to an incomplete type"},
      {"extern int a[];\n", "sizeof a", "a;",
       "invalid application of sizeof to an incomplete type"},
      {"", "sizeof(void)", "(void);", "invalid application of sizeof to an incomplete type"},
  };
  for (const auto &test : cases) {
    std::string source =
        std::string(test.declarations) + "void probe(void) { " + test.expression + "; }\n";
    SCOPED_TRACE(source);
    Checked c;
    check(c, source.c_str());
    ASSERT_EQ(c.parse_errors, 0);
    std::string expected = error_at(source.c_str(), test.needle, 1, test.message);
    EXPECT_EQ(c.diagnostics, expected);
    ast_node *expression = probe_expression(c);
    ASSERT_NE(expression, nullptr);
    if (std::strncmp(test.expression, "sizeof", 6) != 0)
      EXPECT_EQ(expression->expr_type, nullptr) << "an invalid expression has no type";
  }
}

TEST(ExpressionTypeTest, ValidPointerOperationsAreClean) {
  expect_types({
      {"int *p;\n", "p == (void *)0", "int"},
      {"int *p;\n", "0 == p", "int"},
      {"int *p;\n", "p != '\\0'", "int"},
      {"int *p;\n", "p == 1 - 1", "int"},
      {"int *p; const int *q;\n", "p == q", "int"},
      {"int *p; const void *v;\n", "p != v", "int"},
      {"const int *q; int *p;\n", "q == p", "int"},
      {"int *p;\n", "p == (const void *)0", "int"},
      {"int *p;\n", "p == (void *)(void *)0", "int"},
      {"int (*f)(void);\n", "f == (void *)0", "int"},
      {"int (*f)(void);\n", "f == f", "int"},
      {"int (*f)(void);\n", "f == 0", "int"},
      {"int *p; const int *q;\n", "p < q", "int"},
      {"struct T; struct T *a, *b;\n", "a < b", "int"},
      {"int (*a)[3]; int (*b)[3];\n", "a - b", "long long"},
  });
}

TEST(ExpressionTypeTest, AnOperandWithoutATypeAddsNoError) {
  const char *src = "int *p;\n"
                    "struct S { int a; } s;\n"
                    "int f(void);\n"
                    "void probe(void) {\n"
                    "  undeclared + 1 * s;\n"
                    "  (int)undeclared + 1;\n"
                    "  -(*undeclared + s);\n"
                    "  undeclared() ? s : 1;\n"
                    "  undeclared.a + s;\n"
                    "  !(undeclared[0] = s);\n"
                    "  &undeclared->b + s;\n"
                    "  undeclared++ + s;\n"
                    "  (p = undeclared) + s;\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string undeclared = "use of undeclared identifier 'undeclared'";
  std::string expected = error_at(src, "undeclared", 1, undeclared) +
                         error_at(src, "* s", 1, "invalid operands to binary '*'");
  for (int i = 2; i <= 9; i++)
    expected += error_at(src, "undeclared", i, undeclared);
  EXPECT_EQ(c.diagnostics, expected) << "one bad or untyped operand stops at its own report";
}

TEST(ExpressionTypeTest, SizeofMisuseIsReportedOnceWherever) {
  const char *src = "struct T;\n"
                    "int n = sizeof(struct T);\n"
                    "char b[sizeof(struct T)];\n"
                    "enum { E = sizeof(void) };\n"
                    "char c[sizeof(void)];\n"
                    "void f(void) { sizeof(struct T); }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string message = "invalid application of sizeof to an incomplete type";
  EXPECT_EQ(c.diagnostics,
            error_at(src, "(struct T)", 1, message) + error_at(src, "(struct T)", 2, message) +
                error_at(src, "(void)", 1, message) + error_at(src, "(void)", 2, message) +
                error_at(src, "(struct T)", 3, message));
}

TEST(LvalueTest, LvaluesAreMarked) {
  const struct {
    const char *declarations;
    const char *expression;
    int is_lvalue;
  } cases[] = {
      {"int x;\n", "x", 1},
      {"int *p;\n", "*p", 1},
      {"struct T *t;\n", "*t", 1},
      {"int a[3];\n", "a[1]", 1},
      {"struct S { int m; } s;\n", "s.m", 1},
      {"struct S { int m; } *p;\n", "p->m", 1},
      {"struct S { int m; } *p;\n", "(p + 0)->m", 1},
      {"int c; struct S { int m; } s, t;\n", "(c ? s : t).m", 0},
      {"struct S { int m; };\n", "(struct S){0}.m", 1},
      {"", "\"abc\"", 1},
      {"", "(int){1}", 1},
      {"int f(void);\n", "f", 0},
      {"enum E { A };\n", "A", 0},
      {"int x;\n", "x + 1", 0},
      {"int x;\n", "(int)x", 0},
      {"int c, x;\n", "c ? x : x", 0},
      {"int x;\n", "(0, x)", 0},
      {"int (*fp)(void);\n", "*fp", 0},
      {"void *v;\n", "*v", 0},
      {"int x;\n", "x = 1", 0},
      {"int x;\n", "x++", 0},
      {"int x;\n", "--x", 0},
      {"int x;\n", "&x", 0},
  };
  for (const auto &test : cases) {
    std::string source =
        std::string(test.declarations) + "void probe(void) { " + test.expression + "; }\n";
    SCOPED_TRACE(source);
    Checked c;
    check(c, source.c_str());
    ASSERT_EQ(c.parse_errors, 0);
    ASSERT_EQ(c.errors, 0) << c.diagnostics;
    ast_node *expression = probe_expression(c);
    ASSERT_NE(expression, nullptr);
    ASSERT_NE(expression->expr_type, nullptr);
    EXPECT_EQ(expression->is_lvalue, test.is_lvalue);
  }
}

TEST(LvalueTest, OperatorsGiveTheirTypes) {
  expect_types({
      {"int x;\n", "&x", "int *"},
      {"struct S { int m; } s;\n", "&s.m", "int *"},
      {"", "(int[]){1, 2}", "int[2]"},
      {"int a[3];\n", "&a", "int[3] *"},
      {"const int c;\n", "&c", "const int *"},
      {"int f(void);\n", "&f", "int() *"},
      {"int (*fp)(void);\n", "&*fp", "int() *"},
      {"void *v;\n", "&*v", "void *"},
      {"", "&(int){1}", "int *"},
      {"", "&\"ab\"", "char[3] *"},
      {"int *p;\n", "*p", "int"},
      {"int a[3];\n", "a[1]", "int"},
      {"int a[3];\n", "1[a]", "int"},
      {"const int *p;\n", "p[0]", "const int"},
      {"int a[2][3];\n", "a[1][2]", "int"},
      {"typedef int A[2][3];\nconst A x;\n", "x[0]", "const int[3]"},
      {"struct S { int m; } *p;\n", "p->m", "int"},
      {"const struct S { int m; } cs;\n", "cs.m", "const int"},
      {"struct T { int a[2]; };\nconst struct T ct;\n", "ct.a", "const int[2]"},
      {"typedef int A[2];\nstruct T { const A a; } t;\n", "t.a", "const int[2]"},
      {"typedef struct S { int m; } R;\nconst R r;\n", "r.m", "const int"},
      {"struct S { volatile int m; } *const p;\n", "p->m", "volatile int"},
      {"struct S { int m; } const *p;\n", "p->m", "const int"},
      {"union U { char c; double d; } u;\n", "u.d", "double"},
      {"double d;\n", "d++", "double"},
      {"const int *p;\n", "p++", "const int *"},
      {"int x;\n", "--x", "int"},
      {"long x;\n", "x = 1", "long"},
      {"int *p;\n", "p += 2", "int *"},
      {"char c;\n", "c *= 2.5", "char"},
      {"typedef int A[2];\nconst A x;\n", "&x[0]", "const int *"},
  });
}

TEST(LvalueTest, OperandsFollowTheConstraints) {
  const std::string lvalue = "not a modifiable lvalue on the left of '='";
  const std::string incompatible = "incompatible types in assignment";
  const std::string discarded = "qualifiers discarded in assignment";
  const struct {
    const char *declarations;
    const char *expression;
    const char *needle;
    std::string message;
  } cases[] = {
      {"const int c;\n", "c = 1", "= 1", lvalue},
      {"int *const p;\n", "p = 0", "= 0", lvalue},
      {"const int *p;\n", "*p = 1", "= 1", lvalue},
      {"int a[2];\n", "a = 0", "= 0", lvalue},
      {"", "1 = 2", "= 2", lvalue},
      {"int x;\n", "(int)x = 1", "= 1", lvalue},
      {"int x;\n", "(0, x) = 1", "= 1", lvalue},
      {"int c, x, y;\n", "(c ? x : y) = 1", "= 1", lvalue},
      {"struct S { const int m; } s, t;\n", "s = t", "= t", lvalue},
      {"struct S { struct { const int c; } in; } s, t;\n", "s = t", "= t", lvalue},
      {"struct S { const int a[2]; } s, t;\n", "s = t", "= t", lvalue},
      {"typedef int I;\nunion U { const I a[2]; } u, v;\n", "u = v", "= v", lvalue},
      {"typedef int I;\nconst I c;\n", "c = 1", "= 1", lvalue},
      {"const struct S { int m; } s;\n", "s.m = 1", "= 1", lvalue},
      {"struct T { int a[2]; };\nconst struct T t;\n", "t.a[0] = 1", "= 1", lvalue},
      {"typedef int A[2];\nconst A x;\n", "x[0] = 1", "= 1", lvalue},
      {"struct S { int m; } const *p;\n", "p->m = 1", "= 1", lvalue},
      {"struct S;\nextern struct S s, t;\n", "s = t", "= t", lvalue},
      {"int *p;\n", "p = 1", "= 1", incompatible},
      {"int i; int *p;\n", "i = p", "= p", incompatible},
      {"int *p; long *q;\n", "p = q", "= q", incompatible},
      {"int **pp; const int **cpp;\n", "cpp = pp", "= pp", incompatible},
      {"int a[3]; int *p;\n", "p = &a", "= &a", incompatible},
      {"void *v; int (*f)(void);\n", "f = v", "= v", incompatible},
      {"void *v; int (*f)(void);\n", "v = f", "= f", incompatible},
      {"struct S { int a; } s; struct T { int a; } t;\n", "s = t", "= t", incompatible},
      {"struct S { int a; } s;\n", "s = 1", "= 1", incompatible},
      {"double _Complex z; int *p;\n", "z = p", "= p", incompatible},
      {"int *p;\n", "p = 0.0", "= 0.0", incompatible},
      {"int *p; const int *q;\n", "p = q", "= q", discarded},
      {"int *p; volatile int *q;\n", "p = q", "= q", discarded},
      {"int **p; int *restrict *q;\n", "p = q", "= q", discarded},
      {"void *v; const int *q;\n", "v = q", "= q", discarded},
      {"int *p; const void *v;\n", "p = v", "= v", discarded},
      {"int *p;\n", "p += p", "+= p", "invalid operands to binary '+='"},
      {"int i; int *p;\n", "i += p", "+= p", "invalid operands to binary '+='"},
      {"void *v;\n", "v += 1", "+= 1", "invalid operands to binary '+='"},
      {"int *p;\n", "p -= 1.0", "-= 1.0", "invalid operands to binary '-='"},
      {"int *p;\n", "p *= 2", "*= 2", "invalid operands to binary '*='"},
      {"int *p;\n", "p /= 2", "/= 2", "invalid operands to binary '/='"},
      {"double d;\n", "d %= 2", "%= 2", "invalid operands to binary '%='"},
      {"double d;\n", "d <<= 1", "<<= 1", "invalid operands to binary '<<='"},
      {"double d;\n", "d >>= 1", ">>= 1", "invalid operands to binary '>>='"},
      {"double d;\n", "d &= 1", "&= 1", "invalid operands to binary '&='"},
      {"double d;\n", "d ^= 1", "^= 1", "invalid operands to binary '^='"},
      {"double d;\n", "d |= 1", "|= 1", "invalid operands to binary '|='"},
      {"const int i;\n", "i += 1", "+= 1", "not a modifiable lvalue on the left of '+='"},
      {"const int x;\n", "x++", "++", "not a modifiable lvalue as the operand of '++'"},
      {"", "3++", "++", "not a modifiable lvalue as the operand of '++'"},
      {"int a[2];\n", "--a", "--", "not a modifiable lvalue as the operand of '--'"},
      {"struct S { int a; } s;\n", "s++", "++", "invalid operand to unary '++'"},
      {"void *v;\n", "--v", "--", "invalid operand to unary '--'"},
      {"double _Complex z;\n", "z--", "--", "invalid operand to unary '--'"},
      {"int x;\n", "&(x + 1)", "&(",
       "the operand of '&' must be an lvalue or a function designator"},
      {"int x;\n", "&(int)x", "&(",
       "the operand of '&' must be an lvalue or a function designator"},
      {"struct S { int a : 3; } s;\n", "&s.a", "&s", "cannot take the address of a bit-field"},
      {"int x;\n", "*x", "*x", "invalid operand to unary '*'"},
      {"struct S { int a; } s;\n", "*s", "*s", "invalid operand to unary '*'"},
      {"int a[3];\n", "a[1.0]", "[1.0]", "a subscript needs a pointer to an object and an integer"},
      {"int x;\n", "x[1]", "[1]", "a subscript needs a pointer to an object and an integer"},
      {"void *v;\n", "v[0]", "[0]", "a subscript needs a pointer to an object and an integer"},
      {"int *p, *q;\n", "p[q]", "[q]", "a subscript needs a pointer to an object and an integer"},
      {"struct T *t;\n", "t[0]", "[0]", "a subscript needs a pointer to an object and an integer"},
      {"int x;\n", "x.a", ".a", "the left operand of '.' must be a structure or union"},
      {"struct S { int a; } *p;\n", "p.a", ".a",
       "the left operand of '.' must be a structure or union"},
      {"struct S { int a; } s;\n", "s->a", "->a",
       "the left operand of '->' must be a pointer to a structure or union"},
      {"int *p;\n", "p->a", "->a",
       "the left operand of '->' must be a pointer to a structure or union"},
      {"struct S *p;\n", "p->a", "->a", "member access into an incomplete structure or union"},
      {"struct S { int a; } s;\n", "s.b", ".b", "no member named 'b'"},
      {"struct S { int a : 3; } s;\n", "sizeof s.a", ".a;",
       "invalid application of sizeof to a bit-field"},
      {"", "(void){0}", "(void){", "a compound literal needs an object type"},
      {"struct T;\n", "(struct T){0}", "(struct T){", "a compound literal needs an object type"},
      {"extern int n;\n", "(int[n]){0}", "(int[n])",
       "a compound literal cannot have a variable length array type"},
  };
  for (const auto &test : cases) {
    std::string source =
        std::string(test.declarations) + "void probe(void) { " + test.expression + "; }\n";
    SCOPED_TRACE(source);
    Checked c;
    check(c, source.c_str());
    ASSERT_EQ(c.parse_errors, 0);
    EXPECT_EQ(c.diagnostics, error_at(source.c_str(), test.needle, 1, test.message));
    ast_node *expression = probe_expression(c);
    ASSERT_NE(expression, nullptr);
    if (std::strncmp(test.expression, "sizeof", 6) != 0)
      EXPECT_EQ(expression->expr_type, nullptr) << "an invalid expression has no type";
  }
}

TEST(LvalueTest, ValidAssignmentsAreClean) {
  expect_types({
      {"int i; double d;\n", "i = d", "int"},
      {"_Bool b; int *p;\n", "b = p", "_Bool"},
      {"int *p;\n", "p = 0", "int *"},
      {"int *p;\n", "p = (void *)0", "int *"},
      {"int *p; const int *q;\n", "q = p", "const int *"},
      {"int *p; void *v;\n", "p = v", "int *"},
      {"int *p; void *v;\n", "v = p", "void *"},
      {"const int *q; const void *v;\n", "v = q", "const void *"},
      {"struct S { int a; } s, t;\n", "s = t", "S"},
      {"int (*f)(void); int g(void);\n", "f = g", "int() *"},
      {"int a[3]; int *p;\n", "p = a", "int *"},
      {"char *s;\n", "s = \"abc\"", "char *"},
      {"enum E { A } e;\n", "e = 5", "E"},
      {"int *p;\n", "p -= 3", "int *"},
      {"int i;\n", "i <<= 2", "int"},
      {"double d;\n", "d += 1", "double"},
      {"unsigned u;\n", "u %= 3", "unsigned int"},
      {"int (*a)[3]; int b[3];\n", "a = &b", "int[3] *"},
      {"int x;\n", "(x) = 1", "int"},
      {"int x, y;\n", "x = y = 2", "int"},
      {"struct S { volatile int m; } s, t;\n", "s = t", "S"},
      {"struct S { int m; } s;\n", "s.m = 1", "int"},
      {"struct S { const int *p; } s; int x;\n", "s.p = &x", "const int *"},
  });
}

TEST(LvalueTest, RegisterObjectsHaveNoAddress) {
  const char *src = "void f(void) { register int r; int x; &r; &x; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "&r", 1, "cannot take the address of register object 'r'"));
}

TEST(LvalueTest, MemberAccessRecordsTheMember) {
  const char *src = "struct S { int a, b; } s, *p;\n"
                    "void probe(void) { s.b; p->a; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  std::vector<ast_node *> access = nodes_of(c, AST_NODE_TYPE_MEMBER_ACCESS);
  ASSERT_EQ(access.size(), 2u);
  EXPECT_EQ(access[0]->member_access.member, declarations(c, "b")[0]);
  EXPECT_EQ(access[1]->member_access.member, declarations(c, "a")[0]);
}

TEST(CallTest, CallsHaveTheirFunctionsReturnType) {
  expect_types({
      {"int f(void);\n", "f()", "int"},
      {"const int h(void);\n", "h()", "int"},
      {"void v(void);\n", "v()", "void"},
      {"char *f(void);\n", "f()", "char *"},
      {"double (*fp)(int);\n", "fp(1)", "double"},
      {"double (*fp)(int);\n", "(*fp)(1)", "double"},
      {"long f(void);\n", "(&f)()", "long"},
      {"int (*fp)(void);\n", "(&*fp)()", "int"},
      {"struct S { int a; } make(void);\n", "make().a", "int"},
      {"int f();\n", "f(1, 2.0, \"x\")", "int"},
  });
  Checked c;
  check(c, "struct S { int a; } make(void);\nvoid probe(void) { make().a; }\n");
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  ast_node *expression = probe_expression(c);
  ASSERT_NE(expression, nullptr);
  EXPECT_EQ(expression->is_lvalue, 0) << "a member of a call's value is not an lvalue";
  EXPECT_EQ(expression->member_access.left->is_lvalue, 0);
}

TEST(CallTest, ValidCallsAreClean) {
  expect_types({
      {"int f(int, ...);\n", "f(1, 2.0, \"x\")", "int"},
      {"int f(const char *, ...);\n", "f(\"%d\", 1)", "int"},
      {"int f(int *);\n", "f(0)", "int"},
      {"int f(int *);\n", "f((void *)0)", "int"},
      {"int f(int a[]);\nint a[3];\n", "f(a)", "int"},
      {"int f(int (*)(void));\nint h(void);\n", "f(h)", "int"},
      {"int f(const int);\nint x;\n", "f(x)", "int"},
      {"int f(int *const p);\nint *q;\n", "f(q)", "int"},
      {"int f(const char *);\nchar *s;\n", "f(s)", "int"},
      {"int f(void *);\nint *p;\n", "f(p)", "int"},
      {"int f(int *);\nvoid *v;\n", "f(v)", "int"},
      {"struct S { int a; } s;\nint f(struct S);\n", "f(s)", "int"},
      {"int f(a) int a; { return a; }\n", "f(1, 2)", "int"},
      {"int f(a) int a; { return a; }\n", "f()", "int"},
      {"struct S { int a; } s;\nint f(const struct S);\n", "f(s)", "int"},
      {"int f(float);\n", "f(1)", "int"},
      {"typedef int T;\nint f(T);\n", "f(2)", "int"},
      {"int f(int n, int a[n]);\nint b[3];\n", "f(3, b)", "int"},
  });
}

TEST(CallTest, CallsFollowTheConstraints) {
  const struct {
    const char *declarations;
    const char *expression;
    const char *needle;
    const char *message;
  } cases[] = {
      {"int x;\n", "x()", "();", "the called expression is not a function"},
      {"int *p;\n", "p()", "();", "the called expression is not a function"},
      {"struct S { int a; } s;\n", "s()", "();", "the called expression is not a function"},
      {"struct T;\nstruct T make(void);\n", "make()", "();",
       "a called function must return void or a complete object type"},
      {"int f(int);\n", "f()", "();", "too few arguments in call"},
      {"int f(int);\n", "f(1, 2)", "(1, 2)", "too many arguments in call"},
      {"int f(void);\n", "f(1)", "(1)", "too many arguments in call"},
      {"int f(int, ...);\n", "f()", "();", "too few arguments in call"},
      {"int f(int *);\n", "f(1)", "1)", "incompatible types in argument"},
      {"int f(int, int *, int);\n", "f(1, 2, 3)", "2,", "incompatible types in argument"},
      {"int f(int, int, int *);\n", "f(1, 2, 3)", "3)", "incompatible types in argument"},
      {"int f(int);\nstruct S { int a; } s;\n", "f(s)", "s)", "incompatible types in argument"},
      {"int f(int *);\nconst int k;\n", "f(&k)", "&k", "qualifiers discarded in argument"},
      {"int f(int a[]);\nconst int k[2];\n", "f(k)", "k)", "qualifiers discarded in argument"},
      {"void v(void);\nint old();\n", "old(v())", "())",
       "an argument needs a complete object type"},
      {"struct T;\nextern struct T t;\nint old();\n", "old(t)", "t)",
       "an argument needs a complete object type"},
      {"void v(void);\nint print(const char *, ...);\n", "print(\"x\", v())", "())",
       "an argument needs a complete object type"},
      {"struct S { int a; } make(void);\n", "make().a = 1", "= 1",
       "not a modifiable lvalue on the left of '='"},
      {"void v(void);\n", "v() + 1", "+ 1", "invalid operands to binary '+'"},
      {"void v(void);\nint x;\n", "x = v()", "= v", "incompatible types in assignment"},
      {"double h(void);\n", "h() % 2", "% 2", "invalid operands to binary '%'"},
  };
  for (const auto &test : cases) {
    std::string source =
        std::string(test.declarations) + "void probe(void) { " + test.expression + "; }\n";
    SCOPED_TRACE(source);
    Checked c;
    check(c, source.c_str());
    ASSERT_EQ(c.parse_errors, 0);
    EXPECT_EQ(c.diagnostics, error_at(source.c_str(), test.needle, 1, test.message));
    ast_node *expression = probe_expression(c);
    ASSERT_NE(expression, nullptr);
    EXPECT_EQ(expression->expr_type, nullptr) << "an invalid expression has no type";
  }
}

TEST(CallTest, EveryProblemInACallIsReported) {
  const char *src = "int f(int, int *);\n"
                    "void probe(void) { f(\"x\"); }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics, error_at(src, "(\"x\")", 1, "too few arguments in call") +
                               error_at(src, "\"x\")", 1, "incompatible types in argument"));
}

TEST(CallTest, AnUntypedCalleeOrArgumentAddsNoError) {
  const char *src = "int f(int);\n"
                    "void probe(void) { f(undeclared); undeclared(1); }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string message = "use of undeclared identifier 'undeclared'";
  EXPECT_EQ(c.diagnostics,
            error_at(src, "undeclared", 1, message) + error_at(src, "undeclared", 2, message));

  const char *broken = "typedef int U, ;\n"
                       "int g(U);\n"
                       "void probe(void) { g(1); }\n";
  Checked d;
  check(d, broken);
  ASSERT_EQ(d.parse_errors, 1);
  EXPECT_EQ(d.diagnostics, error_at(broken, "g(U)", 1, "unknown type name 'U'"))
      << "a parameter whose typedef failed checks nothing";
}

static std::string note_text(const char *src, const char *needle, int nth,
                             const std::string &text) {
  return where(src, needle, nth) + ": note: " + text + "\n";
}

TEST(StatementTest, ConditionsMustBeScalar) {
  const char *src = "struct S { int a; } s;\n"
                    "int *p;\n"
                    "void f(void) {\n"
                    "  if (s) ;\n"
                    "  while (s) ;\n"
                    "  do ; while (s);\n"
                    "  for (; s; ) ;\n"
                    "  if (p) ; while (p) ; do ; while (p); for (;;) break; for (; 1.5; ) ;\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "if (s)", 1, "the condition of 'if' needs a scalar type") +
                error_at(src, "while (s)", 1, "the condition of 'while' needs a scalar type") +
                error_at(src, "do ;", 1, "the condition of 'do' needs a scalar type") +
                error_at(src, "for (; s", 1, "the condition of 'for' needs a scalar type"));
}

TEST(StatementTest, SwitchNeedsAnIntegerAndDistinctLabels) {
  const char *src = "double d;\n"
                    "int *p;\n"
                    "enum E { A, B } e;\n"
                    "void f(int x, int y, unsigned u, char c, long long l) {\n"
                    "  switch (d) { case 1: ; }\n"
                    "  switch (p) { default: ; }\n"
                    "  switch (x) { case 1: ; case 2 - 1: ; case y: ; case 1.5: ; case 1 / 0: ;\n"
                    "    case (int)3.5: ; default: ; default: break; }\n"
                    "  switch (u) { case -1: ; case 0xFFFFFFFF: ; }\n"
                    "  switch (c) { case 1: ; case 257: ; }\n"
                    "  switch (l) { case -1: ; case 0xFFFFFFFF: ; }\n"
                    "  switch (e) { case A: ; case B: ; }\n"
                    "  switch (x) { case 5: switch (x) { case 5: ; case 5: ; } }\n"
                    "  switch (x) { case 7: while (x) { case 8: ; case 7: ; } }\n"
                    "  switch (x) { case 9: switch (x) { case 10: ; } case 9: ; }\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string previous = "previous case is here";
  EXPECT_EQ(c.diagnostics,
            error_at(src, "switch (d)", 1,
                     "the controlling expression of 'switch' needs an integer type") +
                error_at(src, "switch (p)", 1,
                         "the controlling expression of 'switch' needs an integer type") +
                error_at(src, "case 2 - 1", 1, "duplicate case value") +
                note_text(src, "case 1: ; case 2", 1, previous) +
                error_at(src, "case y", 1, "a case label needs an integer constant expression") +
                error_at(src, "case 1.5", 1, "a case label needs an integer constant expression") +
                error_at(src, "/ 0", 1, "division by zero in a constant expression") +
                error_at(src, "default: break", 1, "duplicate default label") +
                note_text(src, "default: ;", 2, "previous default label is here") +
                error_at(src, "case 0xFFFFFFFF", 1, "duplicate case value") +
                note_text(src, "case -1", 1, previous) +
                error_at(src, "case 5", 3, "duplicate case value") +
                note_text(src, "case 5", 2, previous) +
                error_at(src, "case 7", 2, "duplicate case value") +
                note_text(src, "case 7", 1, previous) +
                error_at(src, "case 9", 2, "duplicate case value") +
                note_text(src, "case 9", 1, previous))
      << "labels belong to the innermost switch, and values compare after promotion";
}

TEST(StatementTest, LabelsAndJumpsNeedTheirStatement) {
  const char *src = "void f(int x) {\n"
                    "  case 1: ;\n"
                    "  default: ;\n"
                    "  break;\n"
                    "  if (x) continue;\n"
                    "  switch (x) { case 2: continue; default: break; }\n"
                    "  while (x) { switch (x) { case 3: continue; } break; }\n"
                    "  for (;;) { do { continue; } while (x); break; }\n"
                    "}\n"
                    "void g(int x) {\n"
                    "  do continue; while (x);\n"
                    "  while (x) ; for (;;) ; switch (x) { default: ; }\n"
                    "  continue;\n"
                    "  break;\n"
                    "  default: ;\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(
      c.diagnostics,
      error_at(src, "case 1", 1, "a case label must be inside a switch") +
          error_at(src, "default: ;", 1, "a default label must be inside a switch") +
          error_at(src, "break;", 1, "a break statement must be inside a loop or switch") +
          error_at(src, "continue;", 1, "a continue statement must be inside a loop") +
          error_at(src, "continue;", 2, "a continue statement must be inside a loop") +
          error_at(src, "continue;\n  break", 1, "a continue statement must be inside a loop") +
          error_at(src, "break;\n  default", 1,
                   "a break statement must be inside a loop or switch") +
          error_at(src, "default: ;\n}", 1, "a default label must be inside a switch"));
}

TEST(StatementTest, ReturnMatchesTheFunction) {
  const char *src = "void v(void);\n"
                    "struct S { int a; };\n"
                    "typedef long L;\n"
                    "void a(void) { return 1; }\n"
                    "void b(void) { return v(); }\n"
                    "void c(void) { return; }\n"
                    "int d(void) { return; }\n"
                    "int *e(void) { return 7; }\n"
                    "char *g(const char *s) { return s; }\n"
                    "struct S h(int x) { return x; }\n"
                    "int *i(void) { return 0; }\n"
                    "_Bool j(int *p) { return p; }\n"
                    "struct S k(struct S t) { return t; }\n"
                    "L m(void) { return 1; }\n"
                    "const int n(void) { return 1; }\n"
                    "double o(void) { return 'a'; }\n"
                    "const struct S q(struct S t) { return t; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "return 1", 1, "a function returning void cannot return a value") +
                error_at(src, "return v", 1, "a function returning void cannot return a value") +
                error_at(src, "return; }", 2, "a function returning a value needs a return value") +
                error_at(src, "7;", 1, "incompatible types in return") +
                error_at(src, "s; }", 1, "qualifiers discarded in return") +
                error_at(src, "x; }", 1, "incompatible types in return"));
}

TEST(StatementTest, UntypedPartsAddNoError) {
  const char *src = "struct S { int a; } s;\n"
                    "void f(void) {\n"
                    "  if (u1) ;\n"
                    "  switch (u2) { case 1: ; case 1: ; }\n"
                    "  switch (s) { case 2: ; case 2: ; }\n"
                    "  switch (1) { case u3: ; }\n"
                    "}\n"
                    "int g(void) { return u4; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "u1", 1, "use of undeclared identifier 'u1'") +
                error_at(src, "u2", 1, "use of undeclared identifier 'u2'") +
                error_at(src, "switch (s)", 1,
                         "the controlling expression of 'switch' needs an integer type") +
                error_at(src, "u3", 1, "use of undeclared identifier 'u3'") +
                error_at(src, "u4", 1, "use of undeclared identifier 'u4'"))
      << "no duplicate check without a controlling type, and no constant error for an unknown name";

  const char *broken = "typedef int U, ;\n"
                       "U h(void) { return; }\n";
  Checked d;
  check(d, broken);
  ASSERT_EQ(d.parse_errors, 1);
  EXPECT_EQ(d.diagnostics, error_at(broken, "h(", 1, "unknown type name 'U'"))
      << "a return type that failed to resolve checks nothing";
}

static std::string symbol_type(const Checked &c, const char *name) {
  symbol *sym = find_symbol(c, name, SYMBOL_VAR);
  return sym != nullptr ? describe(sym->type) : "<no symbol>";
}

TEST(InitializerTest, ArraysOfUnknownSizeTakeTheirSize) {
  const char *src = "int a[] = {1, 2, 3};\n"
                    "int b[] = {[5] = 1};\n"
                    "int c[] = {1, [4] = 2, 3};\n"
                    "int d[] = {[1] = 1, [0] = 2};\n"
                    "int e[] = {1, [0] = 2, 3, 4};\n"
                    "char s[] = \"abc\";\n"
                    "char t[] = {\"abc\"};\n"
                    "unsigned short w[] = L\"ab\";\n"
                    "int m[][2] = {1, 2, 3};\n"
                    "int n[][2] = {{1}, 2, 3, 4};\n"
                    "struct P { int x, y; } ps[] = {1, 2, 3, 4, 5};\n"
                    "struct N { struct { int x, y; } in; int z; };\n"
                    "struct N n1[] = {[1].in.y = 1, 2};\n"
                    "struct N n2[] = {[1].z = 1, 2};\n"
                    "struct N n4[] = {[0] = {.in.x = 1}, 2};\n"
                    "signed char sc[] = \"ab\";\n"
                    "unsigned char uc[] = \"ab\";\n"
                    "int known[4] = {1};\n"
                    "int dz[1 / 0] = {1, 2};\n"
                    "typedef int T[];\n"
                    "T tt = {1, 2};\n"
                    "T tu = {1};\n"
                    "extern int x[];\n"
                    "int x[] = {1, 2};\n"
                    "int bad[] = {missing};\n"
                    "void f(void) {\n"
                    "  int local[] = {1, 2, 3, 4};\n"
                    "  char fits[sizeof local == 16 && sizeof (int[]){1, 2} == 8 ? 1 : -1];\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "/ 0", 1, "division by zero in a constant expression") +
                error_at(src, "missing", 1, "use of undeclared identifier 'missing'"));
  const struct {
    const char *name;
    const char *type;
  } cases[] = {
      {"a", "int[3]"},     {"b", "int[6]"},    {"c", "int[6]"},          {"d", "int[2]"},
      {"e", "int[3]"},     {"s", "char[4]"},   {"t", "char[4]"},         {"w", "unsigned short[3]"},
      {"m", "int[2][2]"},  {"n", "int[2][3]"}, {"ps", "P[3]"},           {"n1", "N[2]"},
      {"n2", "N[3]"},      {"n4", "N[2]"},     {"sc", "signed char[3]"}, {"uc", "unsigned char[3]"},
      {"known", "int[4]"}, {"dz", "int[]"},    {"tt", "int[2]"},         {"tu", "int[1]"},
      {"x", "int[2]"},     {"bad", "int[1]"},  {"local", "int[4]"},
  };
  for (const auto &test : cases)
    EXPECT_EQ(symbol_type(c, test.name), test.type) << test.name;
  symbol *typedef_name = find_symbol(c, "T", SYMBOL_TYPEDEF);
  ASSERT_NE(typedef_name, nullptr);
  EXPECT_EQ(describe(typedef_name->type), "int[]") << "sizing an object leaves its typedef alone";
  EXPECT_EQ(describe(declarations(c, "a")[0]->var_decl.type), "int[3]");
}

TEST(InitializerTest, ElementsMustFit) {
  const char *src = "int a[2] = {1, 2, 13, 14};\n"
                    "struct S { int a, b; } s = {1, 2, 23};\n"
                    "union U { int i; double d; } u = {1, 32};\n"
                    "union U v = {.d = 1.5, 42};\n"
                    "int x = {1, 52};\n"
                    "int e[2] = {{1, 62}, 3};\n"
                    "int m[2][3] = {1, 2, 3, 4, 5, 6, 77};\n"
                    "struct N { struct { int x, y; } in; int z; } n = {.in.y = 1, 2, 83};\n"
                    "struct Q { int a[2]; int b, c; } q = {.a[1] = 1, 2, 3, 94};\n"
                    "struct F { int n; char d[]; } f = {1, 102};\n"
                    "struct B { int a : 3; int : 2; int b; } bf = {1, 2, 113};\n"
                    "char s1[3] = \"abc\";\n"
                    "char s2[2] = \"abc\";\n"
                    "char s3[2] = {\"def\"};\n"
                    "char s4[] = L\"ab\";\n"
                    "unsigned short w1[2] = \"a\";\n"
                    "int b[3] = 5;\n"
                    "struct N n1 = {.in = {.y = 1}, 2};\n"
                    "struct N n2 = {.in.y = 1, 2};\n"
                    "struct Q q1 = {.a[1] = 1, 2, 3};\n"
                    "int p[3] = {1, [0] = 2, 3, 4};\n"
                    "char grid[2][4] = {\"abc\", \"def\"};\n"
                    "struct T { int a; char b[4]; } t1 = {1, \"abc\"}, t2 = {1, {\"abc\"}};\n"
                    "struct S sb = {.b = 1, 124};\n"
                    "int m2[2][2] = {1, [1] = {2}, 135};\n"
                    "char two[3] = {\"ab\", 'c'};\n"
                    "char ds[4] = {[0] = \"xyz\"};\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string excess = "excess elements in initializer";
  EXPECT_EQ(
      c.diagnostics,
      error_at(src, "13", 1, excess) + error_at(src, "23", 1, excess) +
          error_at(src, "32", 1, excess) + error_at(src, "42", 1, excess) +
          error_at(src, "52", 1, excess) + error_at(src, "62", 1, excess) +
          error_at(src, "77", 1, excess) + error_at(src, "83", 1, excess) +
          error_at(src, "94", 1, excess) + error_at(src, "102", 1, excess) +
          error_at(src, "113", 1, excess) +
          error_at(src, "\"abc\";\nchar s3", 1, "initializer string is too long for its array") +
          error_at(src, "\"def\"", 1, "initializer string is too long for its array") +
          error_at(src, "L\"ab\"", 1, "incompatible types in initialization") +
          error_at(src, "\"a\"", 1, "incompatible types in initialization") +
          error_at(src, "5;", 1, "an array needs a brace-enclosed initializer") +
          error_at(src, "124", 1, excess) + error_at(src, "135", 1, excess) +
          error_at(src, "\"ab\", 'c'", 1, "incompatible types in initialization") +
          error_at(src, "\"xyz\"", 1, "incompatible types in initialization"))
      << "excess is reported once per list, and a designation continues inside its aggregate";
}

TEST(InitializerTest, DesignatorsFollowTheConstraints) {
  const char *src = "struct S { int a, b; } s = {.c = 1};\n"
                    "int a[3] = {.x = 1};\n"
                    "struct S t = {[0] = 1};\n"
                    "int b[3] = {[3] = 1};\n"
                    "int c[3] = {[-1] = 1};\n"
                    "int d[3] = {[1.5] = 1};\n"
                    "int e[3] = {[1 / 0] = 1, 2, 3, 4};\n"
                    "int x = {.a = 1};\n"
                    "struct T { int i; } u = {.i.j = 1};\n"
                    "int h[2] = {[5] = 1, 2, 3};\n"
                    "int k[3] = {[2u] = 1, [1] = 2, [0] = 3};\n"
                    "void f(int n) { int g[3] = {[n] = 1}; }\n"
                    "struct S v = {.a = 1, .b = 2, .c = 3, .a = 4};\n"
                    "int w[2] = {[missing] = 1};\n"
                    "int nu[3] = {[-1ull] = 1};\n"
                    "struct N { struct { int x, y; } in; int z; } nf = {.in.y.q = 1, 2, 3, 4};\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(
      c.diagnostics,
      error_at(src, "{.c", 1, "no member named 'c'") +
          error_at(src, "{.x", 1, "a member designator needs a structure or union") +
          error_at(src, "{[0]", 1, "an array designator needs an array") +
          error_at(src, "3] = 1}", 1, "an array designator is past the end of the array") +
          error_at(src, "-1", 1, "an array designator cannot be negative") +
          error_at(src, "1.5", 1, "an array designator needs an integer constant expression") +
          error_at(src, "/ 0", 1, "division by zero in a constant expression") +
          error_at(src, "{.a = 1}", 1, "a designator cannot select from a scalar") +
          error_at(src, "{.i.j", 1,
                   "a designator needs a structure, union or array to select from") +
          error_at(src, "5] = 1, 2", 1, "an array designator is past the end of the array") +
          error_at(src, "n] =", 1, "an array designator needs an integer constant expression") +
          error_at(src, "{.a = 1, .b", 1, "no member named 'c'") +
          error_at(src, "missing", 1, "use of undeclared identifier 'missing'") +
          error_at(src, "-1ull", 1, "an array designator is past the end of the array") +
          error_at(src, "{.in.y.q", 1,
                   "a designator needs a structure, union or array to select from"))
      << "a failed designator skips its value and the next one starts from the list's frame";
}

TEST(InitializerTest, ScalarsConvertAsInAssignment) {
  const char *src = "struct P { int x; } q = {1};\n"
                    "struct O { struct P p; int z; };\n"
                    "void f(void) {\n"
                    "  int *p = 1;\n"
                    "  char *s = (const char *)0;\n"
                    "  struct P pt = 2;\n"
                    "  int i = q;\n"
                    "  int *ok = 0;\n"
                    "  const char *str = \"x\";\n"
                    "  struct P r = q;\n"
                    "  struct P rs[2] = {q, q};\n"
                    "  struct O o = {q, 1};\n"
                    "  struct O o2 = {1, 2};\n"
                    "  const struct P cq = q;\n"
                    "  struct O o3 = {cq, 1};\n"
                    "  double _Complex z = 1;\n"
                    "  _Bool b = &b;\n"
                    "  int braced = {3};\n"
                    "  long l = {missing};\n"
                    "  int *bp = {7};\n"
                    "  struct O uo[1] = {missing2, 171};\n"
                    "  const struct P cps[2] = {q, q};\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "1;\n  char", 1, "incompatible types in initialization") +
                error_at(src, "(const char *)0", 1, "qualifiers discarded in initialization") +
                error_at(src, "2;\n  int i", 1, "incompatible types in initialization") +
                error_at(src, "q;\n  int *ok", 1, "incompatible types in initialization") +
                error_at(src, "missing", 1, "use of undeclared identifier 'missing'") +
                error_at(src, "7}", 1, "incompatible types in initialization") +
                error_at(src, "missing2", 1, "use of undeclared identifier 'missing2'") +
                error_at(src, "171", 1, "excess elements in initializer"))
      << "an untyped value fills a whole element";
}

TEST(InitializerTest, StaticStorageNeedsConstants) {
  const char *src = "int x;\n"
                    "int arr[4];\n"
                    "struct M { int m; int n[2]; } sm;\n"
                    "int f(void);\n"
                    "int y = x;\n"
                    "long z = (long)&x;\n"
                    "int k = (1, 2);\n"
                    "long dd = &arr[1] - &arr[0];\n"
                    "int w[2] = {1, x};\n"
                    "int *cl = (int[]){2, x};\n"
                    "int *through = &*(&arr[x]);\n"
                    "int tern = x ? 1 : 2;\n"
                    "int *p1 = &x;\n"
                    "int *p2 = &x + 1;\n"
                    "int *p3 = arr + 2;\n"
                    "int *p4 = 2 + arr;\n"
                    "int *p5 = &arr[2];\n"
                    "int *p6 = &2[arr];\n"
                    "int *p7 = &arr[1] - 1;\n"
                    "int *p8 = &sm.m;\n"
                    "int *p9 = sm.n;\n"
                    "int *p10 = &(&sm)->m;\n"
                    "int *p11 = &*&x;\n"
                    "int *p12 = (int *)0x1000;\n"
                    "char (*ps)[4] = &\"abc\";\n"
                    "const char *p13 = &\"abc\"[1];\n"
                    "int *p14 = &(int){3};\n"
                    "int *p15 = (int[]){1, 2};\n"
                    "int (*fp)(void) = f;\n"
                    "int (*fq)(void) = &f;\n"
                    "int (*fr)(void) = *f;\n"
                    "int (*fs)(void) = &*f;\n"
                    "char *str = \"x\";\n"
                    "double d = 1.5 * 2;\n"
                    "int e = -(3) + ~1 + !0;\n"
                    "int t = 1 ? 2 : 3;\n"
                    "int *tp = 1 ? &x : 0;\n"
                    "int cast = (int)2.5;\n"
                    "unsigned long size = sizeof(int) + sizeof x;\n"
                    "enum { E = 4 } en = E;\n"
                    "static int u = missing;\n"
                    "int deref = *&x;\n"
                    "int (*fz)(void) = *fp;\n"
                    "int neg = -x;\n"
                    "int nt = !&x;\n"
                    "char *pc = (char *)&x;\n"
                    "int *pcs = &*(int *)&x;\n"
                    "int *pn = (int *)x;\n"
                    "int cmp = &x != 0;\n"
                    "int tn = 1 ? x : 2;\n"
                    "int tm = 1 ? 2 : x;\n"
                    "int *tr = &*(1 ? &x : 0);\n"
                    "int call = f();\n"
                    "struct M *pm;\n"
                    "int *pmm = &pm->m;\n"
                    "struct M sf(void);\n"
                    "int *sfn = sf().n;\n"
                    "int *both = x;\n"
                    "void g(int n) {\n"
                    "  int l;\n"
                    "  static int *r = &l;\n"
                    "  static int *c = &(int){3};\n"
                    "  static int s = n;\n"
                    "  static unsigned long v = sizeof(int[n]);\n"
                    "  extern int ex;\n"
                    "  static int *ok = &ex;\n"
                    "  static const char *name = __func__;\n"
                    "  static const char *first = &__func__[0];\n"
                    "  int automatic = l + n;\n"
                    "  int *local = &l;\n"
                    "  static int *st = &x;\n"
                    "  struct M lm;\n"
                    "  static int *lmm = &lm.m;\n"
                    "  static int keep;\n"
                    "  static int *kp = &keep;\n"
                    "  extern int lk = n;\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string constant = "an initializer for an object with static storage must be constant";
  EXPECT_EQ(
      c.diagnostics,
      error_at(src, "x;\nlong z", 1, constant) + error_at(src, "(long)&x", 1, constant) +
          error_at(src, ", 2)", 1, constant) + error_at(src, "- &arr[0]", 1, constant) +
          error_at(src, "x};\nint *cl", 1, constant) +
          error_at(src, "x};\nint *through", 1, constant) +
          error_at(src, "&*(&arr[x])", 1, constant) + error_at(src, "? 1 : 2", 1, constant) +
          error_at(src, "missing", 1, "use of undeclared identifier 'missing'") +
          error_at(src, "*&x;\nint (*fz", 1, constant) + error_at(src, "*fp;", 1, constant) +
          error_at(src, "-x;", 1, constant) + error_at(src, "!&x", 1, constant) +
          error_at(src, "(int *)x", 1, constant) + error_at(src, "!= 0", 1, constant) +
          error_at(src, "? x", 1, constant) + error_at(src, "? 2 : x", 1, constant) +
          error_at(src, "();\nstruct M *pm", 1, constant) + error_at(src, "&pm->m", 1, constant) +
          error_at(src, ".n;\nint *both", 1, constant) +
          error_at(src, "x;\nvoid g", 1, "incompatible types in initialization") +
          error_at(src, "&l;\n  static int *c", 1, constant) +
          error_at(src, "&(int){3};\n  static int s", 1, constant) +
          error_at(src, "n;\n  static unsigned", 1, constant) +
          error_at(src, "sizeof(int[n])", 1, constant) + error_at(src, "&lm.m", 1, constant) +
          error_at(src, "lk", 1, "cannot initialize block-scope declaration with linkage 'lk'"))
      << "&x != 0 is neither an arithmetic nor an address constant (6.6p7), though GCC accepts it";
}

TEST(InitializerTest, ObjectsMustBeCompleteAndFixedSize) {
  const char *src = "struct T;\n"
                    "typedef struct T TT;\n"
                    "struct T i = {0};\n"
                    "void f(int n) {\n"
                    "  int a[];\n"
                    "  struct T t;\n"
                    "  static struct T u;\n"
                    "  TT tt;\n"
                    "  void v;\n"
                    "  int vla[n] = {0};\n"
                    "  struct T j = {0};\n"
                    "  extern struct T e;\n"
                    "  extern int ea[];\n"
                    "  struct T *p;\n"
                    "  int b[] = {1};\n"
                    "  int ok[n];\n"
                    "  typedef struct T L;\n"
                    "}\n"
                    "void g(int a[], struct T *t) {}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(
      c.diagnostics,
      error_at(src, "i = {0}", 1, "an object with incomplete type cannot be initialized") +
          error_at(src, "a[];", 1, "incomplete type for object 'a'") +
          error_at(src, "t;", 1, "incomplete type for object 't'") +
          error_at(src, "u;", 1, "incomplete type for object 'u'") +
          error_at(src, "tt;", 1, "incomplete type for object 'tt'") +
          error_at(src, "v;", 1, "incomplete type for object 'v'") +
          error_at(src, "vla", 1, "a variable length array cannot be initialized") +
          error_at(src, "j = {0}", 1, "an object with incomplete type cannot be initialized"));
}

TEST(DeclaratorTest, DeclaratorsDescribePossibleTypes) {
  const char *src = "struct T;\n"
                    "struct H { int n; char d[]; };\n"
                    "typedef int F(void);\n"
                    "typedef int I[2];\n"
                    "typedef int *PA[2];\n"
                    "typedef int *PI;\n"
                    "struct T a[3];\n"
                    "extern struct T ea[];\n"
                    "int b[3][];\n"
                    "F g[2];\n"
                    "int fa(void)[3];\n"
                    "int ff(void)(void);\n"
                    "F ffr(void);\n"
                    "struct H hs[2];\n"
                    "struct HM { struct H hm[2]; };\n"
                    "int c[1.5];\n"
                    "restrict int r;\n"
                    "int (*restrict fp)(void);\n"
                    "restrict I ri;\n"
                    "struct T tr(void) { }\n"
                    "int dfa(void)[3] { }\n"
                    "void (*pv[2])(void);\n"
                    "int (*pa(void))[3];\n"
                    "struct T (*pf)(void);\n"
                    "struct T proto(void);\n"
                    "int *restrict ok;\n"
                    "restrict PI rp;\n"
                    "restrict PA rpa;\n"
                    "void q(int *restrict x, int arr[restrict]);\n"
                    "void (*const cfp)(void);\n"
                    "struct H *hp;\n"
                    "void v(void) { return; }\n"
                    "double dd;\n"
                    "void k(void) { int e[dd]; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(
      c.diagnostics,
      error_at(src, "a[3]", 1, "an array needs a complete element type") +
          error_at(src, "ea[]", 1, "an array needs a complete element type") +
          error_at(src, "b[3][]", 1, "an array needs a complete element type") +
          error_at(src, "g[2]", 1, "an array cannot have elements of function type") +
          error_at(src, "fa(", 1, "a function cannot return an array or a function") +
          error_at(src, "ff(", 1, "a function cannot return an array or a function") +
          error_at(src, "ffr(", 1, "a function cannot return an array or a function") +
          error_at(src, "hs[2]", 1, "an array cannot have elements with a flexible array member") +
          error_at(src, "hm[2]", 1, "an array cannot have elements with a flexible array member") +
          error_at(src, "c[1.5]", 1, "an array size needs an integer type") +
          error_at(src, "r;", 1, "restrict needs a pointer to an object or incomplete type") +
          error_at(src, "fp)", 1, "restrict needs a pointer to an object or incomplete type") +
          error_at(src, "ri;", 1, "restrict needs a pointer to an object or incomplete type") +
          error_at(src, "tr(", 1, "incomplete return type in the definition of 'tr'") +
          error_at(src, "dfa(", 1, "a function cannot return an array or a function") +
          error_at(src, "e[dd]", 1, "an array size needs an integer type"));
}

TEST(InlineTest, InlineDefinitionsKeepToThemselves) {
  const char *src =
      "static int hidden;\n"
      "static int helper(void);\n"
      "typedef const int CI;\n"
      "typedef int PLAIN;\n"
      "inline int a(void) { return hidden; }\n"
      "inline int b(void) { return helper(); }\n"
      "inline int c(void) { static int nc; return nc; }\n"
      "inline int d(void) { static const int nd = 1; static CI ci; static CI cia[2]; return nd + "
      "ci + cia[0]; }\n"
      "inline int dp(void) { static const PLAIN cp[2] = {1}; return cp[0]; }\n"
      "inline int e(void) { static const int ne[2] = {1}; return ne[0]; }\n"
      "inline int k(void) { return sizeof hidden; }\n"
      "inline int m(void) { enum { EM = 1 }; extern int ext; int l = 2; return EM + ext + l; }\n"
      "static inline int g(void) { static int ng; return hidden + ng; }\n"
      "extern inline int h(void) { static int nh; return hidden + nh; }\n"
      "int i(void);\n"
      "inline int i(void) { static int ni; return hidden + ni; }\n"
      "inline int u(void);\n"
      "extern inline int u(void);\n"
      "inline int u(void) { static int nu; return hidden + nu; }\n"
      "inline int j(void);\n"
      "void s2(void) { int j2(void); }\n"
      "inline int j2(void) { static int nj2; return nj2; }\n"
      "void s(void) { int j(void); }\n"
      "inline int j(void) { static int nj; return nj; }\n"
      "static int *after = &hidden;\n"
      "void l(void) { static int nl; hidden = nl; }\n"
      "static int helper(void) { return 0; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string internal =
      "an inline definition with external linkage cannot refer to internal linkage identifier";
  const std::string modifiable =
      "an inline definition with external linkage cannot define modifiable static object";
  EXPECT_EQ(c.diagnostics, error_at(src, "hidden; }", 1, internal + " 'hidden'") +
                               error_at(src, "helper()", 1, internal + " 'helper'") +
                               error_at(src, "nc;", 1, modifiable + " 'nc'") +
                               error_at(src, "hidden; }\ninline int m", 1, internal + " 'hidden'") +
                               error_at(src, "nj2;", 1, modifiable + " 'nj2'") +
                               error_at(src, "nj;", 1, modifiable + " 'nj'"))
      << "the definition leaves inline mode when its body ends";
}

TEST(InitializerTest, AnEmptyListLeftByAParseErrorIsSkipped) {
  const char *src = "int x = {};\n"
                    "struct S { int a; } s = {};\n"
                    "void f(void) { double *p = {}; int y = (int){}; }\n";
  Checked c;
  check(c, src);
  EXPECT_EQ(c.parse_errors, 4) << "one for each empty list";
  EXPECT_EQ(c.errors, 0) << c.diagnostics;
}

TEST(InitializerTest, UntypedPartsAddNoError) {
  const char *src = "int uc = (int)missing1;\n"
                    "int ud = -(int)missing2 + 1;\n"
                    "int ue = (int)missing3 ? 1 : 2;\n"
                    "int uf = 1 ? (int)missing4 : 2;\n"
                    "int ug = 1 ? 2 : (int)missing5;\n"
                    "long uh = (long)(int)missing6;\n"
                    "int ui = 1 + (int)missing7;\n"
                    "int ua[missing8];\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  std::string expected;
  for (int i = 1; i <= 8; i++) {
    std::string name = "missing" + std::to_string(i);
    expected += error_at(src, name.c_str(), 1, "use of undeclared identifier '" + name + "'");
  }
  EXPECT_EQ(c.diagnostics, expected) << "a cast of an untyped operand is not known to be constant";

  const char *broken = "typedef int U, ;\n"
                       "restrict U r;\n"
                       "U arr[2];\n"
                       "U ui = {1};\n"
                       "int xv;\n"
                       "U uv = {xv};\n"
                       "struct SU { U m; int n; } su = {1, 2}, sv = {.m.x = 1};\n"
                       "void f(void) { static U su = 1; int a[2] = {(U)1}; }\n";
  Checked d;
  check(d, broken);
  ASSERT_EQ(d.parse_errors, 1);
  EXPECT_EQ(d.diagnostics, error_at(broken, "r;", 1, "unknown type name 'U'") +
                               error_at(broken, "arr[", 1, "unknown type name 'U'") +
                               error_at(broken, "ui =", 1, "unknown type name 'U'") +
                               error_at(broken, "uv =", 1, "unknown type name 'U'") +
                               error_at(broken, "m;", 1, "unknown type name 'U'") +
                               error_at(broken, "su = 1", 1, "unknown type name 'U'") +
                               error_at(broken, "(U)1", 1, "unknown type name 'U'"))
      << "declarators and initializers with a type that failed to resolve check nothing";
}

static void expect_clean_source(const char *source) {
  SCOPED_TRACE(source);
  Checked c;
  check(c, source);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0) << c.diagnostics;
}

TEST(DeclaratorTest, AnArrayOfUnspecifiedSizeIsACompleteElementType) {
  expect_clean_source("double maximum(int n, int m, double a[n][m]);\n"
                      "double maximum(int n, int m, double a[*][*]);\n"
                      "double maximum(int n, int m, double a[ ][*]);\n"
                      "double maximum(int n, int m, double a[ ][m]);\n"
                      "void g(int n, double a[n][*]);\n");
}

TEST(CompositeTypeTest, CompatibleDeclarationsShareTheCompositeType) {
  expect_clean_source("int (*p)[];\n"
                      "int (*p)[3];\n"
                      "int sp[sizeof(*p) == 12 ? 1 : -1];\n");
  expect_clean_source("int a10[10];\n"
                      "int f10(void) { extern int a10[]; return (int)sizeof a10 == 40; }\n");
  expect_clean_source("int (*q3)[3];\n"
                      "int (*qu)[];\n"
                      "void g(int c) {\n"
                      "  int s1[sizeof(*(c ? qu : q3)) == 12 ? 1 : -1];\n"
                      "  int s2[sizeof(*(c ? q3 : qu)) == 12 ? 1 : -1];\n"
                      "  (void)s1; (void)s2;\n"
                      "}\n");
  expect_clean_source("int (*fp)();\n"
                      "int (*fp)(int);\n"
                      "int use(void) { return fp(1); }\n");
}

TEST(CompositeTypeTest, APrototypeInOneDeclarationTypesEveryCall) {
  const char *src = "int (*fp)();\n"
                    "int (*fp)(int);\n"
                    "void bad(void) { fp(1, 2); }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics, error_at(src, "(1, 2)", 1, "too many arguments in call"))
      << "the composite type keeps the prototype, so later calls are checked against it";
}

TEST(CallTest, AVoidArgumentIsAConversionProblem) {
  const char *src = "void ip(int);\n"
                    "void gv(void);\n"
                    "void n(void) { ip(gv()); }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics, error_at(src, "()); }", 1, "incompatible types in argument"))
      << "void is incomplete, but the useful diagnostic is the conversion, not completeness";
}

TEST(CompositeTypeTest, AnIncompatibleBlockScopeDeclarationStillConflicts) {
  const char *src = "int b5[5];\n"
                    "void h(void) { extern int b5[3]; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 1);
  EXPECT_NE(c.diagnostics.find("conflicting types for 'b5'"), std::string::npos) << c.diagnostics;
}

TEST(StatementTest, AGotoCannotEnterTheScopeOfAVariablyModifiedIdentifier) {
  const char *src = "void f(int n) {\n"
                    "  goto lab3;\n"
                    "  {\n"
                    "    double a[n];\n"
                    "  lab3:\n"
                    "    a[0] = 3;\n"
                    "    goto lab4;\n"
                    "    a[0] = 5;\n"
                    "  lab4:\n"
                    "    a[0] = 6;\n"
                    "  }\n"
                    "  goto lab4;\n"
                    "  goto out;\n"
                    "out:\n"
                    "  ;\n"
                    "}\n"
                    "void g(int n) {\n"
                    "back:\n"
                    "  ;\n"
                    "  int v[n];\n"
                    "  goto back;\n"
                    "  goto after;\n"
                    "after:\n"
                    "  ;\n"
                    "}\n"
                    "void h(int n) {\n"
                    "  goto t1;\n"
                    "  {\n"
                    "    typedef int VLA[n];\n"
                    "  t1:\n"
                    "    ;\n"
                    "  }\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string problem =
      "a goto cannot jump into the scope of an identifier with variably modified type";
  EXPECT_EQ(c.diagnostics, error_at(src, "goto lab3", 1, problem) +
                               error_at(src, "goto lab4;\n  goto out", 1, problem) +
                               error_at(src, "goto t1", 1, problem))
      << "C99 6.8.6.1p1; jumping within or out of the scope is allowed";
}

TEST(StatementTest, ASwitchCannotEnterTheScopeOfAVariablyModifiedIdentifier) {
  const char *src = "void f(int n, int x) {\n"
                    "  switch (x) { case 1: ; int a[n]; case 2: a[0] = 1; }\n"
                    "  switch (x) { case 3: ; int b[n]; default: ; }\n"
                    "  { int d[n]; switch (x) { case 4: d[0] = 1; default: ; } }\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string problem =
      "a switch cannot jump into the scope of an identifier with variably modified type";
  EXPECT_EQ(c.diagnostics, error_at(src, "case 2", 1, problem) +
                               error_at(src, "default: ; }\n  { int d", 1, problem))
      << "C99 6.8.4.2p2; a switch entirely inside the scope is allowed";
}

TEST(DefinitionTest, AStaticFunctionThatIsUsedMustBeDefined) {
  const char *src = "static int f(void);\n"
                    "static int g(void);\n"
                    "static int h(void);\n"
                    "static int k(void);\n"
                    "static int k(void) { return 0; }\n"
                    "static int i;\n"
                    "int p(void);\n"
                    "int use(void) { return f() + (int)sizeof(g()) + k() + i + p(); }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "f(void)", 1, "static function used but never defined 'f'"))
      << "C99 6.9p3; a use inside sizeof with a constant result does not count, and an unused "
         "declaration needs no definition";
}

TEST(CompatibilityTest, AnOldStyleDefinitionFixesTheNumberOfParameters) {
  const struct {
    const char *source;
    int conflicts;
  } cases[] = {
      {"int g() { return 0; } int g(int);", 1},
      {"int g(int); int g() { return 0; }", 1},
      {"int g() { return 0; } int g(void);", 0},
      {"int g() { return 0; } int g();", 0},
      {"int g(); int g(int);", 0},
      {"int f(a) int a; { return a; } int f(int);", 0},
      {"int f(a) int a; { return a; } int f(int, int);", 1},
  };
  for (const auto &test : cases) {
    SCOPED_TRACE(test.source);
    Checked c;
    check(c, test.source);
    ASSERT_EQ(c.parse_errors, 0);
    EXPECT_EQ(c.errors, test.conflicts) << c.diagnostics;
  }
}

TEST(LvalueTest, RegisterParametersHaveNoAddress) {
  const char *src = "void f(register int r) { int *p = &r; (void)p; }\n"
                    "int g(a) register int a; { int *p = &a; return *p; }\n"
                    "void h(int r) { int *p = &r; (void)p; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics,
            error_at(src, "&r", 1, "cannot take the address of register object 'r'") +
                error_at(src, "&a", 1, "cannot take the address of register object 'a'"))
      << "C99 6.5.3.2p1";
}

TEST(InlineTest, MainCannotBeInline) {
  const char *src = "inline int main(void);\n"
                    "inline int main(void) { return 0; }\n"
                    "inline int notmain(void) { return 0; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string problem = "main cannot be declared inline";
  EXPECT_EQ(c.diagnostics,
            error_at(src, "main(void);", 1, problem) + error_at(src, "main(void) {", 1, problem))
      << "C99 6.7.4p4";
}

TEST(CallTest, AnArgumentNeedsACompleteType) {
  const char *src = "struct I;\n"
                    "void g(struct I);\n"
                    "void old();\n"
                    "void h(struct I *p) { g(*p); old(*p); }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string problem = "an argument needs a complete object type";
  EXPECT_EQ(c.diagnostics, error_at(src, "*p", 2, problem) + error_at(src, "*p", 3, problem))
      << "C99 6.5.2.2p2; a prototype does not excuse an incomplete argument";
}

TEST(InitializerTest, AStaticInitializerMustEvaluate) {
  const char *src = "static int a = 1 / 0;\n"
                    "static int b = 2147483647 + 1;\n"
                    "static int ok1 = 1 ? 2 : 1 / 0;\n"
                    "static int ok2 = 0 && (1 / 0);\n"
                    "static int ok3 = 2147483647;\n"
                    "int *const cl = (int[]){1 / 0};\n"
                    "void f(void) { int r = 1 / 0; (void)r; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.diagnostics, error_at(src, "/ 0", 1, "division by zero in a constant expression") +
                               error_at(src, "+ 1", 1, "overflow in a constant expression") +
                               error_at(src, "/ 0", 4, "division by zero in a constant expression"))
      << "C99 6.6p4 applies to the constant expressions an initializer requires, not to a value "
         "computed at run time";
}

TEST(DeclaratorTest, ImaginaryTypesAreRejected) {
  const char *src = "float _Imaginary z;\n"
                    "double _Imaginary *p;\n"
                    "typedef long double _Imaginary LI;\n"
                    "double _Complex ok;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string problem = "imaginary types are not supported";
  EXPECT_EQ(c.diagnostics, error_at(src, "z;", 1, problem) + error_at(src, "p;", 1, problem) +
                               error_at(src, "LI;", 1, problem))
      << "Annex G is optional, so the keyword's types are rejected rather than silently treated as "
         "their real type";
}

TEST(ConstantExpressionTest, ConstantsOutOfRangeAreReportedWhereTheyAreWritten) {
  const struct {
    const char *source;
    const char *problem;
  } cases[] = {
      {"unsigned long long a = 99999999999999999999;", "integer constant is too large"},
      {"unsigned long long a = 18446744073709551615;", "integer constant is too large"},
      {"long long a = 9223372036854775808;", "integer constant is too large"},
      {"unsigned long long a = 0xffffffffffffffff;", nullptr},
      {"unsigned long long a = 18446744073709551615u;", nullptr},
      {"double a = 1e999;", "floating constant is out of range for its type"},
      {"float a = 1e39f;", "floating constant is out of range for its type"},
      {"long double a = 1e400L;", nullptr},
      {"double a = 1e-999;", nullptr},
      {"double a = 1e308;", nullptr},
  };
  for (const auto &test : cases) {
    SCOPED_TRACE(test.source);
    Checked c;
    check(c, test.source);
    ASSERT_EQ(c.parse_errors, 0);
    EXPECT_EQ(c.errors, test.problem == nullptr ? 0 : 1) << c.diagnostics;
    if (test.problem != nullptr)
      EXPECT_NE(c.diagnostics.find(test.problem), std::string::npos) << c.diagnostics;
  }
}

TEST(ConstantExpressionTest, AConstantOutOfRangeIsStillReportedOnlyOnce) {
  const char *src = "int a[(int)1e999];\n"
                    "enum { E = 18446744073709551615 };\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 2) << c.diagnostics;
}

TEST(AlignmentTest, AnAlignedTypedefReplacesTheAlignmentAndKeepsTheSize) {
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    SCOPED_TRACE(kind == TARGET_LINUX_X64 ? "linux" : "windows");
    expect_layouts({
        {"typedef int AI __attribute__((aligned(16)));\nstruct R { char c; AI m; };\n",
         32,
         16,
         {{"c", 0, 0, 0}, {"m", 16, 0, 0}}},
        {"typedef int AI2 __attribute__((aligned(2)));\nstruct R { char c; AI2 m; };\n",
         6,
         2,
         {{"c", 0, 0, 0}, {"m", 2, 0, 0}}},
        {"typedef int AI __attribute__((aligned(16)));\n"
         "typedef AI AI8 __attribute__((aligned(8)));\nstruct R { char c; AI8 m; };\n",
         16,
         8,
         {{"c", 0, 0, 0}, {"m", 8, 0, 0}}},
        {"typedef int AI __attribute__((aligned(16)));\ntypedef AI B;\n"
         "struct R { char c; B m; };\n",
         32,
         16,
         {{"c", 0, 0, 0}, {"m", 16, 0, 0}}},
        {"typedef __attribute__((aligned(16))) struct F { unsigned long long p[2]; } FT;\n"
         "typedef FT JB[16];\nstruct R { char c; JB m; };\n",
         272,
         16,
         {{"c", 0, 0, 0}, {"m", 16, 0, 0}}},
        {"typedef __attribute__((aligned(16))) struct F { unsigned long long p[2]; } FT;\n"
         "struct R { char c; struct F m; };\n",
         24,
         8,
         {{"c", 0, 0, 0}, {"m", 8, 0, 0}}},
    });
    expect_values("typedef int AI __attribute__((aligned(16)));\n", {{"sizeof(AI)", 4}});
  }
}

TEST(AlignmentTest, AMemberAlignedAttributeOnlyRaises) {
  expect_layouts({
      {"struct R { char c; int m __attribute__((aligned(16))); };\n",
       32,
       16,
       {{"c", 0, 0, 0}, {"m", 16, 0, 0}}},
      {"struct R { char c; int m __attribute__((aligned(2))); };\n",
       8,
       4,
       {{"c", 0, 0, 0}, {"m", 4, 0, 0}}},
      {"struct R { char c; int m[] __attribute__((aligned(8))); };\n",
       8,
       8,
       {{"c", 0, 0, 0}, {"m", 8, 0, 0}}},
  });
}

TEST(AlignmentTest, ARecordAlignedAttributeOnlyRaisesTheRecord) {
  expect_layouts({
      {"struct R { int x; } __attribute__((aligned(16)));\n", 16, 16, {{"x", 0, 0, 0}}},
      {"struct __attribute__((aligned(8))) R { char x; };\n", 8, 8, {{"x", 0, 0, 0}}},
      {"struct R { double d; } __attribute__((aligned(4)));\n", 8, 8, {{"d", 0, 0, 0}}},
  });
}

TEST(AlignmentTest, PackCapsEveryMemberButNotTheRecordsOwnAttribute) {
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    SCOPED_TRACE(kind == TARGET_LINUX_X64 ? "linux" : "windows");
    expect_layouts(
        {
            {"#pragma pack(1)\nstruct R { char c; int i; };\n",
             5,
             1,
             {{"c", 0, 0, 0}, {"i", 1, 0, 0}}},
            {"#pragma pack(2)\nstruct R { char c; double d; };\n",
             10,
             2,
             {{"c", 0, 0, 0}, {"d", 2, 0, 0}}},
            {"#pragma pack(16)\nstruct R { char c; long double d; };\n",
             32,
             16,
             {{"c", 0, 0, 0}, {"d", 16, 0, 0}}},
            {"#pragma pack(8)\nstruct R { char c; long double d; };\n",
             24,
             8,
             {{"c", 0, 0, 0}, {"d", 8, 0, 0}}},
            {"#pragma pack(2)\nstruct R { char c; int m __attribute__((aligned(16))); };\n",
             6,
             2,
             {{"c", 0, 0, 0}, {"m", 2, 0, 0}}},
            {"typedef int AI __attribute__((aligned(16)));\n#pragma pack(2)\n"
             "struct R { char c; AI m; };\n",
             6,
             2,
             {{"c", 0, 0, 0}, {"m", 2, 0, 0}}},
            {"#pragma pack(2)\nstruct R { char c; int m[] __attribute__((aligned(8))); };\n",
             2,
             2,
             {{"c", 0, 0, 0}, {"m", 2, 0, 0}}},
            {"#pragma pack(2)\nstruct R { int x; } __attribute__((aligned(16)));\n",
             16,
             16,
             {{"x", 0, 0, 0}}},
        },
        true);
  }
}

TEST(AlignmentTest, ThePackInForceAtTheClosingBraceCounts) {
  expect_layouts({{"#pragma pack(1)\nstruct R { char c;\n#pragma pack()\nint i; };\n",
                   8,
                   4,
                   {{"c", 0, 0, 0}, {"i", 4, 0, 0}}}},
                 true);
}

TEST(AlignmentTest, MicrosoftBitFieldsUnderPack) {
  TargetGuard guard(TARGET_WINDOWS_X64);
  expect_layouts(
      {
          {"#pragma pack(1)\nstruct R { char a; int b : 4; };\n",
           5,
           1,
           {{"a", 0, 0, 0}, {"b", 1, 0, 4}}},
          {"#pragma pack(2)\nstruct R { char c; int b : 30; };\n",
           6,
           2,
           {{"c", 0, 0, 0}, {"b", 2, 0, 30}}},
          {"#pragma pack(2)\nstruct R { char c; int a : 3; int : 0; char d; };\n",
           8,
           2,
           {{"c", 0, 0, 0}, {"a", 2, 0, 3}, {"d", 6, 0, 0}}},
          {"#pragma pack(1)\nunion R { int a : 3; char c; };\n",
           1,
           1,
           {{"a", 0, 0, 3}, {"c", 0, 0, 0}}},
          {"#pragma pack(1)\nunion R { int a : 9; };\n", 2, 1, {{"a", 0, 0, 9}}},
          {"#pragma pack(2)\nstruct R { char c; int : 0; char d; };\n",
           2,
           1,
           {{"c", 0, 0, 0}, {"d", 1, 0, 0}}},
          {"#pragma pack(1)\nstruct R { char c; int b : 30; };\n",
           5,
           1,
           {{"c", 0, 0, 0}, {"b", 1, 0, 30}}},
          {"#pragma pack(4)\nstruct R { char c; long long b : 40; };\n",
           12,
           4,
           {{"c", 0, 0, 0}, {"b", 4, 0, 40}}},
          {"#pragma pack(1)\nstruct R { char c; short a : 3; short b : 3; };\n",
           3,
           1,
           {{"c", 0, 0, 0}, {"a", 1, 0, 3}, {"b", 1, 3, 3}}},
      },
      true);
}

TEST(AlignmentTest, SystemVBitFieldsUnderPack) {
  TargetGuard guard(TARGET_LINUX_X64);
  expect_layouts(
      {
          {"#pragma pack(1)\nstruct R { char a; int b : 4; };\n",
           2,
           1,
           {{"a", 0, 0, 0}, {"b", 0, 8, 4}}},
          {"#pragma pack(2)\nstruct R { char c; int b : 30; };\n",
           6,
           2,
           {{"c", 0, 0, 0}, {"b", 0, 8, 30}}},
          {"#pragma pack(2)\nstruct R { char c; int a : 3; int : 0; char d; };\n",
           6,
           2,
           {{"c", 0, 0, 0}, {"a", 0, 8, 3}, {"d", 4, 0, 0}}},
          {"#pragma pack(1)\nunion R { int a : 3; char c; };\n",
           1,
           1,
           {{"a", 0, 0, 3}, {"c", 0, 0, 0}}},
          {"#pragma pack(1)\nunion R { int a : 9; };\n", 2, 1, {{"a", 0, 0, 9}}},
          {"#pragma pack(2)\nstruct R { char c; int : 0; char d; };\n",
           5,
           1,
           {{"c", 0, 0, 0}, {"d", 4, 0, 0}}},
          {"#pragma pack(1)\nstruct R { char c; int b : 30; };\n",
           5,
           1,
           {{"c", 0, 0, 0}, {"b", 0, 8, 30}}},
          {"#pragma pack(4)\nstruct R { char c; long long b : 40; };\n",
           8,
           4,
           {{"c", 0, 0, 0}, {"b", 0, 8, 40}}},
          {"#pragma pack(1)\nstruct R { char c; short a : 3; short b : 3; };\n",
           2,
           1,
           {{"c", 0, 0, 0}, {"a", 0, 8, 3}, {"b", 0, 11, 3}}},
      },
      true);
}

TEST(AlignmentTest, ArrayElementsCannotBeOverAligned) {
  const char *src =
      "typedef int AI __attribute__((aligned(16)));\n"
      "typedef __attribute__((aligned(16))) struct F { unsigned long long p[2]; } FT;\n"
      "AI bad[2];\n"
      "FT good[2];\n"
      "typedef AI AIs[1];\n"
      "struct W { AI m[4]; };\n"
      "int plain[3];\n"
      "AI single;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 3) << c.diagnostics;
  std::string message = "alignment of array elements is greater than element size";
  EXPECT_EQ(c.diagnostics, error_at(src, "bad", 1, message) + error_at(src, "AIs", 1, message) +
                               error_at(src, "m[4]", 1, message));
}

TEST(AlignmentTest, ABitFieldCannotBeAligned) {
  const char *src = "struct R { int a : 3 __attribute__((aligned(8))); int b; };\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 1) << c.diagnostics;
  EXPECT_EQ(c.diagnostics,
            error_at(src, "a :", 1, "the aligned attribute is not supported for bit-field 'a'"));
  symbol *r = record_symbol(c, "R");
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->alignment, 0) << "a record with a bad bit-field has no layout";
}

TEST(ModeTest, ANameDeclaredWithModeCannotBeUsed) {
  const char *src = "typedef int T __attribute__((mode(TI)));\n"
                    "typedef int U __attribute__((mode(DI)));\n"
                    "T x;\n"
                    "int y = sizeof(T);\n"
                    "typedef T T2;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 3) << c.diagnostics;
  std::string message = "use of a name declared with the unsupported mode attribute 'T'";
  EXPECT_EQ(c.diagnostics, error_at(src, "x;", 1, message) + error_at(src, "(T)", 1, message) +
                               error_at(src, "T2", 1, message))
      << "declaring U is fine; only a use is an error, reported where the declaration or type "
         "name is, like every other typedef-name problem";
}

static const char *builtin_declarations = "typedef __builtin_va_list va_list;\n"
                                          "va_list ap;\n"
                                          "struct S { char c; int a[2]; };\n";

TEST(BuiltinTest, EachBuiltinHasItsResultType) {
  const char *d = builtin_declarations;
  expect_types({
      {d, "__builtin_va_arg(ap, int)", "int"},
      {d, "__builtin_va_arg(ap, const double)", "double"},
      {d, "__builtin_va_end(ap)", "void"},
      {d, "__builtin_va_copy(ap, ap)", "void"},
      {d, "__builtin_offsetof(struct S, a[1])", "unsigned long long"},
      {d, "__builtin_types_compatible_p(int, long)", "int"},
      {d, "__builtin_choose_expr(1, 1.0f, \"x\")", "float"},
      {d, "__builtin_choose_expr(0, 1.0f, \"x\")", "char[2]"},
      {d, "__builtin_huge_val()", "double"},
      {d, "__builtin_huge_valf()", "float"},
      {d, "__builtin_huge_vall()", "long double"},
      {d, "__builtin_inff()", "float"},
      {d, "__builtin_nanf(\"\")", "float"},
      {d, "__builtin_isgreater(1.0, 2)", "int"},
      {d, "__builtin_isgreaterequal(1.0f, 2.0)", "int"},
      {d, "__builtin_isless(1.0L, 2)", "int"},
      {d, "__builtin_islessequal(1, 2.0)", "int"},
      {d, "__builtin_islessgreater(1.0, 2.0)", "int"},
      {d, "__builtin_isunordered(1.0, 2.0)", "int"},
      {d, "__builtin_signbit(1.0)", "int"},
      {d, "__builtin_signbitf(1.0f)", "int"},
      {d, "__builtin_signbitl(1.0L)", "int"},
      {d, "__builtin_llabs(1)", "long long"},
      {d, "__builtin_trap()", "void"},
      {d, "__builtin_unreachable()", "void"},
      {"", "1.0iF", "float _Complex"},
      {"", "1.0fi", "float _Complex"},
      {"", "1.0i", "double _Complex"},
      {"", "1.0Lj", "long double _Complex"},
      {"", "0x1p3jL", "long double _Complex"},
  });
  TargetGuard guard(TARGET_LINUX_X64);
  expect_types({{d, "__builtin_offsetof(struct S, a[1])", "unsigned long"}});
}

TEST(BuiltinTest, VaListHasEachTargetsShape) {
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    bool on_linux = kind == TARGET_LINUX_X64;
    SCOPED_TRACE(on_linux ? "linux" : "windows");
    expect_values("struct W { char c; __builtin_va_list v; };\n",
                  {
                      {"sizeof(__builtin_va_list)", on_linux ? 24 : 8},
                      {"sizeof(struct W)", on_linux ? 32 : 16},
                  });
    const char *src = "typedef __builtin_va_list va_list;\n"
                      "int sum(int n, ...) {\n"
                      "  va_list ap, aq;\n"
                      "  __builtin_va_start(ap, n);\n"
                      "  __builtin_va_copy(aq, ap);\n"
                      "  int s = __builtin_va_arg(ap, int) + __builtin_va_arg(aq, int);\n"
                      "  __builtin_va_end(aq);\n"
                      "  __builtin_va_end(ap);\n"
                      "  return s;\n"
                      "}\n"
                      "int vsum(va_list ap) { return __builtin_va_arg(ap, int); }\n"
                      "void copy_param(va_list s) { va_list d; __builtin_va_copy(d, s); }\n";
    Checked c;
    check(c, src);
    ASSERT_EQ(c.parse_errors, 0);
    EXPECT_EQ(c.errors, 0) << c.diagnostics;
  }
}

TEST(BuiltinTest, VaListIsAnLvalueOnlyWhereItIsNotAnArray) {
  const char *src = "void f(int n, ...) {\n"
                    "  __builtin_va_list ap;\n"
                    "  __builtin_va_start(ap, n);\n"
                    "  __builtin_va_end((0, ap));\n"
                    "}\n";
  {
    Checked c;
    check(c, src);
    EXPECT_EQ(c.diagnostics,
              error_at(src, ", ap))", 1, "not an lvalue as an argument of '__builtin_va_end'"))
        << "on Windows va_list is a char *, so va_end needs its address";
  }
  TargetGuard guard(TARGET_LINUX_X64);
  Checked c;
  check(c, src);
  EXPECT_EQ(c.errors, 0) << "on Linux the array decays to a pointer, which is enough\n"
                         << c.diagnostics;
}

TEST(BuiltinTest, VaMisuseIsReported) {
  const char *src = "typedef __builtin_va_list va_list;\n"
                    "int variadic(int n, ...) { return n; }\n"
                    "va_list g;\n"
                    "int y = sizeof(__builtin_va_start(g, y), 1);\n"
                    "void fixed(int n) { va_list ap; __builtin_va_start(ap, n); }\n"
                    "int x;\n"
                    "void bad(int n, ...) {\n"
                    "  va_list ap;\n"
                    "  __builtin_va_start(x, n);\n"
                    "  __builtin_va_start((char *)ap, n);\n"
                    "  __builtin_va_end(1);\n"
                    "  __builtin_va_copy(ap, 2);\n"
                    "  __builtin_va_arg(ap, void);\n"
                    "  __builtin_va_arg(ap, int[2]);\n"
                    "  __builtin_va_arg(ap, int(void));\n"
                    "  __builtin_va_start(ap);\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  std::string type_problem =
      "the type argument of '__builtin_va_arg' must be a complete object type that is not an array";
  EXPECT_EQ(
      c.diagnostics,
      error_at(src, "__builtin_va_start(g", 1,
               "a function without '...' cannot call '__builtin_va_start'") +
          error_at(src, "__builtin_va_start(ap, n)", 1,
                   "a function without '...' cannot call '__builtin_va_start'") +
          error_at(src, "x, n", 1, "not a va_list as an argument of '__builtin_va_start'") +
          error_at(src, "(char *)ap", 1, "not an lvalue as an argument of '__builtin_va_start'") +
          error_at(src, "1);\n  __builtin_va_copy", 1,
                   "not a va_list as an argument of '__builtin_va_end'") +
          error_at(src, "2);", 1, "not a va_list as an argument of '__builtin_va_copy'") +
          error_at(src, "__builtin_va_arg(ap, void)", 1, type_problem) +
          error_at(src, "__builtin_va_arg(ap, int[2])", 1, type_problem) +
          error_at(src, "__builtin_va_arg(ap, int(void))", 1, type_problem) +
          error_at(src, "__builtin_va_start(ap);", 1,
                   "wrong number of arguments to '__builtin_va_start'"));
}

TEST(BuiltinTest, OffsetofComputesTheConstantOffset) {
  expect_values("struct S { char c; int a[4]; struct In { char x; double d; } in[3]; short s;\n"
                "           long double ld; };\n"
                "union U { char c; double d; int a[5]; };\n"
                "typedef struct S T;\n",
                {
                    {"__builtin_offsetof(struct S, c)", 0},
                    {"__builtin_offsetof(struct S, a)", 4},
                    {"__builtin_offsetof(struct S, a[3])", 16},
                    {"__builtin_offsetof(struct S, in)", 24},
                    {"__builtin_offsetof(struct S, in[2])", 56},
                    {"__builtin_offsetof(struct S, in[1].d)", 48},
                    {"__builtin_offsetof(struct S, s)", 72},
                    {"__builtin_offsetof(struct S, ld)", 80},
                    {"__builtin_offsetof(union U, a[4])", 16},
                    {"__builtin_offsetof(T, in[2].x)", 56},
                    {"__builtin_offsetof(struct S, a[1 + 1])", 12},
                });
  const char *src = "struct S { int a[4]; };\n"
                    "unsigned long long at(int n) { return __builtin_offsetof(struct S, a[n]); }\n"
                    "void f(int n) { enum { E = __builtin_offsetof(struct S, a[n]) }; }\n";
  Checked c;
  check(c, src);
  EXPECT_EQ(c.diagnostics, error_at(src, "__builtin_offsetof(struct S, a[n]) }", 1,
                                    "enumerator value is not an integer constant expression 'E'"))
      << "a variable index is allowed, but the result is not a constant";
}

TEST(BuiltinTest, OffsetofMisuseIsReported) {
  const char *src = "struct S { int a[2]; int bf : 3; int *p; struct In { int x; } in; };\n"
                    "struct Inc;\n"
                    "int e1 = __builtin_offsetof(int, a);\n"
                    "int e2 = __builtin_offsetof(struct S, zz);\n"
                    "int e3 = __builtin_offsetof(struct S, bf);\n"
                    "int e4 = __builtin_offsetof(struct Inc, a);\n"
                    "int e5 = __builtin_offsetof(struct S, p[1]);\n"
                    "int e6 = __builtin_offsetof(struct S, a[1.0]);\n"
                    "int e7 = __builtin_offsetof(struct S, in.x.y);\n"
                    "enum { E = __builtin_offsetof(struct Missing, a) };\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  std::string subscript = "a subscript in offsetof needs an array and an integer";
  EXPECT_EQ(c.diagnostics,
            error_at(src, "__builtin_offsetof(int", 1,
                     "offsetof needs a structure or union to find member 'a'") +
                error_at(src, "__builtin_offsetof(struct S, zz", 1, "no member named 'zz'") +
                error_at(src, "__builtin_offsetof(struct S, bf", 1,
                         "offsetof cannot name bit-field 'bf'") +
                error_at(src, "__builtin_offsetof(struct Inc", 1,
                         "member access into an incomplete structure or union") +
                error_at(src, "__builtin_offsetof(struct S, p", 1, subscript) +
                error_at(src, "__builtin_offsetof(struct S, a[1.0", 1, subscript) +
                error_at(src, "__builtin_offsetof(struct S, in", 1,
                         "offsetof needs a structure or union to find member 'y'") +
                error_at(src, "__builtin_offsetof(struct Missing", 1,
                         "member access into an incomplete structure or union"))
      << "one error each: a failed offsetof is not also 'not constant'";
}

TEST(BuiltinTest, TypesCompatibleFollowsGcc) {
  expect_values("enum E { EA };\nstruct S { int x; };\ntypedef struct S T;\nint arr[3];\n",
                {
                    {"__builtin_types_compatible_p(int, const int)", 1},
                    {"__builtin_types_compatible_p(int, long)", 0},
                    {"__builtin_types_compatible_p(char *, const char *)", 0},
                    {"__builtin_types_compatible_p(int * const, int *)", 1},
                    {"__builtin_types_compatible_p(const int[3], int[3])", 1},
                    {"__builtin_types_compatible_p(const int[2][3], int[][3])", 1},
                    {"__builtin_types_compatible_p(int * const[3], int *[3])", 1},
                    {"__builtin_types_compatible_p(const int *[3], int *[3])", 0},
                    {"__builtin_types_compatible_p(int[3], int[4])", 0},
                    {"__builtin_types_compatible_p(enum E, unsigned int)", 1},
                    {"__builtin_types_compatible_p(enum E, int)", 0},
                    {"__builtin_types_compatible_p(int (*)(void), int (*)())", 1},
                    {"__builtin_types_compatible_p(struct S, T)", 1},
                    {"__builtin_types_compatible_p(__typeof__(1.0f), float)", 1},
                    {"__builtin_types_compatible_p(__typeof__(1.0f), double)", 0},
                    {"__builtin_types_compatible_p(__typeof__((char)1), char)", 1},
                    {"__builtin_types_compatible_p(__typeof__(arr), int[3])", 1},
                    {"__builtin_types_compatible_p(__typeof__(1.0iF), float _Complex)", 1},
                });
}

TEST(BuiltinTest, ChooseExprTakesTheChosenBranch) {
  expect_values("int g;\n", {
                                {"__builtin_choose_expr(1, 3, 4.5)", 3},
                                {"__builtin_choose_expr(0, 4.5, 7)", 7},
                                {"__builtin_choose_expr(1, 5, g)", 5},
                                {"sizeof(__builtin_choose_expr(0, 1, 2.0))", 8},
                                {"sizeof(__builtin_choose_expr(1, (char)1, 2.0))", 1},
                            });
  const char *src = "int a, b;\n"
                    "void f(int n) {\n"
                    "  __builtin_choose_expr(1, a, b) = 3;\n"
                    "  __builtin_choose_expr(1, 1, a) = 3;\n"
                    "  int x = __builtin_choose_expr(n, 1, 2);\n"
                    "  int y = __builtin_choose_expr(1.0, 1, 2);\n"
                    "  int z = __builtin_choose_expr(1, 1);\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  std::string not_constant =
      "the first argument of '__builtin_choose_expr' must be an integer constant expression";
  EXPECT_EQ(c.diagnostics, error_at(src, "= 3", 2, "not a modifiable lvalue on the left of '='") +
                               error_at(src, "__builtin_choose_expr(n", 1, not_constant) +
                               error_at(src, "__builtin_choose_expr(1.0", 1, not_constant) +
                               error_at(src, "__builtin_choose_expr(1, 1)", 1,
                                        "wrong number of arguments to '__builtin_choose_expr'"))
      << "the chosen branch decides lvalue-ness: a can be assigned, 1 cannot";
}

TEST(BuiltinTest, FloatingBuiltinsCheckTheirArguments) {
  const char *src = "void f(double d, int *p, double _Complex z, long double ld, const char *s) {\n"
                    "  int ok = __builtin_isgreater(1, d) + __builtin_signbitf(ld);\n"
                    "  long long fine = __builtin_llabs(1.5);\n"
                    "  float n = __builtin_nanf(s);\n"
                    "  int e1 = __builtin_isgreater(1, 2);\n"
                    "  int e2 = __builtin_isless(p, d);\n"
                    "  int e3 = __builtin_isunordered(z, d);\n"
                    "  int e4 = __builtin_signbit(1);\n"
                    "  long long e5 = __builtin_llabs(p);\n"
                    "  float e6 = __builtin_nanf(1);\n"
                    "  float e7 = __builtin_nanf(L\"\");\n"
                    "  double e8 = __builtin_huge_val(1);\n"
                    "  __builtin_trap(1);\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(
      c.diagnostics,
      error_at(src, "__builtin_isgreater(1, 2", 1,
               "non-floating-point arguments in a call to '__builtin_isgreater'") +
          error_at(src, "__builtin_isless", 1,
                   "non-floating-point arguments in a call to '__builtin_isless'") +
          error_at(src, "__builtin_isunordered", 1,
                   "non-floating-point arguments in a call to '__builtin_isunordered'") +
          error_at(src, "__builtin_signbit(", 1,
                   "non-floating-point arguments in a call to '__builtin_signbit'") +
          error_at(src, "p);", 1, "incompatible types in argument") +
          error_at(src, "1);\n  float e7", 1, "incompatible types in argument") +
          error_at(src, "L\"\"", 1, "incompatible types in argument") +
          error_at(src, "__builtin_huge_val(1", 1,
                   "wrong number of arguments to '__builtin_huge_val'") +
          error_at(src, "__builtin_trap(1", 1, "wrong number of arguments to '__builtin_trap'"));
}

TEST(BuiltinTest, ConstantBuiltinsCanInitializeStaticObjects) {
  const char *src = "struct S { int a[4]; };\n"
                    "double h = __builtin_huge_val();\n"
                    "float hf = __builtin_huge_valf();\n"
                    "long double hl = __builtin_huge_vall();\n"
                    "float i = __builtin_inff();\n"
                    "float n = __builtin_nanf(\"\");\n"
                    "unsigned long long o = __builtin_offsetof(struct S, a[2]);\n"
                    "int t = __builtin_types_compatible_p(int, int);\n"
                    "double c = __builtin_choose_expr(1, __builtin_huge_val(), 0);\n"
                    "float _Complex z = 1.0iF;\n"
                    "const char *str;\n"
                    "int k;\n"
                    "float bad1 = __builtin_nanf(str);\n"
                    "int bad2 = __builtin_isgreater(1.0, 2.0);\n"
                    "unsigned long long bad3 = __builtin_offsetof(struct S, a[k]);\n"
                    "double bad4 = __builtin_choose_expr(0, 1, k);\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  std::string message = "an initializer for an object with static storage must be constant";
  EXPECT_EQ(c.diagnostics, error_at(src, "__builtin_nanf(str", 1, message) +
                               error_at(src, "__builtin_isgreater", 1, message) +
                               error_at(src, "__builtin_offsetof(struct S, a[k", 1, message) +
                               error_at(src, "__builtin_choose_expr(0", 1, message));
}

TEST(BuiltinTest, AnImaginaryConstantHasNoRealPart) {
  expect_values("", {
                        {"(int)1.0i", 0},
                        {"(int)2.5", 2},
                        {"sizeof(1.0fi)", 8},
                        {"sizeof(1.0iL)", 32},
                    });
}

TEST(BuiltinTest, PrettyFunctionAndFunctionNameTheFunction) {
  const char *src = "void ff(void) {\n"
                    "  int y[sizeof(__PRETTY_FUNCTION__) == 3 ? 1 : -1];\n"
                    "  int z[sizeof(__FUNCTION__) == 3 ? 1 : -1];\n"
                    "  const char *p = __func__;\n"
                    "}\n"
                    "const char *outside = __PRETTY_FUNCTION__;\n";
  Checked c;
  check(c, src);
  EXPECT_EQ(c.diagnostics, error_at(src, "__PRETTY_FUNCTION__;", 1,
                                    "use of undeclared identifier '__PRETTY_FUNCTION__'"))
      << "like __func__, only inside a function";
}

TEST(BuiltinTest, ATypeofOperandIsNotAUse) {
  const char *src = "static int helper(void);\n"
                    "int t[__builtin_types_compatible_p(__typeof__(helper()), int) ? 1 : -1];\n";
  Checked c;
  check(c, src);
  EXPECT_EQ(c.errors, 0) << "a static function named only inside __typeof__ is never called\n"
                         << c.diagnostics;
}

static const char *tgmath_declarations =
    "float sf(float); double sd(double); long double sl(long double);\n"
    "float _Complex csf(float _Complex); double _Complex csd(double _Complex);\n"
    "long double _Complex csl(long double _Complex);\n"
    "float af(float _Complex); double ad(double _Complex); long double al(long double _Complex);\n"
    "int ef(float); int ed(double); int el(long double);\n"
    "float lf(float, int); double ld(double, int); long double ll(long double, int);\n"
    "float nf(float, long double); double nd(double, long double);\n"
    "long double nl(long double, long double);\n"
    "float pf(float, float); double pd(double, double); long double pl(long double, long double);\n"
    "float _Complex cpf(float _Complex, float _Complex);\n"
    "double _Complex cpd(double _Complex, double _Complex);\n"
    "long double _Complex cpl(long double _Complex, long double _Complex);\n"
    "float gf(int, float); double gd(int, double); long double gl(int, long double);\n"
    "float _Complex kf(float _Complex); double _Complex kd(double _Complex);\n"
    "long double _Complex kl(long double _Complex);\n"
    "const float qf(float); const double qd(double); const long double ql(long double);\n"
    "float vf; double vd; long double vl; int vi; char vc; float _Complex vcf;\n"
    "double _Complex vcd; long double _Complex vcl;\n"
    "#define SQRT(x) __builtin_tgmath(sf, sd, sl, csf, csd, csl, x)\n"
    "#define FABS(x) __builtin_tgmath(sf, sd, sl, af, ad, al, x)\n"
    "#define ILOGB(x) __builtin_tgmath(ef, ed, el, x)\n"
    "#define LDEXP(x, n) __builtin_tgmath(lf, ld, ll, x, n)\n"
    "#define NEXTTOWARD(x, y) __builtin_tgmath(nf, nd, nl, x, y)\n"
    "#define POW(x, y) __builtin_tgmath(pf, pd, pl, cpf, cpd, cpl, x, y)\n"
    "#define LATE(n, x) __builtin_tgmath(gf, gd, gl, n, x)\n"
    "#define CONJ(x) __builtin_tgmath(kf, kd, kl, x)\n"
    "#define QUAL(x) __builtin_tgmath(qf, qd, ql, x)\n";

TEST(TgmathTest, TheArgumentTypesPickTheFunction) {
  std::vector<TypeCase> cases = {
      {"", "SQRT(vf)", "float"},
      {"", "SQRT(vd)", "double"},
      {"", "SQRT(vl)", "long double"},
      {"", "SQRT(vi)", "double"},
      {"", "SQRT(vc)", "double"},
      {"", "SQRT(vcf)", "float _Complex"},
      {"", "SQRT(vcd)", "double _Complex"},
      {"", "SQRT(vcl)", "long double _Complex"},
      {"", "POW(vf, vf)", "float"},
      {"", "POW(vf, vd)", "double"},
      {"", "POW(vf, vi)", "double"},
      {"", "POW(vf, vl)", "long double"},
      {"", "POW(vl, vi)", "long double"},
      {"", "POW(vcf, vf)", "float _Complex"},
      {"", "POW(vf, vcd)", "double _Complex"},
      {"", "FABS(vf)", "float"},
      {"", "FABS(vcf)", "float"},
      {"", "FABS(vcl)", "long double"},
      {"", "ILOGB(vf)", "int"},
      {"", "ILOGB(vl)", "int"},
      {"", "LDEXP(vf, vi)", "float"},
      {"", "LDEXP(vf, vl)", "float"},
      {"", "LDEXP(vi, vi)", "double"},
      {"", "NEXTTOWARD(vf, vl)", "float"},
      {"", "NEXTTOWARD(vi, vl)", "double"},
      {"", "LATE(vl, vf)", "float"},
      {"", "LATE(vi, vd)", "double"},
      {"", "CONJ(vd)", "double _Complex"},
      {"", "CONJ(vf)", "float _Complex"},
      {"", "CONJ(vi)", "double _Complex"},
      {"", "CONJ(vcl)", "long double _Complex"},
      {"", "QUAL(vd)", "double"},
  };
  for (TypeCase &test : cases)
    test.declarations = tgmath_declarations;
  expect_types(cases, true);
}

TEST(TgmathTest, TheChosenFunctionIsRecorded) {
  std::string src = std::string(tgmath_declarations) + "void g(void) { double r = POW(vf, vi); }\n";
  Checked c;
  check_preprocessed(c, src.c_str());
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  std::vector<ast_node *> r = declarations(c, "r");
  ASSERT_EQ(r.size(), 1u);
  ast_node *call = r[0]->var_decl.init_value;
  ASSERT_EQ(call->type, AST_NODE_TYPE_BUILTIN);
  ASSERT_NE(call->builtin.chosen, nullptr);
  ASSERT_EQ(call->builtin.chosen->type, AST_NODE_TYPE_CONVERSION) << "the chosen function decays";
  EXPECT_EQ(call->builtin.chosen->conversion.kind, CONVERSION_FUNCTION_TO_POINTER);
  EXPECT_STREQ(call->builtin.chosen->conversion.operand->tok.value, "pd");
  EXPECT_EQ(call->builtin.value, 6) << "six of the eight arguments are functions";
}

TEST(TgmathTest, MisuseIsReported) {
  std::string src =
      std::string(tgmath_declarations) +
      "int old(); int va(double, ...); float rf(float, int *); double rd(double, int *);\n"
      "long double rl(long double, int *); int *vp;\n"
      "void f(void) {\n"
      "  __builtin_tgmath(sf, sd, sl, vcf);\n"
      "  __builtin_tgmath(sf, sd, sl, vp);\n"
      "  __builtin_tgmath(old, sd, 1.0);\n"
      "  __builtin_tgmath(sd, va, 1.0);\n"
      "  __builtin_tgmath(sf, pd, 1.0);\n"
      "  __builtin_tgmath(sd, sd, 1.0);\n"
      "  __builtin_tgmath(sd, 1.0);\n"
      "  __builtin_tgmath(rf, rd, rl, vf, vd);\n"
      "  __builtin_tgmath(vd, sd, 1.0);\n"
      "}\n";
  const char *s = src.c_str();
  Checked c;
  check_preprocessed(c, s);
  ASSERT_EQ(c.parse_errors, 0);
  std::string prototype =
      "each function given to '__builtin_tgmath' must have a prototype without '...'";
  EXPECT_EQ(
      c.diagnostics,
      error_at(s, "__builtin_tgmath(sf, sd, sl, vcf)", 1,
               "no matching function for type-generic call") +
          error_at(s, "vp);", 1, "an argument of a type-generic call must have arithmetic type") +
          error_at(s, "old, sd", 1, prototype) + error_at(s, "va, 1.0", 1, prototype) +
          error_at(s, "pd, 1.0", 1,
                   "the functions given to '__builtin_tgmath' must take the same number of "
                   "parameters") +
          error_at(s, "__builtin_tgmath(sd, sd", 1,
                   "the functions given to '__builtin_tgmath' must differ in type") +
          error_at(s, "__builtin_tgmath(sd, 1.0)", 1,
                   "wrong number of arguments to '__builtin_tgmath'") +
          error_at(s, "vd);\n  __builtin_tgmath(vd", 1, "incompatible types in argument") +
          error_at(s, "vd, sd", 1, prototype));
}

static std::string shape(ast_node *node);

static std::string shape_list(ast_node **nodes, int count) {
  std::string out;
  for (int i = 0; i < count; i++)
    out += (i > 0 ? ", " : "") + shape(nodes[i]);
  return out;
}

static std::string shape(ast_node *node) {
  if (node == nullptr)
    return "";
  switch (node->type) {
  case AST_NODE_TYPE_CONVERSION: {
    std::string operand = shape(node->conversion.operand);
    switch (node->conversion.kind) {
    case CONVERSION_LVALUE:
      return "load(" + operand + ")";
    case CONVERSION_ARRAY_TO_POINTER:
      return "decay(" + operand + ")";
    case CONVERSION_FUNCTION_TO_POINTER:
      return "address(" + operand + ")";
    default:
      return "(" + describe(node->expr_type) + ")" + operand;
    }
  }
  case AST_NODE_TYPE_IDENTIFIER:
  case AST_NODE_TYPE_NUMBER:
    return node->tok.value;
  case AST_NODE_TYPE_STRING:
    return "\"" +
           (node->literal.bytes != nullptr ? std::string(node->literal.bytes, node->literal.length)
                                           : std::string()) +
           "\"";
  case AST_NODE_TYPE_UNARY_OP: {
    std::string op = node->unary_op.op.value;
    std::string operand = shape(node->unary_op.operand);
    if (node->unary_op.is_postfix)
      return operand + op;
    return op == "sizeof" ? "sizeof " + operand : op + operand;
  }
  case AST_NODE_TYPE_BINARY_OP:
    return "(" + shape(node->binary_op.left) + " " + node->binary_op.op.value + " " +
           shape(node->binary_op.right) + ")";
  case AST_NODE_TYPE_ASSIGNMENT:
    return "(" + shape(node->assignment.left) + " " + node->assignment.op.value + " " +
           shape(node->assignment.right) + ")";
  case AST_NODE_TYPE_TERNARY:
    return "(" + shape(node->ternary.condition) + " ? " + shape(node->ternary.true_branch) + " : " +
           shape(node->ternary.false_branch) + ")";
  case AST_NODE_TYPE_FUNCTION_CALL:
    return shape(node->function_call.callable) + "(" +
           shape_list(node->function_call.arguments, node->function_call.arg_count) + ")";
  case AST_NODE_TYPE_ARRAY_SUBSCRIPT:
    return shape(node->array_subscript.left) + "[" + shape(node->array_subscript.index) + "]";
  case AST_NODE_TYPE_MEMBER_ACCESS:
    return shape(node->member_access.left) + (node->member_access.is_pointer ? "->" : ".") +
           node->member_access.member_name;
  case AST_NODE_TYPE_CAST:
    return "cast<" + describe(node->cast_expr.type) + ">(" + shape(node->cast_expr.operand) + ")";
  case AST_NODE_TYPE_BUILTIN: {
    std::string out = std::string(node->tok.value) + "(" +
                      shape_list(node->builtin.args, node->builtin.arg_count);
    for (int i = 0; i < node->builtin.step_count; i++) {
      if (node->builtin.steps[i].index != nullptr)
        out += "[" + shape(node->builtin.steps[i].index) + "]";
    }
    return out + ")";
  }
  case AST_NODE_TYPE_COMPOUND_LITERAL:
    return "literal";
  case AST_NODE_TYPE_INIT_LIST: {
    std::string out = "{";
    for (int i = 0; i < node->init_list.count; i++)
      out += (i > 0 ? ", " : "") + shape(node->init_list.items[i].value);
    return out + "}";
  }
  default:
    return "?";
  }
}

static void add_statement_shape(ast_node *node, std::vector<std::string> &out) {
  if (node == nullptr)
    return;
  switch (node->type) {
  case AST_NODE_TYPE_DECL_GROUP:
    for (int i = 0; i < node->block.count; i++)
      add_statement_shape(node->block.statements[i], out);
    break;
  case AST_NODE_TYPE_VAR_DECL:
    if (node->var_decl.init_value != nullptr)
      out.push_back(std::string(node->var_decl.var_name) + " = " +
                    shape(node->var_decl.init_value));
    break;
  case AST_NODE_TYPE_RETURN:
    out.push_back("return " + shape(node->return_stmt.return_value));
    break;
  case AST_NODE_TYPE_IF:
    out.push_back("if " + shape(node->if_stmt.condition));
    break;
  case AST_NODE_TYPE_WHILE:
    out.push_back("while " + shape(node->while_stmt.condition));
    break;
  case AST_NODE_TYPE_DO_WHILE:
    out.push_back("do " + shape(node->do_while_stmt.condition));
    break;
  case AST_NODE_TYPE_FOR:
    out.push_back("for " + shape(node->for_stmt.init) + "; " + shape(node->for_stmt.condition) +
                  "; " + shape(node->for_stmt.increment));
    break;
  case AST_NODE_TYPE_SWITCH:
    out.push_back("switch " + shape(node->switch_stmt.condition));
    break;
  default:
    out.push_back(shape(node));
    break;
  }
}

static std::vector<std::string> body_shapes(const Checked &c, const char *function) {
  std::vector<ast_node *> definition = declarations(c, function);
  if (definition.size() != 1 || definition[0]->type != AST_NODE_TYPE_FUNCTION_DEF)
    return {"<no definition>"};
  std::vector<std::string> out;
  ast_node *body = definition[0]->function_def.body;
  for (int i = 0; i < body->block.count; i++)
    add_statement_shape(body->block.statements[i], out);
  return out;
}

static void expect_shapes(const std::string &decls, const std::string &body,
                          const std::vector<std::string> &expected) {
  std::string source = decls + "void probe(void) {\n" + body + "}\n";
  SCOPED_TRACE(source);
  Checked c;
  check(c, source.c_str());
  ASSERT_EQ(c.parse_errors, 0);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  EXPECT_EQ(body_shapes(c, "probe"), expected);
}

static std::string init_shape(const Checked &c, const char *name) {
  std::vector<ast_node *> decl = declarations(c, name);
  if (decl.size() != 1 || decl[0]->type != AST_NODE_TYPE_VAR_DECL)
    return "<no declaration>";
  return shape(decl[0]->var_decl.init_value);
}

TEST(ConversionTest, ArithmeticOperandsMeetInTheTypeTheOperatorWorksIn) {
  expect_shapes("int i; unsigned u; char c; unsigned short us; long double ld;\n"
                "double _Complex z; float fl;\n",
                "i < u;\n"
                "c + us;\n"
                "i + i;\n"
                "ld * c;\n"
                "z + fl;\n"
                "u << c;\n"
                "c >> 1u;\n"
                "-c;\n"
                "~us;\n"
                "!fl;\n"
                "i && fl;\n"
                "c || u;\n"
                "c ? fl : ld;\n",
                {
                    "((unsigned int)load(i) < load(u))",
                    "((int)load(c) + (int)load(us))",
                    "(load(i) + load(i))",
                    "(load(ld) * (long double)load(c))",
                    "(load(z) + (double _Complex)load(fl))",
                    "(load(u) << (int)load(c))",
                    "((int)load(c) >> 1u)",
                    "-(int)load(c)",
                    "~(int)load(us)",
                    "!(_Bool)load(fl)",
                    "((_Bool)load(i) && (_Bool)load(fl))",
                    "((_Bool)load(c) || (_Bool)load(u))",
                    "((_Bool)load(c) ? (long double)load(fl) : load(ld))",
                });
}

TEST(ConversionTest, AnLvalueIsReadOnlyWhereItsValueIsUsed) {
  expect_shapes("int x; const int ci = 1; struct S { int m; } s, *ps; int *p;\n",
                "&x;\n"
                "sizeof x;\n"
                "x++;\n"
                "--x;\n"
                "x = ci;\n"
                "x += ci;\n"
                "s.m;\n"
                "ps->m;\n"
                "*p;\n"
                "(long)x;\n",
                {
                    "&x",
                    "sizeof x",
                    "x++",
                    "--x",
                    "(x = load(ci))",
                    "(x += load(ci))",
                    "s.m",
                    "load(ps)->m",
                    "*load(p)",
                    "cast<long>(load(x))",
                });
}

TEST(ConversionTest, ThrownAwayValuesAreNotConverted) {
  expect_shapes("int x, y; volatile int v;\n",
                "x;\n"
                "v;\n"
                "(void)x;\n"
                "x, y;\n"
                "for (x; 0; v) ;\n",
                {
                    "x",
                    "v",
                    "cast<void>(x)",
                    "(x , load(y))",
                    "for x; (_Bool)0; v",
                });
}

TEST(ConversionTest, ALoadedValueLosesItsQualifiers) {
  const char *src = "const volatile int cv; int r;\n"
                    "void probe(void) { r = cv; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  std::vector<ast_node *> loads = nodes_of(c, AST_NODE_TYPE_CONVERSION);
  ASSERT_EQ(loads.size(), 1u);
  EXPECT_EQ(loads[0]->conversion.kind, CONVERSION_LVALUE);
  EXPECT_EQ(describe(loads[0]->conversion.operand->expr_type), "const volatile int");
  EXPECT_EQ(describe(loads[0]->expr_type), "int");
  EXPECT_EQ(loads[0]->is_lvalue, 0);
  EXPECT_EQ(loads[0]->loc.line, loads[0]->conversion.operand->loc.line);
  EXPECT_EQ(loads[0]->loc.column, loads[0]->conversion.operand->loc.column);
}

TEST(ConversionTest, AnIncompleteObjectIsNeverRead) {
  expect_shapes("struct In; struct In *ip;\n",
                "(0, *ip);\n"
                "0 ? *ip : *ip;\n",
                {
                    "(0 , *load(ip))",
                    "((_Bool)0 ? *load(ip) : *load(ip))",
                });
}

TEST(ConversionTest, ArraysAndFunctionsDecay) {
  expect_shapes("int a[3]; int f(int); int (*fp)(int); char *q;\n",
                "f(1);\n"
                "fp(2);\n"
                "a[1];\n"
                "q = \"hi\";\n"
                "sizeof a;\n"
                "&a;\n"
                "fp = f;\n"
                "char t[] = \"hi\";\n",
                {
                    "address(f)(1)",
                    "load(fp)(2)",
                    "decay(a)[(long long)1]",
                    "(q = decay(\"hi\"))",
                    "sizeof a",
                    "&a",
                    "(fp = address(f))",
                    "t = \"hi\"",
                });
}

TEST(ConversionTest, PointerOperandsFollowTheirOwnRules) {
  expect_shapes("int *p, *q; void *vp; char c; unsigned u;\n",
                "p + c;\n"
                "u + p;\n"
                "p - u;\n"
                "p - q;\n"
                "p < q;\n"
                "c[p];\n"
                "p == 0;\n"
                "0 != p;\n"
                "p == vp;\n"
                "vp != q;\n"
                "p == (void *)0;\n"
                "p && vp;\n"
                "c ? p : 0;\n"
                "c ? vp : p;\n",
                {
                    "(load(p) + (long long)load(c))",
                    "((long long)load(u) + load(p))",
                    "(load(p) - (long long)load(u))",
                    "(load(p) - load(q))",
                    "(load(p) < load(q))",
                    "(long long)load(c)[load(p)]",
                    "(load(p) == (int *)0)",
                    "((int *)0 != load(p))",
                    "((void *)load(p) == load(vp))",
                    "(load(vp) != (void *)load(q))",
                    "(load(p) == (int *)cast<void *>(0))",
                    "((_Bool)load(p) && (_Bool)load(vp))",
                    "((_Bool)load(c) ? load(p) : (int *)0)",
                    "((_Bool)load(c) ? load(vp) : (void *)load(p))",
                });
}

TEST(ConversionTest, ArgumentsConvertToTheirParameterOrArePromoted) {
  expect_shapes("int f(long, const char *); int old(); int v(int, ...);\n"
                "void k(int a[], int g(void)); int h(void); int arr[2];\n"
                "char c; float fl; long double ld; double _Complex z; float _Complex zf;\n"
                "struct B { unsigned u3 : 3; unsigned u32 : 32; } s;\n",
                "f(c, \"x\");\n"
                "old(c, fl, ld);\n"
                "v(c, c, fl, z, zf, s.u3, s.u32);\n"
                "k(arr, h);\n",
                {
                    "address(f)((long)load(c), (const char *)decay(\"x\"))",
                    "address(old)((int)load(c), (double)load(fl), load(ld))",
                    "address(v)((int)load(c), (int)load(c), (double)load(fl), load(z), load(zf), "
                    "(int)load(s.u3), load(s.u32))",
                    "address(k)(decay(arr), address(h))",
                });
}

TEST(ConversionTest, StatementsConvertTheValuesTheyUse) {
  std::string decls = "int i; float fl; char c; double _Complex z; int *p;\n"
                      "struct B { unsigned u3 : 3; } s;\n";
  expect_shapes(decls,
                "if (p) ;\n"
                "while (fl) break;\n"
                "do ; while (z);\n"
                "for (i = 0; i < 3; i++) ;\n"
                "switch (c) { default: ; }\n"
                "switch (s.u3) { default: ; }\n"
                "switch (i) { default: ; }\n"
                "int d = c;\n"
                "double e[2] = { i, c };\n"
                "struct B t = s;\n",
                {
                    "if (_Bool)load(p)",
                    "while (_Bool)load(fl)",
                    "do (_Bool)load(z)",
                    "for (i = 0); (_Bool)(load(i) < 3); i++",
                    "switch (int)load(c)",
                    "switch (int)load(s.u3)",
                    "switch load(i)",
                    "d = (int)load(c)",
                    "e = {(double)load(i), (double)load(c)}",
                    "t = load(s)",
                });

  std::string returns = decls + "char r(void) { return i; }\nint *n(void) { return 0; }\n";
  Checked c;
  check(c, returns.c_str());
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  EXPECT_EQ(body_shapes(c, "r"), std::vector<std::string>{"return (char)load(i)"});
  EXPECT_EQ(body_shapes(c, "n"), std::vector<std::string>{"return (int *)0"});
}

TEST(ConversionTest, CompoundAssignmentAndIncrementRecordTheirComputationType) {
  const char *src = "unsigned char uc; _Bool b; int *p; char c; unsigned short us;\n"
                    "long double ld; float fl; double _Complex z; long long q;\n"
                    "struct B { unsigned u3 : 3; _Bool bb : 1; } s;\n"
                    "void probe(void) {\n"
                    "  uc += 10;\n"
                    "  uc <<= us;\n"
                    "  q <<= c;\n"
                    "  p += c;\n"
                    "  p -= us;\n"
                    "  ld *= c;\n"
                    "  z += fl;\n"
                    "  s.u3 /= -2;\n"
                    "  b++;\n"
                    "  --p;\n"
                    "  fl++;\n"
                    "  uc = c;\n"
                    "  s.bb = 0.5;\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  std::vector<ast_node *> definition = declarations(c, "probe");
  ASSERT_EQ(definition.size(), 1u);
  ast_node *body = definition[0]->function_def.body;
  std::vector<std::string> seen;
  for (int i = 0; i < body->block.count; i++) {
    ast_node *statement = body->block.statements[i];
    type_info *computation = statement->type == AST_NODE_TYPE_ASSIGNMENT
                                 ? statement->assignment.computation_type
                                 : statement->unary_op.computation_type;
    seen.push_back(shape(statement) + " in " + describe(computation));
  }
  std::vector<std::string> expected = {
      "(uc += 10) in int",
      "(uc <<= (int)load(us)) in int",
      "(q <<= (int)load(c)) in long long",
      "(p += (long long)load(c)) in int *",
      "(p -= (long long)load(us)) in int *",
      "(ld *= (long double)load(c)) in long double",
      "(z += (double _Complex)load(fl)) in double _Complex",
      "(s.u3 /= -2) in int",
      "b++ in int",
      "--p in int *",
      "fl++ in float",
      "(uc = (unsigned char)load(c)) in <none>",
      "(s.bb = (_Bool)0.5) in <none>",
  };
  EXPECT_EQ(seen, expected);
}

TEST(ConversionTest, ABitFieldNarrowerThanIntPromotesToInt) {
  const char *s = "struct B { unsigned u3 : 3; unsigned u31 : 31; unsigned u32 : 32; int i5 : 5;\n"
                  "  _Bool b : 1; unsigned long long q3 : 3; unsigned long long q40 : 40; } s;\n";
  expect_types({
      {s, "s.u3 - 4", "int"},
      {s, "s.u31 + 0", "int"},
      {s, "s.u32 - 4", "unsigned int"},
      {s, "s.i5 * 2", "int"},
      {s, "s.b + 0", "int"},
      {s, "s.q3 + 0", "int"},
      {s, "s.q40 + 0", "unsigned long long"},
      {s, "-s.u3", "int"},
      {s, "~s.u32", "unsigned int"},
      {s, "s.u3 << 1", "int"},
      {s, "1 ? s.u3 : s.u3", "int"},
      {s, "s.u3 < 0u", "int"},
      {s, "s.u3", "unsigned int"},
      {s, "s.u3 = 9", "unsigned int"},
      {s, "s.u3 += 1", "unsigned int"},
      {s, "s.u3++", "unsigned int"},
  });
}

TEST(ConversionTest, AVaListIsUsedWhereTheBuiltinNeedsIt) {
  const char *src = "void f(int n, ...) {\n"
                    "  __builtin_va_list ap, aq;\n"
                    "  __builtin_va_start(ap, n);\n"
                    "  __builtin_va_arg(ap, int);\n"
                    "  __builtin_va_copy(aq, ap);\n"
                    "  __builtin_va_end(aq);\n"
                    "}\n"
                    "void g(__builtin_va_list ap) { __builtin_va_arg(ap, int); }\n";
  {
    SCOPED_TRACE("windows: va_list is a char * the builtins write into");
    TargetGuard guard(TARGET_WINDOWS_X64);
    Checked c;
    check(c, src);
    ASSERT_EQ(c.errors, 0) << c.diagnostics;
    EXPECT_EQ(body_shapes(c, "f"), (std::vector<std::string>{
                                       "__builtin_va_start(ap, n)", "__builtin_va_arg(ap)",
                                       "__builtin_va_copy(aq, load(ap))", "__builtin_va_end(aq)"}));
    EXPECT_EQ(body_shapes(c, "g"), std::vector<std::string>{"__builtin_va_arg(ap)"});
  }
  {
    SCOPED_TRACE("linux: va_list is an array, so every use is a pointer to it");
    TargetGuard guard(TARGET_LINUX_X64);
    Checked c;
    check(c, src);
    ASSERT_EQ(c.errors, 0) << c.diagnostics;
    EXPECT_EQ(body_shapes(c, "f"),
              (std::vector<std::string>{
                  "__builtin_va_start(decay(ap), n)", "__builtin_va_arg(decay(ap))",
                  "__builtin_va_copy(decay(aq), decay(ap))", "__builtin_va_end(decay(aq))"}));
    EXPECT_EQ(body_shapes(c, "g"), std::vector<std::string>{"__builtin_va_arg(load(ap))"});
  }
}

TEST(ConversionTest, BuiltinArgumentsConvertToWhatEachBuiltinTakes) {
  expect_shapes("float fl; int i; long double ld; char c; struct S { int a[4]; };\n",
                "__builtin_isgreater(fl, i);\n"
                "__builtin_isunordered(ld, fl);\n"
                "__builtin_signbit(fl);\n"
                "__builtin_signbitf(ld);\n"
                "__builtin_signbitl(fl);\n"
                "__builtin_llabs(c);\n"
                "__builtin_nanf(\"\");\n"
                "__builtin_offsetof(struct S, a[i]);\n",
                {
                    "__builtin_isgreater(load(fl), (float)load(i))",
                    "__builtin_isunordered(load(ld), (long double)load(fl))",
                    "__builtin_signbit(load(fl))",
                    "__builtin_signbitf((float)load(ld))",
                    "__builtin_signbitl((long double)load(fl))",
                    "__builtin_llabs((long long)load(c))",
                    "__builtin_nanf((const char *)decay(\"\"))",
                    "__builtin_offsetof([(long long)load(i)])",
                });
}

TEST(ConversionTest, ConstantRulesStillHoldThroughConversions) {
  const char *src =
      "int x;\n"
      "static _Bool b = &x;\n"
      "static char *p = \"abc\" + 1;\n"
      "static float n = __builtin_nanf(\"\");\n"
      "static int *np = 0;\n"
      "enum { A = -1 < 0u, B = !5, C = (unsigned char)250 + 10, D = (int)(1 ? -1 : 0u) };\n"
      "int arr[sizeof(int) * 2];\n"
      "void f(int v) { switch (v) { case 'a' + 1: case (char)300: case 2u: ; } }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0) << c.diagnostics;
  EXPECT_EQ(init_shape(c, "b"), "(_Bool)&x");
  EXPECT_EQ(init_shape(c, "p"), "(decay(\"abc\") + (long long)1)");
  EXPECT_EQ(init_shape(c, "n"), "__builtin_nanf((const char *)decay(\"\"))");
  EXPECT_EQ(init_shape(c, "np"), "(int *)0");
  const char *names[] = {"A", "B", "C", "D"};
  long long values[] = {0, 0, 260, -1};
  for (int i = 0; i < 4; i++) {
    symbol *sym = find_symbol(c, names[i], SYMBOL_ENUM_CONSTANT);
    ASSERT_NE(sym, nullptr) << names[i];
    EXPECT_EQ(sym->value, values[i]) << names[i];
  }
  type_info *arr = type_of(c, "arr");
  ASSERT_NE(arr, nullptr);
  EXPECT_EQ(arr->array_size, 8);
}

TEST(ConversionTest, ATypeGenericCallConvertsItsArgumentsForTheChosenFunction) {
  std::string src =
      std::string(tgmath_declarations) + "void g(void) { POW(vf, vi); LDEXP(vf, vc); }\n";
  Checked c;
  check_preprocessed(c, src.c_str());
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  EXPECT_EQ(body_shapes(c, "g"),
            (std::vector<std::string>{
                "__builtin_tgmath(pf, address(pd), pl, cpf, cpd, cpl, (double)load(vf), "
                "(double)load(vi))",
                "__builtin_tgmath(address(lf), ld, ll, load(vf), (int)load(vc))"}));
}

static const char *every_operator_source =
    "struct S { unsigned u3 : 3; _Bool b : 1; int i; int arr[4]; } s, *ps = &s;\n"
    "typedef __builtin_va_list va;\n"
    "int f(int, double); int old(); int v(const char *, ...);\n"
    "float fl; char c; unsigned short us; int i, *p, a[10]; unsigned u; long double ld;\n"
    "double _Complex z; const int ci = 3; volatile int vi; void *vp;\n"
    "static int *sp = 0; static char *str = \"abc\" + 1; static int *ap = &a[2];\n"
    "static _Bool sb = &i; static int si = 1.5; static float sf = 2;\n"
    "static void (*fp)(void) = 0;\n"
    "enum E { E0, E1 } e;\n"
    "int g(va list, ...) {\n"
    "  va mine;\n"
    "  __builtin_va_start(mine, list);\n"
    "  int x = __builtin_va_arg(mine, int);\n"
    "  __builtin_va_copy(mine, list);\n"
    "  __builtin_va_end(mine);\n"
    "  return x + __builtin_va_arg(list, int);\n"
    "}\n"
    "long double h(void) {\n"
    "  int r, *rp; void *rv;\n"
    "  r = c + us; r = s.u3 - 4 < 0; r = -s.u3; r = i < u; r = !p && fl;\n"
    "  r = c ? fl : ld; rp = i ? p : 0; rv = i ? vp : p;\n"
    "  r = p == 0; r = 0 == p; r = p == vp; r = p[c]; r = c[p]; r = *(p + c); r = p - p;\n"
    "  r = f(c, i); r = old(c, fl, s); r = v(\"x\", c, fl, ld, z, s.u3);\n"
    "  c += 10; c <<= us; p += c; p -= us; ld *= c; z += fl; s.u3 += 1; s.b = 0.5;\n"
    "  c++; --p; s.b--; fl++; e = E1; r = e + 1; r = (long)ci + ci;\n"
    "  r = sizeof a + sizeof(a + 0); (void)vi; vi; i, vi;\n"
    "  r = ps->i + ps->arr[1] + s.arr[2];\n"
    "  r = __builtin_isgreater(fl, i) + __builtin_signbitf(ld) + __builtin_signbit(fl);\n"
    "  r = (int)__builtin_llabs(c);\n"
    "  switch (c) { case 1: break; }\n"
    "  switch (s.u3) { default: break; }\n"
    "  if (p) r = 1;\n"
    "  while (fl) break;\n"
    "  do ; while (z);\n"
    "  for (; c;) break;\n"
    "  for (r = 0; r < 3; r++) ;\n"
    "  struct S t = s;\n"
    "  int init[3] = { c, fl, 2.5 };\n"
    "  char text[] = \"hi\";\n"
    "  const char *q = text;\n"
    "  int *cl = (int[]){ c, 2 };\n"
    "  r = __builtin_offsetof(struct S, arr[i]);\n"
    "  double vla[c + 1][r];\n"
    "  return c;\n"
    "}\n";

TEST(ConversionTest, EveryOperatorsOperandsHaveTheTypeTheOperatorWorksIn) {
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    SCOPED_TRACE(kind == TARGET_LINUX_X64 ? "linux" : "windows");
    Checked c;
    check(c, every_operator_source);
    ASSERT_EQ(c.parse_errors, 0);
    ASSERT_EQ(c.errors, 0) << c.diagnostics;
    ConversionWalk walk = walk_conversions(c.program);
    EXPECT_EQ(walk.problems, std::vector<std::string>{});
    EXPECT_GT(walk.kinds[CONVERSION_LVALUE], 0);
    EXPECT_GT(walk.kinds[CONVERSION_ARRAY_TO_POINTER], 0);
    EXPECT_GT(walk.kinds[CONVERSION_FUNCTION_TO_POINTER], 0);
    EXPECT_GT(walk.kinds[CONVERSION_VALUE], 0);
  }
}

static std::vector<std::string> layout_text(const Checked &c, const initializer_layout &layout) {
  std::vector<std::string> out;
  for (int i = 0; i < layout.count; i++) {
    const initializer_entry &entry = layout.entries[i];
    if (std::find(c.nodes.begin(), c.nodes.end(), entry.value) == c.nodes.end()) {
      out.push_back("<an entry whose value is not in the tree>");
      continue;
    }
    std::string where = std::to_string(entry.offset);
    if (entry.bit_width != 0)
      where += "." + std::to_string(entry.bit_offset) + ":" + std::to_string(entry.bit_width);
    out.push_back(where + " " + describe(entry.type) + " = " + shape(entry.value));
  }
  return out;
}

static std::vector<std::string> layout_of(const Checked &c, const char *name) {
  std::vector<ast_node *> decl = declarations(c, name);
  if (decl.size() != 1 || decl[0]->type != AST_NODE_TYPE_VAR_DECL)
    return {"<no declaration>"};
  return layout_text(c, decl[0]->var_decl.init_layout);
}

static const char *layout_records = "struct P { int x, y; };\n"
                                    "struct O { struct P p; int z; };\n"
                                    "union U { int i; unsigned char c[4]; };\n"
                                    "struct W { union U u; int tail; };\n"
                                    "struct B { unsigned a : 4, b : 4; int c; };\n"
                                    "struct V { union U u; };\n"
                                    "struct L { int : 32; int a; };\n"
                                    "struct Q { struct L l; int z; };\n"
                                    "struct P q = {7, 8};\n";

TEST(InitializerLayoutTest, EachValueIsPlacedWhereTheObjectStoresIt) {
  std::string src = std::string(layout_records) +
                    "struct O e1 = {1, 2, 3};\n"
                    "int e2[2][3] = {1, 2, 3, 4};\n"
                    "struct { char c; long long d; short e[3]; } e5 = {97, 1, {1, 2, 3}};\n"
                    "int t3[2][2] = {[0][1] = 5, [1][0] = 6};\n"
                    "int e8 = {42};\n"
                    "char e4[] = \"hey\";\n"
                    "char e3[2][4] = {\"ab\", \"cd\"};\n"
                    "void f(void) { struct P r = q; int *cl = (int[]){7, 8}; }\n";
  Checked c;
  check(c, src.c_str());
  ASSERT_EQ(c.parse_errors, 0);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  EXPECT_EQ(layout_of(c, "q"), (std::vector<std::string>{"0 int = 7", "4 int = 8"}));
  EXPECT_EQ(layout_of(c, "e1"), (std::vector<std::string>{"0 int = 1", "4 int = 2", "8 int = 3"}));
  EXPECT_EQ(layout_of(c, "e2"),
            (std::vector<std::string>{"0 int = 1", "4 int = 2", "8 int = 3", "12 int = 4"}));
  EXPECT_EQ(layout_of(c, "e5"),
            (std::vector<std::string>{"0 char = (char)97", "8 long long = (long long)1",
                                      "16 short = (short)1", "18 short = (short)2",
                                      "20 short = (short)3"}));
  EXPECT_EQ(layout_of(c, "t3"), (std::vector<std::string>{"4 int = 5", "8 int = 6"}));
  EXPECT_EQ(layout_of(c, "e8"), std::vector<std::string>{"0 int = 42"});
  EXPECT_EQ(layout_of(c, "e4"), std::vector<std::string>{"0 char[4] = \"hey\""});
  EXPECT_EQ(layout_of(c, "e3"),
            (std::vector<std::string>{"0 char[4] = \"ab\"", "4 char[4] = \"cd\""}));
  EXPECT_EQ(layout_of(c, "r"), std::vector<std::string>{"0 P = load(q)"});
  EXPECT_EQ(layout_of(c, "cl"), std::vector<std::string>{"0 int * = decay(literal)"});
  std::vector<ast_node *> literals = nodes_of(c, AST_NODE_TYPE_COMPOUND_LITERAL);
  ASSERT_EQ(literals.size(), 1u);
  EXPECT_EQ(layout_text(c, literals[0]->compound_literal.init_layout),
            (std::vector<std::string>{"0 int = 7", "4 int = 8"}));
}

TEST(InitializerLayoutTest, AValueReplacesEveryEarlierValueItOverlaps) {
  std::string src = std::string(layout_records) +
                    "union U s2 = {.i = 0x11223344, .c[0] = 1};\n"
                    "union U s3 = {.c[1] = 9, .i = 0x55667788};\n"
                    "int s4[3] = {[0] = 1, [1] = 2, [0] = 3};\n"
                    "struct B s7 = {.b = 3, .a = 2, .b = 1};\n"
                    "struct B s8 = {.b = 3, .a = 2};\n"
                    "struct O s1 = {.p = {7, 8}, .p.y = 5};\n"
                    "struct W s6 = {.u.i = 0x11223344, .u.c[2] = 0xAA, .tail = 1};\n"
                    "void f(void) {\n"
                    "  struct P lq = q;\n"
                    "  struct O a1 = {.p = lq, .p.y = 5};\n"
                    "  struct O a9 = {lq, 3, .p.x = 9};\n"
                    "}\n";
  Checked c;
  check(c, src.c_str());
  ASSERT_EQ(c.parse_errors, 0);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  EXPECT_EQ(layout_of(c, "s2"), std::vector<std::string>{"0 unsigned char = (unsigned char)1"});
  EXPECT_EQ(layout_of(c, "s3"), std::vector<std::string>{"0 int = 0x55667788"});
  EXPECT_EQ(layout_of(c, "s4"), (std::vector<std::string>{"4 int = 2", "0 int = 3"}));
  EXPECT_EQ(layout_of(c, "s7"), (std::vector<std::string>{"0.0:4 unsigned int = (unsigned int)2",
                                                          "0.4:4 unsigned int = (unsigned int)1"}))
      << "two bit-fields in one byte do not overlap";
  EXPECT_EQ(layout_of(c, "s8"), (std::vector<std::string>{"0.4:4 unsigned int = (unsigned int)3",
                                                          "0.0:4 unsigned int = (unsigned int)2"}));
  EXPECT_EQ(layout_of(c, "s1"), (std::vector<std::string>{"0 int = 7", "4 int = 5"}));
  EXPECT_EQ(layout_of(c, "s6"),
            (std::vector<std::string>{"2 unsigned char = (unsigned char)0xAA", "4 int = 1"}));
  EXPECT_EQ(layout_of(c, "a1"), std::vector<std::string>{"4 int = 5"})
      << "a member replaces the whole struct value it is part of";
  EXPECT_EQ(layout_of(c, "a9"), (std::vector<std::string>{"8 int = 3", "0 int = 9"}));
}

TEST(InitializerLayoutTest, ABraceListReinitializesTheWholePartItCovers) {
  std::string src = std::string(layout_records) + "struct O t1 = {.p.y = 5, .p = {7}};\n"
                                                  "struct V t2 = {.u.c[1] = 9, .u = {0x01}};\n"
                                                  "int t3[2][2] = {[0][1] = 5, [0] = {7}};\n"
                                                  "struct O s5 = {.p.y = 5, .p = {7, 8}};\n"
                                                  "struct O t4 = {.z = 3, .p = {7}};\n"
                                                  "struct Q t5 = {.z = 3, .l = {7}};\n"
                                                  "void f(void) {\n"
                                                  "  struct P lq = q;\n"
                                                  "  struct O a5 = {.p.y = 5, .p = lq};\n"
                                                  "}\n";
  Checked c;
  check(c, src.c_str());
  ASSERT_EQ(c.parse_errors, 0);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  EXPECT_EQ(layout_of(c, "t1"), std::vector<std::string>{"0 int = 7"});
  EXPECT_EQ(layout_of(c, "t2"), std::vector<std::string>{"0 int = 0x01"});
  EXPECT_EQ(layout_of(c, "t3"), std::vector<std::string>{"0 int = 7"});
  EXPECT_EQ(layout_of(c, "s5"), (std::vector<std::string>{"0 int = 7", "4 int = 8"}));
  EXPECT_EQ(layout_of(c, "t4"), (std::vector<std::string>{"8 int = 3", "0 int = 7"}))
      << "a list wipes only the part it covers";
  EXPECT_EQ(layout_of(c, "t5"), (std::vector<std::string>{"8 int = 3", "4 int = 7"}))
      << "the wipe starts where the part starts, not where its first named member does";
  EXPECT_EQ(layout_of(c, "a5"), std::vector<std::string>{"0 P = load(lq)"});
}

TEST(InitializerLayoutTest, ABitFieldIsPlacedAsEachTargetLaysItOut) {
  const char *src = "struct M { char c; int b : 3; int d : 5; } m = {1, 2, 3};\n";
  {
    TargetGuard guard(TARGET_WINDOWS_X64);
    Checked c;
    check(c, src);
    ASSERT_EQ(c.errors, 0) << c.diagnostics;
    EXPECT_EQ(layout_of(c, "m"),
              (std::vector<std::string>{"0 char = (char)1", "4.0:3 int = 2", "4.3:5 int = 3"}))
        << "windows starts a new int unit after the char";
  }
  {
    TargetGuard guard(TARGET_LINUX_X64);
    Checked c;
    check(c, src);
    ASSERT_EQ(c.errors, 0) << c.diagnostics;
    EXPECT_EQ(layout_of(c, "m"),
              (std::vector<std::string>{"0 char = (char)1", "0.8:3 int = 2", "0.11:5 int = 3"}))
        << "linux packs the fields after the char";
  }
}

TEST(InitializerLayoutTest, AValueThatDoesNotConvertIsNotRecorded) {
  const char *src = "int a[2] = {1.5, 2};\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(c.errors, 0) << c.diagnostics;
  EXPECT_EQ(layout_of(c, "a"), (std::vector<std::string>{"0 int = (int)1.5", "4 int = 2"}));

  Checked bad;
  check(bad, "int *p = 1.5;\nstruct S { int *q; int n; } s = {2.5, 3};\n");
  EXPECT_EQ(bad.errors, 2) << bad.diagnostics;
  EXPECT_EQ(layout_of(bad, "p"), std::vector<std::string>{});
  EXPECT_EQ(layout_of(bad, "s"), std::vector<std::string>{"8 int = 3"});
}

static long long object_size(type_info *type) {
  type = ConversionWalk::resolved(type);
  switch (type->kind) {
  case TYPE_ARRAY:
    return type->array_size * object_size(type->ptr_to);
  case TYPE_STRUCT:
  case TYPE_UNION:
    return type->symbol->size;
  case TYPE_POINTER:
    return 8;
  default:
    break;
  }
  switch (type->prim) {
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
  case PRIM_FLOAT:
    return 4;
  case PRIM_LONG:
  case PRIM_ULONG:
    return target_current()->long_size;
  default:
    return 8;
  }
}

static std::vector<unsigned char> play(const Checked &c, const char *name);

static std::vector<unsigned char> value_bytes(const Checked &c, ast_node *value, long long size) {
  while (value->type == AST_NODE_TYPE_CONVERSION)
    value = value->conversion.operand;
  std::vector<unsigned char> out((size_t)size, 0);
  if (value->type == AST_NODE_TYPE_IDENTIFIER)
    return play(c, value->tok.value);
  if (value->type == AST_NODE_TYPE_STRING) {
    for (long long i = 0; i < size && i < value->literal.length; i++)
      out[(size_t)i] = (unsigned char)value->literal.bytes[i];
    return out;
  }
  if (value->type == AST_NODE_TYPE_NUMBER) {
    unsigned long long number = std::strtoull(value->tok.value, nullptr, 0);
    for (long long i = 0; i < size && i < 8; i++)
      out[(size_t)i] = (unsigned char)(number >> (8 * i));
  }
  return out;
}

static std::vector<unsigned char> play(const Checked &c, const char *name) {
  std::vector<ast_node *> decl = declarations(c, name);
  if (decl.size() != 1)
    return {};
  long long size = object_size(decl[0]->var_decl.type);
  std::vector<unsigned char> bytes((size_t)size, 0);
  const initializer_layout &layout = decl[0]->var_decl.init_layout;
  for (int i = 0; i < layout.count; i++) {
    const initializer_entry &entry = layout.entries[i];
    std::vector<unsigned char> value = value_bytes(c, entry.value, object_size(entry.type));
    if (entry.bit_width == 0) {
      for (size_t k = 0; k < value.size() && entry.offset + (long long)k < size; k++)
        bytes[(size_t)entry.offset + k] = value[k];
      continue;
    }
    unsigned long long word = 0;
    unsigned long long field = 0;
    for (size_t k = 0; k < 8 && k < value.size(); k++)
      field |= (unsigned long long)value[k] << (8 * k);
    for (long long k = 0; k < 8 && entry.offset + k < size; k++)
      word |= (unsigned long long)bytes[(size_t)(entry.offset + k)] << (8 * k);
    unsigned long long mask = ((1ull << entry.bit_width) - 1) << entry.bit_offset;
    word = (word & ~mask) | ((field << entry.bit_offset) & mask);
    for (long long k = 0; k < 8 && entry.offset + k < size; k++)
      bytes[(size_t)(entry.offset + k)] = (unsigned char)(word >> (8 * k));
  }
  return bytes;
}

static std::string hex(const std::vector<unsigned char> &bytes) {
  std::string out;
  char digits[4];
  for (unsigned char b : bytes) {
    std::snprintf(digits, sizeof digits, "%02x", b);
    out += (out.empty() ? "" : " ") + std::string(digits);
  }
  return out;
}

TEST(InitializerLayoutTest, WritingEachLayoutInOrderGivesGccsBytes) {
  std::string src = std::string(layout_records) +
                    "struct O s1 = {.p = {7, 8}, .p.y = 5};\n"
                    "union U s2 = {.i = 0x11223344, .c[0] = 1};\n"
                    "union U s3 = {.c[1] = 9, .i = 0x55667788};\n"
                    "int s4[3] = {[0] = 1, [1] = 2, [0] = 3};\n"
                    "struct O s5 = {.p.y = 5, .p = {7, 8}};\n"
                    "struct W s6 = {.u.i = 0x11223344, .u.c[2] = 0xAA, .tail = 1};\n"
                    "struct B s7 = {.b = 3, .a = 2, .b = 1};\n"
                    "struct B s8 = {.b = 3, .a = 2};\n"
                    "struct O s9 = {{1, 2}, 3, .p.x = 9};\n"
                    "struct O t1 = {.p.y = 5, .p = {7}};\n"
                    "struct V t2 = {.u.c[1] = 9, .u = {0x01}};\n"
                    "int t3[2][2] = {[0][1] = 5, [0] = {7}};\n"
                    "struct Q t5 = {.z = 3, .l = {7}};\n"
                    "struct O e1 = {1, 2, 3};\n"
                    "int e2[2][3] = {1, 2, 3, 4};\n"
                    "char e3[2][4] = {\"ab\", [0] = \"x\"};\n"
                    "char e4[] = \"hey\";\n"
                    "struct { char c; long long d; short e[3]; } e5 = {97, 0x1122334455667788,\n"
                    "                                                  {1, 2, 3}};\n"
                    "struct B e7 = {1, 2, 3};\n"
                    "int e8 = {42};\n"
                    "union U e9 = {0x7f};\n"
                    "void f(void) {\n"
                    "  struct P lq = q;\n"
                    "  struct O a1 = {.p = lq, .p.y = 5};\n"
                    "  struct O a5 = {.p.y = 5, .p = lq};\n"
                    "  struct O a9 = {lq, 3, .p.x = 9};\n"
                    "  struct P e6[3] = {lq, 5, 6, [2].y = 4};\n"
                    "}\n";
  const std::vector<std::pair<const char *, const char *>> gcc = {
      {"q", "07 00 00 00 08 00 00 00"},
      {"s1", "07 00 00 00 05 00 00 00 00 00 00 00"},
      {"s2", "01 00 00 00"},
      {"s3", "88 77 66 55"},
      {"s4", "03 00 00 00 02 00 00 00 00 00 00 00"},
      {"s5", "07 00 00 00 08 00 00 00 00 00 00 00"},
      {"s6", "00 00 aa 00 01 00 00 00"},
      {"s7", "12 00 00 00 00 00 00 00"},
      {"s8", "32 00 00 00 00 00 00 00"},
      {"s9", "09 00 00 00 02 00 00 00 03 00 00 00"},
      {"t1", "07 00 00 00 00 00 00 00 00 00 00 00"},
      {"t2", "01 00 00 00"},
      {"t3", "07 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"},
      {"t5", "00 00 00 00 07 00 00 00 03 00 00 00"},
      {"e1", "01 00 00 00 02 00 00 00 03 00 00 00"},
      {"e2", "01 00 00 00 02 00 00 00 03 00 00 00 04 00 00 00 00 00 00 00 00 00 00 00"},
      {"e3", "78 00 00 00 00 00 00 00"},
      {"e4", "68 65 79 00"},
      {"e5", "61 00 00 00 00 00 00 00 88 77 66 55 44 33 22 11 01 00 02 00 03 00 00 00"},
      {"e7", "21 00 00 00 03 00 00 00"},
      {"e8", "2a 00 00 00"},
      {"e9", "7f 00 00 00"},
      {"lq", "07 00 00 00 08 00 00 00"},
      {"a1", "00 00 00 00 05 00 00 00 00 00 00 00"},
      {"a5", "07 00 00 00 08 00 00 00 00 00 00 00"},
      {"a9", "09 00 00 00 00 00 00 00 03 00 00 00"},
      {"e6", "07 00 00 00 08 00 00 00 05 00 00 00 06 00 00 00 00 00 00 00 04 00 00 00"},
  };
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    SCOPED_TRACE(kind == TARGET_LINUX_X64 ? "linux" : "windows");
    Checked c;
    check(c, src.c_str());
    ASSERT_EQ(c.parse_errors, 0);
    ASSERT_EQ(c.errors, 0) << c.diagnostics;
    for (const auto &object : gcc)
      EXPECT_EQ(hex(play(c, object.first)), object.second) << object.first;
  }
}

TEST(CaseValueTest, EachLabelKeepsItsValueInTheSwitchsPromotedType) {
  const char *src = "void f(unsigned char c, long long q, unsigned u) {\n"
                    "  switch (c) { case 'a' + 1: case (char)300: case 2u: ; }\n"
                    "  switch (q) { case -1: case 0x100000000: ; }\n"
                    "  switch (u) { case -1: ; }\n"
                    "}\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  std::vector<unsigned long long> seen;
  for (ast_node *label : nodes_of(c, AST_NODE_TYPE_CASE))
    seen.push_back(label->case_stmt.label_value);
  std::vector<unsigned long long> expected = {
      98, 44, 2, 0xFFFFFFFFFFFFFFFFull, 0x100000000ull, 0xFFFFFFFFull};
  EXPECT_EQ(seen, expected);
}

static const initializer_entry *only_entry(const Checked &c, const char *name) {
  std::vector<ast_node *> decl = declarations(c, name);
  if (decl.size() != 1 || decl[0]->type != AST_NODE_TYPE_VAR_DECL ||
      decl[0]->var_decl.init_layout.count != 1)
    return nullptr;
  return &decl[0]->var_decl.init_layout.entries[0];
}

static std::string constant_text(const initializer_entry *entry) {
  if (entry == nullptr)
    return "<no single entry>";
  const constant_value &value = entry->constant;
  char text[96];
  switch (value.kind) {
  case CONSTANT_INTEGER:
    return "integer " + std::to_string((long long)value.bits);
  case CONSTANT_FLOATING:
    if (value.imag != 0)
      std::snprintf(text, sizeof text, "floating %g%+gi", (double)value.real, (double)value.imag);
    else
      std::snprintf(text, sizeof text, "floating %g", (double)value.real);
    return text;
  case CONSTANT_ADDRESS: {
    std::string base = value.symbol    ? value.symbol->name
                       : value.literal ? shape(value.literal)
                                       : "null";
    std::snprintf(text, sizeof text, "%+lld", value.offset);
    return "address " + base + text;
  }
  default:
    return "none";
  }
}

TEST(StaticValueTest, EachKindOfValueIsRecorded) {
  const char *src = "int arr[10];\n"
                    "static int i = (int)(2.5 * 2);\n"
                    "static int t = 0.5 ? 3 : 4;\n"
                    "static int s = 0.0 && 1 / 0;\n"
                    "static unsigned char u = -1;\n"
                    "static double d = -1.5;\n"
                    "static long double l = 1;\n"
                    "static double _Complex z = 1.0 + 2.0i;\n"
                    "static _Bool b = 0.0 / 0.0;\n"
                    "static _Bool bp = &arr[1];\n"
                    "static _Bool bz = &arr[0];\n"
                    "static _Bool bn = (int *)0;\n"
                    "static int *p = &arr[3];\n"
                    "static char str[] = \"a\";\n"
                    "static int cmp = 1.5 < 2.5;\n"
                    "static int ceq = 1.0 + 1.0i == 1.0;\n"
                    "static int rq = (double)9007199254740993LL == 9007199254740992.0;\n"
                    "static int rf = (float)16777217 == 16777216.0f;\n"
                    "static double re = 1.0 + 2.0i;\n"
                    "static int ia = (int)2.5 + (int)(0.5 * 4);\n"
                    "static int sh = (int)(1.5 * 2) << 2;\n"
                    "static int un = -(int)(1.5 * 2);\n"
                    "static int ut = ~(int)(1.5 * 2);\n"
                    "void f(void) { int x = 5; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  EXPECT_EQ(constant_text(only_entry(c, "i")), "integer 5");
  EXPECT_EQ(constant_text(only_entry(c, "t")), "integer 3");
  EXPECT_EQ(constant_text(only_entry(c, "s")), "integer 0") << "1 / 0 is never evaluated";
  EXPECT_EQ(constant_text(only_entry(c, "u")), "integer 255");
  EXPECT_EQ(constant_text(only_entry(c, "d")), "floating -1.5");
  EXPECT_EQ(constant_text(only_entry(c, "l")), "floating 1");
  EXPECT_EQ(constant_text(only_entry(c, "z")), "floating 1+2i");
  EXPECT_EQ(constant_text(only_entry(c, "b")), "integer 1") << "a NaN compares unequal to 0";
  EXPECT_EQ(constant_text(only_entry(c, "bp")), "integer 1");
  EXPECT_EQ(constant_text(only_entry(c, "bz")), "integer 1") << "an object's address is never null";
  EXPECT_EQ(constant_text(only_entry(c, "bn")), "integer 0");
  EXPECT_EQ(constant_text(only_entry(c, "p")), "address arr+12");
  EXPECT_EQ(constant_text(only_entry(c, "str")), "none") << "a string's bytes are the literal";
  EXPECT_EQ(constant_text(only_entry(c, "x")), "none")
      << "an automatic object is filled at run time";
  EXPECT_EQ(constant_text(only_entry(c, "cmp")), "integer 1");
  EXPECT_EQ(constant_text(only_entry(c, "ceq")), "integer 0")
      << "== compares the imaginary parts too";
  EXPECT_EQ(constant_text(only_entry(c, "rq")), "integer 1") << "the conversion rounds to double";
  EXPECT_EQ(constant_text(only_entry(c, "rf")), "integer 1") << "the conversion rounds to float";
  EXPECT_EQ(constant_text(only_entry(c, "re")), "floating 1")
      << "a real type drops the imaginary part";
  EXPECT_EQ(constant_text(only_entry(c, "ia")), "integer 4");
  EXPECT_EQ(constant_text(only_entry(c, "sh")), "integer 12");
  EXPECT_EQ(constant_text(only_entry(c, "un")), "integer -3");
  EXPECT_EQ(constant_text(only_entry(c, "ut")), "integer -4");
}

TEST(StaticValueTest, AnAddressIsABaseAndAByteOffset) {
  const char *src = "struct S { int a; char b[8]; struct { short x, y; } in[3]; } s;\n"
                    "int arr[10];\n"
                    "int f(void);\n"
                    "char *p1 = \"hello\" + 2;\n"
                    "int *p2 = &arr[3];\n"
                    "int *p3 = arr + 5;\n"
                    "char *p4 = &s.b[2];\n"
                    "short *p5 = &s.in[1].y;\n"
                    "int (*p6)(void) = f;\n"
                    "int (*p7)(void) = &f;\n"
                    "int *p8 = 0;\n"
                    "int *p9 = (int *)4096;\n"
                    "char *p10 = &\"xyz\"[1];\n"
                    "int *p11 = (int *)((char *)&arr[2] + 1);\n"
                    "int *p14 = 1 ? &arr[1] : 0;\n"
                    "int *p15 = (int[]){1, 2} + 1;\n"
                    "short *p16 = &((struct S *)0)->in[2].x;\n"
                    "int *p17 = arr + 7 - 2;\n"
                    "int *p18 = &*arr + 1;\n"
                    "struct S *p19 = &s;\n"
                    "int *p20 = &s.a;\n"
                    "char *p21 = s.b + 3;\n";
  const std::vector<std::pair<const char *, const char *>> gcc = {
      {"p1", "address \"hello\"+2"}, {"p2", "address arr+12"},   {"p3", "address arr+20"},
      {"p4", "address s+6"},         {"p5", "address s+18"},     {"p6", "address f+0"},
      {"p7", "address f+0"},         {"p8", "address null+0"},   {"p9", "address null+4096"},
      {"p10", "address \"xyz\"+1"},  {"p11", "address arr+9"},   {"p14", "address arr+4"},
      {"p15", "address literal+4"},  {"p16", "address null+20"}, {"p17", "address arr+20"},
      {"p18", "address arr+4"},      {"p19", "address s+0"},     {"p20", "address s+0"},
      {"p21", "address s+7"},
  };
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    SCOPED_TRACE(kind == TARGET_LINUX_X64 ? "linux" : "windows");
    Checked c;
    check(c, src);
    ASSERT_EQ(c.parse_errors, 0);
    ASSERT_EQ(c.errors, 0) << c.diagnostics;
    for (const auto &object : gcc)
      EXPECT_EQ(constant_text(only_entry(c, object.first)), object.second) << object.first;
  }
}

static std::string constant_bytes(const initializer_entry *entry) {
  if (entry == nullptr)
    return "<no single entry>";
  type_info *type = ConversionWalk::resolved(entry->type);
  const constant_value &value = entry->constant;
  std::vector<unsigned char> bytes;
  if (value.kind == CONSTANT_INTEGER) {
    long long size = sema_type_layout(type).size;
    for (long long i = 0; i < size; i++)
      bytes.push_back((unsigned char)(value.bits >> (8 * i)));
  } else if (value.kind == CONSTANT_FLOATING) {
    for (int part = 0; part < (type->is_complex ? 2 : 1); part++) {
      long double v = part ? value.imag : value.real;
      unsigned char raw[16] = {0};
      if (type->prim == PRIM_FLOAT) {
        float f = (float)v;
        std::memcpy(raw, &f, 4);
        bytes.insert(bytes.end(), raw, raw + 4);
      } else if (type->prim == PRIM_DOUBLE) {
        double d = (double)v;
        std::memcpy(raw, &d, 8);
        bytes.insert(bytes.end(), raw, raw + 8);
      } else {
        std::memcpy(raw, &v, 10);
        bytes.insert(bytes.end(), raw, raw + 16);
      }
    }
  } else {
    return "<not a number>";
  }
  return hex(bytes);
}

TEST(StaticValueTest, ArithmeticValuesHaveGccsBytes) {
  const char *src = "double d1 = 1e308 * 10;\n"
                    "double _Complex z1 = (1.0 + 2.0i) * (3.0 + 4.0i);\n"
                    "double _Complex z2 = (1.0 + 2.0i) / (3.0 + 4.0i);\n"
                    "long double l1 = 1.0L / 3;\n"
                    "float f1 = 16777217;\n"
                    "double g1 = 0.1f;\n"
                    "long long h1 = 9007199254740993.0;\n"
                    "_Bool b1 = 0.5;\n"
                    "_Bool b2 = __builtin_nanf(\"\");\n"
                    "float f2 = 1.0 / 3.0;\n"
                    "double g2 = 1.0f / 3.0f;\n"
                    "unsigned long long u1 = 1.8446744073709552e19 - 4096;\n"
                    "long double l2 = 0x1.fffffffffffffffep0L;\n"
                    "float f3 = 3.4028235677973366e38;\n"
                    "double _Complex z3 = 2.0 * 1.0i;\n"
                    "float _Complex z4 = 1.0 / 3.0 + 0.5i;\n"
                    "char c1 = 300;\n"
                    "int i1 = 7 / 2 * 2.0;\n"
                    "float fr = 16777216.0f + 1.0f - 1.0f;\n"
                    "double dd = 9007199254740992.0 + 1.0 - 1.0;\n"
                    "double neg = -3;\n"
                    "double ub = 18446744073709551615u;\n"
                    "double hv = __builtin_huge_val();\n";
  const std::vector<std::pair<const char *, const char *>> gcc = {
      {"d1", "00 00 00 00 00 00 f0 7f"},
      {"z1", "00 00 00 00 00 00 14 c0 00 00 00 00 00 00 24 40"},
      {"z2", "29 5c 8f c2 f5 28 dc 3f 7b 14 ae 47 e1 7a b4 3f"},
      {"l1", "ab aa aa aa aa aa aa aa fd 3f 00 00 00 00 00 00"},
      {"f1", "00 00 80 4b"},
      {"g1", "00 00 00 a0 99 99 b9 3f"},
      {"h1", "00 00 00 00 00 00 20 00"},
      {"b1", "01"},
      {"b2", "01"},
      {"f2", "ab aa aa 3e"},
      {"g2", "00 00 00 60 55 55 d5 3f"},
      {"u1", "00 f0 ff ff ff ff ff ff"},
      {"l2", "ff ff ff ff ff ff ff ff ff 3f 00 00 00 00 00 00"},
      {"f3", "00 00 80 7f"},
      {"z3", "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 40"},
      {"z4", "ab aa aa 3e 00 00 00 3f"},
      {"c1", "2c"},
      {"i1", "06 00 00 00"},
      {"fr", "ff ff 7f 4b"},
      {"dd", "ff ff ff ff ff ff 3f 43"},
      {"neg", "00 00 00 00 00 00 08 c0"},
      {"ub", "00 00 00 00 00 00 f0 43"},
      {"hv", "00 00 00 00 00 00 f0 7f"},
  };
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    SCOPED_TRACE(kind == TARGET_LINUX_X64 ? "linux" : "windows");
    Checked c;
    check(c, src);
    ASSERT_EQ(c.parse_errors, 0);
    ASSERT_EQ(c.errors, 0) << c.diagnostics;
    for (const auto &object : gcc)
      EXPECT_EQ(constant_bytes(only_entry(c, object.first)), object.second) << object.first;
  }
}

TEST(StaticValueTest, AFloatingValueThatDoesNotFitItsIntegerIsAnError) {
  const char *src = "int a1 = 1e10;\n"
                    "unsigned a2 = -1.0;\n"
                    "int a3 = __builtin_nanf(\"\");\n"
                    "int a4 = -1e10;\n"
                    "unsigned a5 = 4294967296.0;\n"
                    "int big = 1e100;\n"
                    "int ok1 = 2147483647.0;\n"
                    "unsigned ok2 = -0.5;\n"
                    "int ok3 = -2147483648.0;\n"
                    "int m = (int)(2.5 * 2) / 0;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  const std::string overflow = "overflow in a constant expression";
  EXPECT_EQ(c.diagnostics,
            error_at(src, "1e10;", 1, overflow) + error_at(src, "-1.0;", 1, overflow) +
                error_at(src, "__builtin_nanf", 1, overflow) +
                error_at(src, "-1e10;", 1, overflow) + error_at(src, "4294967296.0;", 1, overflow) +
                error_at(src, "1e100;", 1, overflow) +
                error_at(src, "/ 0", 1, "division by zero in a constant expression"))
      << "6.6p4: a constant must be in range for its type; GCC only warns and clamps";
  EXPECT_EQ(constant_text(only_entry(c, "ok1")), "integer 2147483647");
  EXPECT_EQ(constant_text(only_entry(c, "ok2")), "integer 0") << "-0.5 truncates to 0";
  EXPECT_EQ(constant_text(only_entry(c, "ok3")), "integer -2147483648");
}

static symbol *ordinary(const Checked &c, const char *name) {
  symbol *found = find_symbol(c, name, SYMBOL_FUNC);
  return found != nullptr ? found : find_symbol(c, name, SYMBOL_VAR);
}

TEST(DeclarationFactTest, AnAsmLabelNamesTheSymbol) {
  const char *src = "int f(void) __asm__(\"\" \"real_f\");\n"
                    "int g __asm__(\"gee\");\n"
                    "int h;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  ASSERT_NE(ordinary(c, "f"), nullptr);
  ASSERT_NE(ordinary(c, "g"), nullptr);
  ASSERT_NE(ordinary(c, "h"), nullptr);
  EXPECT_STREQ(ordinary(c, "f")->asm_label, "real_f") << "the strings are glued together";
  EXPECT_STREQ(ordinary(c, "g")->asm_label, "gee");
  EXPECT_EQ(ordinary(c, "h")->asm_label, nullptr);
}

TEST(DeclarationFactTest, FactsJoinOverEveryDeclarationOfAName) {
  const char *src = "extern int a;\n"
                    "__attribute__((__dllimport__)) extern int a;\n"
                    "extern int a;\n"
                    "int r(void) __attribute__((returns_twice));\n"
                    "int r(void);\n"
                    "int n(void) __asm__(\"named\");\n"
                    "int n(void);\n"
                    "int k(void) __asm__(\"kk\");\n"
                    "void use(void) { extern int k(void); k(); }\n"
                    "__attribute__((dllimport)) extern int dv;\n"
                    "int jump(void) __attribute__((returns_twice));\n"
                    "void use2(void) { extern int dv; extern int jump(void); dv; jump(); }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  ASSERT_NE(ordinary(c, "a"), nullptr);
  EXPECT_EQ(ordinary(c, "a")->dllimport, 1) << "a later declaration without it does not erase it";
  EXPECT_EQ(ordinary(c, "r")->returns_twice, 1);
  EXPECT_STREQ(ordinary(c, "n")->asm_label, "named");
  std::vector<ast_node *> k = uses(c, "k");
  std::vector<ast_node *> k_decls = declarations(c, "k");
  ASSERT_EQ(k.size(), 1u);
  ASSERT_EQ(k_decls.size(), 2u);
  ASSERT_NE(k[0]->symbol, nullptr);
  EXPECT_NE(k_decls[0]->symbol, k_decls[1]->symbol)
      << "a block-scope extern has a symbol of its own";
  EXPECT_EQ(k[0]->symbol, k_decls[1]->symbol);
  EXPECT_STREQ(k[0]->symbol->asm_label, "kk") << "and takes the facts of what it refers to";
  std::vector<ast_node *> dv = uses(c, "dv");
  std::vector<ast_node *> jump = uses(c, "jump");
  ASSERT_EQ(dv.size(), 1u);
  ASSERT_EQ(jump.size(), 1u);
  ASSERT_NE(dv[0]->symbol, nullptr);
  ASSERT_NE(jump[0]->symbol, nullptr);
  EXPECT_EQ(dv[0]->symbol->dllimport, 1);
  EXPECT_EQ(jump[0]->symbol->returns_twice, 1);

  const char *conflict = "int h(void) __asm__(\"x\");\nint h(void) __asm__(\"y\");\n";
  Checked d;
  check(d, conflict);
  EXPECT_EQ(d.diagnostics,
            error_at(conflict, "h(void) __asm__(\"y\")", 1, "conflicting asm labels for 'h'"));
}

TEST(DeclarationFactTest, DeclspecDllimportIsAnAttribute) {
  const char *src = "__declspec(dllimport) int c(void);\n"
                    "extern __declspec(dllimport) int v;\n";
  Checked c;
  check_preprocessed(c, src);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  ASSERT_NE(ordinary(c, "c"), nullptr);
  ASSERT_NE(ordinary(c, "v"), nullptr);
  EXPECT_EQ(ordinary(c, "c")->dllimport, 1);
  EXPECT_EQ(ordinary(c, "v")->dllimport, 1);
}

TEST(DeclarationFactTest, SetjmpAndItsKinReturnTwiceByName) {
  const char *src =
      "int setjmp(void); int _setjmp(void); int __setjmp(void);\n"
      "int sigsetjmp(void); int __sigsetjmp(void); int savectx(void);\n"
      "int vfork(void); int getcontext(void);\n"
      "int _longjmp(void); int setjmpx(void); int _vfork(void); int ___setjmp(void);\n"
      "static int _getcontext(void) { return 0; }\n"
      "static int __sigsetjmp_local(void) { return 0; }\n"
      "int ordinary(void);\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  for (const char *name : {"setjmp", "_setjmp", "__setjmp", "sigsetjmp", "__sigsetjmp", "savectx",
                           "vfork", "getcontext"}) {
    ASSERT_NE(ordinary(c, name), nullptr) << name;
    EXPECT_EQ(ordinary(c, name)->returns_twice, 1) << name;
  }
  for (const char *name : {"_longjmp", "setjmpx", "_vfork", "___setjmp", "_getcontext",
                           "__sigsetjmp_local", "ordinary"}) {
    ASSERT_NE(ordinary(c, name), nullptr) << name;
    EXPECT_EQ(ordinary(c, name)->returns_twice, 0) << name;
  }

  const char *internal =
      "static int setjmp(void) { return 0; }\nint savectx;\nint getcontext(void) { return 0; }\n";
  Checked d;
  check(d, internal);
  ASSERT_EQ(d.errors, 0) << d.diagnostics;
  ASSERT_NE(ordinary(d, "setjmp"), nullptr);
  EXPECT_EQ(ordinary(d, "setjmp")->returns_twice, 0) << "only a function with external linkage";
  ASSERT_NE(ordinary(d, "savectx"), nullptr);
  EXPECT_EQ(ordinary(d, "savectx")->returns_twice, 0) << "and never a variable";
  ASSERT_NE(ordinary(d, "getcontext"), nullptr);
  EXPECT_EQ(ordinary(d, "getcontext")->returns_twice, 1) << "a definition counts too";
}

static int inline_definition(const Checked &c, const char *name) {
  for (ast_node *decl : declarations(c, name)) {
    if (decl->type == AST_NODE_TYPE_FUNCTION_DEF)
      return decl->function_def.is_inline_definition;
  }
  return -1;
}

TEST(DeclarationFactTest, AnInlineDefinitionIsMarkedAtTheEndOfTheFile) {
  const char *src = "inline int a(void) { return 0; }\n"
                    "inline int b(void) { return 0; }\n"
                    "int b(void);\n"
                    "int h(void);\n"
                    "inline int h(void) { return 0; }\n"
                    "extern inline int e(void) { return 0; }\n"
                    "static inline int s(void) { return 0; }\n"
                    "extern inline __attribute__((__gnu_inline__)) int ge(void) { return 0; }\n"
                    "inline __attribute__((gnu_inline)) int gi(void) { return 0; }\n"
                    "int plain(void) { return 0; }\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.errors, 0) << c.diagnostics;
  EXPECT_EQ(inline_definition(c, "a"), 1) << "every declaration is inline without extern";
  EXPECT_EQ(inline_definition(c, "b"), 0) << "a later plain declaration makes it external";
  EXPECT_EQ(inline_definition(c, "h"), 0) << "so does an earlier one";
  EXPECT_EQ(inline_definition(c, "e"), 0);
  EXPECT_EQ(inline_definition(c, "s"), 0) << "internal linkage is emitted locally";
  EXPECT_EQ(inline_definition(c, "ge"), 1) << "GNU's extern inline is the inline-only one";
  EXPECT_EQ(inline_definition(c, "gi"), 0) << "and GNU's plain inline is emitted";
  EXPECT_EQ(inline_definition(c, "plain"), 0);
}

TEST(SizeTest, TheExportedLayoutIsSemasLayout) {
  const char *src = "struct M { char c; double d; } m;\n"
                    "long double ld;\n"
                    "long l;\n"
                    "int arr[10];\n"
                    "struct Unknown *u;\n";
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    SCOPED_TRACE(kind == TARGET_LINUX_X64 ? "linux" : "windows");
    Checked c;
    check(c, src);
    ASSERT_EQ(c.errors, 0) << c.diagnostics;
    type_layout record = sema_type_layout(type_of(c, "m"));
    EXPECT_EQ(record.known, 1);
    EXPECT_EQ(record.size, 16);
    EXPECT_EQ(record.alignment, 8);
    EXPECT_EQ(sema_type_layout(type_of(c, "ld")).size, 16);
    EXPECT_EQ(sema_type_layout(type_of(c, "l")).size, kind == TARGET_LINUX_X64 ? 8 : 4);
    type_layout array = sema_type_layout(type_of(c, "arr"));
    EXPECT_EQ(array.size, 40);
    EXPECT_EQ(array.alignment, 4);
    EXPECT_EQ(sema_type_layout(type_of(c, "u")->ptr_to).known, 0) << "an incomplete struct";
  }
}

TEST(VlaTest, AVariableLengthIsASizeTValue) {
  const char *src = "void f(int n) { double a[n]; int m[n][n + 1]; int k[4]; }\n";
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    SCOPED_TRACE(kind == TARGET_LINUX_X64 ? "linux" : "windows");
    std::string size_t_name = kind == TARGET_LINUX_X64 ? "unsigned long" : "unsigned long long";
    Checked c;
    check(c, src);
    ASSERT_EQ(c.errors, 0) << c.diagnostics;
    type_info *a = type_of(c, "a");
    type_info *m = type_of(c, "m");
    type_info *k = type_of(c, "k");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(m, nullptr);
    ASSERT_NE(k, nullptr);
    EXPECT_EQ(shape(a->array_size_expr), "(" + size_t_name + ")load(n)");
    EXPECT_EQ(shape(m->array_size_expr), "(" + size_t_name + ")load(n)");
    EXPECT_EQ(shape(m->ptr_to->array_size_expr), "(" + size_t_name + ")(load(n) + 1)");
    EXPECT_EQ(shape(k->array_size_expr), "4") << "a constant size is never evaluated at run time";
  }
}

TEST(StaticValueTest, ANonConstantInitializerIsNotEvaluated) {
  const char *src = "int gv;\n"
                    "static int nz = (1 / 0) + gv;\n";
  Checked c;
  check(c, src);
  ASSERT_EQ(c.parse_errors, 0);
  EXPECT_EQ(
      c.diagnostics,
      error_at(src, "+ gv", 1, "an initializer for an object with static storage must be constant"))
      << "the division inside is never reported";
  EXPECT_EQ(constant_text(only_entry(c, "nz")), "none");
}
