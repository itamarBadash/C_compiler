#include "driver.h"
#include "codegen.h"
#include "host.h"
#include "parser.h"
#include "preprocessor.h"
#include "sema.h"
#include "target.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum stage { STAGE_PREPROCESS, STAGE_IR, STAGE_OBJECT, STAGE_LINK } stage;

typedef enum outcome { OUTCOME_CONTINUE, OUTCOME_FAILED, OUTCOME_DONE } outcome;

typedef struct list {
  char **items;
  int count;
} list;

typedef struct options {
  stage last;
  const char *output;
  list inputs;
  list include_dirs;
  list library_dirs;
  list libraries;
  char *defines;
  target_kind target;
  const char *optimization;
  int verbose;
  int gnu;
} options;

static const char *const optimizations[] = {"-O0", "-O1", "-O2", "-O3", "-Os", "-Oz", "-Og", NULL};

static const char *const ignored_options[] = {
    "-w", "-pedantic", "-pedantic-errors", "-fPIC", "-fpic", "-fPIE", "-fpie", NULL};

static const char *const windows_libraries[] = {
    "-lmingw32", "-lgcc",      "-lgcc_eh",  "-lmoldname", "-lmingwex",  "-lmsvcrt",  "-lkernel32",
    "-lpthread", "-ladvapi32", "-lshell32", "-luser32",   "-lkernel32", "-lmingw32", "-lgcc",
    "-lgcc_eh",  "-lmoldname", "-lmingwex", "-lmsvcrt",   "-lkernel32", NULL};

static const char *const linux_flags[] = {"-m",
                                          "elf_x86_64",
                                          "--hash-style=gnu",
                                          "--eh-frame-hdr",
                                          "--build-id",
                                          "-pie",
                                          "-z",
                                          "now",
                                          "-z",
                                          "relro",
                                          "-dynamic-linker",
                                          "/lib64/ld-linux-x86-64.so.2",
                                          NULL};

static void report(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "error: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
}

static void *checked(void *memory) {
  if (memory == NULL) {
    report("out of memory");
    exit(1);
  }
  return memory;
}

static char *joined(const char *a, const char *b, const char *c) {
  size_t size = strlen(a) + strlen(b) + strlen(c) + 1;
  char *out = checked(malloc(size));
  snprintf(out, size, "%s%s%s", a, b, c);
  return out;
}

static void add(list *l, char *item) {
  l->items = checked(realloc(l->items, sizeof(char *) * (size_t)(l->count + 2)));
  l->items[l->count++] = item;
  l->items[l->count] = NULL;
}

static void add_copy(list *l, const char *item) {
  add(l, joined(item, "", ""));
}

static void add_all(list *l, const char *const *items) {
  for (int i = 0; items && items[i]; i++)
    add_copy(l, items[i]);
}

static void add_pieces(list *l, const char *text) {
  char *copy = joined(text, "", "");
  char *piece = copy;
  while (piece) {
    char *comma = strchr(piece, ',');
    if (comma)
      *comma = '\0';
    if (*piece)
      add_copy(l, piece);
    piece = comma ? comma + 1 : NULL;
  }
  free(copy);
}

static void free_list(list *l) {
  for (int i = 0; i < l->count; i++)
    free(l->items[i]);
  free(l->items);
  l->items = NULL;
  l->count = 0;
}

static int ends_with(const char *text, const char *suffix) {
  size_t length = strlen(text);
  size_t tail = strlen(suffix);
  return length >= tail && strcmp(text + length - tail, suffix) == 0;
}

static char *renamed(const char *path, const char *extension) {
  const char *name = path;
  for (const char *c = path; *c; c++) {
    if (*c == '/' || *c == '\\')
      name = c + 1;
  }
  char *base = joined(name, "", "");
  char *dot = strrchr(base, '.');
  if (dot)
    *dot = '\0';
  char *out = joined(base, extension, "");
  free(base);
  return out;
}

