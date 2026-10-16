#include "target_guard.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "codegen.h"
#include "parser.h"
#include "preprocessor.h"
#include "sema.h"
}

static std::string generate(const char *source, target_kind kind = TARGET_WINDOWS_X64,
                            bool system_headers = false, const char *path = nullptr) {
  TargetGuard guard(kind);
  std::vector<std::string> dirs = {LIB_INCLUDE_DIR};
  if (kind == TARGET_WINDOWS_X64) {
    dirs.push_back(std::string(SYSROOT_DIR) + "/windows/include");
  } else {
    dirs.push_back(std::string(SYSROOT_DIR) + "/linux/include/x86_64-linux-gnu");
    dirs.push_back(std::string(SYSROOT_DIR) + "/linux/include");
  }
  dirs.push_back(std::string(CODEGEN_PROGRAMS_DIR) + "/system");
  std::vector<const char *> system_dirs;
  for (const std::string &dir : dirs)
    system_dirs.push_back(dir.c_str());
  testing::internal::CaptureStderr();
  token_buf tb;
  int errors = path
                   ? pp_run_file(&tb, path, nullptr, 0, system_dirs.data(), (int)system_dirs.size())
               : system_headers ? pp_run_ex(&tb, source, "program.c", nullptr, 0,
                                            system_dirs.data(), (int)system_dirs.size())
                                : pp_run(&tb, source);
  parser p;
  parser_init_from_buf(&p, &tb);
  ast_node *program = parse_program(&p);
  errors += p.had_error;
  parser_destroy(&p);
  token_buf_free(&tb);
  symbol_table *table = symbol_table_create();
  errors += sema_check(table, program);
  std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(errors, 0) << diagnostics;
  char *ir = errors == 0 ? codegen_program(program) : nullptr;
  std::string out = ir ? ir : "<null>";
  free(ir);
  symbol_table_destroy(table);
  free_ast(program);
  return out;
}

static std::string function(const std::string &ir, const std::string &name) {
  size_t at = 0;
  while ((at = ir.find("define ", at)) != std::string::npos) {
    size_t line_end = ir.find('\n', at);
    if (ir.substr(at, line_end - at).find(" @" + name + "(") != std::string::npos)
      return ir.substr(at, ir.find("\n}\n", at) + 3 - at);
    at = line_end;
  }
  return "";
}

static int count(const std::string &text, const std::string &piece) {
  int n = 0;
  for (size_t at = text.find(piece); at != std::string::npos; at = text.find(piece, at + 1))
    n++;
  return n;
}

static void expect_lines(const std::string &text, const std::vector<std::string> &lines) {
  size_t at = 0;
  for (const std::string &line : lines) {
    std::string wanted = line.back() == ':' ? "\n" + line + "\n" : "  " + line + "\n";
    size_t found = text.find(wanted, at);
    ASSERT_NE(found, std::string::npos) << "missing, in order: " << line << "\nin:\n" << text;
    at = found + line.size();
  }
}

TEST(CodegenModuleTest, HeaderNamesEachTarget) {
  const char *source = "int main(void) { return 0; }";
  std::string windows_ir = generate(source);
  EXPECT_EQ(windows_ir.substr(0, windows_ir.find("define")),
            "target datalayout = \"e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-"
            "f80:128-n8:16:32:64-S128\"\n"
            "target triple = \"x86_64-w64-windows-gnu\"\n\n");
  std::string linux_ir = generate(source, TARGET_LINUX_X64);
  EXPECT_EQ(linux_ir.substr(0, linux_ir.find("define")),
            "target datalayout = \"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-"
            "f80:128-n8:16:32:64-S128\"\n"
            "target triple = \"x86_64-pc-linux-gnu\"\n\n");
}

TEST(CodegenModuleTest, ParametersGetSlotsAndTemporariesStartAfterThem) {
  EXPECT_EQ(function(generate("int add(int a, int b) { return a + b; }"), "add"),
            "define i32 @add(i32 %0, i32 %1) {\n"
            "entry:\n"
            "  %a.0 = alloca i32, align 4\n"
            "  %b.1 = alloca i32, align 4\n"
            "  store i32 %0, ptr %a.0, align 4\n"
            "  store i32 %1, ptr %b.1, align 4\n"
            "  %2 = load i32, ptr %a.0, align 4\n"
            "  %3 = load i32, ptr %b.1, align 4\n"
            "  %4 = add i32 %2, %3\n"
            "  ret i32 %4\n"
            "}\n");
}

TEST(CodegenModuleTest, FunctionsEndAsTheirTypeRequires) {
  std::string ir = generate("int main(void) { }\n"
                            "void nothing(void) { }\n"
                            "int missing(void) { }\n"
                            "static int hidden(void) { return 1; }\n"
                            "int uses(void) { return hidden(); }\n"
                            "static int main2(void) { }\n");
  expect_lines(function(ir, "main"), {"ret i32 0"});
  expect_lines(function(ir, "nothing"), {"ret void"});
  expect_lines(function(ir, "missing"), {"ret i32 poison"});
  std::string hidden = function(ir, "hidden");
  EXPECT_EQ(hidden.substr(0, hidden.find('\n')), "define internal i32 @hidden() {");
  std::string main_body = function(ir, "main");
  EXPECT_EQ(main_body.substr(0, main_body.find('\n')), "define i32 @main() {");
}

TEST(CodegenModuleTest, StaticMainIsAnOrdinaryFunction) {
  std::string ir = generate("static int main(void) { }\nint use(void) { return main(); }");
  expect_lines(function(ir, "main"), {"ret i32 poison"});
}

TEST(CodegenModuleTest, OldStyleParametersArrivePromoted) {
  std::string ir = generate("int k(c, f, s, d) char c; float f; short s; double d;"
                            " { return c + s; }");
  std::string body = function(ir, "k");
  EXPECT_EQ(body.substr(0, body.find('\n')),
            "define i32 @k(i32 %0, double %1, i32 %2, double %3) {");
  expect_lines(body, {"%c.0 = alloca i8, align 1", "%f.1 = alloca float, align 4",
                      "%s.2 = alloca i16, align 2", "%d.3 = alloca double, align 8",
                      "%4 = trunc i32 %0 to i8", "store i8 %4, ptr %c.0, align 1",
                      "%5 = fptrunc double %1 to float", "store float %5, ptr %f.1, align 4",
                      "%6 = trunc i32 %2 to i16", "store i16 %6, ptr %s.2, align 2",
                      "store double %3, ptr %d.3, align 8"});
}

TEST(CodegenModuleTest, PrototypedParametersArriveAsDeclared) {
  std::string body = function(generate("int k(char c, float f) { return c; }"), "k");
  EXPECT_EQ(body.substr(0, body.find('\n')), "define i32 @k(i8 %0, float %1) {");
  expect_lines(body, {"store i8 %0, ptr %c.0, align 1", "store float %1, ptr %f.1, align 4"});
}

TEST(CodegenModuleTest, VariadicDefinitionKeepsItsEllipsis) {
  std::string ir = generate("int count(int n, ...) { return n; }");
  std::string body = function(ir, "count");
  EXPECT_EQ(body.substr(0, body.find('\n')), "define i32 @count(i32 %0, ...) {");
}

TEST(CodegenSignednessTest, DivisionRemainderAndShiftFollowTheType) {
  std::string ir = generate("int sd(int a, int b) { return a / b; }\n"
                            "unsigned ud(unsigned a, unsigned b) { return a / b; }\n"
                            "int sr(int a, int b) { return a % b; }\n"
                            "unsigned ur(unsigned a, unsigned b) { return a % b; }\n"
                            "int as(int a, int b) { return a >> b; }\n"
                            "unsigned ls(unsigned a, int b) { return a >> b; }\n");
  expect_lines(function(ir, "sd"), {"%4 = sdiv i32 %2, %3"});
  expect_lines(function(ir, "ud"), {"%4 = udiv i32 %2, %3"});
  expect_lines(function(ir, "sr"), {"%4 = srem i32 %2, %3"});
  expect_lines(function(ir, "ur"), {"%4 = urem i32 %2, %3"});
  expect_lines(function(ir, "as"), {"%4 = ashr i32 %2, %3"});
  expect_lines(function(ir, "ls"), {"%4 = lshr i32 %2, %3"});
}

TEST(CodegenSignednessTest, ShiftAmountTakesTheLeftOperandsWidth) {
  std::string ir = generate("long long wide(long long a, int b) { return a << b; }\n"
                            "int narrow(int a, long long b) { return a << b; }\n");
  expect_lines(function(ir, "wide"), {"%4 = zext i32 %3 to i64", "%5 = shl i64 %2, %4"});
  expect_lines(function(ir, "narrow"), {"%4 = trunc i64 %3 to i32", "%5 = shl i32 %2, %4"});
}

TEST(CodegenSignednessTest, ComparisonsFollowTheOperandType) {
  const char *all = "(a < b) + (a > b) + (a <= b) + (a >= b) + (a == b) + (a != b)";
  std::string ir = generate(("int s(int a, int b) { return " + std::string(all) +
                             "; }\nint u(unsigned a, unsigned b) { return " + all +
                             "; }\nint f(double a, double b) { return " + all +
                             "; }\nint m(int a, unsigned b) { return a < b; }\n"
                             "int p(int *a, int *b) { return a < b; }\n")
                                .c_str());
  expect_lines(function(ir, "s"), {"%4 = icmp slt i32 %2, %3", "%8 = icmp sgt i32 %6, %7",
                                   "%13 = icmp sle i32 %11, %12", "%18 = icmp sge i32 %16, %17",
                                   "%23 = icmp eq i32 %21, %22", "%28 = icmp ne i32 %26, %27"});
  expect_lines(function(ir, "u"), {"%4 = icmp ult i32 %2, %3", "%8 = icmp ugt i32 %6, %7",
                                   "%13 = icmp ule i32 %11, %12", "%18 = icmp uge i32 %16, %17",
                                   "%23 = icmp eq i32 %21, %22", "%28 = icmp ne i32 %26, %27"});
  expect_lines(function(ir, "f"),
               {"%4 = fcmp olt double %2, %3", "%8 = fcmp ogt double %6, %7",
                "%13 = fcmp ole double %11, %12", "%18 = fcmp oge double %16, %17",
                "%23 = fcmp oeq double %21, %22", "%28 = fcmp une double %26, %27"});
  expect_lines(function(ir, "m"), {"%4 = icmp ult i32 %2, %3", "%5 = zext i1 %4 to i32"});
  expect_lines(function(ir, "p"), {"%4 = icmp ult ptr %2, %3"});
}

TEST(CodegenConversionTest, WideningFollowsTheSourceType) {
  std::string ir = generate("int a(short x) { return x; }\n"
                            "int b(unsigned short x) { return x; }\n"
                            "long long c(char x) { return x; }\n"
                            "long long d(unsigned char x) { return x; }\n"
                            "int e(_Bool x) { return x; }\n"
                            "short f(int x) { return x; }\n"
                            "unsigned g(int x) { return x; }\n"
                            "int h(unsigned x) { return x; }\n");
  expect_lines(function(ir, "a"), {"%2 = sext i16 %1 to i32", "ret i32 %2"});
  expect_lines(function(ir, "b"), {"%2 = zext i16 %1 to i32", "ret i32 %2"});
  expect_lines(function(ir, "c"), {"%2 = sext i8 %1 to i64", "ret i64 %2"});
  expect_lines(function(ir, "d"), {"%2 = zext i8 %1 to i64", "ret i64 %2"});
  expect_lines(function(ir, "e"), {"%2 = zext i8 %1 to i32", "ret i32 %2"});
  expect_lines(function(ir, "f"), {"%2 = trunc i32 %1 to i16", "ret i16 %2"});
  expect_lines(function(ir, "g"), {"%1 = load i32, ptr %x.0, align 4", "ret i32 %1"});
  expect_lines(function(ir, "h"), {"%1 = load i32, ptr %x.0, align 4", "ret i32 %1"});
}

TEST(CodegenConversionTest, LongOnWindowsAndLinuxKeepsTheTargetWidth) {
  const char *source = "long f(int x) { return x; }";
  expect_lines(function(generate(source), "f"), {"%1 = load i32, ptr %x.0, align 4", "ret i32 %1"});
  expect_lines(function(generate(source, TARGET_LINUX_X64), "f"),
               {"%2 = sext i32 %1 to i64", "ret i64 %2"});
}

TEST(CodegenConversionTest, FloatingConversionsFollowBothTypes) {
  std::string ir = generate("double a(int x) { return x; }\n"
                            "double b(unsigned x) { return x; }\n"
                            "int c(double x) { return x; }\n"
                            "unsigned d(double x) { return x; }\n"
                            "double e(float x) { return x; }\n"
                            "float f(double x) { return x; }\n"
                            "long double g(double x) { return x; }\n"
                            "double h(long double x) { return x; }\n"
                            "float i(_Bool x) { return x; }\n");
  expect_lines(function(ir, "a"), {"%2 = sitofp i32 %1 to double"});
  expect_lines(function(ir, "b"), {"%2 = uitofp i32 %1 to double"});
  expect_lines(function(ir, "c"), {"%2 = fptosi double %1 to i32"});
  expect_lines(function(ir, "d"), {"%2 = fptoui double %1 to i32"});
  expect_lines(function(ir, "e"), {"%2 = fpext float %1 to double"});
  expect_lines(function(ir, "f"), {"%2 = fptrunc double %1 to float"});
  expect_lines(function(ir, "g"),
               {"%3 = fpext double %2 to x86_fp80", "store x86_fp80 %3, ptr %0, align 16"});
  expect_lines(function(ir, "h"),
               {"%1 = load x86_fp80, ptr %0, align 16", "%3 = fptrunc x86_fp80 %2 to double"});
  expect_lines(function(ir, "i"), {"%2 = uitofp i8 %1 to float"});
}

TEST(CodegenConversionTest, ConversionToBoolCompares) {
  std::string ir = generate("_Bool a(int x) { return x; }\n"
                            "_Bool b(double x) { return x; }\n"
                            "_Bool c(int *x) { return x; }\n"
                            "_Bool d(_Bool x) { return x; }\n");
  expect_lines(function(ir, "a"), {"%2 = icmp ne i32 %1, 0", "%3 = zext i1 %2 to i8", "ret i8 %3"});
  expect_lines(function(ir, "b"), {"%2 = fcmp une double %1, 0x0000000000000000",
                                   "%3 = zext i1 %2 to i8", "ret i8 %3"});
  expect_lines(function(ir, "c"),
               {"%2 = icmp ne ptr %1, null", "%3 = zext i1 %2 to i8", "ret i8 %3"});
  expect_lines(function(ir, "d"), {"%1 = load i8, ptr %x.0, align 1", "ret i8 %1"});
  EXPECT_EQ(count(ir, "trunc"), 0);
}

TEST(CodegenConversionTest, IntegersBecomePointersAsGccDoes) {
  std::string ir = generate("char *a(int x) { return (char *)x; }\n"
                            "char *b(unsigned x) { return (char *)x; }\n"
                            "char *c(long long x) { return (char *)x; }\n"
                            "int d(char *p) { return p == 0; }\n"
                            "long long e(char *p) { return (long long)p; }\n"
                            "short f(char *p) { return (short)p; }\n");
  expect_lines(function(ir, "a"), {"%2 = sext i32 %1 to i64", "%3 = inttoptr i64 %2 to ptr"});
  expect_lines(function(ir, "b"), {"%2 = zext i32 %1 to i64", "%3 = inttoptr i64 %2 to ptr"});
  expect_lines(function(ir, "c"), {"%2 = inttoptr i64 %1 to ptr", "ret ptr %2"});
  expect_lines(function(ir, "d"), {"%2 = icmp eq ptr %1, null"});
  expect_lines(function(ir, "e"), {"%2 = ptrtoint ptr %1 to i64"});
  expect_lines(function(ir, "f"), {"%2 = ptrtoint ptr %1 to i16"});
}

TEST(CodegenPointerTest, ArithmeticStepsByElement) {
  std::string ir = generate("int *a(int *p, int n) { return p + n; }\n"
                            "int *b(int *p, int n) { return n + p; }\n"
                            "int *c(int *p, int n) { return p - n; }\n"
                            "int d(int *p, int n) { return p[n]; }\n"
                            "int e(int *p, int n) { return n[p]; }\n"
                            "double f(double (*m)[3], int i) { return m[i][2]; }\n");
  expect_lines(function(ir, "a"),
               {"%4 = sext i32 %3 to i64", "%5 = getelementptr inbounds i32, ptr %2, i64 %4"});
  expect_lines(function(ir, "b"),
               {"%3 = sext i32 %2 to i64", "%5 = getelementptr inbounds i32, ptr %4, i64 %3"});
  expect_lines(function(ir, "c"), {"%4 = sext i32 %3 to i64", "%5 = sub i64 0, %4",
                                   "%6 = getelementptr inbounds i32, ptr %2, i64 %5"});
  expect_lines(function(ir, "d"), {"%5 = getelementptr inbounds i32, ptr %2, i64 %4",
                                   "%6 = load i32, ptr %5, align 4"});
  expect_lines(function(ir, "e"),
               {"%2 = load ptr, ptr %p.0, align 8", "%3 = load i32, ptr %n.1, align 4",
                "%5 = getelementptr inbounds i32, ptr %2, i64 %4",
                "%6 = load i32, ptr %5, align 4"});
  expect_lines(function(ir, "f"),
               {"%5 = getelementptr inbounds [3 x double], ptr %2, i64 %4",
                "%6 = sext i32 2 to i64", "%7 = getelementptr inbounds double, ptr %5, i64 %6",
                "%8 = load double, ptr %7, align 8"});
}

TEST(CodegenPointerTest, DifferenceDividesByElementSize) {
  std::string ir = generate("long long a(int *p, int *q) { return p - q; }\n"
                            "long long b(char *p, char *q) { return p - q; }\n");
  expect_lines(function(ir, "a"),
               {"%4 = ptrtoint ptr %2 to i64", "%5 = ptrtoint ptr %3 to i64", "%6 = sub i64 %4, %5",
                "%7 = sdiv exact i64 %6, 4", "ret i64 %7"});
  expect_lines(function(ir, "b"), {"%6 = sub i64 %4, %5", "ret i64 %6"});
  EXPECT_EQ(count(function(ir, "b"), "sdiv"), 0);
}

