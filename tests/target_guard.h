#ifndef TARGET_GUARD_H
#define TARGET_GUARD_H

extern "C" {
#include "target.h"
}

class TargetGuard {
public:
  explicit TargetGuard(target_kind kind) {
    target_select(kind);
  }
  ~TargetGuard() {
    target_select(TARGET_WINDOWS_X64);
  }
  TargetGuard(const TargetGuard &) = delete;
  TargetGuard &operator=(const TargetGuard &) = delete;
};

#endif
