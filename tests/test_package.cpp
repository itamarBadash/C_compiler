#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <process.h>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>

extern "C" {
#include "driver.h"
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
  return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::set<std::string> entries(const std::string &folder) {
  std::set<std::string> names;
  WIN32_FIND_DATAA found;
  HANDLE search = FindFirstFileA((folder + "\\*").c_str(), &found);
  if (search == INVALID_HANDLE_VALUE)
    return names;
  do {
    std::string name = found.cFileName;
    if (name != "." && name != "..")
      names.insert(name);
  } while (FindNextFileA(search, &found));
  FindClose(search);
  return names;
}

static std::string backslashed(std::string path) {
  for (char &c : path) {
    if (c == '/')
      c = '\\';
  }
  return path;
}

class PackageTest : public testing::Test {
protected:
  static std::string folder;
  static std::string root;

  static void SetUpTestSuite() {
    char temp[MAX_PATH + 1];
    GetTempPathA(sizeof temp, temp);
    folder = std::string(temp) + "mmc99-package-test-" + std::to_string(_getpid());
    std::system(("rmdir /s /q \"" + folder + "\" 2> nul").c_str());
    CreateDirectoryA(folder.c_str(), nullptr);
    std::string unzip = "C:\\Windows\\System32\\tar.exe -xf \"" + backslashed(PACKAGE_ZIP) +
                        "\" -C \"" + folder + "\"";
    ASSERT_EQ(std::system(unzip.c_str()), 0) << unzip;
    root = folder + "\\MMC99";
    CopyFileA(backslashed(std::string(PACKAGE_PROGRAMS_DIR) + "/check.c").c_str(),
              (folder + "\\check.c").c_str(), FALSE);
  }

  static void TearDownTestSuite() {
    std::system(("rmdir /s /q \"" + folder + "\" 2> nul").c_str());
  }

  static int clean(const std::string &command, std::string *output = nullptr) {
    std::string log = folder + "\\output.txt";
    std::string line = "set \"PATH=C:\\Windows\\System32\" && cd /d \"" + folder + "\" && " +
                       command + " > \"" + log + "\" 2>&1";
    int status = std::system(line.c_str());
    if (output)
      *output = read_file(log);
    return status;
  }

  static std::string mmc99() {
    return "\"" + root + "\\bin\\MMC99.exe\"";
  }
};

std::string PackageTest::folder;
std::string PackageTest::root;

TEST_F(PackageTest, TheZipHoldsExactlyTheLayout) {
  std::set<std::string> top = {"README.txt", "SOURCES.txt", "bin",
                               "include",    "licenses",    "targets"};
  EXPECT_EQ(entries(root), top);
  std::set<std::string> bin = {"MMC99.exe", "clang.exe", "ld.lld.exe"};
  EXPECT_EQ(entries(root + "\\bin"), bin);
  std::set<std::string> targets = {"x86_64-pc-linux-gnu", "x86_64-w64-windows-gnu"};
  EXPECT_EQ(entries(root + "\\targets"), targets);
  for (const std::string &target : targets) {
    std::set<std::string> parts = {"include", "lib"};
    EXPECT_EQ(entries(root + "\\targets\\" + target), parts) << target;
  }
}

TEST_F(PackageTest, ItNamesItsVersion) {
  std::string output;
  ASSERT_EQ(clean(mmc99() + " --version", &output), 0) << output;
  EXPECT_EQ(output.rfind("MMC99 " MMC99_VERSION ", ", 0), 0u) << output;
  EXPECT_NE(std::string(PACKAGE_ZIP).find("MMC99-" MMC99_VERSION "-windows-x86_64.zip"),
            std::string::npos);
  for (const char *text : {"README.txt", "SOURCES.txt"}) {
    std::string contents = read_file(root + "\\" + text);
    EXPECT_EQ(contents.rfind("MMC99 " MMC99_VERSION " - ", 0), 0u) << text;
    EXPECT_EQ(contents.find('@'), std::string::npos) << text << " has an unfilled placeholder";
  }
}

TEST_F(PackageTest, EveryLicenseItNamesIsShipped) {
  std::string sources = read_file(root + "\\SOURCES.txt");
  std::vector<std::string> named;
  for (size_t at = sources.find("licenses/"); at != std::string::npos;
       at = sources.find("licenses/", at + 1)) {
    size_t end = sources.find_first_of(" \n,()", at);
    named.push_back(sources.substr(at, end - at));
  }
  ASSERT_GE(named.size(), 8u) << sources;
  for (const std::string &path : named)
    EXPECT_TRUE(exists(root + "\\" + backslashed(path))) << path;
  std::map<std::string, std::set<std::string>> licenses = {
      {"LLVM", {"LICENSE-clang.TXT", "LICENSE-lld.TXT", "LICENSE-llvm.TXT"}},
      {"GCC", {"COPYING.RUNTIME", "COPYING3"}},
      {"MinGW-w64", {"crt", "headers", "manifest", "winpthreads"}},
      {"glibc", {"copyright"}},
      {"libgcc-linux", {"copyright"}},
      {"linux-libc-dev", {"copyright"}},
  };
  std::set<std::string> folders;
  for (const auto &license : licenses) {
    folders.insert(license.first);
    EXPECT_EQ(entries(root + "\\licenses\\" + license.first), license.second) << license.first;
  }
  EXPECT_EQ(entries(root + "\\licenses"), folders);
  for (const std::string &part : licenses["MinGW-w64"])
    EXPECT_FALSE(entries(root + "\\licenses\\MinGW-w64\\" + part).empty()) << part;
}

TEST_F(PackageTest, FilesThatAreNotMinGWArePutAside) {
  std::string windows = root + "\\targets\\x86_64-w64-windows-gnu";
  for (const char *name :
       {"\\lib\\libz.a", "\\lib\\libiconv.a", "\\include\\zlib.h", "\\include\\zconf.h"})
    EXPECT_FALSE(exists(windows + name)) << name;
  for (const char *name :
       {"\\lib\\crt2.o", "\\lib\\libgcc.a", "\\lib\\libws2_32.a", "\\include\\winsock2.h"})
    EXPECT_TRUE(exists(windows + name)) << name;
  std::string linux_target = root + "\\targets\\x86_64-pc-linux-gnu";
  for (const char *name : {"\\lib\\libc.so", "\\lib\\libm.so", "\\lib\\crt1.o", "\\lib\\libgcc.a",
                           "\\include\\unistd.h", "\\include\\sys\\types.h"})
    EXPECT_TRUE(exists(linux_target + name)) << name;
  for (const char *name : {"\\lib\\libm.a", "\\lib\\libasan.a", "\\include\\x86_64-linux-gnu"})
    EXPECT_FALSE(exists(linux_target + name)) << name;
}

TEST_F(PackageTest, AWindowsProgramBuildsAndRunsWithNothingOnPath) {
  std::string output;
  ASSERT_EQ(clean(mmc99() + " check.c -o check.exe", &output), 0) << output;
  EXPECT_EQ(clean(".\\check.exe", &output), 5);
  EXPECT_EQ(output, "5.0+5.0i 1.4142\n");
}

TEST_F(PackageTest, ALinuxProgramBuildsWithNothingOnPathAndRunsInWsl) {
  std::string output;
  ASSERT_EQ(clean(mmc99() + " --target=x86_64-pc-linux-gnu check.c -lm -o check", &output), 0)
      << output;
  EXPECT_EQ(clean("wsl ./check", &output), 5);
  EXPECT_EQ(output, "5.0+5.0i 1.4142\n");
}

TEST_F(PackageTest, ToolsAreFoundBesideTheCompilerNotOnPath) {
  std::string output;
  ASSERT_EQ(clean(mmc99() + " -v check.c -o verbose.exe", &output), 0) << output;
  std::string bin = root + "/bin/";
  EXPECT_NE(output.find(bin + "clang.exe -c -x ir "), std::string::npos) << output;
  EXPECT_NE(output.find(bin + "ld.lld.exe -m i386pep "), std::string::npos) << output;
}

static std::string captured(const std::string &command) {
  std::string out;
  FILE *pipe = _popen(command.c_str(), "rb");
  if (!pipe)
    return out;
  char buffer[4096];
  size_t count;
  while ((count = fread(buffer, 1, sizeof buffer, pipe)) > 0)
    out.append(buffer, count);
  _pclose(pipe);
  std::string text;
  for (char c : out) {
    if (c != '\r' && c != '\0')
      text += c;
  }
  return text;
}

static std::string wsl_path(const std::string &windows) {
  std::string path = captured("wsl wslpath -u \"" + windows + "\"");
  while (!path.empty() && path.back() == '\n')
    path.pop_back();
  return path;
}

class LinuxPackageTest : public testing::Test {
protected:
  static std::string folder;
  static std::string wsl_folder;
  static std::string unpacked;
  static std::string tarball;
  static std::string check;

  static void SetUpTestSuite() {
    if (std::string(LINUX_PACKAGE).empty() || !exists(LINUX_PACKAGE))
      return;
    char temp[MAX_PATH + 1];
    GetTempPathA(sizeof temp, temp);
    std::string id = std::to_string(_getpid());
    folder = std::string(temp) + "mmc99-linux-test-" + id;
    CreateDirectoryA(folder.c_str(), nullptr);
    wsl_folder = wsl_path(folder);
    unpacked = "/tmp/mmc99-linux-test-" + id;
    tarball = wsl_path(backslashed(LINUX_PACKAGE));
    check = wsl_path(backslashed(std::string(PACKAGE_PROGRAMS_DIR) + "/check.c"));
    std::string output;
    ASSERT_EQ(shell("rm -rf " + unpacked + " && mkdir -p " + unpacked + " && tar -xzf '" + tarball +
                        "' -C " + unpacked + " && cp '" + check + "' " + unpacked,
                    &output),
              0)
        << output;
  }

  static void TearDownTestSuite() {
    if (!unpacked.empty())
      shell("rm -rf " + unpacked);
    if (!folder.empty())
      std::system(("rmdir /s /q \"" + folder + "\" 2> nul").c_str());
  }

  void SetUp() override {
    if (unpacked.empty())
      GTEST_SKIP() << "no Linux download: run packaging/fetch_linux_llvm.py, then reload CMake";
  }

  static int shell(const std::string &script, std::string *output = nullptr,
                   const std::string &distribution = "") {
    std::string file = folder + "\\script.sh";
    std::string log = folder + "\\script.txt";
    {
      std::ofstream out(file, std::ios::binary);
      out << script << "\n";
    }
    std::string wsl = distribution.empty() ? "wsl" : "wsl -d " + distribution;
    int status = std::system((wsl + " sh '" + wsl_path(file) + "' > \"" + log + "\" 2>&1").c_str());
    if (output)
      *output = read_file(log);
    return status;
  }

  static std::string mmc99() {
    return "env -i " + unpacked + "/MMC99/bin/MMC99";
  }
};

std::string LinuxPackageTest::folder;
std::string LinuxPackageTest::wsl_folder;
std::string LinuxPackageTest::unpacked;
std::string LinuxPackageTest::tarball;
std::string LinuxPackageTest::check;

TEST_F(LinuxPackageTest, TheTarballHoldsExactlyTheLayoutWithItsProgramsExecutable) {
  std::string output;
  ASSERT_EQ(shell("cd " + unpacked +
                      "/MMC99 && LC_ALL=C ls -A | tr '\\n' ' ' && echo && LC_ALL=C ls -A bin | "
                      "tr '\\n' ' ' && echo && for f in bin/MMC99 bin/clang bin/ld.lld; do test "
                      "-x $f || echo not executable: $f; done && find . -type f -perm /111 ! "
                      "-path './bin/*' | head -3 && LC_ALL=C ls -A targets | tr '\\n' ' ' && echo",
                  &output),
            0)
      << output;
  EXPECT_EQ(output, "README.txt SOURCES.txt bin include licenses targets \n"
                    "MMC99 clang ld.lld \n"
                    "x86_64-pc-linux-gnu x86_64-w64-windows-gnu \n");
}

TEST_F(LinuxPackageTest, ItsTextsDescribeTheLinuxDownload) {
  std::string output;
  ASSERT_EQ(
      shell("cd " + unpacked + "/MMC99 && cat README.txt && echo ==== && cat SOURCES.txt", &output),
      0);
  std::string readme = output.substr(0, output.find("\n====\n"));
  std::string sources = output.substr(output.find("\n====\n") + 6);
  EXPECT_EQ(
      readme.rfind("MMC99 " MMC99_VERSION " - a C99 compiler for Linux and Windows programs\n", 0),
      0u)
      << readme;
  EXPECT_NE(readme.find("MMC99-" MMC99_VERSION "-linux-x86_64.tar.gz"), std::string::npos);
  EXPECT_EQ(sources.rfind("MMC99 " MMC99_VERSION " - ", 0), 0u) << sources;
  EXPECT_NE(sources.find("\nbin/MMC99\n"), std::string::npos) << sources;
  EXPECT_NE(sources.find("\nbin/clang, bin/ld.lld\n"), std::string::npos) << sources;
  EXPECT_NE(sources.find("stripped of their symbol tables"), std::string::npos) << sources;
  EXPECT_EQ(sources.find(".exe"), std::string::npos) << "no Windows program is in it";
  EXPECT_EQ((readme + sources).find('@'), std::string::npos) << "an unfilled placeholder";
}

TEST_F(LinuxPackageTest, ItNamesItsVersion) {
  std::string output;
  ASSERT_EQ(shell(mmc99() + " --version", &output), 0) << output;
  EXPECT_EQ(output.rfind("MMC99 " MMC99_VERSION ", a C99 compiler for ", 0), 0u) << output;
}

TEST_F(LinuxPackageTest, ALinuxProgramBuildsAndRunsWithAnEmptyEnvironment) {
  std::string output;
  ASSERT_EQ(shell("cd " + unpacked + " && " + mmc99() +
                      " check.c -lm -o check && ./check; echo \"exit $?\"",
                  &output),
            0)
      << output;
  EXPECT_EQ(output, "5.0+5.0i 1.4142\nexit 5\n");
}

TEST_F(LinuxPackageTest, AWindowsProgramBuiltOnLinuxRunsOnWindows) {
  std::string output;
  ASSERT_EQ(shell("cd " + unpacked + " && " + mmc99() +
                      " --target=x86_64-w64-windows-gnu check.c -o '" + wsl_folder +
                      "/from-linux.exe'",
                  &output),
            0)
      << output;
  std::string run = "cd /d \"" + folder + "\" && .\\from-linux.exe > from-linux.txt";
  EXPECT_EQ(std::system(run.c_str()), 5);
  EXPECT_EQ(read_file(folder + "\\from-linux.txt"), "5.0+5.0i 1.4142\n");
}

TEST_F(LinuxPackageTest, OnACleanDebianWithoutACompilerItBuildsAndRuns) {
  std::string distributions = captured("wsl -l -q");
  if (distributions.find("mmc99-clean\n") == std::string::npos)
    GTEST_SKIP() << "no clean Linux: install one with 'wsl --install -d Debian --name mmc99-clean'";
  std::string output;
  ASSERT_EQ(
      shell("for tool in gcc cc clang tcc; do command -v $tool > /dev/null && echo not clean: "
            "$tool; done; rm -rf /tmp/mmc99-clean && mkdir -p /tmp/mmc99-clean && cd "
            "/tmp/mmc99-clean && tar -xzf '" +
                tarball + "' && cp '" + check +
                "' . && env -i ./MMC99/bin/MMC99 check.c -lm -o check && ./check; echo "
                "\"exit $?\"",
            &output, "mmc99-clean"),
      0)
      << output;
  EXPECT_EQ(output, "5.0+5.0i 1.4142\nexit 5\n");
}
