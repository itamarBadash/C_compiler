# C_compiler — roadmap and completion data

Forward-looking plan. Historical detail (preprocessor step log, known ceilings,
build commands, working agreement) stays in `CLAUDE.md`.

Target: **LLVM textual IR** (`.ll`). No hand-written assembly, no `llvm-c`
dependency. Output you can read and diff.

**Goal: a complete C99 compiler** (ISO/IEC 9899:1999), hosted, for two
targets: `x86_64-w64-windows-gnu` and `x86_64-linux-gnu` (P2.2). Not a subset.
"Done" for any component is measured against the whole standard, including
what it puts on the implementation: headers, predefined macros and each
target's ABI. See "Full C99 — what exists and what is missing" below.

**Distribution: self-contained** (decided 2026-10-08). A user installs nothing
but this compiler. Its download carries everything after the IR generator:
LLVM's code generator and linker (`lld`), the headers, and each target's
startup files and static libraries. The C library the finished program calls
at run time is the system's own (Windows' DLL, Linux's `libc.so.6`). The
alternative, our own code generator, assembler and linker, was weighed and
rejected: twice the work and `-O0`-quality output, in exchange for owning every
byte. LLVM installed on the development machine (P0.2) is for tests only.

Last verified: 2026-10-15, branch `25-3d`, after P4 (uncommitted, on top of `27095b9`).
Everything up to and including P3 is committed; this file is untracked.

---

## Completion data

| Component | File | LOC | Tests | State |
|---|---|---|---|---|
| Lexer | `src/lexer.c` | 898 | 59 | complete; one sorted C99 keyword table; digraphs; form feed and vertical tab as whitespace; preprocessing numbers and one number classifier; one literal decoder for multi-character, wide and UCN literals; tokens carry a file and a `#line` offset |
| Parser | `src/parser.c` | 2741 | 226 | complete; anonymous members and named `asm` operands from system headers, and a hash index for large scopes (P5); the builtins and the target's `va_list` declaration; `aligned`, `mode` and the `#pragma pack` stamp recorded; owned `token_buf`; the initializer-list grammar (non-empty, `=` after a designator, closing brace); a non-empty translation unit; one declaration parser for file, block, member and old-style parameter declarations; ordinary names hide typedef names; block items distinct from statements; definitions inside type names and parameter lists; `[*]`, `static` and qualifiers in array declarators; K&R definitions; canonical arithmetic types; declaration specifiers on declaration nodes; a source location on every node; declaration groups distinct from blocks; defining specifiers linked to their definition; array sizes left to sema; a prototype is a declaration and a definition keeps its whole function type |
| AST | `src/ast.c` | 334 | 26 | complete; every node can record its symbol; conversion nodes own what they convert; declarations and compound literals own their initializer layout; casts, compound literals and function types own the definitions they contain |
| Symbol table | `src/symbol_table.c` | 201 | 26 | sema's store: owns symbols until destroy, current-scope lookups, anonymous tags; large scopes hashed (P5) |
| Preprocessor | `src/preprocessor.c` | 2629 | 496 | complete; a hashed macro table, `__GNUC__` seen only by system headers, `defined` produced inside `#if`, `#line`'s range (P5); `-D`/`-U` read as a `<command line>` file, and every token from a macro records its outermost invocation for `-E` (P4); `#include_next`; `#pragma pack` and `_Pragma("pack(...)")`; output reaches the parser; trigraphs; every `#define`, `#undef` and conditional-directive constraint diagnosed; lexer errors counted; keywords are macro names; every token knows its file and presumed line |
| Semantic analysis | `src/sema.c` | 4717 | 231 | anonymous members, the `__sync` builtins, `__umulh`, and what only system headers may do (P5); a case for every constraint sentence (`ConstraintTest`); scopes (including whole selection and iteration statements), name resolution, `__func__`, linkage, tags and typedef names, integer constant expressions, enumeration values, array sizes, typed function symbols, type compatibility, redeclaration, definition and tentative-definition checks, variably modified types, definition parameters, struct and union layout with Microsoft and System V bit-fields, `#pragma pack`, `aligned` and member constraints, expression types and the operand constraints of constants, names, unary `+ - ~ !`, casts, `sizeof`, the binary operators, `?:` and the comma operator, lvalues (`& * [] . -> ++ --`, assignments and compound literals) with modifiable-lvalue and assignment-conversion checks, calls checked against prototypes, statement constraints (conditions, `switch` and its labels, `break`/`continue`, `return`), initializers (brace elision, designators, strings, sizing arrays of unknown size, constant initializers for static storage), declarator constraints (array elements and sizes, function results, `restrict`), object completeness, inline definitions, storage-class, `inline` and `for`-declaration constraints; diagnostics name their file; no caller outside tests. Every implicit conversion is recorded as a conversion node, with the computation type of compound assignment, `++` and `--`, and bit-fields promoted as GCC does (P2.4a). Everything IR generation reads is recorded (P2.4a–c): conversions, initializer layouts, static and `case` values, the headers' declaration facts, inline definitions, exported sizes and VLA sizes |
| Targets | `src/target.c` | 89 | 4 | Windows x64 and Linux x64 (P2.2): `long`, `size_t`, `ptrdiff_t`, `wchar_t` and wide literals, Microsoft and System V bit-fields, each system's predefined macros; one target per run |
| Headers | `lib/include/` | 12 files | 9 | complete for both targets: our freestanding headers, `tgmath.h` and a `math.h` wrapper, in front of each system's own; all 24 C99 headers and every macro pass the front end, values checked against GCC (`HeaderTests`, over copies in `tests/sysroot/`) |
| IR generation (LLVM IR) | `src/codegen.c` | 3014 | 152 | the scalar core (P3a): integer, floating and pointer operators, every conversion node, `volatile`, assignment, `&&` `\|\|` `?:`, every statement, locals and their initializers, arrays, string literals, calls with and without prototypes, K&R definitions. Aggregates and data (P3b): structures and unions, bit-fields on both layouts, globals and their static initializers with relocations, linkage, `static` locals, `__func__`, compound literals, asm labels, `dllimport`, inline definitions, VLAs with their stack given back on every exit. Complex numbers, the builtins, `asm` and `setjmp` (P3c). Both calling conventions, variadic definitions and the `va_` builtins, internal functions emitted only when used (P3d); complete for C99 on both targets: fifteen programs run with GCC's output on both, and objects link with GCC's in both directions. Atomics, `__umulh` and `sfence` for the system headers (P5) |
| Driver | `src/driver.c`, `src/host.c`, `src/main.c` | 758 | 50 | complete (P4), and `windows.h` programs build (P5): `-E` `-S` `-c` `-o` `-I` `-D` `-U` `-L` `-l` `-Wl,` `-pthread` `-O0..3` `-Os` `-Oz` `-Og` `--target` `-std=c99`/`gnu99` `-v`, several inputs, `.o`/`.a` to the linker, and the warning, debug and PIC flags build files pass accepted without effect; IR to objects with `clang -c -x ir`, linking with `ld.lld` and GCC's measured file lists on both targets; every tool, header and runtime file found from the executable's own folder; error counts and exit codes; `host.c` holds what differs between Windows and Linux |
| Download | `packaging/`, `CMakeLists.txt` | — | 13 | MMC99 1.0.0 for Windows and Linux, with `CONFORMANCE.md` (P4.1, P5) |
| Conformance | `tests/test_conformance.cpp` | — | 8 | c-testsuite, Lua's own suite, an SQLite workload and every translation limit, through the built compiler on both targets (P5) |

**1300 tests, all passing** (verified by running each binary, not by reading notes), the
clean-Debian package test included.
`CodegenTests` needs `C:/Program Files/LLVM/bin/clang.exe` and WSL's `gcc` for its thirty
program tests and six cross-linking tests. `DriverTests` runs the driver in-process against
`cmake-build-debug/MMC99`, which CMake assembles as exactly the download's contents (LLVM's `clang`
and `ld.lld`, CLion's MinGW-w64, glibc's files copied out of WSL, the headers and licenses); its
Linux programs run in WSL. `PackageTests` unzips the real download and runs it with nothing on
`PATH`. `ConformanceTests` needs the corpus that `tests/conformance/fetch_corpus.py` downloads.
`SemaTests` parses source, runs `sema_check`, and compares the captured `stderr`
diagnostics exactly.
`SymbolTableTests` links with `--wrap=free` and asserts symbol lifetime by
counting frees.
`ParserTests` includes 8 preprocessor → parser bridge tests, 15
`TypeSpecifierTest` tests covering every C99 6.7.2p2 spelling, 16
`DeclSpecifierTest` tests for storage classes, `inline`, typedefs and union
definitions, and 10 `SourceLocationTest` tests including a walk asserting every
node has a location; `AstTests` includes 4 token-ownership tests that count
frees per allocation.

~15,400 lines of C in `src/` and ~24,300 lines of tests. **Complete: MMC99 1.0.0** (2026-10-15).
Unzipping `MMC99-1.0.0-windows-x86_64.zip` anywhere gives `MMC99 hello.c -o hello.exe` for both
targets with nothing else installed, and the Linux download works the same way on Linux. P5
checked the whole against an outside corpus, every constraint, every translation limit, and
wrote down every choice C99 leaves open (`CONFORMANCE.md`).

Every component is wired: preprocessor → parser → sema → IR generator → `clang -c` →
`ld.lld`, all run by the driver (P4).

---

## Development toolchain

LLVM 23.1.3 installed 2026-10-08 (`winget install LLVM.LLVM`, P0.2), in
`C:/Program Files/LLVM/bin`: `clang`, `ld.lld`, `lld-link`, no `llc`. WSL needs
nothing new: Windows `clang` writes the Linux object file and WSL's `gcc` links
it.

```bash
clang -c --target=x86_64-w64-windows-gnu out.ll -o out.o
gcc out.o -o out.exe                      # CLion's MinGW gcc
clang -c --target=x86_64-linux-gnu out.ll -o out_linux.o
wsl gcc out_linux.o -o out_linux          # WSL's gcc
```

Checked the same day: `ld.lld -m i386pep` links a Windows program from MinGW's
`crt2.o`, `crtbegin.o`/`crtend.o`, `-lmingw32 -lgcc -lmingwex -lmsvcrt
-lkernel32` with no `gcc` at all, and it runs with only `System32` on `PATH`.
That is the link P4 and P4.1 rely on.

This is test scaffolding. The finished compiler never calls a `gcc` or an
LLVM found on the machine; it calls the copies in its own download (P4, P4.1).

---

## Full C99 — what exists and what is missing

First audited 2026-09-18 by running whole programs through preprocessor → parser → sema with
a scratch driver; the table below is kept current as steps land (last 2026-10-15, after P3).
The evidence list is the original audit, kept as history: every gap it names was closed by
P2.1–P2.3, and `CodegenTests`' `stdio` program now compiles the real `<stdio.h>` on both
targets.

**Evidence.**
- A realistic self-contained program went through with zero diagnostics:
  linked lists, VLAs and VLA parameters, designated initializers, compound
  literals, bit-fields in a union, `_Complex`, K&R definitions, `a[static 3]`,
  hex floats, `signal`-style declarators, wide/narrow string concatenation,
  `extern inline`, a flexible array member, `_Pragma`.
- A file of six constraint violations: five diagnosed as GCC
  `-std=c99 -pedantic-errors` does. The miss is `int g() {}` followed by
  `int g(int);` (P2.1).
