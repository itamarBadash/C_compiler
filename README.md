# MMC99

A complete C99 compiler for Windows x64 and Linux x64.

MMC99 compiles ISO/IEC 9899:1999 (C99, with its three technical corrigenda) to LLVM IR, then uses
LLVM's `clang` to produce machine code and `ld.lld` to link. It ships as one self-contained
download per host: unpack it and compile, with nothing else to install.

```console
$ MMC99 hello.c -o hello
$ ./hello
hello, world
```

- **All of C99**, including variable length arrays, `_Complex`, designated initializers,
  compound literals, `inline`, `restrict`, flexible array members, `<tgmath.h>` and universal
  character names.
- **Two targets from either host.** Programs for Windows (`x86_64-w64-windows-gnu`) and Linux
  (`x86_64-pc-linux-gnu`) are built from Windows or from Linux, with `--target=`.
- **ABI-compatible with GCC.** Data layout, bit-fields and calling conventions follow each
  platform's ABI, so objects built by MMC99 and by GCC link together in both directions.
- **A GCC-style command line**, so existing makefiles and build scripts work.
- **The real system headers.** Programs use MinGW-w64's C library on Windows (`windows.h`
  included) and glibc on Linux.
- **Every implementation-defined behaviour documented** in [CONFORMANCE.md](CONFORMANCE.md).

## Contents

