#include "target_guard.h"
#include <cstring>
#include <direct.h>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include "ast.h"
#include "parser.h"
#include "preprocessor.h"
#include <stdlib.h>
}

static const char *size_text(const type_info *type) {
  if (type == nullptr || type->array_size_expr == nullptr ||
      type->array_size_expr->type != AST_NODE_TYPE_NUMBER)
    return "";
  return type->array_size_expr->tok.value;
}

class ParserTest : public ::testing::Test {
protected:
  lexer lex;
  parser p;

  bool ready = false;

  void setup_parser(const char *source) {
    if (ready)
      parser_destroy(&p);
    lexer_init(&lex, source);
    parser_init(&p, &lex);
    ready = true;
  }

  void TearDown() override {
    std::cout << "Entering TearDown\n" << std::flush;
    if (ready)
      parser_destroy(&p);
    std::cout << "Leaving TearDown\n" << std::flush;
  }

  ~ParserTest() override {
    std::cout << "ParserTest Destructor\n" << std::flush;
  }
};

TEST_F(ParserTest, ParsePrimaryNumber) {
  std::cout << "Test Start\n";
  setup_parser("42");
  std::cout << "After setup\n";
  ast_node *node = parse_expression(&p);
  std::cout << "After parse\n";

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_NUMBER);
  EXPECT_STREQ(node->tok.value, "42");
  std::cout << "Before free\n";

  free_ast(node);
  std::cout << "After free\n";
}

TEST_F(ParserTest, ParsePrimaryIdentifier) {
  std::cout << "Start ParsePrimaryIdentifier\n" << std::flush;
  setup_parser("my_var");
  std::cout << "After setup ParsePrimaryIdentifier\n" << std::flush;
  ast_node *node = parse_expression(&p);
  std::cout << "After parse ParsePrimaryIdentifier\n" << std::flush;

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_IDENTIFIER);
  EXPECT_STREQ(node->tok.value, "my_var");

  free_ast(node);
  std::cout << "End ParsePrimaryIdentifier\n" << std::flush;
}

TEST_F(ParserTest, ParsePrimaryString) {
  setup_parser("\"hello world\"");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_STRING);
  EXPECT_STREQ(node->tok.value, "hello world");

  free_ast(node);
}

TEST_F(ParserTest, ParsePrimaryChar) {
  setup_parser("'A'");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_CHAR_LITERAL);
  EXPECT_STREQ(node->tok.value, "A");

  free_ast(node);
}

TEST_F(ParserTest, ParsePostfixArray) {
  setup_parser("arr[5]");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_ARRAY_SUBSCRIPT);

  ASSERT_NE(node->array_subscript.left, nullptr);
  EXPECT_EQ(node->array_subscript.left->type, AST_NODE_TYPE_IDENTIFIER);
  EXPECT_STREQ(node->array_subscript.left->tok.value, "arr");

  ASSERT_NE(node->array_subscript.index, nullptr);
  EXPECT_EQ(node->array_subscript.index->type, AST_NODE_TYPE_NUMBER);
  EXPECT_STREQ(node->array_subscript.index->tok.value, "5");

  free_ast(node);
}

TEST_F(ParserTest, ParsePostfixFunctionCall) {
  setup_parser("arr[0](1, x)");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_FUNCTION_CALL);

  ASSERT_NE(node->function_call.callable, nullptr);
  EXPECT_EQ(node->function_call.callable->type, AST_NODE_TYPE_ARRAY_SUBSCRIPT);

  EXPECT_EQ(node->function_call.arg_count, 2);

  ASSERT_NE(node->function_call.arguments[0], nullptr);
  EXPECT_EQ(node->function_call.arguments[0]->type, AST_NODE_TYPE_NUMBER);

  ASSERT_NE(node->function_call.arguments[1], nullptr);
  EXPECT_EQ(node->function_call.arguments[1]->type, AST_NODE_TYPE_IDENTIFIER);

  free_ast(node);
}

TEST_F(ParserTest, ParsePostfixMemberAccess) {
  setup_parser("obj.field");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_MEMBER_ACCESS);
  EXPECT_EQ(node->member_access.is_pointer, 0);
  EXPECT_STREQ(node->member_access.member_name, "field");

  ASSERT_NE(node->member_access.left, nullptr);
  EXPECT_EQ(node->member_access.left->type, AST_NODE_TYPE_IDENTIFIER);

  free_ast(node);
}

TEST_F(ParserTest, ParsePostfixPointerMemberAccess) {
  setup_parser("ptr->field");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_MEMBER_ACCESS);
  EXPECT_EQ(node->member_access.is_pointer, 1);
  EXPECT_STREQ(node->member_access.member_name, "field");

  free_ast(node);
}

TEST_F(ParserTest, ParseUnaryOperations) {
  const char *ops[] = {"-", "!", "~", "&", "*", "++", "--", "+"};
  token_type types[] = {TOKEN_MINUS, TOKEN_NOT,       TOKEN_TILDE,       TOKEN_AMPERSAND,
                        TOKEN_STAR,  TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS, TOKEN_PLUS};

  for (int i = 0; i < 8; ++i) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%sx", ops[i]);
    setup_parser(buffer);

    ast_node *node = parse_expression(&p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_UNARY_OP);
    EXPECT_EQ(node->unary_op.op.type, types[i]);

    ASSERT_NE(node->unary_op.operand, nullptr);
    EXPECT_EQ(node->unary_op.operand->type, AST_NODE_TYPE_IDENTIFIER);

    free_ast(node);
  }
}

TEST_F(ParserTest, ParseBinaryOperations) {
  setup_parser("a + b * c");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_BINARY_OP);
  EXPECT_EQ(node->binary_op.op.type, TOKEN_PLUS);

  ASSERT_NE(node->binary_op.left, nullptr);
  EXPECT_EQ(node->binary_op.left->type, AST_NODE_TYPE_IDENTIFIER);

  ASSERT_NE(node->binary_op.right, nullptr);
  EXPECT_EQ(node->binary_op.right->type, AST_NODE_TYPE_BINARY_OP);
  EXPECT_EQ(node->binary_op.right->binary_op.op.type, TOKEN_STAR);

  free_ast(node);
}

TEST_F(ParserTest, ParseBitwiseOperations) {
  setup_parser("a | b ^ c & d");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_BINARY_OP);
  EXPECT_EQ(node->binary_op.op.type, TOKEN_PIPE);

  ASSERT_NE(node->binary_op.right, nullptr);
  EXPECT_EQ(node->binary_op.right->type, AST_NODE_TYPE_BINARY_OP);
  EXPECT_EQ(node->binary_op.right->binary_op.op.type, TOKEN_CARET);

  ASSERT_NE(node->binary_op.right->binary_op.right, nullptr);
  EXPECT_EQ(node->binary_op.right->binary_op.right->type, AST_NODE_TYPE_BINARY_OP);
  EXPECT_EQ(node->binary_op.right->binary_op.right->binary_op.op.type, TOKEN_AMPERSAND);

  free_ast(node);
}

TEST_F(ParserTest, ParseShiftOperations) {
  setup_parser("1 << 5 >> 2");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_BINARY_OP);
  EXPECT_EQ(node->binary_op.op.type, TOKEN_RSHIFT); // left associative

  ASSERT_NE(node->binary_op.left, nullptr);
  EXPECT_EQ(node->binary_op.left->type, AST_NODE_TYPE_BINARY_OP);
  EXPECT_EQ(node->binary_op.left->binary_op.op.type, TOKEN_LSHIFT);

  free_ast(node);
}

TEST_F(ParserTest, ParseAssignments) {
  setup_parser("a = b += c");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_ASSIGNMENT);
  EXPECT_EQ(node->assignment.op.type, TOKEN_ASSIGN);

  ASSERT_NE(node->assignment.right, nullptr);
  EXPECT_EQ(node->assignment.right->type, AST_NODE_TYPE_ASSIGNMENT);
  EXPECT_EQ(node->assignment.right->assignment.op.type, TOKEN_PLUS_ASSIGN);

  free_ast(node);
}

TEST_F(ParserTest, ParseCompoundAssignments) {
  const char *ops[] = {"+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "^=", "|="};
  token_type types[] = {TOKEN_PLUS_ASSIGN,   TOKEN_MINUS_ASSIGN,     TOKEN_STAR_ASSIGN,
                        TOKEN_SLASH_ASSIGN,  TOKEN_PERCENT_ASSIGN,   TOKEN_LSHIFT_ASSIGN,
                        TOKEN_RSHIFT_ASSIGN, TOKEN_AMPERSAND_ASSIGN, TOKEN_CARET_ASSIGN,
                        TOKEN_PIPE_ASSIGN};

  for (int i = 0; i < 10; ++i) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "x %s 1", ops[i]);
    setup_parser(buffer);

    ast_node *node = parse_expression(&p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_ASSIGNMENT);
    EXPECT_EQ(node->assignment.op.type, types[i]);

    free_ast(node);
  }
}

TEST_F(ParserTest, ParsePostfixIncrement) {
  setup_parser("x++");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_UNARY_OP);
  EXPECT_EQ(node->unary_op.op.type, TOKEN_PLUS_PLUS);
  EXPECT_EQ(node->unary_op.is_postfix, 1);

  ASSERT_NE(node->unary_op.operand, nullptr);
  EXPECT_EQ(node->unary_op.operand->type, AST_NODE_TYPE_IDENTIFIER);

  free_ast(node);
}

TEST_F(ParserTest, ParseSizeof) {
  setup_parser("sizeof x");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_UNARY_OP);
  EXPECT_EQ(node->unary_op.op.type, TOKEN_SIZEOF);
  EXPECT_EQ(node->unary_op.is_postfix, 0);

  ASSERT_NE(node->unary_op.operand, nullptr);
  EXPECT_EQ(node->unary_op.operand->type, AST_NODE_TYPE_IDENTIFIER);

  free_ast(node);
}

TEST_F(ParserTest, ParseCommaOperator) {
  setup_parser("a = 1, b = 2");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_BINARY_OP);
  EXPECT_EQ(node->binary_op.op.type, TOKEN_COMMA);

  ASSERT_NE(node->binary_op.left, nullptr);
  EXPECT_EQ(node->binary_op.left->type, AST_NODE_TYPE_ASSIGNMENT);

  ASSERT_NE(node->binary_op.right, nullptr);
  EXPECT_EQ(node->binary_op.right->type, AST_NODE_TYPE_ASSIGNMENT);

  free_ast(node);
}

TEST_F(ParserTest, ParseTernary) {
  setup_parser("a ? b : c");
  ast_node *node = parse_expression(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_TERNARY);

  ASSERT_NE(node->ternary.condition, nullptr);
  EXPECT_EQ(node->ternary.condition->type, AST_NODE_TYPE_IDENTIFIER);

  ASSERT_NE(node->ternary.true_branch, nullptr);
  EXPECT_EQ(node->ternary.true_branch->type, AST_NODE_TYPE_IDENTIFIER);

  ASSERT_NE(node->ternary.false_branch, nullptr);
  EXPECT_EQ(node->ternary.false_branch->type, AST_NODE_TYPE_IDENTIFIER);

  free_ast(node);
}

TEST_F(ParserTest, ParseControlFlowSwitch) {
  setup_parser("switch (x) { case 1: break; default: continue; }");
  ast_node *node = parse_statement(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_SWITCH);

  ASSERT_NE(node->switch_stmt.condition, nullptr);
  EXPECT_EQ(node->switch_stmt.condition->type, AST_NODE_TYPE_IDENTIFIER);

  ASSERT_NE(node->switch_stmt.body, nullptr);
  EXPECT_EQ(node->switch_stmt.body->type, AST_NODE_TYPE_BLOCK);

  free_ast(node);
}

TEST_F(ParserTest, ParseControlFlowDoWhile) {
  setup_parser("do { x++; } while(x < 10);");
  ast_node *node = parse_statement(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_DO_WHILE);

  ASSERT_NE(node->do_while_stmt.condition, nullptr);
  EXPECT_EQ(node->do_while_stmt.condition->type, AST_NODE_TYPE_BINARY_OP);

  ASSERT_NE(node->do_while_stmt.body, nullptr);
  EXPECT_EQ(node->do_while_stmt.body->type, AST_NODE_TYPE_BLOCK);

  free_ast(node);
}

TEST_F(ParserTest, ParseVarDeclPointersAndArrays) {
  setup_parser("int *p, arr[10];");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);
  ASSERT_EQ(node->program.count, 1);

  ast_node *block = node->program.declarations[0];
  EXPECT_EQ(block->type, AST_NODE_TYPE_DECL_GROUP);
  EXPECT_EQ(block->block.count, 2);

  ast_node *decl1 = block->block.statements[0];
  EXPECT_EQ(decl1->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(decl1->var_decl.var_name, "p");
  EXPECT_EQ(decl1->var_decl.type->kind, TYPE_POINTER);
  EXPECT_EQ(decl1->var_decl.type->ptr_to->kind, TYPE_PRIMITIVE);

  ast_node *decl2 = block->block.statements[1];
  EXPECT_EQ(decl2->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(decl2->var_decl.var_name, "arr");
  EXPECT_EQ(decl2->var_decl.type->kind, TYPE_ARRAY);
  EXPECT_STREQ(size_text(decl2->var_decl.type), "10");
  EXPECT_EQ(decl2->var_decl.type->ptr_to->kind, TYPE_PRIMITIVE);

  free_ast(node);
}

TEST_F(ParserTest, ParseFunctionDefWithPointers) {
  setup_parser("void * alloc(int size) { return 0; }");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);
  ASSERT_EQ(node->program.count, 1);

  ast_node *func = node->program.declarations[0];
  EXPECT_EQ(func->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_STREQ(func->function_def.name, "alloc");

  ASSERT_NE(func->function_def.type->ptr_to, nullptr);
  EXPECT_EQ(func->function_def.type->ptr_to->kind, TYPE_POINTER);
  EXPECT_EQ(func->function_def.type->ptr_to->ptr_to->kind, TYPE_PRIMITIVE);
  EXPECT_EQ(func->function_def.type->ptr_to->ptr_to->prim, PRIM_VOID);

  EXPECT_EQ(func->function_def.type->param_count, 1);
  EXPECT_STREQ(func->function_def.type->param_names[0], "size");
  EXPECT_EQ(func->function_def.type->param_types[0]->kind, TYPE_PRIMITIVE);

  free_ast(node);
}

TEST_F(ParserTest, ParseTypedef) {
  setup_parser("typedef int MyInt; MyInt x;");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);
  ASSERT_EQ(node->program.count, 2);

  // First decl: typedef int MyInt;
  ast_node *decl1 = node->program.declarations[0];
  EXPECT_EQ(decl1->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(decl1->var_decl.var_name, "MyInt");
  EXPECT_EQ(decl1->var_decl.specs.storage_class, TOKEN_TYPEDEF);

  // Second decl: MyInt x;
  ast_node *decl2 = node->program.declarations[1];
  EXPECT_EQ(decl2->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(decl2->var_decl.var_name, "x");
  EXPECT_EQ(decl2->var_decl.specs.storage_class, 0);
  EXPECT_EQ(decl2->var_decl.type->kind, TYPE_TYPEDEF);
  EXPECT_STREQ(decl2->var_decl.type->tag_name, "MyInt");

  free_ast(node);
}

TEST_F(ParserTest, ParseStruct) {
  setup_parser("struct Point p;");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);
  ASSERT_EQ(node->program.count, 1);

  ast_node *decl = node->program.declarations[0];
  EXPECT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(decl->var_decl.var_name, "p");
  EXPECT_EQ(decl->var_decl.type->kind, TYPE_STRUCT);
  EXPECT_STREQ(decl->var_decl.type->tag_name, "Point");

  free_ast(node);
}

TEST_F(ParserTest, ParseInlineStruct) {
  setup_parser("struct Point { int x, y; } p;");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);
  ASSERT_EQ(node->program.count, 1);

  ast_node *block = node->program.declarations[0];
  EXPECT_EQ(block->type, AST_NODE_TYPE_DECL_GROUP);
  if (block->type != AST_NODE_TYPE_DECL_GROUP) {
    free_ast(node);
    return;
  }
  EXPECT_EQ(block->block.count, 2); // def_node and decl
  if (block->block.count < 2) {
    free_ast(node);
    return;
  }

  ast_node *def = block->block.statements[0];
  EXPECT_EQ(def->type, AST_NODE_TYPE_STRUCT_DEF);
  EXPECT_STREQ(def->struct_def.tag_name, "Point");
  EXPECT_EQ(def->struct_def.member_count, 1);

  ast_node *decl = block->block.statements[1];
  EXPECT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(decl->var_decl.var_name, "p");

  free_ast(node);
}

TEST_F(ParserTest, ParseEnum) {
  setup_parser("enum Color { RED, GREEN } c;");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);
  ASSERT_EQ(node->program.count, 1);

  ast_node *block = node->program.declarations[0];
  EXPECT_EQ(block->type, AST_NODE_TYPE_DECL_GROUP);
  if (block->type != AST_NODE_TYPE_DECL_GROUP) {
    free_ast(node);
    return;
  }
  EXPECT_EQ(block->block.count, 2); // def_node and decl
  if (block->block.count < 2) {
    free_ast(node);
    return;
  }

  ast_node *def = block->block.statements[0];
  EXPECT_EQ(def->type, AST_NODE_TYPE_ENUM_DEF);
  EXPECT_STREQ(def->enum_def.tag_name, "Color");
  EXPECT_EQ(def->enum_def.enumerator_count, 2);
  EXPECT_STREQ(def->enum_def.enumerators[0], "RED");
  EXPECT_STREQ(def->enum_def.enumerators[1], "GREEN");

  free_ast(node);
}

TEST_F(ParserTest, ParseTypeCast) {
  setup_parser("int x = (int)3.14;");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  ast_node *init = decl->var_decl.init_value;

  EXPECT_EQ(init->type, AST_NODE_TYPE_CAST);
  EXPECT_EQ(init->cast_expr.type->kind, TYPE_PRIMITIVE);
  EXPECT_EQ(init->cast_expr.type->prim, PRIM_INT);
  EXPECT_EQ(init->cast_expr.operand->type, AST_NODE_TYPE_NUMBER);

  free_ast(node);
}

TEST_F(ParserTest, ParseSizeofType) {
  setup_parser("int x = sizeof(int*);");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  ast_node *init = decl->var_decl.init_value;

  EXPECT_EQ(init->type, AST_NODE_TYPE_UNARY_OP);
  EXPECT_EQ(init->unary_op.op.type, TOKEN_SIZEOF);

  ast_node *dummy_cast = init->unary_op.operand;
  EXPECT_EQ(dummy_cast->type, AST_NODE_TYPE_CAST);
  EXPECT_EQ(dummy_cast->cast_expr.type->kind, TYPE_POINTER);
  EXPECT_EQ(dummy_cast->cast_expr.type->ptr_to->kind, TYPE_PRIMITIVE);
  EXPECT_EQ(dummy_cast->cast_expr.operand, nullptr);

  free_ast(node);
}

TEST_F(ParserTest, ParseInitializerList) {
  std::cout << "ParseInitializerList 1\n" << std::flush;
  setup_parser("int arr[] = {1, 2, 3};");
  std::cout << "ParseInitializerList 2\n" << std::flush;
  ast_node *node = parse_program(&p);
  std::cout << "ParseInitializerList 3\n" << std::flush;

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  ast_node *init = decl->var_decl.init_value;
  std::cout << "ParseInitializerList 4\n" << std::flush;

  EXPECT_EQ(init->type, AST_NODE_TYPE_INIT_LIST);
  EXPECT_EQ(init->init_list.count, 3);
  std::cout << "ParseInitializerList 5\n" << std::flush;
  EXPECT_EQ(init->init_list.items[0].value->type, AST_NODE_TYPE_NUMBER);
  EXPECT_STREQ(init->init_list.items[0].value->tok.value, "1");
  std::cout << "ParseInitializerList 6\n" << std::flush;

  free_ast(node);
  std::cout << "ParseInitializerList 7\n" << std::flush;
}

TEST_F(ParserTest, ParseEnumValues) {
  setup_parser("enum Color { RED = 1, GREEN = 2 } c;");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);

  ASSERT_GE(node->program.count, 1);
  ast_node *block = node->program.declarations[0];
  ast_node *def = block->block.statements[0];
  EXPECT_EQ(def->type, AST_NODE_TYPE_ENUM_DEF);
  EXPECT_EQ(def->enum_def.enumerator_count, 2);

  EXPECT_STREQ(def->enum_def.enumerators[0], "RED");
  ASSERT_NE(def->enum_def.values[0], nullptr);
  EXPECT_EQ(def->enum_def.values[0]->type, AST_NODE_TYPE_NUMBER);
  EXPECT_STREQ(def->enum_def.values[0]->tok.value, "1");

  free_ast(node);
}

TEST_F(ParserTest, ParseMultidimensionalArray) {
  setup_parser("int arr[10][20];");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  EXPECT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);

  EXPECT_EQ(decl->var_decl.type->kind, TYPE_ARRAY);
  EXPECT_STREQ(size_text(decl->var_decl.type), "10");

  EXPECT_EQ(decl->var_decl.type->ptr_to->kind, TYPE_ARRAY);
  EXPECT_STREQ(size_text(decl->var_decl.type->ptr_to), "20");

  EXPECT_EQ(decl->var_decl.type->ptr_to->ptr_to->kind, TYPE_PRIMITIVE);

  free_ast(node);
}

TEST_F(ParserTest, ParseGotoAndLabel) {
  setup_parser("void f() { goto my_label; my_label: return; }");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  EXPECT_EQ(func->type, AST_NODE_TYPE_FUNCTION_DEF);

  ast_node *body = func->function_def.body;
  EXPECT_EQ(body->type, AST_NODE_TYPE_BLOCK);
  EXPECT_EQ(body->block.count, 2);

  ast_node *goto_stmt = body->block.statements[0];
  EXPECT_EQ(goto_stmt->type, AST_NODE_TYPE_GOTO);
  EXPECT_STREQ(goto_stmt->goto_stmt.label_name, "my_label");

  ast_node *label_stmt = body->block.statements[1];
  EXPECT_EQ(label_stmt->type, AST_NODE_TYPE_LABEL);
  EXPECT_STREQ(label_stmt->label_stmt.label_name, "my_label");
  EXPECT_EQ(label_stmt->label_stmt.statement->type, AST_NODE_TYPE_RETURN);

  free_ast(node);
}

TEST_F(ParserTest, ParseQualifiersAndStorage) {
  setup_parser("static const volatile int x;");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  EXPECT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);

  EXPECT_EQ(decl->var_decl.type->is_const, 1);
  EXPECT_EQ(decl->var_decl.type->is_volatile, 1);
  EXPECT_EQ(decl->var_decl.specs.storage_class, TOKEN_STATIC);
  EXPECT_EQ(decl->var_decl.type->kind, TYPE_PRIMITIVE);

  free_ast(node);
}

TEST_F(ParserTest, ParseForLoopDeclaration) {
  setup_parser("for(int i = 0; i < 10; i++) {}");
  ast_node *node = parse_statement(&p);

  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, AST_NODE_TYPE_FOR);

  ASSERT_NE(node->for_stmt.init, nullptr);
  EXPECT_EQ(node->for_stmt.init->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(node->for_stmt.init->var_decl.var_name, "i");

  free_ast(node);
}

TEST_F(ParserTest, ParseLongDoubleComplex) {
  setup_parser("long double _Complex x;");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  EXPECT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);

  EXPECT_EQ(p.had_error, 0);
  EXPECT_EQ(decl->var_decl.type->prim, PRIM_LDOUBLE);
  EXPECT_EQ(decl->var_decl.type->is_complex, 1);

  free_ast(node);
}

TEST_F(ParserTest, ParseHexFloat) {
  setup_parser("float x = 0x1.fp3;");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  ast_node *init = decl->var_decl.init_value;

  EXPECT_EQ(init->type, AST_NODE_TYPE_NUMBER);
  EXPECT_STREQ(init->tok.value, "0x1.fp3");

  free_ast(node);
}

TEST_F(ParserTest, ParseVLA) {
  setup_parser("int arr[n * 2];");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  EXPECT_EQ(decl->var_decl.type->kind, TYPE_ARRAY);

  ast_node *size_expr = decl->var_decl.type->array_size_expr;
  ASSERT_NE(size_expr, nullptr);
  EXPECT_EQ(size_expr->type, AST_NODE_TYPE_BINARY_OP);

  free_ast(node);
}

TEST_F(ParserTest, ParseCompoundLiteral) {
  setup_parser("void f() { (struct Point){1, 2}; }");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  ast_node *stmt = func->function_def.body->block.statements[0];

  EXPECT_EQ(stmt->type, AST_NODE_TYPE_COMPOUND_LITERAL);
  EXPECT_EQ(stmt->compound_literal.type->kind, TYPE_STRUCT);
  EXPECT_EQ(stmt->compound_literal.init_list->type, AST_NODE_TYPE_INIT_LIST);
  EXPECT_EQ(stmt->compound_literal.init_list->init_list.count, 2);

  free_ast(node);
}

TEST_F(ParserTest, ParseDesignatedInitializer) {
  setup_parser("struct Point p = { .x = 1, [0] = 5 };");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  ast_node *init = decl->var_decl.init_value;

  EXPECT_EQ(init->type, AST_NODE_TYPE_INIT_LIST);
  EXPECT_EQ(init->init_list.count, 2);

  EXPECT_STREQ(init->init_list.items[0].member_name, "x");
  EXPECT_EQ(init->init_list.items[0].index, nullptr);

  EXPECT_EQ(init->init_list.items[1].member_name, nullptr);
  ASSERT_NE(init->init_list.items[1].index, nullptr);
  EXPECT_EQ(init->init_list.items[1].index->type, AST_NODE_TYPE_NUMBER);

  free_ast(node);
}

TEST_F(ParserTest, ParseBool) {
  setup_parser("_Bool flag = 1;");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  EXPECT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(decl->var_decl.type->kind, TYPE_PRIMITIVE);
  EXPECT_EQ(decl->var_decl.type->prim, PRIM_BOOL);

  free_ast(node);
}