- `#include <stdio.h>` fails. Without MinGW's directories no header is found at
  all; with them, `_mingw.h` stops at `#error Only Win32 target is supported`
  (no `_WIN32`), `vadefs.h` at `#error VARARGS not implemented for this
  compiler` (no `__GNUC__`), and GCC's own headers at `#include_next`.
  Predefining `_WIN32`, `_WIN64`, `__MINGW32__`, `__MINGW64__` and `__GNUC__`
  by hand gets `stdio.h`, `stdlib.h` and `string.h` through the preprocessor
  cleanly; the parser then rejects 889 places, led by `__attribute__` (98),
  `__extension__` (46), `__inline__` (14), `__declspec` (11) and
  `__builtin_va_list`.

**Inventory, by part of the standard.**

| Part of C99 | Clause | State | Missing → step |
|---|---|---|---|
| Phases 1–3: character mapping, trigraphs, splicing, preprocessing tokens, comments | 5.1.1.2 | done | — |
| Phase 4: directives, macros, `#include`, `_Pragma` | 6.10 | done (P2.1) | — |
| Phases 5–6: escapes, string concatenation | 5.1.1.2, 6.4.5 | done | — |
| Lexical elements | 6.4 | done (P2.1) | Annex D letters in UCN identifiers are semantics, not a constraint; not required |
| Scopes, linkage, types, compatibility | 6.2 | done (P2.1) | two prototypes' parameter types are not merged into a composite; see `CLAUDE.md` |
| Conversions | 6.3 | every one recorded as a conversion node (P2.4a) | — |
| Expressions | 6.5 | typed, every constraint checked (P2.1) | — |
| Constant expressions | 6.6 | integer, arithmetic (floating included) and address constants evaluated and checked, against GCC on both targets (P2.4c) | — |
| Declarations | 6.7 | done (P2.1) | — |
| Statements | 6.8 | done (P2.1) | — |
| External definitions | 6.9 | done (P2.1) | a use inside a non-constant `sizeof` does not count; see `CLAUDE.md` |
| Predefined macros | 6.10.8 | the standard's seven, each target's system macros (P2.2) and the GCC 4.2 claim the headers test (P2.3a) | — |
| Freestanding headers: `<float.h>` `<iso646.h>` `<limits.h>` `<stdarg.h>` `<stdbool.h>` `<stddef.h>` `<stdint.h>` | 4p6, 7 | ours in `lib/include/` except `<stdint.h>`, which is the library's; values checked against GCC (P2.3e) | — |
| Hosted headers: the other 17 of C99's 24 | 7 | each system's (MinGW-w64, glibc), through the GNU syntax and builtins they need; `<tgmath.h>` and a `<math.h>` wrapper are ours (P2.3) | — |
| The library itself | 7 | the system's: MinGW's `msvcrt` + `mingwex`, glibc | nothing to write; shipping it is P4.1 |
| Phase 7 output: code generation | 5.1.1.2 | LLVM textual IR for every construct, both targets (P3), turned into objects by the driver with `clang -c -x ir` (P4) | — |
| Platform ABI: sizes, alignment, record layout | J.3 | done in sema for both targets (LLP64 and Microsoft bit-fields; LP64 and System V bit-fields; `long double` 16/16), checked against each target's GCC (P2.2) | — |
| Platform ABI: calling convention | — | Win64 and System V, checked by linking with GCC's objects in both directions (P3d) | — |
| Program termination: `main` reaching its `}` returns 0 | 5.1.2.2.3 | done (P3a) | — |
| Standard pragmas `FP_CONTRACT`, `FENV_ACCESS`, `CX_LIMITED_RANGE` | 6.10.6, 7.6.1 | decided (P3c): never contract, always full range, `FENV_ACCESS` ignored as GCC does | honouring `FENV_ACCESS ON` is a recorded ceiling |
| Phase 8: linking, the driver | 5.1.1.2 | done (P4): `ld.lld` with GCC's startup files and libraries on both targets | shipping those files → P4.1 |
| Conformance against an outside corpus | — | c-testsuite, Lua's suite and SQLite pass on both targets, minus 13 listed exclusions that need more than C99 (P5) | — |
| A diagnostic for every constraint, as a whole | 5.1.1.3 | audited 2026-09-19 (362 cases), every gap fixed in P2.1; kept as `ConstraintTest` (237 cases over 101 paragraphs, P5) | — |
| Translation limits | 5.2.4.1 | all 22 in one generated program, run on both targets (P5) | — |
| A document of every implementation-defined characteristic and extension | 4p8 | `CONFORMANCE.md`, shipped in both downloads (P5) | — |

Not required by C99, and so not on this list: warnings (5.1.1.3 requires a
diagnostic only for syntax and constraint violations), error recovery quality,
Annex F (`__STDC_IEC_559__` stays undefined), Annex G.

---

## Priority-ordered steps

Strict dependency order. Each component is built fully before the next; there
are no intermediate "see something working" builds. The first real use of the
compiler is when it is finished. Scaffolding exists only for testing.

Each step follows the loop in `CLAUDE.md`: Claude explains → Claude gives exact
instructions → Itamar implements → Claude reviews, tests, mutation-tests and
does the cosmetic pass in one handback → report.

```
source → preprocessor → parser → AST → sema → IR generator → .ll → clang -c → .o → ld.lld → .exe
└──────────────────────── done (P1–P4): the driver runs every stage ────────────────────────┘
                                                     P4.1 packages it as one download
                                                     P5 conformance runs on the finished download
```

### P0.1 — Resolve the `symbol` / `scope` / `symbol_kind` name collision
**Status:** done 2026-09-10 (Step 1) · **Blocks:** every step from P2 on

`include/parser.h` and `include/symbol_table.h` both declare, at file scope:

| Name | `parser.h` | `symbol_table.h` |
|---|---|---|
| `symbol_kind` | `{SYMBOL_TYPEDEF, SYMBOL_STRUCT_TAG, SYMBOL_ENUM_TAG}` | `{SYMBOL_VAR, SYMBOL_FUNC, SYMBOL_TYPEDEF, SYMBOL_ENUM_CONSTANT, SYMBOL_TAG, SYMBOL_LABEL}` |
| `struct symbol` | 3 fields | 8 fields |
| `struct scope` | 2 fields | 5 fields |

`SYMBOL_TYPEDEF` exists in both with **different values** (0 vs 2). This
compiles today only because no translation unit includes both headers.
Semantic analysis includes both by definition.

Fix: prefix the parser's private stub types (`parser_symbol_kind`,
`parser_symbol`, `parser_scope`, `PARSER_SYMBOL_*`). The stub stays — replacing
it with the real table is P2 work, not this step.

Contained: 16 lines in `src/parser.c`, 9 in `include/parser.h`, **zero** in any
test file.

**Done when:** a TU including both headers compiles, and all 587 tests pass.

---

### P0.2 — Install LLVM
**Status:** done 2026-10-08 · **Blocks:** P3's execution tests only

See "Development toolchain" above. Not a coding step. Development only: a
user of the finished compiler never installs LLVM (see "Distribution" at the
top).

`winget install LLVM.LLVM` (the official installer) ships `clang` and `lld`
but historically not `llc`; `clang -c out.ll` does the same job. The release
page's `clang+llvm-*-x86_64-pc-windows-msvc.tar.xz` archive has `llc` too.
Which one P4.1 ships is decided there.

**Done when:** a hand-written `.ll` becomes a running `.exe` on Windows and a
running program in WSL.

---

### P1 — Unify the token source (preprocessor → parser bridge)
**Status:** done 2026-09-13 (Step 2) · **Depends on:** nothing

**Outcome:** `struct parser` embeds a `token_buf` by value and is its only
owner. `parser_init_from_buf` *moves* the caller's buffer — copy, rewind
`pos`, zero the source — so the caller's `token_buf_free` is a no-op in either
order. Eager draining changed no existing test. The EOF push in `parser_init`'s
drain is deliberately redundant (`token_buf_next` synthesizes EOF) and survives
its mutation by design. `ParserTests` and `AstTests` now link `token_buf.c`;
`ParserTests` also links `preprocessor.c`.

Today the parser pulls from a `lexer*`; the preprocessor emits a `token_buf`.
Two token sources, no connection.

The coupling is remarkably small. `p->lex` appears on exactly **three lines** of
`src/parser.c`, all inside `parser_advance` (1062, 1067) and `parser_init`
(1072). Everything else in the parser goes through `parser_advance`.

Plan: give `parser` an owned `token_buf` as its single token source.
- `parser_init(parser*, lexer*)` **keeps its signature** — it drains the lexer
  into an owned `token_buf`. All 119 parser tests and the `ParserTest` fixture
  are untouched.
- Add `parser_init_from_buf(parser*, token_buf*)` for the preprocessor path.
- `parser_advance` calls `token_buf_next` instead of `lexer_next_token`.

Side effects to decide during the step, not now:
- Draining eagerly surfaces every lex error *before* parsing starts, rather than
  interleaved. Check whether any of the 37 lexer or 119 parser tests observe the
  interleaving.
- Ownership: who frees the drained buffer, and does `token_buf` own its token
  `value` strings.

Bonus, not a goal: an owned buffer makes arbitrary lookahead possible, which C's
declaration-vs-expression ambiguity eventually wants.

**Done when:** `pp_run` output feeds `parse_program` with no lexer in between,
and all 587 tests still pass.

---

### P2 — Semantic analysis
**Status:** checking done (Step 17, 2026-09-17); P2.1 closes its C99 gaps and
P2.4 records what IR generation needs · **Depends on:** P0.1, P1 (both done)

Steps continue the working-loop numbering (Steps 1–2 were P0.1 and P1). The
first three fix what the parser hands sema — real representation bugs found by
reading, not scaffolding:

- **Step 3 — arithmetic type specifiers.** *Done 2026-09-14.* `type_info`
  stores a canonical `prim_kind` instead of the first keyword seen; `base_type`
  and `is_long_long` are gone. Every C99 6.7.2p2 combination is validated,
  missing type specifiers are diagnosed rather than defaulting to `int`, and
  unions get `TYPE_UNION`. Recovery reports one diagnostic and continues with
  the named type or `int`. 15 `TypeSpecifierTest` tests; 23 mutants, 22 killed,
  and M17 (an unobservable free on an error path) survives by design.
- **Step 4 — declaration specifiers leave `type_info`.** *Done 2026-09-14.*
  `decl_specs` (storage class and `inline`) lives on `VAR_DECL` and
  `FUNCTION_DEF`; `type_info` and `var_decl.is_typedef` lost their copies.
  `typedef` is handled as a storage class, so it may appear anywhere in the
  specifiers. At most one storage class (6.7.1p2); parameters allow only
  `register` (6.7.5.3p2); type names allow neither storage classes nor `inline`.
  `typedef int f(int);` is a typedef `VAR_DECL` with an intact function type,
  every name in a file-scope typedef list is registered, and union definitions
  carry `is_union`. 16 `DeclSpecifierTest` tests; 18 mutants, all killed.
- **Step 5 — source locations.** *Done 2026-09-14.* Every AST node carries a
  `source_loc` (line and column, both from 1), set when the parser builds it:
  operators at their operator; calls, subscripts and member access at their
  punctuator; statements at their keyword; declarations at their declarator
  name; definitions at their keyword; synthetic grouping blocks and designator
  wraps at the construct they belong to. `parse_declarator` reports the name's
  position through `out_name_loc`. Types carry no location. 10
  `SourceLocationTest` tests, including a walk asserting every node is located;
  36 mutants, all killed (one needed a test added).