static int parse_target(const char *name, target_kind *kind) {
  if (strcmp(name, "x86_64-w64-windows-gnu") == 0 || strcmp(name, "x86_64-w64-mingw32") == 0) {
    *kind = TARGET_WINDOWS_X64;
    return 1;
  }
  if (strcmp(name, "x86_64-pc-linux-gnu") == 0 || strcmp(name, "x86_64-linux-gnu") == 0) {
    *kind = TARGET_LINUX_X64;
    return 1;
  }
  return 0;
}

static void add_define(options *o, const char *value, int undefine) {
  char *line;
  if (undefine) {
    line = joined("#undef ", value, "\n");
  } else {
    char *name = joined(value, "", "");
    char *equals = strchr(name, '=');
    if (equals) {
      *equals = ' ';
      line = joined("#define ", name, "\n");
    } else {
      line = joined("#define ", name, " 1\n");
    }
    free(name);
  }
  char *longer = joined(o->defines, line, "");
  free(o->defines);
  free(line);
  o->defines = longer;
}

static void usage(void) {
  printf("usage: MMC99 [options] file...\n"
         "  -o <file>        write the output to <file>\n"
         "  -c               compile to an object file, do not link\n"
         "  -S               compile to LLVM IR (.ll), do not assemble\n"
         "  -E               preprocess only\n"
         "  -I <dir>         add a header search folder\n"
         "  -D <name>[=<v>]  define a macro\n"
         "  -U <name>        undefine a macro\n"
         "  -L <dir>         add a library search folder\n"
         "  -l <name>        link with library <name>\n"
         "  -Wl,<a>,<b>      pass options to the linker\n"
         "  -pthread         define _REENTRANT and link with -lpthread\n"
         "  -std=c99         ISO C99 (the default)\n"
         "  -std=gnu99       C99 without __STRICT_ANSI__\n"
         "  -O0 ... -O3, -Os, -Oz, -Og\n"
         "                   optimization level\n"
         "  --target=<t>     x86_64-w64-windows-gnu or x86_64-pc-linux-gnu\n"
         "  -v               show the commands run\n"
         "  -W..., -w, -pedantic, -g..., -fPIC, -fPIE\n"
         "                   accepted, without effect\n");
}

static int listed(const char *arg, const char *const *names) {
  for (int i = 0; names[i]; i++) {
    if (strcmp(arg, names[i]) == 0)
      return 1;
  }
  return 0;
}

static int ignored(const char *arg) {
  if (arg[1] == 'W')
    return strncmp(arg, "-Wa,", 4) != 0 && strncmp(arg, "-Wp,", 4) != 0;
  return arg[1] == 'g' || listed(arg, ignored_options);
}

static outcome parse_option(options *o, char **argv, int *i) {
  const char *arg = argv[*i];
  if (strcmp(arg, "-c") == 0) {
    o->last = STAGE_OBJECT;
  } else if (strcmp(arg, "-S") == 0) {
    o->last = STAGE_IR;
  } else if (strcmp(arg, "-E") == 0) {
    o->last = STAGE_PREPROCESS;
  } else if (strcmp(arg, "-v") == 0) {
    o->verbose = 1;
  } else if (strcmp(arg, "--help") == 0) {
    usage();
    return OUTCOME_DONE;
  } else if (strcmp(arg, "--version") == 0) {
    printf("MMC99 " MMC99_VERSION ", a C99 compiler for x86_64-w64-windows-gnu and "
           "x86_64-pc-linux-gnu\n"
           "It runs LLVM's clang and ld.lld, and links MinGW-w64's and glibc's files.\n"
           "Their licenses are in the licenses folder.\n");
    return OUTCOME_DONE;
  } else if (strncmp(arg, "--target=", 9) == 0) {
    if (!parse_target(arg + 9, &o->target)) {
      report("unknown target '%s'", arg + 9);
      return OUTCOME_FAILED;
    }
  } else if (strcmp(arg, "-O") == 0) {
    o->optimization = "-O1";
  } else if (listed(arg, optimizations)) {
    o->optimization = arg;
  } else if (strcmp(arg, "-std=c99") == 0 || strcmp(arg, "-std=gnu99") == 0) {
    o->gnu = arg[5] == 'g';
  } else if (strncmp(arg, "-std=", 5) == 0) {
    report("only -std=c99 and -std=gnu99 are supported, not '%s'", arg);
    return OUTCOME_FAILED;
  } else if (strncmp(arg, "-Wl,", 4) == 0) {
    add_copy(&o->inputs, arg);
  } else if (strcmp(arg, "-pthread") == 0) {
    add_define(o, "_REENTRANT", 0);
    add_copy(&o->libraries, "-lpthread");
  } else if (arg[0] == '-' && arg[1] && strchr("oILlDU", arg[1])) {
    const char *value = arg[2] ? arg + 2 : argv[++*i];
    if (!value) {
      report("missing argument to '-%c'", arg[1]);
      return OUTCOME_FAILED;
    }
    if (arg[1] == 'o')
      o->output = value;
    else if (arg[1] == 'I')
      add_copy(&o->include_dirs, value);
    else if (arg[1] == 'L')
      add_copy(&o->library_dirs, value);
    else if (arg[1] == 'l')
      add(&o->libraries, joined("-l", value, ""));
    else
      add_define(o, value, arg[1] == 'U');
  } else if (arg[0] == '-' && arg[1]) {
    if (!ignored(arg)) {
      report("unknown option '%s'", arg);
      return OUTCOME_FAILED;
    }
  } else {
    add_copy(&o->inputs, arg);
  }
  return OUTCOME_CONTINUE;
}

