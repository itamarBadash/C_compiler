/* ANSI-C code produced by gperf version 3.3 */
/* Command-line: gperf -t -N check_keyword src/keywords.gperf  */
/* Computed positions: -k'2-3' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gperf@gnu.org>."
#endif

#line 1 "src/keywords.gperf"

#include "token.h"
#include <string.h>
#line 6 "src/keywords.gperf"
struct keyword_map {
    const char *name;
    token_type type;
};

#define TOTAL_KEYWORDS 28
#define MIN_WORD_LENGTH 2
#define MAX_WORD_LENGTH 8
#define MIN_HASH_VALUE 2
#define MAX_HASH_VALUE 41
/* maximum key range = 40, duplicates = 0 */

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
hash (register const char *str, register size_t len)
{
  static unsigned char asso_values[] =
    {
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42,  0, 42, 42,
      42, 15,  5, 30, 10,  5, 42, 42, 20, 42,
       0,  0,  5, 42, 10, 15,  0, 20, 42, 25,
      25,  0,  0, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
      42, 42, 42, 42, 42, 42
    };
  register unsigned int hval = len;

  switch (hval)
    {
      default:
        hval += asso_values[(unsigned char)str[2]];
#if (defined __cplusplus && (__cplusplus >= 201703L || (__cplusplus >= 201103L && defined __clang__ && __clang_major__ + (__clang_minor__ >= 9) > 3))) || (defined __STDC_VERSION__ && __STDC_VERSION__ >= 202000L && ((defined __GNUC__ && __GNUC__ >= 10) || (defined __clang__ && __clang_major__ >= 9)))
      [[fallthrough]];
#elif (defined __GNUC__ && __GNUC__ >= 7) || (defined __clang__ && __clang_major__ >= 10)
      __attribute__ ((__fallthrough__));
#endif
      /*FALLTHROUGH*/
      case 2:
        hval += asso_values[(unsigned char)str[1]];
        break;
    }
  return hval;
}

struct keyword_map *
check_keyword (register const char *str, register size_t len)
{
#if (defined __GNUC__ && __GNUC__ + (__GNUC_MINOR__ >= 6) > 4) || (defined __clang__ && __clang_major__ >= 3)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
  static struct keyword_map wordlist[] =
    {
      {""}, {""},
#line 18 "src/keywords.gperf"
      {"do", TOKEN_DO},
#line 26 "src/keywords.gperf"
      {"int", TOKEN_INT},
#line 27 "src/keywords.gperf"
      {"long", TOKEN_LONG},
#line 15 "src/keywords.gperf"
      {"const", TOKEN_CONST},
#line 32 "src/keywords.gperf"
      {"static", TOKEN_STATIC},
#line 25 "src/keywords.gperf"
      {"if", TOKEN_IF},
#line 16 "src/keywords.gperf"
      {"continue", TOKEN_CONTINUE},
#line 38 "src/keywords.gperf"
      {"void", TOKEN_VOID},
#line 36 "src/keywords.gperf"
      {"union", TOKEN_UNION},
#line 31 "src/keywords.gperf"
      {"sizeof", TOKEN_SIZEOF},
#line 35 "src/keywords.gperf"
      {"typedef", TOKEN_TYPEDEF},
#line 24 "src/keywords.gperf"
      {"for", TOKEN_FOR},
#line 14 "src/keywords.gperf"
      {"char", TOKEN_CHAR},
#line 29 "src/keywords.gperf"
      {"short", TOKEN_SHORT},
#line 33 "src/keywords.gperf"
      {"struct", TOKEN_STRUCT},
      {""}, {""},
#line 13 "src/keywords.gperf"
      {"case", TOKEN_CASE},
#line 39 "src/keywords.gperf"
      {"while", TOKEN_WHILE},
#line 28 "src/keywords.gperf"
      {"return", TOKEN_RETURN},
      {""},
#line 37 "src/keywords.gperf"
      {"unsigned", TOKEN_UNSIGNED},
#line 21 "src/keywords.gperf"
      {"enum", TOKEN_ENUM},
#line 23 "src/keywords.gperf"
      {"float", TOKEN_FLOAT},
#line 19 "src/keywords.gperf"
      {"double", TOKEN_DOUBLE},
#line 17 "src/keywords.gperf"
      {"default", TOKEN_DEFAULT},
      {""}, {""},
#line 12 "src/keywords.gperf"
      {"break", TOKEN_BREAK},
#line 22 "src/keywords.gperf"
      {"extern", TOKEN_EXTERN},
      {""}, {""}, {""}, {""},
#line 34 "src/keywords.gperf"
      {"switch", TOKEN_SWITCH},
      {""}, {""},
#line 20 "src/keywords.gperf"
      {"else", TOKEN_ELSE},
      {""},
#line 30 "src/keywords.gperf"
      {"signed", TOKEN_SIGNED}
    };
#if (defined __GNUC__ && __GNUC__ + (__GNUC_MINOR__ >= 6) > 4) || (defined __clang__ && __clang_major__ >= 3)
#pragma GCC diagnostic pop
#endif

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register unsigned int key = hash (str, len);

      if (key <= MAX_HASH_VALUE)
        {
          register const char *s = wordlist[key].name;

          if (*str == *s && !strcmp (str + 1, s + 1))
            return &wordlist[key];
        }
    }
  return (struct keyword_map *) 0;
}
