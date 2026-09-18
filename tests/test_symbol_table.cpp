#include <gtest/gtest.h>
#include <map>
#include <string>
extern "C" {
#include "parser.h"
#include "symbol_table.h"
}

std::map<void *, int> *tracked_frees = nullptr;

extern "C" {
int st_free_calls = 0;
void __real_free(void *p);
void __wrap_free(void *p) {
  st_free_calls++;
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

static source_loc at(int line, int column) {
  source_loc loc;
  loc.line = line;
  loc.column = column;
  return loc;
}

static void track_symbol(std::map<void *, int> &frees, symbol *sym) {
  frees[sym] = 0;
  frees[sym->name] = 0;
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
  ASSERT_NE(nullptr, symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, at(1, 1)));
  ASSERT_NE(nullptr, symbol_table_insert_tag(table, "S", nullptr, at(1, 1)));
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  ASSERT_NE(nullptr, symbol_table_insert_label(table, "L", at(1, 1)));

  EXPECT_EQ(nullptr, symbol_table_insert_ordinary(nullptr, "x", SYMBOL_VAR, nullptr, at(1, 1)));
  EXPECT_EQ(nullptr, symbol_table_insert_ordinary(table, nullptr, SYMBOL_VAR, nullptr, at(1, 1)));
  EXPECT_EQ(nullptr, symbol_table_insert_tag(nullptr, "S", nullptr, at(1, 1)));
  EXPECT_EQ(nullptr, symbol_table_insert_label(nullptr, "L", at(1, 1)));
  EXPECT_EQ(nullptr, symbol_table_insert_label(table, nullptr, at(1, 1)));

  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary(nullptr, "x"));
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary(table, nullptr));
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary_current(nullptr, "x"));
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary_current(table, nullptr));
  EXPECT_EQ(nullptr, symbol_table_lookup_tag(nullptr, "S"));
  EXPECT_EQ(nullptr, symbol_table_lookup_tag(table, nullptr));
  EXPECT_EQ(nullptr, symbol_table_lookup_tag_current(nullptr, "S"));
  EXPECT_EQ(nullptr, symbol_table_lookup_tag_current(table, nullptr));
  EXPECT_EQ(nullptr, symbol_table_lookup_label(nullptr, "L"));
  EXPECT_EQ(nullptr, symbol_table_lookup_label(table, nullptr));

  symbol_table_enter_scope(nullptr, SCOPE_BLOCK);
  symbol_table_leave_scope(nullptr);
  symbol_table_destroy(nullptr);
  symbol_table_leave_scope(table);
}

TEST(SymbolTableCreateTest, ATableStartsWithNoScopeAndNoSymbols) {
  symbol_table *table = symbol_table_create();
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->current_scope, nullptr);
  EXPECT_EQ(table->all_symbols, nullptr);

  EXPECT_EQ(nullptr, symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, at(1, 1)));
  EXPECT_EQ(nullptr, symbol_table_insert_tag(table, "S", nullptr, at(1, 1)));
  EXPECT_EQ(nullptr, symbol_table_insert_label(table, "L", at(1, 1)));
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary_current(table, "x"));
  EXPECT_EQ(nullptr, symbol_table_lookup_tag_current(table, "S"));
  EXPECT_EQ(table->all_symbols, nullptr);
  symbol_table_destroy(table);
}

TEST_F(SymbolTableTest, LookupOfAnUnknownNameReturnsNull) {
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary(table, "nope"));
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary_current(table, "nope"));
  EXPECT_EQ(nullptr, symbol_table_lookup_tag(table, "nope"));
  EXPECT_EQ(nullptr, symbol_table_lookup_tag_current(table, "nope"));
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  EXPECT_EQ(nullptr, symbol_table_lookup_label(table, "nope"));
  symbol_table_leave_scope(table);
}

TEST_F(SymbolTableTest, InsertReturnsTheSymbolWithTheFieldsItWasGiven) {
  type_info *type = create_type_info(TYPE_PRIMITIVE);
  symbol *sym = symbol_table_insert_ordinary(table, "count", SYMBOL_TYPEDEF, type, at(3, 7));
  ASSERT_NE(sym, nullptr);
  EXPECT_STREQ(sym->name, "count");
  EXPECT_EQ(sym->kind, SYMBOL_TYPEDEF);
  EXPECT_EQ(sym->type, type);
  EXPECT_EQ(sym->loc.line, 3);
  EXPECT_EQ(sym->loc.column, 7);
  EXPECT_EQ(sym->linkage, LINKAGE_NONE);
  EXPECT_EQ(sym->is_tentative, 0);
  EXPECT_EQ(sym->is_defined, 0);
  EXPECT_EQ(symbol_table_lookup_ordinary(table, "count"), sym);
  free_type_info(type);
}

