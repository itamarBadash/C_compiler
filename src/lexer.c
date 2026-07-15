#include "lexer.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

struct keyword_map {
    const char *name;
    token_type type;
};

struct keyword_map *check_keyword(const char *str, size_t len);

void lexer_init (lexer *lex, const char *source)
{
    if (!source || !lex) return;

    lex->source = source;
    lex->current_char = source[0];
    lex->position = 0;
    lex->line = 1;
    lex->column = 1;
}

static char lexer_peek(lexer *lex)
{
    if (lex->current_char == '\0') {
        return '\0';
    }
    return lex->source[lex->position + 1];
}

static void lexer_advance(lexer *lex)
{
    if (lex->current_char != '\0') {
        if (lex->current_char == '\n') {
            lex->line++;
            lex->column = 1;
        } else {
            lex->column++;
        }
        lex->position++;
        lex->current_char = lex->source[lex->position];
    }
}

static int lexer_skip_whitespace_and_comments(lexer *lex)
{
    while (lex->current_char != '\0') {
        if (lex->current_char == ' ' || lex->current_char == '\t' ||
            lex->current_char == '\n' || lex->current_char == '\r') {
            lexer_advance(lex);
        } else if (lex->current_char == '/' && lexer_peek(lex) == '/') {
            while (lex->current_char != '\n' && lex->current_char != '\0') {
                lexer_advance(lex);
            }
        } else if (lex->current_char == '/' && lexer_peek(lex) == '*') {
            lexer_advance(lex);
            lexer_advance(lex);
            int terminated = 0;
            while (lex->current_char != '\0') {
                if (lex->current_char == '*' && lexer_peek(lex) == '/') {
                    lexer_advance(lex);
                    lexer_advance(lex);
                    terminated = 1;
                    break;
                }
                lexer_advance(lex);
            }
            if (!terminated) {
                return 0; // Error: unterminated block comment
            }
        } else {
            break;
        }
    }
    return 1;
}

static token lexer_make_token(lexer *lex, token_type type, const char *value)
{
    token tok;
    tok.type = type;
    tok.line = lex->line;
    tok.column = lex->column;
    if (value) {
        tok.value = strdup(value);
    } else {
        tok.value = NULL;
    }
    return tok;
}

static token lexer_collect_string(lexer *lex) {
    lexer_advance(lex);
    int start_position = lex->position;

    while (lex->current_char != '"' && lex->current_char != '\0') {
        if (lex->current_char == '\\') {
            lexer_advance(lex);
        }
        lexer_advance(lex);
    }

    if (lex->current_char == '\0') {
        return lexer_make_token(lex, TOKEN_UNKNOWN, "Unterminated string");
    }

    int length = lex->position - start_position;

    char* temp_str = (char*)malloc(length + 1);
    if (!temp_str) return lexer_make_token(lex, TOKEN_UNKNOWN, NULL);
    strncpy(temp_str, lex->source + start_position, length);
    temp_str[length] = '\0';

    token tok = lexer_make_token(lex, TOKEN_STRING, temp_str);

    free(temp_str);

    lexer_advance(lex);

    return tok;
}

static token lexer_collect_number(lexer *lex) {
    int start_position = lex->position;

    int has_dot = 0;
    int is_hex = 0;
    
    if (lex->current_char == '0') {
        char next = lex->source[lex->position + 1];
        if (next == 'x' || next == 'X') {
            is_hex = 1;
            lexer_advance(lex); // consume '0'
            lexer_advance(lex); // consume 'x'
        }
    }

    while (1) {
        char c = lex->current_char;
        if (isdigit(c) || (is_hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))) {
            lexer_advance(lex);
        } else if (c == '.' && !has_dot) {
            has_dot = 1;
            lexer_advance(lex);
        } else if ((!is_hex && (c == 'e' || c == 'E')) || (is_hex && (c == 'p' || c == 'P'))) {
            // Exponent part
            lexer_advance(lex);
            if (lex->current_char == '+' || lex->current_char == '-') {
                lexer_advance(lex);
            }
            // Exponent must have at least one digit
            while (isdigit(lex->current_char)) {
                lexer_advance(lex);
            }
            break; // Number ends after exponent
        } else {
            break; // Suffixes like 'f', 'u', 'l' can be appended
        }
    }
    
    // Consume suffixes (e.g., u, l, ll, f, F)
    while (isalpha(lex->current_char)) {
        lexer_advance(lex);
    }

    int length = lex->position - start_position;

    char* temp_str = (char*)malloc(length + 1);
    if (!temp_str) return lexer_make_token(lex, TOKEN_UNKNOWN, NULL);

    strncpy(temp_str, lex->source + start_position, length);
    temp_str[length] = '\0';

    token tok = lexer_make_token(lex, TOKEN_NUMBER, temp_str);

    free(temp_str);

    return tok;
}

