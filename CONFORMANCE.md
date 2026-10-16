# MMC99 conformance

MMC99 is a hosted implementation of ISO/IEC 9899:1999 (C99, with its three technical corrigenda)
for two targets:

| Target | `--target=` | C library |
|---|---|---|
| Windows x64 | `x86_64-w64-windows-gnu` | MinGW-w64 (`msvcrt` and `mingwex`) |
| Linux x64 | `x86_64-pc-linux-gnu` | glibc |

C99 4p8 requires an implementation to document its implementation-defined and locale-specific
characteristics and its extensions. This is that document. Section numbers in brackets are C99's.
Where a choice was open, MMC99 follows what GCC does on the same target, so that objects built by
the two compilers link and behave alike.

The library half of the implementation is each target's C library. Behaviour C99 leaves to the
library (Annex J.3.12, J.4) is documented by MinGW-w64 and glibc, not here.

## How conformance is checked

The test suites in the source tree hold the evidence:

- **Valid programs.** c-testsuite's 220 programs, the Lua 5.4.9 test suite and an SQLite 3.54
  workload compiled and run on both targets (`ConformanceTests`). The programs MMC99 does not
  pass are listed in `tests/conformance/c-testsuite-exclusions.txt`. Each needs something outside
  C99, and the test fails if one of them starts passing.
- **Translation limits [5.2.4.1].** One generated program reaches every minimum at once and runs
  on both targets (`ConformanceTests`).
- **Required diagnostics [5.1.1.3].** Every violation of a syntax rule or a constraint produces
  an error. `ConstraintTest` in `SemaTests` breaks every sentence of every Constraints paragraph
  in clause 6, and the preprocessing rules of 6.10, in 237 small programs across 101 paragraphs.
  Each must produce its own diagnostic, and a valid twin of each must be accepted.
- **The ABI.** Objects built by MMC99 and by GCC call each other in both directions on both
  targets (`CodegenTests`).

## Translation [J.3.1]

- **Diagnostics** are written to the standard error stream as
  `file:line:column: error: message`. Every diagnostic is an error, and the translation unit
  that produced it is not translated further. MMC99 issues no warnings.
- **White space.** Each nonempty sequence of white-space characters other than new-line is kept
  as a single space where it separates tokens (5.1.1.2, phase 3). `-E` output reproduces that
  space; it does not keep the original run.

## Environment [J.3.2]

- **Source characters.** Source files are read as bytes. A byte maps to itself; there is no
  conversion of the source character set. Trigraphs are replaced. Wide literals are read as UTF-8.
- **`main`.** `int main(void)` and `int main(int argc, char *argv[])` are the portable forms;
  any other type is accepted and called by the C library's startup code as if it had one of
  them. Returning from `main` or reaching its closing brace returns its value, or 0, to the host.
- **Arguments, the environment and interactive devices** are the C library's.

## Identifiers [J.3.3]