TEST(CodegenPointerTest, IncrementAndCompoundAssignmentStepPointers) {
  std::string ir = generate("int *a(int *p) { return ++p; }\n"
                            "int *b(int *p) { return p--; }\n"
                            "int *c(int *p, int n) { return p -= n; }\n");
  expect_lines(function(ir, "a"), {"%2 = getelementptr inbounds i32, ptr %1, i64 1",
                                   "store ptr %2, ptr %p.0, align 8", "ret ptr %2"});
  expect_lines(function(ir, "b"), {"%2 = getelementptr inbounds i32, ptr %1, i64 -1",
                                   "store ptr %2, ptr %p.0, align 8", "ret ptr %1"});
  expect_lines(function(ir, "c"),
               {"%2 = load ptr, ptr %p.0, align 8", "%4 = sext i32 %3 to i64", "%5 = sub i64 0, %4",
                "%6 = getelementptr inbounds i32, ptr %2, i64 %5",
                "store ptr %6, ptr %p.0, align 8", "ret ptr %6"});
}

TEST(CodegenExpressionTest, LogicalOperatorsJoinTheBlockTheJumpLeftFrom) {
  EXPECT_EQ(function(generate("int f(int a, int b, int c) { return a && (b || c); }"), "f"),
            "define i32 @f(i32 %0, i32 %1, i32 %2) {\n"
            "entry:\n"
            "  %a.0 = alloca i32, align 4\n"
            "  %b.1 = alloca i32, align 4\n"
            "  %c.2 = alloca i32, align 4\n"
            "  store i32 %0, ptr %a.0, align 4\n"
            "  store i32 %1, ptr %b.1, align 4\n"
            "  store i32 %2, ptr %c.2, align 4\n"
            "  %3 = load i32, ptr %a.0, align 4\n"
            "  %4 = icmp ne i32 %3, 0\n"
            "  br i1 %4, label %and.rhs.3, label %and.end.4\n"
            "and.rhs.3:\n"
            "  %5 = load i32, ptr %b.1, align 4\n"
            "  %6 = icmp ne i32 %5, 0\n"
            "  br i1 %6, label %or.end.6, label %or.rhs.5\n"
            "or.rhs.5:\n"
            "  %7 = load i32, ptr %c.2, align 4\n"
            "  %8 = icmp ne i32 %7, 0\n"
            "  br label %or.end.6\n"
            "or.end.6:\n"
            "  %9 = phi i1 [ true, %and.rhs.3 ], [ %8, %or.rhs.5 ]\n"
            "  %10 = zext i1 %9 to i32\n"
            "  %11 = icmp ne i32 %10, 0\n"
            "  br label %and.end.4\n"
            "and.end.4:\n"
            "  %12 = phi i1 [ false, %entry ], [ %11, %or.end.6 ]\n"
            "  %13 = zext i1 %12 to i32\n"
            "  ret i32 %13\n"
            "}\n");
}

TEST(CodegenExpressionTest, NotFlipsTheTruthOfItsOperand) {
  std::string ir = generate("int a(int x) { return !x; }\nint b(double x) { return !x; }\n");
  expect_lines(function(ir, "a"),
               {"%2 = icmp ne i32 %1, 0", "%3 = xor i1 %2, true", "%4 = zext i1 %3 to i32"});
  expect_lines(function(ir, "b"), {"%2 = fcmp une double %1, 0x0000000000000000",
                                   "%3 = xor i1 %2, true", "%4 = zext i1 %3 to i32"});
}

TEST(CodegenExpressionTest, ConditionalJoinsBothArms) {
  std::string ir = generate("int v(int c, int a, long long b) { return c ? a : b; }\n"
                            "void f(void);\nvoid g(void);\n"
                            "void w(int c) { c ? f() : g(); }\n");
  expect_lines(function(ir, "v"),
               {"br i1 %4, label %cond.true.3, label %cond.false.4", "%6 = sext i32 %5 to i64",
                "br label %cond.end.5", "%7 = load i64, ptr %b.2, align 8", "br label %cond.end.5",
                "%8 = phi i64 [ %6, %cond.true.3 ], [ %7, %cond.false.4 ]",
                "%9 = trunc i64 %8 to i32"});
  std::string body = function(ir, "w");
  expect_lines(body, {"call void () @f()", "br label %cond.end.3", "call void () @g()",
                      "br label %cond.end.3", "ret void"});
  EXPECT_EQ(count(body, "phi"), 0);
}

TEST(CodegenExpressionTest, ConditionalJoinsTheBlocksItsArmsEndedIn) {
  std::string ir = generate("int f(int c, int a, int b) { return c ? a && b : b || a; }");
  expect_lines(function(ir, "f"),
               {"and.end.7:", "%10 = zext i1 %9 to i32", "br label %cond.end.5",
                "or.end.9:", "%16 = zext i1 %15 to i32", "br label %cond.end.5",
                "cond.end.5:", "%17 = phi i32 [ %10, %and.end.7 ], [ %16, %or.end.9 ]"});
}

TEST(CodegenExpressionTest, UnaryOperators) {
  std::string ir =
      generate("int a(int x) { return -x; }\ndouble b(double x) { return -x; }\n"
               "int c(int x) { return ~x; }\nint d(char x) { return +x; }\n"
               "unsigned long long e(int x) { return sizeof x + sizeof(double[3]); }\n");
  expect_lines(function(ir, "a"), {"%2 = sub i32 0, %1"});
  expect_lines(function(ir, "b"), {"%2 = fneg double %1"});
  expect_lines(function(ir, "c"), {"%2 = xor i32 %1, -1"});
  expect_lines(function(ir, "d"), {"%2 = sext i8 %1 to i32", "ret i32 %2"});
  expect_lines(function(ir, "e"), {"%1 = add i64 4, 24", "ret i64 %1"});
}

TEST(CodegenExpressionTest, IncrementComputesInThePromotedType) {
  std::string ir = generate("int a(unsigned char u) { return u++; }\n"
                            "int b(_Bool v) { return ++v; }\n"
                            "double c(double d) { return --d; }\n");
  expect_lines(function(ir, "a"),
               {"%1 = load i8, ptr %u.0, align 1", "%2 = zext i8 %1 to i32", "%3 = add i32 %2, 1",
                "%4 = trunc i32 %3 to i8", "store i8 %4, ptr %u.0, align 1",
                "%5 = zext i8 %1 to i32", "ret i32 %5"});
  expect_lines(function(ir, "b"),
               {"%2 = zext i8 %1 to i32", "%3 = add i32 %2, 1", "%4 = icmp ne i32 %3, 0",
                "%5 = zext i1 %4 to i8", "store i8 %5, ptr %v.0, align 1"});
  expect_lines(function(ir, "c"), {"%2 = fsub double %1, 0x3FF0000000000000",
                                   "store double %2, ptr %d.0, align 8", "ret double %2"});
}

TEST(CodegenExpressionTest, CompoundAssignmentComputesInItsComputationType) {
  std::string ir = generate("int a(char c) { return c += 300; }\n"
                            "int b(unsigned char c) { c >>= 1; return c; }\n"
                            "int d(int x, double y) { return x *= y; }\n");
  expect_lines(function(ir, "a"),
               {"%1 = load i8, ptr %c.0, align 1", "%2 = sext i8 %1 to i32", "%3 = add i32 %2, 300",
                "%4 = trunc i32 %3 to i8", "store i8 %4, ptr %c.0, align 1",
                "%5 = sext i8 %4 to i32", "ret i32 %5"});
  expect_lines(function(ir, "b"),
               {"%2 = zext i8 %1 to i32", "%3 = ashr i32 %2, 1", "%4 = trunc i32 %3 to i8"});
  expect_lines(function(ir, "d"),
               {"%2 = load i32, ptr %x.0, align 4", "%3 = load double, ptr %y.1, align 8",
                "%4 = sitofp i32 %2 to double", "%5 = fmul double %4, %3",
                "%6 = fptosi double %5 to i32", "store i32 %6, ptr %x.0, align 4", "ret i32 %6"});
}

TEST(CodegenExpressionTest, CompoundAssignmentEvaluatesItsTargetOnce) {
  std::string body = function(generate("void f(int *a, int i) { a[i++] += 1; }"), "f");
  EXPECT_EQ(count(body, "add i32"), 2);
  EXPECT_EQ(count(body, "getelementptr"), 1);
}

TEST(CodegenExpressionTest, AssignmentValueIsWhatWasStored) {
  expect_lines(function(generate("int f(int a, char b) { return a = b = 300; }"), "f"),
               {"%2 = trunc i32 300 to i8", "store i8 %2, ptr %b.1, align 1",
                "%3 = sext i8 %2 to i32", "store i32 %3, ptr %a.0, align 4", "ret i32 %3"});
}

TEST(CodegenExpressionTest, VolatileAccessesStayVolatile) {
  std::string ir = generate("typedef volatile int V;\n"
                            "int f(V *p) { *p = *p + 1; return *p; }\n");
  expect_lines(function(ir, "f"),
               {"%2 = load volatile i32, ptr %1, align 4", "store volatile i32 %3, ptr %4, align 4",
                "%6 = load volatile i32, ptr %5, align 4"});
}

TEST(CodegenExpressionTest, DiscardedExpressionsStillHaveTheirEffects) {
  std::string ir = generate("int g(void);\n"
                            "void f(int *a, int i, void *v) { a[i++]; (void)g(); *(int *)v;"
                            " g(), i; *(i++, v); }\n");
  std::string body = function(ir, "f");
  EXPECT_EQ(count(body, "call i32 () @g()"), 2);
  EXPECT_EQ(count(body, "add i32"), 2);
}

TEST(CodegenCallTest, CallsSpellTheirFunctionType) {
  std::string ir = generate("int printf(const char *, ...);\n"
                            "int old();\n"
                            "void none(void);\n"
                            "double two(float, char);\n"
                            "int f(int (*fp)(int), float x) {\n"
                            "  printf(\"%d %f\", 1, x);\n"
                            "  old(x, 'a');\n"
                            "  none();\n"
                            "  two(x, 1);\n"
                            "  return fp(2);\n"
                            "}\n");
  expect_lines(function(ir, "f"),
               {"%2 = load float, ptr %x.1, align 4", "%3 = fpext float %2 to double",
                "%4 = call i32 (ptr, ...) @printf(ptr @.str.0, i32 1, double %3)",
                "%5 = load float, ptr %x.1, align 4", "%6 = fpext float %5 to double",
                "%7 = call i32 (double, i32) @old(double %6, i32 97)", "call void () @none()",
                "%8 = load float, ptr %x.1, align 4", "%9 = trunc i32 1 to i8",
                "%10 = call double (float, i8) @two(float %8, i8 %9)",
                "%11 = load ptr, ptr %fp.0, align 8", "%12 = call i32 (i32) %11(i32 2)"});
}

TEST(CodegenCallTest, DeclaresEachFunctionUsedButNotDefinedOnce) {
  std::string ir = generate("int puts(const char *);\n"
                            "int old();\n"
                            "int sum(int a[], int n, int (*f)(void), ...);\n"
                            "int unused(int);\n"
                            "int later(void);\n"
                            "int g(void) { return puts(\"z\"); }\n"
                            "int f(void) {\n"
                            "  int puts(const char *);\n"
                            "  puts(\"x\");\n"
                            "  puts(\"y\");\n"
                            "  later();\n"
                            "  return old() + sum(0, 1, f);\n"
                            "}\n"
                            "int later(void) { return 0; }\n");
  EXPECT_EQ(ir.substr(ir.find("declare")), "declare i32 @puts(ptr)\n"
                                           "declare i32 @old(...)\n"
                                           "declare i32 @sum(ptr, i32, ptr, ...)\n");
}

TEST(CodegenCallTest, BlockScopeDeclarationOfALaterDefinitionIsNotDeclared) {
  std::string ir = generate("int f(void) { int later(void); return later(); }\n"
                            "int later(void) { return 0; }\n");
  EXPECT_EQ(count(ir, "declare"), 0);
}

TEST(CodegenStatementTest, IfAndLoopsShapeTheirBlocks) {
  EXPECT_EQ(function(generate("int f(int n) {\n"
                              "  int t = 0;\n"
                              "  while (n > 0) {\n"
                              "    if (n == 3) { n--; continue; }\n"
                              "    for (int i = 0; ; i++) { if (i > n) break; t += i; }\n"
                              "    n--;\n"
                              "  }\n"
                              "  do t++; while (t < 0);\n"
                              "  return t;\n"
                              "}\n"),
                     "f"),
            "define i32 @f(i32 %0) {\n"
            "entry:\n"
            "  %n.0 = alloca i32, align 4\n"
            "  %t.1 = alloca i32, align 4\n"
            "  %i.7 = alloca i32, align 4\n"
            "  store i32 %0, ptr %n.0, align 4\n"
            "  store i32 0, ptr %t.1, align 4\n"
            "  br label %while.cond.2\n"
            "while.cond.2:\n"
            "  %1 = load i32, ptr %n.0, align 4\n"
            "  %2 = icmp sgt i32 %1, 0\n"
            "  %3 = zext i1 %2 to i32\n"
            "  %4 = icmp ne i32 %3, 0\n"
            "  br i1 %4, label %while.body.3, label %while.end.4\n"
            "while.body.3:\n"
            "  %5 = load i32, ptr %n.0, align 4\n"
            "  %6 = icmp eq i32 %5, 3\n"
            "  %7 = zext i1 %6 to i32\n"
            "  %8 = icmp ne i32 %7, 0\n"
            "  br i1 %8, label %if.then.5, label %if.end.6\n"
            "if.then.5:\n"
            "  %9 = load i32, ptr %n.0, align 4\n"
            "  %10 = sub i32 %9, 1\n"
            "  store i32 %10, ptr %n.0, align 4\n"
            "  br label %while.cond.2\n"
            "if.end.6:\n"
            "  store i32 0, ptr %i.7, align 4\n"
            "  br label %for.cond.8\n"
            "for.cond.8:\n"
            "  br label %for.body.9\n"
            "for.body.9:\n"
            "  %11 = load i32, ptr %i.7, align 4\n"
            "  %12 = load i32, ptr %n.0, align 4\n"
            "  %13 = icmp sgt i32 %11, %12\n"
            "  %14 = zext i1 %13 to i32\n"
            "  %15 = icmp ne i32 %14, 0\n"
            "  br i1 %15, label %if.then.12, label %if.end.13\n"
            "if.then.12:\n"
            "  br label %for.end.11\n"
            "if.end.13:\n"
            "  %16 = load i32, ptr %t.1, align 4\n"
            "  %17 = load i32, ptr %i.7, align 4\n"
            "  %18 = add i32 %16, %17\n"
            "  store i32 %18, ptr %t.1, align 4\n"
            "  br label %for.inc.10\n"
            "for.inc.10:\n"
            "  %19 = load i32, ptr %i.7, align 4\n"
            "  %20 = add i32 %19, 1\n"
            "  store i32 %20, ptr %i.7, align 4\n"
            "  br label %for.cond.8\n"
            "for.end.11:\n"
            "  %21 = load i32, ptr %n.0, align 4\n"
            "  %22 = sub i32 %21, 1\n"
            "  store i32 %22, ptr %n.0, align 4\n"
            "  br label %while.cond.2\n"
            "while.end.4:\n"
            "  br label %do.body.14\n"
            "do.body.14:\n"
            "  %23 = load i32, ptr %t.1, align 4\n"
            "  %24 = add i32 %23, 1\n"
            "  store i32 %24, ptr %t.1, align 4\n"
            "  br label %do.cond.15\n"
            "do.cond.15:\n"
            "  %25 = load i32, ptr %t.1, align 4\n"
            "  %26 = icmp slt i32 %25, 0\n"
            "  %27 = zext i1 %26 to i32\n"
            "  %28 = icmp ne i32 %27, 0\n"
            "  br i1 %28, label %do.body.14, label %do.end.16\n"
            "do.end.16:\n"
            "  %29 = load i32, ptr %t.1, align 4\n"
            "  ret i32 %29\n"
            "}\n");
}

TEST(CodegenStatementTest, IfElseJoinsOnlyFromArmsThatDidNotReturn) {
  std::string body = function(generate("int f(int c) { int r; if (c) r = 1; else return 2;"
                                       " return r; }"),
                              "f");
  expect_lines(body, {"br i1 %2, label %if.then.2, label %if.else.3",
                      "if.then.2:", "store i32 1, ptr %r.1, align 4", "br label %if.end.4",
                      "if.else.3:", "ret i32 2", "if.end.4:"});
  EXPECT_EQ(count(body, "dead"), 0);
}

TEST(CodegenStatementTest, SwitchListsItsOwnCasesOnly) {
  EXPECT_EQ(function(generate("int f(int x, int y) {\n"
                              "  switch (x) {\n"
                              "  case -1: return 1;\n"
                              "  default: y++;\n"
                              "  case 7: if (y) { case 8: return 2; }\n"
                              "    switch (y) { case 9: break; }\n"
                              "    for (;;) { continue; }\n"
                              "  }\n"
                              "  return 0;\n"
                              "}\n"),
                     "f"),
            "define i32 @f(i32 %0, i32 %1) {\n"
            "entry:\n"
            "  %x.0 = alloca i32, align 4\n"
            "  %y.1 = alloca i32, align 4\n"
            "  store i32 %0, ptr %x.0, align 4\n"
            "  store i32 %1, ptr %y.1, align 4\n"
            "  %2 = load i32, ptr %x.0, align 4\n"
            "  switch i32 %2, label %switch.default.3 [ i32 -1, label %switch.case.2 i32 7, "
            "label %switch.case.4 i32 8, label %switch.case.5 ]\n"
            "switch.case.2:\n"
            "  ret i32 1\n"
            "switch.default.3:\n"
            "  %3 = load i32, ptr %y.1, align 4\n"
            "  %4 = add i32 %3, 1\n"
            "  store i32 %4, ptr %y.1, align 4\n"
            "  br label %switch.case.4\n"
            "switch.case.4:\n"
            "  %5 = load i32, ptr %y.1, align 4\n"
            "  %6 = icmp ne i32 %5, 0\n"
            "  br i1 %6, label %if.then.7, label %if.end.8\n"
            "if.then.7:\n"
            "  br label %switch.case.5\n"
            "switch.case.5:\n"
            "  ret i32 2\n"
            "if.end.8:\n"
            "  %7 = load i32, ptr %y.1, align 4\n"
            "  switch i32 %7, label %switch.end.10 [ i32 9, label %switch.case.9 ]\n"
            "switch.case.9:\n"
            "  br label %switch.end.10\n"
            "switch.end.10:\n"
            "  br label %for.cond.11\n"
            "for.cond.11:\n"
            "  br label %for.body.12\n"
            "for.body.12:\n"
            "  br label %for.inc.13\n"
            "for.inc.13:\n"
            "  br label %for.cond.11\n"
            "for.end.14:\n"
            "  br label %switch.end.6\n"
            "switch.end.6:\n"
            "  ret i32 0\n"
            "}\n");
}

