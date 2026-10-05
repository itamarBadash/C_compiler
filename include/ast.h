#ifndef _AST_H_
#define _AST_H_
#include "token.h"
typedef enum ast_node_type {
  AST_NODE_TYPE_EMPTY,
  AST_NODE_TYPE_PROGRAM,
  AST_NODE_TYPE_NUMBER,
  AST_NODE_TYPE_IDENTIFIER,
  AST_NODE_TYPE_BINARY_OP,
  AST_NODE_TYPE_UNARY_OP,
  AST_NODE_TYPE_ASSIGNMENT,
  AST_NODE_TYPE_TERNARY,
  AST_NODE_TYPE_IF,
  AST_NODE_TYPE_WHILE,
  AST_NODE_TYPE_BLOCK,
  AST_NODE_TYPE_DECL_GROUP,
  AST_NODE_TYPE_FOR,
  AST_NODE_TYPE_FUNCTION_CALL,
  AST_NODE_TYPE_FUNCTION_DEF,
  AST_NODE_TYPE_RETURN,
  AST_NODE_TYPE_STRING,
  AST_NODE_TYPE_CHAR_LITERAL,
  AST_NODE_TYPE_ARRAY_SUBSCRIPT,
  AST_NODE_TYPE_MEMBER_ACCESS,
  AST_NODE_TYPE_VAR_DECL,
  AST_NODE_TYPE_DO_WHILE,
  AST_NODE_TYPE_SWITCH,
  AST_NODE_TYPE_CASE,
  AST_NODE_TYPE_DEFAULT,
  AST_NODE_TYPE_BREAK,
  AST_NODE_TYPE_CONTINUE,
  AST_NODE_TYPE_STRUCT_DEF,
  AST_NODE_TYPE_ENUM_DEF,
  AST_NODE_TYPE_CAST,
  AST_NODE_TYPE_INIT_LIST,
  AST_NODE_TYPE_GOTO,
  AST_NODE_TYPE_LABEL,
  AST_NODE_TYPE_COMPOUND_LITERAL,
  AST_NODE_TYPE_ASM,
  AST_NODE_TYPE_BUILTIN
} ast_node_type;

typedef enum type_kind {
  TYPE_PRIMITIVE,
  TYPE_POINTER,
  TYPE_ARRAY,
  TYPE_FUNCTION,
  TYPE_UNKNOWN,
  TYPE_STRUCT,
  TYPE_UNION,
  TYPE_ENUM,
  TYPE_TYPEDEF
} type_kind;

typedef enum prim_kind {
  PRIM_NONE,
  PRIM_VOID,
  PRIM_BOOL,
  PRIM_CHAR,
  PRIM_SCHAR,
  PRIM_UCHAR,
  PRIM_SHORT,
  PRIM_USHORT,
  PRIM_INT,
  PRIM_UINT,
  PRIM_LONG,
  PRIM_ULONG,
  PRIM_LLONG,
  PRIM_ULLONG,
  PRIM_FLOAT,
  PRIM_DOUBLE,
  PRIM_LDOUBLE
} prim_kind;

struct symbol;

typedef struct type_info {
  type_kind kind;
  prim_kind prim;
  int is_const;
  int is_volatile;
  int is_restrict;
  int is_complex;                   // For _Complex
  int is_imaginary;                 // For _Imaginary
  char *tag_name;                   // For structs, enums, and typedefs
  struct type_info *ptr_to;         // For pointers and arrays
  long long array_size;             // For arrays (-1 if unspecified)
  struct ast_node *array_size_expr; // For Variable Length Arrays (VLAs)
  int is_vla;
  int array_static;
  int array_star;
  struct type_info **param_types; // For functions
  char **param_names;
  struct ast_node **param_definitions;
  int *param_register;
  int has_prototype;
  int from_definition;
  int param_count; // For functions
  int is_variadic; // For functions (e.g. printf)
  struct ast_node *definition;
  struct symbol *symbol;
} type_info;

typedef struct decl_specs {
  token_type storage_class;
  int is_inline;
  int aligned;
  int has_mode;
} decl_specs;

typedef struct source_loc {
  int line;
  int column;
  const char *file;
} source_loc;

typedef enum builtin_kind {
  BUILTIN_VA_START,
  BUILTIN_VA_ARG,
  BUILTIN_VA_END,
  BUILTIN_VA_COPY,
  BUILTIN_OFFSETOF,
  BUILTIN_TYPES_COMPATIBLE_P,
  BUILTIN_CHOOSE_EXPR,
  BUILTIN_HUGE_VAL,
  BUILTIN_HUGE_VALF,
  BUILTIN_HUGE_VALL,
  BUILTIN_INFF,
  BUILTIN_NANF,
  BUILTIN_ISGREATER,
  BUILTIN_ISGREATEREQUAL,
  BUILTIN_ISLESS,
  BUILTIN_ISLESSEQUAL,
  BUILTIN_ISLESSGREATER,
  BUILTIN_ISUNORDERED,
  BUILTIN_SIGNBIT,
  BUILTIN_SIGNBITF,
  BUILTIN_SIGNBITL,
  BUILTIN_LLABS,
  BUILTIN_TRAP,
  BUILTIN_UNREACHABLE,
  BUILTIN_TGMATH
} builtin_kind;

