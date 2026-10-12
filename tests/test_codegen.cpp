#include "target_guard.h"
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

static std::string generate(const char *source, target_kind kind = TARGET_WINDOWS_X64) {
  TargetGuard guard(kind);
  testing::internal::CaptureStderr();
  token_buf tb;
  int errors = pp_run(&tb, source);
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
  expect_lines(function(ir, "g"), {"%2 = fpext double %1 to x86_fp80"});
  expect_lines(function(ir, "h"), {"%2 = fptrunc x86_fp80 %1 to double"});
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
  expect_lines(function(ir, "c"), {"ret x86_fp80 0xK3FFF8000000000000000"});
  expect_lines(function(ir, "d"), {"%0 = fneg x86_fp80 0xK4000A000000000000000"});
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
  std::string ir = generate(source.c_str(), test.kind);
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
    ASSERT_EQ(run("wsl gcc " + base + ".o -o " + base), 0);
    status = run("wsl ./" + base + " > " + base + ".txt");
  }
  std::string actual = read_file(base + ".txt") + "exit " + std::to_string(status) + "\n";
  std::string expected = read_file(std::string(CODEGEN_PROGRAMS_DIR) + "/" + test.name +
                                   (windows ? ".windows.txt" : ".linux.txt"));
  EXPECT_EQ(actual, expected);
}

INSTANTIATE_TEST_SUITE_P(BothTargets, CodegenProgramTest,
                         testing::Values(ProgramCase{"arith", TARGET_WINDOWS_X64},
                                         ProgramCase{"arith", TARGET_LINUX_X64},
                                         ProgramCase{"control", TARGET_WINDOWS_X64},
                                         ProgramCase{"control", TARGET_LINUX_X64},
                                         ProgramCase{"memory", TARGET_WINDOWS_X64},
                                         ProgramCase{"memory", TARGET_LINUX_X64},
                                         ProgramCase{"edge", TARGET_WINDOWS_X64},
                                         ProgramCase{"edge", TARGET_LINUX_X64},
                                         ProgramCase{"tail", TARGET_WINDOWS_X64},
                                         ProgramCase{"tail", TARGET_LINUX_X64}),
                         [](const testing::TestParamInfo<ProgramCase> &info) {
                           return std::string(info.param.name) +
                                  (info.param.kind == TARGET_WINDOWS_X64 ? "Windows" : "Linux");
                         });
