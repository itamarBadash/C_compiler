#include <gtest/gtest.h>
#include <iostream>

extern "C" {
#include "parser.h"
#include "ast.h"
#include <stdlib.h>
}

class ParserTest : public ::testing::Test {
protected:
    lexer lex;
    parser p;

    void setup_parser(const char* source) {
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
    ast_node* node = parse_expression(&p);
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
    ast_node* node = parse_expression(&p);
    std::cout << "After parse ParsePrimaryIdentifier\n" << std::flush;
    
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_IDENTIFIER);
    EXPECT_STREQ(node->tok.value, "my_var");
    
    free_ast(node);
    std::cout << "End ParsePrimaryIdentifier\n" << std::flush;
}

TEST_F(ParserTest, ParsePrimaryString) {
    setup_parser("\"hello world\"");
    ast_node* node = parse_expression(&p);
    
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_STRING);
    EXPECT_STREQ(node->tok.value, "hello world");
    
    free_ast(node);
}

TEST_F(ParserTest, ParsePrimaryChar) {
    setup_parser("'A'");
    ast_node* node = parse_expression(&p);
    
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_CHAR_LITERAL);
    EXPECT_STREQ(node->tok.value, "A");
    
    free_ast(node);
}

TEST_F(ParserTest, ParsePostfixArray) {
    setup_parser("arr[5]");
    ast_node* node = parse_expression(&p);
    
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
    ast_node* node = parse_expression(&p);
    
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
    ast_node* node = parse_expression(&p);
    
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
    ast_node* node = parse_expression(&p);
    
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_MEMBER_ACCESS);
    EXPECT_EQ(node->member_access.is_pointer, 1);
    EXPECT_STREQ(node->member_access.member_name, "field");
    
    free_ast(node);
}

TEST_F(ParserTest, ParseUnaryOperations) {
    const char* ops[] = {"-", "!", "~", "&", "*", "++", "--", "+"};
    token_type types[] = {TOKEN_MINUS, TOKEN_NOT, TOKEN_TILDE, TOKEN_AMPERSAND, 
                          TOKEN_STAR, TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS, TOKEN_PLUS};
    
    for (int i = 0; i < 8; ++i) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%sx", ops[i]);
        setup_parser(buffer);
        
        ast_node* node = parse_expression(&p);
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
    ast_node* node = parse_expression(&p);
    
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
    ast_node* node = parse_expression(&p);
    
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
    ast_node* node = parse_expression(&p);
    
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
    ast_node* node = parse_expression(&p);
    
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_ASSIGNMENT);
    EXPECT_EQ(node->assignment.op.type, TOKEN_ASSIGN);
    
    ASSERT_NE(node->assignment.right, nullptr);
    EXPECT_EQ(node->assignment.right->type, AST_NODE_TYPE_ASSIGNMENT);
    EXPECT_EQ(node->assignment.right->assignment.op.type, TOKEN_PLUS_ASSIGN);
    
    free_ast(node);
}

TEST_F(ParserTest, ParseCompoundAssignments) {
    const char* ops[] = {"+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "^=", "|="};
    token_type types[] = {TOKEN_PLUS_ASSIGN, TOKEN_MINUS_ASSIGN, TOKEN_STAR_ASSIGN, 
                          TOKEN_SLASH_ASSIGN, TOKEN_PERCENT_ASSIGN, TOKEN_LSHIFT_ASSIGN,
                          TOKEN_RSHIFT_ASSIGN, TOKEN_AMPERSAND_ASSIGN, TOKEN_CARET_ASSIGN, TOKEN_PIPE_ASSIGN};
    
    for (int i = 0; i < 10; ++i) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "x %s 1", ops[i]);
        setup_parser(buffer);
        
        ast_node* node = parse_expression(&p);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->type, AST_NODE_TYPE_ASSIGNMENT);
        EXPECT_EQ(node->assignment.op.type, types[i]);
        
        free_ast(node);
    }
}

