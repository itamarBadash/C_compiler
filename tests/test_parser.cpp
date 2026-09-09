#include <gtest/gtest.h>
#include <iostream>

extern "C" {
#include "ast.h"
#include "parser.h"
#include <stdlib.h>
}

class ParserTest : public ::testing::Test {
protected:
  lexer lex;
  parser p;

  void setup_parser(const char *source) {
    lexer_init(&lex, source);
    parser_init(&p, &lex);
  }

  void TearDown() override {
    std::cout << "Entering TearDown\n" << std::flush;
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
  EXPECT_EQ(block->type, AST_NODE_TYPE_BLOCK);
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
  EXPECT_EQ(decl2->var_decl.type->array_size, 10);
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

  ASSERT_NE(func->function_def.return_type, nullptr);
  EXPECT_EQ(func->function_def.return_type->kind, TYPE_POINTER);
  EXPECT_EQ(func->function_def.return_type->ptr_to->kind, TYPE_PRIMITIVE);
  EXPECT_EQ(func->function_def.return_type->ptr_to->base_type.type, TOKEN_VOID);

  EXPECT_EQ(func->function_def.param_count, 1);
  EXPECT_STREQ(func->function_def.parameters[0], "size");
  EXPECT_EQ(func->function_def.param_types[0]->kind, TYPE_PRIMITIVE);

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
  EXPECT_EQ(decl1->var_decl.is_typedef, 1);

  // Second decl: MyInt x;
  ast_node *decl2 = node->program.declarations[1];
  EXPECT_EQ(decl2->type, AST_NODE_TYPE_VAR_DECL);
  EXPECT_STREQ(decl2->var_decl.var_name, "x");
  EXPECT_EQ(decl2->var_decl.is_typedef, 0);
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
  EXPECT_EQ(block->type, AST_NODE_TYPE_BLOCK);
  if (block->type != AST_NODE_TYPE_BLOCK) {
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
  EXPECT_EQ(block->type, AST_NODE_TYPE_BLOCK);
  if (block->type != AST_NODE_TYPE_BLOCK) {
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
  EXPECT_EQ(init->cast_expr.type->base_type.type, TOKEN_INT);
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
  EXPECT_EQ(decl->var_decl.type->array_size, 10);

  EXPECT_EQ(decl->var_decl.type->ptr_to->kind, TYPE_ARRAY);
  EXPECT_EQ(decl->var_decl.type->ptr_to->array_size, 20);

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
  EXPECT_EQ(decl->var_decl.type->storage_class, TOKEN_STATIC);
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

TEST_F(ParserTest, ParseLongLongAndComplex) {
  setup_parser("long long _Complex x;");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *decl = node->program.declarations[0];
  EXPECT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);

  EXPECT_EQ(decl->var_decl.type->is_long_long, 1);
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
  EXPECT_EQ(decl->var_decl.type->base_type.type, TOKEN_BOOL);

  free_ast(node);
}

TEST_F(ParserTest, ParseVariadic) {
  setup_parser("int printf(const char *format, ...);");
  ast_node *node = parse_program(&p);

  ASSERT_NE(node, nullptr);
  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  EXPECT_EQ(func->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_EQ(func->function_def.return_type->is_variadic, 1);

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
  EXPECT_EQ(func_decl->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_EQ(func_decl->function_def.return_type->storage_class, TOKEN_STATIC);
  EXPECT_EQ(func_decl->function_def.param_types[0]->is_restrict, 1);
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
  EXPECT_EQ(func->function_def.param_count, 2);
  EXPECT_EQ(func->function_def.return_type->is_variadic, 1);
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
  EXPECT_EQ(t->ptr_to->param_types[0]->base_type.type, TOKEN_INT);
  EXPECT_EQ(t->ptr_to->param_types[1]->base_type.type, TOKEN_INT);
  ASSERT_EQ(t->ptr_to->ptr_to->kind, TYPE_PRIMITIVE);
  EXPECT_EQ(t->ptr_to->ptr_to->base_type.type, TOKEN_INT);

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
  EXPECT_EQ(t->ptr_to->array_size, 10);
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
  EXPECT_EQ(t->array_size, 10);
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
  EXPECT_EQ(t->array_size, 3);
  ASSERT_EQ(t->ptr_to->kind, TYPE_ARRAY);
  EXPECT_EQ(t->ptr_to->array_size, 4);
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
  EXPECT_EQ(t->array_size, 2);
  ASSERT_EQ(t->ptr_to->kind, TYPE_ARRAY);
  EXPECT_EQ(t->ptr_to->array_size, 3);
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
  EXPECT_EQ(t->array_size, 3);
  ASSERT_EQ(t->ptr_to->kind, TYPE_POINTER);
  ASSERT_EQ(t->ptr_to->ptr_to->kind, TYPE_FUNCTION);
  EXPECT_EQ(t->ptr_to->ptr_to->param_count, 0);
  EXPECT_EQ(t->ptr_to->ptr_to->ptr_to->base_type.type, TOKEN_INT);

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
  EXPECT_EQ(t->ptr_to->ptr_to->ptr_to->base_type.type, TOKEN_CHAR);

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
  EXPECT_EQ(t->ptr_to->base_type.type, TOKEN_CHAR);

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
  EXPECT_EQ(td->var_decl.is_typedef, 1);
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
  ASSERT_EQ(func->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_STREQ(func->function_def.name, "q");
  ASSERT_EQ(func->function_def.param_count, 1);
  EXPECT_STREQ(func->function_def.parameters[0], "cmp");

  type_info *cmp = func->function_def.param_types[0];
  ASSERT_EQ(cmp->kind, TYPE_POINTER);
  ASSERT_EQ(cmp->ptr_to->kind, TYPE_FUNCTION);
  EXPECT_EQ(cmp->ptr_to->param_count, 2);
  EXPECT_EQ(cmp->ptr_to->param_types[0]->kind, TYPE_POINTER);
  EXPECT_EQ(cmp->ptr_to->ptr_to->base_type.type, TOKEN_INT);

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
  EXPECT_EQ(t->ptr_to->ptr_to->ptr_to->array_size, 3);

  free_ast(node);
}

TEST_F(ParserTest, ParseFunctionReturningFunctionPointer) {
  setup_parser("void (*signal(int sig, void (*h)(int)))(int);");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);
  ASSERT_EQ(node->program.count, 1);

  ast_node *func = node->program.declarations[0];
  ASSERT_EQ(func->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_STREQ(func->function_def.name, "signal");
  ASSERT_EQ(func->function_def.param_count, 2);
  EXPECT_STREQ(func->function_def.parameters[0], "sig");
  EXPECT_STREQ(func->function_def.parameters[1], "h");

  type_info *h = func->function_def.param_types[1];
  ASSERT_EQ(h->kind, TYPE_POINTER);
  ASSERT_EQ(h->ptr_to->kind, TYPE_FUNCTION);
  EXPECT_EQ(h->ptr_to->param_count, 1);

  type_info *ret = func->function_def.return_type;
  ASSERT_EQ(ret->kind, TYPE_POINTER);
  ASSERT_EQ(ret->ptr_to->kind, TYPE_FUNCTION);
  EXPECT_EQ(ret->ptr_to->param_count, 1);
  EXPECT_EQ(ret->ptr_to->ptr_to->base_type.type, TOKEN_VOID);

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
  ASSERT_EQ(func->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_EQ(func->function_def.param_count, 0);

  free_ast(node);
}

TEST_F(ParserTest, ParseEmptyParameterList) {
  setup_parser("int f();");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  ASSERT_EQ(func->type, AST_NODE_TYPE_FUNCTION_DEF);
  EXPECT_EQ(func->function_def.param_count, 0);

  free_ast(node);
}

TEST_F(ParserTest, ParseUnnamedParameters) {
  setup_parser("int f(int, char *);");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  ASSERT_EQ(func->function_def.param_count, 2);
  EXPECT_EQ(func->function_def.parameters[0], nullptr);
  EXPECT_EQ(func->function_def.parameters[1], nullptr);
  EXPECT_EQ(func->function_def.param_types[0]->base_type.type, TOKEN_INT);
  EXPECT_EQ(func->function_def.param_types[1]->kind, TYPE_POINTER);

  free_ast(node);
}

TEST_F(ParserTest, ParseStaticArrayParameter) {
  setup_parser("void f(int a[static 4]);");
  ast_node *node = parse_program(&p);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(p.had_error, 0);

  ASSERT_GE(node->program.count, 1);
  ast_node *func = node->program.declarations[0];
  ASSERT_EQ(func->function_def.param_count, 1);
  EXPECT_STREQ(func->function_def.parameters[0], "a");
  ASSERT_EQ(func->function_def.param_types[0]->kind, TYPE_ARRAY);
  EXPECT_EQ(func->function_def.param_types[0]->array_size, 4);

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
  EXPECT_EQ(t->ptr_to->base_type.type, TOKEN_CHAR);

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
  EXPECT_EQ(val->cast_expr.type->base_type.type, TOKEN_BOOL);

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
  EXPECT_EQ(node->program.declarations[0]->function_def.return_type->is_variadic, 1);
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

TEST_F(ParserTest, ConcatenationDecodesTheJoinedText) {
  setup_parser("char *s = \"a\\n\" \"b\";");
  ast_node *node = parse_program(&p);
  ast_node *init = node->program.declarations[0]->var_decl.init_value;
  ASSERT_EQ(init->literal.length, 3);
  EXPECT_EQ(init->literal.bytes[1], '\n');
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