- An identifier may contain universal character names. They must not name a code point below
  `00A0` other than `$`, `@` and `` ` ``, a surrogate, or a code point past `10FFFF`. Other
  multibyte characters are not part of identifiers.
- Every character of an identifier is significant, for internal and external names alike.

## Characters [J.3.4]

- A byte has 8 bits.
- **Execution character set.** A narrow string or character constant holds the source bytes
  between its quotes unchanged, so a UTF-8 source file gives UTF-8 strings.
- **`wchar_t`** is `unsigned short` holding UTF-16 code units on Windows, and `int` holding UTF-32
  on Linux. A wide literal is encoded accordingly; a character outside the Basic Multilingual Plane
  takes two units on Windows.
- **Plain `char`** is signed on both targets.
- **A character constant with more than one character** (`'ab'`) has GCC's value: the bytes
  form a big-endian integer, truncated to `int`. A wide one has the value of its last character.
- **An escape sequence** whose value does not fit its type is an error.
- **Character conversion** between source and execution sets is the identity, as above.

## Integers [J.3.5]

- The integer types have these widths (bits):

  | Type | Windows | Linux |
  |---|---|---|
  | `char`, `signed char`, `unsigned char`, `_Bool` | 8 | 8 |
  | `short` | 16 | 16 |
  | `int` | 32 | 32 |
  | `long` | 32 | 64 |
  | `long long` | 64 | 64 |
  | pointers, `size_t`, `ptrdiff_t` | 64 | 64 |

  `size_t` is `unsigned long long` on Windows and `unsigned long` on Linux; `ptrdiff_t` is the
  matching signed type.
- There are no extended integer types. `__int128`, which MinGW-w64's headers name through
  `mode(TI)`, is rejected if a program uses it.
- Signed integers are two's complement with no padding bits and no trap representations.
- Converting an integer to a signed type that cannot represent it keeps the low-order bits
  (wraps modulo 2^N). No signal is raised.
- `>>` of a negative value is an arithmetic shift. `&`, `|`, `^` and `~` act on the two's
  complement bits.
- A decimal constant too large for `long long` has no type and is an error (GCC accepts it as
  unsigned with a warning).
- The rank of every standard integer type is as C99 6.3.1.1 orders it; there is nothing else to
  rank.

## Floating point [J.3.6]

- `float` and `double` are IEEE 754 binary32 and binary64. `long double` is the x87 80-bit
  extended format, stored in 16 bytes with 16-byte alignment on both targets.
- `FLT_EVAL_METHOD` is 0: every operation is evaluated in its own type.
- `FLT_ROUNDS` is 1, round to nearest. It is a constant, as in GCC; it does not follow
  `fesetround`.
- Converting an integer to a floating type, or a floating value to a narrower floating type,
  rounds to nearest, ties to even, when the value is not exact. Converting a floating value to an
  integer truncates toward zero, as C99 requires.
- **Contraction** (`FP_CONTRACT`): expressions are never contracted.
- **`FENV_ACCESS`** is accepted and ignored, as GCC does without `-frounding-math`; code that
  changes the rounding mode or tests exception flags may see constant-folded results computed in
  the default mode.
- **`CX_LIMITED_RANGE`** is accepted; complex `*` and `/` are always computed with full range,
  through the same `libgcc` routines GCC calls.
- `__STDC_IEC_559__` and `__STDC_IEC_559_COMPLEX__` are not defined (Annexes F and G are not
  claimed). Imaginary types are not supported, which Annex G allows.
- `math_errhandling` is glibc's `MATH_ERRNO | MATH_ERREXCEPT` on Linux. MinGW-w64 defines
  none, so MMC99's `<math.h>` defines `MATH_ERRNO` on Windows. That is only mostly true:
  MinGW-w64's library sets `errno` in 16 of the 24 error cases measured.

## Arrays and pointers [J.3.7]

- Converting a pointer to an integer, or back, keeps its 64-bit value. Converting to a narrower
  integer keeps the low-order bits.
- Subtracting two pointers into the same array gives a `ptrdiff_t`; its range is never exceeded
  by an object MMC99 can create.

## Hints [J.3.8]

- `register` does not affect where an object is kept; it only forbids taking its address.
- `inline` makes a function a candidate for inlining. Whether a call is inlined is left to the
  optimizer (`-O1` and above); at `-O0` nothing is.

## Structures, unions, enumerations and bit-fields [J.3.9]

- A plain `int` bit-field is signed.
- Bit-fields may have any integer type, including `char`, `short`, `long long`, `_Bool` and
  enumerated types, as GCC allows. A `_Bool` bit-field may be up to 8 bits wide.
- **Allocation of bit-fields** follows each target's ABI, as GCC does: from the low-order bit of
  each storage unit upward.
  - On Windows (Microsoft layout), a bit-field shares a storage unit only with preceding
    bit-fields whose declared type has the same size, and never straddles a unit.
  - On Linux (System V layout), a bit-field starts where the previous one ended unless it would
    straddle a boundary of its own type's size.
- **Alignment** of members and records is the target ABI's. Under `#pragma pack(n)` no member is
  aligned to more than `n` bytes.
- **An enumerated type** is compatible with `unsigned int`, or with `int` if any of its
  enumeration constants is negative.

## Qualifiers [J.3.10]

- Each read or write of a `volatile` object named in the source is one access of the object's
  whole size, in program order with other `volatile` accesses.

## Preprocessing directives [J.3.11]

- **`#include <...>`** searches, in order: the folders named by `-I`, MMC99's own `include`
  folder, and the target's system folders.
- **`#include "..."`** first searches the folder of the file containing the directive, then as
  for `<...>`.
- The header name is used as a file name exactly as written; the host file system decides
  whether case matters. `#include` may nest 64 deep.
- A `#include` whose operand is not `"..."` or `<...>` is macro-expanded. In the `<...>` form the
  tokens' spellings are joined, with a space wherever one separated two tokens.
- A character constant in `#if` has the same value as in the program, and plain `char` is signed
  there too.
- **`#pragma`.** `STDC FP_CONTRACT`, `STDC FENV_ACCESS` and `STDC CX_LIMITED_RANGE` behave as
  described under floating point. `pack` is the extension described below. Every other pragma is
  ignored.
- `__DATE__` and `__TIME__` are always available, from the host clock.

## The library [J.3.12, J.4]

Every library function and macro, and every locale-specific behaviour, is the target C library's.
On both targets MMC99's own `<float.h>`, `<limits.h>`, `<stdarg.h>`, `<stddef.h>`, `<stdbool.h>`,
`<iso646.h>` and `<tgmath.h>` come first, with the values GCC gives.

## Architecture [J.3.13]

- Both targets are little-endian x86-64.
- The values in `<limits.h>`, `<float.h>` and `<stdint.h>` are those GCC uses for the target.
- The size and alignment of every type are the target ABI's.

## Translation limits [5.2.4.1]