- **Step 6 — the symbol table becomes sema's store.** *Done 2026-09-15.* The
  table owns every symbol from insertion until `symbol_table_destroy` through an
  ownership list (`all_symbols` / `all_next`); `leave_scope` frees only the
  scope, so a symbol leaving scope stops being found without being freed. Every
  symbol is made by one `calloc` helper, `new_symbol`, and carries its
  declaration's `source_loc`. Inserts return the new `symbol*` and never judge
  validity — duplicates, the file-scope merge and tag redeclaration are sema's —
  and `lookup_ordinary_current` / `lookup_tag_current` tell redeclaration from
  shadowing. `linkage`, `is_tentative` and `is_defined` stay as fields for sema
  to set. Sema links identifier uses to symbols rather than to declaration
  nodes, because parameters and enumerators are not nodes. `SymbolTableTests`
  rewritten (19 → 23) with a `--wrap=free` counter that asserts lifetime by
  counts; 21 mutants, 19 killed by an attributed assertion, and the two
  `calloc` → `malloc` mutants are caught only by heap contents or a crash.
- **Step 7 — scopes and name resolution.** *Done 2026-09-15.* New `sema.c` with one entry point,
  `sema_check(symbol_table*, ast_node*)`. Walks the AST with C99's scopes: file;
  function, which the parameters share with the body's outermost block; block;
  prototype; the `for` statement's own block; and every substatement block
  (6.8.4p3, 6.8.5p5). Declares objects, functions, typedefs, parameters,
  enumeration constants and labels, computes linkage (6.2.2), and records on
  every identifier, declaration, label and `goto` node the symbol it resolved
  to. Diagnoses undeclared identifiers and labels, duplicate labels, and
  redeclarations in one scope where either declaration has no linkage (6.7p3).
  Declarations with linkage in the same scope share one symbol; checking them
  against each other is Step 8. First fixes what the parser hands it:
  declaration groups (`int a, b;`) become `AST_NODE_TYPE_DECL_GROUP` instead of
  a `BLOCK`, which sema would read as a scope. Linkage moved here from Step 8
  because the 6.7p3 check needs it. Outcome: `DECL_GROUP` is built in
  `parse_var_decl` and both branches of `parse_top_level_declaration`, leaving
  `parse_block` as the only builder of `BLOCK`. Every node has a `symbol` field,
  and `FUNCTION_DEF` has `param_symbols`. New `SemaTests` target with 24 tests,
  including a walk asserting that every identifier, declaration, label and
  `goto` in a valid program is resolved and no struct member is; plus 1
  `DeclarationGroupTest` and 2 `AstTests`. 44 mutants across `sema.c`,
  `parser.c` and `ast.c`, all killed, each by its intended test failing an
  assertion when run alone.
- **Step 8 — names inside types.** *Done 2026-09-15.* Resolve the two kinds of name a type can
  contain: struct, union and enum tags (6.7.2.3), and typedef names (6.7.7).
  Every type that names one records its symbol; every tag definition and
  forward declaration records its tag symbol; tag symbols record their kind and
  the definition that completes them. Diagnoses unknown type names, a
  non-typedef name used as a type, tag redefinition, a tag used with the wrong
  keyword, and `enum E` before `E` is complete. Parser fixes it needs: a type
  specifier that defines a struct, union or enum keeps a link to that
  definition (an anonymous type has no other link), and block-scope `struct S;`
  builds a forward declaration instead of an empty group. Moved ahead of type
  compatibility: two struct types in one translation unit are compatible only
  if they are the same declared type, so compatibility written before tags
  could only compare names. Outcome: `type_info` has `definition` (parser,
  borrowed) and `symbol` (sema); `symbol` has `tag_kind` and `definition`, with
  `is_defined` meaning complete for tags. `symbol_table_insert_tag` accepts a
  NULL name as an anonymous tag that the table owns but no scope lists. Every
  struct, union and enum type now has exactly one tag symbol, so later steps
  can treat "same type" as "same tag symbol". `parse_forward_tag` serves both
  file and block scope. 38 `SemaTests` (14 new, and the invariant walk now
  covers definitions and every named type), 2 `TagDeclarationTest` tests, 1 new
  `SymbolTableTests` test. 29 mutants, all killed, each by its intended test
  failing an assertion when run alone.
- **Step 9 — integer constant expressions.** *Done 2026-09-16.* The target's integer types (LLP64
  widths and ranks), the integer promotions and usual arithmetic conversions
  (6.3.1.1, 6.3.1.8), the types and values of integer, character and cast
  floating constants (6.4.4), and an evaluator for integer constant expressions
  (6.6). The evaluator distinguishes three outcomes: a value; a constant
  expression with no value (an evaluation error, or a `sizeof` this step cannot
  size); and not a constant. First consumers: enumeration constant values and
  each enumerated type's compatible integer type (6.7.2.2), and the size of
  every array declarator (positive when constant; a non-constant size marks a
  VLA). The parser stops computing `array_size`. `sizeof` sizes scalars,
  pointers, enums and arrays; structs and unions join with layout, and
  expressions join with expression typing. Ahead of compatibility, because
  array types are compatible only when their constant sizes are equal. Outcome:
  `symbol` has `enum_type`, `has_value` and `value`; `type_info.array_size` is
  `long long` and `is_vla` separates a VLA from a constant size not computable
  yet. Review found three bugs in the step's own code, each caught by a test:
  `sizeof` of a type that had already failed to resolve reported a second error;
  `value_fits` took bare bits, so 18446744073709551615 "fitted" `long long` as
  -1 (it now takes a typed value); and `(_Bool)0.5` truncated before comparing
  with zero. 56 `SemaTests` (18 new, 2 updated), 1 `ArrayDeclaratorTest`, and 12
  parser assertions moved from parsed sizes to size expressions. 53 mutants: 52
  killed by an attributed assertion, 1 equivalent (see `CLAUDE.md`).
- **Step 10 — a function's type in one piece.** *Done 2026-09-16.* `FUNCTION_DEF` holds the whole
  function type instead of four fields taken from it, and means a definition
  only: a prototype goes through the declaration-list branch like any other
  declaration, which fixes `int f(void), x;` and takes `is_variadic` off the
  return type. Function symbols get their type. A declarator declares a function
  when its type resolves to one, so `F f;` with `typedef int F(void);` declares
  a function, and an initializer on a function declaration is an error
  (6.7.8p3). Split from compatibility, which needs every function type in the
  same shape. Outcome: implemented exactly as specified (the step was applied
  to a scratch copy and measured before hand-over, and the file matched it). 18
  parser tests and 2 sema tests still treated a prototype as a `FUNCTION_DEF`;
  reading `function_def` out of what is now a `VAR_DECL` crashed two of them
  instead of failing. 4 `FunctionDeclarationTest`, 5 `FunctionDefinitionTest`
  and 1 `AstTests` test added; 8 mutants, all killed by an attributed assertion.
