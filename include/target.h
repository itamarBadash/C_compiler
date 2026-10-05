#ifndef _TARGET_H_
#define _TARGET_H_
#include "ast.h"

typedef enum target_kind { TARGET_WINDOWS_X64, TARGET_LINUX_X64 } target_kind;

typedef struct target {
  int long_size;
  int wchar_size;
  prim_kind wchar_type;
  prim_kind size_type;
  prim_kind ptrdiff_type;
  int microsoft_bitfields;
  const char *predefines;
  const char *builtin_declarations;
} target;

void target_select(target_kind kind);
const target *target_current(void);

#endif