TEST_F(ParserTest, ParseVariadic) {
  setup_parser("int printf(const char *format, ...);");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  EXPECT_EQ(func->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(func->var_decl.type->is_variadic, 1);

  free_ast(node);
}

TEST_F(ParserTest, ParseMixedDeclarations) {
  setup_parser("void f() { int a = 1; a = 2; int b = 3; }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  ast_node *block = func->function_def.body;
  EXPECT_EQ(block->block.count, 3);
  EXPECT_EQ(block->block.statements[0]->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(block->block.statements[1]->type, AST_NODE_TYPE_ASSIGNMENT);
  EXPECT_EQ(block->block.statements[2]->type, AST_NODE_TYPE_VAR_DECL);
  free_ast(node);
}

TEST_F(ParserTest, ParseComplexVLA) {
  setup_parser("void f(int n, int m) { int arr[n * 2 + m]; }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  ast_node *block = func->function_def.body;
  ast_node *decl = block->block.statements[0];
  EXPECT_EQ(decl->var_decl.type->kind, TYPE_ARRAY);
  EXPECT_EQ(decl->var_decl.type->array_size_expr->type, AST_NODE_TYPE_BINARY_OP);
  free_ast(node);
}

TEST_F(ParserTest, ParseMultipleDesignatedInitializers) {
  setup_parser("struct Point p = { .x = 10, .y = 20 };");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  ast_node *init = decl->var_decl.init_value;
  EXPECT_EQ(init->init_list.count, 2);
  EXPECT_STREQ(init->init_list.items[0].member_name, "x");
  EXPECT_STREQ(init->init_list.items[1].member_name, "y");
  free_ast(node);
}

TEST_F(ParserTest, ParseArrayDesignatedInitializers) {
  setup_parser("int arr[10] = { [2] = 5, [5] = 8 };");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  ast_node *init = decl->var_decl.init_value;
  EXPECT_EQ(init->init_list.count, 2);
  ASSERT_NE(init->init_list.items[0].index, nullptr);
  EXPECT_STREQ(init->init_list.items[0].index->tok.value, "2");
  ASSERT_NE(init->init_list.items[1].index, nullptr);
  EXPECT_STREQ(init->init_list.items[1].index->tok.value, "5");
  free_ast(node);
}

TEST_F(ParserTest, ParseRestrictAndInline) {
  setup_parser("static inline void f(int * restrict p);");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *func_decl = node->program.declarations[0];
  EXPECT_EQ(func_decl->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(func_decl->var_decl.specs.storage_class, TOKEN_STATIC);
  EXPECT_EQ(func_decl->var_decl.specs.is_inline, 1);
  EXPECT_EQ(func_decl->var_decl.type->param_types[0]->is_restrict, 1);
  free_ast(node);
}

TEST_F(ParserTest, ParseEnumTrailingComma) {
  setup_parser("enum State { START, STOP, };");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  EXPECT_EQ(decl->type, AST_NODE_TYPE_ENUM_DEF);
  EXPECT_EQ(decl->enum_def.enumerator_count, 2);
  free_ast(node);
}

TEST_F(ParserTest, ParseFlexibleArrayMember) {
  setup_parser("struct Buffer { int length; char data[]; };");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  EXPECT_EQ(decl->type, AST_NODE_TYPE_STRUCT_DEF);
  EXPECT_EQ(decl->struct_def.member_count, 2);
  ast_node *fam = decl->struct_def.members[1];
  EXPECT_EQ(fam->var_decl.type->kind, TYPE_ARRAY);
  EXPECT_EQ(fam->var_decl.type->array_size, -1);
  EXPECT_EQ(fam->var_decl.type->array_size_expr, nullptr);
  free_ast(node);
}

TEST_F(ParserTest, ParseMultipleVariadicArgs) {
  setup_parser("int printf(int count, const char *format, ...);");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  EXPECT_EQ(func->var_decl.type->param_count, 2);
  EXPECT_EQ(func->var_decl.type->is_variadic, 1);
  free_ast(node);
}

TEST_F(ParserTest, ParseFunctionPointerDeclaration) {
  setup_parser("int (*fp)(int, int);");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(node->program.count, 1);

  ast_node *decl = node->program.declarations[0];
  ASSERT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(decl->var_decl.var_name, "fp");

  type_info *t = decl->var_decl.type;
  ASSERT_NE(t, nullptr);
  ASSERT_EQ(t->kind, TYPE_POINTER);
  ASSERT_EQ(t->ptr_to->kind, TYPE_FUNCTION);
  EXPECT_EQ(t->ptr_to->param_count, 2);
  EXPECT_EQ(t->ptr_to->param_types[0]->prim, PRIM_INT);
  EXPECT_EQ(t->ptr_to->param_types[1]->prim, PRIM_INT);
  ASSERT_EQ(t->ptr_to->ptr_to->kind, TYPE_PRIMITIVE);
  EXPECT_EQ(t->ptr_to->ptr_to->prim, PRIM_INT);

  free_ast(node);
}

TEST_F(ParserTest, ParsePointerToArray) {
  setup_parser("int (*b)[10];");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(node->program.count, 1);

  type_info *t = node->program.declarations[0]->var_decl.type;
  ASSERT_EQ(t->kind, TYPE_POINTER);
  ASSERT_EQ(t->ptr_to->kind, TYPE_ARRAY);
  EXPECT_STREQ(size_text(t->ptr_to), "10");
  EXPECT_EQ(t->ptr_to->ptr_to->kind, TYPE_PRIMITIVE);

  free_ast(node);
}

TEST_F(ParserTest, ParseArrayOfPointers) {
  setup_parser("int *a[10];");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  type_info *t = node->program.declarations[0]->var_decl.type;
  ASSERT_EQ(t->kind, TYPE_ARRAY);
  EXPECT_STREQ(size_text(t), "10");
  ASSERT_EQ(t->ptr_to->kind, TYPE_POINTER);
  EXPECT_EQ(t->ptr_to->ptr_to->kind, TYPE_PRIMITIVE);

  free_ast(node);
}

TEST_F(ParserTest, ParseMultiDimensionalArrayOrder) {
  setup_parser("int m[3][4];");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  type_info *t = node->program.declarations[0]->var_decl.type;
  ASSERT_EQ(t->kind, TYPE_ARRAY);
  EXPECT_STREQ(size_text(t), "3");
  ASSERT_EQ(t->ptr_to->kind, TYPE_ARRAY);
  EXPECT_STREQ(size_text(t->ptr_to), "4");
  EXPECT_EQ(t->ptr_to->ptr_to->kind, TYPE_PRIMITIVE);

  free_ast(node);
}

TEST_F(ParserTest, ParseArrayOfArraysOfPointers) {
  setup_parser("int *m[2][3];");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  type_info *t = node->program.declarations[0]->var_decl.type;
  ASSERT_EQ(t->kind, TYPE_ARRAY);
  EXPECT_STREQ(size_text(t), "2");
  ASSERT_EQ(t->ptr_to->kind, TYPE_ARRAY);
  EXPECT_STREQ(size_text(t->ptr_to), "3");
  EXPECT_EQ(t->ptr_to->ptr_to->kind, TYPE_POINTER);

  free_ast(node);
}

TEST_F(ParserTest, ParseArrayOfFunctionPointers) {
  setup_parser("int (*tbl[3])(void);");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  type_info *t = node->program.declarations[0]->var_decl.type;
  ASSERT_EQ(t->kind, TYPE_ARRAY);
  EXPECT_STREQ(size_text(t), "3");
  ASSERT_EQ(t->ptr_to->kind, TYPE_POINTER);
  ASSERT_EQ(t->ptr_to->ptr_to->kind, TYPE_FUNCTION);
  EXPECT_EQ(t->ptr_to->ptr_to->param_count, 0);
  EXPECT_EQ(t->ptr_to->ptr_to->ptr_to->prim, PRIM_INT);

  free_ast(node);
}

TEST_F(ParserTest, ParseTriplePointer) {
  setup_parser("char ***p;");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  type_info *t = node->program.declarations[0]->var_decl.type;
  ASSERT_EQ(t->kind, TYPE_POINTER);
  ASSERT_EQ(t->ptr_to->kind, TYPE_POINTER);
  ASSERT_EQ(t->ptr_to->ptr_to->kind, TYPE_POINTER);
  EXPECT_EQ(t->ptr_to->ptr_to->ptr_to->prim, PRIM_CHAR);

  free_ast(node);
}

TEST_F(ParserTest, ParseQualifiedPointer) {
  setup_parser("char * const restrict p;");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  type_info *t = node->program.declarations[0]->var_decl.type;
  ASSERT_EQ(t->kind, TYPE_POINTER);
  EXPECT_EQ(t->is_const, 1);
  EXPECT_EQ(t->is_restrict, 1);
  EXPECT_EQ(t->is_volatile, 0);
  EXPECT_EQ(t->ptr_to->prim, PRIM_CHAR);

  free_ast(node);
}

TEST_F(ParserTest, ParseFunctionPointerTypedefAndUse) {
  setup_parser("typedef int (*cb)(void); cb g;");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(node->program.count, 2);

  ast_node *td = node->program.declarations[0];
  ASSERT_EQ(td->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(td->var_decl.var_name, "cb");
  EXPECT_EQ(td->var_decl.specs.storage_class, TOKEN_TYPEDEF);
  ASSERT_EQ(td->var_decl.type->kind, TYPE_POINTER);
  EXPECT_EQ(td->var_decl.type->ptr_to->kind, TYPE_FUNCTION);

  ast_node *use = node->program.declarations[1];
  ASSERT_EQ(use->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(use->var_decl.var_name, "g");
  ASSERT_EQ(use->var_decl.type->kind, TYPE_TYPEDEF);
  EXPECT_STREQ(use->var_decl.type->tag_name, "cb");

  free_ast(node);
}

TEST_F(ParserTest, ParseFunctionPointerParameter) {
  setup_parser("void q(int (*cmp)(const void*, const void*));");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(node->program.count, 1);

  ast_node *func = node->program.declarations[0];
  ASSERT_EQ(func->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(func->var_decl.var_name, "q");
  ASSERT_EQ(func->var_decl.type->param_count, 1);
  EXPECT_STREQ(func->var_decl.type->param_names[0], "cmp");

  type_info *cmp = func->var_decl.type->param_types[0];
  ASSERT_EQ(cmp->kind, TYPE_POINTER);
  ASSERT_EQ(cmp->ptr_to->kind, TYPE_FUNCTION);
  EXPECT_EQ(cmp->ptr_to->param_count, 2);
  EXPECT_EQ(cmp->ptr_to->param_types[0]->kind, TYPE_POINTER);
  EXPECT_EQ(cmp->ptr_to->ptr_to->prim, PRIM_INT);

  free_ast(node);
}

TEST_F(ParserTest, ParsePointerToFunctionReturningPointerToArray) {
  setup_parser("int (*(*f)(void))[3];");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  type_info *t = node->program.declarations[0]->var_decl.type;
  ASSERT_EQ(t->kind, TYPE_POINTER);
  ASSERT_EQ(t->ptr_to->kind, TYPE_FUNCTION);
  EXPECT_EQ(t->ptr_to->param_count, 0);
  ASSERT_EQ(t->ptr_to->ptr_to->kind, TYPE_POINTER);
  ASSERT_EQ(t->ptr_to->ptr_to->ptr_to->kind, TYPE_ARRAY);
  EXPECT_STREQ(size_text(t->ptr_to->ptr_to->ptr_to), "3");

  free_ast(node);
}

TEST_F(ParserTest, ParseFunctionReturningFunctionPointer) {
  setup_parser("void (*signal(int sig, void (*h)(int)))(int);");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(node->program.count, 1);

  ast_node *func = node->program.declarations[0];
  ASSERT_EQ(func->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(func->var_decl.var_name, "signal");
  ASSERT_EQ(func->var_decl.type->param_count, 2);
  EXPECT_STREQ(func->var_decl.type->param_names[0], "sig");
  EXPECT_STREQ(func->var_decl.type->param_names[1], "h");

  type_info *h = func->var_decl.type->param_types[1];
  ASSERT_EQ(h->kind, TYPE_POINTER);
  ASSERT_EQ(h->ptr_to->kind, TYPE_FUNCTION);
  EXPECT_EQ(h->ptr_to->param_count, 1);

  type_info *ret = func->var_decl.type->ptr_to;
  ASSERT_EQ(ret->kind, TYPE_POINTER);
  ASSERT_EQ(ret->ptr_to->kind, TYPE_FUNCTION);
  EXPECT_EQ(ret->ptr_to->param_count, 1);
  EXPECT_EQ(ret->ptr_to->ptr_to->prim, PRIM_VOID);

  free_ast(node);
}

TEST_F(ParserTest, ParseStructMemberFunctionPointer) {
  setup_parser("struct S { int (*fn)(void); int x; };");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(node->program.count, 1);

  ast_node *def = node->program.declarations[0];
  ASSERT_EQ(def->type, AST_NODE_TYPE_STRUCT_DEF);
  ASSERT_EQ(def->struct_def.member_count, 2);

  ast_node *fn = def->struct_def.members[0];
  ASSERT_EQ(fn->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(fn->var_decl.var_name, "fn");
  ASSERT_EQ(fn->var_decl.type->kind, TYPE_POINTER);
  EXPECT_EQ(fn->var_decl.type->ptr_to->kind, TYPE_FUNCTION);

  free_ast(node);
}

TEST_F(ParserTest, ParseVoidParameterListIsEmpty) {
  setup_parser("int f(void);");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  ASSERT_EQ(func->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(func->var_decl.type->param_count, 0);

  free_ast(node);
}

TEST_F(ParserTest, ParseEmptyParameterList) {
  setup_parser("int f();");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  ASSERT_EQ(func->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(func->var_decl.type->param_count, 0);

  free_ast(node);
}

TEST_F(ParserTest, ParseUnnamedParameters) {
  setup_parser("int f(int, char *);");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  ASSERT_EQ(func->var_decl.type->param_count, 2);
  EXPECT_EQ(func->var_decl.type->param_names[0], nullptr);
  EXPECT_EQ(func->var_decl.type->param_names[1], nullptr);
  EXPECT_EQ(func->var_decl.type->param_types[0]->prim, PRIM_INT);
  EXPECT_EQ(func->var_decl.type->param_types[1]->kind, TYPE_POINTER);

  free_ast(node);
}

TEST_F(ParserTest, ParseStaticArrayParameter) {
  setup_parser("void f(int a[static 4]);");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  ASSERT_EQ(func->var_decl.type->param_count, 1);
  EXPECT_STREQ(func->var_decl.type->param_names[0], "a");
  ASSERT_EQ(func->var_decl.type->param_types[0]->kind, TYPE_ARRAY);
  EXPECT_STREQ(size_text(func->var_decl.type->param_types[0]), "4");

  free_ast(node);
}

TEST_F(ParserTest, ParseEmptyStatement) {
  setup_parser("int f(void) { ; return 0; }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(node->program.count, 1);

  ast_node *body = node->program.declarations[0]->function_def.body;
  ASSERT_NE(body, nullptr);
  ASSERT_EQ(body->block.count, 2);
  EXPECT_EQ(body->block.statements[0]->type, AST_NODE_TYPE_EMPTY);
  EXPECT_EQ(body->block.statements[1]->type, AST_NODE_TYPE_RETURN);

  free_ast(node);
}

TEST_F(ParserTest, ParseStrayExtraSemicolon) {
  setup_parser("int f(void) { int x = 1;; return x; }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *body = node->program.declarations[0]->function_def.body;
  ASSERT_NE(body, nullptr);
  ASSERT_EQ(body->block.count, 3);
  EXPECT_EQ(body->block.statements[1]->type, AST_NODE_TYPE_EMPTY);

  free_ast(node);
}

TEST_F(ParserTest, ParseReturnWithoutValue) {
  setup_parser("void f(void) { return; }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *body = node->program.declarations[0]->function_def.body;
  ASSERT_EQ(body->block.count, 1);
  ASSERT_EQ(body->block.statements[0]->type, AST_NODE_TYPE_RETURN);
  EXPECT_EQ(body->block.statements[0]->return_stmt.return_value, nullptr);

  free_ast(node);
}

TEST_F(ParserTest, ParseEmptyForClauses) {
  setup_parser("void f(void) { for (;;) { break; } }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *body = node->program.declarations[0]->function_def.body;
  ASSERT_EQ(body->block.count, 1);
  ast_node *loop = body->block.statements[0];
  ASSERT_EQ(loop->type, AST_NODE_TYPE_FOR);
  EXPECT_EQ(loop->for_stmt.init, nullptr);
  EXPECT_EQ(loop->for_stmt.condition, nullptr);
  EXPECT_EQ(loop->for_stmt.increment, nullptr);

  free_ast(node);
}

TEST_F(ParserTest, ValidProgramReportsNoError) {
  setup_parser("typedef struct Node { int v; struct Node *next; } Node;\n"
               "static int g = 5, arr[10];\n"
               "int add(int a, int b) { return a + b; }\n"
               "int (*fp)(int, int);\n"
               "char *dup(const char *s);\n"
               "void loop(void) {\n"
               "    for (int i = 0; i < 10; i++) { if (i % 2 == 0) continue; g += i; }\n"
               "    do { g--; } while (g > 0);\n"
               "    switch (g) { case 1: break; default: break; }\n"
               "    ;\n"
               "}\n"
               "int main(int argc, char **argv) {\n"
               "    Node n = { .v = 1, .next = 0 };\n"
               "    return add(sizeof(Node), argc) + n.v + (argv ? 1 : 0);\n"
               "}\n");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  EXPECT_EQ(node->program.count, 7);

  free_ast(node);
}

TEST_F(ParserTest, ErrorMissingSemicolonAfterReturn) {
  setup_parser("int f(void) { return 1 }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 1);
  EXPECT_EQ(node->program.count, 0);

  free_ast(node);
}

TEST_F(ParserTest, ErrorMissingCloseParenInParameterList) {
  setup_parser("int f(void { return 1; }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_GE(p.had_error, 1);
  EXPECT_EQ(node->program.count, 0);

  free_ast(node);
}

TEST_F(ParserTest, ErrorUnclosedBlock) {
  setup_parser("int f(void) { return 1;");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 1);
  EXPECT_EQ(node->program.count, 0);

  free_ast(node);
}

TEST_F(ParserTest, ErrorMissingColonAfterCase) {
  setup_parser("void f(void) { switch (1) { case 2 break; } }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_GE(p.had_error, 1);

  free_ast(node);
}

TEST_F(ParserTest, ErrorMissingExpression) {
  setup_parser("int f(void) { int x = ; return x; }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_GE(p.had_error, 1);

  free_ast(node);
}

TEST_F(ParserTest, ErrorMissingSemicolonAfterDeclaration) {
  setup_parser("int x = 5");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_GE(p.had_error, 1);
  EXPECT_EQ(node->program.count, 0);

  free_ast(node);
}

TEST_F(ParserTest, ErrorCountsIndependentMistakes) {
  setup_parser("int a(void) { return 1 }\nint b(void) { return 2 }\n");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 2);
  EXPECT_EQ(node->program.count, 0);

  free_ast(node);
}

TEST_F(ParserTest, RecoveryContinuesAfterBadDeclaration) {
  setup_parser("int broken(void) { return 1 }\nint fine(int a) { return a; }\n");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 1);
  ASSERT_EQ(node->program.count, 1);

  ast_node *func = node->program.declarations[0];
  ASSERT_EQ(func->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_STREQ(func->function_def.name, "fine");

  free_ast(node);
}

TEST_F(ParserTest, RecoveryDoesNotHoistLocalsToFileScope) {
  setup_parser("int broken(undeclared_type x) { int inner_local = 5; return 0; }\n"
               "int fine(void) { return 0; }\n");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 1);
  ASSERT_EQ(node->program.count, 1);

  ast_node *survivor = node->program.declarations[0];
  ASSERT_EQ(survivor->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_STREQ(survivor->function_def.name, "fine");

  free_ast(node);
}

TEST_F(ParserTest, ParseCastToQualifiedPointer) {
  setup_parser("char *f(void *p) { return (const char *)p; }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(node->program.count, 1);

  ast_node *body = node->program.declarations[0]->function_def.body;
  ASSERT_EQ(body->block.count, 1);
  ast_node *ret = body->block.statements[0];
  ASSERT_EQ(ret->type, AST_NODE_TYPE_RETURN);
  ASSERT_NE(ret->return_stmt.return_value, nullptr);
  ASSERT_EQ(ret->return_stmt.return_value->type, AST_NODE_TYPE_CAST);

  type_info *t = ret->return_stmt.return_value->cast_expr.type;
  ASSERT_EQ(t->kind, TYPE_POINTER);
  EXPECT_EQ(t->ptr_to->is_const, 1);
  EXPECT_EQ(t->ptr_to->prim, PRIM_CHAR);

  free_ast(node);
}

TEST_F(ParserTest, ParseCastToVolatilePointer) {
  setup_parser("int f(void *p) { return *(volatile int *)p; }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  free_ast(node);
}

TEST_F(ParserTest, ParseCastToBool) {
  setup_parser("int f(int x) { return (_Bool)x; }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *body = node->program.declarations[0]->function_def.body;
  ast_node *val = body->block.statements[0]->return_stmt.return_value;
  ASSERT_EQ(val->type, AST_NODE_TYPE_CAST);
  EXPECT_EQ(val->cast_expr.type->prim, PRIM_BOOL);

  free_ast(node);
}

TEST_F(ParserTest, ParseSizeofQualifiedType) {
  setup_parser("int f(void) { return sizeof(const char *); }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *body = node->program.declarations[0]->function_def.body;
  ast_node *val = body->block.statements[0]->return_stmt.return_value;
  ASSERT_EQ(val->type, AST_NODE_TYPE_UNARY_OP);
  EXPECT_EQ(val->unary_op.op.type, TOKEN_SIZEOF);
  ASSERT_NE(val->unary_op.operand, nullptr);
  ASSERT_EQ(val->unary_op.operand->type, AST_NODE_TYPE_CAST);
  EXPECT_EQ(val->unary_op.operand->cast_expr.type->kind, TYPE_POINTER);

  free_ast(node);
}

TEST_F(ParserTest, ParseSizeofUnionType) {
  setup_parser("union U { int a; }; int f(void) { return sizeof(union U); }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  EXPECT_EQ(node->program.count, 2);
  free_ast(node);
}

TEST_F(ParserTest, ParseSizeofExpressionStillWorks) {
  setup_parser("int f(int x) { return sizeof x; }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *body = node->program.declarations[0]->function_def.body;
  ast_node *val = body->block.statements[0]->return_stmt.return_value;
  ASSERT_EQ(val->type, AST_NODE_TYPE_UNARY_OP);
  ASSERT_EQ(val->unary_op.operand->type, AST_NODE_TYPE_IDENTIFIER);

  free_ast(node);
}

TEST_F(ParserTest, ParseBitfields) {
  setup_parser("struct S { unsigned a : 3; unsigned b : 5; int c; };");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(node->program.count, 1);

  ast_node *def = node->program.declarations[0];
  ASSERT_EQ(def->type, AST_NODE_TYPE_STRUCT_DEF);
  ASSERT_EQ(def->struct_def.member_count, 3);

  ast_node *a = def->struct_def.members[0];
  EXPECT_STREQ(a->var_decl.var_name, "a");
  ASSERT_NE(a->var_decl.bitfield_width, nullptr);
  EXPECT_STREQ(a->var_decl.bitfield_width->tok.value, "3");

  ast_node *b = def->struct_def.members[1];
  EXPECT_STREQ(b->var_decl.var_name, "b");
  ASSERT_NE(b->var_decl.bitfield_width, nullptr);
  EXPECT_STREQ(b->var_decl.bitfield_width->tok.value, "5");

  ast_node *c = def->struct_def.members[2];
  EXPECT_STREQ(c->var_decl.var_name, "c");
  EXPECT_EQ(c->var_decl.bitfield_width, nullptr);

  free_ast(node);
}

TEST_F(ParserTest, ParseAnonymousBitfieldPadding) {
  setup_parser("struct S { unsigned a : 3; unsigned : 5; };");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *def = node->program.declarations[0];
  ASSERT_EQ(def->struct_def.member_count, 2);

  ast_node *pad = def->struct_def.members[1];
  EXPECT_EQ(pad->var_decl.var_name, nullptr);
  ASSERT_NE(pad->var_decl.bitfield_width, nullptr);
  EXPECT_STREQ(pad->var_decl.bitfield_width->tok.value, "5");

  free_ast(node);
}

TEST_F(ParserTest, ParseAdjacentStringLiteralsAreJoined) {
  setup_parser("const char *s = \"a\" \"b\" \"c\";");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(node->program.count, 1);

  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_NE(init, nullptr);
  ASSERT_EQ(init->type, AST_NODE_TYPE_STRING);
  EXPECT_STREQ(init->tok.value, "abc");

  free_ast(node);
}

TEST_F(ParserTest, ParseSingleStringLiteralUnchanged) {
  setup_parser("const char *s = \"solo\";");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_GE(node->program.count, 1);
  EXPECT_STREQ(node->program.declarations[0]->var_decl.init_value->tok.value, "solo");
  free_ast(node);
}

TEST_F(ParserTest, ParseLeadingDotFloat) {
  setup_parser("double d = .5;");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_NE(init, nullptr);
  ASSERT_EQ(init->type, AST_NODE_TYPE_NUMBER);
  EXPECT_STREQ(init->tok.value, ".5");

  free_ast(node);
}

TEST_F(ParserTest, ParseTrailingDotFloat) {
  setup_parser("double f(void) { return .5 + 1.; }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  free_ast(node);
}

TEST_F(ParserTest, ParseWideStringLiteral) {
  setup_parser("const char *w = L\"hi\";");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(init->type, AST_NODE_TYPE_STRING);
  EXPECT_STREQ(init->tok.value, "hi");

  free_ast(node);
}

TEST_F(ParserTest, ParseWideCharLiteral) {
  setup_parser("int c = L'x';");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(init->type, AST_NODE_TYPE_CHAR_LITERAL);
  EXPECT_STREQ(init->tok.value, "x");

  free_ast(node);
}

TEST_F(ParserTest, IdentifierStartingWithLIsNotAWideLiteral) {
  setup_parser("int L = 1; int Lx = 2;");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(node->program.count, 2);
  EXPECT_STREQ(node->program.declarations[0]->var_decl.var_name, "L");
  EXPECT_STREQ(node->program.declarations[1]->var_decl.var_name, "Lx");
  free_ast(node);
}

TEST_F(ParserTest, EllipsisStillLexesAfterDotFloatChange) {
  setup_parser("int printf(const char *f, ...);");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_GE(node->program.count, 1);
  EXPECT_EQ(node->program.declarations[0]->var_decl.type->is_variadic, 1);
  free_ast(node);
}

TEST_F(ParserTest, MemberAccessDotStillWorks) {
  setup_parser("struct S { int a; }; int f(struct S *s) { return s->a + (*s).a; }");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  free_ast(node);
}

TEST_F(ParserTest, ForwardStructDeclaration) {
  setup_parser("struct S;");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_GE(node->program.count, 1);
  ast_node *d = node->program.declarations[0];
  ASSERT_EQ(d->type, AST_NODE_TYPE_STRUCT_DEF);
  EXPECT_STREQ(d->struct_def.tag_name, "S");
  EXPECT_EQ(d->struct_def.member_count, 0);
  EXPECT_EQ(d->struct_def.is_forward, 1);
  free_ast(node);
}

TEST_F(ParserTest, ForwardUnionDeclaration) {
  setup_parser("union U;");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_GE(node->program.count, 1);
  EXPECT_EQ(node->program.declarations[0]->struct_def.is_forward, 1);
  free_ast(node);
}

TEST_F(ParserTest, ForwardDeclarationThenDefinition) {
  setup_parser("struct S; struct S { int x; };");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_GE(node->program.count, 2);
  EXPECT_EQ(node->program.declarations[0]->struct_def.is_forward, 1);
  EXPECT_EQ(node->program.declarations[1]->struct_def.is_forward, 0);
  EXPECT_EQ(node->program.declarations[1]->struct_def.member_count, 1);
  free_ast(node);
}

TEST_F(ParserTest, PointerToIncompleteStruct) {
  setup_parser("struct S; struct S *p;");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  free_ast(node);
}

TEST_F(ParserTest, NestedDesignatorDesugarsToNestedInitList) {
  setup_parser("struct I { int b; }; struct O { struct I i; }; struct O o = {.i.b = 1};");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_GE(node->program.count, 3);

  ast_node *init = node->program.declarations[2]->var_decl.init_value;
  ASSERT_NE(init, nullptr);
  ASSERT_EQ(init->type, AST_NODE_TYPE_INIT_LIST);
  ASSERT_EQ(init->init_list.count, 1);
  EXPECT_STREQ(init->init_list.items[0].member_name, "i");

  ast_node *inner = init->init_list.items[0].value;
  ASSERT_NE(inner, nullptr);
  ASSERT_EQ(inner->type, AST_NODE_TYPE_INIT_LIST)
      << ".i.b = 1 means .i = {.b = 1}; the chain must nest";
  ASSERT_EQ(inner->init_list.count, 1);
  EXPECT_STREQ(inner->init_list.items[0].member_name, "b");
  EXPECT_STREQ(inner->init_list.items[0].value->tok.value, "1");
  free_ast(node);
}

TEST_F(ParserTest, MixedDesignatorIndexThenField) {
  setup_parser("struct S { int x; }; struct S a[2] = {[0].x = 5};");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_GE(node->program.count, 2);

  ast_node *init = node->program.declarations[1]->var_decl.init_value;
  ASSERT_EQ(init->type, AST_NODE_TYPE_INIT_LIST);
  ASSERT_EQ(init->init_list.count, 1);
  ASSERT_NE(init->init_list.items[0].index, nullptr);
  EXPECT_STREQ(init->init_list.items[0].index->tok.value, "0");

  ast_node *inner = init->init_list.items[0].value;
  ASSERT_EQ(inner->type, AST_NODE_TYPE_INIT_LIST);
  EXPECT_STREQ(inner->init_list.items[0].member_name, "x");
  free_ast(node);
}

TEST_F(ParserTest, MissingMemberNameAfterDotIsAnError) {
  setup_parser("struct S { int x; }; struct S s = {. = 1};");
  ast_node *node = parse_program(&p);
  EXPECT_EQ(p.had_error, 2) << "one diagnostic for the missing member name and one for the "
                               "expression that follows; a count of 1 means the designator itself "
                               "was accepted silently";
  free_ast(node);
}

TEST_F(ParserTest, MissingBracketAfterArrayDesignatorIsAnError) {
  setup_parser("int a[2] = {[0 = 1};");
  ast_node *node = parse_program(&p);
  EXPECT_EQ(p.had_error, 2);
  free_ast(node);
}

TEST_F(ParserTest, StringLiteralDecodesEscapes) {
  setup_parser("char *s = \"a\\nb\";");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(init->type, AST_NODE_TYPE_STRING);
  ASSERT_EQ(init->literal.length, 3);
  EXPECT_EQ(init->literal.bytes[0], 'a');
  EXPECT_EQ(init->literal.bytes[1], '\n');
  EXPECT_EQ(init->literal.bytes[2], 'b');
  free_ast(node);
}

TEST_F(ParserTest, StringLiteralKeepsItsRawSpelling) {
  setup_parser("char *s = \"a\\nb\";");
  ast_node *node = parse_program(&p);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  EXPECT_STREQ(init->tok.value, "a\\nb")
      << "the raw spelling must survive so the preprocessor can stringize it";
  free_ast(node);
}

TEST_F(ParserTest, StringLiteralCanContainAnEmbeddedNul) {
  setup_parser("char *s = \"a\\0b\";");
  ast_node *node = parse_program(&p);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(init->literal.length, 3)
      << "a decoded string needs an explicit length; strlen would stop at 1";
  EXPECT_EQ(init->literal.bytes[1], '\0');
  EXPECT_EQ(init->literal.bytes[2], 'b');
  free_ast(node);
}

TEST_F(ParserTest, OctalAndHexEscapes) {
  setup_parser("char *s = \"\\101\\x42\";");
  ast_node *node = parse_program(&p);
  EXPECT_EQ(p.had_error, 0);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(init->literal.length, 2);
  EXPECT_EQ(init->literal.bytes[0], 'A');
  EXPECT_EQ(init->literal.bytes[1], 'B');
  free_ast(node);
}

TEST_F(ParserTest, AllSimpleEscapesDecode) {
  setup_parser("char *s = \"\\a\\b\\f\\v\\r\\t\\\\\\'\\\"\\?\";");
  ast_node *node = parse_program(&p);
  EXPECT_EQ(p.had_error, 0);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(init->literal.length, 10);
  EXPECT_EQ(init->literal.bytes[0], '\a');
  EXPECT_EQ(init->literal.bytes[1], '\b');
  EXPECT_EQ(init->literal.bytes[2], '\f');
  EXPECT_EQ(init->literal.bytes[3], '\v');
  EXPECT_EQ(init->literal.bytes[4], '\r');
  EXPECT_EQ(init->literal.bytes[5], '\t');
  EXPECT_EQ(init->literal.bytes[6], '\\');
  EXPECT_EQ(init->literal.bytes[7], '\'');
  EXPECT_EQ(init->literal.bytes[8], '"');
  EXPECT_EQ(init->literal.bytes[9], '?');
  free_ast(node);
}

TEST_F(ParserTest, ConcatenationJoinsTheDecodedPieces) {
  setup_parser("char *s = \"a\\n\" \"b\";");
  ast_node *node = parse_program(&p);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(init->literal.length, 3);
  EXPECT_EQ(init->literal.bytes[1], '\n');
  free_ast(node);
}

TEST_F(ParserTest, EachConcatenatedPieceIsDecodedOnItsOwn) {
  setup_parser("char *s = \"\\x1\" \"2\";");
  ast_node *node = parse_program(&p);
  EXPECT_EQ(p.had_error, 0);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(init->literal.length, 2) << "\\x1 then 2, not the single escape \\x12";
  EXPECT_EQ(init->literal.bytes[0], 1);
  EXPECT_EQ(init->literal.bytes[1], '2');
  EXPECT_EQ(init->literal.bytes[2], 0);
  free_ast(node);
}

TEST_F(ParserTest, EveryPieceOfAWideConcatenationIsDecodedWide) {
  setup_parser("int *s = \"\\xffff\" L\"a\" \"\\u20ac\";");
  ast_node *node = parse_program(&p);
  EXPECT_EQ(p.had_error, 0) << "\\xffff is out of range only for a narrow literal";
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  EXPECT_EQ(init->literal.is_wide, 1);
  ASSERT_EQ(init->literal.length, 3);
  EXPECT_EQ(std::vector<char>(init->literal.bytes, init->literal.bytes + 8),
            std::vector<char>({'\xff', '\xff', 'a', 0, '\xac', 0x20, 0, 0}));
  free_ast(node);
}

TEST_F(ParserTest, WideLiteralsOnLinuxHoldThirtyTwoBitUnits) {
  TargetGuard guard(TARGET_LINUX_X64);
  std::vector<char> stale(4 * (20 + 1), '\xaa');
  char *recycled = (char *)malloc(stale.size());
  ASSERT_NE(recycled, nullptr);
  memcpy(recycled, stale.data(), stale.size());
  free(recycled);
  setup_parser("int *s = L\"\\U0001F600\" L\"\\xffffffff\"; int c = L'\\u20ac';");
  ast_node *node = parse_program(&p);
  EXPECT_EQ(p.had_error, 0) << "\\xffffffff fits a 32-bit unit";
  ast_node *s = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(s->literal.length, 2) << "one unit per character, no surrogate pair";
  EXPECT_EQ(
      std::vector<char>(s->literal.bytes, s->literal.bytes + 12),
      std::vector<char>({0x00, '\xf6', 0x01, 0x00, '\xff', '\xff', '\xff', '\xff', 0, 0, 0, 0}))
      << "the terminator is a whole 4-byte unit";
  ast_node *c = node->program.declarations[1]->var_decl.init_value;
  ASSERT_EQ(c->literal.length, 1);
  EXPECT_EQ(std::vector<char>(c->literal.bytes, c->literal.bytes + 8),
            std::vector<char>({'\xac', 0x20, 0, 0, 0, 0, 0, 0}));
  free_ast(node);
}

TEST_F(ParserTest, WideLiteralsHoldSixteenBitUnits) {
  setup_parser("int *s = L\"\\U0001F600\"; int c = L'\\u20ac';");
  ast_node *node = parse_program(&p);
  EXPECT_EQ(p.had_error, 0);
  ast_node *s = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(s->literal.length, 2);
  EXPECT_EQ(std::vector<char>(s->literal.bytes, s->literal.bytes + 6),
            std::vector<char>({0x3d, '\xd8', 0x00, '\xde', 0, 0}));
  ast_node *c = node->program.declarations[1]->var_decl.init_value;
  ASSERT_EQ(c->literal.length, 1);
  EXPECT_EQ(std::vector<char>(c->literal.bytes, c->literal.bytes + 4),
            std::vector<char>({'\xac', 0x20, 0, 0}));
  free_ast(node);
}

TEST_F(ParserTest, AMultiCharacterConstantKeepsEveryCharacter) {
  setup_parser("int c = 'ab';");
  ast_node *node = parse_program(&p);
  EXPECT_EQ(p.had_error, 0);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(init->type, AST_NODE_TYPE_CHAR_LITERAL);
  ASSERT_EQ(init->literal.length, 2);
  EXPECT_EQ(init->literal.bytes[0], 'a');
  EXPECT_EQ(init->literal.bytes[1], 'b');
  free_ast(node);
}

TEST_F(ParserTest, ALiteralErrorPointsAtTheLiteralThatHasIt) {
  setup_parser("char *s = \"ok\"\n  \"\\q\" \"\\x\";\nint c = '\\777', d;");
  testing::internal::CaptureStderr();
  ast_node *node = parse_program(&p);
  std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(p.had_error, 3);
  EXPECT_EQ(diagnostics, "2:3: error: unknown escape sequence in literal (at '\\q')\n"
                         "2:8: error: \\x used with no following hex digits (at '\\x')\n"
                         "3:9: error: octal escape sequence out of range (at '\\777')\n");
  free_ast(node);
}

TEST_F(ParserTest, EveryNumberIsCheckedWhenItBecomesAToken) {
  setup_parser("void f(void) {\n  int a = 08;\n  double b = 1e + 1.5f;\n  a = 0x1p-3 + 0x;\n}\n");
  testing::internal::CaptureStderr();
  ast_node *node = parse_program(&p);
  std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(p.had_error, 3);
  EXPECT_EQ(diagnostics, "2:11: error: invalid digit in an octal constant (at '08')\n"
                         "3:14: error: exponent has no digits (at '1e')\n"
                         "4:16: error: hexadecimal constant has no digits (at '0x')\n");
  free_ast(node);
}

TEST_F(ParserTest, WideStringIsMarkedWide) {
  setup_parser("char *s = L\"hi\";");
  ast_node *node = parse_program(&p);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  EXPECT_EQ(init->literal.is_wide, 1);
  EXPECT_EQ(init->literal.length, 2);
  free_ast(node);
}

TEST_F(ParserTest, NarrowStringIsNotMarkedWide) {
  setup_parser("char *s = \"hi\";");
  ast_node *node = parse_program(&p);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  EXPECT_EQ(init->literal.is_wide, 0);
  free_ast(node);
}

TEST_F(ParserTest, ConcatenationWithAWidePartIsWide) {
  setup_parser("char *s = L\"a\" \"b\";");
  ast_node *node = parse_program(&p);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  EXPECT_EQ(init->literal.is_wide, 1);
  EXPECT_EQ(init->literal.length, 2);
  free_ast(node);
}

TEST_F(ParserTest, CharLiteralDecodes) {
  setup_parser("char c = '\\n';");
  ast_node *node = parse_program(&p);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(init->type, AST_NODE_TYPE_CHAR_LITERAL);
  ASSERT_EQ(init->literal.length, 1);
  EXPECT_EQ(init->literal.bytes[0], '\n');
  EXPECT_EQ(init->literal.is_wide, 0);
  free_ast(node);
}

TEST_F(ParserTest, WideCharLiteralIsMarkedWide) {
  setup_parser("int c = L'x';");
  ast_node *node = parse_program(&p);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(init->type, AST_NODE_TYPE_CHAR_LITERAL);
  EXPECT_EQ(init->literal.is_wide, 1);
  EXPECT_EQ(init->literal.bytes[0], 'x');
  free_ast(node);
}

TEST_F(ParserTest, UnknownEscapeIsAnError) {
  setup_parser("char *s = \"\\q\";");
  ast_node *node = parse_program(&p);
  EXPECT_GT(p.had_error, 0);
  free_ast(node);
}

TEST_F(ParserTest, HexEscapeWithoutDigitsIsAnError) {
  setup_parser("char *s = \"\\xZZ\";");
  ast_node *node = parse_program(&p);
  EXPECT_GT(p.had_error, 0);
  free_ast(node);
}

TEST_F(ParserTest, OctalEscapeOutOfRangeIsAnError) {
  setup_parser("char *s = \"\\777\";");
  ast_node *node = parse_program(&p);
  EXPECT_GT(p.had_error, 0);
  free_ast(node);
}

static bool differ(std::string &why, const std::string &at, const char *what) {
  why = at + ": " + what;
  return false;
}

static bool same_str(const char *a, const char *b) {
  if (a == nullptr || b == nullptr)
    return a == b;
  return strcmp(a, b) == 0;
}

static bool same_type(const type_info *a, const type_info *b, const std::string &at,
                      std::string &why) {
  if (a == nullptr || b == nullptr)
    return a == b || differ(why, at, "one type is null");
  if (a->kind != b->kind || a->is_const != b->is_const || a->is_volatile != b->is_volatile ||
      a->is_restrict != b->is_restrict || a->array_size != b->array_size ||
      a->param_count != b->param_count || a->is_variadic != b->is_variadic)
    return differ(why, at, "type fields differ");
  if (a->prim != b->prim || a->is_complex != b->is_complex || a->is_imaginary != b->is_imaginary)
    return differ(why, at, "arithmetic types differ");
  if (!same_str(a->tag_name, b->tag_name))
    return differ(why, at, "tag names differ");
  if (!same_type(a->ptr_to, b->ptr_to, at + "/ptr_to", why))
    return false;
  for (int i = 0; i < a->param_count; i++) {
    if (!same_type(a->param_types[i], b->param_types[i], at + "/param" + std::to_string(i), why))
      return false;
  }
  return true;
}

static bool same_ast(const ast_node *a, const ast_node *b, const std::string &at, std::string &why);

static bool same_ast_compares_locations = true;

struct ShapeOnly {
  ShapeOnly() {
    same_ast_compares_locations = false;
  }
  ~ShapeOnly() {
    same_ast_compares_locations = true;
  }
};

static bool same_list(ast_node **a, ast_node **b, int n, const std::string &at, std::string &why) {
  for (int i = 0; i < n; i++) {
    if (!same_ast(a[i], b[i], at + "[" + std::to_string(i) + "]", why))
      return false;
  }
  return true;
}

static bool same_ast(const ast_node *a, const ast_node *b, const std::string &at,
                     std::string &why) {
  if (a == nullptr || b == nullptr)
    return a == b || differ(why, at, "one node is null");
  if (a->type != b->type)
    return differ(why, at, "node types differ");
  if (a->tok.type != b->tok.type || !same_str(a->tok.value, b->tok.value))
    return differ(why, at, "node tokens differ");
  if (same_ast_compares_locations && (a->loc.line != b->loc.line || a->loc.column != b->loc.column))
    return differ(why, at, "node locations differ");

  switch (a->type) {
  case AST_NODE_TYPE_PROGRAM:
    if (a->program.count != b->program.count)
      return differ(why, at, "declaration counts differ");
    return same_list(a->program.declarations, b->program.declarations, a->program.count, at, why);
  case AST_NODE_TYPE_BLOCK:
  case AST_NODE_TYPE_DECL_GROUP:
    if (a->block.count != b->block.count)
      return differ(why, at, "statement counts differ");
    return same_list(a->block.statements, b->block.statements, a->block.count, at, why);
  case AST_NODE_TYPE_FUNCTION_DEF:
    if (!same_str(a->function_def.name, b->function_def.name) ||
        a->function_def.type->param_count != b->function_def.type->param_count ||
        a->function_def.specs.storage_class != b->function_def.specs.storage_class ||
        a->function_def.specs.is_inline != b->function_def.specs.is_inline)
      return differ(why, at, "function signatures differ");
    for (int i = 0; i < a->function_def.type->param_count; i++) {
      if (!same_str(a->function_def.type->param_names[i], b->function_def.type->param_names[i]))
        return differ(why, at, "parameter names differ");
      if (!same_type(a->function_def.type->param_types[i], b->function_def.type->param_types[i],
                     at + "/param" + std::to_string(i), why))
        return false;
    }
    return same_type(a->function_def.type->ptr_to, b->function_def.type->ptr_to, at + "/return",
                     why) &&
           same_ast(a->function_def.body, b->function_def.body, at + "/body", why);
  case AST_NODE_TYPE_VAR_DECL:
    if (!same_str(a->var_decl.var_name, b->var_decl.var_name) ||
        a->var_decl.specs.storage_class != b->var_decl.specs.storage_class ||
        a->var_decl.specs.is_inline != b->var_decl.specs.is_inline)
      return differ(why, at, "declarations differ");
    return same_type(a->var_decl.type, b->var_decl.type, at + "/type", why) &&
           same_ast(a->var_decl.init_value, b->var_decl.init_value, at + "/init", why) &&
           same_ast(a->var_decl.bitfield_width, b->var_decl.bitfield_width, at + "/width", why);
  case AST_NODE_TYPE_IF:
    return same_ast(a->if_stmt.condition, b->if_stmt.condition, at + "/cond", why) &&
           same_ast(a->if_stmt.then_branch, b->if_stmt.then_branch, at + "/then", why) &&
           same_ast(a->if_stmt.else_branch, b->if_stmt.else_branch, at + "/else", why);
  case AST_NODE_TYPE_WHILE:
    return same_ast(a->while_stmt.condition, b->while_stmt.condition, at + "/cond", why) &&
           same_ast(a->while_stmt.body, b->while_stmt.body, at + "/body", why);
  case AST_NODE_TYPE_FOR:
    return same_ast(a->for_stmt.init, b->for_stmt.init, at + "/init", why) &&
           same_ast(a->for_stmt.condition, b->for_stmt.condition, at + "/cond", why) &&
           same_ast(a->for_stmt.increment, b->for_stmt.increment, at + "/incr", why) &&
           same_ast(a->for_stmt.body, b->for_stmt.body, at + "/body", why);
  case AST_NODE_TYPE_RETURN:
    return same_ast(a->return_stmt.return_value, b->return_stmt.return_value, at + "/value", why);
  case AST_NODE_TYPE_BINARY_OP:
    if (a->binary_op.op.type != b->binary_op.op.type)
      return differ(why, at, "binary operators differ");
    return same_ast(a->binary_op.left, b->binary_op.left, at + "/left", why) &&
           same_ast(a->binary_op.right, b->binary_op.right, at + "/right", why);
  case AST_NODE_TYPE_UNARY_OP:
    if (a->unary_op.op.type != b->unary_op.op.type ||
        a->unary_op.is_postfix != b->unary_op.is_postfix)
      return differ(why, at, "unary operators differ");
    return same_ast(a->unary_op.operand, b->unary_op.operand, at + "/operand", why);
  case AST_NODE_TYPE_ASSIGNMENT:
    if (a->assignment.op.type != b->assignment.op.type)
      return differ(why, at, "assignment operators differ");
    return same_ast(a->assignment.left, b->assignment.left, at + "/left", why) &&
           same_ast(a->assignment.right, b->assignment.right, at + "/right", why);
  case AST_NODE_TYPE_FUNCTION_CALL:
    if (a->function_call.arg_count != b->function_call.arg_count)
      return differ(why, at, "argument counts differ");
    return same_ast(a->function_call.callable, b->function_call.callable, at + "/callee", why) &&
           same_list(a->function_call.arguments, b->function_call.arguments,
                     a->function_call.arg_count, at + "/args", why);
  case AST_NODE_TYPE_STRING:
    if (a->literal.length != b->literal.length || a->literal.is_wide != b->literal.is_wide ||
        memcmp(a->literal.bytes, b->literal.bytes, a->literal.length) != 0)
      return differ(why, at, "string literals differ");
    return true;
  case AST_NODE_TYPE_STRUCT_DEF:
    if (!same_str(a->struct_def.tag_name, b->struct_def.tag_name) ||
        a->struct_def.is_union != b->struct_def.is_union ||
        a->struct_def.member_count != b->struct_def.member_count)
      return differ(why, at, "records differ");
    return same_list(a->struct_def.members, b->struct_def.members, a->struct_def.member_count,
                     at + "/members", why);
  case AST_NODE_TYPE_NUMBER:
  case AST_NODE_TYPE_IDENTIFIER:
    return true;
  default:
    return differ(why, at,
                  ("comparator does not handle node type " + std::to_string(a->type)).c_str());
  }
}

class BridgeTest : public ::testing::Test {
protected:
  token_buf tb;
  parser p;
  bool have_parser = false;

  void SetUp() override {
    token_buf_init(&tb);
  }

  ast_node *parse_preprocessed(const char *source) {
    EXPECT_EQ(pp_run(&tb, source), 0);
    parser_init_from_buf(&p, &tb);
    have_parser = true;
    return parse_program(&p);
  }

  void TearDown() override {
    if (have_parser) {
      parser_destroy(&p);
    }
    token_buf_free(&tb);
  }
};

TEST_F(BridgeTest, PreprocessedTokensParseToTheSameTreeAsLexedTokens) {
  const char *source = "int counter = 0;\n"
                       "static const char *name = \"abc\";\n"
                       "int add(int a, int b) { return a + b; }\n"
                       "int main(void) {\n"
                       "  int i;\n"
                       "  for (i = 0; i < 10; i++) {\n"
                       "    if (i % 2 == 0)\n"
                       "      counter = add(counter, i);\n"
                       "    else\n"
                       "      counter--;\n"
                       "  }\n"
                       "  while (counter > 100)\n"
                       "    counter = counter - 1;\n"
                       "  return counter;\n"
                       "}\n";

  lexer lex;
  lexer_init(&lex, source);
  parser direct;
  parser_init(&direct, &lex);
  ast_node *expected = parse_program(&direct);

  ast_node *actual = parse_preprocessed(source);

  ASSERT_NE(expected, nullptr);
  ASSERT_NE(actual, nullptr);
  EXPECT_EQ(direct.had_error, 0);
  EXPECT_EQ(p.had_error, 0);
  EXPECT_EQ(expected->program.count, 4);
  std::string why;
  EXPECT_TRUE(same_ast(expected, actual, "program", why)) << why;

  free_ast(expected);
  free_ast(actual);
  parser_destroy(&direct);
}

TEST_F(BridgeTest, AnObjectLikeMacroCanExpandToAWholeDeclaration) {
  ast_node *program = parse_preprocessed("#define DECL int answer = 42;\nDECL\n");
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(program->program.count, 1);
  ast_node *decl = program->program.declarations[0];
  ASSERT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(decl->var_decl.var_name, "answer");
  ASSERT_NE(decl->var_decl.init_value, nullptr);
  EXPECT_EQ(decl->var_decl.init_value->type, AST_NODE_TYPE_NUMBER);
  EXPECT_STREQ(decl->var_decl.init_value->tok.value, "42");
  free_ast(program);
}

TEST_F(BridgeTest, AFunctionLikeMacroCanSupplyAnInitialiser) {
  ast_node *program = parse_preprocessed("#define SQ(a) ((a) * (a))\nint x = SQ(3);\n");
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(program->program.count, 1);
  ast_node *init = program->program.declarations[0]->var_decl.init_value;
  ASSERT_NE(init, nullptr);
  ASSERT_EQ(init->type, AST_NODE_TYPE_BINARY_OP);
  EXPECT_EQ(init->binary_op.op.type, TOKEN_STAR);
  ASSERT_NE(init->binary_op.left, nullptr);
  ASSERT_NE(init->binary_op.right, nullptr);
  EXPECT_STREQ(init->binary_op.left->tok.value, "3");
  EXPECT_STREQ(init->binary_op.right->tok.value, "3");
  free_ast(program);
}

TEST_F(BridgeTest, AnIncludedHeaderParsesAsPartOfTheSameTranslationUnit) {
  _mkdir("bridge_test_inc");
  {
    std::ofstream f("bridge_test_inc/decls.h", std::ios::binary);
    f << "int from_header;\n";
  }
  static const char *dirs[] = {"bridge_test_inc"};
  int rc = pp_run_ex(&tb, "#include \"decls.h\"\nint from_source;\n", "bridge_main.c", dirs, 1,
                     nullptr, 0);
  remove("bridge_test_inc/decls.h");
  _rmdir("bridge_test_inc");
  EXPECT_EQ(rc, 0);

  parser_init_from_buf(&p, &tb);
  have_parser = true;
  ast_node *program = parse_program(&p);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(program->program.count, 2);
  EXPECT_STREQ(program->program.declarations[0]->var_decl.var_name, "from_header");
  EXPECT_STREQ(program->program.declarations[1]->var_decl.var_name, "from_source");
  free_ast(program);
}

TEST_F(BridgeTest, AnInactiveConditionalGroupNeverReachesTheParser) {
  ast_node *program = parse_preprocessed("#if 0\nint int ) ( ;;; {\n#endif\nint kept;\n");
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(program->program.count, 1);
  EXPECT_STREQ(program->program.declarations[0]->var_decl.var_name, "kept");
  free_ast(program);
}

TEST_F(BridgeTest, HandingOverABufferEmptiesTheCallersCopy) {
  ASSERT_EQ(pp_run(&tb, "int moved;\n"), 0);
  ASSERT_GT(tb.count, 0);

  parser_init_from_buf(&p, &tb);
  have_parser = true;

  bool emptied = tb.tokens == nullptr && tb.count == 0 && tb.capacity == 0 && tb.pos == 0;
  EXPECT_TRUE(emptied) << "the caller's buffer still refers to the handed-over tokens";
  if (!emptied) {
    token_buf_init(&tb);
  }

  token_buf_free(&tb);
  ast_node *program = parse_program(&p);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 1);
  EXPECT_STREQ(program->program.declarations[0]->var_decl.var_name, "moved");
  free_ast(program);

  parser_destroy(&p);
  have_parser = false;
  token_buf_free(&tb);
}

TEST_F(BridgeTest, AnAlreadyReadBufferStillParsesFromTheFirstToken) {
  ASSERT_EQ(pp_run(&tb, "int first; int second;\n"), 0);
  while (token_buf_next(&tb).type != TOKEN_EOF) {
  }
  ASSERT_EQ(tb.pos, tb.count);

  parser_init_from_buf(&p, &tb);
  have_parser = true;
  ast_node *program = parse_program(&p);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(program->program.count, 2);
  EXPECT_STREQ(program->program.declarations[0]->var_decl.var_name, "first");
  EXPECT_STREQ(program->program.declarations[1]->var_decl.var_name, "second");
  free_ast(program);
}

TEST_F(BridgeTest, EmptySourceIsAnEmptyProgramWithOneErrorThroughBothEntryPoints) {
  lexer lex;
  lexer_init(&lex, "");
  parser direct;
  parser_init(&direct, &lex);
  testing::internal::CaptureStderr();
  ast_node *from_lexer = parse_program(&direct);
  ASSERT_NE(from_lexer, nullptr);
  EXPECT_EQ(from_lexer->program.count, 0);
  EXPECT_EQ(direct.had_error, 1) << "C99 6.9: a translation unit declares at least one thing";
  free_ast(from_lexer);
  parser_destroy(&direct);

  ast_node *from_pp = parse_preprocessed("");
  std::string diagnostics = testing::internal::GetCapturedStderr();
  ASSERT_NE(from_pp, nullptr);
  EXPECT_EQ(from_pp->program.count, 0);
  EXPECT_EQ(p.had_error, 1);
  EXPECT_EQ(diagnostics,
            "1:1: error: a translation unit needs at least one declaration (at '<eof>')\n"
            "1:1: error: a translation unit needs at least one declaration (at '<eof>')\n");
  free_ast(from_pp);
}

static ast_node *parse_source(const char *source, int *errors) {
  lexer lex;
  lexer_init(&lex, source);
  parser p;
  parser_init(&p, &lex);
  ast_node *program = parse_program(&p);
  *errors = p.had_error;
  parser_destroy(&p);
  return program;
}

static ast_node *top_decl(ast_node *program, int index) {
  if (program == nullptr || index >= program->program.count)
    return nullptr;
  return program->program.declarations[index];
}

static type_info *top_var_type(ast_node *program, int index) {
  ast_node *decl = top_decl(program, index);
  if (decl == nullptr || decl->type != AST_NODE_TYPE_VAR_DECL)
    return nullptr;
  return decl->var_decl.type;
}

struct ArithmeticSpelling {
  const char *source;
  prim_kind prim;
  int is_complex;
  int is_imaginary;
};

static const ArithmeticSpelling every_c99_spelling[] = {
    {"void x;", PRIM_VOID, 0, 0},
    {"_Bool x;", PRIM_BOOL, 0, 0},
    {"char x;", PRIM_CHAR, 0, 0},
    {"signed char x;", PRIM_SCHAR, 0, 0},
    {"unsigned char x;", PRIM_UCHAR, 0, 0},
    {"short x;", PRIM_SHORT, 0, 0},
    {"signed short x;", PRIM_SHORT, 0, 0},
    {"short int x;", PRIM_SHORT, 0, 0},
    {"signed short int x;", PRIM_SHORT, 0, 0},
    {"unsigned short x;", PRIM_USHORT, 0, 0},
    {"unsigned short int x;", PRIM_USHORT, 0, 0},
    {"int x;", PRIM_INT, 0, 0},
    {"signed x;", PRIM_INT, 0, 0},
    {"signed int x;", PRIM_INT, 0, 0},
    {"unsigned x;", PRIM_UINT, 0, 0},
    {"unsigned int x;", PRIM_UINT, 0, 0},
    {"long x;", PRIM_LONG, 0, 0},
    {"signed long x;", PRIM_LONG, 0, 0},
    {"long int x;", PRIM_LONG, 0, 0},
    {"signed long int x;", PRIM_LONG, 0, 0},
    {"unsigned long x;", PRIM_ULONG, 0, 0},
    {"unsigned long int x;", PRIM_ULONG, 0, 0},
    {"long long x;", PRIM_LLONG, 0, 0},
    {"signed long long x;", PRIM_LLONG, 0, 0},
    {"long long int x;", PRIM_LLONG, 0, 0},
    {"signed long long int x;", PRIM_LLONG, 0, 0},
    {"unsigned long long x;", PRIM_ULLONG, 0, 0},
    {"unsigned long long int x;", PRIM_ULLONG, 0, 0},
    {"float x;", PRIM_FLOAT, 0, 0},
    {"double x;", PRIM_DOUBLE, 0, 0},
    {"long double x;", PRIM_LDOUBLE, 0, 0},
    {"float _Complex x;", PRIM_FLOAT, 1, 0},
    {"double _Complex x;", PRIM_DOUBLE, 1, 0},
    {"long double _Complex x;", PRIM_LDOUBLE, 1, 0},
    {"float _Imaginary x;", PRIM_FLOAT, 0, 1},
    {"double _Imaginary x;", PRIM_DOUBLE, 0, 1},
    {"long double _Imaginary x;", PRIM_LDOUBLE, 0, 1},
};

static const ArithmeticSpelling reordered_spellings[] = {
    {"char unsigned x;", PRIM_UCHAR, 0, 0},
    {"char signed x;", PRIM_SCHAR, 0, 0},
    {"int short x;", PRIM_SHORT, 0, 0},
    {"int unsigned short x;", PRIM_USHORT, 0, 0},
    {"int long x;", PRIM_LONG, 0, 0},
    {"long unsigned x;", PRIM_ULONG, 0, 0},
    {"long int long x;", PRIM_LLONG, 0, 0},
    {"long unsigned long int x;", PRIM_ULLONG, 0, 0},
    {"int signed long long x;", PRIM_LLONG, 0, 0},
    {"double long x;", PRIM_LDOUBLE, 0, 0},
    {"_Complex float x;", PRIM_FLOAT, 1, 0},
    {"_Complex long double x;", PRIM_LDOUBLE, 1, 0},
};

static void expect_spellings(const ArithmeticSpelling *rows, size_t n) {
  for (size_t i = 0; i < n; i++) {
    SCOPED_TRACE(rows[i].source);
    int errors = -1;
    ast_node *program = parse_source(rows[i].source, &errors);
    type_info *t = top_var_type(program, 0);
    EXPECT_EQ(errors, 0);
    EXPECT_NE(t, nullptr);
    if (t != nullptr) {
      EXPECT_EQ(t->kind, TYPE_PRIMITIVE);
      EXPECT_EQ(t->prim, rows[i].prim);
      EXPECT_EQ(t->is_complex, rows[i].is_complex);
      EXPECT_EQ(t->is_imaginary, rows[i].is_imaginary);
    }
    free_ast(program);
  }
}

TEST(TypeSpecifierTest, EveryC99SpellingResolvesToItsType) {
  expect_spellings(every_c99_spelling, sizeof(every_c99_spelling) / sizeof(every_c99_spelling[0]));
}

TEST(TypeSpecifierTest, WordOrderDoesNotChangeTheType) {
  expect_spellings(reordered_spellings,
                   sizeof(reordered_spellings) / sizeof(reordered_spellings[0]));
}

TEST(TypeSpecifierTest, PlainSignedAndUnsignedCharAreThreeDistinctTypes) {
  int errors = -1;
  ast_node *program = parse_source("char a; signed char b; unsigned char c;", &errors);
  EXPECT_EQ(errors, 0);
  type_info *a = top_var_type(program, 0);
  type_info *b = top_var_type(program, 1);
  type_info *c = top_var_type(program, 2);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(a->prim, PRIM_CHAR);
  EXPECT_EQ(b->prim, PRIM_SCHAR);
  EXPECT_EQ(c->prim, PRIM_UCHAR);
  free_ast(program);
}

static const char *const invalid_arithmetic_spellings[] = {
    "int int x;",
    "short short x;",
    "long long long x;",
    "void void x;",
    "float float x;",
    "double double x;",
    "_Bool _Bool x;",
    "char char x;",
    "signed signed x;",
    "unsigned unsigned x;",
    "signed unsigned x;",
    "unsigned signed int x;",
    "short char x;",
    "long char x;",
    "char int x;",
    "short long x;",
    "long short int x;",
    "long float x;",
    "long long double x;",
    "short double x;",
    "unsigned double x;",
    "int double x;",
    "float double x;",
    "signed float x;",
    "void int x;",
    "unsigned void x;",
    "_Bool int x;",
    "unsigned _Bool x;",
    "int _Complex x;",
    "char _Imaginary x;",
    "_Complex x;",
    "_Imaginary x;",
    "_Complex _Imaginary double x;",
    "double _Complex _Complex x;",
    "long long _Complex x;",
    "_Bool _Complex x;",
};

TEST(TypeSpecifierTest, AnInvalidCombinationIsDiagnosedOnceAndParsingContinues) {
  for (const char *spelling : invalid_arithmetic_spellings) {
    std::string source = std::string(spelling) + " int after;";
    SCOPED_TRACE(source);
    int errors = -1;
    ast_node *program = parse_source(source.c_str(), &errors);
    EXPECT_EQ(errors, 1);
    type_info *bad = top_var_type(program, 0);
    EXPECT_NE(bad, nullptr);
    if (bad != nullptr)
      EXPECT_EQ(bad->prim, PRIM_INT);
    ast_node *after = top_decl(program, 1);
    EXPECT_NE(after, nullptr);
    if (after != nullptr) {
      EXPECT_EQ(after->type, AST_NODE_TYPE_VAR_DECL);
      if (after->type == AST_NODE_TYPE_VAR_DECL)
        EXPECT_STREQ(after->var_decl.var_name, "after");
    }
    free_ast(program);
  }
}

struct NamedConflict {
  const char *source;
  type_kind kind;
  const char *tag;
};

static const NamedConflict named_type_conflicts[] = {
    {"struct S int x; int after;", TYPE_STRUCT, "S"},
    {"int struct S x; int after;", TYPE_STRUCT, "S"},
    {"struct S _Complex x; int after;", TYPE_STRUCT, "S"},
    {"union U double x; int after;", TYPE_UNION, "U"},
    {"enum E unsigned x; int after;", TYPE_ENUM, "E"},
    {"struct A struct B x; int after;", TYPE_STRUCT, "B"},
    {"struct A enum B x; int after;", TYPE_ENUM, "B"},
};

TEST(TypeSpecifierTest, ANamedTypeCombinedWithAnotherTypeIsDiagnosedOnce) {
  for (const NamedConflict &row : named_type_conflicts) {
    SCOPED_TRACE(row.source);
    int errors = -1;
    ast_node *program = parse_source(row.source, &errors);
    EXPECT_EQ(errors, 1);
    type_info *t = top_var_type(program, 0);
    EXPECT_NE(t, nullptr);
    if (t != nullptr) {
      EXPECT_EQ(t->kind, row.kind);
      EXPECT_STREQ(t->tag_name, row.tag);
      EXPECT_EQ(t->prim, PRIM_NONE);
    }
    type_info *after = top_var_type(program, 1);
    EXPECT_NE(after, nullptr);
    if (after != nullptr)
      EXPECT_EQ(after->prim, PRIM_INT);
    free_ast(program);
  }
}

TEST(TypeSpecifierTest, ATypedefNameCombinedWithAnArithmeticWordIsDiagnosedOnce) {
  int errors = -1;
  ast_node *program = parse_source("typedef int T; T unsigned x; int after;", &errors);
  EXPECT_EQ(errors, 1);
  type_info *t = top_var_type(program, 1);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->kind, TYPE_TYPEDEF);
  EXPECT_STREQ(t->tag_name, "T");
  type_info *after = top_var_type(program, 2);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(after->prim, PRIM_INT);
  free_ast(program);
}

static const char *const missing_type_specifier[] = {
    "static x; int after;",
    "const x; int after;",
    "extern volatile x; int after;",
};

TEST(TypeSpecifierTest, AMissingTypeSpecifierIsDiagnosedAndBecomesInt) {
  for (const char *source : missing_type_specifier) {
    SCOPED_TRACE(source);
    int errors = -1;
    ast_node *program = parse_source(source, &errors);
    EXPECT_EQ(errors, 1);
    type_info *t = top_var_type(program, 0);
    type_info *after = top_var_type(program, 1);
    EXPECT_NE(t, nullptr);
    EXPECT_NE(after, nullptr);
    if (t != nullptr)
      EXPECT_EQ(t->prim, PRIM_INT);
    if (after != nullptr)
      EXPECT_EQ(after->prim, PRIM_INT);
    free_ast(program);
  }
}

TEST(TypeSpecifierTest, AParameterWithoutATypeSpecifierIsDiagnosedAndBecomesInt) {
  int errors = -1;
  ast_node *program = parse_source("int f(register a);", &errors);
  EXPECT_EQ(errors, 1);
  ast_node *f = top_decl(program, 0);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(f->type, AST_NODE_TYPE_VAR_DECL);
  ASSERT_EQ(f->var_decl.type->param_count, 1);
  EXPECT_EQ(f->var_decl.type->param_types[0]->prim, PRIM_INT);
  EXPECT_STREQ(f->var_decl.type->param_names[0], "a");
  free_ast(program);
}

TEST(TypeSpecifierTest, AUnionTypeIsDistinctFromAStructType) {
  int errors = -1;
  ast_node *program =
      parse_source("union U { int a; float b; } u; struct S { int a; } s; union U *ptr;", &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 3);

  ast_node *u_block = program->program.declarations[0];
  ASSERT_EQ(u_block->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(u_block->block.count, 2);
  type_info *u = u_block->block.statements[1]->var_decl.type;
  EXPECT_EQ(u->kind, TYPE_UNION);
  EXPECT_STREQ(u->tag_name, "U");

  ast_node *s_block = program->program.declarations[1];
  ASSERT_EQ(s_block->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(s_block->block.count, 2);
  EXPECT_EQ(s_block->block.statements[1]->var_decl.type->kind, TYPE_STRUCT);

  type_info *ptr = top_var_type(program, 2);
  ASSERT_NE(ptr, nullptr);
  ASSERT_EQ(ptr->kind, TYPE_POINTER);
  ASSERT_NE(ptr->ptr_to, nullptr);
  EXPECT_EQ(ptr->ptr_to->kind, TYPE_UNION);
  free_ast(program);
}

TEST(TypeSpecifierTest, AUnionForwardDeclarationIsStillAccepted) {
  int errors = -1;
  ast_node *program = parse_source("union U; int after;", &errors);
  EXPECT_EQ(errors, 0);
  ast_node *fwd = top_decl(program, 0);
  ASSERT_NE(fwd, nullptr);
  ASSERT_EQ(fwd->type, AST_NODE_TYPE_STRUCT_DEF);
  EXPECT_EQ(fwd->struct_def.is_forward, 1);
  EXPECT_STREQ(fwd->struct_def.tag_name, "U");
  type_info *after = top_var_type(program, 1);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(after->prim, PRIM_INT);
  free_ast(program);
}

TEST(TypeSpecifierTest, AnIdentifierAfterAnArithmeticWordIsTheDeclaredName) {
  int errors = -1;
  ast_node *program = parse_source("typedef int T; unsigned T;", &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 2);
  ast_node *decl = program->program.declarations[1];
  ASSERT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(decl->var_decl.var_name, "T");
  EXPECT_EQ(decl->var_decl.specs.storage_class, 0);
  EXPECT_EQ(decl->var_decl.type->prim, PRIM_UINT);
  free_ast(program);
}

TEST(TypeSpecifierTest, MemberSpecifiersDoNotLeakIntoTheEnclosingDeclaration) {
  int errors = -1;
  ast_node *program =
      parse_source("struct S { unsigned char c; long long n; } s; short after;", &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 2);
  ast_node *block = program->program.declarations[0];
  ASSERT_EQ(block->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(block->block.count, 2);
  ast_node *def = block->block.statements[0];
  ASSERT_EQ(def->type, AST_NODE_TYPE_STRUCT_DEF);
  ASSERT_EQ(def->struct_def.member_count, 2);
  EXPECT_EQ(def->struct_def.members[0]->var_decl.type->prim, PRIM_UCHAR);
  EXPECT_EQ(def->struct_def.members[1]->var_decl.type->prim, PRIM_LLONG);
  type_info *s = block->block.statements[1]->var_decl.type;
  EXPECT_EQ(s->kind, TYPE_STRUCT);
  EXPECT_EQ(s->prim, PRIM_NONE);
  type_info *after = top_var_type(program, 1);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(after->prim, PRIM_SHORT);
  free_ast(program);
}

TEST(TypeSpecifierTest, EveryDeclaratorInAListKeepsTheResolvedType) {
  int errors = -1;
  ast_node *program = parse_source("unsigned char a, *b, c[2];", &errors);
  EXPECT_EQ(errors, 0);
  ast_node *block = top_decl(program, 0);
  ASSERT_NE(block, nullptr);
  ASSERT_EQ(block->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(block->block.count, 3);
  type_info *a = block->block.statements[0]->var_decl.type;
  type_info *b = block->block.statements[1]->var_decl.type;
  type_info *c = block->block.statements[2]->var_decl.type;
  EXPECT_EQ(a->prim, PRIM_UCHAR);
  ASSERT_EQ(b->kind, TYPE_POINTER);
  EXPECT_EQ(b->ptr_to->prim, PRIM_UCHAR);
  ASSERT_EQ(c->kind, TYPE_ARRAY);
  EXPECT_EQ(c->ptr_to->prim, PRIM_UCHAR);
  free_ast(program);
}

TEST(TypeSpecifierTest, AVoidParameterListMeansNoParameters) {
  int errors = -1;
  ast_node *program = parse_source("int f(void); int g(void *q);", &errors);
  EXPECT_EQ(errors, 0);
  ast_node *f = top_decl(program, 0);
  ast_node *g = top_decl(program, 1);
  ASSERT_NE(f, nullptr);
  ASSERT_NE(g, nullptr);
  ASSERT_EQ(f->type, AST_NODE_TYPE_VAR_DECL);
  ASSERT_EQ(g->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(f->var_decl.type->param_count, 0);
  ASSERT_EQ(g->var_decl.type->param_count, 1);
  ASSERT_EQ(g->var_decl.type->param_types[0]->kind, TYPE_POINTER);
  EXPECT_EQ(g->var_decl.type->param_types[0]->ptr_to->prim, PRIM_VOID);
  free_ast(program);
}

TEST(TypeSpecifierTest, AnIdentifierListOutsideADefinitionIsDiagnosed) {
  int errors = -1;
  ast_node *program = parse_source("int f(x); int after;", &errors);
  EXPECT_EQ(errors, 1);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 2);
  type_info *f = top_var_type(program, 0);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(f->kind, TYPE_FUNCTION);
  EXPECT_EQ(f->has_prototype, 0);
  ASSERT_EQ(f->param_count, 1);
  EXPECT_STREQ(f->param_names[0], "x");
  EXPECT_EQ(f->param_types[0], nullptr);
  type_info *after = top_var_type(program, 1);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(after->prim, PRIM_INT);
  free_ast(program);
}

static ast_node *function_body_statement(ast_node *program, int decl_index, int stmt_index) {
  ast_node *fn = top_decl(program, decl_index);
  if (fn == nullptr || fn->type != AST_NODE_TYPE_FUNCTION_DEF || fn->function_def.body == nullptr)
    return nullptr;
  ast_node *body = fn->function_def.body;
  if (stmt_index >= body->block.count)
    return nullptr;
  return body->block.statements[stmt_index];
}

TEST(DeclSpecifierTest, StorageClassIsRecordedOnTheDeclarationWhateverTheTypeShape) {
  int errors = -1;
  ast_node *program =
      parse_source("static int *p; static int a[3]; extern int (*fp)(void);", &errors);
  EXPECT_EQ(errors, 0);
  ast_node *p = top_decl(program, 0);
  ast_node *a = top_decl(program, 1);
  ast_node *fp = top_decl(program, 2);
  ASSERT_NE(p, nullptr);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(fp, nullptr);
  ASSERT_EQ(p->type, AST_NODE_TYPE_VAR_DECL);
  ASSERT_EQ(a->type, AST_NODE_TYPE_VAR_DECL);
  ASSERT_EQ(fp->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(p->var_decl.type->kind, TYPE_POINTER);
  EXPECT_EQ(p->var_decl.specs.storage_class, TOKEN_STATIC);
  EXPECT_EQ(a->var_decl.type->kind, TYPE_ARRAY);
  EXPECT_EQ(a->var_decl.specs.storage_class, TOKEN_STATIC);
  EXPECT_EQ(fp->var_decl.type->kind, TYPE_POINTER);
  EXPECT_EQ(fp->var_decl.specs.storage_class, TOKEN_EXTERN);
  free_ast(program);
}

TEST(DeclSpecifierTest, FunctionSpecifiersAreRecordedOnTheFunction) {
  int errors = -1;
  ast_node *program = parse_source(
      "static inline void f(void); extern int g(void) { return 0; } int h(void);", &errors);
  EXPECT_EQ(errors, 0);
  ast_node *f = top_decl(program, 0);
  ast_node *g = top_decl(program, 1);
  ast_node *h = top_decl(program, 2);
  ASSERT_NE(f, nullptr);
  ASSERT_NE(g, nullptr);
  ASSERT_NE(h, nullptr);
  ASSERT_EQ(f->type, AST_NODE_TYPE_VAR_DECL);
  ASSERT_EQ(g->type, AST_NODE_TYPE_FUNCTION_DEF);
  ASSERT_EQ(h->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(f->var_decl.specs.storage_class, TOKEN_STATIC);
  EXPECT_EQ(f->var_decl.specs.is_inline, 1);
  EXPECT_EQ(g->function_def.specs.storage_class, TOKEN_EXTERN);
  EXPECT_EQ(g->function_def.specs.is_inline, 0);
  EXPECT_EQ(h->var_decl.specs.storage_class, 0);
  EXPECT_EQ(h->var_decl.specs.is_inline, 0);
  free_ast(program);
}

TEST(DeclSpecifierTest, EveryDeclaratorInAListCarriesTheStorageClass) {
  int errors = -1;
  ast_node *program =
      parse_source("static int a, *b, c[2]; void f(void) { register int x, *y, z[2]; }", &errors);
  EXPECT_EQ(errors, 0);
  ast_node *file_list = top_decl(program, 0);
  ASSERT_NE(file_list, nullptr);
  ASSERT_EQ(file_list->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(file_list->block.count, 3);
  for (int i = 0; i < 3; i++)
    EXPECT_EQ(file_list->block.statements[i]->var_decl.specs.storage_class, TOKEN_STATIC) << i;
  ast_node *block_list = function_body_statement(program, 1, 0);
  ASSERT_NE(block_list, nullptr);
  ASSERT_EQ(block_list->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(block_list->block.count, 3);
  for (int i = 0; i < 3; i++)
    EXPECT_EQ(block_list->block.statements[i]->var_decl.specs.storage_class, TOKEN_REGISTER) << i;
  free_ast(program);
}

TEST(DeclSpecifierTest, SpecifiersDoNotLeakIntoTheNextDeclaration) {
  int errors = -1;
  ast_node *program = parse_source("static inline int a(void); int b(void); static int c; int d; "
                                   "void f(void) { register int x; int y; }",
                                   &errors);
  EXPECT_EQ(errors, 0);
  ast_node *b = top_decl(program, 1);
  ASSERT_NE(b, nullptr);
  ASSERT_EQ(b->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(b->var_decl.specs.storage_class, 0);
  EXPECT_EQ(b->var_decl.specs.is_inline, 0);
  ast_node *d = top_decl(program, 3);
  ASSERT_NE(d, nullptr);
  ASSERT_EQ(d->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(d->var_decl.specs.storage_class, 0);
  ast_node *x = function_body_statement(program, 4, 0);
  ast_node *y = function_body_statement(program, 4, 1);
  ASSERT_NE(x, nullptr);
  ASSERT_NE(y, nullptr);
  EXPECT_EQ(x->var_decl.specs.storage_class, TOKEN_REGISTER);
  EXPECT_EQ(y->var_decl.specs.storage_class, 0);
  free_ast(program);
}

TEST(DeclSpecifierTest, TypedefMayAppearAnywhereInTheSpecifiers) {
  int errors = -1;
  ast_node *program = parse_source(
      "int typedef T; T x; unsigned typedef long U; U y; void f(void) { char typedef C; C c; }",
      &errors);
  EXPECT_EQ(errors, 0);
  ast_node *t = top_decl(program, 0);
  ASSERT_NE(t, nullptr);
  ASSERT_EQ(t->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(t->var_decl.var_name, "T");
  EXPECT_EQ(t->var_decl.specs.storage_class, TOKEN_TYPEDEF);
  EXPECT_EQ(t->var_decl.type->prim, PRIM_INT);
  type_info *x = top_var_type(program, 1);
  ASSERT_NE(x, nullptr);
  EXPECT_EQ(x->kind, TYPE_TYPEDEF);
  EXPECT_STREQ(x->tag_name, "T");
  ast_node *u = top_decl(program, 2);
  ASSERT_NE(u, nullptr);
  ASSERT_EQ(u->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(u->var_decl.specs.storage_class, TOKEN_TYPEDEF);
  EXPECT_EQ(u->var_decl.type->prim, PRIM_ULONG);
  type_info *y = top_var_type(program, 3);
  ASSERT_NE(y, nullptr);
  EXPECT_EQ(y->kind, TYPE_TYPEDEF);
  EXPECT_STREQ(y->tag_name, "U");
  ast_node *c_type = function_body_statement(program, 4, 0);
  ast_node *c = function_body_statement(program, 4, 1);
  ASSERT_NE(c_type, nullptr);
  ASSERT_NE(c, nullptr);
  ASSERT_EQ(c_type->type, AST_NODE_TYPE_VAR_DECL);
  ASSERT_EQ(c->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(c_type->var_decl.specs.storage_class, TOKEN_TYPEDEF);
  EXPECT_EQ(c_type->var_decl.type->prim, PRIM_CHAR);
  EXPECT_EQ(c->var_decl.type->kind, TYPE_TYPEDEF);
  EXPECT_STREQ(c->var_decl.type->tag_name, "C");
  free_ast(program);
}

struct StorageConflict {
  const char *source;
  token_type kept;
};

static const StorageConflict storage_conflicts[] = {
    {"static extern int x; int after;", TOKEN_STATIC},
    {"typedef static int T; int after;", TOKEN_TYPEDEF},
    {"extern extern int y; int after;", TOKEN_EXTERN},
    {"int static register z; int after;", TOKEN_STATIC},
    {"auto register int w; int after;", TOKEN_AUTO},
};

TEST(DeclSpecifierTest, MoreThanOneStorageClassIsDiagnosedOnceAndTheFirstIsKept) {
  for (const StorageConflict &row : storage_conflicts) {
    SCOPED_TRACE(row.source);
    int errors = -1;
    ast_node *program = parse_source(row.source, &errors);
    EXPECT_EQ(errors, 1);
    ast_node *bad = top_decl(program, 0);
    ast_node *after = top_decl(program, 1);
    EXPECT_NE(bad, nullptr);
    EXPECT_NE(after, nullptr);
    if (bad != nullptr && bad->type == AST_NODE_TYPE_VAR_DECL)
      EXPECT_EQ(bad->var_decl.specs.storage_class, row.kept);
    else
      ADD_FAILURE() << "first declaration is not a VAR_DECL";
    if (after != nullptr && after->type == AST_NODE_TYPE_VAR_DECL) {
      EXPECT_STREQ(after->var_decl.var_name, "after");
      EXPECT_EQ(after->var_decl.specs.storage_class, 0);
    } else {
      ADD_FAILURE() << "second declaration is not a VAR_DECL";
    }
    free_ast(program);
  }
}

TEST(DeclSpecifierTest, AutoIsAStorageClassButNotForParameters) {
  int errors = -1;
  ast_node *program = parse_source("void f(void) { auto int x; int auto y, *z; }", &errors);
  EXPECT_EQ(errors, 0);
  ast_node *x = function_body_statement(program, 0, 0);
  ast_node *group = function_body_statement(program, 0, 1);
  ASSERT_NE(x, nullptr);
  ASSERT_NE(group, nullptr);
  ASSERT_EQ(x->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(x->var_decl.var_name, "x");
  EXPECT_EQ(x->var_decl.specs.storage_class, TOKEN_AUTO);
  ASSERT_EQ(group->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(group->block.count, 2);
  for (int i = 0; i < 2; i++)
    EXPECT_EQ(group->block.statements[i]->var_decl.specs.storage_class, TOKEN_AUTO) << i;
  free_ast(program);

  errors = -1;
  program = parse_source("void g(auto int a); int after;", &errors);
  EXPECT_EQ(errors, 1);
  ast_node *after = top_decl(program, 1);
  ASSERT_NE(after, nullptr);
  ASSERT_EQ(after->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(after->var_decl.var_name, "after");
  free_ast(program);
}

TEST(DeclSpecifierTest, AFunctionTypedefIsATypeNotAFunction) {
  int errors = -1;
  ast_node *program = parse_source("typedef int handler(int); handler *h;", &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 2);
  ast_node *td = program->program.declarations[0];
  ASSERT_EQ(td->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(td->var_decl.var_name, "handler");
  EXPECT_EQ(td->var_decl.specs.storage_class, TOKEN_TYPEDEF);
  type_info *fn = td->var_decl.type;
  ASSERT_EQ(fn->kind, TYPE_FUNCTION);
  ASSERT_NE(fn->ptr_to, nullptr);
  EXPECT_EQ(fn->ptr_to->prim, PRIM_INT);
  ASSERT_EQ(fn->param_count, 1);
  EXPECT_EQ(fn->param_types[0]->prim, PRIM_INT);
  type_info *h = top_var_type(program, 1);
  ASSERT_NE(h, nullptr);
  ASSERT_EQ(h->kind, TYPE_POINTER);
  ASSERT_NE(h->ptr_to, nullptr);
  EXPECT_EQ(h->ptr_to->kind, TYPE_TYPEDEF);
  EXPECT_STREQ(h->ptr_to->tag_name, "handler");
  free_ast(program);
}

TEST(DeclSpecifierTest, AFunctionTypedefAtBlockScopeIsATypeNotAFunction) {
  int errors = -1;
  ast_node *program =
      parse_source("void f(void) { typedef int handler(int); handler *h; }", &errors);
  EXPECT_EQ(errors, 0);
  ast_node *td = function_body_statement(program, 0, 0);
  ast_node *h = function_body_statement(program, 0, 1);
  ASSERT_NE(td, nullptr);
  ASSERT_NE(h, nullptr);
  ASSERT_EQ(td->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(td->var_decl.specs.storage_class, TOKEN_TYPEDEF);
  EXPECT_EQ(td->var_decl.type->kind, TYPE_FUNCTION);
  ASSERT_EQ(h->type, AST_NODE_TYPE_VAR_DECL);
  ASSERT_EQ(h->var_decl.type->kind, TYPE_POINTER);
  EXPECT_EQ(h->var_decl.type->ptr_to->kind, TYPE_TYPEDEF);
  free_ast(program);
}

TEST(DeclSpecifierTest, AFunctionTypedefWithABodyIsDiagnosed) {
  int errors = -1;
  ast_node *program = parse_source("typedef int g(void) { return 0; } int after;", &errors);
  EXPECT_EQ(errors, 1);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 1);
  ast_node *after = top_decl(program, 0);
  ASSERT_EQ(after->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(after->var_decl.var_name, "after");
  free_ast(program);
}

TEST(DeclSpecifierTest, EveryTypedefNameInAListIsRegisteredAtFileScope) {
  int errors = -1;
  ast_node *program = parse_source("typedef int A, *B; A a; B b;", &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 3);
  type_info *a = top_var_type(program, 1);
  type_info *b = top_var_type(program, 2);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(a->kind, TYPE_TYPEDEF);
  EXPECT_STREQ(a->tag_name, "A");
  EXPECT_EQ(b->kind, TYPE_TYPEDEF);
  EXPECT_STREQ(b->tag_name, "B");
  free_ast(program);
}

TEST(DeclSpecifierTest, EveryTypedefNameInAListIsRegisteredAtBlockScope) {
  int errors = -1;
  ast_node *program = parse_source("void f(void) { typedef int A, *B; A a; B b; }", &errors);
  EXPECT_EQ(errors, 0);
  ast_node *b = function_body_statement(program, 0, 2);
  ASSERT_NE(b, nullptr);
  ASSERT_EQ(b->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(b->var_decl.type->kind, TYPE_TYPEDEF);
  EXPECT_STREQ(b->var_decl.type->tag_name, "B");
  free_ast(program);
}

static ast_node *leading_struct_def(ast_node *decl) {
  if (decl == nullptr)
    return nullptr;
  if (decl->type == AST_NODE_TYPE_STRUCT_DEF)
    return decl;
  if (decl->type == AST_NODE_TYPE_DECL_GROUP && decl->block.count > 0 &&
      decl->block.statements[0]->type == AST_NODE_TYPE_STRUCT_DEF)
    return decl->block.statements[0];
  return nullptr;
}

struct UnionMarking {
  const char *source;
  int is_union;
  int is_forward;
};

static const UnionMarking union_markings[] = {
    {"union U { int a; };", 1, 0},
    {"struct S { int a; };", 0, 0},
    {"union U;", 1, 1},
    {"struct S;", 0, 1},
    {"union U { int a; } u;", 1, 0},
    {"struct S { int a; } s;", 0, 0},
};

TEST(DeclSpecifierTest, AUnionDefinitionIsMarkedAsAUnion) {
  for (const UnionMarking &row : union_markings) {
    SCOPED_TRACE(row.source);
    int errors = -1;
    ast_node *program = parse_source(row.source, &errors);
    EXPECT_EQ(errors, 0);
    ast_node *def = leading_struct_def(top_decl(program, 0));
    EXPECT_NE(def, nullptr);
    if (def != nullptr) {
      EXPECT_EQ(def->struct_def.is_union, row.is_union);
      EXPECT_EQ(def->struct_def.is_forward, row.is_forward);
    }
    free_ast(program);
  }
}

TEST(DeclSpecifierTest, AUnionDefinitionAtBlockScopeIsMarkedAsAUnion) {
  int errors = -1;
  ast_node *program =
      parse_source("void f(void) { union U { int a; } u; struct S { int a; } s; }", &errors);
  EXPECT_EQ(errors, 0);
  ast_node *u = leading_struct_def(function_body_statement(program, 0, 0));
  ast_node *s = leading_struct_def(function_body_statement(program, 0, 1));
  ASSERT_NE(u, nullptr);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(u->struct_def.is_union, 1);
  EXPECT_EQ(s->struct_def.is_union, 0);
  free_ast(program);
}

TEST(DeclSpecifierTest, RegisterIsAllowedInAParameter) {
  int errors = -1;
  ast_node *program = parse_source("int f(register int a);", &errors);
  EXPECT_EQ(errors, 0);
  ast_node *f = top_decl(program, 0);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(f->type, AST_NODE_TYPE_VAR_DECL);
  ASSERT_EQ(f->var_decl.type->param_count, 1);
  EXPECT_EQ(f->var_decl.type->param_types[0]->prim, PRIM_INT);
  free_ast(program);
}

static const char *const disallowed_parameter_specifiers[] = {
    "int f(static int a); int after;",
    "int f(extern int a); int after;",
    "int f(typedef int a); int after;",
    "int f(inline int a); int after;",
    "int f(register register int a); int after;",
};

TEST(DeclSpecifierTest, OnlyRegisterMayAppearInAParameterDeclaration) {
  for (const char *source : disallowed_parameter_specifiers) {
    SCOPED_TRACE(source);
    int errors = -1;
    ast_node *program = parse_source(source, &errors);
    EXPECT_EQ(errors, 1);
    ast_node *f = top_decl(program, 0);
    EXPECT_NE(f, nullptr);
    if (f != nullptr && f->type == AST_NODE_TYPE_VAR_DECL) {
      EXPECT_EQ(f->var_decl.type->param_count, 1);
      if (f->var_decl.type->param_count == 1)
        EXPECT_STREQ(f->var_decl.type->param_names[0], "a");
    } else {
      ADD_FAILURE() << "the function declaration did not survive";
    }
    type_info *after = top_var_type(program, 1);
    EXPECT_NE(after, nullptr);
    free_ast(program);
  }
}

static const char *const type_names_with_declaration_specifiers[] = {
    "int f(void) { return sizeof(int static); } int after;",
    "int f(void) { return sizeof(int typedef); } int after;",
    "int f(void) { return (int extern)1; } int after;",
    "int f(void) { return sizeof(int inline); } int after;",
};

TEST(DeclSpecifierTest, AStorageClassOrInlineInATypeNameIsDiagnosed) {
  for (const char *source : type_names_with_declaration_specifiers) {
    SCOPED_TRACE(source);
    int errors = -1;
    ast_node *program = parse_source(source, &errors);
    EXPECT_EQ(errors, 1);
    ast_node *f = top_decl(program, 0);
    EXPECT_NE(f, nullptr);
    if (f != nullptr)
      EXPECT_EQ(f->type, AST_NODE_TYPE_FUNCTION_DEF);
    type_info *after = top_var_type(program, 1);
    EXPECT_NE(after, nullptr);
    free_ast(program);
  }
}

struct SourcePos {
  int line;
  int column;
};

static SourcePos pos_of(const char *source, const char *needle, int occurrence) {
  const char *hit = nullptr;
  const char *from = source;
  for (int i = 0; i < occurrence; i++) {
    hit = strstr(from, needle);
    if (hit == nullptr)
      return SourcePos{0, 0};
    from = hit + 1;
  }
  int line = 1;
  int column = 1;
  for (const char *c = source; c < hit; c++) {
    if (*c == '\n') {
      line++;
      column = 1;
    } else {
      column++;
    }
  }
  return SourcePos{line, column};
}

static ::testing::AssertionResult located_at(const ast_node *node, const char *source,
                                             const char *needle, int occurrence = 1) {
  if (node == nullptr)
    return ::testing::AssertionFailure() << "node is null (looking for '" << needle << "')";
  SourcePos want = pos_of(source, needle, occurrence);
  if (want.line == 0)
    return ::testing::AssertionFailure()
           << "'" << needle << "' occurrence " << occurrence << " is not in the source";
  if (node->loc.line != want.line || node->loc.column != want.column)
    return ::testing::AssertionFailure()
           << "node type " << node->type << " is at " << node->loc.line << ":" << node->loc.column
           << " but '" << needle << "' is at " << want.line << ":" << want.column;
  return ::testing::AssertionSuccess();
}

static void gather_nodes(const ast_node *node, std::vector<const ast_node *> &out);

static void gather_type_nodes(const type_info *type, std::vector<const ast_node *> &out) {
  for (const type_info *t = type; t != nullptr; t = t->ptr_to) {
    gather_nodes(t->array_size_expr, out);
    if (t->param_types != nullptr) {
      for (int i = 0; i < t->param_count; i++) {
        if (t->param_definitions != nullptr)
          gather_nodes(t->param_definitions[i], out);
        gather_type_nodes(t->param_types[i], out);
      }
    }
  }
}

static void gather_nodes(const ast_node *node, std::vector<const ast_node *> &out) {
  if (node == nullptr)
    return;
  out.push_back(node);
  switch (node->type) {
  case AST_NODE_TYPE_PROGRAM:
    for (int i = 0; i < node->program.count; i++)
      gather_nodes(node->program.declarations[i], out);
    break;
  case AST_NODE_TYPE_BLOCK:
  case AST_NODE_TYPE_DECL_GROUP:
    for (int i = 0; i < node->block.count; i++)
      gather_nodes(node->block.statements[i], out);
    break;
  case AST_NODE_TYPE_BINARY_OP:
    gather_nodes(node->binary_op.left, out);
    gather_nodes(node->binary_op.right, out);
    break;
  case AST_NODE_TYPE_UNARY_OP:
    gather_nodes(node->unary_op.operand, out);
    break;
  case AST_NODE_TYPE_ASSIGNMENT:
    gather_nodes(node->assignment.left, out);
    gather_nodes(node->assignment.right, out);
    break;
  case AST_NODE_TYPE_TERNARY:
    gather_nodes(node->ternary.condition, out);
    gather_nodes(node->ternary.true_branch, out);
    gather_nodes(node->ternary.false_branch, out);
    break;
  case AST_NODE_TYPE_IF:
    gather_nodes(node->if_stmt.condition, out);
    gather_nodes(node->if_stmt.then_branch, out);
    gather_nodes(node->if_stmt.else_branch, out);
    break;
  case AST_NODE_TYPE_WHILE:
    gather_nodes(node->while_stmt.condition, out);
    gather_nodes(node->while_stmt.body, out);
    break;
  case AST_NODE_TYPE_FOR:
    gather_nodes(node->for_stmt.init, out);
    gather_nodes(node->for_stmt.condition, out);
    gather_nodes(node->for_stmt.increment, out);
    gather_nodes(node->for_stmt.body, out);
    break;
  case AST_NODE_TYPE_FUNCTION_CALL:
    gather_nodes(node->function_call.callable, out);
    for (int i = 0; i < node->function_call.arg_count; i++)
      gather_nodes(node->function_call.arguments[i], out);
    break;
  case AST_NODE_TYPE_FUNCTION_DEF:
    gather_type_nodes(node->function_def.type->ptr_to, out);
    for (int i = 0; i < node->function_def.type->param_count; i++)
      gather_type_nodes(node->function_def.type->param_types[i], out);
    gather_nodes(node->function_def.body, out);
    break;
  case AST_NODE_TYPE_RETURN:
    gather_nodes(node->return_stmt.return_value, out);
    break;
  case AST_NODE_TYPE_ARRAY_SUBSCRIPT:
    gather_nodes(node->array_subscript.left, out);
    gather_nodes(node->array_subscript.index, out);
    break;
  case AST_NODE_TYPE_MEMBER_ACCESS:
    gather_nodes(node->member_access.left, out);
    break;
  case AST_NODE_TYPE_VAR_DECL:
    gather_type_nodes(node->var_decl.type, out);
    gather_nodes(node->var_decl.init_value, out);
    gather_nodes(node->var_decl.bitfield_width, out);
    break;
  case AST_NODE_TYPE_DO_WHILE:
    gather_nodes(node->do_while_stmt.body, out);
    gather_nodes(node->do_while_stmt.condition, out);
    break;
  case AST_NODE_TYPE_SWITCH:
    gather_nodes(node->switch_stmt.condition, out);
    gather_nodes(node->switch_stmt.body, out);
    break;
  case AST_NODE_TYPE_CASE:
    gather_nodes(node->case_stmt.value, out);
    gather_nodes(node->case_stmt.body, out);
    break;
  case AST_NODE_TYPE_DEFAULT:
    gather_nodes(node->default_stmt.body, out);
    break;
  case AST_NODE_TYPE_STRUCT_DEF:
    for (int i = 0; i < node->struct_def.member_count; i++)
      gather_nodes(node->struct_def.members[i], out);
    break;
  case AST_NODE_TYPE_ENUM_DEF:
    if (node->enum_def.values != nullptr) {
      for (int i = 0; i < node->enum_def.enumerator_count; i++)
        gather_nodes(node->enum_def.values[i], out);
    }
    break;
  case AST_NODE_TYPE_CAST:
    gather_nodes(node->cast_expr.definition, out);
    gather_type_nodes(node->cast_expr.type, out);
    gather_nodes(node->cast_expr.operand, out);
    break;
  case AST_NODE_TYPE_INIT_LIST:
    for (int i = 0; i < node->init_list.count; i++) {
      gather_nodes(node->init_list.items[i].index, out);
      gather_nodes(node->init_list.items[i].value, out);
    }
    break;
  case AST_NODE_TYPE_COMPOUND_LITERAL:
    gather_nodes(node->compound_literal.definition, out);
    gather_type_nodes(node->compound_literal.type, out);
    gather_nodes(node->compound_literal.init_list, out);
    break;
  case AST_NODE_TYPE_LABEL:
    gather_nodes(node->label_stmt.statement, out);
    break;
  default:
    break;
  }
}

static const char *every_node_kind_source =
    "struct fwd;\n"
    "struct point { int x; int y; };\n"
    "union number { int i; float f; };\n"
    "enum color { RED, GREEN = 2 };\n"
    "typedef int handler(int);\n"
    "static int table[3] = { 1, 2, 3 };\n"
    "struct point origin = { .x = 0, .y = 0 };\n"
    "struct wrap { struct point p; } w = { .p.x = 1 };\n"
    "struct pair { int a; int b; } make_pair(void);\n"
    "int sum(int a, int b) { return a + b; }\n"
    "int main(void) {\n"
    "  int i, *ptr = &i;\n"
    "  struct point pt;\n"
    "  i = sizeof(int) + sizeof i;\n"
    "  ptr = (int *)ptr;\n"
    "  pt = (struct point){ 1, 2 };\n"
    "  pt.x = ptr[0];\n"
    "  (&pt)->y = i > 0 ? i : -i;\n"
    "  for (i = 0; i < 3; i++) { if (i == 1) continue; else break; }\n"
    "  while (i) i--;\n"
    "  do { i++; } while (i < 2);\n"
    "  switch (i) { case 1: i = 2; break; default: ; }\n"
    "  goto done;\n"
    "done:\n"
    "  i = sum(i, 1), i++;\n"
    "  return \"s\" \"t\"[0] + 'c';\n"
    "}\n";

TEST(SourceLocationTest, EveryNodeTheParserBuildsHasALocation) {
  int errors = -1;
  ast_node *program = parse_source(every_node_kind_source, &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);

  std::vector<const ast_node *> nodes;
  gather_nodes(program, nodes);

  std::vector<int> seen(AST_NODE_TYPE_COMPOUND_LITERAL + 1, 0);
  for (const ast_node *n : nodes) {
    seen[n->type] = 1;
    EXPECT_GE(n->loc.line, 1) << "node type " << n->type << " has no line";
    EXPECT_GE(n->loc.column, 1) << "node type " << n->type << " has no column";
  }

  const ast_node_type expected_kinds[] = {
      AST_NODE_TYPE_EMPTY,         AST_NODE_TYPE_PROGRAM,      AST_NODE_TYPE_NUMBER,
      AST_NODE_TYPE_IDENTIFIER,    AST_NODE_TYPE_BINARY_OP,    AST_NODE_TYPE_UNARY_OP,
      AST_NODE_TYPE_ASSIGNMENT,    AST_NODE_TYPE_TERNARY,      AST_NODE_TYPE_IF,
      AST_NODE_TYPE_WHILE,         AST_NODE_TYPE_BLOCK,        AST_NODE_TYPE_FOR,
      AST_NODE_TYPE_FUNCTION_CALL, AST_NODE_TYPE_FUNCTION_DEF, AST_NODE_TYPE_RETURN,
      AST_NODE_TYPE_STRING,        AST_NODE_TYPE_CHAR_LITERAL, AST_NODE_TYPE_ARRAY_SUBSCRIPT,
      AST_NODE_TYPE_MEMBER_ACCESS, AST_NODE_TYPE_VAR_DECL,     AST_NODE_TYPE_DO_WHILE,
      AST_NODE_TYPE_SWITCH,        AST_NODE_TYPE_CASE,         AST_NODE_TYPE_DEFAULT,
      AST_NODE_TYPE_BREAK,         AST_NODE_TYPE_CONTINUE,     AST_NODE_TYPE_STRUCT_DEF,
      AST_NODE_TYPE_ENUM_DEF,      AST_NODE_TYPE_CAST,         AST_NODE_TYPE_INIT_LIST,
      AST_NODE_TYPE_GOTO,          AST_NODE_TYPE_LABEL,        AST_NODE_TYPE_COMPOUND_LITERAL,
      AST_NODE_TYPE_DECL_GROUP,
  };
  for (ast_node_type kind : expected_kinds)
    EXPECT_EQ(seen[kind], 1) << "the source did not exercise node type " << kind;

  free_ast(program);
}

TEST(DeclarationGroupTest, OnlyACompoundStatementIsABlock) {
  int errors = -1;
  ast_node *program = parse_source("int a, b;\n"
                                   "struct pair { int first, second; } make(void);\n"
                                   "void f(void) {\n"
                                   "  int c, d;\n"
                                   "  { }\n"
                                   "}\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 3);

  ast_node *file_list = top_decl(program, 0);
  EXPECT_EQ(file_list->type, AST_NODE_TYPE_DECL_GROUP) << "a file-scope declarator list";
  EXPECT_EQ(file_list->block.count, 2);

  ast_node *with_function = top_decl(program, 1);
  ASSERT_EQ(with_function->type, AST_NODE_TYPE_DECL_GROUP) << "a definition and a function";
  ASSERT_EQ(with_function->block.count, 2);
  ast_node *pair = with_function->block.statements[0];
  ASSERT_EQ(pair->type, AST_NODE_TYPE_STRUCT_DEF);
  ASSERT_EQ(pair->struct_def.member_count, 1);
  EXPECT_EQ(pair->struct_def.members[0]->type, AST_NODE_TYPE_DECL_GROUP) << "a member list";
  EXPECT_EQ(with_function->block.statements[1]->type, AST_NODE_TYPE_VAR_DECL);

  ast_node *fn = top_decl(program, 2);
  ASSERT_EQ(fn->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_EQ(fn->function_def.body->type, AST_NODE_TYPE_BLOCK);
  ast_node *local_list = function_body_statement(program, 2, 0);
  ast_node *inner = function_body_statement(program, 2, 1);
  ASSERT_NE(local_list, nullptr);
  ASSERT_NE(inner, nullptr);
  EXPECT_EQ(local_list->type, AST_NODE_TYPE_DECL_GROUP) << "a block-scope declarator list";
  EXPECT_EQ(inner->type, AST_NODE_TYPE_BLOCK);
  free_ast(program);
}

TEST(TagDeclarationTest, ATagDeclaredAloneInABlockIsAForwardDeclaration) {
  int errors = -1;
  ast_node *program = parse_source("void f(void) {\n"
                                   "  struct S;\n"
                                   "  union U;\n"
                                   "}\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);

  ast_node *s = function_body_statement(program, 0, 0);
  ast_node *u = function_body_statement(program, 0, 1);
  ASSERT_NE(s, nullptr);
  ASSERT_NE(u, nullptr);

  ASSERT_EQ(s->type, AST_NODE_TYPE_STRUCT_DEF);
  EXPECT_STREQ(s->struct_def.tag_name, "S");
  EXPECT_EQ(s->struct_def.is_forward, 1);
  EXPECT_EQ(s->struct_def.is_union, 0);
  EXPECT_EQ(s->loc.line, 2);
  EXPECT_EQ(s->loc.column, 3);

  ASSERT_EQ(u->type, AST_NODE_TYPE_STRUCT_DEF);
  EXPECT_STREQ(u->struct_def.tag_name, "U");
  EXPECT_EQ(u->struct_def.is_forward, 1);
  EXPECT_EQ(u->struct_def.is_union, 1);
  free_ast(program);
}

TEST(TagDeclarationTest, OnlyAStructOrUnionTagDeclaredAloneIsADeclaration) {
  const struct {
    const char *source;
    const char *diagnostics;
  } cases[] = {
      {"void f(void) {\n  enum E;\n}\n",
       "2:9: error: declaration does not declare anything (at ';')\n"},
      {"enum E;\n", "1:7: error: declaration does not declare anything (at ';')\n"},
      {"int;\nconst;\n", "1:4: error: declaration does not declare anything (at ';')\n"
                         "2:6: error: a declaration needs a type specifier (at ';')\n"
                         "2:6: error: declaration does not declare anything (at ';')\n"},
      {"struct { int a; };\n", "1:18: error: declaration does not declare anything (at ';')\n"},
      {"union { int a; };\n", "1:17: error: declaration does not declare anything (at ';')\n"},
      {"typedef struct { int a; };\n",
       "1:26: error: declaration does not declare anything (at ';')\n"},
      {"void f(void) {\n  struct { int a; };\n}\n",
       "2:20: error: declaration does not declare anything (at ';')\n"},
      {"struct { int a; } v;\n", ""},
      {"struct S { int a; };\n", ""},
      {"enum { A };\n", ""},
      {"struct S a, ;\n", "1:13: error: expected a name in this declaration (at ';')\n"},
      {"struct S *;\n", "1:11: error: expected a name in this declaration (at ';')\n"},
      {"struct *p;\n", "1:8: error: expected a tag name or '{' (at '*')\n"},
      {"enum *e;\n", "1:6: error: expected a tag name or '{' (at '*')\n"},
  };
  for (const auto &test : cases) {
    SCOPED_TRACE(test.source);
    int errors = -1;
    testing::internal::CaptureStderr();
    ast_node *program = parse_source(test.source, &errors);
    std::string diagnostics = testing::internal::GetCapturedStderr();
    EXPECT_EQ(diagnostics, test.diagnostics);
    free_ast(program);
  }
}

TEST(TagDeclarationTest, ADefiningSpecifierLinksEveryDeclaratorToTheDefinition) {
  int errors = -1;
  ast_node *program = parse_source("typedef struct { int x; } point, *point_ptr;\n"
                                   "enum { RED } shade;\n"
                                   "struct named { int y; } one;\n"
                                   "struct named two;\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 4);

  ast_node *group = top_decl(program, 0);
  ASSERT_EQ(group->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(group->block.count, 3);
  ast_node *def = group->block.statements[0];
  ASSERT_EQ(def->type, AST_NODE_TYPE_STRUCT_DEF);
  type_info *point = group->block.statements[1]->var_decl.type;
  type_info *point_ptr = group->block.statements[2]->var_decl.type;
  EXPECT_EQ(point->definition, def);
  ASSERT_NE(point_ptr->ptr_to, nullptr);
  EXPECT_EQ(point_ptr->definition, nullptr) << "the pointer is not the specifier";
  EXPECT_EQ(point_ptr->ptr_to->definition, def) << "every declarator's copy keeps the link";
  EXPECT_NE(point_ptr->ptr_to, point);

  ast_node *enum_group = top_decl(program, 1);
  ASSERT_EQ(enum_group->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(enum_group->block.count, 2);
  EXPECT_EQ(enum_group->block.statements[1]->var_decl.type->definition,
            enum_group->block.statements[0]);

  ast_node *named = top_decl(program, 2);
  ASSERT_EQ(named->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(named->block.count, 2);
  EXPECT_EQ(named->block.statements[1]->var_decl.type->definition, named->block.statements[0]);

  ast_node *two = top_decl(program, 3);
  ASSERT_EQ(two->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(two->var_decl.type->definition, nullptr) << "a plain reference defines nothing";
  EXPECT_EQ(two->var_decl.type->symbol, nullptr) << "the parser never resolves";
  free_ast(program);
}

TEST(ArrayDeclaratorTest, TheParserKeepsTheSizeExpressionAndLeavesTheSizeToSema) {
  int errors = -1;
  ast_node *program = parse_source("int a[10];\nint b[0x10];\n", &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 2);
  type_info *a = top_var_type(program, 0);
  type_info *b = top_var_type(program, 1);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_STREQ(size_text(a), "10");
  EXPECT_STREQ(size_text(b), "0x10");
  EXPECT_EQ(a->array_size, -1);
  EXPECT_EQ(b->array_size, -1);
  EXPECT_EQ(a->is_vla, 0);
  free_ast(program);
}

TEST(FunctionDeclarationTest, ADefinitionKeepsItsWholeFunctionType) {
  int errors = -1;
  ast_node *program = parse_source("int sum(int a, char *b, ...) { return a; }\n", &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);
  ast_node *fn = top_decl(program, 0);
  ASSERT_NE(fn, nullptr);
  ASSERT_EQ(fn->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_STREQ(fn->function_def.name, "sum");
  type_info *type = fn->function_def.type;
  ASSERT_NE(type, nullptr);
  EXPECT_EQ(type->kind, TYPE_FUNCTION);
  EXPECT_EQ(type->is_variadic, 1);
  ASSERT_EQ(type->param_count, 2);
  EXPECT_STREQ(type->param_names[0], "a");
  EXPECT_STREQ(type->param_names[1], "b");
  EXPECT_EQ(type->param_types[1]->kind, TYPE_POINTER);
  ASSERT_NE(type->ptr_to, nullptr);
  EXPECT_EQ(type->ptr_to->prim, PRIM_INT);
  EXPECT_EQ(type->ptr_to->is_variadic, 0) << "the return type is not variadic";
  EXPECT_NE(fn->function_def.body, nullptr);
  free_ast(program);
}

TEST(FunctionDeclarationTest, APrototypeIsADeclaration) {
  int errors = -1;
  ast_node *program = parse_source("int f(void), x;\n"
                                   "int g(int a);\n"
                                   "int h(void) = 1;\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 3);

  ast_node *list = top_decl(program, 0);
  ASSERT_EQ(list->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(list->block.count, 2);
  ast_node *f = list->block.statements[0];
  ast_node *x = list->block.statements[1];
  ASSERT_EQ(f->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(f->var_decl.var_name, "f");
  EXPECT_EQ(f->var_decl.type->kind, TYPE_FUNCTION);
  ASSERT_EQ(x->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(x->var_decl.var_name, "x");
  EXPECT_EQ(x->var_decl.type->kind, TYPE_PRIMITIVE);

  ast_node *g = top_decl(program, 1);
  ASSERT_EQ(g->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(g->var_decl.type->kind, TYPE_FUNCTION);

  ast_node *h = top_decl(program, 2);
  ASSERT_EQ(h->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_NE(h->var_decl.init_value, nullptr) << "sema rejects it; the parser only builds it";
  free_ast(program);
}

TEST(FunctionDeclarationTest, ADefinitionAfterAStructDefinitionStaysGrouped) {
  int errors = -1;
  ast_node *program =
      parse_source("struct pair { int a; } make(void) { struct pair p; return p; }\n", &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);
  ast_node *group = top_decl(program, 0);
  ASSERT_EQ(group->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(group->block.count, 2);
  EXPECT_EQ(group->block.statements[0]->type, AST_NODE_TYPE_STRUCT_DEF);
  ASSERT_EQ(group->block.statements[1]->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_EQ(group->block.statements[1]->function_def.type->kind, TYPE_FUNCTION);
  free_ast(program);
}

TEST(FunctionDeclarationTest, AFunctionDeclaratorFollowedByNeitherSemicolonNorBodyIsReported) {
  int errors = -1;
  testing::internal::CaptureStderr();
  ast_node *program = parse_source("int f(void) int x;\n", &errors);
  std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_GT(errors, 0);
  EXPECT_NE(diagnostics.find("expected ';' after declaration"), std::string::npos) << diagnostics;
  free_ast(program);
}

TEST(SourceLocationTest, LiteralsAndIdentifiersAreLocatedAtTheirToken) {
  const char *src = "int f(int count) {\n"
                    "  return count\n"
                    "    + 42\n"
                    "    + \"ab\" \"cd\"[0]\n"
                    "    + 'z';\n"
                    "}\n";
  int errors = -1;
  ast_node *program = parse_source(src, &errors);
  EXPECT_EQ(errors, 0);
  ast_node *ret = function_body_statement(program, 0, 0);
  ASSERT_NE(ret, nullptr);
  ASSERT_EQ(ret->type, AST_NODE_TYPE_RETURN);
  EXPECT_TRUE(located_at(ret, src, "return"));

  ast_node *outer = ret->return_stmt.return_value;
  ASSERT_EQ(outer->type, AST_NODE_TYPE_BINARY_OP);
  ast_node *middle = outer->binary_op.left;
  ASSERT_EQ(middle->type, AST_NODE_TYPE_BINARY_OP);
  ast_node *inner = middle->binary_op.left;
  ASSERT_EQ(inner->type, AST_NODE_TYPE_BINARY_OP);

  EXPECT_TRUE(located_at(inner->binary_op.left, src, "count", 2));
  EXPECT_TRUE(located_at(inner->binary_op.right, src, "42"));
  ast_node *sub = middle->binary_op.right;
  ASSERT_EQ(sub->type, AST_NODE_TYPE_ARRAY_SUBSCRIPT);
  EXPECT_TRUE(located_at(sub->array_subscript.left, src, "\"ab\""));
  EXPECT_TRUE(located_at(sub->array_subscript.index, src, "0]"));
  EXPECT_TRUE(located_at(outer->binary_op.right, src, "'z'"));
  free_ast(program);
}

TEST(SourceLocationTest, OperatorNodesAreLocatedAtTheirOperator) {
  const char *src = "void g(int a, int b, int *p) {\n"
                    "  a = b << 1;\n"
                    "  a += -b;\n"
                    "  a = !b, b = ~a;\n"
                    "  p++;\n"
                    "  --a;\n"
                    "  a = b ? a : b;\n"
                    "  a = sizeof(int) + sizeof a;\n"
                    "  a = b * a + b;\n"
                    "}\n";
  int errors = -1;
  ast_node *program = parse_source(src, &errors);
  EXPECT_EQ(errors, 0);

  ast_node *s0 = function_body_statement(program, 0, 0);
  ASSERT_NE(s0, nullptr);
  EXPECT_TRUE(located_at(s0, src, "= b <<"));
  EXPECT_TRUE(located_at(s0->assignment.right, src, "<<"));

  ast_node *s1 = function_body_statement(program, 0, 1);
  ASSERT_NE(s1, nullptr);
  EXPECT_TRUE(located_at(s1, src, "+="));
  EXPECT_TRUE(located_at(s1->assignment.right, src, "-b"));

  ast_node *s2 = function_body_statement(program, 0, 2);
  ASSERT_NE(s2, nullptr);
  ASSERT_EQ(s2->type, AST_NODE_TYPE_BINARY_OP);
  EXPECT_TRUE(located_at(s2, src, ", b = ~"));
  EXPECT_TRUE(located_at(s2->binary_op.left, src, "= !b"));
  EXPECT_TRUE(located_at(s2->binary_op.left->assignment.right, src, "!b"));
  EXPECT_TRUE(located_at(s2->binary_op.right, src, "= ~a"));
  EXPECT_TRUE(located_at(s2->binary_op.right->assignment.right, src, "~a"));

  EXPECT_TRUE(located_at(function_body_statement(program, 0, 3), src, "++"));
  EXPECT_TRUE(located_at(function_body_statement(program, 0, 4), src, "--a"));

  ast_node *s5 = function_body_statement(program, 0, 5);
  ASSERT_NE(s5, nullptr);
  EXPECT_TRUE(located_at(s5, src, "= b ?"));
  EXPECT_TRUE(located_at(s5->assignment.right, src, "?"));

  ast_node *s6 = function_body_statement(program, 0, 6);
  ASSERT_NE(s6, nullptr);
  EXPECT_TRUE(located_at(s6, src, "= sizeof"));
  ast_node *plus = s6->assignment.right;
  ASSERT_EQ(plus->type, AST_NODE_TYPE_BINARY_OP);
  EXPECT_TRUE(located_at(plus, src, "+ sizeof a"));
  EXPECT_TRUE(located_at(plus->binary_op.left, src, "sizeof(int)"));
  EXPECT_TRUE(located_at(plus->binary_op.left->unary_op.operand, src, "(int)"));
  EXPECT_TRUE(located_at(plus->binary_op.right, src, "sizeof a"));

  ast_node *s7 = function_body_statement(program, 0, 7);
  ASSERT_NE(s7, nullptr);
  ast_node *add = s7->assignment.right;
  ASSERT_EQ(add->type, AST_NODE_TYPE_BINARY_OP);
  EXPECT_TRUE(located_at(add, src, "+ b;"));
  EXPECT_TRUE(located_at(add->binary_op.left, src, "* a +"));
  free_ast(program);
}

TEST(SourceLocationTest, PostfixNodesAreLocatedAtTheirPunctuator) {
  const char *src = "struct s { int v; int *w; };\n"
                    "void h(struct s x, struct s *y, int (*fn)(int)) {\n"
                    "  fn(x.v);\n"
                    "  y->w[2] = 1;\n"
                    "}\n";
  int errors = -1;
  ast_node *program = parse_source(src, &errors);
  EXPECT_EQ(errors, 0);

  ast_node *call = function_body_statement(program, 1, 0);
  ASSERT_NE(call, nullptr);
  ASSERT_EQ(call->type, AST_NODE_TYPE_FUNCTION_CALL);
  EXPECT_TRUE(located_at(call, src, "(x.v)"));
  EXPECT_TRUE(located_at(call->function_call.callable, src, "fn(x"));
  ASSERT_EQ(call->function_call.arg_count, 1);
  EXPECT_TRUE(located_at(call->function_call.arguments[0], src, ".v"));

  ast_node *assign = function_body_statement(program, 1, 1);
  ASSERT_NE(assign, nullptr);
  EXPECT_TRUE(located_at(assign, src, "= 1"));
  ast_node *sub = assign->assignment.left;
  ASSERT_EQ(sub->type, AST_NODE_TYPE_ARRAY_SUBSCRIPT);
  EXPECT_TRUE(located_at(sub, src, "[2]"));
  EXPECT_TRUE(located_at(sub->array_subscript.left, src, "->w"));
  free_ast(program);
}

TEST(SourceLocationTest, CastsCompoundLiteralsAndInitListsAreLocatedAtTheirOpeningToken) {
  const char *src = "struct pt { int x; int y; };\n"
                    "struct box { struct pt a; };\n"
                    "void k(long n) {\n"
                    "  struct pt q = (struct pt){ 1, 2 };\n"
                    "  struct box b = { .a.x = 3 };\n"
                    "  n = (long)q.x;\n"
                    "}\n";
  int errors = -1;
  ast_node *program = parse_source(src, &errors);
  EXPECT_EQ(errors, 0);

  ast_node *q = function_body_statement(program, 2, 0);
  ASSERT_NE(q, nullptr);
  ASSERT_EQ(q->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_TRUE(located_at(q, src, "q = ("));
  ast_node *literal = q->var_decl.init_value;
  ASSERT_EQ(literal->type, AST_NODE_TYPE_COMPOUND_LITERAL);
  EXPECT_TRUE(located_at(literal, src, "(struct pt){"));
  EXPECT_TRUE(located_at(literal->compound_literal.init_list, src, "{ 1, 2"));

  ast_node *b = function_body_statement(program, 2, 1);
  ASSERT_NE(b, nullptr);
  ASSERT_EQ(b->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_TRUE(located_at(b, src, "b = {"));
  ast_node *outer = b->var_decl.init_value;
  ASSERT_EQ(outer->type, AST_NODE_TYPE_INIT_LIST);
  EXPECT_TRUE(located_at(outer, src, "{ .a.x"));
  ASSERT_EQ(outer->init_list.count, 1);
  ast_node *wrap = outer->init_list.items[0].value;
  ASSERT_EQ(wrap->type, AST_NODE_TYPE_INIT_LIST);
  EXPECT_TRUE(located_at(wrap, src, "{ .a.x"));

  ast_node *assign = function_body_statement(program, 2, 2);
  ASSERT_NE(assign, nullptr);
  ast_node *cast = assign->assignment.right;
  ASSERT_EQ(cast->type, AST_NODE_TYPE_CAST);
  EXPECT_TRUE(located_at(cast, src, "(long)"));
  EXPECT_TRUE(located_at(cast->cast_expr.operand, src, ".x;"));
  free_ast(program);
}

TEST(SourceLocationTest, StatementsAreLocatedAtTheirKeyword) {
  const char *src = "int m(int i) {\n"
                    "  if (i) i = 1; else i = 2;\n"
                    "  while (i) i--;\n"
                    "  do { i++; } while (i < 3);\n"
                    "  for (;;) break;\n"
                    "  switch (i) {\n"
                    "  case 1:\n"
                    "    goto out;\n"
                    "  default:\n"
                    "    ;\n"
                    "  }\n"
                    "  for (;;) continue;\n"
                    "out:\n"
                    "  return i;\n"
                    "}\n";
  int errors = -1;
  ast_node *program = parse_source(src, &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);
  EXPECT_TRUE(located_at(program, src, "int m"));

  ast_node *fn = top_decl(program, 0);
  ASSERT_NE(fn, nullptr);
  ASSERT_EQ(fn->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_TRUE(located_at(fn->function_def.body, src, "{\n  if"));

  EXPECT_TRUE(located_at(function_body_statement(program, 0, 0), src, "if"));
  EXPECT_TRUE(located_at(function_body_statement(program, 0, 1), src, "while (i) i--"));

  ast_node *dowhile = function_body_statement(program, 0, 2);
  ASSERT_NE(dowhile, nullptr);
  EXPECT_TRUE(located_at(dowhile, src, "do"));
  EXPECT_TRUE(located_at(dowhile->do_while_stmt.body, src, "{ i++; }"));

  ast_node *for_break = function_body_statement(program, 0, 3);
  ASSERT_NE(for_break, nullptr);
  EXPECT_TRUE(located_at(for_break, src, "for (;;) break"));
  EXPECT_TRUE(located_at(for_break->for_stmt.body, src, "break"));

  ast_node *sw = function_body_statement(program, 0, 4);
  ASSERT_NE(sw, nullptr);
  EXPECT_TRUE(located_at(sw, src, "switch"));
  ast_node *sw_body = sw->switch_stmt.body;
  ASSERT_EQ(sw_body->type, AST_NODE_TYPE_BLOCK);
  EXPECT_TRUE(located_at(sw_body, src, "{\n  case"));
  ASSERT_EQ(sw_body->block.count, 2);
  ast_node *case_stmt = sw_body->block.statements[0];
  EXPECT_TRUE(located_at(case_stmt, src, "case"));
  EXPECT_TRUE(located_at(case_stmt->case_stmt.body, src, "goto"));
  ast_node *default_stmt = sw_body->block.statements[1];
  EXPECT_TRUE(located_at(default_stmt, src, "default"));
  EXPECT_TRUE(located_at(default_stmt->default_stmt.body, src, ";\n  }"));

  ast_node *for_continue = function_body_statement(program, 0, 5);
  ASSERT_NE(for_continue, nullptr);
  EXPECT_TRUE(located_at(for_continue, src, "for (;;) continue"));
  EXPECT_TRUE(located_at(for_continue->for_stmt.body, src, "continue"));

  ast_node *label = function_body_statement(program, 0, 6);
  ASSERT_NE(label, nullptr);
  EXPECT_TRUE(located_at(label, src, "out:"));
  EXPECT_TRUE(located_at(label->label_stmt.statement, src, "return"));
  free_ast(program);

  ast_node *empty = parse_source("", &errors);
  ASSERT_NE(empty, nullptr);
  EXPECT_EQ(empty->loc.line, 1);
  EXPECT_EQ(empty->loc.column, 1);
  free_ast(empty);
}

TEST(SourceLocationTest, DeclarationsAreLocatedAtTheirName) {
  const char *src = "static unsigned long *counter;\n"
                    "int (*fp)(void), plain, *ptr;\n"
                    "struct flags { unsigned : 3; int bit : 1; } f;\n"
                    "union u;\n"
                    "enum e { A, B } ev;\n"
                    "int sum(int a) { int local, *lp; return a; }\n";
  int errors = -1;
  ast_node *program = parse_source(src, &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 6);

  EXPECT_TRUE(located_at(top_decl(program, 0), src, "counter"));

  ast_node *list = top_decl(program, 1);
  ASSERT_EQ(list->type, AST_NODE_TYPE_DECL_GROUP);
  EXPECT_TRUE(located_at(list, src, "int (*fp)"));
  ASSERT_EQ(list->block.count, 3);
  EXPECT_TRUE(located_at(list->block.statements[0], src, "fp)"));
  EXPECT_TRUE(located_at(list->block.statements[1], src, "plain"));
  EXPECT_TRUE(located_at(list->block.statements[2], src, "ptr;"));

  ast_node *flags = top_decl(program, 2);
  ASSERT_EQ(flags->type, AST_NODE_TYPE_DECL_GROUP);
  EXPECT_TRUE(located_at(flags, src, "struct flags"));
  ASSERT_EQ(flags->block.count, 2);
  ast_node *def = flags->block.statements[0];
  ASSERT_EQ(def->type, AST_NODE_TYPE_STRUCT_DEF);
  EXPECT_TRUE(located_at(def, src, "struct flags"));
  ASSERT_EQ(def->struct_def.member_count, 2);
  EXPECT_TRUE(located_at(def->struct_def.members[0], src, ": 3"));
  EXPECT_TRUE(located_at(def->struct_def.members[1], src, "bit"));
  EXPECT_TRUE(located_at(flags->block.statements[1], src, "f;"));

  ast_node *forward = top_decl(program, 3);
  ASSERT_EQ(forward->type, AST_NODE_TYPE_STRUCT_DEF);
  EXPECT_TRUE(located_at(forward, src, "union u;"));

  ast_node *enum_block = top_decl(program, 4);
  ASSERT_EQ(enum_block->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(enum_block->block.count, 2);
  EXPECT_TRUE(located_at(enum_block->block.statements[0], src, "enum"));
  EXPECT_TRUE(located_at(enum_block->block.statements[1], src, "ev;"));

  ast_node *sum = top_decl(program, 5);
  ASSERT_EQ(sum->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_TRUE(located_at(sum, src, "sum"));
  ast_node *locals = function_body_statement(program, 5, 0);
  ASSERT_NE(locals, nullptr);
  ASSERT_EQ(locals->type, AST_NODE_TYPE_DECL_GROUP);
  EXPECT_TRUE(located_at(locals, src, "int local"));
  ASSERT_EQ(locals->block.count, 2);
  EXPECT_TRUE(located_at(locals->block.statements[0], src, "local"));
  EXPECT_TRUE(located_at(locals->block.statements[1], src, "lp;"));
  free_ast(program);
}

TEST(SourceLocationTest, BlockScopeDefinitionsAreLocatedAtTheirKeyword) {
  const char *src = "void z(void) {\n"
                    "  struct inner { int q; } iv;\n"
                    "  union { int a; } anon;\n"
                    "}\n";
  int errors = -1;
  ast_node *program = parse_source(src, &errors);
  EXPECT_EQ(errors, 0);

  ast_node *first = function_body_statement(program, 0, 0);
  ASSERT_NE(first, nullptr);
  ASSERT_EQ(first->type, AST_NODE_TYPE_DECL_GROUP);
  EXPECT_TRUE(located_at(first, src, "struct inner"));
  ASSERT_EQ(first->block.count, 2);
  EXPECT_TRUE(located_at(first->block.statements[0], src, "struct inner"));
  EXPECT_TRUE(located_at(first->block.statements[1], src, "iv"));

  ast_node *second = function_body_statement(program, 0, 1);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->type, AST_NODE_TYPE_DECL_GROUP);
  ASSERT_EQ(second->block.count, 2);
  EXPECT_TRUE(located_at(second->block.statements[0], src, "union"));
  EXPECT_TRUE(located_at(second->block.statements[1], src, "anon"));
  free_ast(program);
}

TEST(SourceLocationTest, AMacroBodyTokenKeepsItsDefinitionPositionAndAnArgumentItsUsePosition) {
  const char *src = "#define TWICE(x) ((x) + (x))\n"
                    "int v = TWICE(3);\n";
  token_buf tb;
  token_buf_init(&tb);
  ASSERT_EQ(pp_run(&tb, src), 0);
  parser p;
  parser_init_from_buf(&p, &tb);
  ast_node *program = parse_program(&p);
  EXPECT_EQ(p.had_error, 0);

  ast_node *v = top_decl(program, 0);
  ASSERT_NE(v, nullptr);
  ASSERT_EQ(v->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_TRUE(located_at(v, src, "v ="));
  ast_node *sum = v->var_decl.init_value;
  ASSERT_NE(sum, nullptr);
  ASSERT_EQ(sum->type, AST_NODE_TYPE_BINARY_OP);
  EXPECT_TRUE(located_at(sum, src, "+"));
  EXPECT_TRUE(located_at(sum->binary_op.left, src, "3)"));

  free_ast(program);
  parser_destroy(&p);
  token_buf_free(&tb);
}

TEST(SourceLocationTest, ADefinitionFollowedByAFunctionDeclaratorIsLocated) {
  const char *src = "struct pair { int a; int b; } make_pair(void);";
  int errors = -1;
  ast_node *program = parse_source(src, &errors);
  EXPECT_EQ(errors, 0);
  ast_node *block = top_decl(program, 0);
  ASSERT_NE(block, nullptr);
  ASSERT_EQ(block->type, AST_NODE_TYPE_DECL_GROUP);
  EXPECT_TRUE(located_at(block, src, "struct pair"));
  ASSERT_EQ(block->block.count, 2);
  EXPECT_TRUE(located_at(block->block.statements[0], src, "struct pair"));
  ASSERT_EQ(block->block.statements[1]->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_TRUE(located_at(block->block.statements[1], src, "make_pair"));
  free_ast(program);
}

static std::string parse_diagnostics(const char *source, int *errors) {
  testing::internal::CaptureStderr();
  ast_node *program = parse_source(source, errors);
  std::string diagnostics = testing::internal::GetCapturedStderr();
  free_ast(program);
  return diagnostics;
}

struct DiagnosticCase {
  const char *source;
  const char *message;
};

static void expect_diagnosed(const std::vector<DiagnosticCase> &cases) {
  for (const DiagnosticCase &test : cases) {
    SCOPED_TRACE(test.source);
    int errors = -1;
    std::string diagnostics = parse_diagnostics(test.source, &errors);
    EXPECT_GT(errors, 0);
    EXPECT_NE(diagnostics.find(test.message), std::string::npos) << diagnostics;
  }
}

static void expect_clean(const std::vector<const char *> &sources) {
  for (const char *source : sources) {
    SCOPED_TRACE(source);
    int errors = -1;
    std::string diagnostics = parse_diagnostics(source, &errors);
    EXPECT_EQ(errors, 0) << diagnostics;
  }
}

TEST(TypedefScopeTest, OrdinaryNamesHideATypedefNameUntilTheirScopeEnds) {
  expect_clean({
      "typedef int T; void f(void) { int T; T = 1; }",
      "typedef int T; void f(int T) { T = 1; }",
      "typedef int T; void f(T T) { T = 1; }",
      "typedef int T; void f(int T); T x;",
      "typedef int T; void f(void) { { int T; } T x; }",
      "typedef int T; void f(void) { enum { T }; int y = T; }",
      "typedef int T; void f(void) { enum { T }; T + 1; }",
      "typedef int T; void f(int c) { if (c) c = sizeof(enum { T = 1 }); else c = (T){1}; }",
      "typedef int T; void f(int c) { do c = sizeof(enum { T = 1 }); while ((T){0}); }",
      "typedef int T; void f(void) { for (int T = 0; T < 1; T++) ; T x; }",
      "typedef int T; void f(int c) { if (sizeof(enum { T = 1 })) c = T; T x; }",
      "typedef int T; void f(int c) { while (c) c = sizeof(enum { T = 1 }); T x; }",
      "typedef int T; struct S { int T; T x; };",
      "int T; void f(void) { typedef int T; T x; }",
      "typedef int T; void f(void) { T: goto T; }",
      "typedef int T; void f(void) { int T = sizeof(T); }",
      "typedef int T; void f(void) { T T; T = 1; }",
      "typedef int T; void f(int (T));",
  });
}

TEST(TypedefScopeTest, AHiddenTypedefNameIsNotAType) {
  expect_diagnosed({
      {"typedef int T; void f(void) { int T; T x; }", "expected ';' after expression statement"},
      {"typedef int T; void g(int T, T x);", "expected a parameter type"},
      {"typedef int T; void f(void) { enum { T }; T x; }",
       "expected ';' after expression statement"},
  });
}

static ast_node *last_declared(ast_node *item) {
  if (item != nullptr && item->type == AST_NODE_TYPE_DECL_GROUP && item->block.count > 0)
    item = item->block.statements[item->block.count - 1];
  return item != nullptr && item->type == AST_NODE_TYPE_VAR_DECL ? item : nullptr;
}

TEST(TypedefScopeTest, AParenthesizedDeclaratorMayRedeclareATypedefName) {
  int errors = -1;
  ast_node *program = parse_source("typedef int T;\n"
                                   "void f(void) { int (T); T = 1; }\n"
                                   "void g(void) { int (*T); T = 0; }\n"
                                   "void h(void) { int (T)[2]; T[0] = 1; }\n"
                                   "void i(void) { int ((T)); T = 1; }\n"
                                   "void j(void) { int x, (T); T = x; }\n"
                                   "void k(void) { T (T); T = 1; }\n"
                                   "struct S { int (T); };\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  const type_kind kinds[] = {TYPE_PRIMITIVE, TYPE_POINTER,   TYPE_ARRAY,
                             TYPE_PRIMITIVE, TYPE_PRIMITIVE, TYPE_TYPEDEF};
  for (int i = 0; i < 6; i++) {
    SCOPED_TRACE(i);
    ast_node *decl = last_declared(function_body_statement(program, i + 1, 0));
    ASSERT_NE(decl, nullptr);
    EXPECT_STREQ(decl->var_decl.var_name, "T");
    EXPECT_EQ(decl->var_decl.type->kind, kinds[i]);
  }
  ast_node *def = leading_struct_def(top_decl(program, 7));
  ASSERT_NE(def, nullptr);
  ASSERT_EQ(def->struct_def.member_count, 1);
  EXPECT_STREQ(def->struct_def.members[0]->var_decl.var_name, "T");
  EXPECT_EQ(def->struct_def.members[0]->var_decl.type->kind, TYPE_PRIMITIVE);
  free_ast(program);
}

TEST(TypedefScopeTest, AParenthesizedTypedefNameInAParameterOrTypeNameIsAParameterList) {
  int errors = -1;
  ast_node *program = parse_source("typedef int T;\n"
                                   "void f(int (T));\n"
                                   "void g(int ((T)));\n"
                                   "int s = sizeof(int (T));\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  for (int i = 1; i <= 2; i++) {
    SCOPED_TRACE(i);
    type_info *fn = top_var_type(program, i);
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->kind, TYPE_FUNCTION);
    ASSERT_EQ(fn->param_count, 1);
    EXPECT_EQ(fn->param_names[0], nullptr);
    type_info *param = fn->param_types[0];
    ASSERT_EQ(param->kind, TYPE_FUNCTION);
    ASSERT_EQ(param->param_count, 1);
    EXPECT_EQ(param->param_types[0]->kind, TYPE_TYPEDEF);
  }
  ast_node *s = top_decl(program, 3);
  ASSERT_NE(s, nullptr);
  ast_node *init = s->var_decl.init_value;
  ASSERT_NE(init, nullptr);
  ASSERT_EQ(init->unary_op.operand->type, AST_NODE_TYPE_CAST);
  EXPECT_EQ(init->unary_op.operand->cast_expr.type->kind, TYPE_FUNCTION);
  free_ast(program);
}

TEST(StatementTest, ADeclarationIsNotAStatement) {
  expect_diagnosed({
      {"void f(int c) { if (c) int x; }", "a declaration is not a statement"},
      {"void f(int c) { if (c) ; else int x; }", "a declaration is not a statement"},
      {"void f(int c) { while (c) int x; }", "a declaration is not a statement"},
      {"void f(int c) { do int x; while (c); }", "a declaration is not a statement"},
      {"void f(int c) { for (;;) int x; }", "a declaration is not a statement"},
      {"void f(int c) { switch (c) int x; }", "a declaration is not a statement"},
      {"void f(int c) { switch (c) { case 1: int x; } }", "a declaration is not a statement"},
      {"void f(int c) { switch (c) { default: int x; } }", "a declaration is not a statement"},
      {"void f(void) { L: int x; }", "a declaration is not a statement"},
  });
  expect_clean({"void f(void) { int a; a = 1; int b; { int c; } for (int i = 0; ;) break; }"});
}

TEST(DeclarationShapeTest, MembersParametersAndTypedefsFollowTheirOwnRules) {
  expect_diagnosed({
      {"struct S { int a = 1; };", "a member cannot have an initializer"},
      {"struct S { static int a; };", "a member cannot have a storage class or be inline"},
      {"struct S { inline int a; };", "a member cannot have a storage class or be inline"},
      {"typedef int T = 1;", "a typedef cannot have an initializer"},
      {"struct S { };", "a struct or union needs at least one member"},
      {"enum E { };", "an enum needs at least one enumerator"},
      {"struct S { int a;", "expected '}' to close the member list"},
      {"enum { A B } x;", "expected '}' to close the enumerator list"},
      {"struct S { foo; int a; };", "expected a member declaration"},
      {"struct S { int; };", "declaration does not declare anything"},
      {"struct S { struct T; int a; };", "declaration does not declare anything"},
      {"struct S { enum { A } ; int a; };", "declaration does not declare anything"},
      {"int f(...);", "a named parameter must come before '...'"},
      {"int x, f(void) { return 0; }", "expected ';' after declaration"},
      {"typedef int F(void) { return 0; }", "expected ';' after declaration"},
      {"void f(void) { int b : 3; }", "expected ';' after declaration"},
      {"int a[1, 2];", "expected ']' after array size"},
  });
  expect_clean({
      "struct S { int a : 3, : 0; unsigned b : 1; };",
      "enum E { A, B, };",
      "struct S; union U; void f(void) { struct S; }",
      "static struct S { int a; };",
      "int f(int, ...);",
  });
}

TEST(DeclarationShapeTest, AMemberInitializerIsDroppedAndATypedefKeepsNone) {
  int errors = -1;
  testing::internal::CaptureStderr();
  ast_node *program = parse_source("struct S { int a = 1; }; typedef int T = 2;", &errors);
  testing::internal::GetCapturedStderr();
  EXPECT_EQ(errors, 2);
  ast_node *def = top_decl(program, 0);
  ASSERT_NE(def, nullptr);
  ASSERT_EQ(def->type, AST_NODE_TYPE_STRUCT_DEF);
  ASSERT_EQ(def->struct_def.member_count, 1);
  EXPECT_EQ(def->struct_def.members[0]->var_decl.init_value, nullptr);
  ast_node *t = top_decl(program, 1);
  ASSERT_NE(t, nullptr);
  ASSERT_EQ(t->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_EQ(t->var_decl.init_value, nullptr);
  free_ast(program);
}

TEST(TypeNameTest, TypeNamesMayDefineTagsAndCompoundLiteralsTakePostfixOperators) {
  int errors = -1;
  ast_node *program = parse_source("void f(void) {\n"
                                   "  long a = sizeof(struct P { int x; });\n"
                                   "  int b = (enum { K = 3 })K;\n"
                                   "  int c = (int[]){1, 2}[1];\n"
                                   "  int d = sizeof (int){1};\n"
                                   "  int e = ((struct { int y; }){4}).y;\n"
                                   "}\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  ast_node *a = function_body_statement(program, 0, 0);
  ast_node *b = function_body_statement(program, 0, 1);
  ast_node *c = function_body_statement(program, 0, 2);
  ast_node *d = function_body_statement(program, 0, 3);
  ast_node *e = function_body_statement(program, 0, 4);
  ASSERT_TRUE(a && b && c && d && e);

  ast_node *size = a->var_decl.init_value;
  ASSERT_EQ(size->type, AST_NODE_TYPE_UNARY_OP);
  ASSERT_EQ(size->unary_op.operand->type, AST_NODE_TYPE_CAST);
  ast_node *struct_def = size->unary_op.operand->cast_expr.definition;
  ASSERT_NE(struct_def, nullptr);
  ASSERT_EQ(struct_def->type, AST_NODE_TYPE_STRUCT_DEF);
  EXPECT_STREQ(struct_def->struct_def.tag_name, "P");
  EXPECT_EQ(size->unary_op.operand->cast_expr.type->definition, struct_def);

  ast_node *cast = b->var_decl.init_value;
  ASSERT_EQ(cast->type, AST_NODE_TYPE_CAST);
  ASSERT_NE(cast->cast_expr.definition, nullptr);
  EXPECT_EQ(cast->cast_expr.definition->type, AST_NODE_TYPE_ENUM_DEF);
  ASSERT_NE(cast->cast_expr.operand, nullptr);
  EXPECT_EQ(cast->cast_expr.operand->type, AST_NODE_TYPE_IDENTIFIER);

  ast_node *subscript = c->var_decl.init_value;
  ASSERT_EQ(subscript->type, AST_NODE_TYPE_ARRAY_SUBSCRIPT);
  EXPECT_EQ(subscript->array_subscript.left->type, AST_NODE_TYPE_COMPOUND_LITERAL);

  ast_node *literal_size = d->var_decl.init_value;
  ASSERT_EQ(literal_size->type, AST_NODE_TYPE_UNARY_OP);
  EXPECT_EQ(literal_size->unary_op.operand->type, AST_NODE_TYPE_COMPOUND_LITERAL);

  ast_node *member = e->var_decl.init_value;
  ASSERT_EQ(member->type, AST_NODE_TYPE_MEMBER_ACCESS);
  ASSERT_EQ(member->member_access.left->type, AST_NODE_TYPE_COMPOUND_LITERAL);
  ASSERT_NE(member->member_access.left->compound_literal.definition, nullptr);
  EXPECT_EQ(member->member_access.left->compound_literal.definition->type,
            AST_NODE_TYPE_STRUCT_DEF);
  free_ast(program);
}

TEST(TypeNameTest, UnfinishedTypeNamesAndPostfixOperatorsAreReported) {
  expect_diagnosed({
      {"int a = (int 1;", "expected ')' after a type name"},
      {"int b = sizeof(int 1;", "expected ')' after a type name"},
      {"void f(void) { f(1; }", "expected ')' after the arguments"},
      {"void f(int *a) { a[1; }", "expected ']' after the subscript"},
      {"struct S { int x; } s; void f(void) { s.; }", "expected a member name"},
  });
}

TEST(ParameterTest, AParameterSpecifierMayDefineATag) {
  int errors = -1;
  ast_node *program = parse_source("void f(struct Q { int y; } q, enum { R } r);\n"
                                   "void g(int a, struct Z { int z; } *b);\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  type_info *f = top_var_type(program, 0);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(f->param_count, 2);
  ASSERT_NE(f->param_definitions, nullptr);
  ASSERT_NE(f->param_definitions[0], nullptr);
  EXPECT_EQ(f->param_definitions[0]->type, AST_NODE_TYPE_STRUCT_DEF);
  EXPECT_EQ(f->param_types[0]->definition, f->param_definitions[0]);
  ASSERT_NE(f->param_definitions[1], nullptr);
  EXPECT_EQ(f->param_definitions[1]->type, AST_NODE_TYPE_ENUM_DEF);
  type_info *g = top_var_type(program, 1);
  ASSERT_NE(g, nullptr);
  ASSERT_EQ(g->param_count, 2);
  ASSERT_NE(g->param_definitions, nullptr);
  EXPECT_EQ(g->param_definitions[0], nullptr);
  ASSERT_NE(g->param_definitions[1], nullptr);
  EXPECT_STREQ(g->param_definitions[1]->struct_def.tag_name, "Z");
  free_ast(program);
}

TEST(ParameterTest, AbstractDeclaratorsMayBeFunctions) {
  int errors = -1;
  ast_node *program = parse_source("typedef int T;\n"
                                   "void f(int (int), int (), int (char *, ...), int (*)(int), "
                                   "int (T));\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  type_info *f = top_var_type(program, 1);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(f->param_count, 5);
  EXPECT_EQ(f->param_types[0]->kind, TYPE_FUNCTION);
  EXPECT_EQ(f->param_types[0]->has_prototype, 1);
  EXPECT_EQ(f->param_types[1]->kind, TYPE_FUNCTION);
  EXPECT_EQ(f->param_types[1]->has_prototype, 0);
  EXPECT_EQ(f->param_types[2]->kind, TYPE_FUNCTION);
  EXPECT_EQ(f->param_types[2]->is_variadic, 1);
  ASSERT_EQ(f->param_types[3]->kind, TYPE_POINTER);
  EXPECT_EQ(f->param_types[3]->ptr_to->kind, TYPE_FUNCTION);
  ASSERT_EQ(f->param_types[4]->kind, TYPE_FUNCTION) << "C99 6.7.5.3p11: (T) is a parameter list";
  ASSERT_EQ(f->param_types[4]->param_count, 1);
  EXPECT_EQ(f->param_types[4]->param_types[0]->kind, TYPE_TYPEDEF);
  free_ast(program);
}

TEST(ArrayDeclaratorTest, StaticQualifiersAndStarBelongToParameters) {
  int errors = -1;
  ast_node *program = parse_source(
      "void f(int a[static 3], int b[const], int c[static const 4][5], int d[*], int e[*][*],\n"
      "       int (*g)(int h[*]));\n"
      "void k(int n, int *p, int a[*p]);\n",
      &errors);
  EXPECT_EQ(errors, 0);
  type_info *f = top_var_type(program, 0);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(f->param_count, 6);
  EXPECT_EQ(f->param_types[0]->array_static, 1);
  EXPECT_NE(f->param_types[0]->array_size_expr, nullptr);
  EXPECT_EQ(f->param_types[1]->is_const, 1);
  EXPECT_EQ(f->param_types[1]->array_static, 0);
  EXPECT_EQ(f->param_types[2]->array_static, 1);
  EXPECT_EQ(f->param_types[2]->is_const, 1);
  EXPECT_EQ(f->param_types[2]->ptr_to->array_static, 0);
  EXPECT_EQ(f->param_types[2]->ptr_to->is_const, 0);
  EXPECT_EQ(f->param_types[3]->array_star, 1);
  EXPECT_EQ(f->param_types[3]->array_size_expr, nullptr);
  EXPECT_EQ(f->param_types[4]->ptr_to->array_star, 1);
  free_ast(program);

  expect_diagnosed({
      {"int a[static 3];",
       "static and qualifiers in [] belong only to a parameter's outermost array"},
      {"int a[const 3];",
       "static and qualifiers in [] belong only to a parameter's outermost array"},
      {"void f(int a[3][static 4]);",
       "static and qualifiers in [] belong only to a parameter's outermost array"},
      {"void f(int (*a)[const 3]);",
       "static and qualifiers in [] belong only to a parameter's outermost array"},
      {"struct S { int a[restrict 2]; };",
       "static and qualifiers in [] belong only to a parameter's outermost array"},
      {"int a[*];", "[*] is only allowed in a function prototype"},
      {"void f(void) { long n = sizeof(int[*]); }", "[*] is only allowed in a function prototype"},
      {"void f(int a[static]);", "static in an array declarator needs a size"},
      {"void f(int a[static static 3]);", "static appears twice in an array declarator"},
  });
}

TEST(KnrDefinitionTest, AnIdentifierListTakesItsTypesFromTheDeclarationList) {
  int errors = -1;
  ast_node *program = parse_source("int f(a, b, c) int a; double *b, c; { return a; }\n"
                                   "int g(p) register int p; { return p; }\n"
                                   "int h(s) struct S { int x; } s; { return s.x; }\n"
                                   "typedef int T;\n"
                                   "int k(t) T t; { T u; return t; }\n"
                                   "int empty() { return 0; }\n"
                                   "int none(void) { return 0; }\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  ast_node *f = top_decl(program, 0);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(f->type, AST_NODE_TYPE_FUNCTION_DEF);
  type_info *type = f->function_def.type;
  EXPECT_EQ(type->has_prototype, 0);
  ASSERT_EQ(type->param_count, 3);
  EXPECT_STREQ(type->param_names[0], "a");
  ASSERT_NE(type->param_types[0], nullptr);
  EXPECT_EQ(type->param_types[0]->prim, PRIM_INT);
  ASSERT_NE(type->param_types[1], nullptr);
  EXPECT_EQ(type->param_types[1]->kind, TYPE_POINTER);
  ASSERT_NE(type->param_types[2], nullptr);
  EXPECT_EQ(type->param_types[2]->prim, PRIM_DOUBLE);

  ast_node *h = top_decl(program, 2);
  ASSERT_NE(h, nullptr);
  ASSERT_EQ(h->type, AST_NODE_TYPE_FUNCTION_DEF);
  ASSERT_NE(h->function_def.type->param_definitions, nullptr);
  ASSERT_NE(h->function_def.type->param_definitions[0], nullptr);
  EXPECT_EQ(h->function_def.type->param_definitions[0]->type, AST_NODE_TYPE_STRUCT_DEF);

  ast_node *empty = top_decl(program, 5);
  ast_node *none = top_decl(program, 6);
  ASSERT_TRUE(empty && none);
  EXPECT_EQ(empty->function_def.type->has_prototype, 0);
  EXPECT_EQ(none->function_def.type->has_prototype, 1);
  free_ast(program);
}

TEST(KnrDefinitionTest, IdentifierListsAndParameterNamesAreChecked) {
  expect_diagnosed({
      {"int f(a);", "an identifier list is only allowed in a function definition"},
      {"int (*fp)(a);", "an identifier list is only allowed in a function definition"},
      {"int g(int h(x));", "an identifier list is only allowed in a function definition"},
      {"long n = sizeof(int (*)(x));",
       "an identifier list is only allowed in a function definition"},
      {"int f(a) { return 0; }", "a parameter in the identifier list is not declared"},
      {"int f(a) int b; int a; { return 0; }",
       "this declaration names no parameter of the function"},
      {"int f(a) int a; int a; { return 0; }", "a parameter is declared twice"},
      {"int f(a) static int a; { return 0; }",
       "only register may appear in a parameter declaration"},
      {"int f(a) int a = 1; { return 0; }", "a parameter cannot have an initializer"},
      {"int f(a) int; int a; { return 0; }", "declaration does not declare anything"},
      {"int f(a, 1) { return 0; }", "expected a parameter name"},
  });
  expect_clean({"int f(int) { return 0; }", "int f(int a, char) { return a; }"});
}

TEST(SourceLocationTest, NodesAndErrorsNameTheFileTheTokensCameFrom) {
  token_buf tb;
  pp_run_ex(&tb, "int a = 08;\n#line 30 \"gen.c\"\nint b = 09;\n", "main.c", nullptr, 0, nullptr,
            0);
  parser p;
  parser_init_from_buf(&p, &tb);
  testing::internal::CaptureStderr();
  ast_node *program = parse_program(&p);
  std::string diagnostics = testing::internal::GetCapturedStderr();
  parser_destroy(&p);
  EXPECT_EQ(diagnostics, "main.c:1:9: error: invalid digit in an octal constant (at '08')\n"
                         "gen.c:30:9: error: invalid digit in an octal constant (at '09')\n");
  ast_node *a = top_decl(program, 0);
  ast_node *b = top_decl(program, 1);
  ASSERT_TRUE(a && b);
  EXPECT_STREQ(a->loc.file, "main.c") << "file names outlive the token buffer";
  EXPECT_STREQ(a->var_decl.init_value->loc.file, "main.c");
  EXPECT_STREQ(b->loc.file, "gen.c");
  EXPECT_EQ(b->loc.line, 30);
  free_ast(program);

  int errors = -1;
  program = parse_source("int c;", &errors);
  ASSERT_NE(top_decl(program, 0), nullptr);
  EXPECT_EQ(top_decl(program, 0)->loc.file, nullptr)
      << "source handed straight to the lexer has no file";
  free_ast(program);
}

TEST(InitializerTest, InitializerListsFollowTheGrammar) {
  expect_diagnosed({
      {"int a[2] = {};", "an initializer list needs at least one initializer"},
      {"int a[2][2] = { {}, { 1 } };", "an initializer list needs at least one initializer"},
      {"int a[2] = { [1] 5 };", "expected '=' after a designator"},
      {"struct P { int x; } p = { .x 1 };", "expected '=' after a designator"},
      {"int a[2] = { 1, 2;", "expected '}' to close the initializer list"},
      {"int a[2][2] = { { 1 };", "expected '}' to close the initializer list"},
  });
  expect_clean({
      "int a[2] = { [1] = 5, };",
      "struct P { int x; } p = { .x = 1 };",
      "int b[2][2] = { { 1 }, { 2, 3 } };",
      "int c = { 1 };",
  });
  int errors = -1;
  EXPECT_EQ(parse_diagnostics("int a[2] = { [1] };", &errors),
            "1:18: error: expected '=' after a designator (at '}')\n"
            "1:18: error: expected an expression (at '}')\n")
      << "a missing value does not also lose the closing brace";
}

TEST(TranslationUnitTest, ATranslationUnitNeedsADeclaration) {
  int errors = -1;
  EXPECT_EQ(parse_diagnostics("", &errors),
            "1:1: error: a translation unit needs at least one declaration (at '<eof>')\n");
  EXPECT_EQ(errors, 1);
  std::string dropped = parse_diagnostics("int x", &errors);
  EXPECT_EQ(errors, 1);
  EXPECT_NE(dropped.find("expected ';' after declaration"), std::string::npos) << dropped;
  EXPECT_EQ(dropped.find("translation unit"), std::string::npos)
      << "a unit whose only declaration was dropped after an error is not reported again as empty";
}

TEST(ParameterTest, VoidIsOnlyTheEmptyListWhenItIsAloneUnnamedAndUnqualified) {
  int errors = -1;
  ast_node *program = parse_source("int g(void);\n"
                                   "int h(const void);\n"
                                   "int k(void, ...);\n"
                                   "int m(void x);\n"
                                   "int n(void, int);\n",
                                   &errors);
  EXPECT_EQ(errors, 0) << "C99 6.7.5.3p4 constrains only a definition's parameters";
  const int counts[] = {0, 1, 1, 1, 2};
  for (int i = 0; i < 5; i++) {
    type_info *type = top_var_type(program, i);
    ASSERT_NE(type, nullptr) << i;
    EXPECT_EQ(type->param_count, counts[i]) << i;
    EXPECT_EQ(type->has_prototype, 1) << i;
  }
  free_ast(program);

  expect_clean({"int f(void) { return 0; }", "int f(void *p) { return p != 0; }",
                "int f(void x) { return 0; }", "int f(void, int a) { return a; }",
                "int f(const void) { return 0; }"});
}

TEST(ParameterTest, RegisterOnAParameterIsRecorded) {
  int errors = -1;
  ast_node *program = parse_source("void f(register int a, int b);\n"
                                   "int g(a, b) int a; register char *b; { return a; }\n"
                                   "void h(int a);\n",
                                   &errors);
  EXPECT_EQ(errors, 0);

  type_info *f = top_var_type(program, 0);
  ASSERT_NE(f, nullptr);
  ASSERT_NE(f->param_register, nullptr);
  EXPECT_EQ(f->param_register[0], 1);
  EXPECT_EQ(f->param_register[1], 0);

  ast_node *g = top_decl(program, 1);
  ASSERT_NE(g, nullptr);
  ASSERT_EQ(g->type, AST_NODE_TYPE_FUNCTION_DEF);
  ASSERT_NE(g->function_def.type->param_register, nullptr);
  EXPECT_EQ(g->function_def.type->param_register[0], 0);
  EXPECT_EQ(g->function_def.type->param_register[1], 1);

  EXPECT_EQ(top_var_type(program, 2)->param_register, nullptr)
      << "the array is allocated only when some parameter is a register";
  free_ast(program);
}

static ast_node *parse_preprocessed(const std::string &source, int *errors) {
  token_buf tb;
  EXPECT_EQ(pp_run(&tb, source.c_str()), 0) << source;
  parser p;
  parser_init_from_buf(&p, &tb);
  ast_node *program = parse_program(&p);
  *errors = p.had_error;
  parser_destroy(&p);
  token_buf_free(&tb);
  return program;
}

static const char *const gnu_syntax_body =
    "A int A * A first A, second L A;\n"
    "static A INL int A twice(int (A *g)(int), A int y) { E int z = E y; E z++; return g(z); }\n"
    "E typedef long long A wide;\n"
    "E struct S { E unsigned u : 3 A; int (A *cb)(void) A; C int k; };\n"
    "extern int renamed(const char *R fmt, ...) L A;\n"
    "SG char sc; V int vv;\n";

TEST(GnuSyntaxTest, AttributesExtensionsAndSpellingsBuildTheSameTreeAsPlainC) {
  std::string gnu = "#define A __attribute__((__unused__, format(printf, 1, 2), __const__, ))\n"
                    "#define L __asm__(\"\" \"renamed_in_asm\")\n"
                    "#define E __extension__\n"
                    "#define INL __inline__\n"
                    "#define C __const\n"
                    "#define R __restrict__\n"
                    "#define SG __signed__\n"
                    "#define V __volatile__\n";
  std::string plain = "#define A\n"
                      "#define L\n"
                      "#define E\n"
                      "#define INL inline\n"
                      "#define C const\n"
                      "#define R restrict\n"
                      "#define SG signed\n"
                      "#define V volatile\n";
  int gnu_errors = -1;
  int plain_errors = -1;
  ast_node *with = parse_preprocessed(gnu + gnu_syntax_body, &gnu_errors);
  ast_node *without = parse_preprocessed(plain + gnu_syntax_body, &plain_errors);
  EXPECT_EQ(gnu_errors, 0);
  EXPECT_EQ(plain_errors, 0);
  ASSERT_NE(with, nullptr);
  ASSERT_NE(without, nullptr);
  EXPECT_EQ(with->program.count, 7);
  std::string why;
  {
    ShapeOnly shape;
    EXPECT_TRUE(same_ast(with, without, "program", why)) << why;
  }
  free_ast(with);
  free_ast(without);
}

TEST(GnuSyntaxTest, AnAttributeAfterAParenthesisIsLookedPast) {
  int errors = -1;
  ast_node *program = parse_source("void f(int (__attribute__((cdecl)) *)(int));\n"
                                   "void h(int (__attribute__((unused)) int));\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  type_info *f = top_var_type(program, 0);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(f->param_count, 1);
  type_info *pointer = f->param_types[0];
  ASSERT_EQ(pointer->kind, TYPE_POINTER) << "(attribute *) is a declarator";
  ASSERT_EQ(pointer->ptr_to->kind, TYPE_FUNCTION);
  EXPECT_EQ(pointer->ptr_to->param_count, 1);
  type_info *h = top_var_type(program, 1);
  ASSERT_NE(h, nullptr);
  ASSERT_EQ(h->param_count, 1);
  ASSERT_EQ(h->param_types[0]->kind, TYPE_FUNCTION) << "(attribute int) is a parameter list";
  ASSERT_EQ(h->param_types[0]->param_count, 1);
  EXPECT_EQ(h->param_types[0]->param_types[0]->prim, PRIM_INT);
  free_ast(program);
}

TEST(GnuSyntaxTest, MalformedGnuSyntaxIsReported) {
  expect_diagnosed({
      {"int __attribute__(x) y;", "expected '((' after __attribute__"},
      {"int __attribute__((x(1 y;", "expected ')' after the attribute's arguments"},
      {"int __attribute__((x) y;", "expected '))' to close __attribute__"},
      {"int f(void) __asm__(f);", "expected a string in parentheses after __asm__"},
      {"int f(void) __asm__(\"a\";", "expected ')' after the __asm__ name"},
      {"void f(void) __attribute__((x)) {}", "expected ';' after declaration"},
      {"void f(void) { __asm__ x; }", "expected '(' and an assembly string after __asm__"},
      {"void f(int a) { __asm__(\"\" : \"r\" a); }",
       "expected '(' and an operand after an asm constraint"},
      {"void f(int a) { __asm__(\"\" : \"=r\"(a; }", "expected ')' after an asm operand"},
      {"void f(void) { __asm__(\"\" }", "expected ')' to close the __asm__ statement"},
      {"void f(void) { __asm__(\"\") }", "expected ';' after the __asm__ statement"},
      {"__extension__", "expected a declaration at file scope"},
  });
}

TEST(GnuSyntaxTest, AnAsmStatementKeepsItsTextOperandsAndClobbers) {
  int errors = -1;
  ast_node *program = parse_source(
      "void f(int code) {\n"
      "  int out, o2;\n"
      "  __asm__ __volatile__(\"mov %2, %0\" \"\\n\" : \"=r\"(out), \"=m\"(o2) : \"r\"(code) : "
      "\"memory\", \"cc\");\n"
      "  __asm__(\"int {$}3\" :);\n"
      "  __asm__(\"int {$}0x29\" : : \"c\"(code));\n"
      "  __asm__(\"nop\");\n"
      "  __asm__(\"\" ::: \"memory\");\n"
      "}\n",
      &errors);
  EXPECT_EQ(errors, 0);
  ast_node *full = function_body_statement(program, 0, 1);
  ASSERT_NE(full, nullptr);
  ASSERT_EQ(full->type, AST_NODE_TYPE_ASM);
  EXPECT_EQ(full->asm_stmt.is_volatile, 1);
  ASSERT_NE(full->asm_stmt.template_text, nullptr);
  EXPECT_EQ(std::string(full->asm_stmt.template_text->literal.bytes,
                        full->asm_stmt.template_text->literal.length),
            "mov %2, %0\n")
      << "the template's pieces are joined";
  ASSERT_EQ(full->asm_stmt.operand_count, 3);
  EXPECT_EQ(full->asm_stmt.output_count, 2) << "outputs come first, as GCC numbers them";
  const char *constraints[] = {"=r", "=m", "r"};
  const char *names[] = {"out", "o2", "code"};
  for (int i = 0; i < 3; i++) {
    SCOPED_TRACE(i);
    EXPECT_EQ(std::string(full->asm_stmt.constraints[i]->literal.bytes,
                          full->asm_stmt.constraints[i]->literal.length),
              constraints[i]);
    ASSERT_EQ(full->asm_stmt.operands[i]->type, AST_NODE_TYPE_IDENTIFIER);
    EXPECT_STREQ(full->asm_stmt.operands[i]->tok.value, names[i]);
  }
  ASSERT_EQ(full->asm_stmt.clobber_count, 2);
  EXPECT_EQ(std::string(full->asm_stmt.clobbers[1]->literal.bytes,
                        full->asm_stmt.clobbers[1]->literal.length),
            "cc");
  const struct {
    int statement;
    int is_volatile;
    int operands;
    int outputs;
    int clobbers;
  } rows[] = {{2, 0, 0, 0, 0}, {3, 0, 1, 0, 0}, {4, 0, 0, 0, 0}, {5, 0, 0, 0, 1}};
  for (const auto &row : rows) {
    SCOPED_TRACE(row.statement);
    ast_node *node = function_body_statement(program, 0, row.statement);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->type, AST_NODE_TYPE_ASM);
    EXPECT_EQ(node->asm_stmt.is_volatile, row.is_volatile);
    EXPECT_EQ(node->asm_stmt.operand_count, row.operands);
    EXPECT_EQ(node->asm_stmt.output_count, row.outputs);
    EXPECT_EQ(node->asm_stmt.clobber_count, row.clobbers);
  }
  free_ast(program);
}

TEST(GnuSyntaxTest, ALoneSemicolonIsForgivenOnlyInASystemHeader) {
  _mkdir("parse_sys_dir");
  {
    std::ofstream f("parse_sys_dir/stray.h", std::ios::binary);
    f << "int before;\n;\nint after;\n";
  }
  static const char *dirs[] = {"parse_sys_dir"};
  for (int as_system = 0; as_system <= 1; as_system++) {
    SCOPED_TRACE(as_system ? "system folder" : "programmer's folder");
    token_buf tb;
    pp_run_ex(&tb, "#include <stray.h>\nint main_decl;\n", "main.c", as_system ? nullptr : dirs,
              as_system ? 0 : 1, as_system ? dirs : nullptr, as_system ? 1 : 0);
    parser p;
    parser_init_from_buf(&p, &tb);
    testing::internal::CaptureStderr();
    ast_node *program = parse_program(&p);
    std::string diagnostics = testing::internal::GetCapturedStderr();
    EXPECT_EQ(p.had_error, as_system ? 0 : 1) << diagnostics;
    EXPECT_EQ(program->program.count, 3) << "the declarations around the ';' survive";
    parser_destroy(&p);
    free_ast(program);
  }
  remove("parse_sys_dir/stray.h");
  _rmdir("parse_sys_dir");
  int errors = -1;
  testing::internal::CaptureStderr();
  ast_node *program = parse_preprocessed("int a;\n;\nint b;\n", &errors);
  testing::internal::GetCapturedStderr();
  EXPECT_EQ(errors, 1) << "in the main file a lone ';' stays a syntax error";
  free_ast(program);
}

TEST(GnuSyntaxTest, ExtensionMayPrecedeAnyDeclarationStatementOrExpression) {
  int errors = -1;
  ast_node *program = parse_source("__extension__ __extension__ int a;\n"
                                   "struct S { __extension__ int m; };\n"
                                   "int f(void) {\n"
                                   "  __extension__ int b = __extension__ (int)1;\n"
                                   "  __extension__ b++;\n"
                                   "  return __extension__ b;\n"
                                   "}\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  ASSERT_EQ(program->program.count, 3);
  ast_node *b = function_body_statement(program, 2, 0);
  ASSERT_NE(b, nullptr);
  ASSERT_EQ(b->type, AST_NODE_TYPE_VAR_DECL);
  ASSERT_NE(b->var_decl.init_value, nullptr);
  EXPECT_EQ(b->var_decl.init_value->type, AST_NODE_TYPE_CAST)
      << "__extension__ leaves the expression it marks unchanged";
  ast_node *increment = function_body_statement(program, 2, 1);
  ASSERT_NE(increment, nullptr);
  EXPECT_EQ(increment->type, AST_NODE_TYPE_UNARY_OP);
  free_ast(program);
}

static const ast_node *declared_named(const ast_node *program, const char *name) {
  std::vector<const ast_node *> all;
  gather_nodes(program, all);
  for (const ast_node *n : all) {
    if (n->type == AST_NODE_TYPE_VAR_DECL && n->var_decl.var_name != nullptr &&
        std::strcmp(n->var_decl.var_name, name) == 0)
      return n;
  }
  return nullptr;
}

static const ast_node *record_named(const ast_node *program, const char *tag) {
  std::vector<const ast_node *> all;
  gather_nodes(program, all);
  for (const ast_node *n : all) {
    if (n->type == AST_NODE_TYPE_STRUCT_DEF && n->struct_def.tag_name != nullptr &&
        std::strcmp(n->struct_def.tag_name, tag) == 0)
      return n;
  }
  return nullptr;
}

TEST(AttributeTest, AlignedIsRecordedOnTheThingItNames) {
  int errors = -1;
  ast_node *program = parse_source(
      "typedef int A __attribute__((aligned(16))), B;\n"
      "__attribute__((__aligned__(8))) typedef int C, D;\n"
      "typedef int E __attribute__((aligned(4), aligned(32), aligned(2)));\n"
      "typedef int F __attribute__((aligned));\n"
      "typedef int G __attribute__((aligned(16u))) __attribute__((aligned(0x8)));\n"
      "typedef int H __attribute__((aligned(268435456)));\n"
      "typedef int N1 __attribute__((__aligned(8))), N2 __attribute__((alignedx(8))),\n"
      "    N3 __attribute__((__alignedx__(8))), N4 __attribute__((_aligned_(8)));\n"
      "struct __attribute__((aligned(8))) S1 { int x; };\n"
      "struct S2 { int x; } __attribute__((aligned(4)));\n"
      "struct __attribute__((aligned(2))) S3 { int x; } __attribute__((aligned(8)));\n"
      "typedef __attribute__((aligned(16))) struct S4 { int x; } T4;\n"
      "struct S5 { int m __attribute__((aligned(16))); int n; };\n"
      "struct S6 { int x; };\n",
      &errors);
  EXPECT_EQ(errors, 0);
  struct Expected {
    const char *name;
    int aligned;
  };
  for (Expected e : std::vector<Expected>{{"A", 16},
                                          {"B", 0},
                                          {"C", 8},
                                          {"D", 8},
                                          {"E", 32},
                                          {"F", 16},
                                          {"G", 16},
                                          {"H", 268435456},
                                          {"N1", 0},
                                          {"N2", 0},
                                          {"N3", 0},
                                          {"N4", 0},
                                          {"T4", 16},
                                          {"m", 16},
                                          {"n", 0}}) {
    SCOPED_TRACE(e.name);
    const ast_node *decl = declared_named(program, e.name);
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->var_decl.specs.aligned, e.aligned);
  }
  for (Expected e : std::vector<Expected>{{"S1", 8}, {"S2", 4}, {"S3", 8}, {"S4", 0}, {"S6", 0}}) {
    SCOPED_TRACE(e.name);
    const ast_node *def = record_named(program, e.name);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->struct_def.aligned, e.aligned);
  }
  free_ast(program);
}

TEST(AttributeTest, ModeIsRecordedOnATypedef) {
  int errors = -1;
  ast_node *program = parse_source("typedef int T __attribute__((mode(DI)));\n"
                                   "__attribute__((__mode__(SI))) typedef int U;\n"
                                   "typedef int V;\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  for (const char *name : {"T", "U", "V"}) {
    SCOPED_TRACE(name);
    const ast_node *decl = declared_named(program, name);
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->var_decl.specs.has_mode, std::strcmp(name, "V") != 0);
  }
  free_ast(program);
}

TEST(AttributeTest, AlignedModeAndPackedAreRejectedWhereTheyCannotBeHonoured) {
  const char *mode = "the mode attribute is only supported on a typedef";
  const char *here = "this attribute is not supported here";
  const char *constant = "the aligned attribute needs an integer constant";
  const char *power = "the aligned attribute needs a power of two";
  const char *packed = "the packed attribute is not supported";
  expect_diagnosed({
      {"int v __attribute__((mode(DI)));", mode},
      {"__attribute__((mode(DI))) int h(void) { return 0; }", mode},
      {"void f(int x __attribute__((mode(DI))));", mode},
      {"void g(__attribute__((mode(DI))) int y);", mode},
      {"struct S { int m __attribute__((mode(DI))); };", mode},
      {"int k(a) int a __attribute__((mode(DI))); { return a; }", mode},
      {"long z = sizeof(int __attribute__((mode(DI))));", here},
      {"int *__attribute__((aligned(8))) p;", here},
      {"int (__attribute__((aligned(8))) *q);", here},
      {"long w = sizeof(int __attribute__((aligned(8))));", here},
      {"int x __attribute__((packed));", packed},
      {"struct __attribute__((__packed__)) P { char c; };", packed},
      {"int y __attribute__((aligned(1.5)));", constant},
      {"int y __attribute__((aligned(sizeof(int))));", constant},
      {"int y __attribute__((aligned(2 + 2)));", constant},
      {"int y __attribute__((aligned(08)));", constant},
      {"int y __attribute__((aligned(3)));", power},
      {"int y __attribute__((aligned(0)));", power},
      {"int y __attribute__((aligned(536870912)));", power},
  });
  expect_clean({
      "typedef int T __attribute__((mode(DI)));",
      "int k __attribute__((aligned(268435456)));",
      "void f(int x __attribute__((unused)), char *y __attribute__((unused)));",
      "void g(int a __attribute__((aligned(8)))) {}",
  });
}

TEST(AttributeTest, ARecordTakesThePackStampedOnItsClosingBrace) {
  token_buf tb;
  ASSERT_EQ(pp_run(&tb, "struct S { char c; } s;\nstruct T { char c; };\n"), 0);
  int closing = 0;
  for (int i = 0; i < tb.count; i++) {
    token *t = &tb.tokens[i];
    if (t->type == TOKEN_LBRACE)
      t->pack = 1;
    else if (t->type == TOKEN_RBRACE)
      t->pack = closing++ == 0 ? 4 : 2;
    else if (t->type == TOKEN_IDENTIFIER && std::strcmp(t->value, "s") == 0)
      t->pack = 8;
  }
  parser p;
  parser_init_from_buf(&p, &tb);
  ast_node *program = parse_program(&p);
  EXPECT_EQ(p.had_error, 0);
  parser_destroy(&p);
  token_buf_free(&tb);
  const ast_node *s = record_named(program, "S");
  const ast_node *t = record_named(program, "T");
  ASSERT_NE(s, nullptr);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(s->struct_def.pack, 4) << "not the '{' (1) and not the token after the '}' (8)";
  EXPECT_EQ(t->struct_def.pack, 2);
  free_ast(program);
}

static const ast_node *initializer_of(const ast_node *program, const char *name) {
  const ast_node *decl = declared_named(program, name);
  return decl != nullptr ? decl->var_decl.init_value : nullptr;
}

TEST(BuiltinTest, EachArgumentShapeIsParsed) {
  int errors = -1;
  ast_node *program =
      parse_source("void *ap;\n"
                   "struct S { int a[4]; struct In { char x; double d; } in[3]; };\n"
                   "int va = __builtin_va_arg(ap, int);\n"
                   "unsigned long off = __builtin_offsetof(struct S, in[1].d);\n"
                   "int tc = __builtin_types_compatible_p(__typeof__(va + 1), const int);\n"
                   "unsigned long def = __builtin_offsetof(struct N { int a; char b; }, b);\n"
                   "float hf = __builtin_huge_valf();\n"
                   "int ce = __builtin_choose_expr(1, 2, 3);\n"
                   "int ty = __builtin_types_compatible_p(__typeof(va), int);\n",
                   &errors);
  EXPECT_EQ(errors, 0);

  const ast_node *va = initializer_of(program, "va");
  ASSERT_NE(va, nullptr);
  ASSERT_EQ(va->type, AST_NODE_TYPE_BUILTIN);
  EXPECT_EQ(va->builtin.kind, BUILTIN_VA_ARG);
  EXPECT_STREQ(va->tok.value, "__builtin_va_arg");
  ASSERT_EQ(va->builtin.arg_count, 1);
  EXPECT_EQ(va->builtin.args[0]->type, AST_NODE_TYPE_IDENTIFIER);
  ASSERT_NE(va->builtin.types[0], nullptr);
  EXPECT_EQ(va->builtin.types[0]->prim, PRIM_INT);

  const ast_node *off = initializer_of(program, "off");
  ASSERT_NE(off, nullptr);
  ASSERT_EQ(off->type, AST_NODE_TYPE_BUILTIN);
  EXPECT_EQ(off->builtin.kind, BUILTIN_OFFSETOF);
  ASSERT_NE(off->builtin.types[0], nullptr);
  EXPECT_EQ(off->builtin.types[0]->kind, TYPE_STRUCT);
  ASSERT_EQ(off->builtin.step_count, 3) << "in, [1], d";
  EXPECT_STREQ(off->builtin.steps[0].member, "in");
  EXPECT_EQ(off->builtin.steps[0].index, nullptr);
  EXPECT_EQ(off->builtin.steps[1].member, nullptr);
  ASSERT_NE(off->builtin.steps[1].index, nullptr);
  EXPECT_STREQ(off->builtin.steps[1].index->tok.value, "1");
  EXPECT_STREQ(off->builtin.steps[2].member, "d");

  const ast_node *tc = initializer_of(program, "tc");
  ASSERT_NE(tc, nullptr);
  ASSERT_EQ(tc->type, AST_NODE_TYPE_BUILTIN);
  EXPECT_EQ(tc->builtin.kind, BUILTIN_TYPES_COMPATIBLE_P);
  EXPECT_EQ(tc->builtin.types[0], nullptr) << "a __typeof__ slot holds an expression";
  ASSERT_NE(tc->builtin.type_exprs[0], nullptr);
  EXPECT_EQ(tc->builtin.type_exprs[0]->type, AST_NODE_TYPE_BINARY_OP);
  ASSERT_NE(tc->builtin.types[1], nullptr);
  EXPECT_TRUE(tc->builtin.types[1]->is_const);

  const ast_node *def = initializer_of(program, "def");
  ASSERT_NE(def, nullptr);
  ASSERT_NE(def->builtin.definitions[0], nullptr);
  EXPECT_EQ(def->builtin.definitions[0]->type, AST_NODE_TYPE_STRUCT_DEF);
  EXPECT_STREQ(def->builtin.definitions[0]->struct_def.tag_name, "N");

  const ast_node *hf = initializer_of(program, "hf");
  ASSERT_NE(hf, nullptr);
  ASSERT_EQ(hf->type, AST_NODE_TYPE_BUILTIN);
  EXPECT_EQ(hf->builtin.kind, BUILTIN_HUGE_VALF) << "not the shorter __builtin_huge_val";
  EXPECT_EQ(hf->builtin.arg_count, 0);

  const ast_node *ce = initializer_of(program, "ce");
  ASSERT_NE(ce, nullptr);
  EXPECT_EQ(ce->builtin.kind, BUILTIN_CHOOSE_EXPR);
  EXPECT_EQ(ce->builtin.arg_count, 3);

  const ast_node *ty = initializer_of(program, "ty");
  ASSERT_NE(ty, nullptr);
  EXPECT_NE(ty->builtin.type_exprs[0], nullptr) << "__typeof is the other spelling";
  free_ast(program);
}

TEST(BuiltinTest, OnlyAKnownNameFollowedByAParenthesisIsABuiltin) {
  int errors = -1;
  ast_node *program = parse_source("int __builtin_trap;\n"
                                   "int plain = __builtin_trap;\n"
                                   "int other = __builtin_frobnicate(1);\n"
                                   "int prefix = __builtin_huge_valx();\n",
                                   &errors);
  EXPECT_EQ(errors, 0);
  const ast_node *plain = initializer_of(program, "plain");
  ASSERT_NE(plain, nullptr);
  EXPECT_EQ(plain->type, AST_NODE_TYPE_IDENTIFIER);
  const ast_node *other = initializer_of(program, "other");
  ASSERT_NE(other, nullptr);
  EXPECT_EQ(other->type, AST_NODE_TYPE_FUNCTION_CALL) << "an unknown builtin is an ordinary call";
  const ast_node *prefix = initializer_of(program, "prefix");
  ASSERT_NE(prefix, nullptr);
  EXPECT_EQ(prefix->type, AST_NODE_TYPE_FUNCTION_CALL);
  free_ast(program);
}

TEST(BuiltinTest, MalformedBuiltinsAreReported) {
  expect_diagnosed({
      {"void *ap; int x = __builtin_va_arg(ap int);", "expected ',' between the arguments"},
      {"int x = __builtin_offsetof(1, a);", "expected a type name"},
      {"struct S { int a[2]; }; int x = __builtin_offsetof(struct S, );", "expected a member name"},
      {"struct S { int a[2]; }; int x = __builtin_offsetof(struct S, a.);",
       "expected a member name"},
      {"struct S { int a[2]; }; int x = __builtin_offsetof(struct S, a[1);",
       "expected ']' after the subscript"},
      {"int x = __builtin_types_compatible_p(__typeof__ 1, int);", "expected '(' after __typeof__"},
      {"int x = __builtin_types_compatible_p(__typeof__(1;", "expected ')' after the __typeof__"},
      {"int x = __builtin_types_compatible_p(int; int);", "expected ',' between the arguments"},
      {"int x = __builtin_huge_val(1;", "expected ')' after the arguments"},
      {"int x = __builtin_choose_expr(1, 2 3);", "expected ')' after the arguments"},
  });
}

TEST(BuiltinTest, TheTargetsDeclarationsAreReadFirstAndKeptApart) {
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    TargetGuard guard(kind);
    bool on_linux = kind == TARGET_LINUX_X64;
    SCOPED_TRACE(on_linux ? "linux" : "windows");
    int errors = -1;
    ast_node *program = parse_source("__builtin_va_list ap;\nint after;\n", &errors);
    EXPECT_EQ(errors, 0) << "__builtin_va_list is a typedef name before the first line";
    ASSERT_EQ(program->program.count, 2) << "the user's tokens are parsed after the target's";
    const ast_node *ap = declared_named(program, "ap");
    ASSERT_NE(ap, nullptr);
    EXPECT_EQ(ap->var_decl.type->kind, TYPE_TYPEDEF);
    ast_node *builtins = program->program.builtins;
    ASSERT_NE(builtins, nullptr);
    ASSERT_EQ(builtins->type, AST_NODE_TYPE_DECL_GROUP);
    const ast_node *list = declared_named(builtins, "__builtin_va_list");
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->var_decl.specs.storage_class, TOKEN_TYPEDEF);
    if (on_linux) {
      ASSERT_EQ(list->var_decl.type->kind, TYPE_ARRAY);
      ASSERT_NE(list->var_decl.type->ptr_to, nullptr);
      EXPECT_EQ(list->var_decl.type->ptr_to->kind, TYPE_STRUCT);
      EXPECT_STREQ(list->var_decl.type->ptr_to->tag_name, "__va_list_tag");
      EXPECT_NE(record_named(builtins, "__va_list_tag"), nullptr);
    } else {
      ASSERT_EQ(list->var_decl.type->kind, TYPE_POINTER);
      EXPECT_EQ(list->var_decl.type->ptr_to->prim, PRIM_CHAR);
      EXPECT_EQ(record_named(builtins, "__va_list_tag"), nullptr);
    }
    for (int i = 0; i < program->program.count; i++) {
      const ast_node *decl = program->program.declarations[i];
      EXPECT_FALSE(decl->type == AST_NODE_TYPE_VAR_DECL && decl->var_decl.var_name &&
                   std::strcmp(decl->var_decl.var_name, "__builtin_va_list") == 0)
          << "the target's declarations are not the user's";
    }
    free_ast(program);
  }
}

TEST_F(ParserTest, AnAsmLabelAndTheRecordedAttributesStayOnTheDeclaration) {
  setup_parser("int f(void) __asm__(\"a\" \"b\");\n"
               "__attribute__((__dllimport__)) int v __attribute__((returns_twice, gnu_inline));\n"
               "int w;\n"
               "__attribute__((dllimport, __returns_twice__)) int x(void) __asm__(\"ex\");\n");
  ast_node *program = parse_program(&p);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(program->program.count, 4);
  ast_node *f = program->program.declarations[0];
  ast_node *v = program->program.declarations[1];
  ast_node *w = program->program.declarations[2];
  ast_node *x = program->program.declarations[3];
  ASSERT_EQ(f->type, AST_NODE_TYPE_VAR_DECL);
  ASSERT_EQ(v->type, AST_NODE_TYPE_VAR_DECL);
  ASSERT_EQ(w->type, AST_NODE_TYPE_VAR_DECL);
  ASSERT_EQ(x->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(f->var_decl.asm_label, "ab") << "the label's strings are glued together";
  EXPECT_EQ(v->var_decl.asm_label, nullptr);
  EXPECT_EQ(v->var_decl.specs.dllimport, 1);
  EXPECT_EQ(v->var_decl.specs.returns_twice, 1);
  EXPECT_EQ(v->var_decl.specs.gnu_inline, 1);
  EXPECT_EQ(w->var_decl.specs.dllimport, 0);
  EXPECT_EQ(w->var_decl.specs.returns_twice, 0);
  EXPECT_EQ(w->var_decl.specs.gnu_inline, 0);
  EXPECT_STREQ(x->var_decl.asm_label, "ex");
  EXPECT_EQ(x->var_decl.specs.dllimport, 1);
  EXPECT_EQ(x->var_decl.specs.returns_twice, 1);
  EXPECT_EQ(x->var_decl.specs.gnu_inline, 0);
  free_ast(program);
}
