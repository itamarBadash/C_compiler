#include "target_guard.h"
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
};

static FrontEndResult run_front_end(const char *probe, target_kind kind) {
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
