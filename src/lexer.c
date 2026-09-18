#include "lexer.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct keyword {
  const char *name;
  token_type type;
} keyword;

static const keyword keywords[] = {
    {"_Bool", TOKEN_BOOL},
    {"_Complex", TOKEN_COMPLEX},
    {"_Imaginary", TOKEN_IMAGINARY},
    {"auto", TOKEN_AUTO},
    {"break", TOKEN_BREAK},
    {"case", TOKEN_CASE},
    {"char", TOKEN_CHAR},
    {"const", TOKEN_CONST},
    {"continue", TOKEN_CONTINUE},
    {"default", TOKEN_DEFAULT},
    {"do", TOKEN_DO},
    {"double", TOKEN_DOUBLE},
    {"else", TOKEN_ELSE},
    {"enum", TOKEN_ENUM},
    {"extern", TOKEN_EXTERN},
    {"float", TOKEN_FLOAT},
    {"for", TOKEN_FOR},
    {"goto", TOKEN_GOTO},
    {"if", TOKEN_IF},
    {"inline", TOKEN_INLINE},
    {"int", TOKEN_INT},
    {"long", TOKEN_LONG},
    {"register", TOKEN_REGISTER},
    {"restrict", TOKEN_RESTRICT},
    {"return", TOKEN_RETURN},
    {"short", TOKEN_SHORT},
    {"signed", TOKEN_SIGNED},
    {"sizeof", TOKEN_SIZEOF},
    {"static", TOKEN_STATIC},
    {"struct", TOKEN_STRUCT},
    {"switch", TOKEN_SWITCH},
    {"typedef", TOKEN_TYPEDEF},
    {"union", TOKEN_UNION},
    {"unsigned", TOKEN_UNSIGNED},
    {"void", TOKEN_VOID},
    {"volatile", TOKEN_VOLATILE},
    {"while", TOKEN_WHILE},
};

static const size_t keyword_count = sizeof keywords / sizeof keywords[0];

static int compare_keyword(const void *name, const void *entry) {
  return strcmp((const char *)name, ((const keyword *)entry)->name);
}

static token_type keyword_type(const char *name) {
  const keyword *found =
      (const keyword *)bsearch(name, keywords, keyword_count, sizeof keywords[0], compare_keyword);
  return found ? found->type : TOKEN_IDENTIFIER;
}