TEST_F(ParserTest, ParsePostfixIncrement) {
    setup_parser("x++");
    ast_node* node = parse_expression(&p);
    
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
    ast_node* node = parse_expression(&p);
    
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
    ast_node* node = parse_expression(&p);
    
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
    ast_node* node = parse_expression(&p);
    
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
    ast_node* node = parse_statement(&p);
    
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
    ast_node* node = parse_statement(&p);
    
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
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);
    EXPECT_EQ(node->program.count, 1);
    
    ast_node* block = node->program.declarations[0];
    EXPECT_EQ(block->type, AST_NODE_TYPE_BLOCK);
    EXPECT_EQ(block->block.count, 2);
    
    ast_node* decl1 = block->block.statements[0];
    EXPECT_EQ(decl1->type, AST_NODE_TYPE_VAR_DECL);
    EXPECT_STREQ(decl1->var_decl.var_name, "p");
    EXPECT_EQ(decl1->var_decl.type->kind, TYPE_POINTER);
    EXPECT_EQ(decl1->var_decl.type->ptr_to->kind, TYPE_PRIMITIVE);
    
    ast_node* decl2 = block->block.statements[1];
    EXPECT_EQ(decl2->type, AST_NODE_TYPE_VAR_DECL);
    EXPECT_STREQ(decl2->var_decl.var_name, "arr");
    EXPECT_EQ(decl2->var_decl.type->kind, TYPE_ARRAY);
    EXPECT_EQ(decl2->var_decl.type->array_size, 10);
    EXPECT_EQ(decl2->var_decl.type->ptr_to->kind, TYPE_PRIMITIVE);
    
    free_ast(node);
}

TEST_F(ParserTest, ParseFunctionDefWithPointers) {
    setup_parser("void * alloc(int size) { return 0; }");
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);
    EXPECT_EQ(node->program.count, 1);
    
    ast_node* func = node->program.declarations[0];
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
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);
    EXPECT_EQ(node->program.count, 2);
    
    // First decl: typedef int MyInt;
    ast_node* decl1 = node->program.declarations[0];
    EXPECT_EQ(decl1->type, AST_NODE_TYPE_VAR_DECL);
    EXPECT_STREQ(decl1->var_decl.var_name, "MyInt");
    EXPECT_EQ(decl1->var_decl.is_typedef, 1);
    
    // Second decl: MyInt x;
    ast_node* decl2 = node->program.declarations[1];
    EXPECT_EQ(decl2->type, AST_NODE_TYPE_VAR_DECL);
    EXPECT_STREQ(decl2->var_decl.var_name, "x");
    EXPECT_EQ(decl2->var_decl.is_typedef, 0);
    EXPECT_EQ(decl2->var_decl.type->kind, TYPE_TYPEDEF);
    EXPECT_STREQ(decl2->var_decl.type->tag_name, "MyInt");
    
    free_ast(node);
}

TEST_F(ParserTest, ParseStruct) {
    setup_parser("struct Point p;");
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);
    EXPECT_EQ(node->program.count, 1);
    
    ast_node* decl = node->program.declarations[0];
    EXPECT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);
    EXPECT_STREQ(decl->var_decl.var_name, "p");
    EXPECT_EQ(decl->var_decl.type->kind, TYPE_STRUCT);
    EXPECT_STREQ(decl->var_decl.type->tag_name, "Point");
    
    free_ast(node);
}

TEST_F(ParserTest, ParseInlineStruct) {
    setup_parser("struct Point { int x, y; } p;");
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);
    EXPECT_EQ(node->program.count, 1);
    
    ast_node* block = node->program.declarations[0];
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
    
    ast_node* def = block->block.statements[0];
    EXPECT_EQ(def->type, AST_NODE_TYPE_STRUCT_DEF);
    EXPECT_STREQ(def->struct_def.tag_name, "Point");
    EXPECT_EQ(def->struct_def.member_count, 1);
    
    ast_node* decl = block->block.statements[1];
    EXPECT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);
    EXPECT_STREQ(decl->var_decl.var_name, "p");
    
    free_ast(node);
}

