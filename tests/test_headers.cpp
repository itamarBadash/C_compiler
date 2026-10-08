#include "conversion_walk.h"
#include "target_guard.h"
#include <functional>
#include <gtest/gtest.h>
#include <string>
#include <vector>

extern "C" {
#include "parser.h"
#include "preprocessor.h"
#include "sema.h"
}

struct FrontEndResult {
  int errors = -1;
  std::string diagnostics;
  ConversionWalk conversions;
};

static FrontEndResult
run_front_end(const char *probe, target_kind kind,
              const std::function<void(ast_node *, symbol_table *)> &inspect = nullptr) {
  TargetGuard guard(kind);
  std::string probe_path = std::string(HEADER_PROBES_DIR) + "/" + probe;
  std::vector<std::string> dirs = {LIB_INCLUDE_DIR};
  if (kind == TARGET_WINDOWS_X64) {
    dirs.push_back(std::string(SYSROOT_DIR) + "/windows/include");
  } else {
    dirs.push_back(std::string(SYSROOT_DIR) + "/linux/include/x86_64-linux-gnu");
    dirs.push_back(std::string(SYSROOT_DIR) + "/linux/include");
  }
  std::vector<const char *> system_dirs;
  for (const std::string &dir : dirs)
    system_dirs.push_back(dir.c_str());

  FrontEndResult result;
  testing::internal::CaptureStderr();
  token_buf tb;
  int pp_errors =
      pp_run_file(&tb, probe_path.c_str(), nullptr, 0, system_dirs.data(), (int)system_dirs.size());
  parser p;
  parser_init_from_buf(&p, &tb);
  ast_node *program = parse_program(&p);
  int parse_errors = p.had_error;
  parser_destroy(&p);
  token_buf_free(&tb);
  symbol_table *table = symbol_table_create();
  int sema_errors = sema_check(table, program);
  result.conversions = walk_conversions(program);
  if (inspect)
    inspect(program, table);
  symbol_table_destroy(table);
  free_ast(program);
  result.diagnostics = testing::internal::GetCapturedStderr();
  result.errors = pp_errors + parse_errors + sema_errors;
  return result;
}

static void expect_clean(const char *probe, target_kind kind) {
  SCOPED_TRACE(std::string(probe) + (kind == TARGET_LINUX_X64 ? " on linux" : " on windows"));
  FrontEndResult result = run_front_end(probe, kind);
  EXPECT_EQ(result.errors, 0) << result.diagnostics;
  EXPECT_EQ(result.diagnostics, "");
}

TEST(HeaderTests, EveryC99HeaderAndMacroPassesOnWindows) {
  for (const char *probe : {"all_headers.c", "macros_1.c", "macros_2.c"})
    expect_clean(probe, TARGET_WINDOWS_X64);
}

TEST(HeaderTests, EveryC99HeaderAndMacroPassesOnLinux) {
  for (const char *probe : {"all_headers.c", "macros_1.c", "macros_2.c"})
    expect_clean(probe, TARGET_LINUX_X64);
}

TEST(HeaderTests, StddefHandsOutOnlyWhatWasAskedFor) {
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64})
    expect_clean("need_protocol.c", kind);
}

TEST(HeaderTests, FreestandingValuesAndTypesMatchGccOnWindows) {
  expect_clean("values_windows.c", TARGET_WINDOWS_X64);
}

TEST(HeaderTests, FreestandingValuesAndTypesMatchGccOnLinux) {
  expect_clean("values_linux.c", TARGET_LINUX_X64);
}

TEST(HeaderTests, TypeGenericMathPicksGccsFunctionOnWindows) {
  expect_clean("tgmath_windows.c", TARGET_WINDOWS_X64);
}

TEST(HeaderTests, TypeGenericMathPicksGccsFunctionOnLinux) {
  expect_clean("tgmath_linux.c", TARGET_LINUX_X64);
}

TEST(HeaderTests, EveryOperatorInTheHeadersHasConvertedOperands) {
  for (target_kind kind : {TARGET_WINDOWS_X64, TARGET_LINUX_X64}) {
    SCOPED_TRACE(kind == TARGET_LINUX_X64 ? "linux" : "windows");
    int kinds[4] = {0, 0, 0, 0};
    for (const char *probe : {"all_headers.c", "macros_1.c", "macros_2.c"}) {
      SCOPED_TRACE(probe);
      FrontEndResult result = run_front_end(probe, kind);
      ASSERT_EQ(result.errors, 0) << result.diagnostics;
      EXPECT_EQ(result.conversions.problems, std::vector<std::string>{});
      for (int i = 0; i < 4; i++)
        kinds[i] += result.conversions.kinds[i];
    }
    EXPECT_GT(kinds[CONVERSION_LVALUE], 0);
    EXPECT_GT(kinds[CONVERSION_ARRAY_TO_POINTER], 0);
    EXPECT_GT(kinds[CONVERSION_FUNCTION_TO_POINTER], 0);
    EXPECT_GT(kinds[CONVERSION_VALUE], 0);
  }
}

static symbol *named(symbol_table *table, const char *name) {
  for (symbol *sym = table->all_symbols; sym != nullptr; sym = sym->all_next) {
    if (sym->name != nullptr && std::string(sym->name) == name &&
        (sym->kind == SYMBOL_FUNC || sym->kind == SYMBOL_VAR))
      return sym;
  }
  return nullptr;
}

TEST(HeaderTests, TheHeadersRenamesAndMarksAreRecorded) {
  std::string facts;
  run_front_end("all_headers.c", TARGET_LINUX_X64, [&](ast_node *, symbol_table *table) {
    for (const char *name : {"fscanf", "scanf", "sscanf", "printf", "setjmp", "_setjmp"}) {
      symbol *sym = named(table, name);
      facts += std::string(name) + (sym == nullptr ? " missing" : "") +
               " label=" + (sym && sym->asm_label ? sym->asm_label : "-") +
               " twice=" + std::to_string(sym ? sym->returns_twice : -1) + "\n";
    }
  });
  EXPECT_EQ(facts, "fscanf label=__isoc99_fscanf twice=0\n"
                   "scanf label=__isoc99_scanf twice=0\n"
                   "sscanf label=__isoc99_sscanf twice=0\n"
                   "printf label=- twice=0\n"
                   "setjmp label=- twice=1\n"
                   "_setjmp label=- twice=1\n");

  std::string windows;
  run_front_end("all_headers.c", TARGET_WINDOWS_X64, [&](ast_node *program, symbol_table *table) {
    for (const char *name : {"_sys_nerr", "_setjmp", "strtod"}) {
      symbol *sym = named(table, name);
      windows += std::string(name) + (sym == nullptr ? " missing" : "") +
                 " dllimport=" + std::to_string(sym ? sym->dllimport : -1) +
                 " twice=" + std::to_string(sym ? sym->returns_twice : -1) + "\n";
    }
    for (int i = 0; i < program->program.count; i++) {
      ast_node *item = program->program.declarations[i];
      if (item->type == AST_NODE_TYPE_FUNCTION_DEF &&
          std::string(item->function_def.name) == "__debugbreak")
        windows += "__debugbreak inline_definition=" +
                   std::to_string(item->function_def.is_inline_definition) + "\n";
    }
  });
  EXPECT_EQ(windows, "_sys_nerr dllimport=1 twice=0\n"
                     "_setjmp dllimport=0 twice=1\n"
                     "strtod dllimport=0 twice=0\n"
                     "__debugbreak inline_definition=1\n");
}
