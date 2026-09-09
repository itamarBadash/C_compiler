#include <gtest/gtest.h>
#include <string>
extern "C" {
#include "symbol_table.h"
}

class SymbolTableTest : public ::testing::Test {
protected:
  symbol_table *table;

  void SetUp() override {
    table = symbol_table_create();
    symbol_table_enter_scope(table, SCOPE_FILE);
  }

  void TearDown() override {
    symbol_table_destroy(table);
  }
};

TEST_F(SymbolTableTest, NullArgumentsAreRejectedWithoutCrashing) {
  EXPECT_EQ(-1,
            symbol_table_insert_ordinary(nullptr, "x", SYMBOL_VAR, nullptr, LINKAGE_NONE, 0, 0));
  EXPECT_EQ(-1,
            symbol_table_insert_ordinary(table, nullptr, SYMBOL_VAR, nullptr, LINKAGE_NONE, 0, 0));
  EXPECT_EQ(-1, symbol_table_insert_tag(nullptr, "S", nullptr));
  EXPECT_EQ(-1, symbol_table_insert_label(nullptr, "L"));
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary(nullptr, "x"));
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary(table, nullptr));
  EXPECT_EQ(nullptr, symbol_table_lookup_tag(nullptr, "S"));
  EXPECT_EQ(nullptr, symbol_table_lookup_label(nullptr, "L"));
}

TEST_F(SymbolTableTest, LookupOfAnUnknownNameReturnsNull) {
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary(table, "nope"));
  EXPECT_EQ(nullptr, symbol_table_lookup_tag(table, "nope"));
}

TEST_F(SymbolTableTest, RedeclarationInABlockScopeIsRejected) {
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  EXPECT_EQ(0, symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, LINKAGE_NONE, 0, 1));
  EXPECT_EQ(-1, symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, LINKAGE_NONE, 0, 1))
      << "a block scope has no tentative-definition merging";
  symbol_table_leave_scope(table);
}

TEST_F(SymbolTableTest, TwoDefinitionsAtFileScopeAreRejected) {
  EXPECT_EQ(0,
            symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, LINKAGE_EXTERNAL, 0, 1));
  EXPECT_EQ(-1,
            symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, LINKAGE_EXTERNAL, 0, 1));
}

TEST_F(SymbolTableTest, ATentativeDefinitionIsCompletedByARealOne) {
  EXPECT_EQ(0,
            symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, LINKAGE_EXTERNAL, 1, 0));
  EXPECT_EQ(0,
            symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, LINKAGE_EXTERNAL, 0, 1));
  symbol *sym = symbol_table_lookup_ordinary(table, "x");
  ASSERT_NE(sym, nullptr);
  EXPECT_EQ(sym->is_defined, 1);
  EXPECT_EQ(sym->is_tentative, 0);
}

TEST_F(SymbolTableTest, LeavingAScopeDiscardsItsSymbols) {
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol_table_insert_ordinary(table, "inner", SYMBOL_VAR, nullptr, LINKAGE_NONE, 0, 1);
  ASSERT_NE(nullptr, symbol_table_lookup_ordinary(table, "inner"));
  symbol_table_leave_scope(table);
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary(table, "inner"));
}

TEST_F(SymbolTableTest, LookupWalksOutwardThroughSeveralScopes) {
  symbol_table_insert_ordinary(table, "outer", SYMBOL_VAR, nullptr, LINKAGE_EXTERNAL, 0, 1);
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol *sym = symbol_table_lookup_ordinary(table, "outer");
  ASSERT_NE(sym, nullptr);
  EXPECT_STREQ(sym->name, "outer");
  symbol_table_leave_scope(table);
  symbol_table_leave_scope(table);
  symbol_table_leave_scope(table);
}

TEST_F(SymbolTableTest, TagsAndLabelsAreSeparateNamespaces) {
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  EXPECT_EQ(0, symbol_table_insert_ordinary(table, "n", SYMBOL_VAR, nullptr, LINKAGE_NONE, 0, 1));
  EXPECT_EQ(0, symbol_table_insert_tag(table, "n", nullptr));
  EXPECT_EQ(0, symbol_table_insert_label(table, "n"));

  EXPECT_NE(nullptr, symbol_table_lookup_ordinary(table, "n"));
  EXPECT_NE(nullptr, symbol_table_lookup_tag(table, "n"));
  EXPECT_NE(nullptr, symbol_table_lookup_label(table, "n"));
  symbol_table_leave_scope(table);
}

TEST_F(SymbolTableTest, ADuplicateTagInTheSameScopeIsRejected) {
  EXPECT_EQ(0, symbol_table_insert_tag(table, "S", nullptr));
  EXPECT_EQ(-1, symbol_table_insert_tag(table, "S", nullptr));
}

TEST_F(SymbolTableTest, ADuplicateLabelInTheSameFunctionIsRejected) {
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  EXPECT_EQ(0, symbol_table_insert_label(table, "L"));
  EXPECT_EQ(-1, symbol_table_insert_label(table, "L"));
  symbol_table_leave_scope(table);
}

TEST_F(SymbolTableTest, LabelsAreVisibleFromNestedBlocksOfTheSameFunction) {
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  symbol_table_insert_label(table, "L");
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  EXPECT_NE(nullptr, symbol_table_lookup_label(table, "L"))
      << "labels live in the function scope, not the enclosing block";
  symbol_table_leave_scope(table);
  symbol_table_leave_scope(table);
  symbol_table_leave_scope(table);
}

