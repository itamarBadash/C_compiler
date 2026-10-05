#include "target_guard.h"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

static std::vector<std::string> defined_names(const target *t) {
  std::vector<std::string> out;
  std::istringstream lines(t->predefines);
  std::string line;
  while (std::getline(lines, line)) {
    EXPECT_EQ(line.rfind("#define ", 0), 0u) << line;
    std::string name = line.substr(8);
    out.push_back(name.substr(0, name.find_first_of(" (")));
  }
  return out;
}

TEST(TargetTest, WindowsIsTheDefault) {
  const target *t = target_current();
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->long_size, 4);
  EXPECT_EQ(t->microsoft_bitfields, 1);
}

TEST(TargetTest, WindowsIsLlp64WithUtf16WideCharacters) {
  TargetGuard guard(TARGET_WINDOWS_X64);
  const target *t = target_current();
  EXPECT_EQ(t->long_size, 4);
  EXPECT_EQ(t->wchar_size, 2);
  EXPECT_EQ(t->wchar_type, PRIM_USHORT);
  EXPECT_EQ(t->size_type, PRIM_ULLONG);
  EXPECT_EQ(t->ptrdiff_type, PRIM_LLONG);
  EXPECT_EQ(t->microsoft_bitfields, 1);
  EXPECT_EQ(
      defined_names(t),
      std::vector<std::string>(
          {"_WIN32",     "_WIN64",        "__WIN32",          "__WIN32__",      "__WIN64",
           "__WIN64__",  "__WINNT",       "__WINNT__",        "__MINGW32__",    "__MINGW64__",
           "__MSVCRT__", "__SEH__",       "__x86_64",         "__x86_64__",     "__amd64",
           "__amd64__",  "__SIZE_TYPE__", "__PTRDIFF_TYPE__", "__WCHAR_TYPE__", "__WINT_TYPE__",
           "__cdecl",    "__stdcall",     "__fastcall",       "__thiscall",     "__declspec"}));
}

TEST(TargetTest, LinuxIsLp64WithUtf32WideCharacters) {
  TargetGuard guard(TARGET_LINUX_X64);
  const target *t = target_current();
  EXPECT_EQ(t->long_size, 8);
  EXPECT_EQ(t->wchar_size, 4);
  EXPECT_EQ(t->wchar_type, PRIM_INT);
  EXPECT_EQ(t->size_type, PRIM_ULONG);
  EXPECT_EQ(t->ptrdiff_type, PRIM_LONG);
  EXPECT_EQ(t->microsoft_bitfields, 0);
  EXPECT_EQ(defined_names(t),
            std::vector<std::string>({"__linux", "__linux__", "__gnu_linux__", "__unix", "__unix__",
                                      "__ELF__", "_LP64", "__LP64__", "__x86_64", "__x86_64__",
                                      "__amd64", "__amd64__", "__SIZE_TYPE__", "__PTRDIFF_TYPE__",
                                      "__WCHAR_TYPE__", "__WINT_TYPE__"}));
}

TEST(TargetTest, SelectingATargetSwitchesTheCurrentOne) {
  {
    TargetGuard guard(TARGET_LINUX_X64);
    EXPECT_EQ(target_current()->long_size, 8);
  }
  EXPECT_EQ(target_current()->long_size, 4) << "the guard puts Windows back";
}