static token lexer_collect_identifier(lexer *lex) {
    int start_position = lex->position;

    while (isalnum(lex->current_char) || lex->current_char == '_') {
        lexer_advance(lex);
    }

    int length = lex->position - start_position;

    char* temp_str = (char*)malloc(length + 1);
    if (!temp_str) return lexer_make_token(lex, TOKEN_UNKNOWN, NULL);
    strncpy(temp_str, lex->source + start_position, length);
    temp_str[length] = '\0';

    const struct keyword_map *kw = check_keyword(temp_str, length);

    token_type type;
    if (kw != NULL) {
        type = kw->type;
    } else if (strcmp(temp_str, "goto") == 0) {
        type = TOKEN_GOTO;
    } else if (strcmp(temp_str, "volatile") == 0) {
        type = TOKEN_VOLATILE;
    } else if (strcmp(temp_str, "restrict") == 0) {
        type = TOKEN_RESTRICT;
    } else if (strcmp(temp_str, "register") == 0) {
        type = TOKEN_REGISTER;
    } else if (strcmp(temp_str, "inline") == 0) {
        type = TOKEN_INLINE;
    } else if (strcmp(temp_str, "_Complex") == 0) {
        type = TOKEN_COMPLEX;
    } else if (strcmp(temp_str, "_Imaginary") == 0) {
        type = TOKEN_IMAGINARY;
    } else if (strcmp(temp_str, "_Bool") == 0) {
        type = TOKEN_BOOL;
    } else {
        type = TOKEN_IDENTIFIER;
    }

    token tok = lexer_make_token(lex, type, temp_str);

    free(temp_str);

    return tok;
}

static token lexer_collect_char_literal(lexer *lex) {
    lexer_advance(lex);

    if (lex->current_char == '\'') {
        lexer_advance(lex);
        return lexer_make_token(lex, TOKEN_UNKNOWN, "Empty char literal");
    }

    int start_position = lex->position;

    if (lex->current_char == '\\') {
        lexer_advance(lex);
        if (lex->current_char == '\0') {
            return lexer_make_token(lex, TOKEN_UNKNOWN, "Unterminated char literal");
        }
    }

    lexer_advance(lex);

    if (lex->current_char != '\'') {
        if (lex->current_char == '\0') {
            return lexer_make_token(lex, TOKEN_UNKNOWN, "Unterminated char literal");
        } else {
            return lexer_make_token(lex, TOKEN_UNKNOWN, "Invalid char literal");
        }
    }

    lexer_advance(lex);

    int length = lex->position - start_position - 1;
    char* temp_str = (char*)malloc(length + 1);
    if (!temp_str) return lexer_make_token(lex, TOKEN_UNKNOWN, NULL);
    strncpy(temp_str, lex->source + start_position, length);
    temp_str[length] = '\0';

    token tok = lexer_make_token(lex, TOKEN_CHAR_LITERAL, temp_str);
    free(temp_str);
    return tok;
}