TEST(CodegenStatementTest, SwitchWithoutDefaultGoesToTheEnd) {
  expect_lines(function(generate("void f(char c) { switch (c) { case 'a': break; } }"), "f"),
               {"%2 = sext i8 %1 to i32",
                "switch i32 %2, label %switch.end.2 [ i32 97, label %switch.case.1 ]"});
}

TEST(CodegenStatementTest, BreakInsideSwitchKeepsTheLoopsContinue) {
  std::string body = function(generate("void f(int n) { while (n) { switch (n) { case 1:"
                                       " continue; } break; } }"),
                              "f");
  expect_lines(
      body, {"switch.case.4:", "br label %while.cond.1", "switch.end.5:", "br label %while.end.3"});
}

TEST(CodegenStatementTest, GotoJumpsForwardAndBackward) {
  expect_lines(function(generate("int f(int n) { again: if (n) goto out; n++; goto again;"
                                 " out: return n; }"),
                        "f"),
               {"br label %label.1", "label.1:", "br i1 %2, label %if.then.2, label %if.end.3",
                "if.then.2:", "br label %label.4", "if.end.3:", "br label %label.1",
                "label.4:", "ret i32 %5"});
}

TEST(CodegenStatementTest, EachFunctionHasItsOwnLabels) {
  std::string ir = generate("void f(int n) { again: if (n) goto again; }\n"
                            "void g(int n) { int x = n; again: if (x) goto again; }\n");
  expect_lines(function(ir, "g"),
               {"br label %label.2", "label.2:", "br i1 %3, label %if.then.3, label %if.end.4",
                "if.then.3:", "br label %label.2"});
}

TEST(CodegenStatementTest, ContinueInADoLoopGoesToItsCondition) {
  expect_lines(
      function(generate("void f(int n) { do { if (n) continue; n++; } while (n < 9); }"), "f"),
      {"if.then.4:", "br label %do.cond.2", "if.end.5:"});
}

TEST(CodegenStatementTest, LoopTargetsComeBackAfterAnInnerLoop) {
  expect_lines(function(generate("void f(int n) { while (n) { do { if (n) continue; break; }"
                                 " while (0); if (n) continue; break; } }"),
                        "f"),
               {"do.body.4:", "br label %do.cond.5", "br label %do.end.6",
                "do.end.6:", "br label %while.cond.1", "br label %while.end.3"});
}

TEST(CodegenModuleTest, FunctionsDoNotShareTheirBuffers) {
  std::string ir = generate("int a(int x) { return x; }\nint b(void) { return 1; }\n");
  EXPECT_EQ(function(ir, "b"), "define i32 @b() {\nentry:\n  ret i32 1\n}\n");
  std::string first = generate("int b(void) { return 1; }\nint a(int x) { return x; }\n");
  EXPECT_EQ(function(first, "b"), "define i32 @b() {\nentry:\n  ret i32 1\n}\n");
}

TEST(CodegenStatementTest, CodeAfterAJumpGoesIntoADeadBlock) {
  expect_lines(function(generate("int f(int n) { return n; n++; return 0; }"), "f"),
               {"ret i32 %1", "dead.1:", "%2 = load i32, ptr %n.0, align 4", "ret i32 0"});
}

TEST(CodegenStatementTest, LocalsAreAllocatedOnceInTheEntryBlock) {
  std::string body = function(generate("int f(int n) { int t = 0; while (n--) { int x = n;"
                                       " t += x; } return t; }"),
                              "f");
  size_t first_block = body.find("while.cond");
  EXPECT_LT(body.find("%x."), first_block);
  EXPECT_EQ(count(body, "alloca"), 3);
}

TEST(CodegenDataTest, StringLiteralsAreEscapedConstants) {
  std::string ir = generate("int puts(const char *);\n"
                            "void f(void) { puts(\"a\\\"b\\\\c\\n\\377\"); }\n");
  EXPECT_NE(ir.find("@.str.0 = private unnamed_addr constant [8 x i8] "
                    "c\"a\\22b\\5Cc\\0A\\FF\\00\", align 1\n"),
            std::string::npos)
      << ir;
}

TEST(CodegenDataTest, WideStringLiteralsAreRawBytesAlignedToTheirUnit) {
  const char *source = "int f(const void *);\nvoid g(void) { f(L\"hi\"); }\n";
  EXPECT_NE(generate(source).find("constant [6 x i8] c\"h\\00i\\00\\00\\00\", align 2"),
            std::string::npos);
  EXPECT_NE(
      generate(source, TARGET_LINUX_X64)
          .find("constant [12 x i8] c\"h\\00\\00\\00i\\00\\00\\00\\00\\00\\00\\00\", align 4"),
      std::string::npos);
}

TEST(CodegenDataTest, CharacterArraysCopyTheirString) {
  std::string ir = generate("void f(void) { char a[3] = \"abc\"; char b[] = \"hi\";"
                            " char c[10] = \"hi\"; }");
  expect_lines(function(ir, "f"),
               {"call void @llvm.memset.p0.i64(ptr align 1 %a.0, i8 0, i64 3, i1 false)",
                "call void @llvm.memcpy.p0.p0.i64(ptr align 1 %a.0, ptr align 1 @.str.0, i64 3, i1 "
                "false)",
                "call void @llvm.memset.p0.i64(ptr align 1 %b.1, i8 0, i64 3, i1 false)",
                "call void @llvm.memcpy.p0.p0.i64(ptr align 1 %b.1, ptr align 1 @.str.1, i64 3, i1 "
                "false)",
                "call void @llvm.memset.p0.i64(ptr align 1 %c.2, i8 0, i64 10, i1 false)",
                "call void @llvm.memcpy.p0.p0.i64(ptr align 1 %c.2, ptr align 1 @.str.2, i64 3, i1 "
                "false)"});
  EXPECT_NE(ir.find("@.str.0 = private unnamed_addr constant [4 x i8] c\"abc\\00\", align 1"),
            std::string::npos);
  EXPECT_EQ(ir.substr(ir.find("declare")),
            "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)\n"
            "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n");
}

TEST(CodegenDataTest, ArrayInitializersStoreEachEntryAtItsOffset) {
  expect_lines(function(generate("void f(int x) { int a[4] = {x, [2] = 5}; short s = 7; }"), "f"),
               {"call void @llvm.memset.p0.i64(ptr align 4 %a.1, i8 0, i64 16, i1 false)",
                "%1 = load i32, ptr %x.0, align 4", "store i32 %1, ptr %a.1, align 4",
                "%2 = getelementptr inbounds i8, ptr %a.1, i64 8", "store i32 5, ptr %2, align 4",
                "%3 = trunc i32 7 to i16", "store i16 %3, ptr %s.2, align 2"});
}

TEST(CodegenDataTest, IntrinsicsAreDeclaredOnlyWhenUsed) {
  std::string ir = generate("int f(void) { int x = 1; return x; }");
  EXPECT_EQ(count(ir, "declare"), 0);
  std::string only_set = generate("void f(void) { int a[2] = {1}; }");
  EXPECT_EQ(only_set.substr(only_set.find("declare")),
            "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)\n");
}

TEST(CodegenDataTest, ConstantsAreExact) {
  std::string ir = generate("float a(void) { return 1.5f; }\n"
                            "double b(void) { return 0.1; }\n"
                            "long double c(void) { return 1.0L; }\n"
                            "long double d(void) { return -2.5L; }\n"
                            "unsigned e(void) { return 4294967295u; }\n"
                            "int f(void) { return 0x10 + 010 + 'A' + L'\\xFFFF'; }\n"
                            "int g(void) { return 'ab'; }\n");
  expect_lines(function(ir, "a"), {"ret float 0x3FF8000000000000"});
  expect_lines(function(ir, "b"), {"ret double 0x3FB999999999999A"});
  expect_lines(function(ir, "c"), {"store x86_fp80 0xK3FFF8000000000000000, ptr %0, align 16"});
  expect_lines(function(ir, "d"), {"%1 = fneg x86_fp80 0xK4000A000000000000000"});
  expect_lines(function(ir, "e"), {"ret i32 -1"});
  expect_lines(function(ir, "f"), {"%0 = add i32 16, 8", "%1 = add i32 %0, 65",
                                   "%2 = zext i16 -1 to i32", "%3 = add i32 %1, %2"});
  expect_lines(function(ir, "g"), {"ret i32 24930"});
}

TEST(CodegenDataTest, FloatConstantsAreRoundedOnceToTheirOwnType) {
  std::string ir = generate("float f(void) { return 0.1f; }\n"
                            "float g(void) { return 0x1.000001000000000001p0f; }\n");
  expect_lines(function(ir, "f"), {"ret float 0x3FB99999A0000000"});
  expect_lines(function(ir, "g"), {"ret float 0x3FF0000020000000"});
}

TEST(CodegenDataTest, EnumeratorsAreTheirValues) {
  expect_lines(function(generate("enum e { A = -3, B };\nint f(enum e x) { return x < B; }"), "f"),
               {"%2 = icmp slt i32 %1, -2"});
}

TEST(CodegenDataTest, UnusualNamesAreQuoted) {
  std::string ir = generate("int caf\\u00e9(int na\\u00efve) { return na\\u00efve; }");
  EXPECT_NE(ir.find("define i32 @\"caf\\5Cu00e9\"(i32 %0) {"), std::string::npos) << ir;
  EXPECT_NE(ir.find("%\"na\\5Cu00efve.0\" = alloca i32, align 4"), std::string::npos) << ir;
}

static bool has_line(const std::string &text, const std::string &line) {
  return text.find("\n" + line + "\n") != std::string::npos;
}

static const char *records = "struct point { int x, y; };\n"
                             "struct shape { char tag; struct point corner[2]; };\n"
                             "union u { int i; char c; };\n";

TEST(CodegenRecordTest, MemberAccessIsAByteOffsetFromTheRecord) {
  std::string ir = generate((std::string(records) +
                             "int get(struct point *p) { return p->y; }\n"
                             "int nested(struct shape *s) { return s->corner[1].y; }\n"
                             "int dot(void) { struct shape s; s.tag = 1; return s.corner[0].x; }\n"
                             "char un(union u *v) { return v->c; }\n")
                                .c_str());
  expect_lines(function(ir, "get"),
               {"%1 = load ptr, ptr %p.0, align 8", "%2 = getelementptr inbounds i8, ptr %1, i64 4",
                "%3 = load i32, ptr %2, align 4", "ret i32 %3"});
  expect_lines(function(ir, "nested"),
               {"%2 = getelementptr inbounds i8, ptr %1, i64 4",
                "%4 = getelementptr inbounds [8 x i8], ptr %2, i64 %3",
                "%5 = getelementptr inbounds i8, ptr %4, i64 4", "%6 = load i32, ptr %5, align 4"});
  expect_lines(function(ir, "dot"),
               {"%s.0 = alloca [20 x i8], align 4", "store i8 %0, ptr %s.0, align 1",
                "%1 = getelementptr inbounds i8, ptr %s.0, i64 4",
                "%3 = getelementptr inbounds [8 x i8], ptr %1, i64 %2",
                "%4 = load i32, ptr %3, align 4"});
  std::string un = function(ir, "un");
  expect_lines(un, {"%1 = load ptr, ptr %v.0, align 8", "%2 = load i8, ptr %1, align 1"});
  EXPECT_EQ(count(un, "getelementptr"), 0) << un;
}

TEST(CodegenRecordTest, StructureAssignmentCopiesItsBytes) {
  std::string ir =
      generate((std::string(records) +
                "void copy(struct shape *a, struct shape *b) { *a = *b; }\n"
                "void vcopy(volatile struct point *a, struct point *b) { *a = *b; }\n"
                "int comma(void) { struct point a, b; b = (a.x = 1, a); return b.x; }\n"
                "void ucopy(union u *a, union u *b) { *a = *b; }\n")
                   .c_str());
  expect_lines(
      function(ir, "copy"),
      {"%2 = load ptr, ptr %b.1, align 8", "%3 = load ptr, ptr %a.0, align 8",
       "call void @llvm.memcpy.p0.p0.i64(ptr align 4 %3, ptr align 4 %2, i64 20, i1 false)",
       "ret void"});
  expect_lines(
      function(ir, "vcopy"),
      {"call void @llvm.memcpy.p0.p0.i64(ptr align 4 %3, ptr align 4 %2, i64 8, i1 true)"});
  expect_lines(
      function(ir, "comma"),
      {"store i32 1, ptr %a.0, align 4",
       "call void @llvm.memcpy.p0.p0.i64(ptr align 4 %b.1, ptr align 4 %a.0, i64 8, i1 false)",
       "%0 = load i32, ptr %b.1, align 4"});
  expect_lines(
      function(ir, "ucopy"),
      {"call void @llvm.memcpy.p0.p0.i64(ptr align 4 %3, ptr align 4 %2, i64 4, i1 false)"});
  EXPECT_TRUE(has_line(ir, "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)")) << ir;
}

TEST(CodegenRecordTest, ConditionalOfStructuresJoinsTheirAddresses) {
  std::string ir = generate((std::string(records) +
                             "int pick(int c, struct point *a, struct point *b) {\n"
                             "  struct point r = c ? *a : *b; return r.x; }\n"
                             "int member(int c) { struct point a, b; return (c ? a : b).y; }\n")
                                .c_str());
  expect_lines(
      function(ir, "pick"),
      {"%r.3 = alloca [8 x i8], align 4", "cond.true.4:", "%5 = load ptr, ptr %a.1, align 8",
       "cond.false.5:", "%6 = load ptr, ptr %b.2, align 8",
       "cond.end.6:", "%7 = phi ptr [ %5, %cond.true.4 ], [ %6, %cond.false.5 ]",
       "call void @llvm.memcpy.p0.p0.i64(ptr align 4 %r.3, ptr align 4 %7, i64 8, i1 false)",
       "%8 = load i32, ptr %r.3, align 4"});
  expect_lines(function(ir, "member"),
               {"cond.end.5:", "%3 = phi ptr [ %a.1, %cond.true.3 ], [ %b.2, %cond.false.4 ]",
                "%4 = getelementptr inbounds i8, ptr %3, i64 4", "%5 = load i32, ptr %4, align 4",
                "ret i32 %5"});
}

TEST(CodegenRecordTest, AnAnonymousMemberAddsItsOffsetOnTheWay) {
  std::string ir = generate("#include <anon.h>\n"
                            "int high(struct pair *p) { return p->high; }\n"
                            "unsigned low(struct pair p) { return p.low; }\n",
                            TARGET_WINDOWS_X64, true);
  expect_lines(function(ir, "high"),
               {"%1 = load ptr, ptr %p.0, align 8", "%2 = getelementptr inbounds i8, ptr %1, i64 8",
                "%3 = getelementptr inbounds i8, ptr %2, i64 4", "%4 = load i32, ptr %3, align 4",
                "ret i32 %4"});
  expect_lines(function(ir, "low"), {"%1 = getelementptr inbounds i8, ptr %p.0, i64 8",
                                     "%2 = load i32, ptr %1, align 4", "ret i32 %2"});
}

static const char *flags =
    "struct flags { unsigned ready : 1; int delta : 5; unsigned mode : 3; };\n"
    "struct full { int x : 32; _Bool b : 1; };\n";

TEST(CodegenBitfieldTest, ReadsShiftTheFieldOutOfItsBytes) {
  std::string ir =
      generate((std::string(flags) + "int rd(struct flags *f) { return f->delta; }\n"
                                     "unsigned rm(struct flags *f) { return f->mode; }\n")
                   .c_str());
  expect_lines(function(ir, "rd"), {"%2 = load i8, ptr %1, align 1", "%3 = shl i8 %2, 2",
                                    "%4 = ashr i8 %3, 3", "%5 = sext i8 %4 to i32", "ret i32 %5"});
  expect_lines(function(ir, "rm"),
               {"%2 = load i16, ptr %1, align 1", "%3 = shl i16 %2, 7", "%4 = lshr i16 %3, 13",
                "%5 = zext i16 %4 to i32", "ret i32 %5"});
}

TEST(CodegenBitfieldTest, WritesMergeTheFieldIntoItsBytes) {
  std::string ir =
      generate((std::string(flags) + "void wr(struct flags *f, int v) { f->delta = v; }\n"
                                     "void wf(struct full *f, int v) { f->x = v; }\n")
                   .c_str());
  expect_lines(function(ir, "wr"),
               {"%2 = load i32, ptr %v.1, align 4", "%3 = load ptr, ptr %f.0, align 8",
                "%4 = trunc i32 %2 to i8", "%5 = load i8, ptr %3, align 1", "%6 = and i8 %4, 31",
                "%7 = shl i8 %6, 1", "%8 = and i8 %5, -63", "%9 = or i8 %8, %7",
                "store i8 %9, ptr %3, align 1"});
  std::string wf = function(ir, "wf");
  expect_lines(wf,
               {"%3 = load ptr, ptr %f.0, align 8", "store i32 %2, ptr %3, align 1", "ret void"});
  EXPECT_EQ(count(wf, " and "), 0) << wf;
  EXPECT_EQ(count(wf, "load i32, ptr %3"), 0) << wf;
}