- [Requirements](#requirements)
- [Installation](#installation)
  - [Windows](#windows)
  - [Linux](#linux)
- [Usage](#usage)
- [Conformance](#conformance)
- [Limitations](#limitations)
- [Building from source](#building-from-source)
- [Running the tests](#running-the-tests)
- [How it works](#how-it-works)
- [Project layout](#project-layout)
- [License](#license)

## Requirements

| | Windows download | Linux download |
|---|---|---|
| Runs on | Windows 10 or 11, x64 | x86-64 Linux with glibc 2.34 or newer, `libstdc++` and `zlib` (Ubuntu 22.04, Debian 12 and later have all three) |
| Disk space | 370 MB unpacked (a 100 MB download) | 560 MB unpacked (a 165 MB download) |
| Builds programs for | Windows x64 and Linux x64 | Linux x64 and Windows x64 |

Linux programs that MMC99 builds need glibc 2.34 or newer on the machine that runs them.
No compiler, SDK or other toolchain needs to be installed.

## Installation

Each release on the [releases page](https://github.com/itamarBadash/C_compiler/releases) has
three files:

| File | Contents |
|---|---|
| `MMC99-1.0.0-windows-x86_64.zip` | the compiler for Windows hosts |
| `MMC99-1.0.0-linux-x86_64.tar.gz` | the compiler for Linux hosts |
| `SHA256SUMS` | the SHA-256 checksum of each archive |

### Windows

1. **Download** `MMC99-1.0.0-windows-x86_64.zip` and `SHA256SUMS`.

2. **Verify the download.** In PowerShell, in the download folder, print the archive's
   checksum and compare it with the line for the zip in `SHA256SUMS`:

   ```powershell
   (Get-FileHash .\MMC99-1.0.0-windows-x86_64.zip -Algorithm SHA256).Hash.ToLower()
   Get-Content .\SHA256SUMS
   ```

3. **Unpack it** anywhere. This guide uses `C:\Tools`, which gives `C:\Tools\MMC99`:

   ```powershell
   Expand-Archive .\MMC99-1.0.0-windows-x86_64.zip -DestinationPath C:\Tools
   ```

4. **Add it to `PATH`** for your user, so `MMC99` works from any folder:

   ```powershell
   $bin = "C:\Tools\MMC99\bin"
   $path = [Environment]::GetEnvironmentVariable("Path", "User")
   [Environment]::SetEnvironmentVariable("Path", "$path;$bin", "User")
   ```

   Close and reopen the terminal. (Alternatively: Settings → System → About → Advanced system
   settings → Environment Variables → `Path` → New.)

5. **Check it:**

   ```powershell
   MMC99 --version
   ```

**Uninstall:** delete `C:\Tools\MMC99` and remove its entry from `Path`. MMC99 writes nothing
outside its own folder, except the output files you ask for and temporary files it removes.

### Linux

1. **Download** `MMC99-1.0.0-linux-x86_64.tar.gz` and `SHA256SUMS`.

2. **Verify the download:**

   ```sh
   sha256sum --check --ignore-missing SHA256SUMS
   ```

   It must print `MMC99-1.0.0-linux-x86_64.tar.gz: OK`.

3. **Unpack it.** For all users, into `/opt`:

   ```sh
   sudo tar -xzf MMC99-1.0.0-linux-x86_64.tar.gz -C /opt
   ```

   Or for yourself only, without `sudo`:

   ```sh
   mkdir -p ~/.local/opt && tar -xzf MMC99-1.0.0-linux-x86_64.tar.gz -C ~/.local/opt
   ```

4. **Put it on `PATH`.** Either link the program into a folder already on `PATH`:

   ```sh
   sudo ln -s /opt/MMC99/bin/MMC99 /usr/local/bin/MMC99
   ```

   or add its folder to your shell's start-up file:

   ```sh
   echo 'export PATH="$HOME/.local/opt/MMC99/bin:$PATH"' >> ~/.bashrc
   ```

   The link works because MMC99 finds its files from its real location, not from the link's.

5. **Check it:**

   ```sh
   MMC99 --version
   ```

**Uninstall:** remove the `MMC99` folder and the link or `PATH` line you added.

## Usage

MMC99 takes the same options as GCC for everything it supports. `MMC99 --help` lists them.

```sh
MMC99 hello.c -o hello                 # compile and link
MMC99 main.c list.c util.c -o app      # several source files into one program
MMC99 -c list.c                        # compile only, to list.o
MMC99 main.o list.o -o app             # link objects
MMC99 -O2 app.c -o app                 # optimize (-O0 to -O3, -Os, -Oz, -Og)
MMC99 -E app.c                         # preprocess only, to standard output
MMC99 -S app.c                         # stop at LLVM IR, written to app.ll
MMC99 -I include -D DEBUG=1 app.c      # header folders and macros
MMC99 app.c -lm -o app                 # link a library (Linux needs -lm for <math.h>)
MMC99 -v hello.c                       # show the clang and ld.lld commands it runs
```

| Option | Meaning |
|---|---|
| `-o <file>` | write the output to `<file>` |
| `-c` | compile to an object file; do not link |
| `-S` | compile to LLVM IR (`.ll`); do not assemble |
| `-E` | preprocess only |
| `-I <dir>` | add a header search folder |
| `-D <name>[=<value>]`, `-U <name>` | define or undefine a macro |
| `-L <dir>`, `-l <name>` | add a library folder; link a library |
| `-Wl,<a>,<b>` | pass options to the linker |
| `-pthread` | define `_REENTRANT` and link with `-lpthread` |
| `-std=c99` | ISO C99 (the default) |
| `-std=gnu99` | C99 without `__STRICT_ANSI__`, so system headers declare their extras (POSIX functions, `M_PI`, ...) |
| `-O0` to `-O3`, `-Os`, `-Oz`, `-Og` | optimization level |
| `--target=<triple>` | `x86_64-w64-windows-gnu` or `x86_64-pc-linux-gnu`; the default is the host |
| `-v` | print each command it runs |
| `--help`, `--version` | print help or the version |

Flags that build files commonly pass (`-Wall`, `-Wextra`, `-Werror`, `-w`, `-pedantic`, `-g`,
`-fPIC`, `-fPIE` and friends) are accepted and have no effect. Other `-f` options are rejected,
because some of them change what a program means.

### Cross-compiling

Every download builds for both targets:

```sh
MMC99 --target=x86_64-w64-windows-gnu hello.c -o hello.exe   # a Windows program, from Linux
```

```powershell
MMC99 --target=x86_64-pc-linux-gnu hello.c -o hello          # a Linux program, from Windows
```

### Diagnostics

Errors are reported as `file:line:column: error: message`, and the exit status is non-zero.
MMC99 has no warnings: everything it reports is an error.

## Conformance

MMC99 aims to be a conforming hosted C99 implementation, and the claim is backed by tests:

- **Real programs.** c-testsuite's 220 programs, Lua 5.4.9's own test suite and an SQLite 3.54
  workload compile and run correctly on both targets. The 13 c-testsuite programs that need
  something beyond C99 (C11 features, GCC extensions) are listed with reasons in
  [tests/conformance/c-testsuite-exclusions.txt](tests/conformance/c-testsuite-exclusions.txt).
- **Required diagnostics.** Every constraint in C99 clause 6 and every preprocessing rule has a
  test program that must be rejected with its own message, alongside a valid twin that must be
  accepted (237 cases over 101 paragraphs).
- **Translation limits.** One generated program reaches every minimum of C99 5.2.4.1 at once
  (127 nested blocks, 4095 macros, 1023 `case` labels, ...) and runs on both targets with GCC's
  output.
- **The ABI.** Programs split between MMC99 and GCC objects run correctly on both targets.

[CONFORMANCE.md](CONFORMANCE.md) is the document C99 4p8 requires: every implementation-defined
and locale-specific behaviour, every extension, and the places MMC99 is stricter or more lenient
than GCC.

## Limitations

- **C99 only.** C11 and later features (`_Generic`, `_Static_assert`, anonymous structures in
  your own code, `_Alignof`, ...) and GNU extensions (statement expressions, `typeof`,
  `__attribute__((packed))`, ...) are rejected with an error. System headers may use the GNU
  syntax they need.
- **No warnings.** Every diagnostic is an error.
- **No debug information.** `-g` is accepted but produces none.
- **No shared libraries.** MMC99 builds executables and object files.
- **`#pragma STDC FENV_ACCESS ON`** is accepted and ignored, as in GCC without
  `-frounding-math`.
- **x86-64 only.** There are no other targets.

## Building from source

The build runs on Windows and produces the compiler, its tests and both downloads.

**Prerequisites**

| Tool | Used for |
|---|---|
| CMake 4.0 or newer, and Ninja | the build |
| MinGW-w64 GCC (the one bundled with CLion works) | compiling MMC99 and the tests |
| LLVM 23.1.3, installed in `C:\Program Files\LLVM` | the `clang` and `ld.lld` the Windows download ships |
| WSL with an Ubuntu distribution, `gcc`, `libc6-dev` and `linux-libc-dev` | the Linux target's headers and libraries, and the Linux build of MMC99 |
| Python 3.14 | fetching LLVM's Linux tools for the Linux download |

**Steps**

```sh
git clone https://github.com/itamarBadash/C_compiler.git
cd C_compiler
cmake -S . -B cmake-build-debug -G Ninja
cmake --build cmake-build-debug
```

That builds the compiler, every test binary and the Windows download. The ready-to-use compiler
is in `cmake-build-debug/MMC99`, laid out exactly as the download.

For the Linux download, fetch LLVM's Linux tools once (about 1.1 GB, checked against its
SHA-256 and stripped), then configure again:

```sh
python packaging/fetch_linux_llvm.py cmake-build-debug/llvm-23.1.3-linux
cmake -S . -B cmake-build-debug
```

**Making a release**

```sh
cmake --build cmake-build-debug --target release
```

This builds both downloads and puts them, with `SHA256SUMS`, in `release/<version>/`. The
version comes from `MMC99_VERSION` in [include/driver.h](include/driver.h). The `release` folder
is not committed; its contents are what gets attached to a GitHub release.

## Running the tests

```sh
ctest --test-dir cmake-build-debug
```

Or run one suite, from the build folder:

```sh
cd cmake-build-debug
./SemaTests.exe
./SemaTests.exe --gtest_filter='ConstraintTest.*'
```

| Suite | What it covers |
|---|---|
| `LexerTests`, `PreprocessorTests`, `ParserTests`, `AstTests`, `SymbolTableTests`, `SemaTests` | the front end, phase by phase |
| `TargetTests`, `HeaderTests` | the two targets and the system headers |
| `CodegenTests` | LLVM IR, and real programs compared with GCC's output on both targets |
| `DriverTests` | the command line, tools and linking |
| `PackageTests` | the real downloads, unpacked and run with nothing on `PATH` |
| `ConformanceTests` | c-testsuite, Lua, SQLite and the translation limits |

`ConformanceTests` needs the outside programs, downloaded once at pinned versions and hashes:

```sh
python tests/conformance/fetch_corpus.py cmake-build-debug/corpus
```

Without them those tests are skipped. Some suites run programs for Linux in WSL, and
`PackageTests` uses a clean WSL distribution named `mmc99-clean` when one exists
(`wsl --install -d Debian --name mmc99-clean --no-launch`).

## How it works

```
source.c → preprocessor → parser → semantic analysis → IR generator → source.ll
         → clang -c -x ir → source.o → ld.lld (with the target's C library) → program
```

1. **Preprocessor** (`src/preprocessor.c`): translation phases 1 to 4, with macros, `#include`,
   conditional inclusion, `#line`, `#pragma` and `_Pragma`.
2. **Parser** (`src/parser.c`): a hand-written recursive-descent parser producing an AST.
3. **Semantic analysis** (`src/sema.c`): scopes, types, every constraint check, implicit
   conversions, record layout and constant evaluation, recording everything code generation
   needs.
4. **IR generator** (`src/codegen.c`): textual LLVM IR, with each target's calling convention.
5. **Driver** (`src/driver.c`): the command line, and the `clang` and `ld.lld` invocations with
   each platform's startup files and libraries.

## Project layout

| Path | Contents |
|---|---|
| `src/`, `include/` | the compiler |
| `lib/include/` | MMC99's own headers (`<stddef.h>`, `<stdarg.h>`, `<float.h>`, `<limits.h>`, `<tgmath.h>`, ...) |
| `tests/` | the test suites; `tests/sysroot/` holds copies of the system headers they use |
| `packaging/` | download assembly scripts, README templates and LLVM's licenses |
| `CONFORMANCE.md` | implementation-defined behaviour and extensions |
| `ROADMAP.md` | the project's design history and the C99 coverage inventory |
| `CHANGELOG.md` | changes by version |

## License

MMC99 is released under the [MIT License](LICENSE).

The downloads also contain third-party software, each under its own license, which is shipped
in the download's `licenses` folder. `SOURCES.txt` there says where every file comes from.

| Component | License |
|---|---|
| LLVM `clang` and `ld.lld` | Apache License 2.0 with LLVM Exceptions |
| MinGW-w64 headers and runtime | MinGW-w64's licenses (public domain, ZPL 2.1 and MIT-style) |
| GCC runtime support (`libgcc`, `crt*.o`) | GPL 3 with the GCC Runtime Library Exception |
| glibc headers and link files | LGPL 2.1 or later |
| Linux kernel headers | GPL 2 with the syscall note |

Programs you build with MMC99 are yours to license as you choose. The runtime files linked into
them come with terms that allow this: the GCC Runtime Library Exception, glibc's LGPL with its
exception for startup files, and MinGW-w64's permissive licenses. Each license in the `licenses`
folder has the details.