TEST_F(SymbolTableTest, TagAndLabelSymbolsAreFullyInitialised) {
  type_info *type = create_type_info(TYPE_PRIMITIVE);
  symbol *tag = symbol_table_insert_tag(table, "S", type, at(2, 8));
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  symbol *label = symbol_table_insert_label(table, "done", at(9, 1));
  ASSERT_NE(tag, nullptr);
  ASSERT_NE(label, nullptr);

  EXPECT_STREQ(tag->name, "S");
  EXPECT_EQ(tag->kind, SYMBOL_TAG);
  EXPECT_EQ(tag->type, type);
  EXPECT_EQ(tag->loc.line, 2);
  EXPECT_EQ(tag->loc.column, 8);
  EXPECT_EQ(tag->linkage, LINKAGE_NONE);
  EXPECT_EQ(tag->is_tentative, 0);
  EXPECT_EQ(tag->is_defined, 0);
  EXPECT_EQ(tag->tag_kind, TYPE_PRIMITIVE) << "zero until sema records the keyword";
  EXPECT_EQ(tag->definition, nullptr);
  EXPECT_EQ(symbol_table_lookup_tag(table, "S"), tag);

  EXPECT_STREQ(label->name, "done");
  EXPECT_EQ(label->kind, SYMBOL_LABEL);
  EXPECT_EQ(label->type, nullptr);
  EXPECT_EQ(label->loc.line, 9);
  EXPECT_EQ(label->loc.column, 1);
  EXPECT_EQ(label->linkage, LINKAGE_NONE);
  EXPECT_EQ(label->is_tentative, 0);
  EXPECT_EQ(label->is_defined, 0);
  EXPECT_EQ(symbol_table_lookup_label(table, "done"), label);

  symbol_table_leave_scope(table);
  free_type_info(type);
}

TEST(SymbolLifetimeTest, TheNameIsACopyOfTheCallersString) {
  char name[] = "x";
  std::map<void *, int> frees;
  frees[name] = 0;
  {
    TrackFrees track(frees);
    symbol_table *table = symbol_table_create();
    symbol_table_enter_scope(table, SCOPE_FILE);
    symbol *sym = symbol_table_insert_ordinary(table, name, SYMBOL_VAR, nullptr, at(1, 5));
    ASSERT_NE(sym, nullptr);
    EXPECT_NE(sym->name, name);
    name[0] = 'y';
    EXPECT_STREQ(sym->name, "x");
    EXPECT_EQ(symbol_table_lookup_ordinary(table, "x"), sym);
    symbol_table_destroy(table);
  }
  EXPECT_EQ(frees[name], 0) << "the table must never free the caller's string";
}

TEST_F(SymbolTableTest, InsertingANameAgainInTheSameScopeAddsASecondSymbol) {
  symbol *first = symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, at(1, 5));
  symbol *second = symbol_table_insert_ordinary(table, "x", SYMBOL_FUNC, nullptr, at(2, 6));
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first, second);
  EXPECT_EQ(symbol_table_lookup_ordinary(table, "x"), second);
  EXPECT_EQ(symbol_table_lookup_ordinary_current(table, "x"), second);
  EXPECT_EQ(first->kind, SYMBOL_VAR);
  EXPECT_EQ(first->loc.line, 1) << "the earlier declaration stays intact for a diagnostic to cite";
}

TEST_F(SymbolTableTest, ATagCanBeInsertedAgainInTheSameScope) {
  symbol *declared = symbol_table_insert_tag(table, "S", nullptr, at(1, 8));
  symbol *completed = symbol_table_insert_tag(table, "S", nullptr, at(2, 8));
  ASSERT_NE(declared, nullptr) << "struct S;";
  ASSERT_NE(completed, nullptr) << "struct S { int a; };";
  EXPECT_NE(declared, completed);
  EXPECT_EQ(symbol_table_lookup_tag(table, "S"), completed);
  EXPECT_EQ(symbol_table_lookup_tag_current(table, "S"), completed);
}

TEST_F(SymbolTableTest, ALabelCanBeInsertedAgainInTheSameFunction) {
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  symbol *first = symbol_table_insert_label(table, "L", at(2, 1));
  symbol *second = symbol_table_insert_label(table, "L", at(5, 1));
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first, second);
  EXPECT_EQ(symbol_table_lookup_label(table, "L"), second);
  symbol_table_leave_scope(table);
}