static outcome parse_options(options *o, int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    outcome result = parse_option(o, argv, &i);
    if (result != OUTCOME_CONTINUE)
      return result;
  }
  int files = 0;
  int sources = 0;
  for (int i = 0; i < o->inputs.count; i++) {
    files += strncmp(o->inputs.items[i], "-Wl,", 4) != 0;
    sources += ends_with(o->inputs.items[i], ".c");
  }
  if (files == 0) {
    report("no input files");
    return OUTCOME_FAILED;
  }
  if (o->output && o->last != STAGE_LINK && sources > 1) {
    report("cannot specify '-o' with '-c', '-S' or '-E' with several files");
    return OUTCOME_FAILED;
  }
  if (o->gnu) {
    char *defines = joined("#undef __STRICT_ANSI__\n", o->defines, "");
    free(o->defines);
    o->defines = defines;
  }
  return OUTCOME_CONTINUE;
}

static const char *quote_of(const token *t) {
  switch (t->type) {
  case TOKEN_STRING:
    return "\"";
  case TOKEN_WIDE_STRING:
    return "L\"";
  case TOKEN_CHAR_LITERAL:
    return "'";
  case TOKEN_WIDE_CHAR:
    return "L'";
  default:
    return "";
  }
}

static int spelled_length(const token *t) {
  const char *quote = quote_of(t);
  int closing = quote[0] ? 1 : 0;
  return (int)(strlen(quote) + strlen(t->value ? t->value : "")) + closing;
}

static void write_token(FILE *out, const token *t) {
  const char *quote = quote_of(t);
  fputs(quote, out);
  fputs(t->value ? t->value : "", out);
  if (quote[0])
    fputc(quote[strlen(quote) - 1], out);
}

static int adjacent(const token *before, const token *t) {
  return before->file == t->file && before->line == t->line &&
         before->column + spelled_length(before) == t->column;
}

static void line_marker(FILE *out, int line, const char *file) {
  fprintf(out, "# %d \"", line);
  for (const char *c = file ? file : ""; *c; c++) {
    if (*c == '\\' || *c == '"')
      fputc('\\', out);
    fputc(*c, out);
  }
  fprintf(out, "\"\n");
}

static void write_tokens(FILE *out, token_buf *tb) {
  const char *file = NULL;
  int line = 0;
  const token *previous = NULL;
  for (int i = 0; i < tb->count && tb->tokens[i].type != TOKEN_EOF; i++) {
    const token *t = &tb->tokens[i];
    const char *where = t->expansion_line ? t->expansion_file : t->file;
    int at = t->expansion_line ? t->expansion_line : t->line;
    if (!previous || (t->at_line_start && (where != file || at != line))) {
      if (!previous || where != file || at < line || at > line + 8) {
        if (previous)
          fputc('\n', out);
        line_marker(out, at, where);
      } else {
        for (; line < at; line++)
          fputc('\n', out);
      }
      file = where;
      line = at;
      for (int column = 1; !t->expansion_line && column < t->column; column++)
        fputc(' ', out);
    } else if (!adjacent(previous, t)) {
      fputc(' ', out);
    }
    write_token(out, t);
    previous = t;
  }
  if (previous)
    fputc('\n', out);
}