TEST(CodegenBitfieldTest, AssignmentValueIsWhatTheFieldNowHolds) {
  std::string ir =
      generate((std::string(flags) + "int av(struct flags *f) { return f->delta = 100; }\n"
                                     "unsigned inc(struct flags *f) { return ++f->mode; }\n"
                                     "int post(struct flags *f) { return f->delta++; }\n"
                                     "void add(struct flags *f) { f->mode += 3; }\n")
                   .c_str());
  expect_lines(function(ir, "av"), {"%2 = trunc i32 100 to i8", "store i8 %7, ptr %1, align 1",
                                    "%8 = shl i32 100, 27", "%9 = ashr i32 %8, 27", "ret i32 %9"});
  expect_lines(function(ir, "inc"),
               {"%2 = load i16, ptr %1, align 1", "%5 = zext i16 %4 to i32", "%6 = add i32 %5, 1",
                "%7 = trunc i32 %6 to i16", "%8 = load i16, ptr %1, align 1", "%9 = and i16 %7, 7",
                "%10 = shl i16 %9, 6", "%11 = and i16 %8, -449", "%12 = or i16 %11, %10",
                "store i16 %12, ptr %1, align 1", "%13 = shl i32 %6, 29", "%14 = lshr i32 %13, 29",
                "ret i32 %14"});
  expect_lines(function(ir, "post"),
               {"%4 = ashr i8 %3, 3", "%5 = sext i8 %4 to i32", "%6 = add i32 %5, 1",
                "store i8 %12, ptr %1, align 1", "ret i32 %5"});
  expect_lines(function(ir, "add"),
               {"%5 = zext i16 %4 to i32", "%6 = add i32 %5, 3", "%7 = trunc i32 %6 to i16",
                "%9 = and i16 %7, 7", "%10 = shl i16 %9, 6", "store i16 %12, ptr %1, align 1"});
}

TEST(CodegenBitfieldTest, BoolFieldStoresTheConvertedValue) {
  std::string ir = generate(
      (std::string(flags) + "int bb(struct full *f) { f->b = 5; return f->b; }\n").c_str());
  expect_lines(function(ir, "bb"),
               {"%1 = icmp ne i32 5, 0", "%2 = zext i1 %1 to i8",
                "%4 = getelementptr inbounds i8, ptr %3, i64 4", "%5 = load i8, ptr %4, align 1",
                "%6 = and i8 %2, 1", "%7 = and i8 %5, -2", "%8 = or i8 %7, %6",
                "store i8 %8, ptr %4, align 1", "%13 = load i8, ptr %12, align 1",
                "%14 = shl i8 %13, 7", "%15 = lshr i8 %14, 7", "%16 = zext i8 %15 to i32"});
}

TEST(CodegenBitfieldTest, FieldsSitWhereEachTargetPutsThem) {
  const char *source = "struct mixed { char c : 4; int i : 6; };\n"
                       "int rd(struct mixed *m) { return m->i; }\n";
  expect_lines(function(generate(source), "rd"),
               {"%2 = getelementptr inbounds i8, ptr %1, i64 4", "%3 = load i8, ptr %2, align 1",
                "%4 = shl i8 %3, 2", "%5 = ashr i8 %4, 2", "%6 = sext i8 %5 to i32"});
  std::string linux_rd = function(generate(source, TARGET_LINUX_X64), "rd");
  expect_lines(linux_rd, {"%2 = load i16, ptr %1, align 1", "%3 = shl i16 %2, 6",
                          "%4 = ashr i16 %3, 10", "%5 = sext i16 %4 to i32"});
  EXPECT_EQ(count(linux_rd, "getelementptr"), 0) << linux_rd;
}

TEST(CodegenBitfieldTest, PackedFieldPastItsTypeUsesAWiderUnit) {
  const char *source = "#pragma pack(1)\n"
                       "struct packed { char c; long long s : 60; };\n"
                       "#pragma pack()\n"
                       "long long rs(struct packed *p) { return p->s; }\n"
                       "void ws(struct packed *p, long long v) { p->s = v; }\n";
  std::string linux_ir = generate(source, TARGET_LINUX_X64);
  expect_lines(function(linux_ir, "rs"),
               {"%2 = load i72, ptr %1, align 1", "%3 = shl i72 %2, 4", "%4 = ashr i72 %3, 12",
                "%5 = trunc i72 %4 to i64", "ret i64 %5"});
  expect_lines(function(linux_ir, "ws"),
               {"%4 = zext i64 %2 to i72", "%5 = load i72, ptr %3, align 1",
                "%6 = and i72 %4, u0x00000000000000000FFFFFFFFFFFFFFF", "%7 = shl i72 %6, 8",
                "%8 = and i72 %5, u0x00000000000000F000000000000000FF", "%9 = or i72 %8, %7",
                "store i72 %9, ptr %3, align 1", "%10 = shl i64 %2, 4", "%11 = ashr i64 %10, 4"});
  std::string windows_ir = generate(source);
  expect_lines(function(windows_ir, "rs"),
               {"%2 = getelementptr inbounds i8, ptr %1, i64 1", "%3 = load i64, ptr %2, align 1",
                "%4 = shl i64 %3, 4", "%5 = ashr i64 %4, 4", "ret i64 %5"});
  expect_lines(function(windows_ir, "ws"),
               {"%6 = and i64 %2, 1152921504606846975", "%7 = and i64 %5, -1152921504606846976",
                "%8 = or i64 %7, %6", "store i64 %8, ptr %4, align 1"});
}

TEST(CodegenGlobalTest, StaticObjectsAreByteImages) {
  std::string ir = generate("int table[4] = {1, 2};\n"
                            "static int hidden;\n"
                            "short s = -2;\n"
                            "float f = 1.5f;\n"
                            "double d = -2.0;\n"
                            "long double ld = 1.0L;\n"
                            "struct bits { unsigned x : 3; unsigned y : 5; int z : 4; } bits = "
                            "{5, 17, -1};\n"
                            "char text[8] = \"hi\";\n"
                            "char exact[3] = \"hey\";\n"
                            "struct sb { char a[2]; char b; } sb = {.b = 'x', .a = \"ab\"};\n"
                            "char *null_pointer = 0;\n"
                            "char *number = (char *)64;\n"
                            "int three[3];\n"
                            "int use(void) { return hidden; }\n");
  for (const char *line :
       {"@table = global [16 x i8] c\"\\01\\00\\00\\00\\02\\00\\00\\00\\00\\00\\00\\00\\00\\00\\00"
        "\\00\", align 16",
        "@hidden = internal global [4 x i8] zeroinitializer, align 4",
        "@s = global [2 x i8] c\"\\FE\\FF\", align 2",
        "@f = global [4 x i8] c\"\\00\\00\\C0?\", align 4",
        "@d = global [8 x i8] c\"\\00\\00\\00\\00\\00\\00\\00\\C0\", align 8",
        "@ld = global [16 x i8] "
        "c\"\\00\\00\\00\\00\\00\\00\\00\\80\\FF?\\00\\00\\00\\00\\00\\00\", "
        "align 16",
        "@bits = global [4 x i8] c\"\\8D\\0F\\00\\00\", align 4",
        "@text = global [8 x i8] c\"hi\\00\\00\\00\\00\\00\\00\", align 1",
        "@exact = global [3 x i8] c\"hey\", align 1", "@sb = global [3 x i8] c\"abx\", align 1",
        "@null_pointer = global [8 x i8] zeroinitializer, align 8",
        "@number = global [8 x i8] c\"@\\00\\00\\00\\00\\00\\00\\00\", align 8",
        "@three = global [12 x i8] zeroinitializer, align 4"})
    EXPECT_TRUE(has_line(ir, line)) << line << "\nin:\n" << ir;
  expect_lines(function(ir, "use"), {"%0 = load i32, ptr @hidden, align 4"});
}

TEST(CodegenGlobalTest, AddressesAreRelocationsInMemoryOrder) {
  std::string ir =
      generate("int table[4];\n"
               "int *middle = &table[2];\n"
               "int *first = table;\n"
               "int a, b;\n"
               "int *backwards[2] = {[1] = &a, [0] = &b};\n"
               "struct mix { char c; int *p; char *q; } mix = {'x', &a, \"s\" + 1};\n");
  for (const char *line :
       {"@middle = global <{ ptr }> <{ ptr getelementptr (i8, ptr @table, i64 8) }>, align 8",
        "@first = global <{ ptr }> <{ ptr @table }>, align 8",
        "@backwards = global <{ ptr, ptr }> <{ ptr @b, ptr @a }>, align 16",
        "@mix = global <{ [8 x i8], ptr, ptr }> <{ [8 x i8] c\"x\\00\\00\\00\\00\\00\\00\\00\", "
        "ptr @a, ptr getelementptr (i8, ptr @.str.0, i64 1) }>, align 8"})
    EXPECT_TRUE(has_line(ir, line)) << line << "\nin:\n" << ir;
}

TEST(CodegenGlobalTest, EachObjectIsDefinedOnce) {
  std::string ir = generate("int c;\nint c;\n"
                            "int answer = 42;\nextern int answer;\n"
                            "extern int late;\nint late = 5;\n"
                            "int open[];\n"
                            "int use(void) { return c + answer + late + open[0]; }\n");
  EXPECT_EQ(count(ir, "@c = "), 1) << ir;
  EXPECT_EQ(count(ir, "@answer = "), 1) << ir;
  EXPECT_EQ(count(ir, "@late = "), 1) << ir;
  EXPECT_TRUE(has_line(ir, "@c = global [4 x i8] zeroinitializer, align 4")) << ir;
  EXPECT_TRUE(has_line(ir, "@answer = global [4 x i8] c\"*\\00\\00\\00\", align 4")) << ir;
  EXPECT_TRUE(has_line(ir, "@late = global [4 x i8] c\"\\05\\00\\00\\00\", align 4")) << ir;
  EXPECT_TRUE(has_line(ir, "@open = global [4 x i8] zeroinitializer, align 4")) << ir;
  EXPECT_EQ(count(ir, "external"), 0) << ir;
}

TEST(CodegenGlobalTest, ArraysOfSixteenBytesOrMoreAreAlignedTo16) {
  std::string ir = generate("char sixteen[16];\nchar fifteen[15];\nint four[4];\n"
                            "struct pair { long long a, b; } pair;\n"
                            "void use(char *);\n"
                            "void locals(void) { char buf[16]; char small[15]; int many[4];\n"
                            "  use(buf); use(small); use((char *)many); }\n");
  EXPECT_TRUE(has_line(ir, "@sixteen = global [16 x i8] zeroinitializer, align 16")) << ir;
  EXPECT_TRUE(has_line(ir, "@fifteen = global [15 x i8] zeroinitializer, align 1")) << ir;
  EXPECT_TRUE(has_line(ir, "@four = global [16 x i8] zeroinitializer, align 16")) << ir;
  EXPECT_TRUE(has_line(ir, "@pair = global [16 x i8] zeroinitializer, align 8")) << ir;
  expect_lines(function(ir, "locals"),
               {"%buf.0 = alloca [16 x i8], align 16", "%small.1 = alloca [15 x i8], align 1",
                "%many.2 = alloca [4 x i32], align 16"});
}

TEST(CodegenGlobalTest, DataUsedButNotDefinedIsDeclared) {
  std::string ir = generate("extern int ext;\n"
                            "extern int unused;\n"
                            "__attribute__((dllimport)) extern int imported;\n"
                            "__attribute__((dllimport)) int far(int);\n"
                            "int use(void) { return ext + imported + far(1); }\n");
  expect_lines(function(ir, "use"),
               {"%0 = load i32, ptr @ext, align 4", "%1 = load i32, ptr @imported, align 4",
                "%3 = call i32 (i32) @far(i32 1)"});
  EXPECT_TRUE(has_line(ir, "@ext = external global i8")) << ir;
  EXPECT_TRUE(has_line(ir, "@imported = external dllimport global i8")) << ir;
  EXPECT_TRUE(has_line(ir, "declare dllimport i32 @far(i32)")) << ir;
  EXPECT_EQ(count(ir, "unused"), 0) << ir;
}

TEST(CodegenGlobalTest, AsmLabelsNameEveryUseAndDefinition) {
  std::string ir = generate("int labelled(int) __asm__(\"real_name\");\n"
                            "int other_name(int) __asm__(\"real_name\");\n"
                            "int data_label __asm__(\"data_real\") = 3;\n"
                            "extern int ext_label __asm__(\"ext_real\");\n"
                            "int defined_label(void) __asm__(\"defined_real\");\n"
                            "int defined_label(void) { return 1; }\n"
                            "int use(void) { return labelled(2) + other_name(3) + data_label +\n"
                            "  ext_label + defined_label(); }\n");
  expect_lines(function(ir, "use"),
               {"%0 = call i32 (i32) @real_name(i32 2)", "%1 = call i32 (i32) @real_name(i32 3)",
                "%3 = load i32, ptr @data_real, align 4", "%5 = load i32, ptr @ext_real, align 4",
                "%7 = call i32 () @defined_real()"});
  EXPECT_NE(function(ir, "defined_real"), "") << ir;
  EXPECT_TRUE(has_line(ir, "@data_real = global [4 x i8] c\"\\03\\00\\00\\00\", align 4")) << ir;
  EXPECT_TRUE(has_line(ir, "@ext_real = external global i8")) << ir;
  EXPECT_EQ(count(ir, "declare i32 @real_name(i32)"), 1) << ir;
  EXPECT_EQ(count(ir, "declare i32 @defined_real"), 0) << ir;
  for (const char *name : {"labelled", "other_name", "data_label", "ext_label", "defined_label"})
    EXPECT_EQ(count(ir, std::string("@") + name), 0) << name << "\n" << ir;
}

TEST(CodegenGlobalTest, InlineDefinitionsAreLeftToAnotherFile) {
  std::string ir = generate("inline int helper(void) { return 2; }\n"
                            "static inline int local(void) { return 3; }\n"
                            "int use(void) { return helper() + local(); }\n");
  EXPECT_EQ(function(ir, "helper"), "") << ir;
  EXPECT_TRUE(has_line(ir, "declare i32 @helper()")) << ir;
  EXPECT_NE(function(ir, "local"), "") << ir;
  expect_lines(function(ir, "use"), {"%0 = call i32 () @helper()", "%1 = call i32 () @local()"});
}

TEST(CodegenGlobalTest, StaticLocalsAreInternalGlobalsNamedByTheirFunction) {
  std::string ir = generate("int one(void) { static int calls; return ++calls; }\n"
                            "int two(void) { static int calls = 5;\n"
                            "  { static int calls = 9; calls++; }\n"
                            "  static int *p = &calls; return *p; }\n");
  for (const char *line : {"@one.calls.0 = internal global [4 x i8] zeroinitializer, align 4",
                           "@two.calls.1 = internal global [4 x i8] c\"\\05\\00\\00\\00\", align 4",
                           "@two.calls.2 = internal global [4 x i8] c\"\\09\\00\\00\\00\", align 4",
                           "@two.p.3 = internal global <{ ptr }> <{ ptr @two.calls.1 }>, align 8"})
    EXPECT_TRUE(has_line(ir, line)) << line << "\nin:\n" << ir;
  expect_lines(function(ir, "one"), {"%0 = load i32, ptr @one.calls.0, align 4",
                                     "store i32 %1, ptr @one.calls.0, align 4"});
  std::string two = function(ir, "two");
  expect_lines(two,
               {"%0 = load i32, ptr @two.calls.2, align 4",
                "store i32 %1, ptr @two.calls.2, align 4", "%2 = load ptr, ptr @two.p.3, align 8"});
  EXPECT_EQ(count(two, "alloca"), 0) << two;
}

TEST(CodegenGlobalTest, FuncIsOneConstantPerFunction) {
  std::string ir =
      generate("const char *name(void) { return __func__; }\n"
               "const char *both(void) { const char *a = __FUNCTION__;\n"
               "  const char *b = __PRETTY_FUNCTION__; return a == b ? __func__ : 0; }\n");
  EXPECT_TRUE(
      has_line(ir, "@__func__.0 = private unnamed_addr constant [5 x i8] c\"name\\00\", align 1"))
      << ir;
  EXPECT_TRUE(
      has_line(ir, "@__func__.1 = private unnamed_addr constant [5 x i8] c\"both\\00\", align 1"))
      << ir;
  EXPECT_EQ(count(ir, "private unnamed_addr constant"), 2) << ir;
  expect_lines(function(ir, "name"), {"ret ptr @__func__.0"});
  expect_lines(function(ir, "both"), {"store ptr @__func__.1, ptr %a.0, align 8",
                                      "store ptr @__func__.1, ptr %b.1, align 8"});
}

TEST(CodegenGlobalTest, FileScopeCompoundLiteralsAreInternalGlobals) {
  std::string ir = generate("int *literal = (int[]){10, 20, 30};\n"
                            "int *pair[2] = {(int[]){1}, (int[]){2}};\n"
                            "int *inside = &(int[]){4, 5}[1];\n");
  for (const char *line :
       {"@.compound.0 = internal global [12 x i8] c\"\\0A\\00\\00\\00\\14\\00\\00\\00\\1E\\00\\00"
        "\\00\", align 4",
        "@literal = global <{ ptr }> <{ ptr @.compound.0 }>, align 8",
        "@.compound.1 = internal global [4 x i8] c\"\\01\\00\\00\\00\", align 4",
        "@.compound.2 = internal global [4 x i8] c\"\\02\\00\\00\\00\", align 4",
        "@pair = global <{ ptr, ptr }> <{ ptr @.compound.1, ptr @.compound.2 }>, align 16",
        "@inside = global <{ ptr }> <{ ptr getelementptr (i8, ptr @.compound.3, i64 4) }>, align "
        "8"})
    EXPECT_TRUE(has_line(ir, line)) << line << "\nin:\n" << ir;
}

TEST(CodegenGlobalTest, CompoundLiteralsInAFunctionAreInitializedEachTime) {
  std::string body = function(generate("int loop(int n) { int s = 0;\n"
                                       "  for (int i = 0; i < n; i++) { int *r = (int[]){i, 2};\n"
                                       "    s += r[0]; } return s; }\n"),
                              "loop");
  expect_lines(body,
               {"%compound.8 = alloca [2 x i32], align 4", "for.body.4:",
                "call void @llvm.memset.p0.i64(ptr align 4 %compound.8, i8 0, i64 8, i1 false)",
                "%6 = load i32, ptr %i.2, align 4", "store i32 %6, ptr %compound.8, align 4",
                "%7 = getelementptr inbounds i8, ptr %compound.8, i64 4",
                "store i32 2, ptr %7, align 4", "store ptr %compound.8, ptr %r.7, align 8",
                "for.inc.5:"});
}