TEST_F(SymbolTableTest, LeavingAScopeHidesItsSymbols) {
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol_table_insert_ordinary(table, "inner", SYMBOL_VAR, nullptr, at(1, 1));
  symbol_table_insert_tag(table, "Inner", nullptr, at(1, 1));
  ASSERT_NE(nullptr, symbol_table_lookup_ordinary(table, "inner"));
  ASSERT_NE(nullptr, symbol_table_lookup_tag(table, "Inner"));
  symbol_table_leave_scope(table);
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary(table, "inner"));
  EXPECT_EQ(nullptr, symbol_table_lookup_tag(table, "Inner"));
}

TEST_F(SymbolTableTest, LookupWalksOutwardThroughSeveralScopes) {
  symbol *outer = symbol_table_insert_ordinary(table, "outer", SYMBOL_VAR, nullptr, at(1, 5));
  symbol *tag = symbol_table_insert_tag(table, "Outer", nullptr, at(2, 8));
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  EXPECT_EQ(symbol_table_lookup_ordinary(table, "outer"), outer);
  EXPECT_EQ(symbol_table_lookup_tag(table, "Outer"), tag);
  symbol_table_leave_scope(table);
  symbol_table_leave_scope(table);
  symbol_table_leave_scope(table);
}

TEST_F(SymbolTableTest, OrdinaryNamesTagsAndLabelsAreSeparateNamespaces) {
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  symbol *var = symbol_table_insert_ordinary(table, "n", SYMBOL_VAR, nullptr, at(1, 1));
  symbol *tag = symbol_table_insert_tag(table, "n", nullptr, at(2, 1));
  symbol *label = symbol_table_insert_label(table, "n", at(3, 1));
  ASSERT_NE(var, nullptr);
  ASSERT_NE(tag, nullptr);
  ASSERT_NE(label, nullptr);

  EXPECT_EQ(symbol_table_lookup_ordinary(table, "n"), var);
  EXPECT_EQ(symbol_table_lookup_ordinary_current(table, "n"), var);
  EXPECT_EQ(symbol_table_lookup_tag(table, "n"), tag);
  EXPECT_EQ(symbol_table_lookup_tag_current(table, "n"), tag);
  EXPECT_EQ(symbol_table_lookup_label(table, "n"), label);
  symbol_table_leave_scope(table);
}

TEST_F(SymbolTableTest, ALabelInsertedFromANestedBlockLivesInTheFunctionScope) {
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol *label = symbol_table_insert_label(table, "loop_start", at(4, 3));
  ASSERT_NE(label, nullptr);

  symbol_table_leave_scope(table);
  symbol_table_leave_scope(table);
  EXPECT_EQ(symbol_table_lookup_label(table, "loop_start"), label);

  symbol_table_leave_scope(table);
  EXPECT_EQ(nullptr, symbol_table_lookup_label(table, "loop_start"));
}

TEST_F(SymbolTableTest, LabelsAreVisibleFromNestedBlocksOfTheSameFunction) {
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  symbol *label = symbol_table_insert_label(table, "L", at(1, 1));
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  EXPECT_EQ(symbol_table_lookup_label(table, "L"), label)
      << "labels live in the function scope, not the enclosing block";
  symbol_table_leave_scope(table);
  symbol_table_leave_scope(table);
  symbol_table_leave_scope(table);
}

TEST_F(SymbolTableTest, LabelsDoNotLeakBetweenFunctions) {
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  symbol_table_insert_label(table, "L", at(1, 1));
  symbol_table_leave_scope(table);

  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  EXPECT_EQ(nullptr, symbol_table_lookup_label(table, "L"));
  symbol_table_leave_scope(table);
}

TEST_F(SymbolTableTest, ALabelOutsideAnyFunctionIsRejected) {
  EXPECT_EQ(nullptr, symbol_table_insert_label(table, "L", at(1, 1)));
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  EXPECT_EQ(nullptr, symbol_table_insert_label(table, "L", at(1, 1)));
  symbol_table_leave_scope(table);
  EXPECT_EQ(table->all_symbols, nullptr) << "a rejected label must not be created";
}