token lexer_next_token(lexer *lex)
{
    if(!lex || !lex->source) {
        token tok = {TOKEN_UNKNOWN, NULL, 0, 0};
        return tok;
    }

    int start_line = lex->line;
    int start_column = lex->column;

    if (!lexer_skip_whitespace_and_comments(lex)) {
        token tok = lexer_make_token(lex, TOKEN_UNKNOWN, "Unterminated block comment");
        tok.line = start_line;
        tok.column = start_column;
        return tok;
    }

    start_line = lex->line;
    start_column = lex->column;

    token tok;
    switch (lex->current_char)
    {
        case '\0':
            tok = lexer_make_token(lex, TOKEN_EOF, NULL);
        break;
        case '=':
            if (lexer_peek(lex) == '=')
            {
                tok = lexer_make_token(lex, TOKEN_EQ, "==");
                lexer_advance(lex);
            }
            else
            {
                tok = lexer_make_token(lex, TOKEN_ASSIGN, "=");
            }
            lexer_advance(lex);
        break;
        case '+':
            if (lexer_peek(lex) == '+') {
                tok = lexer_make_token(lex, TOKEN_PLUS_PLUS, "++");
                lexer_advance(lex);
            } else if (lexer_peek(lex) == '=') {
                tok = lexer_make_token(lex, TOKEN_PLUS_ASSIGN, "+=");
                lexer_advance(lex);
            } else {
                tok = lexer_make_token(lex, TOKEN_PLUS, "+");
            }
            lexer_advance(lex);
        break;
        case '-':
            if (lexer_peek(lex) == '-') {
                tok = lexer_make_token(lex, TOKEN_MINUS_MINUS, "--");
                lexer_advance(lex);
            } else if (lexer_peek(lex) == '=') {
                tok = lexer_make_token(lex, TOKEN_MINUS_ASSIGN, "-=");
                lexer_advance(lex);
            } else if (lexer_peek(lex) == '>') {
                tok = lexer_make_token(lex, TOKEN_ARROW, "->");
                lexer_advance(lex);
            } else {
                tok = lexer_make_token(lex, TOKEN_MINUS, "-");
            }
            lexer_advance(lex);
        break;
        case '*':
            if (lexer_peek(lex) == '=') {
                tok = lexer_make_token(lex, TOKEN_STAR_ASSIGN, "*=");
                lexer_advance(lex);
            } else {
                tok = lexer_make_token(lex, TOKEN_STAR, "*");
            }
            lexer_advance(lex);
        break;
        case '/':
            if (lexer_peek(lex) == '=') {
                tok = lexer_make_token(lex, TOKEN_SLASH_ASSIGN, "/=");
                lexer_advance(lex);
            } else {
                tok = lexer_make_token(lex, TOKEN_SLASH, "/");
            }
            lexer_advance(lex);
        break;
        case ';':
            tok =  lexer_make_token(lex, TOKEN_SEMICOLON, ";");
            lexer_advance(lex);

        break;
        case '(':
            tok =  lexer_make_token(lex, TOKEN_LPAREN, "(");
            lexer_advance(lex);
        break;
        case ')':
            tok =  lexer_make_token(lex, TOKEN_RPAREN, ")");
            lexer_advance(lex);
        break;
        case '{':
            tok =  lexer_make_token(lex, TOKEN_LBRACE, "{");
            lexer_advance(lex);
        break;
        case '}':
            tok = lexer_make_token(lex, TOKEN_RBRACE, "}");
            lexer_advance(lex);
        break;
        case ',':
            tok = lexer_make_token(lex, TOKEN_COMMA, ",");
            lexer_advance(lex);
        break;
        case ':':
            tok = lexer_make_token(lex, TOKEN_COLON, ":");
            lexer_advance(lex);
        break;
        case '?':
            tok = lexer_make_token(lex, TOKEN_QUESTION, "?");
            lexer_advance(lex);
        break;
        case '~':
            tok = lexer_make_token(lex, TOKEN_TILDE, "~");
            lexer_advance(lex);
        break;
        case '%':
            if (lexer_peek(lex) == '=') {
                tok = lexer_make_token(lex, TOKEN_PERCENT_ASSIGN, "%=");
                lexer_advance(lex);
            } else {
                tok = lexer_make_token(lex, TOKEN_PERCENT, "%");
            }
            lexer_advance(lex);
        break;
        case '[':
            tok = lexer_make_token(lex, TOKEN_LBRACKET, "[");
            lexer_advance(lex);
        break;
        case ']':
            tok = lexer_make_token(lex, TOKEN_RBRACKET, "]");
            lexer_advance(lex);
        break;
        case '.':
        if (lex->source[lex->position] == '.' && lex->source[lex->position+1] == '.') {
            lexer_advance(lex);
            lexer_advance(lex);
            lexer_advance(lex);
            tok.type = TOKEN_ELLIPSIS;
            tok.value = strdup("...");
        } else {
            tok.type = TOKEN_DOT;
            tok.value = strdup(".");
            lexer_advance(lex);
        }
        break;
        case '<':
            if (lexer_peek (lex) == '=')
            {
                tok = lexer_make_token (lex, TOKEN_LTE, "<=");
                lexer_advance (lex);
            }
            else if (lexer_peek(lex) == '<') 
            {
                lexer_advance(lex);
                if (lexer_peek(lex) == '=') {
                    tok = lexer_make_token(lex, TOKEN_LSHIFT_ASSIGN, "<<=");
                    lexer_advance(lex);
                } else {
                    tok = lexer_make_token(lex, TOKEN_LSHIFT, "<<");
                }
            }
            else
            {
                tok = lexer_make_token (lex, TOKEN_LT, "<");
            }
        lexer_advance (lex);
        break;
        case '>':
            if (lexer_peek (lex) == '=')
            {
                tok = lexer_make_token (lex, TOKEN_GTE, ">=");
                lexer_advance (lex);
            }
            else if (lexer_peek(lex) == '>') 
            {
                lexer_advance(lex);
                if (lexer_peek(lex) == '=') {
                    tok = lexer_make_token(lex, TOKEN_RSHIFT_ASSIGN, ">>=");
                    lexer_advance(lex);
                } else {
                    tok = lexer_make_token(lex, TOKEN_RSHIFT, ">>");
                }
            }
            else
            {
                tok = lexer_make_token (lex, TOKEN_GT, ">");
            }
        lexer_advance (lex);
        break;
        case '!':
            if (lexer_peek (lex) == '=')
            {
                tok = lexer_make_token (lex, TOKEN_NEQ, "!=");
                lexer_advance (lex);
            }
            else
            {
                tok = lexer_make_token (lex, TOKEN_NOT, "!");
            }
        lexer_advance (lex);
        break;
        case '&':
            if (lexer_peek (lex) == '&')
            {
                tok = lexer_make_token (lex, TOKEN_AND, "&&");
                lexer_advance (lex);
            }
            else if (lexer_peek(lex) == '=') 
            {
                tok = lexer_make_token(lex, TOKEN_AMPERSAND_ASSIGN, "&=");
                lexer_advance(lex);
            }
            else
            {
                tok = lexer_make_token(lex, TOKEN_AMPERSAND, "&");
            }
        lexer_advance (lex);
        break;
        case '|':
            if (lexer_peek (lex) == '|')
            {
                tok = lexer_make_token (lex, TOKEN_OR, "||");
                lexer_advance (lex);
            }
            else if (lexer_peek(lex) == '=') 
            {
                tok = lexer_make_token(lex, TOKEN_PIPE_ASSIGN, "|=");
                lexer_advance(lex);
            }
            else
            {
                tok = lexer_make_token(lex, TOKEN_PIPE, "|");
            }
        lexer_advance (lex);
        break;
        case '^':
            if (lexer_peek(lex) == '=') {
                tok = lexer_make_token(lex, TOKEN_CARET_ASSIGN, "^=");
                lexer_advance(lex);
            } else {
                tok = lexer_make_token(lex, TOKEN_CARET, "^");
            }
            lexer_advance(lex);
        break;
        case '\'':
            tok = lexer_collect_char_literal(lex);
        break;
        case '"':
            tok = lexer_collect_string(lex);
        break;

        default:
            if (isalpha(lex->current_char) || lex->current_char == '_') {
                tok = lexer_collect_identifier(lex);
            } else if (isdigit(lex->current_char)) {
                tok = lexer_collect_number(lex);
            } else {
                tok = lexer_make_token(lex, TOKEN_UNKNOWN, NULL);
                lexer_advance(lex);
            }
            break;
    }
    tok.line = start_line;
    tok.column = start_column;
    return tok;
}