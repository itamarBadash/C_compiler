#ifndef _TOKEN_H_
#define _TOKEN_H_
typedef enum token_type {
    TOKEN_EOF,

    // Literals and identifiers
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_CHAR_LITERAL,

    // Keywords - types
    TOKEN_INT,
    TOKEN_VOID,
    TOKEN_CHAR,
    TOKEN_FLOAT,
    TOKEN_DOUBLE,
    TOKEN_LONG,
    TOKEN_SHORT,
    TOKEN_UNSIGNED,
    TOKEN_SIGNED,

    // Keywords - control flow
    TOKEN_RETURN,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_DO,
    TOKEN_SWITCH,
    TOKEN_CASE,
    TOKEN_DEFAULT,
    TOKEN_BREAK,
    TOKEN_CONTINUE,

    // Keywords - storage/qualifiers
    TOKEN_CONST,
    TOKEN_STATIC,
    TOKEN_EXTERN,
    TOKEN_TYPEDEF,

    // Keywords - composite types
    TOKEN_STRUCT,
    TOKEN_ENUM,
    TOKEN_UNION,

    // Keywords - operators
    TOKEN_SIZEOF,

    // Arithmetic operators
    TOKEN_ASSIGN,           // =
    TOKEN_PLUS,             // +
    TOKEN_MINUS,            // -
    TOKEN_STAR,             // *
    TOKEN_SLASH,            // /
    TOKEN_PERCENT,          // %

    // Increment/decrement
    TOKEN_PLUS_PLUS,        // ++
    TOKEN_MINUS_MINUS,      // --

    // Compound assignment
    TOKEN_PLUS_ASSIGN,      // +=
    TOKEN_MINUS_ASSIGN,     // -=
    TOKEN_STAR_ASSIGN,      // *=
    TOKEN_SLASH_ASSIGN,     // /=
    TOKEN_PERCENT_ASSIGN,   // %=

    // Comparison operators
    TOKEN_EQ,               // ==
    TOKEN_NEQ,              // !=
    TOKEN_LT,               // <
    TOKEN_GT,               // >
    TOKEN_LTE,              // <=
    TOKEN_GTE,              // >=

    // Logical operators
    TOKEN_AND,              // &&
    TOKEN_OR,               // ||
    TOKEN_NOT,              // !

    // Bitwise operators
    TOKEN_AMPERSAND,        // &
    TOKEN_PIPE,             // |
    TOKEN_TILDE,            // ~

    // Member access
    TOKEN_DOT,              // .
    TOKEN_ARROW,            // ->

    // Punctuation
    TOKEN_SEMICOLON,        // ;
    TOKEN_COMMA,            // ,
    TOKEN_COLON,            // :
    TOKEN_QUESTION,         // ?
    TOKEN_LPAREN,           // (
    TOKEN_RPAREN,           // )
    TOKEN_LBRACE,           // {
    TOKEN_RBRACE,           // }
    TOKEN_LBRACKET,         // [
    TOKEN_RBRACKET,         // ]

    TOKEN_UNKNOWN
} token_type;

typedef struct token {
    token_type type;
    char *value;
    int line;
    int column;
} token;
#endif //_TOKEN_H_