TEST_F(SymbolTableTest, CurrentLookupSearchesOnlyTheInnermostScope) {
  symbol *outer = symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, at(1, 5));
  symbol *tag = symbol_table_insert_tag(table, "S", nullptr, at(2, 8));
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  EXPECT_EQ(symbol_table_lookup_ordinary(table, "x"), outer);
  EXPECT_EQ(symbol_table_lookup_ordinary_current(table, "x"), nullptr);
  EXPECT_EQ(symbol_table_lookup_tag(table, "S"), tag);
  EXPECT_EQ(symbol_table_lookup_tag_current(table, "S"), nullptr);
  symbol_table_leave_scope(table);
  EXPECT_EQ(symbol_table_lookup_ordinary_current(table, "x"), outer);
  EXPECT_EQ(symbol_table_lookup_tag_current(table, "S"), tag);
}

TEST_F(SymbolTableTest, ShadowingHidesTheOuterSymbolUntilTheInnerScopeCloses) {
  symbol *outer = symbol_table_insert_ordinary(table, "x", SYMBOL_FUNC, nullptr, at(1, 6));
  symbol *outer_tag = symbol_table_insert_tag(table, "S", nullptr, at(2, 8));
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol *inner = symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, at(4, 7));
  symbol *inner_tag = symbol_table_insert_tag(table, "S", nullptr, at(5, 10));

  EXPECT_EQ(symbol_table_lookup_ordinary(table, "x"), inner);
  EXPECT_EQ(symbol_table_lookup_ordinary_current(table, "x"), inner);
  EXPECT_EQ(symbol_table_lookup_tag(table, "S"), inner_tag);
  EXPECT_EQ(symbol_table_lookup_tag_current(table, "S"), inner_tag);

  symbol_table_leave_scope(table);
  EXPECT_EQ(symbol_table_lookup_ordinary(table, "x"), outer);
  EXPECT_EQ(symbol_table_lookup_ordinary_current(table, "x"), outer);
  EXPECT_EQ(symbol_table_lookup_tag(table, "S"), outer_tag);
  EXPECT_EQ(symbol_table_lookup_tag_current(table, "S"), outer_tag);
}

TEST_F(SymbolTableTest, LookupMatchesWholeNamesOnly) {
  symbol_table_insert_ordinary(table, "xy", SYMBOL_VAR, nullptr, at(1, 1));
  symbol_table_insert_tag(table, "Sx", nullptr, at(1, 1));
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  symbol_table_insert_label(table, "Lx", at(1, 1));

  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary(table, "x"));
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary(table, "xyz"));
  EXPECT_EQ(nullptr, symbol_table_lookup_tag(table, "S"));
  EXPECT_EQ(nullptr, symbol_table_lookup_label(table, "L"));
  symbol_table_leave_scope(table);
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary_current(table, "x"));
  EXPECT_EQ(nullptr, symbol_table_lookup_tag_current(table, "S"));
}

TEST_F(SymbolTableTest, ManyScopesAndSymbolsDoNotCrash) {
  for (int depth = 0; depth < 50; depth++) {
    symbol_table_enter_scope(table, SCOPE_BLOCK);
    for (int i = 0; i < 20; i++) {
      std::string name = "v" + std::to_string(depth) + "_" + std::to_string(i);
      EXPECT_NE(nullptr, symbol_table_insert_ordinary(table, name.c_str(), SYMBOL_VAR, nullptr,
                                                      at(depth + 1, i + 1)));
    }
  }
  EXPECT_NE(nullptr, symbol_table_lookup_ordinary(table, "v0_0"));
  for (int depth = 0; depth < 50; depth++) {
    symbol_table_leave_scope(table);
  }
  EXPECT_EQ(nullptr, symbol_table_lookup_ordinary(table, "v0_0"));
}

TEST(SymbolLifetimeTest, AnAnonymousTagIsOwnedByTheTableButInNoScope) {
  symbol_table *table = symbol_table_create();
  symbol_table_enter_scope(table, SCOPE_FILE);
  symbol *named = symbol_table_insert_tag(table, "S", nullptr, at(1, 8));
  symbol *anonymous = symbol_table_insert_tag(table, nullptr, nullptr, at(2, 1));
  ASSERT_NE(named, nullptr);
  ASSERT_NE(anonymous, nullptr);
  EXPECT_EQ(anonymous->name, nullptr);
  EXPECT_EQ(anonymous->kind, SYMBOL_TAG);
  EXPECT_EQ(anonymous->loc.line, 2);
  EXPECT_EQ(anonymous->loc.column, 1);
  ASSERT_EQ(table->current_scope->tag_symbols, named) << "an anonymous tag is on no scope list";
  EXPECT_EQ(named->next, nullptr);
  EXPECT_EQ(symbol_table_lookup_tag(table, "S"), named);
  EXPECT_EQ(symbol_table_lookup_tag_current(table, "S"), named);
  EXPECT_EQ(table->all_symbols, anonymous) << "but the table owns it";

  std::map<void *, int> frees;
  frees[anonymous] = 0;
  {
    TrackFrees track(frees);
    symbol_table_destroy(table);
  }
  EXPECT_EQ(frees[anonymous], 1);
}