MMC99 has no fixed limit below what memory allows, except `#include` nesting (64). The program in
`ConformanceTests` exceeds each minimum C99 sets: 127 nested blocks, 63 nested conditional
inclusions, 12 declarators modifying one type, 63 nested parenthesized declarators and
expressions, 63 and 31 significant characters in internal and external identifiers, 4095 external
identifiers, 511 identifiers in one block, 4095 macros, 127 parameters and arguments in functions
and macros, a 4095-character logical line and string literal, a 65535-byte object, 15 nested
`#include` files, 1023 `case` labels, members and enumeration constants, and 63 nested structure
definitions.

## Extensions [4p6, J.5]

Every extension below uses a name C99 reserves for the implementation (7.1.3), or is accepted
only inside a system header, so no strictly conforming program can tell it is there.

**Accepted anywhere:**

- `__attribute__((...))`, `__asm__` and `__extension__`, and the GNU spellings `__inline__`,
  `__const__`, `__signed__`, `__volatile__`, `__restrict__` (with and without trailing
  underscores). Most attributes are parsed and ignored. These have effect: `aligned`, `mode`
  (only on a `typedef`, which may not then be used), `gnu_inline`, `dllimport` and
  `returns_twice`. `packed` is an error.
- `__asm__("name")` after a declarator, which gives the object or function that symbol name.
- `__asm__` statements with GCC's operand syntax: outputs, inputs, clobbers and named operands
  (`[name]`), with the constraint letters the system headers use. File-scope `__asm__` and
  `asm goto` are not supported.
- Builtins: `__builtin_va_start`, `__builtin_va_arg`, `__builtin_va_end`, `__builtin_va_copy`,
  `__builtin_offsetof`, `__builtin_types_compatible_p`, `__builtin_choose_expr`,
  `__builtin_huge_val` (and `f`, `l`), `__builtin_inff`, `__builtin_nanf`, the classification
  comparisons `__builtin_isgreater` and its five relatives, `__builtin_signbit` (and `f`, `l`),
  `__builtin_llabs`, `__builtin_trap`, `__builtin_unreachable`, `__builtin_tgmath`,
  `__builtin_ia32_sfence`, the `__sync_` atomic operations (`fetch_and_` and `_and_fetch` with
  `add`, `sub`, `or`, `and`, `xor`, `nand`; `bool_compare_and_swap`, `val_compare_and_swap`,
  `lock_test_and_set`, `lock_release`, `synchronize`), `__umulh` and `__mulh`. `__typeof__` is
  accepted as a builtin's type argument.
- `__PRETTY_FUNCTION__` and `__FUNCTION__`, like `__func__`.
- `#pragma pack(n)`, `pack(push, n)`, `pack(pop)` and `pack()`, with GCC's meaning.
- `#include_next`.
- `defined` produced by macro expansion inside `#if` is evaluated as the operator, as GCC does
  (C99 6.10.1p4 leaves this undefined).
- Command-line `-std=gnu99`, which leaves `__STRICT_ANSI__` undefined so system headers declare
  their extras.
- Each target's GCC predefined macros (`_WIN32`, `__linux__`, `__x86_64__`, `__SIZE_TYPE__`, ...).
  `__GNUC__`, `__GNUC_MINOR__` and `__GNUC_PATCHLEVEL__` (4.2.1, the dialect MMC99 claims) are
  visible only inside system headers, so a program's own `#ifdef __GNUC__` does not assume GCC.

**Accepted only inside a system header**, because the targets' headers use them:

- an empty declaration (a lone `;`) at file scope;
- anonymous structure and union members, and a tagged structure defined inside another without
  declaring a member;
- an array of size zero;
- a `typedef` repeated with a compatible type;
- an enumeration constant whose value fits `unsigned int` but not `int`. It then has type
  `unsigned int`;
- an imaginary floating constant with an `i` or `j` suffix (`1.0iF`, which `<complex.h>` uses for
  `_Complex_I`). It has the complex type with a zero real part. A program writes `2.0 * I`.

In a program's own files each of these is an error, as C99 requires.

## Where MMC99 is stricter than GCC

Each of these is a constraint violation or undefined behaviour in C99 that GCC accepts, usually
with a warning. MMC99 has no warnings, so it reports an error:

- assigning or returning a pointer that discards qualifiers, or converting between a function
  pointer and `void *`;
- a forward declaration of an enumeration (`enum E;`);
- `void v;` at file scope, and an empty structure;
- `&x != 0` in a static initializer;
- a floating constant too large for its type (`1e999`), which GCC turns into infinity with a
  warning;
- a floating constant that does not fit its integer type in a constant expression;
- fewer than one argument for a variadic macro's `...`;
- `#pragma pack` without parentheses, and its named `push, id` and `pop, id` forms.

And where it is more lenient, because C99's text allows what GCC refuses:

- a `_Bool` bit-field may be up to 8 bits wide (C99 6.7.2.1p3; GCC applies C11's limit of 1);
- `int f(void, int);`, `int f(void x);` and `int f(const void);` are accepted as declarations
  (6.7.5.3p4 constrains only a definition's parameters);
- `static _Bool b = &x;` is accepted and is 1.