- **Parser and lexer holes.** *Done 2026-09-17.* Every hole listed below that
  belonged to the lexer or parser, done by Claude at Itamar's request rather
  than as a step document. The lexer has one sorted keyword table with `auto`
  (gperf and `lex_keywords.c` are gone), reads C99 preprocessing numbers, and
  exports one number classifier and one literal decoder that the parser, sema
  and `#if` all share. Multi-character constants take GCC's values, wide
  literals are UTF-16, UCNs work in literals and identifiers, and literals end
  at a newline. The parser has one declaration parser for file, block, member
  and old-style parameter declarations. It records ordinary names so they hide
  typedef names, in the same scopes sema opens. Declarations are block items,
  not statements, and a declaration must declare something. Definitions may
  appear inside casts, `sizeof`, compound literals and parameter lists, which
  now own them. It also parses `[*]`, `static` and qualifiers in array
  declarators, abstract function declarators, and K&R definitions with
  `has_prototype`. Compound literals take postfix operators, and a missing `)`
  or `]` is reported. Sema opens the whole-statement scopes and checks
  `auto`/`register` at file scope, block-scope function storage classes,
  `inline` on non-functions and `for` declarations; its diagnostics, like the
  parser's and the lexer's, name the file. Tokens and `source_loc` carry an
  interned file name, and `#line` moves the file and line of every following
  token. `DESIGNATOR` is gone. Found on the way: a `#define` parameter list
  ran onto the next line. Tests rewritten with evidence were the ones that
  pinned a hole (a multi-character constant as an error, a declaration as a
  substatement, `enum E;` as valid), or that reached sema only through one, or
  that counted the removed `presumed_name` frees. 717 → 768 tests. 226
  mutants: 225 killed by an attributed assertion and 1 equivalent
  (`utf8_decode`'s `lead < 0xC2`). Five first survived or were caught only by
  other tests, and each exposed a missing test case, now added: an empty string,
  an enumerator hiding a typedef at a statement start, a substatement scope
  ending before `else` and before `do`'s condition, the first token after
  `#line`, and a header included twice. One kill (`￿` canonicalized as
  `\U`) fails by assertion in the full run and crashes alone, because the mutant
  overflows the identifier buffer.
- **Step 11 — declarations that share a symbol.** *Done 2026-09-17.* Type
  compatibility (6.2.7, 6.7.3p9, 6.7.5.1-3, 6.7.2.2p4), with parameters
  adjusted and default argument promotions for unprototyped types. Every
  declaration with linkage is checked against an earlier one, in its scope or
  any other, for kind, linkage (6.2.2p7) and compatible type; a compatible
  redeclaration that completes an array size or supplies a prototype becomes
  the symbol's type. One definition per object or function (6.9p3, p5),
  tentative definitions (6.9.2) with incomplete types rejected at the
  declaration for internal linkage and at the end of the file otherwise, and
  variably modified types only at block scope without linkage and never as a
  static VLA or a member (6.7.5.2p2). `typedef void V; int f(V);` has no
  parameters, and the definition-parameter checks (names, 6.9.1p5; complete
  types, 6.7.5.3p4) moved from the parser to sema so they see through
  typedefs. Review found one bug in the step's own code, flagged as a risk in
  the document: `int f(void); int f(V) {}` reported conflicting types because
  `(V)` was removed only after `declare`; it is now removed by a lookup first.
  10 `SemaTests` tests; 5 parser rows became clean-parse rows. 104 mutants:
  102 killed by an attributed assertion; P4 and I3 exposed conditions that
  cannot change a result and were removed from the code, along with three more
  found by reading before the run. R4 exposed a missing case (a conflicting
  redeclaration must not complete the type), now added.
- **Step 12 — struct and union layout.** *Done 2026-09-17.* One `layout_of`
  gives the size and alignment of every complete type (complex types are two
  of their real type, which `sizeof` had wrong), and `sizeof` reads it. Every
  struct and union is laid out at its closing brace: tag symbols carry
  `size`, `alignment` (0 means no layout) and `has_flexible_member`, members
  carry `offset`, `bit_offset` and `bit_width`. Bit-fields follow the
  Microsoft layout that `x86_64-w64-mingw32` GCC and clang use; every
  integer type and enum may be a bit-field, as on that GCC. Checked against
  that GCC on 12,000 random records and 30 layout rows generated from it.
  Diagnoses bit-field types and widths (6.7.2.1p3-4), duplicate members,
  incomplete and function members, flexible array members out of place, a
  struct ending in one used as a member, and records with no named member.
  2 `SemaTests` rewritten because they pinned "structs have no size"; 5
  `LayoutTest` tests added. 67 mutants: 66 killed by an attributed assertion,
  and R12 (`layout.alignment = 1`) is equivalent but kept, because without it
  two other mutants divide by zero before any assertion can report. Three
  more conditions that cannot change a result were removed.
- **Step 13 — expression types.** *Done 2026-09-17.* Every expression node has
  `expr_type`: constants, string literals and names, unary `+ - ~ !`, casts,
  `sizeof`, every binary operator, `?:` and the comma operator. Operands go
  through lvalue conversion and decay (`value_type`), integer promotions and
  the usual arithmetic conversions with floating and complex types, and each
  operator checks its 6.5 constraint, including pointer arithmetic,
  comparisons and null pointer constants. Parameters are declared with their
  adjusted pointer types. `sizeof` misuse is reported wherever it appears and
  `sizeof` of an expression is a constant. Derived types are owned by the
  `PROGRAM` node and freed shallowly; arithmetic types are shared statics.
  The document's C3 anchor, "immediately before `resolve`", also matched the
  forward declaration at the top of `sema.c`, where the section landed; moved
  at review, and the file then matched the verified version exactly. Checked
  against GCC on 68 snippets (two mismatches need Steps 14 and 15). 2 tests
  rewritten because `sizeof x` now has a value; 8 `ExpressionTypeTest` tests
  and 1 `AstTests` test added. 99 mutants, all killed by an attributed
  assertion after two missing cases (qualifiers written on a typedef name, for
  a parameter and for a `?:` target), one case the intended test lacked
  (`const` on the left of `==`), and one mutant rewritten because it only
  looped forever. Two conditions that could not change a result were removed.
- **Step 14 — lvalues.** *Done 2026-09-17.* `is_lvalue` on every expression and
  types for `&`, unary `*`, `[]`, `.`, `->`, prefix and postfix `++`/`--`, every
  assignment operator and compound literals. Modifiable lvalues (6.3.2.1p1,
  including `const` members at any depth), the assignment conversions of
  6.5.16.1 in one `conversion_problem` for Steps 15-17 to reuse, compound
  assignment (6.5.16.2), member lookup recorded on `member_access.member`,
  qualifiers carried from a record to its members and through arrays to
  their elements (which removed Step 13's one-level ceiling), `&` of a
  bit-field or `register` object, `sizeof` of a bit-field, and compound
  literal types. Checked against GCC on 74 snippets; the one mismatch needs
  calls. Implemented as specified; Block B's placement did not name
  `sema.c`. 1 test rewritten because its untyped operands now have types;
  6 `LvalueTest` tests added. 84 mutants, all killed by an attributed
  assertion after one missing case (`const` on a member declared with a
  typedef of an array) and one mutant rewritten because it only crashed.
  Five conditions that could not change a result were removed.
- **Step 15 — function calls.** *Done 2026-09-17.* Calls have their function's
  unqualified return type. The callee must be a pointer to a function returning
  `void` or a complete object type; a prototype's argument count is checked
  (at least the named parameters when variadic); each argument with a
  parameter goes through `conversion_problem` against the parameter's adjusted,
  unqualified type; an argument without one needs a complete object type.
  Checked against GCC on 40 snippets with no mismatch. Implemented as
  specified. 1 test rewritten because the last untyped operand, a call, now has
  a type; 5 `CallTest` tests added. 25 mutants, all killed by an attributed
  assertion after two missing cases (a `const struct` parameter, and a short
  call to an old-style definition); one mutant that could only read past the
  parameter array was dropped. One redundant `void` check was removed.
- **Step 16 — statements.** *Done 2026-09-17.* Scalar conditions for `if`,
  `while`, `do` and `for`; an integer controlling expression for `switch`,
  whose case labels are integer constant expressions compared after conversion
  to the promoted controlling type, with at most one `default`, and labels only
  inside a switch (a stack of switch contexts, so labels belong to the
  innermost); `break` only in a loop or switch and `continue` only in a loop;
  `return` with a value only in a non-void function, without one only in a
  void function, and its value converted as by assignment. Checked against GCC
  on 43 snippets with no mismatch. Implemented as specified. No existing test
  changed; 5 `StatementTest` tests added. 45 mutants, all killed by an
  attributed assertion after the first run found five missing cases (jumps
  and labels after a loop or switch closes, `continue` directly in `do`, an
  outer case after a nested switch, a `const struct` return, a return type
  that failed to resolve) and one equivalent `resolved_type`, which was
  removed; four mutants that could only crash were rewritten.
- **Step 17 — initializers and the remaining declaration constraints.** *Done
  2026-09-17.* An initializer walk with a stack of frames: brace elision,
  designators continuing inside the innermost designated aggregate (the parser
  marks the one-item lists it builds from a designator chain with
  `is_designation`), strings for character and `wchar_t` arrays, excess
  elements reported once per list, scalars converted as by assignment, and
  arrays of unknown size (including compound literals) sized by the highest
  element touched. Objects with static storage (`has_static_storage`) need
  arithmetic or address constants (6.6p7-9), classified without reading
  anything. A VLA or an incomplete object cannot be initialized, and an object
  without linkage must be complete (6.7p7). Every declarator is checked for
  incomplete, function or flexible-member array elements, an integer array
  size, functions returning arrays or functions, and `restrict` on a
  non-object pointer; a definition needs a complete return type. An inline
  definition with external linkage cannot refer to internal linkage or define
  a modifiable static object (6.7.4p3), judged from the declarations before
  its body. Checked against GCC on 116 snippets; the remaining differences
  follow C99's letter (`&x != 0` is not a constant). Implemented as
  specified. 3 tests changed for the new diagnostics and sizes; 9 tests added.
  Review fixed one cascade: a cast of an untyped operand made its initializer
  "not constant", so unknown constants now pass through casts, unary and
  binary operators and `?:` conditions. 180 mutants on the first run; the
  survivors found missing cases and ten unobservable conditions, which were
  removed. The final list is 170 mutants, all killed by an attributed
  assertion except two kept equivalents (an unknown-element guard for
  unresolved tags, and a `length >= 0` bound that the unsigned comparison
  already implies). Of five crash-only mutants, three were rewritten, one
  dropped, and one made attributable by removing the case that crashed first.
  **Sema is complete.**

  Carried into the declarations step, found while writing Step 4:
  - `FUNCTION_DEF` has no `is_variadic`; `parse_top_level_declaration` stores
    it on the function's *return type* instead. *Fixed in Step 10.*
  - `int f(void), x;` at file scope fails: the function branch of
    `parse_top_level_declaration` never reads a comma. *Fixed in Step 10.*
  - Declaration-context constraints that `decl_specs` now makes checkable:
    `inline` on a non-function (6.7.4p2), `register` at file scope (6.9p2), a
    storage class on a struct member, and an initializer on a `typedef`.
    *Fixed in the holes pass.*

  Found while writing Step 5:
  - **Locations have no file.** Tokens carry line and column only. The
    preprocessor's `source_file` owns the file name and frees it on pop, so a
    token cannot point at it once `pp_run` returns, and after `#include` a line
    number alone is ambiguous. Needs a file table that outlives the
    preprocessor; when it lands it adds a file to `source_loc`. Needed before
    sema's diagnostics mean anything across `#include`. *Fixed in the holes
    pass:* tokens and `source_loc` carry an interned file name, and `#line`
    moves both the file and the line of every following token.
  - **Macro expansions keep the macro body's position.** A node built from a
    macro body points into the `#define`; tokens passed as arguments keep the
    position where they were used. `SourceLocationTest` documents both.
  - **Names stored as plain strings have no location**: parameter names,
    enumerator names, designator member names. Diagnostics about them can only
    point at the enclosing node.
  - `parse_unary`: `sizeof(type` with a missing `)` leaks `sizeof_type` and
    re-parses the operand from the wrong token. *Fixed in the holes pass.*

  Found while writing Step 7:
  - `AST_NODE_TYPE_DESIGNATOR` is never built; designators live on `INIT_LIST`
    items. The node type and its `free_ast` case are dead. *Removed in the holes
    pass.*
  - The parser accepts a declaration as a substatement (`if (c) int y;`), which
    C99's grammar forbids. *Fixed in the holes pass.*
  - A struct, union or enum definition inside a cast, `sizeof` or parameter list
    does not parse (`out_def` is NULL there). Until it does, the block C99 makes
    of a whole selection statement (6.8.4p3) cannot be observed, so Step 7 opens
    scopes only for `for` and for substatements. *Fixed in the holes pass, and
    sema now opens the whole-statement scopes.*
  - A parameter with no name in a function definition (6.9.1p5) is not
    diagnosed. *Fixed in the holes pass.*

  Found while writing Step 8:
  - `struct *p;` parses without a diagnostic, giving a struct type with neither
    a tag nor a definition; C99 6.7.2.1 requires one or the other. *Fixed in the
    holes pass.*
  - A declaration that declares nothing — `int;`, or `enum E;` at block scope —
    becomes an empty `DECL_GROUP` with no diagnostic (6.7p2). At file scope,
    `enum E;` gets "expected a name in this declaration". *Fixed in the holes
    pass.*
  - `struct S *;` is accepted as a forward declaration of `S`: the check looks
    at the specifier, not at whether a declarator followed it. *Fixed in the
    holes pass.*

  Found while reviewing Step 9:
  - `(int)-2.9` is not an integer constant expression here: the cast's operand is
    `-2.9`, a unary expression, not a floating constant. That is C99's letter;
    GCC accepts it.
  - `08` reports "invalid suffix on an integer constant" rather than naming the
    bad octal digit, and a mixed-case `lL` suffix is accepted as `ll`. *Fixed in
    the holes pass.*

  Found by the audit after the holes pass, and fixed the same day: digraphs and
  trigraphs, form feed and vertical tab as whitespace, lexer errors missing from
  `pp_run`'s count, `__func__`, an initialized block-scope declaration with
  linkage (6.7.8p5), an empty initializer list, a designator without `=`, an
  initializer list without `}`, an empty translation unit, a `void` parameter
  in a definition, and every silent `#define`, `#undef` and conditional-directive
  constraint (no name, malformed or duplicate parameters, `#` without a
  parameter, `##` at either end, an incompatible redefinition, no whitespace
  after an object-like name, extra tokens). 62 mutants, all killed by an attributed assertion; the audit that found them also exposed two equivalent conditions and two missing redefinition cases, all resolved before the final run.

  Found while fixing the parser and lexer holes, still open:
  - A failing block item abandons the whole function body, so one error can
    cascade into "expected a declaration at file scope" at the closing brace.
  - Identifiers with universal character names are not checked against C99
    Annex D.
  - Multi-character constants and constants too long for their type are
    accepted silently; GCC warns, and there is no warning channel.
  - The two Step 5 location findings above (macro expansions, names stored as
    plain strings) are unchanged.
  - C99 6.10p5 allows only space and horizontal tab between the tokens of a
    directive; a form feed or vertical tab there is not diagnosed.
  - `typedef void V; int f(V);` declares a function with one parameter of type
    `V`; the `(void)` special case needs the resolved type. *Fixed in Step 11.*
  - The audit's other mismatches are all Step 11 onward: type checking of
    expressions and initializers, lvalues, `case`/`default`/`break`/`continue`
    placement, `return` against the function's type, struct layout (bit-field
    widths, duplicate and flexible members), declarator constraints on function
    and array types, `restrict` on non-pointers, and variably modified types at
    file scope (*fixed in Step 11*).

  Left by Step 11:
  - Composite types are built only at the top level (array size, prototype)
    and only for declarations sharing one symbol; a block-scope `extern` does
    not take a visible file-scope declaration's type (6.2.7p4).
  - `int f() {}` followed by `int f(int);` is accepted: a type does not record
    that an empty identifier list came from a definition.
  - A declaration is checked against one earlier linked declaration, and
    compatibility is not transitive.
  - Completeness of objects without linkage (6.7p7) waits for initializers.

  Left by Step 12:
  - An array of a struct with a flexible array member, or of an incomplete
    element type, is diagnosed only as a member; elsewhere it joins the
    declarator constraints.
  - Member access and `offsetof` need expression typing.
  - A record size that overflows `long long` is not detected.

  Left by Step 13:
  - `?:` with two compatible pointer targets keeps the left target with both
    qualifier sets rather than a composite type.
  - Qualifiers a typedef puts on an array decay onto the element only one
    array level deep. *Fixed in Step 14.*
  - `_Imaginary` is treated as its real type in arithmetic.
  - The parser does not report a `?:` with no `:` (`parse_ternary` skips the
    check), and an operator whose operand failed to parse keeps a NULL child.

  Left by Step 14:
  - `&` of a `register` parameter is not diagnosed: the parser checks a
    parameter's storage class but does not keep it.
  - The decay of a `register` array (undefined, 6.3.2.1p3) is not diagnosed.
  - `g().a = 1` waits for call types (Step 15).

New component, `src/sema.c` + `include/sema.h`, walking the AST and using the
real `symbol_table.c` (which gains its first caller here). Built complete — its
output is the only source of types the IR generator will read.

Order within the step, roughly: declaration collection and scope building →
name resolution → type computation on expressions → usual arithmetic
conversions → lvalue and assignability rules → function call checking →
`return`-type checking → constant expression folding where C requires it.

Known invariant, do not re-derive: **`symbol_table` does not own `type_info`.**
It stores a borrowed `type_info*` and never calls `free_type_info`
(`src/symbol_table.c` frees only `name` and the node itself). The AST owns
types, so the AST must outlive the table.

Target type sizes are sema's business, and this target is **LLP64**: `int` and
`long` are both 32 bits, `long long` and pointers are 64. So `long` is `i32` in
the IR, not `i64`.

The parser's `parser_symbol` stub **cannot** be retired in favour of the real
table: C needs to know whether a name is a typedef *while parsing* (`T * x;`),
and sema runs after the parser. The stub stays. Since the holes pass it records
ordinary names as well as typedef names, in the same scopes sema opens (block,
prototype, a definition's parameters, `for`, every selection and iteration
statement and substatement), so `typedef int T; void f(void) { int T; T = 1; }`
parses as C99 6.2.1p4 requires. Struct members are not recorded: they live in
their own name space and do not hide a typedef.

**Done when:** the type of every expression node is resolved, and the diagnostics
that C requires at this stage are emitted with source locations.

---

### P2.1 — Close C99's remaining front-end gaps
**Status:** done 2026-09-20 · **Depends on:** P2

**Follow-up, 2026-10-03.** An independent audit (about 430 new cases against GCC, plus
5,500 fuzzed inputs with no crash) found three more, all fixed with 6 tests (880 → 886)
and 21 mutants, every one killed by an attributed assertion: a typedef name used as an
expression was accepted (6.5.1p2); a definition's parameter types were compared with an
earlier declaration before they were resolved, so a typedef or tag mismatch passed
(6.7p4); and a parenthesized declarator could not redeclare a typedef name in an inner
scope (`int (T);`). A definition's name is now declared after its parameters, as
6.2.1p7 puts its scope.

**Outcome.** All 25 items below are fixed, with 51 tests added (829 → 880) and
67 mutants, every one killed by an attributed assertion. The audit that found
them now differs from C99 in nothing: its three remaining rows were wrong
expectations of mine (`__DATE__`'s format is correct; `case 1:` and `case 257:`
on an `unsigned char` switch are distinct because the controlling value is
promoted to `int`; and `#` applied to a lone `\` produces `"\"`, which
6.10.3.2p2 leaves undefined, so rejecting it is allowed).

Two defects turned up while fixing the listed ones, and are fixed with them
because they were the cause of a listed example failing:
- **`#` spaced its string by comparing token positions**, which is wrong for a
  token that came from a macro expansion: EXAMPLE 4's
  `#include xstr(INCFILE(2).h)` gave `"vers2 . h"`. Tokens now carry
  `leading_space`, set by the lexer and inherited through substitution as C99
  describes, and `#` uses it.
- **An empty argument next to `##` was not a placemarker** (6.10.3.3p2):
  `#define c(a, x, y) a x ## y` with `c(1, , 2)` pasted `1` and `2` into `12`.
  The rewritten `substitute` handles it.

Found and fixed in passing: the lexer built its `.` and `...` tokens by hand
and never set their `file`, so a diagnostic on one printed a garbage file name.
Both now go through `lexer_make_token`.

This is the complete list of what stands between the lexer, preprocessor,
parser and sema and C99 conformance, excluding what later steps own (headers,
target macros are P2.2, headers and builtins P2.3; what IR generation needs recorded is P2.4;
code generation is P3).

**How it was found.** A 2026-09-19 audit ran 362 cases through preprocessor →
parser → sema with a scratch driver: every Constraints paragraph of 6.4–6.10,
the standard's own EXAMPLEs, 38 constant-expression checks of types and values
(LLP64 conversions, constant types, `char` signedness, layouts), and all 20
translation limits of 5.2.4.1. Each case was judged against the standard, with
GCC `-std=c99 -pedantic-errors` as a cross-check. Every rejection was checked
to be for the right reason, not an unrelated error. Everything else passed,
including all 20 limits. The items below are the whole of the difference: 25
items, nine of which reject valid code or produce wrong output.

**Wrong output or valid code rejected (fix first).**
- **Blue paint** (6.10.3.4p2), preprocessor. A macro name left unreplaced
  because it was being expanded must never be replaced later. The standard's
  EXAMPLE 3 fails: with `#define z z[0]`, `f(f(z))` gives `z[0][0][0]`, not
  `z[0]`, and `t(t(g)(0) + t)(1)` gives `f(2 * (2 * (0)))`. Cause: the
  `macro_disabled` stack-walk approximation (`CLAUDE.md` ceiling); the fix is a
  per-token "never replace" mark.
- **A `##` that did not come from the replacement list is an ordinary token**
  (6.10.3.3, its EXAMPLE), preprocessor. `#define hh # ## #` then
  `mkstr(a hh b)` through one more macro gives `"ab"`; the standard gives
  `"a ## b"`.
- **`#` escapes `\` only inside string and character literals** (6.10.3.2p2),
  preprocessor. EXAMPLE 4's `str(: @\n)` gives `": @\\n"`, not `": @\n"`.
- **A stray character is a preprocessing token** (6.4p1, last category), lexer.
  `@` or a lone `\` is an error only if it reaches the parser (6.4p2). Today
  the lexer rejects it on sight, so the `_Pragma` EXAMPLE (6.10.9,
  `..\listing.dir`), `#x` applied to `@`, and a `\` inside `#if 0` are all
  rejected.
- **Computed `#include`** (6.10.2p4), preprocessor: `#include MACRO` and
  EXAMPLE 4's `#include xstr(INCFILE(2).h)`. The tokens after `include` are
  macro-expanded and must then form `"..."` or `<...>`.
- **`[*]` below the outermost array** (6.7.5.2p4), sema. `double a[*][*]`,
  `double a[][*]` and `double a[n][*]` are rejected with "an array needs a
  complete element type"; a `[*]` array is a VLA type, which is complete. Two
  lines of 6.7.5.3 EXAMPLE 5 fail.
- **Composite types** (6.2.7p3-4, 6.5.15p6), sema, three false errors, each
  "invalid application of sizeof to an incomplete type":
  `int (*p)[]; int (*p)[3];` then `sizeof *p` (composite below the top level);
  `int a[10];` then `extern int a[];` at block scope and `sizeof a` (the
  visible declaration's composite); `sizeof *(1 ? pu : p3)` with
  `int (*pu)[]`, `int (*p3)[3]` (`?:` of two pointers).

**Required diagnostics that are missing.**
- **Macro argument count** (6.10.3p4), preprocessor. On a mismatch, or an
  argument list left open at end of file, the macro's name is emitted alone
  and the arguments are dropped. The error arrives later only by accident:
  `int F(int); #define F(a, b) a + b` then `int (*x)(int) = F(1);` is accepted.
- **Zero variable arguments** (6.10.3p4), preprocessor: `#define F(a, ...) a`
  then `F(1)`. `CLAUDE.md` called accepting it a deliberate deviation; C99
  requires a diagnostic, and GCC gives one.
- **`__VA_ARGS__` in ordinary text** (6.10.3p5), preprocessor. It is
  diagnosed in a non-variadic macro's definition, not outside any macro.
- **Signed overflow in `#if`** (6.6p4), preprocessor:
  `#if 0x7fffffffffffffff + 1`.
- **Extra tokens after `#include "file"`** (6.10.2 syntax), preprocessor.
- **Form feed or vertical tab inside a directive** (6.10p5), preprocessor.
- **Conditionals balance per file** (5.1.1.2 phase 4 processes an included
  file as its own preprocessing file), preprocessor. An `#if` opened in a
  header and closed by the includer is accepted (`CLAUDE.md` ceiling).
- **An integer constant with no type** (6.4.4.1p5, 6.4.4p2), lexer and sema.
  `99999999999999999999` (past `unsigned long long`) is accepted silently,
  and `18446744073709551615` (decimal, past `long long`) is made unsigned
  (`CLAUDE.md` ceiling).
- **A floating constant out of range** (6.4.4p2): `1e999` is accepted
  silently.
- **A declaration that declares nothing** (6.7p2), parser:
  `struct { int a; };`. `int;` and `struct S { int; }` are diagnosed.
- **Jumps into the scope of a variably modified type**, sema: `goto` from
  outside a VLA's scope to a label inside it (6.8.6.1p1, both directions of
  6.8.6.1 EXAMPLE 2), and a `case` or `default` inside a VLA's scope when
  the `switch` is not (6.8.4.2p2).
- **A `static` function used but never defined** (6.9p3), sema:
  `static int f(void); int g(void) { return f(); }`. Use inside a `sizeof`
  with a constant result stays allowed.
- **An old-style definition then a prototype** (6.7.5.3p15, 6.7p4), sema:
  `int g() { return 0; }` then `int g(int);`. A type does not record that its
  empty identifier list came from a definition.
- **`&` of a `register` parameter** (6.5.3.2p1), sema: the parser checks a
  parameter's storage class but does not keep it.
- **`inline` on `main`** (6.7.4p4), sema.
- **An argument of incomplete type** (6.5.2.2p2), sema: `g(*p)` with
  `struct I *p` and `void g(struct I);`.
- **Division by zero in a static initializer** (6.6p4, 6.7.8p4), sema:
  `static int a = 1 / 0;` is accepted, while `enum { E = 1 / 0 };` is not.
- **`_Imaginary`** (Annex G is optional), sema: reject its types with a
  diagnostic, as GCC does, instead of treating them as their real type, and
  leave `__STDC_IEC_559_COMPLEX__` undefined.

**The decision that was taken.** C99 requires a *diagnostic*, which may be a
warning (5.1.1.3). GCC only warns for three of these: `1e999`, zero variable
arguments and a decimal constant past `long long`. With no warning channel in
the project, all three became errors, which is conforming and stricter than
GCC. A warning channel can still be added later; it would move exactly these
three.

Removed after the audit: "a redeclaration is checked against the newest linked
declaration only". `int f(int (*)[3]); int f(int (*)[]); int f(int (*)[4]);`
is diagnosed against the first declaration, and no failing case was found.

**Done when:** every item has a test that fails without its fix, and every
standard EXAMPLE named above produces the standard's result. Met on
2026-09-20. The 362-case audit stays a scratch harness rather than repo tests;
the 51 gtests added here cover the items it found, and turning an outside
corpus into kept tests is P5's job.

---

### P2.2 — Two targets: Windows x64 and Linux x64
**Status:** done 2026-10-03 · **Depends on:** P2.1 · **Decided:** 2026-10-03

**Outcome.** Implemented as specified; the document's code was compiled and
compared with both GCCs before hand-over, and the implementation matched it
exactly. 17 tests added (886 → 903), including a new `TargetTests` target, Linux
layout rows generated from GCC in WSL, and `BitFieldsFollowTheConstraints`
running on both targets. `GrowthDoublesRatherThanIncrementing` counted every
`realloc` in a run, so 15 more predefined macros broke it; it now subtracts an
empty file's cost. 47 mutants: 46 killed by an attributed assertion, and one
kept equivalent (the constant evaluator's `sizeof` type, see `CLAUDE.md`). The
first run exposed one missing case: no test checked that a named zero-width
bit-field is still an error on Linux.

Itamar chose two targets and no others: `x86_64-w64-windows-gnu` (MinGW's
headers, the `msvcrt` library) and `x86_64-linux-gnu` (glibc). LLVM
translates to either machine, but everything the front end decides before
LLVM sees the program (type sizes, record layout, predefined macros,
headers, and in P3 the calling convention) belongs to one target, so the
compiler carries a description of each.

**How a target is chosen.** One `target` description, picked by a flag on
the compiler's command line (`--target=windows` or `--target=linux`, P4),
defaulting to the system the compiler runs on. Until the driver exists, tests
pass the target directly. No build-system layer: choosing a target is the
compiler's job, and CMake only decides which commands build a project.

**What differs, measured 2026-10-03** with the CLion MinGW GCC 13.1 and GCC
15.2 in WSL Ubuntu (glibc 2.43), the two oracles for every check in this step:

| | Windows | Linux |
|---|---|---|
| `long`, `unsigned long` | 4 bytes (LLP64) | 8 bytes (LP64) |
| `size_t`, `ptrdiff_t` | `unsigned long long`, `long long` | `unsigned long`, `long` |
| `wchar_t`, wide literals | `unsigned short`, UTF-16 | `int`, UTF-32 |
| bit-field layout | Microsoft (`struct { char a:4; int b:4; }` is 8) | System V (the same struct is 4) |
| `va_list` | `char *`, 8 bytes | `__va_list_tag[1]`, 24 bytes |
| predefined macros | `_WIN32`, `_WIN64`, `__MINGW32__`, `__MINGW64__` | `__linux__`, `__linux`, `__unix__`, `__unix`, `__gnu_linux__`, `__ELF__`, `__LP64__`, `_LP64` |

The same on both: pointers, `long long` and `long double` (x87, 16/16),
signed `char`, `__x86_64__`.

**Work in this step:** the `target` description; sema's integer types, sizes
and layouts read from it (Steps 9 and 12 were written for Windows only); a
System V bit-field layout beside the Microsoft one; the shared literal decoder
takes the `wchar_t` unit width, so wide literals are UTF-32 on Linux and there
is still one decoder; the target's predefined macros. Every size, alignment,
layout and constant-value test runs for both targets.

**What later steps gain:** P2.3 parses glibc's headers as well as MinGW's
(both use the same GCC extensions, so the extension work is shared). P3 adds
the System V calling convention and its `va_list` beside Win64's; it is the
largest addition, because System V splits a struct across registers by
classifying each 8-byte piece. Producing a Linux program needs Linux's
headers and linker; WSL Ubuntu has both, so the simplest route is to build
and run the compiler inside WSL for Linux. Cross-compiling from Windows with a
copied sysroot is decided in P4.

**Done when:** for both targets, every type size and alignment, every record
layout and every predefined macro matches that target's GCC.

---

### P2.3 — Headers for both targets
**Status:** done 2026-10-05 (P2.3a–e) · **Depends on:** P2.2

C99 makes the headers part of the implementation (clause 7), and
`__STDC_HOSTED__` is predefined as 1, which promises all 24.

**Target macros** come from P2.2. Both systems' headers also test `__GNUC__`
and its version macros. `__STDC_IEC_559__` stays undefined.

**Compiler-owned freestanding headers**, which must agree with the target's
types from P2.2: `<stddef.h>` (`size_t`, `ptrdiff_t` and `wchar_t` differ per
target, `NULL`, `offsetof`), `<stdarg.h>`,
`<stdbool.h>`, `<stdint.h>`, `<limits.h>`, `<float.h>` (x87 `long double`:
`LDBL_MANT_DIG` 64), `<iso646.h>`. Two need compiler support, not only text:
- `<stdarg.h>`: a `va_list` type and `va_start`, `va_arg` (whose second
  operand is a type name), `va_end`, `va_copy` as builtins in the parser, sema
  and IR generation. MinGW's `vadefs.h` spells them `__builtin_va_list` and
  `__builtin_va_*`.
- `offsetof` must be an integer constant expression (7.17p3); MinGW defines it
  as `__builtin_offsetof(T, m)`, which the constant evaluator must accept.

**Decided 2026-10-04: option 1, each system's real headers.**

**Measured 2026-10-04** by running all 24 headers through the front end on both
targets, then expanding them with each GCC (claiming GCC 4.2, as clang does) and
counting what is not C99. With 12 GNU keywords stubbed out as macros, every
glibc header passes parser and sema except the three we write ourselves
(`float.h`, `iso646.h`, `tgmath.h`). Windows needs more: `_mingw.h` defines
a few `gnu_inline` functions in GNU inline assembly, and `string_s.h` leaves a
stray `;` at file scope, which GCC tolerates only in system headers. The full
list: `__attribute__` (most ignorable; `aligned`, `gnu_inline`, `dllimport`,
`returns_twice` matter), `__extension__`, `__inline__`/`__inline`,
`__restrict__`/`__restrict`, `__asm__("name")` labels (13 on Linux, which rename
symbols such as `fscanf` to `__isoc99_fscanf`), `__asm__ __volatile__`
statements (Windows), `#pragma pack` (28 on Windows), `__builtin_va_list` and
`__builtin_va_*`, and the builtins the standard macros expand to
(`__builtin_huge_val*`, `__builtin_inf*`, `__builtin_nan*`, the
`__builtin_is{greater,less,...}` family, `__builtin_signbit`, `__builtin_trap`,
`__builtin_expect`, `__builtin_choose_expr`, `__builtin_types_compatible_p`,
and library calls spelled `__builtin_llabs`/`__builtin_vsnprintf`). Glibc's
`tgmath.h` needs GCC 8 or later, so `tgmath.h` is ours on both targets.

**Split into five steps, in dependency order (decided 2026-10-04):**
- **P2.3a** — *done 2026-10-04* — the preprocessor: system header folders searched after the
  user's, every token marked when it comes from a system header, and the
  predefined macros that claim GCC 4.2 (as clang does), each target's as a
  `#define` text read before the main file. Done when the preprocessed output
  of each hosted header matches GCC's `-E` output token for token.
  Outcome: implemented exactly as specified (verified against a scratch copy
  that was compared with both GCCs before hand-over). 14 tests added
  (903 → 917); 10 calls updated for the new `pp_run_ex`/`pp_run_file`; and
  `SkippedGroupsDoNoExpansionWork` subtracts an empty file's `realloc` cost,
  like P2.2's growth test. 35 mutants: 33 killed by an attributed assertion,
  2 untestable by design (see `CLAUDE.md`).
- **P2.3b** — *done 2026-10-04* — GNU syntax in the parser: `__attribute__`, `__extension__`, the
  `__`-spellings of keywords, `__asm__` labels and statements (option a), and a
  stray `;` only in a system header. Attributes and `__asm__("name")` labels are
  parsed and discarded: nothing reads them yet, and each later step records what
  it needs in its own shape. `__asm__` statements become an `AST_NODE_TYPE_ASM`
  node whose operands sema checks, for P3 to pass to LLVM.
  Outcome: implemented exactly as specified (verified against a scratch copy
  in which 40 of the 42 header runs passed the whole front end; the other two
  fail in the draft `stddef.h`, which is P2.3e's). 9 tests added (917 → 926).
  35 mutants, all killed by an attributed assertion.
- **P2.3c** — *done 2026-10-05* — attributes that change types or layouts:
  `#pragma pack` (and `_Pragma`), `aligned`, and `mode` (MinGW's
  `typedef int __int128 __attribute__((mode(TI)))` must not silently become a
  4-byte `int`; a use of it is now an error).
  Outcome: implemented as specified with three review fixes. A lost `}` in
  `pp_run_ex` stopped `preprocessor.c` compiling, and `pp_next` missed its two
  initializer edits. The step document was also wrong that GCC reads the pack
  value as plain decimal: it reads an integer constant (`pack(010)` is 8), so
  `pack_value` now uses `classify_number` and `strtoull` base 0. Every expected
  layout in the tests was measured with GCC on both systems, and 21,000 random
  records per system match GCC. The header pass is unchanged: 40 of 42, the two
  failures being the draft `stddef.h` (P2.3e). 26 tests added (926 → 952), and
  `SemaTests` now links `preprocessor.c`. 60 mutants: 59 killed by an
  attributed assertion, 1 by a crash only (see `CLAUDE.md`). Also closed a
  P2.3b gap: attributes after a parameter's declarator now parse.
- **P2.3d** — *done 2026-10-05* — the builtins: `__builtin_va_list` and `va_*`,
  `offsetof`, and the builtins the standard macros expand to, plus the three GNU
  spellings behind them (imaginary constants for `I`, `__typeof__` inside
  `__builtin_types_compatible_p`, `__PRETTY_FUNCTION__` for glibc's `assert`).
  Outcome: implemented exactly as specified. Every block's intermediate state was
  built and tested before hand-over. With no stubs, all 42 header runs pass the whole
  front end, and two probes using every standard macro pass on both targets; 85
  `offsetof`, `types_compatible_p`, size and `choose_expr` values match GCC on both.
  The one wrong value found before hand-over (array element qualifiers in
  `types_compatible_p`) came from that comparison. 20 tests added (952 → 972).
  59 mutants, all killed by an attributed assertion after one test was strengthened.
- **P2.3e** — *done 2026-10-05* — the compiler's own headers in `lib/include/`
  (`stddef.h`, `stdarg.h`, `stdbool.h`, `iso646.h`, `limits.h`, `float.h`,
  `tgmath.h`, `mm_malloc.h`, and a `math.h` that adds MinGW's missing
  `math_errhandling` through `#include_next`), plus `#include_next` and
  `__builtin_tgmath`. `stdint.h` stays the library's: both ship a complete one.
  Outcome: blocks A and B implemented exactly as specified; I wrote block C (the
  header files) as asked. Decided: the system headers the tests need are copied into
  `tests/sysroot/` (116 files with their licenses). With them, all 24 C99 headers
  and two probes using every macro pass the whole front end on both targets with no
  stubs; 276 values and types and 318 type-generic choices match GCC on both.
  MinGW's runtime honours `math_errhandling` only partly (see `CLAUDE.md`). 18 tests
  added (972 → 990), including the new `HeaderTests` target. 51 mutants, all killed
  by an attributed assertion after one probe was added and one mutant was aimed at
  the system where it is not an equivalent.

**Hosted headers — the options that were weighed:**
1. *Parse each system's real headers*, MinGW's and glibc's (recommended; with
   two targets the extension work below is shared, because glibc's headers use
   the same GCC extensions). They already match each runtime's struct layouts (`FILE`, `jmp_buf`, `struct tm`) exactly. The cost is a
   bounded extension set, whose full list comes from running the headers until
   they parse: `__attribute__((...))` (mostly ignored, but `aligned` and
   `packed` change layout), `__declspec(...)` (`dllimport` reaches IR
   generation), `__extension__`, `__inline__`, `__asm__("name")` on
   declarations, `#pragma pack(push, n)` / `pop` (changes layout, cannot be
   ignored), and the builtins the headers call: `__builtin_huge_val*`,
   `__builtin_inf*`, `__builtin_nan*`, `__builtin_is{greater,less,unordered}*`,
   `__builtin_choose_expr`, `__builtin_types_compatible_p`,
   `__builtin_frame_address` (MinGW's `setjmp` macro). `<complex.h>` defines
   `I` as `(__extension__ 1.0iF)`, a GNU imaginary constant. With `__x86_64__`
   defined, `malloc.h` includes GCC's `mm_malloc.h`; supply our own rather
   than pulling in GCC's header directory (which starts with `#include_next`).
2. *Write our own hosted headers* declaring each runtime's functions. No
   extensions, but 17 headers twice, for two runtimes, whose layouts must match
   by hand; a mistake is silent memory corruption.

Either way `<tgmath.h>` needs one type-generic builtin, `__builtin_tgmath`:
MinGW ships no `<tgmath.h>`, so on Windows it is ours, and glibc's uses that
builtin when `__GNUC__` is 8 or later.

`_mingw.h` turns on MinGW's C99 `printf`/`scanf` automatically when
`__STDC_VERSION__ >= 199901L` against `msvcrt` (`__USE_MINGW_ANSI_STDIO`).
Keep that path: `msvcrt`'s own `printf` has no `%lld`, `%zu` or 80-bit
`long double`.

**Done when:** for each target, a file including all 24 C99 headers passes
preprocessor, parser and sema with zero diagnostics, and every freestanding
macro and type is checked against that target's GCC (the CLion MinGW GCC, and
GCC in WSL Ubuntu).

---

### P2.4 — What sema hands the IR generator
**Status:** done 2026-10-08 (P2.4a–c) · **Depends on:** P2.1, P2.3 (so it annotates the
final AST, builtins included)

Sema records `expr_type` and `is_lvalue` on each expression, nothing more. P3
promises that the IR generator never recomputes a type, and that promise does
not hold today: in `int a; unsigned b; a < b;` the `<` has type `int`, but the
comparison is unsigned (`icmp ult`), and nothing on the tree says so. Needed:

- **Implicit conversions made explicit.** Every conversion 6.3 applies —
  lvalue conversion, array and function decay, integer promotions, the usual
  arithmetic conversions, conversion as if by assignment (`=`, arguments,
  `return`, initializers), default argument promotions — becomes an
  implicit-conversion node carrying its target type (clang's
  `ImplicitCastExpr`). Compound assignment records its computation type
  (`c += 10` on an `unsigned char` computes in `int`).
- **`case` values** on `case_stmt`; today they are computed and dropped.
- **Constant values for static storage**: arithmetic constants (floating
  included) evaluated, address constants as symbol plus byte offset.
- **Initializer layouts**: the Step 17 walk's result (byte offset, bit-field,
  type, value) recorded rather than walked again.
- **Declaration facts the headers carry**: `__asm__("name")` labels (glibc
  renames `fscanf` to `__isoc99_fscanf`), and the attributes `gnu_inline` (never
  emit), `dllimport` and `returns_twice` (`setjmp`). P2.3b parses and discards
  them; this step records them.
- **Sizes**: `layout_of` is `static` in `sema.c`; `sema.h` exports only
  `sema_check`.
- **VLA sizes**: which expressions compute each variably modified type, so
  each is evaluated once where the declaration is reached, and `sizeof` of a
  VLA reads that value.

**Done when:** a walk test asserts that every operator's operands already have
the type the operator works in, and every value above is present where IR
generation will look.

- **P2.4a** — *done 2026-10-06* — implicit conversions made explicit. Outcome: blocks A–D
  implemented exactly as specified. A conversion node (lvalue, array to pointer, function to
  pointer, value) is wrapped in place around every operand C converts, so each operator's operands
  have the type it works in: checked by a walk over a program using every operator and over the
  C99 headers' own code, on both targets. Compound assignment, `++` and `--` record their
  computation type. Found on the way: a bit-field narrower than 32 bits promotes to `int`
  (6.3.1.1p2, measured on both GCCs), which sema had typed as its declared type. Also found, by
  AddressSanitizer and a fuzzer, and fixed: `#if` leaked a token list, and two crashes on code the
  parser had already rejected. 21 tests added (990 → 1011), 68 mutants, all killed by an
  attributed assertion or, for the two crash fixes, a crash in the test run alone.
- **P2.4b** — *done 2026-10-07* — initializer layouts. Outcome: blocks A and B implemented
  exactly as specified. Each declaration and compound literal with an initializer records a list
  of entries (byte, bits for a bit-field, type, converted value) built by the Step 17 walk. Where
  later initializers overlap earlier ones, two rules measured on GCC decide (a value removes what
  it overlaps, in bits; a brace list first wipes its whole part), and played into bytes the
  layouts match GCC exactly for 27 objects on both targets. 7 tests added (1011 → 1018), 22
  mutants, all killed by an attributed assertion.
- **P2.4c** — *done 2026-10-08* — everything else, in one step (the planned c, d and e merged):
  `case` values; the value of every static initializer entry (integer, floating, complex and
  address constants); the headers' `__asm__` labels, `dllimport`, `returns_twice` (by attribute
  and by GCC's name rule for `setjmp`) and inline definitions under C99 and `gnu_inline`; sizes
  exported from `sema.h`; VLA sizes converted to `size_t`. Outcome: blocks A–D implemented exactly
  as specified. 23 static values match GCC's bytes and 19 addresses its offsets on both targets;
  on the real headers, glibc's `fscanf` is `__isoc99_fscanf`, 580 MinGW declarations are
  `dllimport`, and `setjmp` returns twice on both. A floating value out of its integer's range is
  now an error (6.6p4), where GCC warns. 16 tests added (1018 → 1034), 72 mutants, all killed by
  an attributed assertion.

---

### P3 — IR generation (LLVM textual IR)
**Status:** done 2026-10-14 (P3a 2026-10-12, P3b 2026-10-13, P3c and P3d 2026-10-14) · **Depends on:** P2.4, and P0.2 for execution tests

Split into four steps, cut only where a dependency forces it:

- **P3a** — *done 2026-10-12* — the scalar core. Outcome: blocks A–D implemented exactly as
  specified. A new `codegen_program` returns the module as text: every operator on integer,
  floating, `_Bool` and pointer types, every conversion node, `volatile`, assignment and compound
  assignment, `&&`, `||` and `?:` through `phi`, every statement, locals in entry-block `alloca`s
  with initializers from sema's layouts, arrays, string literals, and calls with and without
  prototypes, including K&R definitions. The target card gained the triple and data layout. 63
  tests (1034 → 1097): golden IR on both targets, and ten programs assembled by clang 23 and run
  on Windows and in WSL, matching GCC's output. 117 mutants: 114 killed by an attributed
  assertion, two killed only by a crash in their own test, one equivalent.
- **P3b** — *done 2026-10-13* — aggregates and data. Outcome: blocks A–E implemented exactly as
  specified. A record is `[size x i8]` addressed by sema's offsets, a structure value is its
  address and storing one is a `memcpy`; bit-fields are read and written through the smallest
  `iN` covering their bytes (`i72` and `u0x` masks for a packed field past its type); static
  initializers are byte images with `ptr` relocations; one definition per object, tentative
  definitions, `internal`, external data declarations, 16-byte alignment for arrays of 16 bytes
  or more, `static` locals as `@fn.name.N`, `__func__`, compound literals, asm labels,
  `dllimport`, inline definitions left out; VLAs through a dynamic `alloca` between
  `stacksave` and `stackrestore`, restored at block and `for` ends, `break`, `continue` and
  `goto`. 35 tests (1097 → 1132): 27 golden and four programs on both targets matching GCC,
  ASan/UBSan clean. 103 mutants: 100 killed by an attributed assertion, three equivalent;
  a fourth equivalent showed `compound_global`'s lookup was dead, and it was removed.
  The 117 P3a mutants were re-run; 15 had their text retargeted to the new code.
- **P3c** — *done 2026-10-14* — complex numbers, builtins, `asm` and `setjmp`. Outcome: blocks A–E
  implemented exactly as specified. A complex value is an SSA pair (`{ double, double }`); `+`, `-`,
  `==`, `!=`, unary `-`, conversions, truth, imaginary constants and static values work part by
  part, and `*` and `/` call libgcc's `__mul?c3`/`__div?c3` in each target's convention (the
  target card gained `windows_abi`). Every builtin but the `va_` ones (P3d), with `offsetof`'s
  variable indices recorded by sema as `scale`, and `signbit` returning GCC's values. GCC extended
  and basic `asm` become LLVM inline asm (the parser records `is_basic`). `returns_twice` calls and
  declarations carry `#0`. The standard pragmas need nothing (never contract, always full range,
  `FENV_ACCESS` ignored as GCC does). 29 tests (1132 → 1161): golden IR, four programs matching GCC
  on both targets (`CodegenTests` now sees the system headers), and parser, sema and target
  checks; ASan/UBSan clean. 97 mutants: 95 killed by an attributed assertion, one null-path guard
  killed only by a crash in its own test, one equivalent. P3a's and P3b's lists were re-run.
- **P3d** — *done 2026-10-14* — the calling conventions, variadic definitions, cross-linking.
  Written directly by Claude at Itamar's request (no step document). Outcome: one `passing` record
  per argument and result (direct with `signext`/`zeroext`, coerced pieces, pointer to a copy,
  `byval`), shared by definitions, declarations, calls and `return`. Windows: aggregates of 1, 2,
  4 or 8 bytes as integers, others and `long double` and complex `double`/`long double` by
  pointer and `sret`. Linux: the System V eightbyte classification with register counting,
  following GCC where clang differs (an unnamed bit-field counts as INTEGER). `va_start`,
  `va_end`, `va_copy` as intrinsics, `va_arg` lowered per target as clang does. Internal
  functions are emitted only when reachable from an external one or a global, so MinGW's unused
  inline `printf` family is dropped and the real `<stdio.h>` works. 19 tests (1161 → 1180):
  golden IR, a `stdio` program, and a two-file program (`tests/codegen/abi/`) built with our
  compiler on either side and GCC on the other, on both targets, matching the all-GCC output.
  ASan/UBSan clean. 82 mutants, all killed by an attributed assertion; P3a's, P3b's and P3c's
  lists re-run.

New component, `src/codegen.c` + `include/codegen.h`. Walks the sema-annotated
AST and emits `.ll` text. Reads types **only** from sema's annotations (P2.4) —
it never recomputes or guesses a type — so choices like `zext` vs `sext`, `udiv`
vs `sdiv` and `icmp ult` vs `icmp slt` come straight from sema's decisions.

Scope is all of 6.5–6.9, rough order: integer constants and arithmetic → local
variables via `alloca`/`load`/`store` → assignment → control flow (`if`,
`while`, `for`, `do`, `switch`, `goto`) with phi-free `alloca` locals →
function definitions and calls → pointers and arrays → structs and unions →
globals and initializers. Parts easy to leave out:
- **Win64 calling convention.** LLVM does not lower it; the front end must. A
  struct or union of size 1, 2, 4 or 8 goes as an integer of that size; any
  other goes by pointer to a caller-made copy, and is returned through a hidden
  `sret` pointer. MinGW passes and returns `long double` indirectly too.
  Verified by linking our objects with GCC-compiled ones, calling both ways.
- **Variadic functions.** Win64's `va_list` is a `char *` over 8-byte slots;
  a struct not of size 1, 2, 4 or 8 sits in its slot by pointer.
- **Bit-fields**: Microsoft layout from the recorded offsets, read-modify-write.
- **VLAs**: `llvm.stacksave` / `llvm.stackrestore`, including jumps out of
  their scope.
- **`long double`** is `x86_fp80` in a 16-byte slot.
- **`_Complex`**: a pair of reals; multiplication and division through
  libgcc's `__mul?c3` / `__div?c3`.
- **`_Bool`**: conversion normalises to 0 or 1.
- **`volatile`** accesses as `load volatile` / `store volatile`.
- **`setjmp`**: MinGW's macro calls `_setjmp`, which must be marked
  `returns_twice`.
- **`main`** falling off its closing brace returns 0 (5.1.2.2.3).
- **Standard pragmas** (6.10.6). The preprocessor drops every `#pragma` today
  (P2.3 has to pass `pack` through anyway). `FP_CONTRACT` and
  `CX_LIMITED_RANGE` may be ignored as long as the IR never carries `contract`
  or fast-math flags and complex `*` and `/` stay full-range. `FENV_ACCESS ON`
  needs LLVM's constrained floating-point intrinsics (`strictfp`). GCC
  ignores `FENV_ACCESS`; whether to go past GCC here is decided in the step.
- **`__declspec(dllimport)`** data becomes an LLVM `dllimport` global.
- String and wide literals, compound literals with automatic storage,
  tentative definitions emitted as zero-initialized definitions, internal and
  external linkage.

Tested through golden `.ll` files, which need no LLVM. Execution tests —
assemble the output, run it, check the exit code — need P0.2. Both are test
scaffolding and call the generator directly, not through a driver.

**Done when:** every construct above compiles, the output runs with the
expected result, and the cross-linking ABI tests pass in both directions.

---

### P4 — Driver
**Status:** done (2026-10-15) · **Depends on:** P3

Built: `driver_run` (`src/driver.c`), the host layer (`src/host.c`: install root, running a
process with Windows' quoting rules or `posix_spawn`, temporary files) and a `main` that only
hands over. The preprocessor reads `-D`/`-U` as a `<command line>` file between `<built-in>` and
the main file, and stamps every token from a macro with its outermost invocation
(`expansion_file`, `expansion_line`), which `-E` uses to keep the source's lines. The link lines
are GCC's, measured with `gcc -v` on both targets. Afterwards, the flags real build files pass
(`-W...`, `-g`, `-fPIC`, `-Wl,`, `-pthread`, `-Os`, `-std=gnu99`). 59 new tests (49 driver and
host, 10 preprocessor); 132 mutants, all killed by an attributed assertion.

Left for later, found while adding `-std=gnu99`: on Linux, glibc's non-strict mode needs its
full header set (P4.1 decides what ships) and anonymous struct and union members, which
`bits/sigcontext.h` uses (C11 6.7.2.1p13; a GNU extension in gnu99). Until then `-std=gnu99`
fails loudly on Linux and works on Windows.

Replace the token-dump `main.c` with a real driver: read the files, run
preprocessor → parser → sema → IR generation, write `.ll`, then turn it into an
object file and link with `lld` (`ld.lld -m i386pep` is its MinGW mode on
Windows; plain `ld.lld` for ELF). Report diagnostics with counts and exit
non-zero on error. Flags every C compiler has: `-E`, `-S`, `-c`, `-o`, `-I`,
`-D`, `-U`, `--target`, several input files. Every tool, header folder and
runtime file is found relative to the driver's own executable, in the layout
P4.1 ships, never through `PATH` or a system install. Header search order: the
compiler's own header directory (P2.3), then the target's system headers in
that layout.

**Done when:** `C_compiler.exe hello.c -o hello.exe` builds a program that
includes `<stdio.h>` and runs, multi-file programs link, and diagnostics are
usable.

---

### P4.1 — Self-contained distribution
**Status:** done (2026-10-15): the Windows download (P4.1a) and the Linux one (P4.1b), checked
on a clean Debian and in a clean Windows 10 VM · **Depends on:** P4

Decided (2026-10-15): the program is MMC99, version 0.9.0 until P5 and 1.0.0 after it (in
`include/driver.h`, which CMake reads); `clang` stays the code generator; every download builds for both targets; all of
MinGW-w64's and glibc's headers and link files ship, with `libgcc` for the helper routines; the
files come from this machine, recorded in `SOURCES.txt`; the clean Windows machine is a test with
nothing on `PATH` (Windows 11 Home has no Sandbox), plus one manual VirtualBox check, and the
clean Linux one is a fresh WSL distribution.

P4.1a built: the `MMC99` target, `cmake-build-debug/MMC99` (the download's exact contents), the
`dist` target's 97 MB zip, `packaging/` (the Linux collection script, `README.txt.in`,
`SOURCES.txt.in`, LLVM's licenses), and 7 `PackageTests`, checked against nine broken zips.

P4.1b built: `MMC99` for Linux (WSL's GCC, `-O2`), LLVM's Linux `clang` and `ld.lld` (from
the 1.1 GB release archive, checked against its SHA-256 and stripped by
`packaging/fetch_linux_llvm.py`), the `dist_linux` target's 157 MB tarball, a Linux `README`, and
6 `LinuxPackageTest`s, checked against eight broken tarballs. A Windows program built on Linux
runs on Windows. The clean distribution is installed (`wsl --install -d Debian --name
mmc99-clean --no-launch`, so it has no user and runs as root), and its test passes. The
VirtualBox check passed too: in a fresh Windows 10 Enterprise Evaluation VM (`mmc99-win`) with no
compiler on it and only `System32` on `PATH`, the 1.0.0 zip unpacked, built and ran `check.c` and
the `windows.h` program with the expected output, and built a Linux program. Windows 11 24H2
froze at the end of its first setup stage three times under VirtualBox on Hyper-V. Found
while doing P4.1a and done in P5: glibc's GNU mode needs anonymous
struct and union members (C11 6.7.2.1p13), and `windows.h` needs `defined` produced by a macro
inside `#if` (6.10.1p4 leaves it undefined; GCC accepts it).

The download a user unzips: our compiler, LLVM's code generator and `lld`
(Apache 2.0 with the LLVM exception, which allows shipping them), our headers,
and for each target its system headers, startup files and static libraries.
Windows: MinGW-w64's headers, `crt2.o`, import libraries for the system C
library DLL, `libmingwex` (C99's `printf` and `long double` support). Linux:
glibc's headers (LGPL; shipping them is allowed), `crt1.o`/`crti.o`/`crtn.o`
and `libc_nonshared.a`, which a bare Linux system lacks without `libc6-dev`.
Both: the helper routines LLVM emits calls to (complex `*` and `/`, Windows'
stack probe `___chkstk_ms`), from `compiler-rt` or `libgcc`. Decided in the
step: whether to ship `llc` or `clang` as the code generator, whether one
download builds for both targets, and how big the download may be. Every
license goes in the download beside what it covers.

**Done when:** on a clean Windows machine with nothing installed, and on a
clean Linux system without a compiler, unzipping the download and running it
builds and runs a program that includes `<stdio.h>`, `<math.h>` and
`<complex.h>`.

---

### P5 — Conformance
**Status:** done (2026-10-15), version 1.0.0 · **Depends on:** P4.1 (the corpus runs on the shipped download)

Built: `ConformanceTests` (c-testsuite with `tests/conformance/c-testsuite-exclusions.txt`, Lua
5.4.9's own suite, an SQLite 3.54 workload against GCC's output, one program at every
translation limit; the corpus fetched at pinned hashes by `tests/conformance/fetch_corpus.py`),
`ConstraintTest` in `SemaTests`, and `CONFORMANCE.md`. For the release: `LICENSE` (MIT, also
shipped as `licenses/MMC99`), `README.md`, `CHANGELOG.md` and a `release` target that puts both
downloads and `SHA256SUMS` in `release/<version>/` (ignored by git). The corpus found no wrong code. It found
what the system headers need (anonymous members, the `__sync` builtins, `__umulh`, named `asm`
operands, `defined` produced inside `#if`, five things only a system header may do, `__GNUC__`
kept from user code), and that `windows.h` took 55 seconds, which hash tables for macros and
large scopes brought to one. The constraint audit found `#line` accepting 2147483648 on Windows,
and that an imaginary constant (`2.0i`) in a program went undiagnosed; now only a system header
may write one.

Every earlier step's done-when is checked against samples. This step is where
"it compiles C99" becomes evidence rather than belief, and it will find bugs;
fixing them is part of the step.

- **Valid programs**: an outside corpus run end to end, chosen at the time:
  c-testsuite, and one or two real C99 programs (Lua, the SQLite amalgamation).
  Every exclusion is listed with its reason, and the only acceptable reason is
  that the test needs something outside C99.
- **Required diagnostics** (5.1.1.3): every Constraints paragraph in C99
  clause 6 has at least one test that expects its diagnostic. The steps added
  them clause by clause; nothing has audited the coverage as a whole.
- **Translation limits** (5.2.4.1): one test at each minimum — 127 nesting
  levels of blocks, 63 of conditional inclusion, 63 of parenthesized
  expressions and of parenthesized declarators, 12 declarators modifying one
  type, 4095 characters in a logical line and in a string literal, 127
  parameters and arguments (functions and macros), 511 block-scope identifiers
  in one block, 4095 macros, 1023 members, enumerators and `case` labels, 63
  levels of nested struct definitions, 15 levels of `#include`, a 65535-byte
  object.
- **The conformance document** 4p8 requires: every implementation-defined
  characteristic (Annex J.3), locale-specific characteristic (J.4) and
  extension. Most of it is already decided — LLP64, signed `char`, Microsoft
  bit-fields, the ceilings in `CLAUDE.md` — but scattered.

**Done when:** the corpus passes minus the listed exclusions, every constraint
and every limit has its test, and the document exists.

---

## Housekeeping, unscheduled

- `.clang-format` is untracked — it governs the whole repo's formatting and
  should be committed.
- `graphify-out/` is untracked and should be gitignored.
- `CMakeLists.txt` does `file(GLOB SOURCES "src/*.c")` and then re-lists every
  one of those same files explicitly under `add_executable`. The explicit list
  is dead weight.
- No CI.