TEST(SymbolLifetimeTest, LeavingAScopeFreesTheScopeAndNothingElse) {
  symbol_table *table = symbol_table_create();
  symbol_table_enter_scope(table, SCOPE_FILE);
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  symbol *var = symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, at(1, 1));
  symbol *tag = symbol_table_insert_tag(table, "S", nullptr, at(2, 1));
  symbol *label = symbol_table_insert_label(table, "L", at(3, 1));
  ASSERT_NE(var, nullptr);
  ASSERT_NE(tag, nullptr);
  ASSERT_NE(label, nullptr);
  std::map<void *, int> frees;
  track_symbol(frees, var);
  track_symbol(frees, tag);
  track_symbol(frees, label);

  {
    TrackFrees track(frees);
    st_free_calls = 0;
    symbol_table_leave_scope(table);
    int leave_frees = st_free_calls;
    EXPECT_EQ(leave_frees, 1) << "only the scope itself; its symbols belong to the table";
    symbol_table_destroy(table);
  }

  EXPECT_EQ(not_freed_exactly_once(frees), 0);
}

TEST(SymbolLifetimeTest, ASymbolOutlivesTheScopeItWasDeclaredIn) {
  type_info *type = create_type_info(TYPE_PRIMITIVE);
  symbol_table *table = symbol_table_create();
  symbol_table_enter_scope(table, SCOPE_FILE);
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol *sym = symbol_table_insert_ordinary(table, "local", SYMBOL_TYPEDEF, type, at(5, 9));
  ASSERT_NE(sym, nullptr);
  std::map<void *, int> frees;
  track_symbol(frees, sym);
  char *name = sym->name;

  {
    TrackFrees track(frees);
    symbol_table_leave_scope(table);
    EXPECT_EQ(frees[sym], 0);
    EXPECT_EQ(frees[name], 0);
    EXPECT_EQ(sym->name, name);
    EXPECT_STREQ(sym->name, "local");
    EXPECT_EQ(sym->kind, SYMBOL_TYPEDEF);
    EXPECT_EQ(sym->type, type);
    EXPECT_EQ(sym->loc.line, 5);
    EXPECT_EQ(sym->loc.column, 9);
    EXPECT_EQ(nullptr, symbol_table_lookup_ordinary(table, "local"))
        << "out of scope means no longer found, not freed";
    symbol_table_destroy(table);
  }

  EXPECT_EQ(not_freed_exactly_once(frees), 0);
  free_type_info(type);
}

TEST(SymbolLifetimeTest, DestroyFreesEverySymbolExactlyOnceWhereverItWasDeclared) {
  symbol_table *table = symbol_table_create();
  symbol_table_enter_scope(table, SCOPE_FILE);
  symbol *file_var = symbol_table_insert_ordinary(table, "x", SYMBOL_VAR, nullptr, at(1, 5));
  symbol *file_tag = symbol_table_insert_tag(table, "S", nullptr, at(2, 8));
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol *closed = symbol_table_insert_ordinary(table, "gone", SYMBOL_VAR, nullptr, at(3, 5));
  symbol_table_leave_scope(table);
  symbol_table_enter_scope(table, SCOPE_FUNCTION);
  symbol *label = symbol_table_insert_label(table, "L", at(4, 1));
  symbol_table_enter_scope(table, SCOPE_BLOCK);
  symbol *open = symbol_table_insert_ordinary(table, "inner", SYMBOL_VAR, nullptr, at(5, 7));
  ASSERT_NE(file_var, nullptr);
  ASSERT_NE(file_tag, nullptr);
  ASSERT_NE(closed, nullptr);
  ASSERT_NE(label, nullptr);
  ASSERT_NE(open, nullptr);
  std::map<void *, int> frees;
  track_symbol(frees, file_var);
  track_symbol(frees, file_tag);
  track_symbol(frees, closed);
  track_symbol(frees, label);
  track_symbol(frees, open);

  {
    TrackFrees track(frees);
    st_free_calls = 0;
    symbol_table_destroy(table);
    int destroy_frees = st_free_calls;
    EXPECT_EQ(destroy_frees, 5 + 5 + 3 + 1)
        << "five names, five symbols, three open scopes, and the table";
  }

  EXPECT_EQ(not_freed_exactly_once(frees), 0);
}
