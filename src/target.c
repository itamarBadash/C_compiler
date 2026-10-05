#include "target.h"

static const char windows_predefines[] = "#define _WIN32 1\n"
                                         "#define _WIN64 1\n"
                                         "#define __WIN32 1\n"
                                         "#define __WIN32__ 1\n"
                                         "#define __WIN64 1\n"
                                         "#define __WIN64__ 1\n"
                                         "#define __WINNT 1\n"
                                         "#define __WINNT__ 1\n"
                                         "#define __MINGW32__ 1\n"
                                         "#define __MINGW64__ 1\n"
                                         "#define __MSVCRT__ 1\n"
                                         "#define __SEH__ 1\n"
                                         "#define __x86_64 1\n"
                                         "#define __x86_64__ 1\n"
                                         "#define __amd64 1\n"
                                         "#define __amd64__ 1\n"
                                         "#define __SIZE_TYPE__ long long unsigned int\n"
                                         "#define __PTRDIFF_TYPE__ long long int\n"
                                         "#define __WCHAR_TYPE__ short unsigned int\n"
                                         "#define __WINT_TYPE__ short unsigned int\n"
                                         "#define __cdecl __attribute__((__cdecl__))\n"
                                         "#define __stdcall __attribute__((__stdcall__))\n"
                                         "#define __fastcall __attribute__((__fastcall__))\n"
                                         "#define __thiscall __attribute__((__thiscall__))\n"
                                         "#define __declspec(x) __attribute__((x))\n";

static const char linux_predefines[] = "#define __linux 1\n"
                                       "#define __linux__ 1\n"
                                       "#define __gnu_linux__ 1\n"
                                       "#define __unix 1\n"
                                       "#define __unix__ 1\n"
                                       "#define __ELF__ 1\n"
                                       "#define _LP64 1\n"
                                       "#define __LP64__ 1\n"
                                       "#define __x86_64 1\n"
                                       "#define __x86_64__ 1\n"
                                       "#define __amd64 1\n"
                                       "#define __amd64__ 1\n"
                                       "#define __SIZE_TYPE__ long unsigned int\n"
                                       "#define __PTRDIFF_TYPE__ long int\n"
                                       "#define __WCHAR_TYPE__ int\n"
                                       "#define __WINT_TYPE__ unsigned int\n";

static const char windows_builtins[] = "typedef char *__builtin_va_list;\n";

static const char linux_builtins[] = "typedef struct __va_list_tag {\n"
                                     "  unsigned int gp_offset;\n"
                                     "  unsigned int fp_offset;\n"
                                     "  void *overflow_arg_area;\n"
                                     "  void *reg_save_area;\n"
                                     "} __builtin_va_list[1];\n";

static const target targets[] = {
    [TARGET_WINDOWS_X64] = {.long_size = 4,
                            .wchar_size = 2,
                            .wchar_type = PRIM_USHORT,
                            .size_type = PRIM_ULLONG,
                            .ptrdiff_type = PRIM_LLONG,
                            .microsoft_bitfields = 1,
                            .predefines = windows_predefines,
                            .builtin_declarations = windows_builtins},
    [TARGET_LINUX_X64] = {.long_size = 8,
                          .wchar_size = 4,
                          .wchar_type = PRIM_INT,
                          .size_type = PRIM_ULONG,
                          .ptrdiff_type = PRIM_LONG,
                          .microsoft_bitfields = 0,
                          .predefines = linux_predefines,
                          .builtin_declarations = linux_builtins},
};

static target_kind selected = TARGET_WINDOWS_X64;

void target_select(target_kind kind) {
  selected = kind;
}

const target *target_current(void) {
  return &targets[selected];
}