typedef struct ast_node {
  ast_node_type type;
  token tok;
  source_loc loc;
  struct symbol *symbol;
  struct type_info *expr_type;
  int is_lvalue;
  union {
    struct {
      char *bytes;
      int length;
      int is_wide;
    } literal;

    struct {
      struct ast_node **declarations;
      int count;
      struct type_info **derived_types;
      int derived_count;
      struct ast_node *builtins;
    } program;

    struct {
      token op;
      struct ast_node *left;
      struct ast_node *right;
    } binary_op;

    struct {
      token op;
      struct ast_node *operand;
      int is_postfix; // 1 for x++, 0 for ++x
    } unary_op;

    struct {
      token op;
      struct ast_node *left;
      struct ast_node *right;
    } assignment;

    struct {
      struct ast_node *condition;
      struct ast_node *true_branch;
      struct ast_node *false_branch;
    } ternary;

    struct {
      struct ast_node *condition;
      struct ast_node *then_branch;
      struct ast_node *else_branch;
    } if_stmt;

    struct {
      struct ast_node *condition;
      struct ast_node *body;
    } while_stmt;

    struct {
      struct ast_node *init;
      struct ast_node *condition;
      struct ast_node *increment;
      struct ast_node *body;
    } for_stmt;

    struct {
      struct ast_node **statements;
      int count;
    } block;

    struct {
      struct ast_node *return_value;
    } return_stmt;

    struct {
      struct ast_node *callable; // Changed from char* name to support function pointers
      struct ast_node **arguments;
      int arg_count;
    } function_call;
    struct {
      struct type_info *type;
      char *var_name;
      struct ast_node *init_value;
      struct ast_node *bitfield_width;
      decl_specs specs;
      long long offset;
      int bit_offset;
      int bit_width;
    } var_decl;

    struct {
      char *name;
      struct type_info *type;
      struct symbol **param_symbols;
      struct ast_node *body;
      decl_specs specs;
      struct type_info *name_type;
    } function_def;

    struct {
      struct ast_node *left;
      struct ast_node *index;
    } array_subscript;

    struct {
      struct ast_node *left;
      char *member_name;
      int is_pointer; // 1 for ->, 0 for .
      struct ast_node *member;
    } member_access;

    struct {
      struct ast_node *body;
      struct ast_node *condition;
    } do_while_stmt;

    struct {
      struct ast_node *condition;
      struct ast_node *body;
    } switch_stmt;

    struct {
      struct ast_node *value;
      struct ast_node *body;
    } case_stmt;

    struct {
      struct ast_node *body;
    } default_stmt;

    struct {
      char *tag_name;
      struct ast_node **members;
      int member_count;
      int is_forward;
      int is_union;
      int aligned;
      int pack;
    } struct_def;

    struct {
      char *tag_name;
      char **enumerators;
      struct ast_node **values;
      int enumerator_count;
    } enum_def;

    struct {
      struct type_info *type;
      struct ast_node *operand;
      struct ast_node *definition;
    } cast_expr;

    struct {
      struct {
        char *member_name;      // For .x
        struct ast_node *index; // For [0]
        struct ast_node *value; // The actual value
      } *items;
      int count;
      int is_designation;
    } init_list;

    struct {
      struct type_info *type;
      struct ast_node *init_list;
      struct ast_node *definition;
    } compound_literal;

    struct {
      char *label_name;
    } goto_stmt;

    struct {
      char *label_name;
      struct ast_node *statement;
    } label_stmt;

    struct {
      struct ast_node *template_text;
      int is_volatile;
      struct ast_node **constraints;
      struct ast_node **operands;
      int output_count;
      int operand_count;
      struct ast_node **clobbers;
      int clobber_count;
    } asm_stmt;

    struct {
      builtin_kind kind;
      struct ast_node **args;
      int arg_count;
      struct type_info *types[2];
      struct ast_node *type_exprs[2];
      struct ast_node *definitions[2];
      struct {
        char *member;
        struct ast_node *index;
      } *steps;
      int step_count;
      long long value;
      int has_value;
      struct ast_node *chosen;
    } builtin;
  };
} ast_node;

ast_node *create_ast_node(ast_node_type type);
void free_ast(ast_node *node);

type_info *create_type_info(type_kind kind);
void free_type_info(type_info *type);

#endif //_AST_H_
