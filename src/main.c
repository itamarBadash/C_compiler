#include <stdio.h>
#include <stdlib.h>
#include "../include/lexer.h"

const char* token_type_name(token_type type) {
  switch (type) {
    case TOKEN_EOF: return "TOKEN_EOF";
    case TOKEN_IDENTIFIER: return "TOKEN_IDENTIFIER";
    case TOKEN_NUMBER: return "TOKEN_NUMBER";
    case TOKEN_STRING: return "TOKEN_STRING";
    case TOKEN_CHAR_LITERAL: return "TOKEN_CHAR_LITERAL";
    case TOKEN_INT: return "TOKEN_INT";
    case TOKEN_VOID: return "TOKEN_VOID";
    case TOKEN_CHAR: return "TOKEN_CHAR";
    case TOKEN_FLOAT: return "TOKEN_FLOAT";
    case TOKEN_DOUBLE: return "TOKEN_DOUBLE";
    case TOKEN_LONG: return "TOKEN_LONG";
    case TOKEN_SHORT: return "TOKEN_SHORT";
    case TOKEN_UNSIGNED: return "TOKEN_UNSIGNED";
    case TOKEN_SIGNED: return "TOKEN_SIGNED";
    case TOKEN_RETURN: return "TOKEN_RETURN";
    case TOKEN_IF: return "TOKEN_IF";
    case TOKEN_ELSE: return "TOKEN_ELSE";
    case TOKEN_WHILE: return "TOKEN_WHILE";
    case TOKEN_FOR: return "TOKEN_FOR";
    case TOKEN_DO: return "TOKEN_DO";
    case TOKEN_SWITCH: return "TOKEN_SWITCH";
    case TOKEN_CASE: return "TOKEN_CASE";
    case TOKEN_DEFAULT: return "TOKEN_DEFAULT";
    case TOKEN_BREAK: return "TOKEN_BREAK";
    case TOKEN_CONTINUE: return "TOKEN_CONTINUE";
    case TOKEN_CONST: return "TOKEN_CONST";
    case TOKEN_STATIC: return "TOKEN_STATIC";
    case TOKEN_EXTERN: return "TOKEN_EXTERN";
    case TOKEN_TYPEDEF: return "TOKEN_TYPEDEF";
    case TOKEN_STRUCT: return "TOKEN_STRUCT";
    case TOKEN_ENUM: return "TOKEN_ENUM";
    case TOKEN_UNION: return "TOKEN_UNION";
    case TOKEN_SIZEOF: return "TOKEN_SIZEOF";
    case TOKEN_ASSIGN: return "TOKEN_ASSIGN";
    case TOKEN_PLUS: return "TOKEN_PLUS";
    case TOKEN_MINUS: return "TOKEN_MINUS";
    case TOKEN_STAR: return "TOKEN_STAR";
    case TOKEN_SLASH: return "TOKEN_SLASH";
    case TOKEN_PERCENT: return "TOKEN_PERCENT";
    case TOKEN_PLUS_PLUS: return "TOKEN_PLUS_PLUS";
    case TOKEN_MINUS_MINUS: return "TOKEN_MINUS_MINUS";
    case TOKEN_PLUS_ASSIGN: return "TOKEN_PLUS_ASSIGN";
    case TOKEN_MINUS_ASSIGN: return "TOKEN_MINUS_ASSIGN";
    case TOKEN_STAR_ASSIGN: return "TOKEN_STAR_ASSIGN";
    case TOKEN_SLASH_ASSIGN: return "TOKEN_SLASH_ASSIGN";
    case TOKEN_PERCENT_ASSIGN: return "TOKEN_PERCENT_ASSIGN";
    case TOKEN_EQ: return "TOKEN_EQ";
    case TOKEN_NEQ: return "TOKEN_NEQ";
    case TOKEN_LT: return "TOKEN_LT";
    case TOKEN_GT: return "TOKEN_GT";
    case TOKEN_LTE: return "TOKEN_LTE";
    case TOKEN_GTE: return "TOKEN_GTE";
    case TOKEN_AND: return "TOKEN_AND";
    case TOKEN_OR: return "TOKEN_OR";
    case TOKEN_NOT: return "TOKEN_NOT";
    case TOKEN_AMPERSAND: return "TOKEN_AMPERSAND";
    case TOKEN_PIPE: return "TOKEN_PIPE";
    case TOKEN_TILDE: return "TOKEN_TILDE";
    case TOKEN_DOT: return "TOKEN_DOT";
    case TOKEN_ARROW: return "TOKEN_ARROW";
    case TOKEN_SEMICOLON: return "TOKEN_SEMICOLON";
    case TOKEN_COMMA: return "TOKEN_COMMA";
    case TOKEN_COLON: return "TOKEN_COLON";
    case TOKEN_QUESTION: return "TOKEN_QUESTION";
    case TOKEN_LPAREN: return "TOKEN_LPAREN";
    case TOKEN_RPAREN: return "TOKEN_RPAREN";
    case TOKEN_LBRACE: return "TOKEN_LBRACE";
    case TOKEN_RBRACE: return "TOKEN_RBRACE";
    case TOKEN_LBRACKET: return "TOKEN_LBRACKET";
    case TOKEN_RBRACKET: return "TOKEN_RBRACKET";
    case TOKEN_UNKNOWN: return "TOKEN_UNKNOWN";
    default: return "UNKNOWN";
  }
}

int main() {
  // קוד הבדיקה שלנו שכולל מילים שמורות, משתנים, מספרים, רווחים וירידות שורה
  const char *source_code =
      "int my_var_1 = 100 + 50;\n"
      "return my_var_1;";

  printf("Analyzing source code:\n%s\n", source_code);
  printf("====================================================\n");
  printf("%-18s | %-12s | %-4s | %-4s\n", "TYPE", "VALUE", "LINE", "COL");
  printf("-------------------+--------------+------+------\n");

  lexer lex;
  lexer_init(&lex, source_code);

  token t;
  do {
    t = lexer_next_token(&lex);

    printf("%-18s | %-12s | %-4d | %-4d\n",
           token_type_name(t.type),
           t.value ? t.value : "NULL",
           t.line,
           t.column);

    if (t.value != NULL) {
      free(t.value);
    }

  } while (t.type != TOKEN_EOF);

  printf("====================================================\n");
  printf("Lexical analysis completed successfully.\n");

  return 0;
}