TEST(CodegenVlaTest, VlaIsAllocatedWhereItsDeclarationRuns) {
  std::string ir = generate("long alloc(int n) { int a[n]; a[0] = 1; return sizeof a; }\n");
  EXPECT_EQ(function(ir, "alloc"), "define i32 @alloc(i32 %0) {\n"
                                   "entry:\n"
                                   "  %n.0 = alloca i32, align 4\n"
                                   "  %size.1 = alloca i64, align 8\n"
                                   "  store i32 %0, ptr %n.0, align 4\n"
                                   "  %1 = load i32, ptr %n.0, align 4\n"
                                   "  %2 = sext i32 %1 to i64\n"
                                   "  store i64 %2, ptr %size.1, align 8\n"
                                   "  %3 = load i64, ptr %size.1, align 8\n"
                                   "  %4 = mul i64 %3, 4\n"
                                   "  %5 = call ptr @llvm.stacksave.p0()\n"
                                   "  %a.2 = alloca i8, i64 %4, align 4\n"
                                   "  %6 = sext i32 0 to i64\n"
                                   "  %7 = getelementptr inbounds i32, ptr %a.2, i64 %6\n"
                                   "  store i32 1, ptr %7, align 4\n"
                                   "  %8 = load i64, ptr %size.1, align 8\n"
                                   "  %9 = mul i64 %8, 4\n"
                                   "  %10 = trunc i64 %9 to i32\n"
                                   "  ret i32 %10\n"
                                   "}\n");
  EXPECT_TRUE(has_line(ir, "declare ptr @llvm.stacksave.p0()")) << ir;
  EXPECT_TRUE(has_line(ir, "declare void @llvm.stackrestore.p0(ptr)")) << ir;
  std::string plain = generate("int none(void) { int a[4]; a[0] = 0; return a[0]; }\n");
  EXPECT_EQ(count(plain, "stacksave"), 0) << plain;
  EXPECT_EQ(count(plain, "stackrestore"), 0) << plain;
}

TEST(CodegenVlaTest, SizeofEvaluatesItsOperandOnlyForAVla) {
  std::string ir = generate("long sz(int k) { long x = sizeof(int[k++]);\n"
                            "  long y = sizeof(int[4][k++]); return x + y + k; }\n"
                            "int noeval(int k) { long z = sizeof(k++); return k + (int)z; }\n"
                            "long object(int n) { int a[n][3]; return sizeof a[n++]; }\n"
                            "long ptr(int n, int (*p)[n]) { long s = sizeof *p++;\n"
                            "  return s + (p == 0); }\n"
                            "long outer(int n) { int a[3][n]; return sizeof a; }\n");
  expect_lines(function(ir, "sz"),
               {"%1 = load i32, ptr %k.0, align 4", "%2 = add i32 %1, 1",
                "store i32 %2, ptr %k.0, align 4", "%3 = sext i32 %1 to i64",
                "store i64 %3, ptr %size.2, align 8", "%4 = load i64, ptr %size.2, align 8",
                "%5 = mul i64 %4, 4", "%6 = trunc i64 %5 to i32", "store i32 %6, ptr %x.1, align 4",
                "%8 = add i32 %7, 1", "store i64 %9, ptr %size.4, align 8", "%11 = mul i64 %10, 4",
                "%12 = mul i64 4, %11", "%13 = trunc i64 %12 to i32"});
  std::string noeval = function(ir, "noeval");
  expect_lines(noeval, {"%1 = trunc i64 4 to i32", "store i32 %1, ptr %z.1, align 4"});
  EXPECT_EQ(count(noeval, "store i32 %"), 2) << noeval;
  EXPECT_EQ(count(noeval, " add "), 1) << noeval;
  std::string object = function(ir, "object");
  EXPECT_EQ(count(object, "add i32"), 0) << object;
  expect_lines(object, {"%a.2 = alloca i8, i64 %4, align 4", "%6 = trunc i64 12 to i32"});
  expect_lines(function(ir, "ptr"),
               {"store i64 %3, ptr %size.2, align 8", "%4 = load ptr, ptr %p.1, align 8",
                "%7 = mul i64 1, %6", "%8 = getelementptr inbounds i8, ptr %4, i64 %7",
                "store ptr %8, ptr %p.1, align 8", "%9 = load i64, ptr %size.2, align 8",
                "%10 = mul i64 %9, 4", "%11 = trunc i64 %10 to i32"});
  expect_lines(function(ir, "outer"),
               {"%4 = mul i64 %3, 4", "%5 = mul i64 3, %4", "%a.2 = alloca i8, i64 %5, align 4",
                "%8 = mul i64 %7, 4", "%9 = mul i64 3, %8", "%10 = trunc i64 %9 to i32"});
}

TEST(CodegenVlaTest, StepsOverAVlaElementAreInBytes) {
  std::string body = function(
      generate("long step(int m, int (*p)[m], int (*q)[m]) { p = p + 1; return p - q; }\n"),
      "step");
  expect_lines(body, {"store i64 %4, ptr %size.3, align 8", "store i64 %6, ptr %size.4, align 8",
                      "%7 = load ptr, ptr %p.1, align 8", "%8 = sext i32 1 to i64",
                      "%9 = load i64, ptr %size.3, align 8", "%10 = mul i64 %9, 4",
                      "%11 = mul i64 %8, %10", "%12 = getelementptr inbounds i8, ptr %7, i64 %11",
                      "store ptr %12, ptr %p.1, align 8", "%17 = sub i64 %15, %16",
                      "%18 = load i64, ptr %size.3, align 8", "%19 = mul i64 %18, 4",
                      "%20 = sdiv exact i64 %17, %19"});
}

TEST(CodegenVlaTest, SizesAreEvaluatedWhereTheTypeIsWritten) {
  std::string ir = generate("int frozen(int m) { typedef int row[m]; m = 100; row r;\n"
                            "  return sizeof r; }\n"
                            "int params(int n, int m, int a[n][m]) { return a[1][2]; }\n"
                            "void cast(int n, void *p) { (void)(int (*)[n++])p; }\n");
  expect_lines(function(ir, "frozen"),
               {"%2 = sext i32 %1 to i64", "store i64 %2, ptr %size.1, align 8",
                "store i32 100, ptr %m.0, align 4", "%3 = load i64, ptr %size.1, align 8",
                "%4 = mul i64 %3, 4", "%5 = call ptr @llvm.stacksave.p0()",
                "%r.2 = alloca i8, i64 %4, align 4", "%6 = load i64, ptr %size.1, align 8"});
  EXPECT_EQ(count(function(ir, "frozen"), "store i64"), 1);
  expect_lines(function(ir, "params"),
               {"store ptr %2, ptr %a.2, align 8", "%3 = load i32, ptr %n.0, align 4",
                "store i64 %4, ptr %size.3, align 8", "%5 = load i32, ptr %m.1, align 4",
                "store i64 %6, ptr %size.4, align 8", "%9 = load i64, ptr %size.4, align 8",
                "%10 = mul i64 %9, 4", "%12 = getelementptr inbounds i8, ptr %7, i64 %11",
                "%14 = getelementptr inbounds i32, ptr %12, i64 %13"});
  expect_lines(function(ir, "cast"),
               {"%2 = load i32, ptr %n.0, align 4", "%3 = add i32 %2, 1",
                "store i32 %3, ptr %n.0, align 4", "store i64 %4, ptr %size.2, align 8"});
}

static const char *vla_paths =
    "void use(char *);\n"
    "void block(int n) { { char a[n]; use(a); } use(0); }\n"
    "void brk(int n) { while (n) { char a[n]; if (n > 5) break; use(a); } }\n"
    "void cont(int n) { while (n--) { char a[n];\n"
    "  switch (n) { case 1: { char b[n]; use(b); continue; } } use(a); } }\n"
    "void jump(int n) { char a[n]; { char b[n]; use(b); goto out; } out: use(a); }\n"
    "void fors(int n) { for (char a[n]; n; n--) use(a); use(0); }\n"
    "void back(int n) { again: { char a[n]; use(a); if (n--) goto again; } }\n"
    "void inner(int n) { char a[n]; again: { char b[n]; use(b); if (n--) goto again; } use(a); }\n"
    "int early(int n) { char a[n]; use(a); return 0; }\n"
    "void deep(int n) { char a[n]; while (n--) { char b[n]; if (n == 3) break;\n"
    "  if (n == 5) continue; use(b); } use(a); }\n"
    "void fl(int n) { for (char a[n]; n; n--) use(a); x: { char b[n]; if (n++ < 3) goto x; } }\n";

static std::vector<std::string> stack_lines(const std::string &body) {
  std::vector<std::string> out;
  std::istringstream in(body);
  std::string line;
  while (std::getline(in, line)) {
    if (line.find("stack") != std::string::npos || line.back() == ':' || line.find("  br ") == 0 ||
        line.find("  ret") == 0)
      out.push_back(line);
  }
  return out;
}

TEST(CodegenVlaTest, LeavingAScopeGivesItsStackBack) {
  std::string ir = generate(vla_paths);
  EXPECT_EQ(stack_lines(function(ir, "block")),
            (std::vector<std::string>{"entry:", "  %5 = call ptr @llvm.stacksave.p0()",
                                      "  call void @llvm.stackrestore.p0(ptr %5)", "  ret void"}));
  EXPECT_EQ(stack_lines(function(ir, "fors")),
            (std::vector<std::string>{
                "entry:", "  %5 = call ptr @llvm.stacksave.p0()", "  br label %for.cond.3",
                "for.cond.3:", "  br i1 %7, label %for.body.4, label %for.end.6",
                "for.body.4:", "  br label %for.inc.5", "for.inc.5:", "  br label %for.cond.3",
                "for.end.6:", "  call void @llvm.stackrestore.p0(ptr %5)", "  ret void"}));
  EXPECT_EQ(
      stack_lines(function(ir, "early")),
      (std::vector<std::string>{"entry:", "  %5 = call ptr @llvm.stacksave.p0()", "  ret i32 0"}));
}

TEST(CodegenVlaTest, BreakAndContinueGiveBackWhatTheyLeave) {
  std::string ir = generate(vla_paths);
  EXPECT_EQ(stack_lines(function(ir, "brk")),
            (std::vector<std::string>{
                "entry:", "  br label %while.cond.1",
                "while.cond.1:", "  br i1 %2, label %while.body.2, label %while.end.3",
                "while.body.2:", "  %7 = call ptr @llvm.stacksave.p0()",
                "  br i1 %11, label %if.then.6, label %if.end.7",
                "if.then.6:", "  call void @llvm.stackrestore.p0(ptr %7)",
                "  br label %while.end.3", "if.end.7:", "  call void @llvm.stackrestore.p0(ptr %7)",
                "  br label %while.cond.1", "while.end.3:", "  ret void"}));
  EXPECT_EQ(stack_lines(function(ir, "cont")),
            (std::vector<std::string>{
                "entry:", "  br label %while.cond.1",
                "while.cond.1:", "  br i1 %3, label %while.body.2, label %while.end.3",
                "while.body.2:", "  %8 = call ptr @llvm.stacksave.p0()",
                "switch.case.6:", "  %14 = call ptr @llvm.stacksave.p0()",
                "  call void @llvm.stackrestore.p0(ptr %8)", "  br label %while.cond.1",
                "switch.end.7:", "  call void @llvm.stackrestore.p0(ptr %8)",
                "  br label %while.cond.1", "while.end.3:", "  ret void"}));
  EXPECT_EQ(stack_lines(function(ir, "deep")),
            (std::vector<std::string>{"entry:",
                                      "  %5 = call ptr @llvm.stacksave.p0()",
                                      "  br label %while.cond.3",
                                      "while.cond.3:",
                                      "  br i1 %8, label %while.body.4, label %while.end.5",
                                      "while.body.4:",
                                      "  %13 = call ptr @llvm.stacksave.p0()",
                                      "  br i1 %17, label %if.then.8, label %if.end.9",
                                      "if.then.8:",
                                      "  call void @llvm.stackrestore.p0(ptr %13)",
                                      "  br label %while.end.5",
                                      "if.end.9:",
                                      "  br i1 %21, label %if.then.10, label %if.end.11",
                                      "if.then.10:",
                                      "  call void @llvm.stackrestore.p0(ptr %13)",
                                      "  br label %while.cond.3",
                                      "if.end.11:",
                                      "  call void @llvm.stackrestore.p0(ptr %13)",
                                      "  br label %while.cond.3",
                                      "while.end.5:",
                                      "  call void @llvm.stackrestore.p0(ptr %5)",
                                      "  ret void"}));
}

TEST(CodegenVlaTest, GotoGivesBackOnlyTheScopesItLeaves) {
  std::string ir = generate(vla_paths);
  EXPECT_EQ(
      stack_lines(function(ir, "jump")),
      (std::vector<std::string>{
          "entry:", "  %5 = call ptr @llvm.stacksave.p0()", "  %10 = call ptr @llvm.stacksave.p0()",
          "  call void @llvm.stackrestore.p0(ptr %10)", "  br label %label.5",
          "label.5:", "  call void @llvm.stackrestore.p0(ptr %5)", "  ret void"}));
  EXPECT_EQ(stack_lines(function(ir, "back")),
            (std::vector<std::string>{
                "entry:", "  br label %label.1", "label.1:", "  %5 = call ptr @llvm.stacksave.p0()",
                "  br i1 %8, label %if.then.4, label %if.end.5",
                "if.then.4:", "  call void @llvm.stackrestore.p0(ptr %5)", "  br label %label.1",
                "if.end.5:", "  call void @llvm.stackrestore.p0(ptr %5)", "  ret void"}));
  EXPECT_EQ(stack_lines(function(ir, "inner")),
            (std::vector<std::string>{
                "entry:", "  %5 = call ptr @llvm.stacksave.p0()", "  br label %label.3",
                "label.3:", "  %10 = call ptr @llvm.stacksave.p0()",
                "  br i1 %13, label %if.then.6, label %if.end.7",
                "if.then.6:", "  call void @llvm.stackrestore.p0(ptr %10)", "  br label %label.3",
                "if.end.7:", "  call void @llvm.stackrestore.p0(ptr %10)",
                "  call void @llvm.stackrestore.p0(ptr %5)", "  ret void"}));
  EXPECT_EQ(stack_lines(function(ir, "fl")),
            (std::vector<std::string>{"entry:",
                                      "  %5 = call ptr @llvm.stacksave.p0()",
                                      "  br label %for.cond.3",
                                      "for.cond.3:",
                                      "  br i1 %7, label %for.body.4, label %for.end.6",
                                      "for.body.4:",
                                      "  br label %for.inc.5",
                                      "for.inc.5:",
                                      "  br label %for.cond.3",
                                      "for.end.6:",
                                      "  call void @llvm.stackrestore.p0(ptr %5)",
                                      "  br label %label.7",
                                      "label.7:",
                                      "  %14 = call ptr @llvm.stacksave.p0()",
                                      "  br i1 %19, label %if.then.10, label %if.end.11",
                                      "if.then.10:",
                                      "  call void @llvm.stackrestore.p0(ptr %14)",
                                      "  br label %label.7",
                                      "if.end.11:",
                                      "  call void @llvm.stackrestore.p0(ptr %14)",
                                      "  ret void"}));
}

TEST(CodegenComplexTest, ComplexTypesArePairsAndImaginaryConstantsHaveNoRealPart) {
  std::string body =
      function(generate("#include <imaginary.h>\n"
                        "void slots(void) { float _Complex f; double _Complex d;\n"
                        "  long double _Complex l; f = IMAG_F; d = IMAG_D; l = IMAG_L; }\n",
                        TARGET_WINDOWS_X64, true),
               "slots");
  expect_lines(
      body, {"%f.0 = alloca { float, float }, align 4", "%d.1 = alloca { double, double }, align 8",
             "%l.2 = alloca { x86_fp80, x86_fp80 }, align 16",
             "store { float, float } { float 0x0000000000000000, float 0x3FF0000000000000 }, "
             "ptr %f.0, align 4",
             "store { double, double } { double 0x0000000000000000, double "
             "0x4000000000000000 }, ptr %d.1, align 8",
             "store { x86_fp80, x86_fp80 } { x86_fp80 0xK00000000000000000000, x86_fp80 "
             "0xK4000C000000000000000 }, ptr %l.2, align 16"});
}

TEST(CodegenComplexTest, AdditionAndNegationWorkPartByPart) {
  std::string ir = generate("void add(double _Complex *a, double _Complex *b) { *a = *a + *b; }\n"
                            "void sub(float _Complex *a) { *a = *a - *a; }\n"
                            "void neg(float _Complex *f) { *f = -*f; }\n");
  expect_lines(function(ir, "add"), {"%3 = load { double, double }, ptr %2, align 8",
                                     "%5 = load { double, double }, ptr %4, align 8",
                                     "%6 = extractvalue { double, double } %3, 0",
                                     "%7 = extractvalue { double, double } %3, 1",
                                     "%8 = extractvalue { double, double } %5, 0",
                                     "%9 = extractvalue { double, double } %5, 1",
                                     "%10 = fadd double %6, %8", "%11 = fadd double %7, %9",
                                     "%12 = insertvalue { double, double } poison, double %10, 0",
                                     "%13 = insertvalue { double, double } %12, double %11, 1",
                                     "store { double, double } %13, ptr %14, align 8"});
  expect_lines(function(ir, "sub"), {"%9 = fsub float %5, %7", "%10 = fsub float %6, %8"});
  expect_lines(function(ir, "neg"),
               {"%3 = extractvalue { float, float } %2, 0",
                "%4 = extractvalue { float, float } %2, 1", "%5 = fneg float %3",
                "%6 = fneg float %4", "%7 = insertvalue { float, float } poison, float %5, 0",
                "%8 = insertvalue { float, float } %7, float %6, 1"});
}

static const char *complex_helpers =
    "void mul(float _Complex *f, double _Complex *d, long double _Complex *l) {\n"
    "  *f = *f * *f; *d = *d * *d; *l = *l * *l; *f = *f / *f; *d /= *d; *l = *l / *l; }\n"
    "void twice(double _Complex *d) { *d = *d * *d; *d = *d * *d; }\n";

