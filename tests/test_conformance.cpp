#include <algorithm>
#include <cstdlib>
#include <direct.h>
#include <fstream>
#include <gtest/gtest.h>
#include <io.h>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static const std::string compiler = std::string(TOOLCHAIN_DIR) + "/bin/MMC99.exe";
static const std::string corpus = CORPUS_DIR;

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

static std::string in_quotes(const std::string &text) {
  return "\"" + text + "\"";
}

static int run(const std::string &command) {
  return std::system((command[0] == '"' ? "\"" + command + "\"" : command).c_str());
}

static std::string wsl_path(std::string path) {
  for (char &c : path) {
    if (c == '\\')
      c = '/';
  }
  if (path.size() < 2 || path[1] != ':')
    return path;
  return std::string("/mnt/") + (char)tolower((unsigned char)path[0]) + path.substr(2);
}

static std::vector<std::string> files_in(const std::string &folder, const std::string &pattern) {
  std::vector<std::string> names;
  _finddata_t found;
  intptr_t search = _findfirst((folder + "/" + pattern).c_str(), &found);
  if (search == -1)
    return names;
  do
    names.push_back(found.name);
  while (_findnext(search, &found) == 0);
  _findclose(search);
  std::sort(names.begin(), names.end());
  return names;
}

static std::string current_folder() {
  char folder[4096];
  return _getcwd(folder, sizeof folder) ? folder : ".";
}

struct Target {
  const char *name;
  const char *triple;
  bool windows;
};

class ConformanceTest : public testing::TestWithParam<Target> {
protected:
  std::string work;

  void SetUp() override {
    work = current_folder() + "/conformance_" + GetParam().name;
    _mkdir(work.c_str());
  }

  std::string program(const std::string &name) const {
    return work + "/" + name + (GetParam().windows ? ".exe" : "");
  }

  bool compile(const std::string &arguments, const std::string &exe, const std::string &log) const {
    return run(in_quotes(compiler) + " --target=" + GetParam().triple + " " + arguments + " -o " +
               in_quotes(exe) + " > " + in_quotes(log) + " 2>&1") == 0;
  }

  int execute(const std::string &folder, const std::string &exe, const std::string &arguments,
              const std::string &output) const {
    if (GetParam().windows)
      return run("cd /d " + in_quotes(folder) + " && " + in_quotes(exe) + " " + arguments + " > " +
                 in_quotes(output) + " 2>&1");
    return run("wsl sh -c \"cd '" + wsl_path(folder) + "' && '" + wsl_path(exe) + "' " + arguments +
               " > '" + wsl_path(output) + "' 2>&1\"");
  }
};

struct Exclusion {
  std::string targets;
  std::string reason;
};