static void summarize(int errors) {
  if (errors > 0)
    fprintf(stderr, "%d error%s generated.\n", errors, errors == 1 ? "" : "s");
}

static int preprocess(const options *o, const char *root, const char *path, token_buf *tb) {
  char *own = joined(root, "/include", "");
  char *system = joined(root, "/targets/", target_current()->triple);
  char *system_include = joined(system, "/include", "");
  const char *dirs[] = {own, system_include};
  int errors = pp_run_file_ex(tb, path, o->defines, (const char **)o->include_dirs.items,
                              o->include_dirs.count, dirs, 2);
  free(own);
  free(system);
  free(system_include);
  return errors;
}

static int write_preprocessed(const options *o, const char *root, const char *path) {
  token_buf tb;
  int errors = preprocess(o, root, path, &tb);
  summarize(errors);
  FILE *out = o->output ? fopen(o->output, "w") : stdout;
  if (!out) {
    report("cannot write '%s'", o->output);
    token_buf_free(&tb);
    return 0;
  }
  write_tokens(out, &tb);
  if (o->output)
    fclose(out);
  token_buf_free(&tb);
  return errors == 0;
}

static char *translate(const options *o, const char *root, const char *path) {
  token_buf tb;
  int errors = preprocess(o, root, path, &tb);
  parser p;
  parser_init_from_buf(&p, &tb);
  ast_node *program = parse_program(&p);
  errors += p.had_error;
  parser_destroy(&p);
  token_buf_free(&tb);
  symbol_table *table = symbol_table_create();
  errors += sema_check(table, program);
  summarize(errors);
  char *ir = errors ? NULL : checked(codegen_program(program));
  symbol_table_destroy(table);
  free_ast(program);
  return ir;
}

static int write_file(const char *path, const char *text) {
  FILE *out = fopen(path, "wb");
  if (!out) {
    report("cannot write '%s'", path);
    return 0;
  }
  fputs(text, out);
  fclose(out);
  return 1;
}

static char *tool(const char *root, const char *name) {
  char *base = joined(root, "/bin/", name);
  char *path = joined(base, host_executable_suffix(), "");
  free(base);
  return path;
}

static int run_tool(const options *o, list *command) {
  if (o->verbose) {
    for (int i = 0; i < command->count; i++)
      fprintf(stderr, "%s%s", i ? " " : "", command->items[i]);
    fprintf(stderr, "\n");
  }
  int code = host_run((const char *const *)command->items);
  if (code < 0)
    report("cannot run '%s'", command->items[0]);
  else if (code > 0)
    report("'%s' failed with exit code %d", command->items[0], code);
  free_list(command);
  return code == 0;
}

static int compile_object(const options *o, const char *root, const char *ir_path,
                          const char *object_path) {
  list command = {NULL, 0};
  add(&command, tool(root, "clang"));
  const char *const flags[] = {
      "-c", "-x", "ir", ir_path, "-o", object_path, o->optimization, "-Wno-override-module", NULL};
  add_all(&command, flags);
  add(&command, joined("--target=", target_current()->triple, ""));
  return run_tool(o, &command);
}

