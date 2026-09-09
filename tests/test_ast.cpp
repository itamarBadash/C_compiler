#include <cstring>
#include <gtest/gtest.h>

extern "C" {
#include "ast.h"
#include "parser.h"
#include <stdlib.h>
}

extern "C" {
int ast_free_calls = 0;
void __real_free(void *p);
void __wrap_free(void *p) {
  ast_free_calls++;
  __real_free(p);
}
}

TEST(AstTests, CreateNodeSetsTheTypeAndZeroesEverythingElse) {
  ast_node *n = create_ast_node(AST_NODE_TYPE_NUMBER);
  ASSERT_NE(n, nullptr);
  EXPECT_EQ(n->type, AST_NODE_TYPE_NUMBER);
  EXPECT_EQ(n->tok.value, nullptr);
  EXPECT_EQ(n->literal.bytes, nullptr);
  EXPECT_EQ(n->literal.length, 0);
  EXPECT_EQ(n->literal.is_wide, 0);
  free_ast(n);
}

TEST(AstTests, FreeAstAcceptsNull) {
  free_ast(nullptr);
  SUCCEED();
}

TEST(AstTests, FreeTypeInfoAcceptsNull) {
  free_type_info(nullptr);
  SUCCEED();
}

TEST(AstTests, CreateTypeInfoDefaultsArraySizeToUnspecified) {
  type_info *t = create_type_info(TYPE_PRIMITIVE);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->kind, TYPE_PRIMITIVE);
  EXPECT_EQ(t->array_size, -1);
  EXPECT_EQ(t->ptr_to, nullptr);
  EXPECT_EQ(t->param_count, 0);
  free_type_info(t);
}

TEST(AstTests, FreeAstReleasesTheTokenValue) {
  ast_node *n = create_ast_node(AST_NODE_TYPE_IDENTIFIER);
  n->tok.value = strdup("abc");

  ast_free_calls = 0;
  free_ast(n);
  EXPECT_EQ(ast_free_calls, 2) << "the token value and the node itself";
}

TEST(AstTests, FreeAstReleasesDecodedLiteralBytes) {
  ast_node *n = create_ast_node(AST_NODE_TYPE_STRING);
  n->tok.value = strdup("hi");
  n->literal.bytes = strdup("hi");
  n->literal.length = 2;

  ast_free_calls = 0;
  free_ast(n);
  EXPECT_EQ(ast_free_calls, 3) << "decoded bytes, the raw token value, and the node";
}

TEST(AstTests, FreeAstRecursesIntoChildren) {
  ast_node *left = create_ast_node(AST_NODE_TYPE_NUMBER);
  left->tok.value = strdup("1");
  ast_node *right = create_ast_node(AST_NODE_TYPE_NUMBER);
  right->tok.value = strdup("2");

  ast_node *op = create_ast_node(AST_NODE_TYPE_BINARY_OP);
  op->binary_op.op.value = strdup("+");
  op->binary_op.left = left;
  op->binary_op.right = right;

  ast_free_calls = 0;
  free_ast(op);
  EXPECT_EQ(ast_free_calls, 6) << "operator spelling, two child values, and three nodes";
}

TEST(AstTests, FreeTypeInfoFollowsThePointerChain) {
  type_info *inner = create_type_info(TYPE_PRIMITIVE);
  type_info *outer = create_type_info(TYPE_POINTER);
  outer->ptr_to = inner;

  ast_free_calls = 0;
  free_type_info(outer);
  EXPECT_EQ(ast_free_calls, 2);
}

TEST(AstTests, FreeTypeInfoReleasesTagAndParameters) {
  type_info *fn = create_type_info(TYPE_FUNCTION);
  fn->tag_name = strdup("S");
  fn->param_count = 2;
  fn->param_types = (type_info **)malloc(sizeof(type_info *) * 2);
  fn->param_types[0] = create_type_info(TYPE_PRIMITIVE);
  fn->param_types[1] = create_type_info(TYPE_PRIMITIVE);
  fn->param_names = (char **)malloc(sizeof(char *) * 2);
  fn->param_names[0] = strdup("a");
  fn->param_names[1] = strdup("b");

  ast_free_calls = 0;
  free_type_info(fn);
  EXPECT_EQ(ast_free_calls, 8)
      << "tag, two param types, the type array, two names, the name array, "
         "and the type_info itself";
}

TEST(AstTests, FreeTypeInfoReleasesAVlaSizeExpression) {
  type_info *arr = create_type_info(TYPE_ARRAY);
  arr->array_size_expr = create_ast_node(AST_NODE_TYPE_IDENTIFIER);
  arr->array_size_expr->tok.value = strdup("n");

  ast_free_calls = 0;
  free_type_info(arr);
  EXPECT_EQ(ast_free_calls, 3) << "the identifier spelling, its node, and the type";
}

TEST(AstTests, AParsedProgramFreesCompletely) {
  const char *source = "struct S { int x; };\n"
                       "int f(int a, int b) {\n"
                       "  int c[3] = {1, 2, 3};\n"
                       "  char *s = \"a\\nb\";\n"
                       "  for (int i = 0; i < 3; i++) { c[i] = a + b; }\n"
                       "  return c[0];\n"
                       "}\n";

  lexer lex;
  lexer_init(&lex, source);
  parser p;
  parser_init(&p, &lex);
  ast_node *program = parse_program(&p);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(p.had_error, 0);

  free_ast(program);
  parser_destroy(&p);
  SUCCEED();
}