static std::map<std::string, Exclusion> exclusions() {
  std::map<std::string, Exclusion> out;
  std::istringstream lines(read_file(std::string(CONFORMANCE_DIR) + "/c-testsuite-exclusions.txt"));
  std::string line;
  while (std::getline(lines, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream fields(line);
    std::string name;
    Exclusion exclusion;
    fields >> name >> exclusion.targets;
    std::getline(fields, exclusion.reason);
    out[name] = exclusion;
  }
  return out;
}

TEST_P(ConformanceTest, CTestSuiteProgramsMatchTheirExpectedOutput) {
  std::string suite = corpus + "/c-testsuite/tests/single-exec";
  std::vector<std::string> sources = files_in(suite, "*.c");
  if (sources.empty())
    GTEST_SKIP() << "no corpus: run python tests/conformance/fetch_corpus.py " << corpus;
  std::map<std::string, Exclusion> excluded = exclusions();
  ASSERT_FALSE(excluded.empty());
  int passed = 0;
  std::vector<std::string> failures;
  for (const std::string &file : sources) {
    std::string name = file.substr(0, file.size() - 2);
    std::string exe = program(name);
    std::string log = work + "/" + name + ".log";
    std::string output = work + "/" + name + ".out";
    bool pass = compile("-w " + in_quotes(suite + "/" + file) + " -lm", exe, log) &&
                execute(work, exe, "", output) == 0 &&
                read_file(output) == read_file(suite + "/" + file + ".expected");
    auto exclusion = excluded.find(name);
    bool is_excluded =
        exclusion != excluded.end() &&
        (exclusion->second.targets == "both" || exclusion->second.targets == GetParam().name);
    if (pass && is_excluded)
      failures.push_back(name + " passes now: remove it from the exclusions");
    else if (!pass && !is_excluded)
      failures.push_back(name + " fails:\n" + read_file(log) + read_file(output));
    passed += pass;
  }
  EXPECT_EQ(failures, std::vector<std::string>{});
  EXPECT_GT(passed, 200) << "of " << sources.size();
}

TEST_P(ConformanceTest, LuaPassesItsOwnTestSuite) {
  std::string source = corpus + "/lua/src";
  std::vector<std::string> files;
  for (const std::string &file : files_in(source, "*.c")) {
    if (file != "luac.c")
      files.push_back(file);
  }
  if (files.empty())
    GTEST_SKIP() << "no corpus: run python tests/conformance/fetch_corpus.py " << corpus;
  std::string arguments = "-O2";
  for (const std::string &file : files)
    arguments += " " + in_quotes(source + "/" + file);
  std::string lua = program("lua");
  std::string log = work + "/lua.log";
  ASSERT_TRUE(compile(arguments + " -lm", lua, log)) << read_file(log);
  std::string output = work + "/lua-tests.out";
  int status = execute(corpus + "/lua-tests", lua, "-e_U=true all.lua", output);
  std::string text = read_file(output);
  EXPECT_EQ(status, 0) << text.substr(text.size() > 3000 ? text.size() - 3000 : 0);
  EXPECT_NE(text.find("final OK !!!"), std::string::npos)
      << text.substr(text.size() > 3000 ? text.size() - 3000 : 0);
}

TEST_P(ConformanceTest, SqliteRunsAWorkloadExactlyAsGccsBuildDoes) {
  std::string sqlite = corpus + "/sqlite";
  if (!exists(sqlite + "/sqlite3.c"))
    GTEST_SKIP() << "no corpus: run python tests/conformance/fetch_corpus.py " << corpus;
  std::string fixtures = std::string(CONFORMANCE_DIR) + "/sqlite";
  std::string exe = program("sqltest");
  std::string log = work + "/sqlite.log";
  ASSERT_TRUE(compile(std::string(GetParam().windows ? "" : "-std=gnu99 ") +
                          "-O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -I" +
                          in_quotes(sqlite) + " " + in_quotes(fixtures + "/sqltest.c") + " " +
                          in_quotes(sqlite + "/sqlite3.c") + " -lm",
                      exe, log))
      << read_file(log);
  std::string output = work + "/sqlite.out";
  std::string workload = GetParam().windows ? in_quotes(fixtures + "/workload.sql")
                                            : "'" + wsl_path(fixtures + "/workload.sql") + "'";
  EXPECT_EQ(execute(work, exe, workload, output), 0);
  EXPECT_EQ(read_file(output), read_file(fixtures + "/expected.txt"));
}

static std::string numbered(const std::string &prefix, int count, const std::string &separator,
                            const std::string &suffix = "") {
  std::string out;
  for (int i = 0; i < count; i++)
    out += (i ? separator : "") + prefix + std::to_string(i) + suffix;
  return out;
}

static std::string limits_program() {
  std::string c = "#include \"include1.h\"\n#include <stdio.h>\n";
  for (int i = 0; i < 4095; i++)
    c += "#define MACRO" + std::to_string(i) + " " + std::to_string(i % 7) + "\n";
  for (int i = 0; i < 63; i++)
    c += "#if MACRO" + std::to_string(i) + " >= 0\n";
  c += "#define CONDITIONAL_DEPTH 63\n";
  for (int i = 0; i < 63; i++)
    c += "#endif\n";
  c += "#define ADD127(" + numbered("p", 127, ", ") + ") (" + numbered("p", 127, " + ") + ")\n";
  std::string line = "int long_line = 0";
  while (line.size() < 4095 - 5)
    line += " + 1";
  line += std::string(4095 - 1 - line.size(), ' ') + ";";
  c += line + "\n";
  c += "static const char text[] = \"" + std::string(2000, 'a') + "\"\n  \"" +
       std::string(2095, 'b') + "\";\n";
  std::string internal(62, 'i');
  c += "static int " + internal + "a = 1;\nstatic int " + internal + "b = 2;\n";
  std::string external(30, 'e');
  c += "int " + external + "a = 3;\nint " + external + "b = 4;\n";
  c += "int " + numbered("external", 4095, ", ") + ";\n";
  c += "static int " + std::string(12, '*') + "twelve;\n";
  c += "static int *(*(*(*mixed[2])[3])(void))[4];\n";
  c += "static int " + std::string(63, '(') + "parenthesized" + std::string(63, ')') + " = 5;\n";
  c += "static char object[65535];\n";
  c += "struct members { " + numbered("int m", 1023, " ", ";") + " };\n";
  c += "enum constants { " + numbered("E", 1023, ", ") + " };\n";
  std::string nested = "int deepest;";
  for (int i = 62; i >= 0; i--)
    nested = "struct level" + std::to_string(i) + " { " + nested + " } f" + std::to_string(i) + ";";
  c += "struct outer { " + nested + " };\n";
  c += "static int add127(" + numbered("int p", 127, ", ") + ") { return " +
       numbered("p", 127, " + ") + "; }\n";
  c += "static int choose(int k) {\n  switch (k) {\n";
  for (int i = 0; i < 1023; i++)
    c += "  case " + std::to_string(i) + ": return " + std::to_string(i * 3 % 1000) + ";\n";
  c += "  }\n  return -1;\n}\n";
  c += "static int nest(void) {\n  int depth = 0;\n";
  for (int i = 0; i < 127; i++)
    c += "{ depth++;\n";
  c += std::string(127, '}') + "\n  return depth;\n}\n";
  c += "int main(void) {\n";
  c += "  int b0 = 0";
  for (int i = 1; i < 511; i++)
    c += ", b" + std::to_string(i) + " = " + std::to_string(i);
  c += ";\n";
  c += "  struct members m;\n  struct outer o;\n";
  c += "  m.m1022 = 9;\n";
  std::string access = "o";
  for (int i = 0; i < 63; i++)
    access += ".f" + std::to_string(i);
  c += "  " + access + ".deepest = 63;\n";
  c += "  object[65534] = 1;\n  external4094 = 6;\n";
  c += "  twelve = 0;\n  mixed[1] = 0;\n";
  c += "  printf(\"%d %d %d %d\\n\", INCLUDE_DEPTH, CONDITIONAL_DEPTH, MACRO4094, ADD127(" +
       numbered("", 127, ", ") + "));\n";
  c += "  printf(\"%d %u %d %d\\n\", long_line, (unsigned)sizeof text - 1, text[1999], "
       "text[4094]);\n";
  c += "  printf(\"%d %d %d %d %d\\n\", " + internal + "a, " + internal + "b, " + external + "a, " +
       external + "b, external4094);\n";
  c += "  printf(\"%d %d %d\\n\", b0 + b510, " + std::string(63, '(') + "parenthesized" +
       std::string(63, ')') + ", nest());\n";
  c += "  printf(\"%d %u %d %d\\n\", add127(" + numbered("", 127, ", ") +
       "), (unsigned)sizeof object, object[65534], m.m1022);\n";
  c += "  printf(\"%d %d %d %d\\n\", E1022, choose(1022), choose(1023), " + access + ".deepest);\n";
  c += "  return twelve == 0 && mixed[0] == 0 ? 0 : 1;\n}\n";
  return c;
}

static std::string limits_expected() {
  int macro_sum = 0;
  for (int i = 0; i < 127; i++)
    macro_sum += i;
  int long_line = 0;
  std::string line = "int long_line = 0";
  while (line.size() < 4095 - 5) {
    line += " + 1";
    long_line++;
  }
  return "15 63 6 " + std::to_string(macro_sum) + "\n" + std::to_string(long_line) +
         " 4095 97 98\n1 2 3 4 6\n510 5 127\n" + std::to_string(macro_sum) + " 65535 1 9\n1022 " +
         std::to_string(1022 * 3 % 1000) + " -1 63\n";
}

TEST_P(ConformanceTest, OneProgramReachesEveryTranslationLimit) {
  std::string folder = work + "/limits_source";
  _mkdir(folder.c_str());
  for (int level = 1; level <= 15; level++) {
    std::ofstream header(folder + "/include" + std::to_string(level) + ".h", std::ios::binary);
    if (level < 15)
      header << "#include \"include" << level + 1 << ".h\"\n";
    else
      header << "#define INCLUDE_DEPTH 15\n";
  }
  std::string source = folder + "/limits.c";
  std::string program_text = limits_program();
  {
    std::ofstream out(source, std::ios::binary);
    out << program_text;
  }
  std::string exe = program("limits");
  std::string log = work + "/limits.log";
  ASSERT_TRUE(compile(in_quotes(source), exe, log)) << read_file(log);
  std::string output = work + "/limits.out";
  EXPECT_EQ(execute(work, exe, "", output), 0);
  EXPECT_EQ(read_file(output), limits_expected());
}

INSTANTIATE_TEST_SUITE_P(BothTargets, ConformanceTest,
                         testing::Values(Target{"windows", "x86_64-w64-windows-gnu", true},
                                         Target{"linux", "x86_64-pc-linux-gnu", false}),
                         [](const testing::TestParamInfo<Target> &info) {
                           return std::string(info.param.windows ? "Windows" : "Linux");
                         });