TEST(CodegenComplexTest, MultiplicationAndDivisionCallLibgccOnWindows) {
  std::string ir = generate(complex_helpers);
  expect_lines(
      function(ir, "mul"),
      {"%complex.3 = alloca { double, double }, align 8",
       "%complex.4 = alloca { x86_fp80, x86_fp80 }, align 16",
       "%complex.5 = alloca x86_fp80, align 16", "%complex.8 = alloca x86_fp80, align 16",
       "%11 = call i64 @__mulsc3(float %7, float %8, float %9, float %10)",
       "%12 = bitcast i64 %11 to <2 x float>", "%13 = extractelement <2 x float> %12, i32 0",
       "%14 = extractelement <2 x float> %12, i32 1",
       "%15 = insertvalue { float, float } poison, float %13, 0",
       "call void @__muldc3(ptr sret({ double, double }) %complex.3, double %22, double %23, "
       "double %24, double %25)",
       "%26 = load { double, double }, ptr %complex.3, align 8",
       "store x86_fp80 %32, ptr %complex.5, align 16",
       "store x86_fp80 %35, ptr %complex.8, align 16",
       "call void @__mulxc3(ptr sret({ x86_fp80, x86_fp80 }) %complex.4, ptr %complex.5, "
       "ptr %complex.6, ptr %complex.7, ptr %complex.8)",
       "%36 = load { x86_fp80, x86_fp80 }, ptr %complex.4, align 16",
       "%46 = call i64 @__divsc3(float %42, float %43, float %44, float %45)",
       "call void @__divdc3(ptr sret({ double, double }) %complex.9, double %57, double "
       "%58, double %59, double %60)",
       "call void @__divxc3(ptr sret({ x86_fp80, x86_fp80 }) %complex.10, ptr %complex.11, "
       "ptr %complex.12, ptr %complex.13, ptr %complex.14)"});
  for (const char *line : {"declare i64 @__mulsc3(float, float, float, float)",
                           "declare void @__muldc3(ptr, double, double, double, double)",
                           "declare void @__mulxc3(ptr, ptr, ptr, ptr, ptr)",
                           "declare i64 @__divsc3(float, float, float, float)",
                           "declare void @__divdc3(ptr, double, double, double, double)",
                           "declare void @__divxc3(ptr, ptr, ptr, ptr, ptr)"})
    EXPECT_EQ(count(ir, std::string("\n") + line + "\n"), 1) << line << "\n" << ir;
  expect_lines(function(ir, "twice"), {"%complex.1 = alloca { double, double }, align 8",
                                       "%complex.2 = alloca { double, double }, align 8"});
}

TEST(CodegenComplexTest, MultiplicationAndDivisionCallLibgccOnLinux) {
  std::string ir = generate(complex_helpers, TARGET_LINUX_X64);
  std::string mul = function(ir, "mul");
  expect_lines(mul, {"%11 = call <2 x float> @__mulsc3(float %7, float %8, float %9, float %10)",
                     "%12 = extractelement <2 x float> %11, i32 0",
                     "%13 = extractelement <2 x float> %11, i32 1",
                     "%25 = call { double, double } @__muldc3(double %21, double %22, double %23, "
                     "double %24)",
                     "%35 = call { x86_fp80, x86_fp80 } @__mulxc3(x86_fp80 %31, x86_fp80 %32, "
                     "x86_fp80 %33, x86_fp80 %34)",
                     "%45 = call <2 x float> @__divsc3(float %41, float %42, float %43, float %44)",
                     "%59 = call { double, double } @__divdc3(double %55, double %56, double %57, "
                     "double %58)",
                     "%68 = call { x86_fp80, x86_fp80 } @__divxc3(x86_fp80 %64, x86_fp80 %65, "
                     "x86_fp80 %66, x86_fp80 %67)"});
  EXPECT_EQ(count(mul, "alloca"), 3) << mul;
  EXPECT_EQ(count(mul, "bitcast"), 0) << mul;
  for (const char *line : {"declare <2 x float> @__mulsc3(float, float, float, float)",
                           "declare { double, double } @__muldc3(double, double, double, double)",
                           "declare { x86_fp80, x86_fp80 } @__mulxc3(x86_fp80, x86_fp80, "
                           "x86_fp80, x86_fp80)",
                           "declare <2 x float> @__divsc3(float, float, float, float)"})
    EXPECT_EQ(count(ir, std::string("\n") + line + "\n"), 1) << line << "\n" << ir;
}

TEST(CodegenComplexTest, ConversionsTakeOrSupplyTheImaginaryPart) {
  std::string body = function(
      generate("void conv(double _Complex *z, float *f, int *i, _Bool *b, long double _Complex *l) "
               "{ *f = *z; *i = *z; *b = *z; *l = *z; *z = *i; }\n"),
      "conv");
  expect_lines(
      body, {"%7 = extractvalue { double, double } %6, 0", "%8 = fptrunc double %7 to float",
             "%12 = extractvalue { double, double } %11, 0", "%13 = fptosi double %12 to i32",
             "%17 = extractvalue { double, double } %16, 0",
             "%18 = fcmp une double %17, 0x0000000000000000",
             "%19 = extractvalue { double, double } %16, 1",
             "%20 = fcmp une double %19, 0x0000000000000000", "%21 = or i1 %18, %20",
             "%22 = zext i1 %21 to i8", "%26 = extractvalue { double, double } %25, 0",
             "%27 = fpext double %26 to x86_fp80", "%28 = extractvalue { double, double } %25, 1",
             "%29 = fpext double %28 to x86_fp80",
             "%31 = insertvalue { x86_fp80, x86_fp80 } %30, x86_fp80 %29, 1",
             "%35 = sitofp i32 %34 to double",
             "%36 = insertvalue { double, double } poison, double %35, 0",
             "%37 = insertvalue { double, double } %36, double 0x0000000000000000, 1"});
}

TEST(CodegenComplexTest, EqualityComparesBothPartsAndAConditionTestsEither) {
  std::string ir = generate("int eq(double _Complex *a, double _Complex *b) {\n"
                            "  return (*a == *b) + (*a != *b); }\n"
                            "int cond(float _Complex *z) { if (*z) return 1; return 0; }\n");
  expect_lines(function(ir, "eq"), {"%10 = fcmp oeq double %6, %8", "%11 = fcmp oeq double %7, %9",
                                    "%12 = and i1 %10, %11", "%22 = fcmp une double %18, %20",
                                    "%23 = fcmp une double %19, %21", "%24 = or i1 %22, %23"});
  expect_lines(
      function(ir, "cond"),
      {"%3 = extractvalue { float, float } %2, 0", "%4 = fcmp une float %3, 0x0000000000000000",
       "%5 = extractvalue { float, float } %2, 1", "%6 = fcmp une float %5, 0x0000000000000000",
       "%7 = or i1 %4, %6", "br i1 %7, label %if.then.1, label %if.end.2"});
}

TEST(CodegenComplexTest, StaticComplexObjectsHoldBothParts) {
  std::string ir = generate("#include <imaginary.h>\n"
                            "double _Complex sd = 1.0 - IMAG_D;\n"
                            "float _Complex sf = 3.0f + IMAG_F4;\n"
                            "long double _Complex sl = 1.0L + IMAG_L1;\n",
                            TARGET_WINDOWS_X64, true);
  for (
      const char *line :
      {"@sd = global [16 x i8] c\"\\00\\00\\00\\00\\00\\00\\F0?\\00\\00\\00\\00\\00\\00\\00\\C0\", "
       "align 8",
       "@sf = global [8 x i8] c\"\\00\\00@@\\00\\00\\80@\", align 4",
       "@sl = global [32 x i8] c\"\\00\\00\\00\\00\\00\\00\\00\\80\\FF?\\00\\00\\00\\00\\00\\00\\00"
       "\\00\\00\\00\\00\\00\\00\\80\\FF?\\00\\00\\00\\00\\00\\00\", align 16"})
    EXPECT_TRUE(has_line(ir, line)) << line << "\nin:\n" << ir;
}

TEST(CodegenBuiltinTest, OffsetofAddsEachVariableIndex) {
  std::string ir = generate("struct r { int a; char b[10]; struct { int x[4]; } in[3]; };\n"
                            "long off(int i) { return __builtin_offsetof(struct r, in[i].x[2]) +\n"
                            "  __builtin_offsetof(struct r, b[3]); }\n"
                            "int compat(void) { return __builtin_types_compatible_p(int, const int)"
                            " + __builtin_types_compatible_p(int, long); }\n");
  expect_lines(function(ir, "off"), {"%2 = sext i32 %1 to i64", "%3 = mul i64 %2, 16",
                                     "%4 = add i64 24, %3", "%5 = add i64 %4, 7"});
  expect_lines(function(ir, "compat"), {"%0 = add i32 1, 0"});
}

TEST(CodegenBuiltinTest, ChooseExprIsItsChosenOperand) {
  std::string body = function(generate("int choose(void) { int x = 5; double y = 2.5;\n"
                                       "  __builtin_choose_expr(1, x, y) = 9;\n"
                                       "  return __builtin_choose_expr(0, x, y) > 1; }\n"),
                              "choose");
  expect_lines(body, {"store i32 5, ptr %x.0, align 4", "store i32 9, ptr %x.0, align 4",
                      "%0 = load double, ptr %y.1, align 8", "%2 = fcmp ogt double %0, %1"});
  expect_lines(
      function(generate("int rv(int x) { return __builtin_choose_expr(1, x + 1, 2.0); }\n"), "rv"),
      {"%1 = load i32, ptr %x.0, align 4", "%2 = add i32 %1, 1", "ret i32 %2"});
}

TEST(CodegenBuiltinTest, InfinityAndNanAreConstantsOfTheirType) {
  std::string body = function(generate("void inf(double *d, float *f, long double *l) {\n"
                                       "  *d = __builtin_huge_val(); *f = __builtin_inff();\n"
                                       "  *l = __builtin_huge_vall(); *f = __builtin_nanf(\"\");\n"
                                       "  *f = __builtin_huge_valf(); }\n"),
                              "inf");
  expect_lines(body, {"store double 0x7FF0000000000000, ptr %3, align 8",
                      "store float 0x7FF0000000000000, ptr %4, align 4",
                      "store x86_fp80 0xK7FFF8000000000000000, ptr %5, align 16",
                      "store float 0x7FF8000000000000, ptr %6, align 4",
                      "store float 0x7FF0000000000000, ptr %7, align 4"});
}

TEST(CodegenBuiltinTest, ClassificationComparisonsAreQuiet) {
  std::string body =
      function(generate("int order(double a, double b) { return __builtin_isgreater(a, b) +\n"
                        "  __builtin_isgreaterequal(a, b) + __builtin_isless(a, b) +\n"
                        "  __builtin_islessequal(a, b) + __builtin_islessgreater(a, b) +\n"
                        "  __builtin_isunordered(a, b); }\n"),
               "order");
  expect_lines(body, {"%4 = fcmp ogt double %2, %3", "%5 = zext i1 %4 to i32",
                      "%8 = fcmp oge double %6, %7", "%13 = fcmp olt double %11, %12",
                      "%18 = fcmp ole double %16, %17", "%23 = fcmp one double %21, %22",
                      "%28 = fcmp uno double %26, %27"});
}

TEST(CodegenBuiltinTest, SignbitAndLlabsMatchGcc) {
  std::string ir = generate("int sign(float f, double d, long double *l) {\n"
                            "  return __builtin_signbitf(f) + __builtin_signbit(d) +\n"
                            "  __builtin_signbitl(*l); }\n"
                            "long long ab(long long v) { return __builtin_llabs(v); }\n");
  expect_lines(function(ir, "sign"),
               {"%4 = bitcast float %3 to i32", "%5 = and i32 %4, -2147483648",
                "%7 = bitcast double %6 to i64", "%8 = lshr i64 %7, 63", "%9 = trunc i64 %8 to i32",
                "%13 = bitcast x86_fp80 %12 to i80", "%14 = lshr i80 %13, 79",
                "%15 = trunc i80 %14 to i32", "%16 = shl i32 %15, 9"});
  expect_lines(function(ir, "ab"), {"%2 = icmp slt i64 %1, 0", "%3 = sub i64 0, %1",
                                    "%4 = select i1 %2, i64 %3, i64 %1", "ret i64 %4"});
}

static const char *sync_source =
    "int add(int *p, int v) { return __sync_fetch_and_add(p, v); }\n"
    "unsigned char nand(unsigned char *p) { return __sync_nand_and_fetch(p, 3); }\n"
    "long long sub(long long *p) { return __sync_sub_and_fetch(p, 2); }\n"
    "_Bool cas(int *p, int o, int n) { return __sync_bool_compare_and_swap(p, o, n); }\n"
    "char *vcas(char **p, char *n) { return __sync_val_compare_and_swap(p, 0, n); }\n"
    "short tas(short *p) { return __sync_lock_test_and_set(p, 1); }\n"
    "void rel(char **p) { __sync_lock_release(p); }\n"
    "void rel2(int *p) { __sync_lock_release(p); }\n"
    "void fence(void) { __sync_synchronize(); __builtin_ia32_sfence(); }\n"
    "unsigned long long hi(unsigned long long a, unsigned long long b) { return __umulh(a, b); }\n"
    "long long shi(long long a, int b) { return __mulh(a, b); }\n";

TEST(CodegenBuiltinTest, SyncArithmeticIsAnAtomicReadModifyWrite) {
  std::string ir = generate(sync_source);
  expect_lines(function(ir, "add"), {"%4 = atomicrmw add ptr %2, i32 %3 seq_cst", "ret i32 %4"});
  expect_lines(function(ir, "nand"),
               {"%2 = trunc i32 3 to i8", "%3 = atomicrmw nand ptr %1, i8 %2 seq_cst",
                "%4 = and i8 %3, %2", "%5 = xor i8 %4, -1", "ret i8 %5"});
  expect_lines(function(ir, "sub"),
               {"%3 = atomicrmw sub ptr %1, i64 %2 seq_cst", "%4 = sub i64 %3, %2", "ret i64 %4"});
}

TEST(CodegenBuiltinTest, SyncSwapsLocksAndFences) {
  std::string ir = generate(sync_source);
  expect_lines(function(ir, "cas"),
               {"%6 = cmpxchg ptr %3, i32 %4, i32 %5 seq_cst seq_cst",
                "%7 = extractvalue { i32, i1 } %6, 1", "%8 = zext i1 %7 to i8", "ret i8 %8"});
  expect_lines(function(ir, "vcas"), {"%4 = cmpxchg ptr %2, ptr null, ptr %3 seq_cst seq_cst",
                                      "%5 = extractvalue { ptr, i1 } %4, 0", "ret ptr %5"});
  expect_lines(function(ir, "tas"), {"%3 = atomicrmw xchg ptr %1, i16 %2 acquire", "ret i16 %3"});
  expect_lines(function(ir, "rel"), {"store atomic ptr null, ptr %1 release, align 8"});
  expect_lines(function(ir, "rel2"), {"store atomic i32 0, ptr %1 release, align 4"});
  expect_lines(function(ir, "fence"),
               {"fence seq_cst", "call void @llvm.x86.sse.sfence()", "ret void"});
  EXPECT_EQ(count(ir, "declare void @llvm.x86.sse.sfence()\n"), 1) << ir;
  EXPECT_EQ(count(generate("int f(void) { return 0; }"), "sfence"), 0);
}

TEST(CodegenBuiltinTest, MultiplyHighKeepsTheUpperHalf) {
  std::string ir = generate(sync_source);
  expect_lines(function(ir, "hi"),
               {"%3 = zext i64 %2 to i128", "%5 = zext i64 %4 to i128", "%6 = mul i128 %3, %5",
                "%7 = lshr i128 %6, 64", "%8 = trunc i128 %7 to i64", "ret i64 %8"});
  expect_lines(function(ir, "shi"),
               {"%3 = sext i64 %2 to i128", "%5 = sext i32 %4 to i64", "%6 = sext i64 %5 to i128",
                "%7 = mul i128 %3, %6", "%8 = lshr i128 %7, 64", "%9 = trunc i128 %8 to i64"});
}

TEST(CodegenBuiltinTest, TrapAndUnreachableEndTheirBlock) {
  std::string ir = generate("int trap(int i) { if (i) __builtin_trap(); return 1; }\n"
                            "int unr(int i) { if (i) __builtin_unreachable(); return 2; }\n");
  expect_lines(function(ir, "trap"),
               {"if.then.1:", "call void @llvm.trap()", "unreachable", "if.end.2:"});
  std::string unr = function(ir, "unr");
  expect_lines(unr, {"if.then.1:", "unreachable", "if.end.2:"});
  EXPECT_EQ(count(unr, "trap"), 0) << unr;
  EXPECT_EQ(count(ir, "declare void @llvm.trap()\n"), 1) << ir;
  EXPECT_EQ(count(generate("int f(void) { return 0; }"), "trap"), 0);
}

TEST(CodegenBuiltinTest, TgmathCallsTheChosenFunction) {
  std::string ir = generate("#include <tgmath.h>\n"
                            "float f(float x) { return sqrt(x); }\n"
                            "double g(int x) { return sqrt(x); }\n",
                            TARGET_WINDOWS_X64, true);
  expect_lines(function(ir, "f"), {"%2 = call float (float) @sqrtf(float %1)"});
  expect_lines(function(ir, "g"), {"%3 = call double (double) @sqrt(double %2)"});
  EXPECT_TRUE(has_line(ir, "declare float @sqrtf(float)")) << ir;
}

static const char *asm_source =
    "int one(int x) { int r; __asm__(\"movl %1, %0\\n\\taddl $1, %0\" : \"=r\"(r) : \"r\"(x));\n"
    "  return r; }\n"
    "int two(int x) { int lo, hi; __asm__(\"movl %2, %0\\n\\tmovl %3, %1\" : \"=&r\"(lo),\n"
    "  \"=r\"(hi) : \"g\"(x), \"0\"(5)); return lo + hi; }\n"
    "int tied(int y) { __asm__ volatile(\"addl $5, %0\" : \"+r\"(y)); return y; }\n"
    "long mem(long q) { __asm__(\"incq %q0 # 100%% %=\" : \"+m\"(q) : \"i\"(7)); return q; }\n"
    "void basic(void) { __asm__(\"movl %eax, %eax\"); }\n"
    "unsigned regs(unsigned a, unsigned d) { __asm__(\"xchgl %0, %1\" : \"+a\"(a), \"+d\"(d)\n"
    "  : \"S\"(1), \"D\"(2), \"b\"(3), \"c\"(4) : \"%edx\", \"memory\", \"cc\"); return a; }\n"
    "void memory(int *p) { __asm__(\"movl $7, %0\" : \"=m\"(*p) : \"m\"(*p)); }\n";

