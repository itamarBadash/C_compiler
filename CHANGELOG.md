# Changelog

All notable changes to MMC99 are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and version numbers follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-10-15

The first release: a complete, hosted C99 compiler for Windows x64 and Linux x64.

### Added

- The whole of ISO C99 (with its three technical corrigenda): the preprocessor, every
  declaration, type, expression and statement, variable length arrays, complex numbers,
  designated initializers, `inline`, `restrict`, flexible array members, universal character
  names and the C99 standard headers.
- Two targets, `x86_64-w64-windows-gnu` and `x86_64-pc-linux-gnu`, each with its platform's
  data layout, bit-field layout and calling convention, so objects link with GCC's in both
  directions. Either target can be built from either host.
- A GCC-compatible command line: `-c`, `-S`, `-E`, `-o`, `-I`, `-D`, `-U`, `-L`, `-l`, `-Wl,`,
  `-pthread`, `-O0` to `-O3`, `-Os`, `-Oz`, `-Og`, `-std=c99`, `-std=gnu99`, `--target=` and
  `-v`. Warning, debug and PIC flags from existing build files are accepted.
- Self-contained downloads for Windows and Linux: LLVM's `clang` and `ld.lld`, MinGW-w64 and
  glibc headers and libraries, and GCC's runtime support are included, so nothing else needs to
  be installed.
- `windows.h` and the rest of MinGW-w64's and glibc's headers compile, through the GNU extensions
  those headers use (accepted only inside system headers where C99 would require a diagnostic).
- `CONFORMANCE.md`, documenting every implementation-defined behaviour and every extension, as
  C99 4p8 requires.
- A test suite of 1300 tests, including c-testsuite, Lua 5.4.9's own test suite and an SQLite
  3.54 workload on both targets, one program reaching every translation limit, and a case for
  every constraint in C99 clause 6.

[1.0.0]: https://github.com/itamarBadash/C_compiler/releases/tag/v1.0.0