TEST_F(ParserTest, ParseEnum) {
    setup_parser("enum Color { RED, GREEN } c;");
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);
    EXPECT_EQ(node->program.count, 1);
    
    ast_node* block = node->program.declarations[0];
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
    
    ast_node* def = block->block.statements[0];
    EXPECT_EQ(def->type, AST_NODE_TYPE_ENUM_DEF);
    EXPECT_STREQ(def->enum_def.tag_name, "Color");
    EXPECT_EQ(def->enum_def.enumerator_count, 2);
    EXPECT_STREQ(def->enum_def.enumerators[0], "RED");
    EXPECT_STREQ(def->enum_def.enumerators[1], "GREEN");
    
    free_ast(node);
}

TEST_F(ParserTest, ParseTypeCast) {
    setup_parser("int x = (int)3.14;");
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
    ast_node* init = decl->var_decl.init_value;
    
    EXPECT_EQ(init->type, AST_NODE_TYPE_CAST);
    EXPECT_EQ(init->cast_expr.type->kind, TYPE_PRIMITIVE);
    EXPECT_EQ(init->cast_expr.type->base_type.type, TOKEN_INT);
    EXPECT_EQ(init->cast_expr.operand->type, AST_NODE_TYPE_NUMBER);
    
    free_ast(node);
}

TEST_F(ParserTest, ParseSizeofType) {
    setup_parser("int x = sizeof(int*);");
    ast_node* node = parse_program(&p);
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
    ast_node* init = decl->var_decl.init_value;
    
    EXPECT_EQ(init->type, AST_NODE_TYPE_UNARY_OP);
    EXPECT_EQ(init->unary_op.op.type, TOKEN_SIZEOF);
    
    ast_node* dummy_cast = init->unary_op.operand;
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
    ast_node* node = parse_program(&p);
    std::cout << "ParseInitializerList 3\n" << std::flush;
    
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
    ast_node* init = decl->var_decl.init_value;
    std::cout << "ParseInitializerList 4\n" << std::flush;
    
    EXPECT_EQ(init->type, AST_NODE_TYPE_INIT_LIST);
    EXPECT_EQ(init->init_list.count, 3) ;
    std::cout << "ParseInitializerList 5\n" << std::flush;
    EXPECT_EQ(init->init_list.items[0].value->type, AST_NODE_TYPE_NUMBER);
    EXPECT_STREQ(init->init_list.items[0].value->tok.value, "1");
    std::cout << "ParseInitializerList 6\n" << std::flush;
    
    free_ast(node);
    std::cout << "ParseInitializerList 7\n" << std::flush;
}

TEST_F(ParserTest, ParseEnumValues) {
    setup_parser("enum Color { RED = 1, GREEN = 2 } c;");
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_PROGRAM);
    
    ast_node* block = node->program.declarations[0];
    ast_node* def = block->block.statements[0];
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
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
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
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    ast_node* func = node->program.declarations[0];
    EXPECT_EQ(func->type, AST_NODE_TYPE_FUNCTION_DEF);
    
    ast_node* body = func->function_def.body;
    EXPECT_EQ(body->type, AST_NODE_TYPE_BLOCK);
    EXPECT_EQ(body->block.count, 2);
    
    ast_node* goto_stmt = body->block.statements[0];
    EXPECT_EQ(goto_stmt->type, AST_NODE_TYPE_GOTO);
    EXPECT_STREQ(goto_stmt->goto_stmt.label_name, "my_label");
    
    ast_node* label_stmt = body->block.statements[1];
    EXPECT_EQ(label_stmt->type, AST_NODE_TYPE_LABEL);
    EXPECT_STREQ(label_stmt->label_stmt.label_name, "my_label");
    EXPECT_EQ(label_stmt->label_stmt.statement->type, AST_NODE_TYPE_RETURN);
    
    free_ast(node);
}

TEST_F(ParserTest, ParseQualifiersAndStorage) {
    setup_parser("static const volatile int x;");
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
    EXPECT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);
    
    EXPECT_EQ(decl->var_decl.type->is_const, 1);
    EXPECT_EQ(decl->var_decl.type->is_volatile, 1);
    EXPECT_EQ(decl->var_decl.type->storage_class, TOKEN_STATIC);
    EXPECT_EQ(decl->var_decl.type->kind, TYPE_PRIMITIVE);
    
    free_ast(node);
}