TEST(CodegenAsmTest, RegisterOutputsAreTheCallsResult) {
  std::string ir = generate(asm_source);
  expect_lines(function(ir, "one"), {"%2 = call i32 asm \"movl $1, $0\\0A\\09addl $$1, $0\", "
                                     "\"=r,r,~{dirflag},~{fpsr},~{flags}\"(i32 %1)",
                                     "store i32 %2, ptr %r.1, align 4"});
  expect_lines(function(ir, "two"),
               {"%2 = call { i32, i32 } asm \"movl $2, $0\\0A\\09movl $3, $1\", "
                "\"=&r,=r,imr,0,~{dirflag},~{fpsr},~{flags}\"(i32 %1, i32 5)",
                "%3 = extractvalue { i32, i32 } %2, 0", "store i32 %3, ptr %lo.1, align 4",
                "%4 = extractvalue { i32, i32 } %2, 1", "store i32 %4, ptr %hi.2, align 4"});
}

TEST(CodegenAsmTest, ReadWriteOperandsAreTiedAfterTheInputs) {
  std::string ir = generate(asm_source);
  expect_lines(function(ir, "tied"), {"%1 = load i32, ptr %y.0, align 4",
                                      "%2 = call i32 asm sideeffect \"addl $$5, $0\", "
                                      "\"=r,0,~{dirflag},~{fpsr},~{flags}\"(i32 %1)",
                                      "store i32 %2, ptr %y.0, align 4"});
  expect_lines(function(ir, "mem"),
               {"call void asm \"incq ${0:q} # 100% ${:uid}\", "
                "\"=*m,i,*m,~{dirflag},~{fpsr},~{flags}\"(ptr elementtype(i32) %q.0, i32 7, "
                "ptr elementtype(i32) %q.0)"});
  expect_lines(function(ir, "memory"),
               {"call void asm \"movl $$7, $0\", \"=*m,*m,~{dirflag},~{fpsr},~{flags}\"(ptr "
                "elementtype(i32) %1, ptr elementtype(i32) %2)"});
}

TEST(CodegenAsmTest, BasicAsmKeepsItsPercentSigns) {
  expect_lines(function(generate(asm_source), "basic"),
               {"call void asm sideeffect \"movl %eax, %eax\", "
                "\"~{dirflag},~{fpsr},~{flags}\"()"});
  expect_lines(function(generate("void percent(void) { __asm__(\"# 50%% %0 %=\"); }\n"), "percent"),
               {"call void asm sideeffect \"# 50%% %0 %=\", \"~{dirflag},~{fpsr},~{flags}\"()"});
}

TEST(CodegenAsmTest, NamedOperandsAreNumbered) {
  expect_lines(function(generate("int named(int a, int b) {\n"
                                 "  __asm__(\"addl %[b], %[a]\" : [a] \"+r\"(a) : [b] \"r\"(b));\n"
                                 "  return a; }\n"),
                        "named"),
               {"%4 = call i32 asm \"addl $1, $0\", \"=r,r,0,~{dirflag},~{fpsr},~{flags}\"(i32 %3, "
                "i32 %2)"});
}

TEST(CodegenAsmTest, RegisterLettersAndClobbersAreTranslated) {
  expect_lines(function(generate(asm_source), "regs"),
               {"%4 = call { i32, i32 } asm \"xchgl $0, $1\", "
                "\"={ax},={dx},{si},{di},{bx},{cx},0,1,~{edx},~{memory},~{cc},~{dirflag},~{fpsr},"
                "~{flags}\"(i32 1, i32 2, i32 3, i32 4, i32 %2, i32 %3)",
                "%5 = extractvalue { i32, i32 } %4, 0", "%6 = extractvalue { i32, i32 } %4, 1"});
}

TEST(CodegenSetjmpTest, ReturnsTwiceMarksTheCallAndTheDeclaration) {
  const char *source = "#include <setjmp.h>\n"
                       "static jmp_buf e;\n"
                       "int f(void) { if (setjmp(e)) return 1; return 0; }\n";
  std::string windows_ir = generate(source, TARGET_WINDOWS_X64, true);
  expect_lines(function(windows_ir, "f"), {"%0 = call ptr () @mingw_getsp()",
                                           "%1 = call i32 (ptr, ptr) @_setjmp(ptr @e, ptr %0) #0"});
  EXPECT_TRUE(has_line(windows_ir, "declare i32 @_setjmp(ptr, ptr) #0")) << windows_ir;
  EXPECT_TRUE(has_line(windows_ir, "declare ptr @mingw_getsp()")) << windows_ir;
  EXPECT_EQ(count(windows_ir, "attributes #0 = { returns_twice }\n"), 1) << windows_ir;
  std::string linux_ir = generate(source, TARGET_LINUX_X64, true);
  expect_lines(function(linux_ir, "f"), {"%0 = call i32 (ptr) @_setjmp(ptr @e) #0"});
  EXPECT_TRUE(has_line(linux_ir, "declare i32 @_setjmp(ptr) #0")) << linux_ir;
  std::string referred = generate("int setjmp(void *);\nvoid *p = (void *)&setjmp;\n");
  EXPECT_TRUE(has_line(referred, "declare i32 @setjmp(ptr) #0")) << referred;
  EXPECT_TRUE(has_line(referred, "attributes #0 = { returns_twice }")) << referred;
  EXPECT_EQ(count(generate("int f(void) { return 0; }"), "attributes"), 0);
}

TEST(CodegenPragmaTest, FpContractNeverFusesAMultiplyAndAnAdd) {
  std::string body =
      function(generate("#pragma STDC FP_CONTRACT ON\n"
                        "double f(double a, double b, double c) { return a * b + c; }\n"),
               "f");
  expect_lines(body, {"%5 = fmul double %3, %4", "%7 = fadd double %5, %6"});
  EXPECT_EQ(count(body, "fma"), 0) << body;
  EXPECT_EQ(count(body, "contract"), 0) << body;
}

static std::string first_line(const std::string &text) {
  return text.substr(0, text.find('\n'));
}

static const char *windows_abi_source =
    "struct s4 { int a; };\n"
    "struct s12 { int a, b, c; };\n"
    "struct s4 small(struct s4 x) { x.a++; return x; }\n"
    "struct s12 big(struct s12 x) { x.c++; return x; }\n"
    "long double ld(long double x) { return x; }\n"
    "float _Complex cf(float _Complex x) { return x; }\n"
    "double _Complex cd(double _Complex x) { return x; }\n"
    "_Bool b(_Bool x, char c) { return x && c; }\n"
    "int use(struct s4 *p, struct s12 *q) { struct s4 r = small(*p); struct s12 s = big(*q);\n"
    "  return r.a + s.c + big(*q).b; }\n"
    "long double callld(long double x) { return ld(x); }\n";

TEST(CodegenAbiWindowsTest, SmallRecordsAreIntegersAndOthersGoByPointer) {
  std::string ir = generate(windows_abi_source);
  std::string small = function(ir, "small");
  EXPECT_EQ(first_line(small), "define i32 @small(i32 %0) {");
  expect_lines(
      small, {"store i32 %0, ptr %x.0, align 4", "%3 = load i32, ptr %x.0, align 4", "ret i32 %3"});
  std::string big = function(ir, "big");
  EXPECT_EQ(first_line(big), "define void @big(ptr sret([12 x i8]) align 4 %0, ptr %1) {");
  expect_lines(
      big, {"call void @llvm.memcpy.p0.p0.i64(ptr align 4 %x.0, ptr align 4 %1, i64 12, i1 false)",
            "call void @llvm.memcpy.p0.p0.i64(ptr align 4 %0, ptr align 4 %x.0, i64 12, i1 false)",
            "ret void"});
  expect_lines(
      function(ir, "use"),
      {"%3 = load i32, ptr %2, align 4", "%4 = call i32 (i32) @small(i32 %3)",
       "store i32 %4, ptr %tmp.3, align 4",
       "call void @llvm.memcpy.p0.p0.i64(ptr align 4 %tmp.6, ptr align 4 %5, i64 12, i1 false)",
       "call void (ptr, ptr) @big(ptr sret([12 x i8]) align 4 %tmp.5, ptr %tmp.6)",
       "call void @llvm.memcpy.p0.p0.i64(ptr align 4 %s.4, ptr align 4 %tmp.5, i64 12, i1 false)",
       "call void (ptr, ptr) @big(ptr sret([12 x i8]) align 4 %tmp.7, ptr %tmp.8)",
       "%11 = getelementptr inbounds i8, ptr %tmp.7, i64 4"});
}

TEST(CodegenAbiWindowsTest, LongDoubleAndComplexFollowMinGW) {
  std::string ir = generate(windows_abi_source);
  std::string ld = function(ir, "ld");
  EXPECT_EQ(first_line(ld), "define void @ld(ptr sret(x86_fp80) align 16 %0, ptr %1) {");
  expect_lines(ld, {"%2 = load x86_fp80, ptr %1, align 16", "store x86_fp80 %3, ptr %0, align 16",
                    "ret void"});
  std::string cf = function(ir, "cf");
  EXPECT_EQ(first_line(cf), "define i64 @cf(i64 %0) {");
  expect_lines(cf,
               {"store i64 %0, ptr %x.0, align 4", "store { float, float } %1, ptr %tmp.1, align 4",
                "%2 = load i64, ptr %tmp.1, align 4", "ret i64 %2"});
  EXPECT_EQ(first_line(function(ir, "cd")),
            "define void @cd(ptr sret({ double, double }) align 8 %0, ptr %1) {");
  expect_lines(function(ir, "callld"),
               {"store x86_fp80 %3, ptr %tmp.2, align 16",
                "call void (ptr, ptr) @ld(ptr sret(x86_fp80) align 16 %tmp.1, ptr %tmp.2)",
                "%4 = load x86_fp80, ptr %tmp.1, align 16"});
  EXPECT_EQ(first_line(function(ir, "b")), "define zeroext i8 @b(i8 zeroext %0, i8 %1) {");
}

static const char *linux_abi_source =
    "struct s3 { char a[3]; };\n"
    "struct s12 { int a, b, c; };\n"
    "struct s16 { long long a, b; };\n"
    "struct s24 { long long a, b, c; };\n"
    "struct f2 { float a, b; };\n"
    "struct mix { int a; double b; };\n"
    "struct mix2 { double a; int b; };\n"
    "struct ld1 { long double a; };\n"
    "union u3 { float f[3]; int i; };\n"
    "#pragma pack(1)\n"
    "struct packed { char c; int i; };\n"
    "struct packed2 { char c; int i; char d[11]; };\n"
    "#pragma pack()\n"
    "struct s3 r3(struct s3 x) { return x; }\n"
    "struct s12 r12(struct s12 x) { return x; }\n"
    "struct s24 r24(struct s24 x) { return x; }\n"
    "struct f2 rf2(struct f2 x) { return x; }\n"
    "struct mix rmix(struct mix x) { return x; }\n"
    "struct mix2 rmix2(struct mix2 x) { return x; }\n"
    "struct ld1 rld(struct ld1 x) { return x; }\n"
    "union u3 ru3(union u3 x) { return x; }\n"
    "struct packed rp(struct packed x) { return x; }\n"
    "struct packed2 rp2(struct packed2 x) { return x; }\n"
    "long double _Complex rcl(long double _Complex x) { return x; }\n"
    "char c(char x, unsigned char u, short s, unsigned short us, _Bool b) {\n"
    "  return x + u + s + us + b; }\n"
    "int spill(int a, int b, int c, int d, int e, struct s16 f, int g) { return a + (int)f.b + g; "
    "}\n"
    "int callspill(struct s16 *p) { return spill(1, 2, 3, 4, 5, *p, 6); }\n"
    "double use(struct s12 *p, struct mix *m) { extern double take(struct mix, struct s12);\n"
    "  return r12(*p).c + take(*m, *p); }\n";

TEST(CodegenAbiLinuxTest, RecordsAreClassifiedIntoEightbytes) {
  std::string ir = generate(linux_abi_source, TARGET_LINUX_X64);
  const char *const lines[][2] = {
      {"r3", "define i24 @r3(i24 %0) {"},
      {"r12", "define { i64, i32 } @r12(i64 %0, i32 %1) {"},
      {"r24",
       "define void @r24(ptr sret([24 x i8]) align 8 %0, ptr byval([24 x i8]) align 8 %1) {"},
      {"rf2", "define double @rf2(double %0) {"},
      {"rmix", "define { i64, double } @rmix(i64 %0, double %1) {"},
      {"rmix2", "define { double, i64 } @rmix2(double %0, i64 %1) {"},
      {"rld", "define x86_fp80 @rld(ptr byval([16 x i8]) align 16 %0) {"},
      {"ru3", "define { i64, float } @ru3(i64 %0, float %1) {"},
      {"rp", "define void @rp(ptr sret([5 x i8]) align 1 %0, ptr byval([5 x i8]) align 8 %1) {"},
      {"rp2",
       "define void @rp2(ptr sret([16 x i8]) align 1 %0, ptr byval([16 x i8]) align 8 %1) {"},
      {"rcl",
       "define { x86_fp80, x86_fp80 } @rcl(ptr byval({ x86_fp80, x86_fp80 }) align 16 %0) {"},
      {"c", "define signext i8 @c(i8 signext %0, i8 zeroext %1, i16 signext %2, i16 zeroext %3, "
            "i8 zeroext %4) {"}};
  for (const auto &line : lines)
    EXPECT_EQ(first_line(function(ir, line[0])), line[1]) << line[0];
}

TEST(CodegenAbiLinuxTest, PiecesAreStoredAndRebuiltThroughMemory) {
  std::string ir = generate(linux_abi_source, TARGET_LINUX_X64);
  expect_lines(function(ir, "r12"),
               {"store i64 %0, ptr %x.0, align 4",
                "%2 = getelementptr inbounds i8, ptr %x.0, i64 8", "store i32 %1, ptr %2, align 4",
                "%3 = load i64, ptr %x.0, align 4", "%5 = load i32, ptr %4, align 4",
                "%6 = insertvalue { i64, i32 } poison, i64 %3, 0",
                "%7 = insertvalue { i64, i32 } %6, i32 %5, 1", "ret { i64, i32 } %7"});
  expect_lines(function(ir, "use"),
               {"%6 = call { i64, i32 } (i64, i32) @r12(i64 %3, i32 %5)",
                "%7 = extractvalue { i64, i32 } %6, 0", "store i64 %7, ptr %tmp.2, align 4",
                "%8 = extractvalue { i64, i32 } %6, 1", "store i32 %8, ptr %9, align 4",
                "%14 = load i64, ptr %13, align 8", "%16 = load double, ptr %15, align 8",
                "%21 = call double (i64, double, i64, i32) @take(i64 %14, double %16, i64 %18, "
                "i32 %20)"});
  EXPECT_TRUE(has_line(ir, "declare double @take(i64, double, i64, i32)")) << ir;
}

TEST(CodegenAbiLinuxTest, ARecordThatDoesNotFitTheRegistersLeftGoesInMemory) {
  std::string ir = generate(linux_abi_source, TARGET_LINUX_X64);
  EXPECT_EQ(first_line(function(ir, "spill")),
            "define i32 @spill(i32 %0, i32 %1, i32 %2, i32 %3, i32 %4, ptr byval([16 x i8]) align "
            "8 %5, i32 %6) {");
  expect_lines(
      function(ir, "callspill"),
      {"%2 = call i32 (i32, i32, i32, i32, i32, ptr, i32) @spill(i32 1, i32 2, i32 3, i32 4, "
       "i32 5, ptr byval([16 x i8]) align 8 %1, i32 6)"});
  std::string fits = generate("struct s16 { long long a, b; };\n"
                              "int g(int a, int b, int c, int d, struct s16 f) { return a; }\n",
                              TARGET_LINUX_X64);
  EXPECT_EQ(first_line(function(fits, "g")),
            "define i32 @g(i32 %0, i32 %1, i32 %2, i32 %3, i64 %4, i64 %5) {");
  std::string returned = generate("struct s24 { long long a, b, c; };\n"
                                  "struct s16 { long long a, b; };\n"
                                  "int g(struct s24 r, int a, int b, int c, int d,\n"
                                  "  struct s16 f) { return a; }\n"
                                  "struct s24 h(int a, int b, int c, int d, struct s16 f) {\n"
                                  "  struct s24 r = {0}; return r; }\n",
                                  TARGET_LINUX_X64);
  EXPECT_EQ(first_line(function(returned, "g")),
            "define i32 @g(ptr byval([24 x i8]) align 8 %0, i32 %1, i32 %2, i32 %3, i32 %4, i64 "
            "%5, i64 %6) {")
      << "a record in memory takes no register";
  EXPECT_EQ(first_line(function(returned, "h")),
            "define void @h(ptr sret([24 x i8]) align 8 %0, i32 %1, i32 %2, i32 %3, i32 %4, "
            "ptr byval([16 x i8]) align 8 %5) {")
      << "the result's hidden pointer takes one";
}

static const char *linux_register_source =
    "struct s12 { int a, b, c; };\n"
    "struct s16 { long long a, b; };\n"
    "struct s24 { long long a, b, c; };\n"
    "struct d2 { double a, b; };\n"
    "struct unnamed { float a; int : 32; };\n"
    "union lu { long double l; double d[2]; };\n"
    "union ll { long double l; long long x; };\n"
    "struct __attribute__((aligned(16))) aligned { long long a, b; };\n"
    "struct unnamed run(struct unnamed x) { return x; }\n"
    "union lu rlu(union lu x) { return x; }\n"
    "union ll rll(union ll x) { return x; }\n"
    "struct aligned ral(struct aligned x) { return x; }\n"
    "long long four(struct s16 a, struct s16 b, struct s16 c, struct s16 d) { return a.a; }\n"
    "double five(struct d2 a, struct d2 b, struct d2 c, struct d2 d, struct d2 e) { return a.a; }\n"
    "struct s12 none(void) { }\n"
    "struct s24 hidden_spill(int a, int b, int c, int d, struct s16 f);\n"
    "long long callh(struct s16 *f) { return hidden_spill(1, 2, 3, 4, *f).c; }\n";

