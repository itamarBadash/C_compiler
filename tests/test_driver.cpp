#include "target_guard.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif

extern "C" {
#include "driver.h"
#include "host.h"
}

struct Outcome {
  int code;
  std::string out;
  std::string err;
};

static Outcome drive_from(const char *root, std::vector<std::string> args) {
  std::string name = "MMC99";
  std::vector<char *> argv = {&name[0]};
  for (std::string &arg : args)
    argv.push_back(&arg[0]);
  argv.push_back(nullptr);
  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  int code = driver_run(root, (int)argv.size() - 1, argv.data());
  std::string out = testing::internal::GetCapturedStdout();
  std::string err = testing::internal::GetCapturedStderr();
  return {code, out, err};
}

static Outcome drive(std::vector<std::string> args) {
  return drive_from(TOOLCHAIN_DIR, args);
}

static std::string source(const char *name) {
  return std::string(DRIVER_PROGRAMS_DIR) + "/" + name;
}

static std::string backslashed(std::string path) {
  for (char &c : path) {
    if (c == '/')
      c = '\\';
  }
  return path;
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

static bool exists(const std::string &path) {
  std::ifstream in(path);
  return in.good();
}

static int run(const std::string &command) {
  return std::system((command[0] == '"' ? "\"" + command + "\"" : command).c_str());
}

static std::string run_program(const std::string &program, target_kind kind) {
  std::string output = "\"" + program + ".txt\" 2> nul";
  int status = kind == TARGET_WINDOWS_X64 ? run(".\\\"" + program + "\" > " + output)
                                          : run("wsl './" + program + "' > " + output);
  return read_file(program + ".txt") + "exit " + std::to_string(status) + "\n";
}

static std::string command_of(const std::string &err, const char *tool) {
  std::string start = std::string(TOOLCHAIN_DIR) + "/bin/" + tool;
  size_t at = err.find(start);
  if (at == std::string::npos)
    return "<" + std::string(tool) + " not run>";
  std::string line = err.substr(at, err.find('\n', at) - at);
  for (size_t root; (root = line.find(TOOLCHAIN_DIR)) != std::string::npos;)
    line.replace(root, std::string(TOOLCHAIN_DIR).size(), "<root>");
  return std::regex_replace(line, std::regex("[^ ]*cc99-[0-9]+-[0-9]+\\.ll"), "<ir>");
}

class DriverProgramTest : public testing::TestWithParam<target_kind> {
protected:
  std::string target() const {
    return GetParam() == TARGET_WINDOWS_X64 ? "--target=x86_64-w64-windows-gnu"
                                            : "--target=x86_64-pc-linux-gnu";
  }
  std::string program(const char *name) const {
    return std::string("driver_") + name + (GetParam() == TARGET_WINDOWS_X64 ? ".exe" : "");
  }
};

TEST_P(DriverProgramTest, HelloIncludesTheSystemHeadersAndRuns) {
  TargetGuard guard(GetParam());
  std::string out = program("hello");
  Outcome built = drive({target(), "-DNUM=42", source("hello.c"), "-lm", "-o", out});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_EQ(built.err, "");
  EXPECT_EQ(run_program(out, GetParam()), "hello world 42 1.414\nexit 3\n");
}

TEST_P(DriverProgramTest, SeveralSourcesLinkIntoOneProgram) {
  TargetGuard guard(GetParam());
  std::string out = program("several");
  Outcome built = drive({target(), source("data.c"), source("other.c"), "-o", out});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_EQ(run_program(out, GetParam()), "two 7 1\nexit 7\n");
}

TEST_P(DriverProgramTest, AnObjectFromDashCLinksLater) {
  TargetGuard guard(GetParam());
  std::string object = program("other") + ".o";
  remove(object.c_str());
  Outcome compiled = drive({target(), "-c", source("other.c"), "-o", object});
  ASSERT_EQ(compiled.code, 0) << compiled.err;
  ASSERT_TRUE(exists(object));
  std::string out = program("later");
  Outcome linked = drive({target(), source("data.c"), object, "-o", out});
  ASSERT_EQ(linked.code, 0) << linked.err;
  EXPECT_EQ(run_program(out, GetParam()), "two 7 1\nexit 7\n");
}

TEST_P(DriverProgramTest, OptimizedProgramsBehaveTheSame) {
  TargetGuard guard(GetParam());
  std::string out = program("optimized");
  Outcome built = drive({target(), "-O2", "-v", source("data.c"), source("other.c"), "-o", out});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_NE(command_of(built.err, "clang").find(" -O2 "), std::string::npos) << built.err;
  EXPECT_EQ(run_program(out, GetParam()), "two 7 1\nexit 7\n");
}

TEST_P(DriverProgramTest, WithoutDashOTheProgramIsNamedLikeGcc) {
  TargetGuard guard(GetParam());
  std::string out = GetParam() == TARGET_WINDOWS_X64 ? "a.exe" : "a.out";
  remove(out.c_str());
  Outcome built = drive({target(), "-DNUM=1", source("hello.c"), "-lm"});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_EQ(run_program(out, GetParam()), "hello world 1 1.414\nexit 3\n");
}

TEST_P(DriverProgramTest, ThreadedBuildsLink) {
  TargetGuard guard(GetParam());
  std::string out = program("threaded");
  Outcome built = drive({target(), "-pthread", "-Wl,--no-gc-sections", source("data.c"),
                         source("other.c"), "-o", out});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_EQ(run_program(out, GetParam()), "two 7 1\nexit 7\n");
}

TEST_P(DriverProgramTest, ToolsAreRunLikeGccRunsThem) {
  TargetGuard guard(GetParam());
  bool windows = GetParam() == TARGET_WINDOWS_X64;
  std::string triple = windows ? "x86_64-w64-windows-gnu" : "x86_64-pc-linux-gnu";
  std::string object = program("golden") + ".o";
  Outcome compiled = drive({target(), "-v", "-c", source("other.c"), "-o", object});
  ASSERT_EQ(compiled.code, 0) << compiled.err;
  EXPECT_EQ(command_of(compiled.err, "clang"), "<root>/bin/clang.exe -c -x ir <ir> -o " + object +
                                                   " -O0 -Wno-override-module --target=" + triple);
  Outcome linked = drive({target(), "-v", object, object, "-o", program("golden")});
  EXPECT_EQ(linked.code, 1) << "two copies of one object, and no main, cannot link";
  std::string lib = "<root>/targets/" + triple + "/lib/";
  std::string expected =
      windows
          ? "<root>/bin/ld.lld.exe -m i386pep -o driver_golden.exe " + lib + "crt2.o " + lib +
                "crtbegin.o " + object + " " + object + " -L" + lib +
                " -lmingw32 -lgcc -lgcc_eh -lmoldname -lmingwex -lmsvcrt -lkernel32 -lpthread "
                "-ladvapi32 -lshell32 -luser32 -lkernel32 -lmingw32 -lgcc -lgcc_eh -lmoldname "
                "-lmingwex -lmsvcrt -lkernel32 " +
                lib + "default-manifest.o " + lib + "crtend.o"
          : "<root>/bin/ld.lld.exe -m elf_x86_64 --hash-style=gnu --eh-frame-hdr --build-id "
            "-pie -z now -z relro -dynamic-linker /lib64/ld-linux-x86-64.so.2 -o "
            "driver_golden " +
                lib + "Scrt1.o " + lib + "crti.o " + lib + "crtbeginS.o " + object + " " + object +
                " -L" + lib + " -lgcc -lc " + lib + "libc_nonshared.a --as-needed " + lib +
                "ld-linux-x86-64.so.2 --no-as-needed -lgcc " + lib + "crtendS.o " + lib + "crtn.o";
  EXPECT_EQ(command_of(linked.err, "ld.lld"), expected);
}

INSTANTIATE_TEST_SUITE_P(BothTargets, DriverProgramTest,
                         testing::Values(TARGET_WINDOWS_X64, TARGET_LINUX_X64),
                         [](const testing::TestParamInfo<target_kind> &info) {
                           return info.param == TARGET_WINDOWS_X64 ? "Windows" : "Linux";
                         });

TEST(DriverTest, TheHostTargetIsTheDefault) {
  remove("driver_default.ll");
  Outcome built = drive({"-S", source("other.c"), "-o", "driver_default.ll"});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_NE(read_file("driver_default.ll").find("target triple = \"x86_64-w64-windows-gnu\""),
            std::string::npos);
}

TEST(DriverTest, DashSWritesIrNamedAfterTheSource) {
  TargetGuard guard(TARGET_WINDOWS_X64);
  remove("other.ll");
  Outcome built = drive({"--target=x86_64-linux-gnu", "-S", source("other.c")});
  ASSERT_EQ(built.code, 0) << built.err;
  std::string ir = read_file("other.ll");
  EXPECT_NE(ir.find("target triple = \"x86_64-pc-linux-gnu\""), std::string::npos) << ir;
  EXPECT_NE(ir.find("define i32 @bump(i32 %0)"), std::string::npos) << ir;
}

TEST(DriverTest, DashCWritesAnObjectNamedAfterTheSource) {
  remove("other.o");
  Outcome built = drive({"--target=x86_64-w64-mingw32", "-c", backslashed(source("other.c"))});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_TRUE(exists("other.o"));
}

TEST(DriverTest, DashEPrintsTokensOnTheirInvocationLines) {
  std::string path = backslashed(source("e.c"));
  Outcome built = drive({"-E", "-DX=1", "-UX", "-DY=2", path});
  ASSERT_EQ(built.code, 0) << built.err;
  std::string escaped;
  for (char c : path) {
    if (c == '\\')
      escaped += '\\';
    escaped += c;
  }
  std::string marker = "\"" + escaped + "\"\n";
  EXPECT_EQ(built.out, "# 7 " + marker +
                           "int a = 2 ;\n"
                           "2 ;\n"
                           "  int b = - - 1;\n"
                           "      int c;\n"
                           "int d = 1 + 2 ; int e;\n"
                           "\n"
                           "const char *s = \"q\\\"x\", *w = L\"wide\";\n"
                           "int k = 'k' + L'w';\n"
                           "\n"
                           "\n"
                           "\n"
                           "int f = 18 ;\n"
                           "# 29 " +
                           marker +
                           "int g;\n"
                           "# 5 " +
                           marker + "int h;\n");
}

TEST(DriverTest, DashENeverGluesTokensThatWereApart) {
  Outcome built = drive({"-E", "-DM=-", source("glue.c")});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_EQ(built.out.substr(built.out.find('\n') + 1), "int xyz =- - ;\n\nint xyz =- - ;\n")
      << "a minus from the command line or from an earlier line is a separate token";
}

TEST(DriverTest, DashEWithDashOWritesTheFile) {
  remove("driver_include.i");
  Outcome built = drive({"-E", "-I", std::string(DRIVER_PROGRAMS_DIR) + "/include", "-DZ",
                         source("include.c"), "-o", "driver_include.i"});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_EQ(built.out, "");
  std::string text = read_file("driver_include.i");
  EXPECT_NE(text.find("\nint v = 5 + 1 ;\n"), std::string::npos) << text;
}

TEST(DriverTest, AttachedOptionValuesWork) {
  Outcome built = drive(
      {"-E", "-I" + std::string(DRIVER_PROGRAMS_DIR) + "/include", "-DZ=4", source("include.c")});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_NE(built.out.find("\nint v = 5 + 4 ;\n"), std::string::npos) << built.out;
}

TEST(DriverTest, TheCompilersOwnHeadersComeBeforeTheSystemsOnes) {
  Outcome built = drive({"-E", source("errhandling.c")});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_EQ(built.out.substr(built.out.rfind("\n", built.out.size() - 2)), "\n1\n")
      << "only our <math.h> defines math_errhandling on Windows";
}

TEST(DriverTest, CompileErrorsAreCountedAndStopTheBuild) {
  remove("driver_broken.exe");
  Outcome built = drive({source("broken.c"), "-o", "driver_broken.exe"});
  EXPECT_EQ(built.code, 1);
  EXPECT_NE(built.err.find("broken.c:1:29: error: expected an expression"), std::string::npos)
      << built.err;
  EXPECT_NE(built.err.find("\n2 errors generated.\n"), std::string::npos) << built.err;
  EXPECT_EQ(built.err.find("ld.lld"), std::string::npos) << "the linker never runs";
  EXPECT_FALSE(exists("driver_broken.exe"));
}

TEST(DriverTest, EveryFileIsCheckedEvenAfterOneFails) {
  Outcome built = drive({"-c", source("broken.c"), "no_such_file.c", source("second_broken.c")});
  EXPECT_EQ(built.code, 1);
  EXPECT_NE(built.err.find("\n2 errors generated.\nerror: cannot open 'no_such_file.c'\n"),
            std::string::npos)
      << built.err;
  EXPECT_NE(built.err.find("second_broken.c:1:20: error: a function returning a value needs a "
                           "return value\n1 error generated.\n"),
            std::string::npos)
      << built.err;
}

TEST(DriverTest, NoCodeIsGeneratedForAProgramWithErrors) {
  remove("driver_second.o");
  Outcome built = drive({"-v", "-c", source("second_broken.c"), "-o", "driver_second.o"});
  EXPECT_EQ(built.code, 1);
  EXPECT_EQ(built.err.find("clang"), std::string::npos) << built.err;
  EXPECT_FALSE(exists("driver_second.o"));
}

TEST(DriverTest, LinkErrorsFail) {
  Outcome built = drive({source("data.c"), "-o", "driver_unlinked.exe"});
  EXPECT_EQ(built.code, 1);
  EXPECT_NE(built.err.find("undefined symbol: bump"), std::string::npos) << built.err;
  EXPECT_NE(built.err.find("ld.lld.exe' failed with exit code 1"), std::string::npos) << built.err;
}

TEST(DriverTest, AMissingToolIsReported) {
  Outcome built = drive_from("no_such_root", {"-c", source("other.c"), "-o", "driver_none.o"});
  EXPECT_EQ(built.code, 1);
  EXPECT_EQ(built.err, "error: cannot run 'no_such_root/bin/clang.exe'\n");
}

TEST(DriverTest, LibraryOptionsReachTheLinker) {
  Outcome built = drive({"-v", "-L", "first", "-Lsecond", "-lm", source("other.c"),
                         source("data.c"), "-o", "driver_libraries.exe"});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_NE(command_of(built.err, "ld.lld").find(" -Lfirst -Lsecond -lm -lmingw32 "),
            std::string::npos)
      << built.err;
}

TEST(DriverTest, BareDashOMeansLevelOne) {
  Outcome built = drive({"-O", "-v", "-c", source("other.c"), "-o", "driver_o1.o"});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_NE(command_of(built.err, "clang").find(" -O1 "), std::string::npos) << built.err;
}

TEST(DriverTest, FlagsFromBuildFilesAreAcceptedWithoutEffect) {
  remove("driver_flags.exe");
  Outcome built = drive({"-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-w", "-pedantic",
                         "-pedantic-errors", "-g", "-g3", "-ggdb", "-fPIC", "-fpic", "-fPIE",
                         "-fpie", source("data.c"), source("other.c"), "-o", "driver_flags.exe"});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_EQ(built.err, "");
  EXPECT_EQ(run_program("driver_flags.exe", TARGET_WINDOWS_X64), "two 7 1\nexit 7\n");
}

TEST(DriverTest, PreprocessorAndAssemblerPassThroughsAreStillRejected) {
  Outcome preprocessor = drive({"-Wp,-DX=1", source("other.c")});
  EXPECT_EQ(preprocessor.code, 1);
  EXPECT_EQ(preprocessor.err, "error: unknown option '-Wp,-DX=1'\n");
  Outcome assembler = drive({"-Wa,--noexecstack", source("other.c")});
  EXPECT_EQ(assembler.code, 1);
  EXPECT_EQ(assembler.err, "error: unknown option '-Wa,--noexecstack'\n");
  Outcome other = drive({"-fwrapv", source("other.c")});
  EXPECT_EQ(other.err, "error: unknown option '-fwrapv'\n")
      << "an -f option that changes what code means is not ignored";
}

TEST(DriverTest, EveryOptimizationSpellingReachesClang) {
  for (const char *level : {"-Os", "-Oz", "-Og", "-O3"}) {
    Outcome built = drive({level, "-v", "-c", source("other.c"), "-o", "driver_level.o"});
    ASSERT_EQ(built.code, 0) << built.err;
    EXPECT_NE(command_of(built.err, "clang").find(std::string(" ") + level + " "),
              std::string::npos)
        << built.err;
  }
}

TEST(DriverTest, LinkerOptionsReachTheLinkerInTheirPlace) {
  Outcome object = drive({"-c", source("other.c"), "-o", "driver_wl_other.o"});
  ASSERT_EQ(object.code, 0) << object.err;
  remove("driver_map.txt");
  Outcome built = drive({"-v", "driver_wl_other.o", "-Wl,-Map,driver_map.txt,,--no-gc-sections",
                         source("data.c"), "-o", "driver_wl.exe"});
  ASSERT_EQ(built.code, 0) << built.err;
  std::string link = command_of(built.err, "ld.lld");
  EXPECT_NE(link.find(" driver_wl_other.o -Map driver_map.txt --no-gc-sections "),
            std::string::npos)
      << link;
  EXPECT_LT(link.find(" -Map "), link.find("cc99-")) << "data.c's object comes after the option";
  EXPECT_TRUE(exists("driver_map.txt"));
}

TEST(DriverTest, LinkerOptionsAreNotInputs) {
  Outcome alone = drive({"-Wl,--no-gc-sections"});
  EXPECT_EQ(alone.code, 1);
  EXPECT_EQ(alone.err, "error: no input files\n");
  Outcome compiled =
      drive({"-c", "-Wl,--no-such-linker-option", source("other.c"), "-o", "driver_wl_unused.o"});
  EXPECT_EQ(compiled.code, 0) << "without linking, linker options are unused";
  EXPECT_EQ(compiled.err, "");
}

TEST(DriverTest, PthreadDefinesReentrantAndLinksThePthreadLibrary) {
  Outcome plain = drive({"-E", source("strict.c")});
  ASSERT_EQ(plain.code, 0) << plain.err;
  EXPECT_NE(plain.out.find("\n_REENTRANT\n"), std::string::npos) << plain.out;
  Outcome threaded = drive({"-pthread", "-E", source("strict.c")});
  ASSERT_EQ(threaded.code, 0) << threaded.err;
  EXPECT_NE(threaded.out.find("\n1\n"), std::string::npos) << threaded.out;
  Outcome linked =
      drive({"-pthread", "-v", source("data.c"), source("other.c"), "-o", "driver_pthread.exe"});
  ASSERT_EQ(linked.code, 0) << linked.err;
  EXPECT_NE(command_of(linked.err, "ld.lld").find(" -lpthread -lmingw32 "), std::string::npos)
      << linked.err;
}

TEST(DriverTest, Gnu99LeavesStrictAnsiUndefinedUnlessTheUserDefinesIt) {
  struct Case {
    std::vector<std::string> flags;
    const char *mode;
  };
  std::vector<Case> cases = {
      {{}, "strict"},
      {{"-std=c99"}, "strict"},
      {{"-std=gnu99"}, "gnu"},
      {{"-std=gnu99", "-std=c99"}, "strict"},
      {{"-std=c99", "-std=gnu99"}, "gnu"},
      {{"-std=gnu99", "-D__STRICT_ANSI__"}, "strict"},
  };
  for (Case &c : cases) {
    std::vector<std::string> args = c.flags;
    args.push_back("-E");
    args.push_back(source("strict.c"));
    Outcome result = drive(args);
    ASSERT_EQ(result.code, 0) << result.err;
    EXPECT_NE(result.out.find(std::string("\n") + c.mode + "\n"), std::string::npos)
        << c.mode << ":\n"
        << result.out;
  }
}

TEST(DriverTest, Gnu99ProgramsSeeTheSystemsExtensions) {
  Outcome strict = drive({source("pi.c"), "-o", "driver_pi.exe"});
  EXPECT_EQ(strict.code, 1);
  EXPECT_NE(strict.err.find("use of undeclared identifier 'M_PI'"), std::string::npos)
      << strict.err;
  Outcome gnu = drive({"-std=gnu99", source("pi.c"), "-o", "driver_pi.exe"});
  ASSERT_EQ(gnu.code, 0) << gnu.err;
  EXPECT_EQ(run_program("driver_pi.exe", TARGET_WINDOWS_X64), "3.1416\nexit 0\n");
}

TEST(DriverTest, AWindowsApiProgramBuildsAndRuns) {
  Outcome built = drive({source("winapi.c"), "-o", "driver_winapi.exe"});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_EQ(built.err, "");
  EXPECT_EQ(run_program("driver_winapi.exe", TARGET_WINDOWS_X64),
            "large 1 2\nquad 8589934597\nlast error 42\nlength 7\nwide 4 98\npage 4096\n"
            "exchange 3 9\nthreads 2000 4000 exit 7\nexit 0\n");
}

TEST(DriverTest, ArgumentsWithSpacesQuotesAndBackslashesReachToolsWhole) {
  std::string out = "driver spaced program.exe";
  remove(out.c_str());
  Outcome built =
      drive({"-Lquote\"inside", "-Ltrailing\\", source("other.c"), source("data.c"), "-o", out});
  ASSERT_EQ(built.code, 0) << built.err;
  EXPECT_EQ(run_program(out, TARGET_WINDOWS_X64), "two 7 1\nexit 7\n");
}

TEST(DriverTest, TemporaryFilesAreRemoved) {
  Outcome built = drive({source("other.c"), source("data.c"), "-o", "driver_temporaries.exe"});
  ASSERT_EQ(built.code, 0) << built.err;
  char *next = host_temporary_file(".ll");
  std::string path = next;
  free(next);
  size_t dash = path.rfind('-');
  int counter = std::stoi(path.substr(dash + 1));
  ASSERT_GE(counter, 4);
  for (int i = counter - 4; i < counter; i++) {
    std::string base = path.substr(0, dash + 1) + std::to_string(i);
    EXPECT_FALSE(exists(base + ".ll")) << base;
    EXPECT_FALSE(exists(base + ".o")) << base;
  }
}

TEST(DriverTest, LinkerInputsAreUnusedWithoutLinking) {
  Outcome built = drive({"-c", source("other.c"), "missing_but_unused.o", "-o", "driver_only.o"});
  EXPECT_EQ(built.code, 1);
  EXPECT_EQ(built.err, "error: cannot open 'missing_but_unused.o'\n");
  Outcome object = drive({"-c", source("other.c"), "-o", "driver_only.o"});
  ASSERT_EQ(object.code, 0) << object.err;
  Outcome warned = drive({"-c", source("other.c"), "driver_only.o", "-o", "driver_only2.o"});
  EXPECT_EQ(warned.code, 0);
  EXPECT_EQ(warned.err, "warning: 'driver_only.o' is a linker input, unused without linking\n");
}

TEST(DriverTest, BadCommandLinesAreRejected) {
  struct Case {
    std::vector<std::string> args;
    const char *message;
  };
  std::vector<Case> cases = {
      {{}, "error: no input files\n"},
      {{"-q", "x.c"}, "error: unknown option '-q'\n"},
      {{"x.c", "-o"}, "error: missing argument to '-o'\n"},
      {{"-I"}, "error: missing argument to '-I'\n"},
      {{"--target=arm-none-eabi", "x.c"}, "error: unknown target 'arm-none-eabi'\n"},
      {{"-std=c11", "x.c"}, "error: only -std=c99 and -std=gnu99 are supported, not '-std=c11'\n"},
      {{"-c", "-o", "x.o", "a.c", "b.c"},
       "error: cannot specify '-o' with '-c', '-S' or '-E' with several files\n"},
      {{"no_such_file.c"}, "error: cannot open 'no_such_file.c'\n"},
  };
  for (Case &c : cases) {
    Outcome result = drive(c.args);
    EXPECT_EQ(result.code, 1) << c.message;
    EXPECT_EQ(result.err, c.message);
  }
}

TEST(DriverTest, StdC99IsAccepted) {
  Outcome built = drive({"-std=c99", "-E", source("other.c")});
  EXPECT_EQ(built.code, 0) << built.err;
}

TEST(DriverTest, OneErrorIsSingular) {
  Outcome built = drive({"-E", "-DQ(", source("other.c")});
  EXPECT_EQ(built.code, 1);
  EXPECT_NE(built.err.find("\n1 error generated.\n"), std::string::npos) << built.err;
}

TEST(DriverTest, HelpAndVersionPrintAndStop) {
  Outcome help = drive({"--help", "no_such_file.c"});
  EXPECT_EQ(help.code, 0);
  EXPECT_EQ(help.out.rfind("usage: MMC99 [options] file...\n", 0), 0u) << help.out;
  EXPECT_EQ(help.err, "");
  Outcome version = drive({"--version", "no_such_file.c"});
  EXPECT_EQ(version.code, 0);
  EXPECT_EQ(version.out.rfind("MMC99 " MMC99_VERSION ", a C99 compiler for ", 0), 0u)
      << version.out;
  EXPECT_NE(version.out.find("\nTheir licenses are in the licenses folder.\n"), std::string::npos)
      << version.out;
  EXPECT_EQ(version.err, "");
}

TEST(HostTest, RunReturnsTheProgramsExitCode) {
#ifdef _WIN32
  const char *const fails[] = {TOOLCHAIN_DIR "/bin/ld.lld.exe", "--no-such-option", nullptr};
  const char *const succeeds[] = {TOOLCHAIN_DIR "/bin/ld.lld.exe", "--version", nullptr};
  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  int failed = host_run(fails);
  int succeeded = host_run(succeeds);
  testing::internal::GetCapturedStdout();
  testing::internal::GetCapturedStderr();
  EXPECT_EQ(failed, 1);
  EXPECT_EQ(succeeded, 0);
#else
  const char *const exits[] = {"/bin/sh", "-c", "exit 3", nullptr};
  EXPECT_EQ(host_run(exits), 3);
  const char *const succeeds[] = {"/bin/sh", "-c", "exit 0", nullptr};
  EXPECT_EQ(host_run(succeeds), 0);
#endif
}

TEST(HostTest, RunReportsAProgramThatCannotStart) {
  const char *const missing[] = {"no_such_program", nullptr};
  EXPECT_EQ(host_run(missing), -1);
}

TEST(HostTest, TheInstallRootIsTwoFoldersAboveTheExecutable) {
#ifdef _WIN32
  std::string exe = _pgmptr;
#else
  char buffer[4096];
  ssize_t length = readlink("/proc/self/exe", buffer, sizeof buffer - 1);
  buffer[length > 0 ? length : 0] = '\0';
  std::string exe = buffer;
#endif
  std::string folder = exe.substr(0, exe.find_last_of("/\\"));
  std::string expected = folder.substr(0, folder.find_last_of("/\\"));
  char *root = host_install_root();
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root, expected);
  free(root);
}

TEST(HostTest, TemporaryFilesHaveDistinctNames) {
  char *first = host_temporary_file(".ll");
  char *second = host_temporary_file(".ll");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_STRNE(first, second);
  EXPECT_NE(std::string(first).find(".ll"), std::string::npos);
  free(first);
  free(second);
}

TEST(DriverTest, TheExecutableFindsItsToolchainFromItsOwnFolder) {
  remove("driver_main.exe");
  ASSERT_EQ(run("\"" TOOLCHAIN_DIR "/bin/MMC99.exe\" -DNUM=5 \"" + source("hello.c") +
                "\" -o driver_main.exe"),
            0);
  EXPECT_EQ(run_program("driver_main.exe", TARGET_WINDOWS_X64), "hello world 5 1.414\nexit 3\n");
  EXPECT_EQ(run("\"" TOOLCHAIN_DIR "/bin/MMC99.exe\" no_such_file.c 2> driver_main.txt"), 1);
}