TEST_F(ParserTest, ParseForLoopDeclaration) {
    setup_parser("for(int i = 0; i < 10; i++) {}");
    ast_node* node = parse_statement(&p);
    
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type, AST_NODE_TYPE_FOR);
    
    ASSERT_NE(node->for_stmt.init, nullptr);
    EXPECT_EQ(node->for_stmt.init->type, AST_NODE_TYPE_VAR_DECL);
    EXPECT_STREQ(node->for_stmt.init->var_decl.var_name, "i");
    
    free_ast(node);
}

TEST_F(ParserTest, ParseLongLongAndComplex) {
    setup_parser("long long _Complex x;");
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
    EXPECT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);
    
    EXPECT_EQ(decl->var_decl.type->is_long_long, 1);
    EXPECT_EQ(decl->var_decl.type->is_complex, 1);
    
    free_ast(node);
}

TEST_F(ParserTest, ParseHexFloat) {
    setup_parser("float x = 0x1.fp3;");
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
    ast_node* init = decl->var_decl.init_value;
    
    EXPECT_EQ(init->type, AST_NODE_TYPE_NUMBER);
    EXPECT_STREQ(init->tok.value, "0x1.fp3");
    
    free_ast(node);
}

TEST_F(ParserTest, ParseVLA) {
    setup_parser("int arr[n * 2];");
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
    EXPECT_EQ(decl->var_decl.type->kind, TYPE_ARRAY);
    
    ast_node* size_expr = decl->var_decl.type->array_size_expr;
    ASSERT_NE(size_expr, nullptr);
    EXPECT_EQ(size_expr->type, AST_NODE_TYPE_BINARY_OP);
    
    free_ast(node);
}

TEST_F(ParserTest, ParseCompoundLiteral) {
    setup_parser("void f() { (struct Point){1, 2}; }");
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    ast_node* func = node->program.declarations[0];
    ast_node* stmt = func->function_def.body->block.statements[0];
    
    EXPECT_EQ(stmt->type, AST_NODE_TYPE_COMPOUND_LITERAL);
    EXPECT_EQ(stmt->compound_literal.type->kind, TYPE_STRUCT);
    EXPECT_EQ(stmt->compound_literal.init_list->type, AST_NODE_TYPE_INIT_LIST);
    EXPECT_EQ(stmt->compound_literal.init_list->init_list.count, 2);
    
    free_ast(node);
}

TEST_F(ParserTest, ParseDesignatedInitializer) {
    setup_parser("struct Point p = { .x = 1, [0] = 5 };");
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
    ast_node* init = decl->var_decl.init_value;
    
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
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
    EXPECT_EQ(decl->type, AST_NODE_TYPE_VAR_DECL);
    EXPECT_EQ(decl->var_decl.type->kind, TYPE_PRIMITIVE);
    EXPECT_EQ(decl->var_decl.type->base_type.type, TOKEN_BOOL);
    
    free_ast(node);
}

TEST_F(ParserTest, ParseVariadic) {
    setup_parser("int printf(const char *format, ...);");
    ast_node* node = parse_program(&p);
    
    ASSERT_NE(node, nullptr);
    ast_node* func = node->program.declarations[0];
    EXPECT_EQ(func->type, AST_NODE_TYPE_FUNCTION_DEF);
    EXPECT_EQ(func->function_def.return_type->is_variadic, 1);
    
    free_ast(node);
}

TEST_F(ParserTest, ParseMixedDeclarations) {
    setup_parser("void f() { int a = 1; a = 2; int b = 3; }");
    ast_node* node = parse_program(&p);
    ASSERT_NE(node, nullptr);
    ast_node* func = node->program.declarations[0];
    ast_node* block = func->function_def.body;
    EXPECT_EQ(block->block.count, 3);
    EXPECT_EQ(block->block.statements[0]->type, AST_NODE_TYPE_VAR_DECL);
    EXPECT_EQ(block->block.statements[1]->type, AST_NODE_TYPE_ASSIGNMENT);
    EXPECT_EQ(block->block.statements[2]->type, AST_NODE_TYPE_VAR_DECL);
    free_ast(node);
}

