#include "target.h"
#include <stddef.h>

static const char *const windows_macros[] = {
    "_WIN32",     "_WIN64",    "__WIN32",     "__WIN32__",   "__WIN64",    "__WIN64__",
    "__WINNT",    "__WINNT__", "__MINGW32__", "__MINGW64__", "__MSVCRT__", "__x86_64",
    "__x86_64__", "__amd64",   "__amd64__",   NULL};

static const char *const linux_macros[] = {
    "__linux",  "__linux__", "__gnu_linux__", "__unix",  "__unix__",  "__ELF__", "_LP64",
    "__LP64__", "__x86_64",  "__x86_64__",    "__amd64", "__amd64__", NULL};

static const target targets[] = {
    [TARGET_WINDOWS_X64] = {.long_size = 4,
                            .wchar_size = 2,
                            .wchar_type = PRIM_USHORT,
                            .size_type = PRIM_ULLONG,
                            .ptrdiff_type = PRIM_LLONG,
                            .microsoft_bitfields = 1,
                            .macros = windows_macros},
    [TARGET_LINUX_X64] = {.long_size = 8,
                          .wchar_size = 4,
                          .wchar_type = PRIM_INT,
                          .size_type = PRIM_ULONG,
                          .ptrdiff_type = PRIM_LONG,
                          .microsoft_bitfields = 0,
                          .macros = linux_macros},
};

static target_kind selected = TARGET_WINDOWS_X64;

void target_select(target_kind kind) {
  selected = kind;
}

const target *target_current(void) {
  return &targets[selected];
}