static int link_program(const options *o, const char *root, list *objects) {
  const char *output = o->output ? o->output : o->target == TARGET_WINDOWS_X64 ? "a.exe" : "a.out";
  char *lib = joined(root, "/targets/", target_current()->triple);
  char *dir = joined(lib, "/lib/", "");
  free(lib);
  list command = {NULL, 0};
  add(&command, tool(root, "ld.lld"));
  if (o->target == TARGET_WINDOWS_X64) {
    const char *const flags[] = {"-m", "i386pep", "-o", output, NULL};
    add_all(&command, flags);
    add(&command, joined(dir, "crt2.o", ""));
    add(&command, joined(dir, "crtbegin.o", ""));
  } else {
    add_all(&command, linux_flags);
    add_copy(&command, "-o");
    add_copy(&command, output);
    add(&command, joined(dir, "Scrt1.o", ""));
    add(&command, joined(dir, "crti.o", ""));
    add(&command, joined(dir, "crtbeginS.o", ""));
  }
  add_all(&command, (const char *const *)objects->items);
  add(&command, joined("-L", dir, ""));
  for (int i = 0; i < o->library_dirs.count; i++)
    add(&command, joined("-L", o->library_dirs.items[i], ""));
  add_all(&command, (const char *const *)o->libraries.items);
  if (o->target == TARGET_WINDOWS_X64) {
    add_all(&command, windows_libraries);
    add(&command, joined(dir, "default-manifest.o", ""));
    add(&command, joined(dir, "crtend.o", ""));
  } else {
    add_copy(&command, "-lgcc");
    add_copy(&command, "-lc");
    add(&command, joined(dir, "libc_nonshared.a", ""));
    add_copy(&command, "--as-needed");
    add(&command, joined(dir, "ld-linux-x86-64.so.2", ""));
    add_copy(&command, "--no-as-needed");
    add_copy(&command, "-lgcc");
    add(&command, joined(dir, "crtendS.o", ""));
    add(&command, joined(dir, "crtn.o", ""));
  }
  free(dir);
  return run_tool(o, &command);
}

static int compile_source(const options *o, const char *root, const char *path, list *objects,
                          list *temporaries) {
  if (o->last == STAGE_PREPROCESS)
    return write_preprocessed(o, root, path);
  char *ir = translate(o, root, path);
  if (!ir)
    return 0;
  if (o->last == STAGE_IR) {
    char *out = o->output ? joined(o->output, "", "") : renamed(path, ".ll");
    int written = write_file(out, ir);
    free(out);
    free(ir);
    return written;
  }
  char *ir_path = checked(host_temporary_file(".ll"));
  add(temporaries, ir_path);
  int written = write_file(ir_path, ir);
  free(ir);
  char *object;
  if (o->last == STAGE_LINK) {
    object = checked(host_temporary_file(".o"));
    add_copy(temporaries, object);
  } else {
    object = o->output ? joined(o->output, "", "") : renamed(path, ".o");
  }
  add(objects, object);
  return written && compile_object(o, root, ir_path, object);
}

static int build(const options *o, const char *root) {
  list objects = {NULL, 0};
  list temporaries = {NULL, 0};
  int built = 1;
  for (int i = 0; i < o->inputs.count; i++) {
    const char *input = o->inputs.items[i];
    if (strncmp(input, "-Wl,", 4) == 0) {
      add_pieces(&objects, input + 4);
    } else if (!host_file_exists(input)) {
      report("cannot open '%s'", input);
      built = 0;
    } else if (ends_with(input, ".c")) {
      built &= compile_source(o, root, input, &objects, &temporaries);
    } else if (o->last != STAGE_LINK) {
      fprintf(stderr, "warning: '%s' is a linker input, unused without linking\n", input);
    } else {
      add_copy(&objects, input);
    }
  }
  if (built && o->last == STAGE_LINK)
    built = link_program(o, root, &objects);
  for (int i = 0; i < temporaries.count; i++)
    remove(temporaries.items[i]);
  free_list(&temporaries);
  free_list(&objects);
  return built;
}

int driver_run(const char *root, int argc, char **argv) {
  options o = {.last = STAGE_LINK,
               .defines = joined("", "", ""),
               .target = TARGET_WINDOWS_X64,
               .optimization = "-O0"};
#ifndef _WIN32
  o.target = TARGET_LINUX_X64;
#endif
  outcome result = parse_options(&o, argc, argv);
  if (result == OUTCOME_CONTINUE) {
    target_select(o.target);
    result = build(&o, root) ? OUTCOME_DONE : OUTCOME_FAILED;
  }
  free_list(&o.inputs);
  free_list(&o.include_dirs);
  free_list(&o.library_dirs);
  free_list(&o.libraries);
  free(o.defines);
  return result == OUTCOME_FAILED;
}
