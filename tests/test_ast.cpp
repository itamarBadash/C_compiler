#include <cstring>
#include <gtest/gtest.h>
#include <map>

extern "C" {
#include "ast.h"
#include "parser.h"
#include <stdlib.h>
}

std::map<void *, int> *tracked_frees = nullptr;

extern "C" {
int ast_free_calls = 0;
void __real_free(void *p);
void __wrap_free(void *p) {
  ast_free_calls++;
  if (tracked_frees != nullptr && p != nullptr) {
    auto it = tracked_frees->find(p);
    if (it != tracked_frees->end()) {
      it->second++;
      return;
    }
  }
  __real_free(p);
}
}

struct TrackFrees {
  explicit TrackFrees(std::map<void *, int> &frees) {
    tracked_frees = &frees;
  }
  ~TrackFrees() {
    tracked_frees = nullptr;
  }
};

TEST(AstTests, CreateNodeSetsTheTypeAndZeroesEverythingElse) {
  ast_node *n = create_ast_node(AST_NODE_TYPE_NUMBER);
  ASSERT_NE(n, nullptr);
  EXPECT_EQ(n->type, AST_NODE_TYPE_NUMBER);
  EXPECT_EQ(n->tok.value, nullptr);
  EXPECT_EQ(n->literal.bytes, nullptr);
  EXPECT_EQ(n->literal.length, 0);
  EXPECT_EQ(n->literal.is_wide, 0);
  EXPECT_EQ(n->symbol, nullptr);
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
  EXPECT_EQ(t->definition, nullptr);
  EXPECT_EQ(t->symbol, nullptr);
  EXPECT_EQ(t->is_vla, 0);
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

TEST(AstTests, FreeAstReleasesADeclarationGroupLikeABlock) {
  ast_node *group = create_ast_node(AST_NODE_TYPE_DECL_GROUP);
  group->block.count = 2;
  group->block.statements = (ast_node **)malloc(sizeof(ast_node *) * 2);
  group->block.statements[0] = create_ast_node(AST_NODE_TYPE_EMPTY);
  group->block.statements[1] = create_ast_node(AST_NODE_TYPE_EMPTY);

  ast_free_calls = 0;
  free_ast(group);
  EXPECT_EQ(ast_free_calls, 4) << "two children, the statement array, and the group";
}

TEST(AstTests, FreeAstReleasesTheParameterSymbolArrayButNotTheSymbols) {
  int first = 0;
  int second = 0;
  ast_node *fn = create_ast_node(AST_NODE_TYPE_FUNCTION_DEF);
  fn->function_def.param_symbols = (struct symbol **)malloc(sizeof(struct symbol *) * 2);
  fn->function_def.param_symbols[0] = (struct symbol *)&first;
  fn->function_def.param_symbols[1] = (struct symbol *)&second;
  void *array = fn->function_def.param_symbols;

  std::map<void *, int> frees;
  frees[array] = 0;
  frees[&first] = 0;
  frees[&second] = 0;
  {
    TrackFrees track(frees);
    free_ast(fn);
  }

  EXPECT_EQ(frees[array], 1);
  EXPECT_EQ(frees[&first], 0) << "the symbol table owns the symbols";
  EXPECT_EQ(frees[&second], 0);
}

TEST(AstTests, FreeAstReleasesAFunctionDefinitionsTypeOnce) {
  ast_node *fn = create_ast_node(AST_NODE_TYPE_FUNCTION_DEF);
  fn->function_def.name = strdup("f");
  fn->function_def.type = create_type_info(TYPE_FUNCTION);
  fn->function_def.type->ptr_to = create_type_info(TYPE_PRIMITIVE);
  fn->function_def.body = create_ast_node(AST_NODE_TYPE_BLOCK);
  void *type = fn->function_def.type;
  void *return_type = fn->function_def.type->ptr_to;

  std::map<void *, int> frees;
  frees[type] = 0;
  frees[return_type] = 0;
  {
    TrackFrees track(frees);
    free_ast(fn);
  }

  EXPECT_EQ(frees[type], 1);
  EXPECT_EQ(frees[return_type], 1);
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

static const char *ownership_source = "typedef int word;\n"
                                      "word add(word a, word b) { return a + b; }\n"
                                      "int main(void) { word x = add(1, 2); return x; }\n";

static std::map<void *, int> buffer_allocations(const token_buf *tb) {
  std::map<void *, int> out;
  out[tb->tokens] = 0;
  for (int i = 0; i < tb->count; i++) {
    if (tb->tokens[i].value != nullptr) {
      out[tb->tokens[i].value] = 0;
    }
  }
  return out;
}

static int not_freed_exactly_once(const std::map<void *, int> &frees) {
  int bad = 0;
  for (const auto &entry : frees) {
    if (entry.second != 1) {
      bad++;
    }
  }
  return bad;
}

TEST(AstTests, ParsingFreesEveryBufferedAllocationExactlyOnce) {
  lexer lex;
  lexer_init(&lex, ownership_source);
  parser p;
  parser_init(&p, &lex);
  std::map<void *, int> frees = buffer_allocations(&p.tokens);
  ASSERT_GT(frees.size(), 10u);

  {
    TrackFrees track(frees);
    ast_node *program = parse_program(&p);
    EXPECT_EQ(p.had_error, 0);
    free_ast(program);
    parser_destroy(&p);
  }

  EXPECT_EQ(not_freed_exactly_once(frees), 0);
}

TEST(AstTests, DestroyingAParserBeforeParsingFreesEveryBufferedAllocationExactlyOnce) {
  lexer lex;
  lexer_init(&lex, ownership_source);
  parser p;
  parser_init(&p, &lex);
  ASSERT_NE(p.current_token.value, nullptr);
  std::map<void *, int> frees = buffer_allocations(&p.tokens);

  {
    TrackFrees track(frees);
    parser_destroy(&p);
  }

  EXPECT_EQ(not_freed_exactly_once(frees), 0);
}

TEST(AstTests, AHandedOverBufferHasEveryAllocationFreedExactlyOnce) {
  token_buf tb;
  token_buf_init(&tb);
  lexer lex;
  lexer_init(&lex, ownership_source);
  token t;
  do {
    t = lexer_next_token(&lex);
    token_buf_push(&tb, t);
  } while (t.type != TOKEN_EOF);
  std::map<void *, int> frees = buffer_allocations(&tb);

  {
    TrackFrees track(frees);
    parser p;
    parser_init_from_buf(&p, &tb);
    ast_node *program = parse_program(&p);
    EXPECT_EQ(p.had_error, 0);
    free_ast(program);
    parser_destroy(&p);
    token_buf_free(&tb);
  }

  EXPECT_EQ(not_freed_exactly_once(frees), 0);
}

TEST(AstTests, IdentifiersInTheTreeAreCopiesNotAliasesOfBufferedTokens) {
  lexer lex;
  lexer_init(&lex, "int f(int a) { return a; }\n");
  parser p;
  parser_init(&p, &lex);
  std::map<void *, int> buffered = buffer_allocations(&p.tokens);
  TrackFrees track(buffered);

  ast_node *program = parse_program(&p);
  ASSERT_NE(program, nullptr);
  ASSERT_EQ(program->program.count, 1);
  ast_node *body = program->program.declarations[0]->function_def.body;
  ASSERT_NE(body, nullptr);
  ASSERT_EQ(body->type, AST_NODE_TYPE_BLOCK);
  ASSERT_EQ(body->block.count, 1);
  ast_node *ret = body->block.statements[0];
  ASSERT_EQ(ret->type, AST_NODE_TYPE_RETURN);
  ast_node *id = ret->return_stmt.return_value;
  ASSERT_NE(id, nullptr);
  ASSERT_EQ(id->type, AST_NODE_TYPE_IDENTIFIER);

  EXPECT_EQ(buffered.count(id->tok.value), 0u);

  parser_destroy(&p);
  EXPECT_STREQ(id->tok.value, "a");
  free_ast(program);
}

TEST(AstTests, FreeReleasesTheDefinitionsATypeNameOrParameterListOwns) {
  ast_node *cast = create_ast_node(AST_NODE_TYPE_CAST);
  cast->cast_expr.type = create_type_info(TYPE_PRIMITIVE);
  cast->cast_expr.definition = create_ast_node(AST_NODE_TYPE_EMPTY);
  ast_node *literal = create_ast_node(AST_NODE_TYPE_COMPOUND_LITERAL);
  literal->compound_literal.type = create_type_info(TYPE_PRIMITIVE);
  literal->compound_literal.definition = create_ast_node(AST_NODE_TYPE_EMPTY);
  type_info *fn = create_type_info(TYPE_FUNCTION);
  fn->param_count = 2;
  fn->param_types = (type_info **)calloc(2, sizeof(type_info *));
  fn->param_names = (char **)calloc(2, sizeof(char *));
  fn->param_definitions = (ast_node **)calloc(2, sizeof(ast_node *));
  fn->param_definitions[1] = create_ast_node(AST_NODE_TYPE_EMPTY);

  std::map<void *, int> frees;
  frees[cast->cast_expr.definition] = 0;
  frees[literal->compound_literal.definition] = 0;
  frees[fn->param_definitions] = 0;
  frees[fn->param_definitions[1]] = 0;
  {
    TrackFrees track(frees);
    free_ast(cast);
    free_ast(literal);
    free_type_info(fn);
  }
  for (const auto &entry : frees)
    EXPECT_EQ(entry.second, 1);
}

TEST(AstTests, FreeAstReleasesEveryPartOfAnAsmStatement) {
  lexer lex;
  lexer_init(&lex, "void f(int a) { __asm__(\"t\" : \"=r\"(a) : \"r\"(a) : \"cc\", \"memory\"); }");
  parser p;
  parser_init(&p, &lex);
  ast_node *program = parse_program(&p);
  ASSERT_EQ(p.had_error, 0);
  parser_destroy(&p);
  ast_node *node = program->program.declarations[0]->function_def.body->block.statements[0];
  ASSERT_EQ(node->type, AST_NODE_TYPE_ASM);
  ASSERT_EQ(node->asm_stmt.operand_count, 2);
  ASSERT_EQ(node->asm_stmt.clobber_count, 2);

  std::map<void *, int> frees;
  frees[node] = 0;
  frees[node->asm_stmt.template_text] = 0;
  frees[node->asm_stmt.constraints] = 0;
  frees[node->asm_stmt.operands] = 0;
  frees[node->asm_stmt.clobbers] = 0;
  for (int i = 0; i < 2; i++) {
    frees[node->asm_stmt.constraints[i]] = 0;
    frees[node->asm_stmt.operands[i]] = 0;
    frees[node->asm_stmt.clobbers[i]] = 0;
  }
  {
    TrackFrees track(frees);
    free_ast(program);
  }
  for (const auto &entry : frees)
    EXPECT_EQ(entry.second, 1);
}

TEST(AstTests, FreeAstReleasesEachDerivedTypeButNothingItPointsTo) {
  ast_node *program = create_ast_node(AST_NODE_TYPE_PROGRAM);
  type_info *borrowed = create_type_info(TYPE_PRIMITIVE);
  type_info *pointer = (type_info *)calloc(1, sizeof(type_info));
  type_info *array = (type_info *)calloc(1, sizeof(type_info));
  pointer->kind = TYPE_POINTER;
  pointer->ptr_to = borrowed;
  array->kind = TYPE_ARRAY;
  array->ptr_to = borrowed;
  program->program.derived_types = (type_info **)malloc(2 * sizeof(type_info *));
  program->program.derived_types[0] = pointer;
  program->program.derived_types[1] = array;
  program->program.derived_count = 2;
  void *list = program->program.derived_types;

  std::map<void *, int> frees;
  frees[pointer] = 0;
  frees[array] = 0;
  frees[list] = 0;
  frees[borrowed] = 0;
  {
    TrackFrees track(frees);
    free_ast(program);
  }
  EXPECT_EQ(frees[pointer], 1);
  EXPECT_EQ(frees[array], 1);
  EXPECT_EQ(frees[list], 1);
  EXPECT_EQ(frees[borrowed], 0) << "a derived type borrows what it points to";
  free_type_info(borrowed);
}

TEST(AstTests, FreeAstReleasesTheFunctionNameType) {
  ast_node *fn = create_ast_node(AST_NODE_TYPE_FUNCTION_DEF);
  fn->function_def.name_type = create_type_info(TYPE_ARRAY);
  fn->function_def.name_type->ptr_to = create_type_info(TYPE_PRIMITIVE);
  void *array = fn->function_def.name_type;
  void *element = fn->function_def.name_type->ptr_to;

  std::map<void *, int> frees;
  frees[array] = 0;
  frees[element] = 0;
  {
    TrackFrees track(frees);
    free_ast(fn);
  }
  EXPECT_EQ(frees[array], 1);
  EXPECT_EQ(frees[element], 1);
}

TEST(AstTests, FreeAstReleasesEveryPartOfABuiltin) {
  lexer lex;
  lexer_init(&lex, "unsigned long long off = __builtin_offsetof(struct N { int a[2]; }, a[1]);\n"
                   "int tc = __builtin_types_compatible_p(__typeof__(off), int);\n"
                   "int ce = __builtin_choose_expr(1, 2, 3);\n");
  parser p;
  parser_init(&p, &lex);
  ast_node *program = parse_program(&p);
  ASSERT_EQ(p.had_error, 0);
  parser_destroy(&p);
  ASSERT_EQ(program->program.count, 3);
  ast_node *off = program->program.declarations[0]->var_decl.init_value;
  ast_node *tc = program->program.declarations[1]->var_decl.init_value;
  ast_node *ce = program->program.declarations[2]->var_decl.init_value;
  ASSERT_EQ(off->type, AST_NODE_TYPE_BUILTIN);
  ASSERT_EQ(tc->type, AST_NODE_TYPE_BUILTIN);
  ASSERT_EQ(ce->type, AST_NODE_TYPE_BUILTIN);
  ASSERT_EQ(off->builtin.step_count, 2);
  ASSERT_EQ(ce->builtin.arg_count, 3);

  std::map<void *, int> frees;
  frees[off] = 0;
  frees[off->tok.value] = 0;
  frees[off->builtin.types[0]] = 0;
  frees[off->builtin.definitions[0]] = 0;
  frees[off->builtin.steps] = 0;
  frees[off->builtin.steps[0].member] = 0;
  frees[off->builtin.steps[1].index] = 0;
  frees[tc->builtin.type_exprs[0]] = 0;
  frees[tc->builtin.types[1]] = 0;
  frees[ce->builtin.args] = 0;
  for (int i = 0; i < 3; i++)
    frees[ce->builtin.args[i]] = 0;
  frees[program->program.builtins] = 0;
  {
    TrackFrees track(frees);
    free_ast(program);
  }
  for (const auto &entry : frees)
    EXPECT_EQ(entry.second, 1);
}