int token_is_keyword(token_type type) {
  for (size_t i = 0; i < keyword_count; i++) {
    if (keywords[i].type == type)
      return 1;
  }
  return 0;
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static int ucn_at(const char *s, unsigned long *code_point) {
  if (s[0] != '\\' || (s[1] != 'u' && s[1] != 'U'))
    return 0;
  int digits = s[1] == 'u' ? 4 : 8;
  unsigned long value = 0;
  for (int i = 0; i < digits; i++) {
    int digit = hex_digit(s[2 + i]);
    if (digit < 0)
      return 0;
    value = value * 16 + (unsigned long)digit;
  }
  if (code_point)
    *code_point = value;
  return digits + 2;
}

static int ucn_is_valid(unsigned long code_point) {
  if (code_point < 0xA0 && code_point != 0x24 && code_point != 0x40 && code_point != 0x60)
    return 0;
  return (code_point < 0xD800 || code_point > 0xDFFF) && code_point <= 0x10FFFF;
}

static int utf8_decode(const char *s, unsigned long *code_point) {
  static const unsigned long smallest[] = {0, 0, 0x80, 0x800, 0x10000};
  unsigned char lead = (unsigned char)s[0];
  int length;
  if (lead < 0x80)
    length = 1;
  else if (lead < 0xC2)
    return 0;
  else if (lead < 0xE0)
    length = 2;
  else if (lead < 0xF0)
    length = 3;
  else if (lead < 0xF5)
    length = 4;
  else
    return 0;
  unsigned long value = length == 1 ? lead : lead & (0x7F >> length);
  for (int i = 1; i < length; i++) {
    unsigned char next = (unsigned char)s[i];
    if ((next & 0xC0) != 0x80)
      return 0;
    value = (value << 6) | (next & 0x3F);
  }
  if (value < smallest[length] || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
    return 0;
  *code_point = value;
  return length;
}

static void put_unit(char *out, int *units, int wide, unsigned long value) {
  if (wide) {
    out[*units * 2] = (char)(value & 0xFF);
    out[*units * 2 + 1] = (char)((value >> 8) & 0xFF);
  } else {
    out[*units] = (char)(value & 0xFF);
  }
  (*units)++;
}

static void put_code_point(char *out, int *units, int wide, unsigned long code_point) {
  if (wide && code_point > 0xFFFF) {
    put_unit(out, units, 1, 0xD800 | ((code_point - 0x10000) >> 10));
    put_unit(out, units, 1, 0xDC00 | (code_point & 0x3FF));
  } else if (wide || code_point < 0x80) {
    put_unit(out, units, wide, code_point);
  } else if (code_point < 0x800) {
    put_unit(out, units, 0, 0xC0 | (code_point >> 6));
    put_unit(out, units, 0, 0x80 | (code_point & 0x3F));
  } else if (code_point < 0x10000) {
    put_unit(out, units, 0, 0xE0 | (code_point >> 12));
    put_unit(out, units, 0, 0x80 | ((code_point >> 6) & 0x3F));
    put_unit(out, units, 0, 0x80 | (code_point & 0x3F));
  } else {
    put_unit(out, units, 0, 0xF0 | (code_point >> 18));
    put_unit(out, units, 0, 0x80 | ((code_point >> 12) & 0x3F));
    put_unit(out, units, 0, 0x80 | ((code_point >> 6) & 0x3F));
    put_unit(out, units, 0, 0x80 | (code_point & 0x3F));
  }
}

static const char simple_escapes[] = "n\nt\tr\ra\ab\bf\fv\v\\\\''\"\"??";

static int simple_escape(char c, unsigned long *value) {
  for (const char *e = simple_escapes; *e; e += 2) {
    if (*e == c) {
      *value = (unsigned char)e[1];
      return 1;
    }
  }
  return 0;
}

static void keep_first(const char **error, const char *message) {
  if (!*error)
    *error = message;
}

const char *decode_literal(const char *spelling, int wide, char *out, int *units) {
  const char *error = NULL;
  unsigned long limit = wide ? 0xFFFF : 0xFF;
  const char *s = spelling;
  while (*s) {
    unsigned long value;
    if (*s != '\\') {
      int length = wide ? utf8_decode(s, &value) : 0;
      if (length > 0) {
        put_code_point(out, units, 1, value);
        s += length;
        continue;
      }
      if (wide)
        keep_first(&error, "invalid UTF-8 in a wide literal");
      put_unit(out, units, wide, (unsigned char)*s++);
      continue;
    }

    int ucn = ucn_at(s, &value);
    if (ucn) {
      if (ucn_is_valid(value))
        put_code_point(out, units, wide, value);
      else
        keep_first(&error, "invalid universal character name");
      s += ucn;
      continue;
    }

    s++;
    if (*s == '\0') {
      keep_first(&error, "stray backslash at end of literal");
      break;
    }
    if (simple_escape(*s, &value)) {
      s++;
    } else if (*s >= '0' && *s <= '7') {
      value = 0;
      for (int digits = 0; digits < 3 && *s >= '0' && *s <= '7'; digits++)
        value = value * 8 + (unsigned long)(*s++ - '0');
      if (value > limit)
        keep_first(&error, "octal escape sequence out of range");
    } else if (*s == 'x') {
      s++;
      if (hex_digit(*s) < 0) {
        keep_first(&error, "\\x used with no following hex digits");
        continue;
      }
      value = 0;
      for (; hex_digit(*s) >= 0; s++) {
        value = value * 16 + (unsigned long)hex_digit(*s);
        if (value > limit)
          keep_first(&error, "hex escape sequence out of range");
      }
    } else if (*s == 'u' || *s == 'U') {
      keep_first(&error, "incomplete universal character name");
      s++;
      continue;
    } else {
      keep_first(&error, "unknown escape sequence in literal");
      value = (unsigned char)*s++;
    }
    put_unit(out, units, wide, value);
  }
  return error;
}

long long char_constant_value(const char *bytes, int units, int wide) {
  if (units < 1)
    return 0;
  if (wide)
    return (unsigned char)bytes[units * 2 - 2] | (unsigned char)bytes[units * 2 - 1] << 8;
  if (units == 1)
    return (signed char)bytes[0];
  unsigned int value = 0;
  for (int i = 0; i < units; i++)
    value = (value << 8) | (unsigned char)bytes[i];
  return (int)value;
}

int integer_suffix(const char *suffix, int *is_unsigned, int *long_count) {
  const char *s = suffix;
  *is_unsigned = 0;
  *long_count = 0;
  if (*s == 'u' || *s == 'U') {
    *is_unsigned = 1;
    s++;
  }
  if (*s == 'l' || *s == 'L') {
    *long_count = s[1] == s[0] ? 2 : 1;
    s += *long_count;
  }
  if (!*is_unsigned && (*s == 'u' || *s == 'U')) {
    *is_unsigned = 1;
    s++;
  }
  return *s == '\0';
}

static const char *skip_digits(const char *s, int hex) {
  while (hex ? hex_digit(*s) >= 0 : (*s >= '0' && *s <= '9'))
    s++;
  return s;
}

static const char *skip_octal(const char *s) {
  while (*s >= '0' && *s <= '7')
    s++;
  return s;
}

static number_kind invalid_number(const char **error, const char *message) {
  if (error)
    *error = message;
  return NUMBER_INVALID;
}

number_kind classify_number(const char *spelling, const char **error) {
  if (error)
    *error = NULL;
  const char *s = spelling;
  int hex = s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
  if (hex)
    s += 2;
  const char *start = s;
  s = skip_digits(s, hex);
  int digits = s > start;
  int dot = *s == '.';
  if (dot) {
    const char *fraction = ++s;
    s = skip_digits(s, hex);
    digits = digits || s > fraction;
  }
  int exponent = hex ? (*s == 'p' || *s == 'P') : (*s == 'e' || *s == 'E');

  if (!dot && !exponent) {
    int is_unsigned, long_count;
    if (!digits)
      return invalid_number(error, "hexadecimal constant has no digits");
    if (spelling[0] == '0' && skip_digits(spelling, 0) != skip_octal(spelling))
      return invalid_number(error, "invalid digit in an octal constant");
    if (!integer_suffix(s, &is_unsigned, &long_count))
      return invalid_number(error, "invalid suffix on an integer constant");
    return NUMBER_INTEGER;
  }

  if (!digits)
    return invalid_number(error, "floating constant has no digits");
  if (exponent) {
    s++;
    if (*s == '+' || *s == '-')
      s++;
    const char *power = s;
    s = skip_digits(s, 0);
    if (s == power)
      return invalid_number(error, "exponent has no digits");
  } else if (hex) {
    return invalid_number(error, "hexadecimal floating constant requires an exponent");
  }
  if (*s == 'f' || *s == 'F' || *s == 'l' || *s == 'L')
    s++;
  if (*s != '\0')
    return invalid_number(error, "invalid suffix on a floating constant");
  return NUMBER_FLOATING;
}

void lexer_init(lexer *lex, const char *source) {
  if (!source || !lex)
    return;

  lex->source = source;
  lex->current_char = source[0];
  lex->position = 0;
  lex->line = 1;
  lex->column = 1;
  lex->at_line_start = 1;
  lex->error_count = 0;
  lex->file = NULL;
  lex->line_offset = 0;
}

static void lexer_error(lexer *lex, int line, int column, const char *message) {
  if (!lex)
    return;
  lex->error_count++;
  if (lex->file)
    fprintf(stderr, "%s:", lex->file);
  fprintf(stderr, "%d:%d: error: %s\n", line + lex->line_offset, column, message);
}

static char lexer_peek(lexer *lex) {
  if (lex->current_char == '\0') {
    return '\0';
  }
  return lex->source[lex->position + 1];
}

static void lexer_advance(lexer *lex) {
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

static int lexer_skip_whitespace_and_comments(lexer *lex) {
  while (lex->current_char != '\0') {
    if (lex->current_char == ' ' || lex->current_char == '\t' || lex->current_char == '\n' ||
        lex->current_char == '\r' || lex->current_char == '\f' || lex->current_char == '\v') {
      if (lex->current_char == '\n') {
        lex->at_line_start = 1;
      }
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

static token lexer_make_token(lexer *lex, token_type type, const char *value) {
  token tok;
  tok.type = type;
  tok.line = lex->line;
  tok.column = lex->column;
  tok.at_line_start = 0;
  tok.file = lex->file;
  if (value) {
    tok.value = strdup(value);
  } else {
    tok.value = NULL;
  }
  return tok;
}

static token lexer_collect_quoted(lexer *lex, token_type type, char quote) {
  int start_line = lex->line;
  int start_column = lex->column;

  lexer_advance(lex);
  int start_position = lex->position;

  while (lex->current_char != quote && lex->current_char != '\n' && lex->current_char != '\0') {
    if (lex->current_char == '\\' && lexer_peek(lex) != '\n')
      lexer_advance(lex);
    lexer_advance(lex);
  }

  if (lex->current_char != quote) {
    lexer_error(lex, start_line, start_column,
                quote == '"' ? "unterminated string literal" : "unterminated character constant");
    return lexer_make_token(lex, TOKEN_UNKNOWN, NULL);
  }

  int length = lex->position - start_position;
  lexer_advance(lex);
  if (quote == '\'' && length == 0) {
    lexer_error(lex, start_line, start_column, "empty character constant");
    return lexer_make_token(lex, TOKEN_UNKNOWN, NULL);
  }

  char *temp_str = (char *)malloc(length + 1);
  if (!temp_str)
    return lexer_make_token(lex, TOKEN_UNKNOWN, NULL);
  memcpy(temp_str, lex->source + start_position, length);
  temp_str[length] = '\0';

  token tok = lexer_make_token(lex, type, temp_str);
  free(temp_str);
  return tok;
}

static token lexer_collect_number(lexer *lex) {
  const char *s = lex->source + lex->position;
  int length = 1;
  for (;;) {
    char previous = s[length - 1];
    int ucn = ucn_at(s + length, NULL);
    if ((s[length] == '+' || s[length] == '-') &&
        (previous == 'e' || previous == 'E' || previous == 'p' || previous == 'P'))
      length++;
    else if (isalnum((unsigned char)s[length]) || s[length] == '_' || s[length] == '.')
      length++;
    else if (ucn)
      length += ucn;
    else
      break;
  }

  char *temp_str = (char *)malloc(length + 1);
  if (!temp_str)
    return lexer_make_token(lex, TOKEN_UNKNOWN, NULL);
  memcpy(temp_str, s, length);
  temp_str[length] = '\0';
  for (int i = 0; i < length; i++)
    lexer_advance(lex);

  token tok = lexer_make_token(lex, TOKEN_NUMBER, temp_str);
  free(temp_str);
  return tok;
}

static token lexer_collect_identifier(lexer *lex) {
  const char *s = lex->source + lex->position;
  unsigned long code_point;
  int length = 0;
  for (;;) {
    int ucn = ucn_at(s + length, &code_point);
    if (ucn)
      length += ucn;
    else if (isalnum((unsigned char)s[length]) || s[length] == '_')
      length++;
    else
      break;
  }

  char *temp_str = (char *)malloc(length + 1);
  if (!temp_str)
    return lexer_make_token(lex, TOKEN_UNKNOWN, NULL);
  int kept = 0;
  for (int i = 0; i < length;) {
    int ucn = ucn_at(s + i, &code_point);
    if (!ucn) {
      temp_str[kept++] = s[i++];
      continue;
    }
    if (!ucn_is_valid(code_point))
      lexer_error(lex, lex->line, lex->column + i,
                  "invalid universal character name in identifier");
    kept += sprintf(temp_str + kept, code_point > 0xFFFF ? "\\U%08lx" : "\\u%04lx", code_point);
    i += ucn;
  }
  temp_str[kept] = '\0';
  for (int i = 0; i < length; i++)
    lexer_advance(lex);

  token_type type = keyword_type(temp_str);
  token tok = lexer_make_token(lex, type, temp_str);
  free(temp_str);
  return tok;
}

token lexer_next_token(lexer *lex) {
  if (!lex || !lex->source) {
    token tok = {TOKEN_UNKNOWN, NULL, 0, 0, 0, NULL};
    return tok;
  }

  int start_line = lex->line;
  int start_column = lex->column;

  if (!lexer_skip_whitespace_and_comments(lex)) {
    lexer_error(lex, start_line, start_column, "unterminated block comment");
    token tok = lexer_make_token(lex, TOKEN_UNKNOWN, NULL);
    tok.line = start_line + lex->line_offset;
    tok.column = start_column;
    tok.at_line_start = lex->at_line_start;
    return tok;
  }

  start_line = lex->line;
  start_column = lex->column;

  int start_of_line = lex->at_line_start;
  lex->at_line_start = 0;

  token tok;
  switch (lex->current_char) {
  case '\0':
    tok = lexer_make_token(lex, TOKEN_EOF, NULL);
    break;
  case '#':
    if (lexer_peek(lex) == '#') {
      tok = lexer_make_token(lex, TOKEN_HASH_HASH, "##");
      lexer_advance(lex);
    } else {
      tok = lexer_make_token(lex, TOKEN_HASH, "#");
    }
    lexer_advance(lex);
    break;
  case '=':
    if (lexer_peek(lex) == '=') {
      tok = lexer_make_token(lex, TOKEN_EQ, "==");
      lexer_advance(lex);
    } else {
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
    tok = lexer_make_token(lex, TOKEN_SEMICOLON, ";");
    lexer_advance(lex);

    break;
  case '(':
    tok = lexer_make_token(lex, TOKEN_LPAREN, "(");
    lexer_advance(lex);
    break;
  case ')':
    tok = lexer_make_token(lex, TOKEN_RPAREN, ")");
    lexer_advance(lex);
    break;
  case '{':
    tok = lexer_make_token(lex, TOKEN_LBRACE, "{");
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
    if (lexer_peek(lex) == '>') {
      tok = lexer_make_token(lex, TOKEN_RBRACKET, ":>");
      lexer_advance(lex);
    } else {
      tok = lexer_make_token(lex, TOKEN_COLON, ":");
    }
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
    } else if (lexer_peek(lex) == '>') {
      tok = lexer_make_token(lex, TOKEN_RBRACE, "%>");
      lexer_advance(lex);
    } else if (lexer_peek(lex) == ':' && lex->source[lex->position + 2] == '%' &&
               lex->source[lex->position + 3] == ':') {
      tok = lexer_make_token(lex, TOKEN_HASH_HASH, "%:%:");
      lexer_advance(lex);
      lexer_advance(lex);
      lexer_advance(lex);
    } else if (lexer_peek(lex) == ':') {
      tok = lexer_make_token(lex, TOKEN_HASH, "%:");
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
    if (isdigit((unsigned char)lexer_peek(lex))) {
      tok = lexer_collect_number(lex);
    } else if (lexer_peek(lex) == '.' && lex->source[lex->position + 2] == '.') {
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
    if (lexer_peek(lex) == ':') {
      tok = lexer_make_token(lex, TOKEN_LBRACKET, "<:");
      lexer_advance(lex);
    } else if (lexer_peek(lex) == '%') {
      tok = lexer_make_token(lex, TOKEN_LBRACE, "<%");
      lexer_advance(lex);
    } else if (lexer_peek(lex) == '=') {
      tok = lexer_make_token(lex, TOKEN_LTE, "<=");
      lexer_advance(lex);
    } else if (lexer_peek(lex) == '<') {
      lexer_advance(lex);
      if (lexer_peek(lex) == '=') {
        tok = lexer_make_token(lex, TOKEN_LSHIFT_ASSIGN, "<<=");
        lexer_advance(lex);
      } else {
        tok = lexer_make_token(lex, TOKEN_LSHIFT, "<<");
      }
    } else {
      tok = lexer_make_token(lex, TOKEN_LT, "<");
    }
    lexer_advance(lex);
    break;
  case '>':
    if (lexer_peek(lex) == '=') {
      tok = lexer_make_token(lex, TOKEN_GTE, ">=");
      lexer_advance(lex);
    } else if (lexer_peek(lex) == '>') {
      lexer_advance(lex);
      if (lexer_peek(lex) == '=') {
        tok = lexer_make_token(lex, TOKEN_RSHIFT_ASSIGN, ">>=");
        lexer_advance(lex);
      } else {
        tok = lexer_make_token(lex, TOKEN_RSHIFT, ">>");
      }
    } else {
      tok = lexer_make_token(lex, TOKEN_GT, ">");
    }
    lexer_advance(lex);
    break;
  case '!':
    if (lexer_peek(lex) == '=') {
      tok = lexer_make_token(lex, TOKEN_NEQ, "!=");
      lexer_advance(lex);
    } else {
      tok = lexer_make_token(lex, TOKEN_NOT, "!");
    }
    lexer_advance(lex);
    break;
  case '&':
    if (lexer_peek(lex) == '&') {
      tok = lexer_make_token(lex, TOKEN_AND, "&&");
      lexer_advance(lex);
    } else if (lexer_peek(lex) == '=') {
      tok = lexer_make_token(lex, TOKEN_AMPERSAND_ASSIGN, "&=");
      lexer_advance(lex);
    } else {
      tok = lexer_make_token(lex, TOKEN_AMPERSAND, "&");
    }
    lexer_advance(lex);
    break;
  case '|':
    if (lexer_peek(lex) == '|') {
      tok = lexer_make_token(lex, TOKEN_OR, "||");
      lexer_advance(lex);
    } else if (lexer_peek(lex) == '=') {
      tok = lexer_make_token(lex, TOKEN_PIPE_ASSIGN, "|=");
      lexer_advance(lex);
    } else {
      tok = lexer_make_token(lex, TOKEN_PIPE, "|");
    }
    lexer_advance(lex);
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
    tok = lexer_collect_quoted(lex, TOKEN_CHAR_LITERAL, '\'');
    break;
  case '"':
    tok = lexer_collect_quoted(lex, TOKEN_STRING, '"');
    break;

  default:
    if (lex->current_char == 'L' && (lexer_peek(lex) == '"' || lexer_peek(lex) == '\'')) {
      lexer_advance(lex);
      tok = (lex->current_char == '"') ? lexer_collect_quoted(lex, TOKEN_WIDE_STRING, '"')
                                       : lexer_collect_quoted(lex, TOKEN_WIDE_CHAR, '\'');
    } else if (isalpha((unsigned char)lex->current_char) || lex->current_char == '_' ||
               ucn_at(lex->source + lex->position, NULL)) {
      tok = lexer_collect_identifier(lex);
    } else if (isdigit(lex->current_char)) {
      tok = lexer_collect_number(lex);
    } else {
      char text[2] = {lex->current_char, '\0'};
      char message[64];
      snprintf(message, sizeof(message), "unexpected character '%c' in source", text[0]);
      lexer_error(lex, start_line, start_column, message);
      tok = lexer_make_token(lex, TOKEN_UNKNOWN, text);
      lexer_advance(lex);
    }
    break;
  }
  tok.line = start_line + lex->line_offset;
  tok.column = start_column;
  tok.at_line_start = start_of_line;
  return tok;
}