TEST(CodegenAbiLinuxTest, UnionsWithLongDoubleAndUnnamedBitFieldsFollowGcc) {
  std::string ir = generate(linux_register_source, TARGET_LINUX_X64);
  EXPECT_EQ(first_line(function(ir, "run")), "define i64 @run(i64 %0) {")
      << "GCC counts an unnamed bit-field, so the eightbyte is INTEGER";
  EXPECT_EQ(
      first_line(function(ir, "rlu")),
      "define void @rlu(ptr sret([16 x i8]) align 16 %0, ptr byval([16 x i8]) align 16 %1) {");
  EXPECT_EQ(
      first_line(function(ir, "rll")),
      "define void @rll(ptr sret([16 x i8]) align 16 %0, ptr byval([16 x i8]) align 16 %1) {");
  std::string ral = function(ir, "ral");
  EXPECT_EQ(first_line(ral), "define { i64, i64 } @ral(i64 %0, i64 %1) {");
  expect_lines(ral, {"store i64 %0, ptr %x.0, align 16", "store i64 %1, ptr %2, align 8",
                     "%3 = load i64, ptr %x.0, align 16", "%5 = load i64, ptr %4, align 8"});
}

TEST(CodegenAbiLinuxTest, EachRecordTakesTheRegistersItNeedsUntilTheyRunOut) {
  std::string ir = generate(linux_register_source, TARGET_LINUX_X64);
  EXPECT_EQ(first_line(function(ir, "four")),
            "define i64 @four(i64 %0, i64 %1, i64 %2, i64 %3, i64 %4, i64 %5, ptr byval([16 x i8]) "
            "align 8 %6) {");
  EXPECT_EQ(first_line(function(ir, "five")),
            "define double @five(double %0, double %1, double %2, double %3, double %4, double %5, "
            "double %6, double %7, ptr byval([16 x i8]) align 8 %8) {");
  expect_lines(function(ir, "callh"),
               {"call void (ptr, i32, i32, i32, i32, ptr) @hidden_spill(ptr sret([24 x i8]) "
                "align 8 %tmp.1, i32 1, i32 2, i32 3, i32 4, ptr byval([16 x i8]) align 8 %1)"});
  EXPECT_TRUE(has_line(ir, "declare void @hidden_spill(ptr sret([24 x i8]) align 8, i32, i32, i32, "
                           "i32, ptr byval([16 x i8]) align 8)"))
      << ir;
  expect_lines(function(ir, "none"), {"ret { i64, i32 } poison"});
}

TEST(CodegenAbiWindowsTest, DeclarationsAndFallingOffTheEndUseTheSameLowering) {
  std::string ir = generate("struct s12 { int a, b, c; };\n"
                            "extern struct s12 ext(struct s12, long double, _Bool);\n"
                            "int use(struct s12 *p) { return ext(*p, 1.0L, 1).a; }\n"
                            "struct s12 none(void) { }\n");
  expect_lines(
      function(ir, "use"),
      {"call void @llvm.memcpy.p0.p0.i64(ptr align 4 %tmp.2, ptr align 4 %1, i64 12, i1 "
       "false)",
       "call void (ptr, ptr, ptr, i8) @ext(ptr sret([12 x i8]) align 4 %tmp.1, ptr %tmp.2, "
       "ptr %tmp.3, i8 zeroext %3)"});
  EXPECT_TRUE(has_line(ir, "declare void @ext(ptr sret([12 x i8]) align 4, ptr, ptr, i8 zeroext)"))
      << ir;
  std::string none = function(ir, "none");
  EXPECT_EQ(first_line(none), "define void @none(ptr sret([12 x i8]) align 4 %0) {");
  expect_lines(none, {"ret void"});
}

TEST(CodegenVariadicTest, WindowsVaArgStepsEightBytesAndFollowsPointers) {
  std::string body =
      function(generate("#include <stdarg.h>\n"
                        "struct s12 { int a, b, c; };\n"
                        "double f(int n, ...) { va_list ap, aq; va_start(ap, n); va_copy(aq, ap);\n"
                        "  int i = va_arg(ap, int); struct s12 s = va_arg(ap, struct s12);\n"
                        "  long double l = va_arg(ap, long double); va_end(aq); va_end(ap);\n"
                        "  return i + s.c + (double)l; }\n",
                        TARGET_WINDOWS_X64, true),
               "f");
  EXPECT_EQ(first_line(body), "define double @f(i32 %0, ...) {");
  expect_lines(body,
               {"call void @llvm.va_start.p0(ptr %ap.1)", "%1 = load ptr, ptr %ap.1, align 8",
                "store ptr %1, ptr %aq.2, align 8", "%2 = load ptr, ptr %ap.1, align 8",
                "%3 = getelementptr inbounds i8, ptr %2, i64 8", "store ptr %3, ptr %ap.1, align 8",
                "%4 = load i32, ptr %2, align 4", "%6 = getelementptr inbounds i8, ptr %5, i64 8",
                "%7 = load ptr, ptr %5, align 8",
                "call void @llvm.memcpy.p0.p0.i64(ptr align 4 %s.4, ptr align 4 %7, i64 12, i1 "
                "false)",
                "%10 = load ptr, ptr %8, align 8", "%11 = load x86_fp80, ptr %10, align 16",
                "call void @llvm.va_end.p0(ptr %aq.2)", "call void @llvm.va_end.p0(ptr %ap.1)"});
  std::string declared =
      generate("#include <stdarg.h>\n"
               "int f(int n, ...) { va_list ap, aq; va_start(ap, n); va_copy(aq, ap); va_end(ap);\n"
               "  return n; }\n",
               TARGET_LINUX_X64, true);
  for (const char *line :
       {"declare void @llvm.va_start.p0(ptr)", "declare void @llvm.va_end.p0(ptr)",
        "declare void @llvm.va_copy.p0(ptr, ptr)"})
    EXPECT_EQ(count(declared, std::string("\n") + line + "\n"), 1) << line << "\n" << declared;
}

TEST(CodegenVariadicTest, SystemVVaArgChecksTheRegisterAreasItNeeds) {
  std::string body = function(
      generate("#include <stdarg.h>\n"
               "struct s12 { int a, b, c; };\n"
               "struct mix { int a; double b; };\n"
               "struct d2 { double a, b; };\n"
               "double f(int n, ...) { va_list ap, aq; va_start(ap, n); va_copy(aq, ap);\n"
               "  int i = va_arg(ap, int); double d = va_arg(ap, double);\n"
               "  struct s12 s = va_arg(ap, struct s12); struct mix m = va_arg(ap, struct mix);\n"
               "  struct d2 e = va_arg(ap, struct d2); long double l = va_arg(ap, long double);\n"
               "  va_end(ap); return i + d + s.c + m.b + e.b + (double)l; }\n",
               TARGET_LINUX_X64, true),
      "f");
  expect_lines(body, {"call void @llvm.va_start.p0(ptr %ap.1)",
                      "call void @llvm.va_copy.p0(ptr %aq.2, ptr %ap.1)",
                      "%1 = load i32, ptr %ap.1, align 4",
                      "%2 = icmp ule i32 %1, 40",
                      "br i1 %2, label %va.reg.4, label %va.mem.5",
                      "va.reg.4:",
                      "%3 = getelementptr inbounds i8, ptr %ap.1, i64 16",
                      "%5 = getelementptr i8, ptr %4, i32 %1",
                      "%6 = add i32 %1, 8",
                      "store i32 %6, ptr %ap.1, align 4",
                      "va.mem.5:",
                      "%7 = getelementptr inbounds i8, ptr %ap.1, i64 8",
                      "%9 = getelementptr i8, ptr %8, i64 8",
                      "store ptr %9, ptr %7, align 8",
                      "va.end.6:",
                      "%10 = phi ptr [ %5, %va.reg.4 ], [ %8, %va.mem.5 ]",
                      "%14 = icmp ule i32 %13, 160",
                      "%18 = add i32 %13, 16",
                      "%26 = icmp ule i32 %25, 32",
                      "%30 = add i32 %25, 16",
                      "%33 = getelementptr i8, ptr %32, i64 16",
                      "%40 = and i1 %36, %39",
                      "%44 = load i64, ptr %43, align 8",
                      "store i64 %44, ptr %tmp.19, align 8",
                      "%46 = load double, ptr %45, align 8",
                      "store double %46, ptr %47, align 8",
                      "%48 = add i32 %35, 8",
                      "%49 = add i32 %38, 16",
                      "%54 = phi ptr [ %tmp.19, %va.reg.16 ], [ %52, %va.mem.17 ]"});
  expect_lines(body,
               {"%57 = icmp ule i32 %56, 144", "%60 = getelementptr i8, ptr %59, i32 %56",
                "%61 = load double, ptr %60, align 8", "store double %61, ptr %tmp.24, align 8",
                "%63 = getelementptr inbounds i8, ptr %62, i64 16",
                "%64 = load double, ptr %63, align 8",
                "%65 = getelementptr inbounds i8, ptr %tmp.24, i64 8", "%66 = add i32 %56, 32"});
  expect_lines(body,
               {"%74 = ptrtoint ptr %73 to i64", "%75 = add i64 %74, 15", "%76 = and i64 %75, -16",
                "%77 = inttoptr i64 %76 to ptr", "%78 = getelementptr i8, ptr %77, i64 16",
                "%79 = load x86_fp80, ptr %77, align 16"});
}

TEST(CodegenModuleTest, UnusedInternalFunctionsAreLeftOut) {
  std::string ir = generate("static int leaf(void) { return 1; }\n"
                            "static int middle(void) { return leaf(); }\n"
                            "static int unused(void) { return middle(); }\n"
                            "static int lonely(void) { return 2; }\n"
                            "static int only_from_unused(void) { return 3; }\n"
                            "static int dead_end(void) { return only_from_unused(); }\n"
                            "static int pointed(void) { return 4; }\n"
                            "int (*table[1])(void) = {pointed};\n"
                            "int api(void) { return middle(); }\n");
  for (const char *kept : {"leaf", "middle", "pointed", "api"})
    EXPECT_NE(function(ir, kept), "") << kept << "\n" << ir;
  for (const char *dropped : {"unused", "lonely", "only_from_unused", "dead_end"})
    EXPECT_EQ(function(ir, dropped), "") << dropped << "\n" << ir;
  EXPECT_LT(ir.find("define internal i32 @leaf"), ir.find("define internal i32 @middle"))
      << "kept functions stay in source order";
}

static std::string read_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  std::string out;
  for (char c : text.str()) {
    if (c != '\r')
      out += c;
  }
  return out;
}

static int run(const std::string &command) {
  return std::system((command[0] == '"' ? "\"" + command + "\"" : command).c_str());
}

struct ProgramCase {
  const char *name;
  target_kind kind;
};

class CodegenProgramTest : public testing::TestWithParam<ProgramCase> {};

TEST_P(CodegenProgramTest, RunsAndMatchesGcc) {
  ProgramCase test = GetParam();
  bool windows = test.kind == TARGET_WINDOWS_X64;
  std::string base = std::string("codegen_") + test.name + (windows ? "_windows" : "_linux");
  std::string source = read_file(std::string(CODEGEN_PROGRAMS_DIR) + "/" + test.name + ".c");
  std::string ir = generate(source.c_str(), test.kind, true);
  {
    std::ofstream out(base + ".ll", std::ios::binary);
    out << ir;
  }
  std::string triple = windows ? "x86_64-w64-windows-gnu" : "x86_64-pc-linux-gnu";
  ASSERT_EQ(run("\"" CLANG_PATH "\" -c -Wno-override-module --target=" + triple + " " + base +
                ".ll -o " + base + ".o"),
            0)
      << ir;
  int status;
  if (windows) {
    std::string path = std::string(GCC_PATH);
    std::string bin = path.substr(0, path.find_last_of("/\\"));
    std::string old_path = getenv("PATH") ? getenv("PATH") : "";
    _putenv_s("PATH", (bin + ";" + old_path).c_str());
    ASSERT_EQ(run("\"" + path + "\" " + base + ".o -o " + base + ".exe"), 0);
    status = run(".\\" + base + ".exe > " + base + ".txt");
    _putenv_s("PATH", old_path.c_str());
  } else {
    ASSERT_EQ(run("wsl gcc " + base + ".o -o " + base + " -lm"), 0);
    status = run("wsl ./" + base + " > " + base + ".txt");
  }
  std::string actual = read_file(base + ".txt") + "exit " + std::to_string(status) + "\n";
  std::string expected = read_file(std::string(CODEGEN_PROGRAMS_DIR) + "/" + test.name +
                                   (windows ? ".windows.txt" : ".linux.txt"));
  EXPECT_EQ(actual, expected);
}

INSTANTIATE_TEST_SUITE_P(
    BothTargets, CodegenProgramTest,
    testing::Values(
        ProgramCase{"arith", TARGET_WINDOWS_X64}, ProgramCase{"arith", TARGET_LINUX_X64},
        ProgramCase{"control", TARGET_WINDOWS_X64}, ProgramCase{"control", TARGET_LINUX_X64},
        ProgramCase{"memory", TARGET_WINDOWS_X64}, ProgramCase{"memory", TARGET_LINUX_X64},
        ProgramCase{"edge", TARGET_WINDOWS_X64}, ProgramCase{"edge", TARGET_LINUX_X64},
        ProgramCase{"tail", TARGET_WINDOWS_X64}, ProgramCase{"tail", TARGET_LINUX_X64},
        ProgramCase{"records", TARGET_WINDOWS_X64}, ProgramCase{"records", TARGET_LINUX_X64},
        ProgramCase{"bitfields", TARGET_WINDOWS_X64}, ProgramCase{"bitfields", TARGET_LINUX_X64},
        ProgramCase{"globals", TARGET_WINDOWS_X64}, ProgramCase{"globals", TARGET_LINUX_X64},
        ProgramCase{"vla", TARGET_WINDOWS_X64}, ProgramCase{"vla", TARGET_LINUX_X64},
        ProgramCase{"complex", TARGET_WINDOWS_X64}, ProgramCase{"complex", TARGET_LINUX_X64},
        ProgramCase{"builtins", TARGET_WINDOWS_X64}, ProgramCase{"builtins", TARGET_LINUX_X64},
        ProgramCase{"asm", TARGET_WINDOWS_X64}, ProgramCase{"asm", TARGET_LINUX_X64},
        ProgramCase{"setjmp", TARGET_WINDOWS_X64}, ProgramCase{"setjmp", TARGET_LINUX_X64},
        ProgramCase{"stdio", TARGET_WINDOWS_X64}, ProgramCase{"stdio", TARGET_LINUX_X64},
        ProgramCase{"extensions", TARGET_WINDOWS_X64}, ProgramCase{"extensions", TARGET_LINUX_X64}),
    [](const testing::TestParamInfo<ProgramCase> &info) {
      return std::string(info.param.name) +
             (info.param.kind == TARGET_WINDOWS_X64 ? "Windows" : "Linux");
    });

static std::string wsl_path(const std::string &path) {
  if (path.size() < 2 || path[1] != ':')
    return path;
  return std::string("/mnt/") + (char)tolower((unsigned char)path[0]) + path.substr(2);
}

struct AbiCase {
  target_kind kind;
  bool ours_caller;
  bool ours_callee;
};

class CodegenAbiTest : public testing::TestWithParam<AbiCase> {};

TEST_P(CodegenAbiTest, LinksWithGccAndMatchesItsOutput) {
  AbiCase test = GetParam();
  bool windows = test.kind == TARGET_WINDOWS_X64;
  std::string dir = std::string(CODEGEN_PROGRAMS_DIR) + "/abi";
  std::string base = std::string("codegen_abi_") + (windows ? "windows" : "linux") +
                     (test.ours_caller ? "_ours" : "_gcc") + (test.ours_callee ? "_ours" : "_gcc");
  std::string triple = windows ? "x86_64-w64-windows-gnu" : "x86_64-pc-linux-gnu";
  std::string gcc = std::string(GCC_PATH);
  std::string bin = gcc.substr(0, gcc.find_last_of("/\\"));
  std::string old_path = getenv("PATH") ? getenv("PATH") : "";
  if (windows)
    _putenv_s("PATH", (bin + ";" + old_path).c_str());
  std::string objects;
  for (const char *half : {"caller", "callee"}) {
    bool ours = std::string(half) == "caller" ? test.ours_caller : test.ours_callee;
    std::string path = dir + "/" + half + ".c";
    std::string object = base + "_" + half + ".o";
    if (ours) {
      std::string ir = generate(nullptr, test.kind, true, path.c_str());
      {
        std::ofstream out(base + "_" + half + ".ll", std::ios::binary);
        out << ir;
      }
      ASSERT_EQ(run("\"" CLANG_PATH "\" -c -Wno-override-module --target=" + triple + " " + base +
                    "_" + half + ".ll -o " + object),
                0)
          << ir;
    } else if (windows) {
      ASSERT_EQ(run("\"" + gcc + "\" -std=c99 -w -c \"" + path + "\" -o " + object), 0);
    } else {
      ASSERT_EQ(run("wsl gcc -std=c99 -w -c '" + wsl_path(path) + "' -o " + object), 0);
    }
    objects += " " + object;
  }
  int status;
  if (windows) {
    ASSERT_EQ(run("\"" + gcc + "\"" + objects + " -o " + base + ".exe"), 0);
    status = run(".\\" + base + ".exe > " + base + ".txt");
    _putenv_s("PATH", old_path.c_str());
  } else {
    ASSERT_EQ(run("wsl gcc" + objects + " -o " + base), 0);
    status = run("wsl ./" + base + " > " + base + ".txt");
  }
  std::string actual = read_file(base + ".txt") + "exit " + std::to_string(status) + "\n";
  std::string expected = read_file(dir + (windows ? "/abi.windows.txt" : "/abi.linux.txt"));
  EXPECT_EQ(actual, expected);
}

INSTANTIATE_TEST_SUITE_P(
    BothTargets, CodegenAbiTest,
    testing::Values(AbiCase{TARGET_WINDOWS_X64, true, false},
                    AbiCase{TARGET_WINDOWS_X64, false, true},
                    AbiCase{TARGET_WINDOWS_X64, true, true}, AbiCase{TARGET_LINUX_X64, true, false},
                    AbiCase{TARGET_LINUX_X64, false, true}, AbiCase{TARGET_LINUX_X64, true, true}),
    [](const testing::TestParamInfo<AbiCase> &info) {
      return std::string(info.param.kind == TARGET_WINDOWS_X64 ? "Windows" : "Linux") +
             (info.param.ours_caller ? "OurCaller" : "GccCaller") +
             (info.param.ours_callee ? "OurCallee" : "GccCallee");
    });