TEST_F(ParserTest, ParseComplexVLA) {
    setup_parser("void f(int n, int m) { int arr[n * 2 + m]; }");
    ast_node* node = parse_program(&p);
    ASSERT_NE(node, nullptr);
    ast_node* func = node->program.declarations[0];
    ast_node* block = func->function_def.body;
    ast_node* decl = block->block.statements[0];
    EXPECT_EQ(decl->var_decl.type->kind, TYPE_ARRAY);
    EXPECT_EQ(decl->var_decl.type->array_size_expr->type, AST_NODE_TYPE_BINARY_OP);
    free_ast(node);
}

TEST_F(ParserTest, ParseMultipleDesignatedInitializers) {
    setup_parser("struct Point p = { .x = 10, .y = 20 };");
    ast_node* node = parse_program(&p);
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
    ast_node* init = decl->var_decl.init_value;
    EXPECT_EQ(init->init_list.count, 2);
    EXPECT_STREQ(init->init_list.items[0].member_name, "x");
    EXPECT_STREQ(init->init_list.items[1].member_name, "y");
    free_ast(node);
}

TEST_F(ParserTest, ParseArrayDesignatedInitializers) {
    setup_parser("int arr[10] = { [2] = 5, [5] = 8 };");
    ast_node* node = parse_program(&p);
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
    ast_node* init = decl->var_decl.init_value;
    EXPECT_EQ(init->init_list.count, 2);
    ASSERT_NE(init->init_list.items[0].index, nullptr);
    EXPECT_STREQ(init->init_list.items[0].index->tok.value, "2");
    ASSERT_NE(init->init_list.items[1].index, nullptr);
    EXPECT_STREQ(init->init_list.items[1].index->tok.value, "5");
    free_ast(node);
}

TEST_F(ParserTest, ParseRestrictAndInline) {
    setup_parser("static inline void f(int * restrict p);");
    ast_node* node = parse_program(&p);
    ASSERT_NE(node, nullptr);
    ast_node* func_decl = node->program.declarations[0];
    EXPECT_EQ(func_decl->type, AST_NODE_TYPE_FUNCTION_DEF);
    EXPECT_EQ(func_decl->function_def.return_type->storage_class, TOKEN_STATIC);
    EXPECT_EQ(func_decl->function_def.param_types[0]->is_restrict, 1);
    free_ast(node);
}

TEST_F(ParserTest, ParseEnumTrailingComma) {
    setup_parser("enum State { START, STOP, };");
    ast_node* node = parse_program(&p);
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
    EXPECT_EQ(decl->type, AST_NODE_TYPE_ENUM_DEF);
    EXPECT_EQ(decl->enum_def.enumerator_count, 2);
    free_ast(node);
}

TEST_F(ParserTest, ParseFlexibleArrayMember) {
    setup_parser("struct Buffer { int length; char data[]; };");
    ast_node* node = parse_program(&p);
    ASSERT_NE(node, nullptr);
    ast_node* decl = node->program.declarations[0];
    EXPECT_EQ(decl->type, AST_NODE_TYPE_STRUCT_DEF);
    EXPECT_EQ(decl->struct_def.member_count, 2);
    ast_node* fam = decl->struct_def.members[1];
    EXPECT_EQ(fam->var_decl.type->kind, TYPE_ARRAY);
    EXPECT_EQ(fam->var_decl.type->array_size, -1);
    EXPECT_EQ(fam->var_decl.type->array_size_expr, nullptr);
    free_ast(node);
}

TEST_F(ParserTest, ParseMultipleVariadicArgs) {
    setup_parser("int printf(int count, const char *format, ...);");
    ast_node* node = parse_program(&p);
    ASSERT_NE(node, nullptr);
    ast_node* func = node->program.declarations[0];
    EXPECT_EQ(func->function_def.param_count, 2);
    EXPECT_EQ(func->function_def.return_type->is_variadic, 1);
    free_ast(node);
}