TEST_F(SymbolTableTest, LabelsDoNotLeakBetweenFunctions) {
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  symbol_table_insert_label(table, "L");
  symbol_table_leave_scope(table);

  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  EXPECT_EQ(nullptr, symbol_table_lookup_label(table, "L"));
  symbol_table_leave_scope(table);
}

TEST_F(SymbolTableTest, ShadowingRestoresTheOuterSymbolOnExit) {
  symbol_table_insert_ordinary(table, "x", SYMBOL_FUNC, nullptr, LINKAGE_EXTERNAL, 0, 1);
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, LINKAGE_NONE, 0, 1);
  EXPECT_EQ(SYMBOL_VAR, symbol_table_lookup_ordinary(table, "x")->kind);
  symbol_table_leave_scope(table);
  EXPECT_EQ(SYMBOL_FUNC, symbol_table_lookup_ordinary(table, "x")->kind);
}

TEST_F(SymbolTableTest, ManyScopesAndSymbolsDoNotCrash) {
  for (int depth = 0; depth < 50; depth++) {
    symbol_table_enter_scope(table, SCOPE_BLOCK);
    for (int i = 0; i < 20; i++) {
      std::string name = "v" + std::to_string(depth) + "_" + std::to_string(i);
      EXPECT_EQ(0, symbol_table_insert_ordinary(table, name.c_str(), SYMBOL_VAR, nullptr,
                                                LINKAGE_NONE, 0, 1));
    }
  }
  EXPECT_NE(nullptr, symbol_table_lookup_ordinary(table, "v0_0"));
  for (int depth = 0; depth < 50; depth++) {
    symbol_table_leave_scope(table);
  }
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary(table, "v0_0"));
}

TEST_F(SymbolTableTest, BasicInsertionAndLookup) {
  type_info *t = NULL; // Dummy type
  EXPECT_EQ(0, symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, t, LINKAGE_EXTERNAL, 1, 0));
  symbol *sym = symbol_table_lookup_ordinary(table, "x");
  ASSERT_NE(sym, nullptr);
  EXPECT_STREQ(sym->name, "x");
  EXPECT_EQ(sym->kind, SYMBOL_VAR);
  EXPECT_EQ(sym->linkage, LINKAGE_EXTERNAL);
}

TEST_F(SymbolTableTest, BlockScopeShadowing) {
  symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, NULL, LINKAGE_EXTERNAL, 1, 0);

  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, NULL, LINKAGE_NONE, 0, 1); // inner x

  symbol *sym = symbol_table_lookup_ordinary(table, "x");
  ASSERT_NE(sym, nullptr);
  EXPECT_EQ(sym->linkage, LINKAGE_NONE);

  symbol_table_leave_scope(table);

  sym = symbol_table_lookup_ordinary(table, "x");
  ASSERT_NE(sym, nullptr);
  EXPECT_EQ(sym->linkage, LINKAGE_EXTERNAL);
}

TEST_F(SymbolTableTest, LabelScopeHandling) {
  symbol_table_enter_scope(table, SCOPE_FUNCTION); // Enter a function

  symbol_table_enter_scope(table, SCOPE_BLOCK); // Deeply nested block
  symbol_table_enter_scope(table, SCOPE_BLOCK);

  EXPECT_EQ(0, symbol_table_insert_label(table, "loop_start"));

  symbol_table_leave_scope(table);
  symbol_table_leave_scope(table); // Back to function scope

  // Should still be able to find the label from anywhere in the function
  symbol *lbl = symbol_table_lookup_label(table, "loop_start");
  ASSERT_NE(lbl, nullptr);
  EXPECT_STREQ(lbl->name, "loop_start");
  EXPECT_EQ(lbl->kind, SYMBOL_LABEL);

  symbol_table_leave_scope(table); // Leave function scope

  // After leaving function scope, label should be gone
  EXPECT_EQ(nullptr, symbol_table_lookup_label(table, "loop_start"));
}

TEST_F(SymbolTableTest, TentativeDefinitions) {
  // Insert tentative definition
  EXPECT_EQ(0, symbol_table_insert_ordinary(table, "global_var", SYMBOL_VAR, NULL, LINKAGE_EXTERNAL,
                                            1, 0));

  // Insert it again (e.g. another declaration)
  EXPECT_EQ(0, symbol_table_insert_ordinary(table, "global_var", SYMBOL_VAR, NULL, LINKAGE_EXTERNAL,
                                            1, 0));

  // Insert the actual definition
  EXPECT_EQ(0, symbol_table_insert_ordinary(table, "global_var", SYMBOL_VAR, NULL, LINKAGE_EXTERNAL,
                                            0, 1));

  // Inserting a definition again should fail (Redefinition)
  EXPECT_EQ(-1, symbol_table_insert_ordinary(table, "global_var", SYMBOL_VAR, NULL,
                                             LINKAGE_EXTERNAL, 0, 1));
}

TEST_F(SymbolTableTest, TagsAndOrdinaryNamespaces) {
  EXPECT_EQ(0,
            symbol_table_insert_ordinary(table, "Point", SYMBOL_VAR, NULL, LINKAGE_EXTERNAL, 1, 0));
  EXPECT_EQ(0, symbol_table_insert_tag(table, "Point", NULL)); // Same name, different namespace

  symbol *ord = symbol_table_lookup_ordinary(table, "Point");
  ASSERT_NE(ord, nullptr);
  EXPECT_EQ(ord->kind, SYMBOL_VAR);

  symbol *tag = symbol_table_lookup_tag(table, "Point");
  ASSERT_NE(tag, nullptr);
  EXPECT_EQ(tag->kind, SYMBOL_TAG);
}
