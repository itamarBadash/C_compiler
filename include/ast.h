#ifndef _AST_H_
#define _AST_H_
#include "token.h"
typedef enum ast_node_type {
    AST_NODE_TYPE_UNKNOWN,
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
    AST_NODE_TYPE_DESIGNATOR
} ast_node_type;

typedef enum type_kind {
    TYPE_PRIMITIVE,
    TYPE_POINTER,
    TYPE_ARRAY,
    TYPE_FUNCTION,
    TYPE_UNKNOWN,
    TYPE_STRUCT,
    TYPE_ENUM,
    TYPE_TYPEDEF
} type_kind;

typedef struct type_info {
    type_kind kind;
    int is_const;
    int is_volatile;
    int is_restrict;
    int is_long_long; // For long long
    int is_complex;   // For _Complex
    int is_imaginary; // For _Imaginary
    int is_inline;    // For inline
    token_type storage_class; // TOKEN_STATIC, TOKEN_EXTERN, TOKEN_REGISTER, etc. (0 if none)
    token base_type; // For primitive types (e.g. TOKEN_INT)
    char *tag_name; // For structs, enums, and typedefs
    struct type_info *ptr_to; // For pointers and arrays
    int array_size; // For arrays (-1 if unspecified)
    struct ast_node *array_size_expr; // For Variable Length Arrays (VLAs)
    struct type_info **param_types; // For functions
    int param_count; // For functions
    int is_variadic; // For functions (e.g. printf)
} type_info;

typedef struct ast_node {
    ast_node_type type;
    union {
        token tok;
        struct {
            struct ast_node **declarations;
            int count;
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
            int is_typedef;
        } var_decl;

        struct {
            char *name;
            struct type_info *return_type;
            char **parameters;
            struct type_info **param_types;
            int param_count;
            struct ast_node *body;
        } function_def;

        struct {
            struct ast_node *left;
            struct ast_node *index;
        } array_subscript;

        struct {
            struct ast_node *left;
            char *member_name;
            int is_pointer; // 1 for ->, 0 for .
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
        } cast_expr;

        struct {
            struct {
                char *member_name;         // For .x
                struct ast_node *index;    // For [0]
                struct ast_node *value;    // The actual value
            } *items;
            int count;
        } init_list;

        struct {
            struct type_info *type;
            struct ast_node *init_list;
        } compound_literal;

        struct {
            char *member_name;
            struct ast_node *index;
        } designator;

        struct {
            char *label_name;
        } goto_stmt;

        struct {
            char *label_name;
            struct ast_node *statement;
        } label_stmt;
    };
} ast_node;

ast_node* create_ast_node(ast_node_type type);
void free_ast(ast_node *node);

type_info* create_type_info(type_kind kind);
void free_type_info(type_info *type);


#endif //_AST_H_
