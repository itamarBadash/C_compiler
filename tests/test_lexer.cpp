
#include "target_guard.h"
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

extern "C" {
#include "lexer.h"
#include <stdlib.h>
}

// פונקציית עזר לבדיקת טוקנים שמונעת דליפות זיכרון ושכפול קוד
void expect_token(lexer *lex, token_type expected_type, const char *expected_value = nullptr) {
  token t = lexer_next_token(lex);
  EXPECT_EQ(t.type, expected_type) << "Mismatch in token type!";

  if (expected_value != nullptr) {
    ASSERT_NE(t.value, nullptr) << "Expected value '" << expected_value
                                << "' but token value is NULL";
    EXPECT_STREQ(t.value, expected_value);
  }

  if (t.value != nullptr) {
    free(t.value);
  }
}

// ==========================================
// 1. בדיקת מילים שמורות, מזהים ומספרים
// ==========================================
TEST(LexerTests, KeywordsIdentifiersAndNumbers) {
  const char *source = "int main_123 return 0";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_INT, "int");
  expect_token(&lex, TOKEN_IDENTIFIER, "main_123");
  expect_token(&lex, TOKEN_RETURN, "return");
  expect_token(&lex, TOKEN_NUMBER, "0");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 2. בדיקת סימני פיסוק ואופרטורים בודדים
// ==========================================
TEST(LexerTests, SingleCharacterTokens) {
  const char *source = "+ - * / ( ) { } ;";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_PLUS, "+");
  expect_token(&lex, TOKEN_MINUS, "-");
  expect_token(&lex, TOKEN_STAR, "*");
  expect_token(&lex, TOKEN_SLASH, "/");
  expect_token(&lex, TOKEN_LPAREN, "(");
  expect_token(&lex, TOKEN_RPAREN, ")");
  expect_token(&lex, TOKEN_LBRACE, "{");
  expect_token(&lex, TOKEN_RBRACE, "}");
  expect_token(&lex, TOKEN_SEMICOLON, ";");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 3. בדיקת אופרטורים לוגיים והשוואות (Lookahead)
// ==========================================
TEST(LexerTests, LookaheadOperators) {
  // בודק שמזהים נכון מתי זה = ומתי זה ==, מתי < ומתי <= וכו'
  const char *source = "= == ! != < <= > >= && ||";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_ASSIGN, "=");
  expect_token(&lex, TOKEN_EQ, "==");
  expect_token(&lex, TOKEN_NOT, "!");
  expect_token(&lex, TOKEN_NEQ, "!=");
  expect_token(&lex, TOKEN_LT, "<");
  expect_token(&lex, TOKEN_LTE, "<=");
  expect_token(&lex, TOKEN_GT, ">");
  expect_token(&lex, TOKEN_GTE, ">=");
  expect_token(&lex, TOKEN_AND, "&&");
  expect_token(&lex, TOKEN_OR, "||");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 4. בדיקת הערות (Comments) ודילוגים
// ==========================================
TEST(LexerTests, CommentsAndWhitespace) {
  const char *source = "// this is a line comment\n"
                       "int x;\n"
                       "/* multi \n"
                       "   line \n"
                       "   comment */ y = 5;";

  lexer lex;
  lexer_init(&lex, source);

  // הלקסר אמור לדלג על ההערה הראשונה ולהגיע ישירות ל-int
  expect_token(&lex, TOKEN_INT, "int");
  expect_token(&lex, TOKEN_IDENTIFIER, "x");
  expect_token(&lex, TOKEN_SEMICOLON, ";");

  // הלקסר אמור לדלג על בלוק ההערה הענק ולהגיע ישירות ל-y
  expect_token(&lex, TOKEN_IDENTIFIER, "y");
  expect_token(&lex, TOKEN_ASSIGN, "=");
  expect_token(&lex, TOKEN_NUMBER, "5");
  expect_token(&lex, TOKEN_SEMICOLON, ";");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 5. בדיקת מחרוזות (כולל תווי מילוט ושגיאות)
// ==========================================
TEST(LexerTests, StringLiterals) {
  // מחרוזת רגילה ומחרוזת עם לוכסן הפוך
  const char *source = "\"hello\" \"escaped \\\" quote\"";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_STRING, "hello");
  expect_token(&lex, TOKEN_STRING, "escaped \\\" quote");
  expect_token(&lex, TOKEN_EOF);
}

TEST(LexerTests, UnterminatedStringError) {
  // מחרוזת שאין לה מרכאות סוגרות
  const char *source = "\"forgot to close";
  lexer lex;
  lexer_init(&lex, source);

  // מצפים שהלקסר יזהה את זה בתור שגיאה ולא יקרוס
  expect_token(&lex, TOKEN_UNKNOWN);
  EXPECT_EQ(lex.error_count, 1)
      << "the diagnostic belongs in the error channel, not in the token value";
}

// ==========================================
// 6. תווים לא חוקיים (Unknown Tokens)
// ==========================================
TEST(LexerTests, UnknownCharacters) {
  // תווים כמו @ או # שלא קיימים בשפה שלנו כרגע
  const char *source = "int @ test #";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_INT, "int");
  expect_token(&lex, TOKEN_UNKNOWN, NULL); // מצפים להיכשל על ה-@
  expect_token(&lex, TOKEN_IDENTIFIER, "test");
  expect_token(&lex, TOKEN_HASH, "#");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 7. בדיקת מעקב שורות ועמודות (Line/Column Tracking)
// ==========================================
TEST(LexerTests, LineAndColumnTracking) {
  const char *source = "int x = 1;\n"
                       "  return x;";

  lexer lex;
  lexer_init(&lex, source);

  token t;

  // בודקים את ה-return בשורה השנייה
  expect_token(&lex, TOKEN_INT, "int");
  expect_token(&lex, TOKEN_IDENTIFIER, "x");
  expect_token(&lex, TOKEN_ASSIGN, "=");
  expect_token(&lex, TOKEN_NUMBER, "1");
  expect_token(&lex, TOKEN_SEMICOLON, ";");

  t = lexer_next_token(&lex);
  EXPECT_EQ(t.type, TOKEN_RETURN);
  EXPECT_EQ(t.line, 2) << "Return should be on line 2";
  EXPECT_EQ(t.column, 3) << "Return should start at column 3";
  free(t.value);
}

// ==========================================
// 8. אופרטורים מורכבים (Compound/Increment/Decrement)
// ==========================================
TEST(LexerTests, CompoundOperators) {
  const char *source = "++ -- += -= *= /= %=";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_PLUS_PLUS, "++");
  expect_token(&lex, TOKEN_MINUS_MINUS, "--");
  expect_token(&lex, TOKEN_PLUS_ASSIGN, "+=");
  expect_token(&lex, TOKEN_MINUS_ASSIGN, "-=");
  expect_token(&lex, TOKEN_STAR_ASSIGN, "*=");
  expect_token(&lex, TOKEN_SLASH_ASSIGN, "/=");
  expect_token(&lex, TOKEN_PERCENT_ASSIGN, "%=");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 9. סימני פיסוק חדשים
// ==========================================
TEST(LexerTests, NewPunctuation) {
  const char *source = ", : ? ~ % [ ] .";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_COMMA, ",");
  expect_token(&lex, TOKEN_COLON, ":");
  expect_token(&lex, TOKEN_QUESTION, "?");
  expect_token(&lex, TOKEN_TILDE, "~");
  expect_token(&lex, TOKEN_PERCENT, "%");
  expect_token(&lex, TOKEN_LBRACKET, "[");
  expect_token(&lex, TOKEN_RBRACKET, "]");
  expect_token(&lex, TOKEN_DOT, ".");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 10. גישה לשדות (Member Access: dot ו-arrow)
// ==========================================
TEST(LexerTests, MemberAccess) {
  const char *source = "a.b c->d";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_IDENTIFIER, "a");
  expect_token(&lex, TOKEN_DOT, ".");
  expect_token(&lex, TOKEN_IDENTIFIER, "b");
  expect_token(&lex, TOKEN_IDENTIFIER, "c");
  expect_token(&lex, TOKEN_ARROW, "->");
  expect_token(&lex, TOKEN_IDENTIFIER, "d");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 11. אופרטורים ביטוויים (Bitwise Operators)
// ==========================================
TEST(LexerTests, BitwiseOperators) {
  const char *source = "& | ~";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_AMPERSAND, "&");
  expect_token(&lex, TOKEN_PIPE, "|");
  expect_token(&lex, TOKEN_TILDE, "~");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 12. כל מילות המפתח (All Keywords)
// ==========================================
TEST(LexerTests, AllKeywords) {
  const char *source = "int void char float double long short unsigned signed "
                       "return if else while for do switch case default break continue "
                       "const static extern typedef struct enum union sizeof";
  lexer lex;
  lexer_init(&lex, source);

  // Types
  expect_token(&lex, TOKEN_INT, "int");
  expect_token(&lex, TOKEN_VOID, "void");
  expect_token(&lex, TOKEN_CHAR, "char");
  expect_token(&lex, TOKEN_FLOAT, "float");
  expect_token(&lex, TOKEN_DOUBLE, "double");
  expect_token(&lex, TOKEN_LONG, "long");
  expect_token(&lex, TOKEN_SHORT, "short");
  expect_token(&lex, TOKEN_UNSIGNED, "unsigned");
  expect_token(&lex, TOKEN_SIGNED, "signed");

  // Control flow
  expect_token(&lex, TOKEN_RETURN, "return");
  expect_token(&lex, TOKEN_IF, "if");
  expect_token(&lex, TOKEN_ELSE, "else");
  expect_token(&lex, TOKEN_WHILE, "while");
  expect_token(&lex, TOKEN_FOR, "for");
  expect_token(&lex, TOKEN_DO, "do");
  expect_token(&lex, TOKEN_SWITCH, "switch");
  expect_token(&lex, TOKEN_CASE, "case");
  expect_token(&lex, TOKEN_DEFAULT, "default");
  expect_token(&lex, TOKEN_BREAK, "break");
  expect_token(&lex, TOKEN_CONTINUE, "continue");

  // Storage/qualifiers
  expect_token(&lex, TOKEN_CONST, "const");
  expect_token(&lex, TOKEN_STATIC, "static");
  expect_token(&lex, TOKEN_EXTERN, "extern");
  expect_token(&lex, TOKEN_TYPEDEF, "typedef");

  // Composite types
  expect_token(&lex, TOKEN_STRUCT, "struct");
  expect_token(&lex, TOKEN_ENUM, "enum");
  expect_token(&lex, TOKEN_UNION, "union");

  // Operators
  expect_token(&lex, TOKEN_SIZEOF, "sizeof");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 13. אופרטור טרנרי (Ternary Operator)
// ==========================================
TEST(LexerTests, TernaryOperator) {
  const char *source = "x ? 1 : 0";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_IDENTIFIER, "x");
  expect_token(&lex, TOKEN_QUESTION, "?");
  expect_token(&lex, TOKEN_NUMBER, "1");
  expect_token(&lex, TOKEN_COLON, ":");
  expect_token(&lex, TOKEN_NUMBER, "0");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 14. הבחנה בין אופרטורים דומים (Operator Disambiguation)
// ==========================================
TEST(LexerTests, OperatorDisambiguation) {
  // מוודאים שהלקסר מבדיל נכון בין - ל-- ל-= ל->
  const char *source = "- -- -= ->";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_MINUS, "-");
  expect_token(&lex, TOKEN_MINUS_MINUS, "--");
  expect_token(&lex, TOKEN_MINUS_ASSIGN, "-=");
  expect_token(&lex, TOKEN_ARROW, "->");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 15. ביטוי מורכב - לולאת for עם מערך (Complex Expression)
// ==========================================
TEST(LexerTests, ComplexForLoopExpression) {
  const char *source = "for (int i = 0; i < 10; i++) { arr[i] *= 2; }";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_FOR, "for");
  expect_token(&lex, TOKEN_LPAREN, "(");
  expect_token(&lex, TOKEN_INT, "int");
  expect_token(&lex, TOKEN_IDENTIFIER, "i");
  expect_token(&lex, TOKEN_ASSIGN, "=");
  expect_token(&lex, TOKEN_NUMBER, "0");
  expect_token(&lex, TOKEN_SEMICOLON, ";");
  expect_token(&lex, TOKEN_IDENTIFIER, "i");
  expect_token(&lex, TOKEN_LT, "<");
  expect_token(&lex, TOKEN_NUMBER, "10");
  expect_token(&lex, TOKEN_SEMICOLON, ";");
  expect_token(&lex, TOKEN_IDENTIFIER, "i");
  expect_token(&lex, TOKEN_PLUS_PLUS, "++");
  expect_token(&lex, TOKEN_RPAREN, ")");
  expect_token(&lex, TOKEN_LBRACE, "{");
  expect_token(&lex, TOKEN_IDENTIFIER, "arr");
  expect_token(&lex, TOKEN_LBRACKET, "[");
  expect_token(&lex, TOKEN_IDENTIFIER, "i");
  expect_token(&lex, TOKEN_RBRACKET, "]");
  expect_token(&lex, TOKEN_STAR_ASSIGN, "*=");
  expect_token(&lex, TOKEN_NUMBER, "2");
  expect_token(&lex, TOKEN_SEMICOLON, ";");
  expect_token(&lex, TOKEN_RBRACE, "}");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 16. הצהרת struct עם typedef (Struct Declaration)
// ==========================================
TEST(LexerTests, StructDeclaration) {
  const char *source = "typedef struct {\n"
                       "    int x, y;\n"
                       "} Point;";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_TYPEDEF, "typedef");
  expect_token(&lex, TOKEN_STRUCT, "struct");
  expect_token(&lex, TOKEN_LBRACE, "{");
  expect_token(&lex, TOKEN_INT, "int");
  expect_token(&lex, TOKEN_IDENTIFIER, "x");
  expect_token(&lex, TOKEN_COMMA, ",");
  expect_token(&lex, TOKEN_IDENTIFIER, "y");
  expect_token(&lex, TOKEN_SEMICOLON, ";");
  expect_token(&lex, TOKEN_RBRACE, "}");
  expect_token(&lex, TOKEN_IDENTIFIER, "Point");
  expect_token(&lex, TOKEN_SEMICOLON, ";");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 17. תווים בודדים - Character Literals
// ==========================================
TEST(LexerTests, CharLiteralSimple) {
  const char *source = "'a' 'Z' '0'";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_CHAR_LITERAL, "a");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "Z");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "0");
  expect_token(&lex, TOKEN_EOF);
}

TEST(LexerTests, CharLiteralEscapeSequences) {
  const char *source = "'\\n' '\\\\' '\\'' '\\t' '\\0'";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\n");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\\\");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\'");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\t");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\0");
  expect_token(&lex, TOKEN_EOF);
}

TEST(LexerTests, CharLiteralErrors) {
  // empty literal
  const char *source1 = "''";
  lexer lex1;
  lexer_init(&lex1, source1);
  expect_token(&lex1, TOKEN_UNKNOWN);
  EXPECT_EQ(lex1.error_count, 1);

  // literal cut off by the end of its line
  const char *source2 = "'a\nx";
  lexer lex2;
  lexer_init(&lex2, source2);
  expect_token(&lex2, TOKEN_UNKNOWN);
  expect_token(&lex2, TOKEN_IDENTIFIER, "x");
  EXPECT_EQ(lex2.error_count, 1);

  // unterminated literal
  const char *source3 = "'a";
  lexer lex3;
  lexer_init(&lex3, source3);
  expect_token(&lex3, TOKEN_UNKNOWN);
  EXPECT_EQ(lex3.error_count, 1);
}

TEST(LexerTests, CharLiteralInContext) {
  const char *source = "char c = 'x';";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_CHAR, "char");
  expect_token(&lex, TOKEN_IDENTIFIER, "c");
  expect_token(&lex, TOKEN_ASSIGN, "=");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "x");
  expect_token(&lex, TOKEN_SEMICOLON, ";");
  expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 18. הערות - Comments
// ==========================================
TEST(LexerTests, UnterminatedBlockComment) {
  const char *source = "int x; /* this comment never ends";
  lexer lex;
  lexer_init(&lex, source);

  expect_token(&lex, TOKEN_INT, "int");
  expect_token(&lex, TOKEN_IDENTIFIER, "x");
  expect_token(&lex, TOKEN_SEMICOLON, ";");
  expect_token(&lex, TOKEN_UNKNOWN);
  EXPECT_EQ(lex.error_count, 1);
}

TEST(LexerTests, WideStringHasItsOwnTokenType) {
  lexer lex;
  lexer_init(&lex, "L\"hi\"");
  expect_token(&lex, TOKEN_WIDE_STRING, "hi");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 0);
}

TEST(LexerTests, WideCharHasItsOwnTokenType) {
  lexer lex;
  lexer_init(&lex, "L'x'");
  expect_token(&lex, TOKEN_WIDE_CHAR, "x");
  expect_token(&lex, TOKEN_EOF);
}

TEST(LexerTests, NarrowAndWideLiteralsAreDistinguishable) {
  lexer lex;
  lexer_init(&lex, "\"a\" L\"a\" 'b' L'b'");
  expect_token(&lex, TOKEN_STRING, "a");
  expect_token(&lex, TOKEN_WIDE_STRING, "a");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "b");
  expect_token(&lex, TOKEN_WIDE_CHAR, "b");
  expect_token(&lex, TOKEN_EOF);
}

TEST(LexerTests, LFollowedBySomethingElseIsAnIdentifier) {
  lexer lex;
  lexer_init(&lex, "L Lx");
  expect_token(&lex, TOKEN_IDENTIFIER, "L");
  expect_token(&lex, TOKEN_IDENTIFIER, "Lx");
  expect_token(&lex, TOKEN_EOF);
}

TEST(LexerTests, U8PrefixIsAnIdentifierInC99) {
  lexer lex;
  lexer_init(&lex, "u8\"x\"");
  expect_token(&lex, TOKEN_IDENTIFIER, "u8");
  expect_token(&lex, TOKEN_STRING, "x");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 0)
      << "C99 has no u8 prefix; u8\"x\" really is an identifier then a string";
}

TEST(LexerTests, EscapesArePreservedRawForThePreprocessor) {
  lexer lex;
  lexer_init(&lex, "\"a\\nb\"");
  expect_token(&lex, TOKEN_STRING, "a\\nb");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 0)
      << "phase 5 decoding happens in the parser; the lexer must keep the "
         "spelling so # stringize can reproduce it";
}

TEST(LexerTests, ErrorCountStartsAtZero) {
  lexer lex;
  lexer_init(&lex, "int x;");
  EXPECT_EQ(lex.error_count, 0);
  expect_token(&lex, TOKEN_INT, "int");
  EXPECT_EQ(lex.error_count, 0);
}

TEST(LexerTests, ErrorsAccumulateRatherThanLatch) {
  lexer lex;
  lexer_init(&lex, "'' '' ''");
  expect_token(&lex, TOKEN_UNKNOWN);
  expect_token(&lex, TOKEN_UNKNOWN);
  expect_token(&lex, TOKEN_UNKNOWN);
  EXPECT_EQ(lex.error_count, 3) << "error_count is a count, not a flag";
}

TEST(LexerTests, UnknownCharacterTokenCarriesTheCharacter) {
  lexer lex;
  lexer_init(&lex, "@");
  expect_token(&lex, TOKEN_UNKNOWN, "@");
  EXPECT_EQ(lex.error_count, 0) << "a stray character is a preprocessing token (C99 6.4p1); it is "
                                   "diagnosed only if it becomes a token";
}

TEST(LexerTests, StrayCharactersArePreprocessingTokensNotErrors) {
  lexer lex;
  lexer_init(&lex, "@ \\ $ `");
  expect_token(&lex, TOKEN_UNKNOWN, "@");
  expect_token(&lex, TOKEN_UNKNOWN, "\\");
  expect_token(&lex, TOKEN_UNKNOWN, "$");
  expect_token(&lex, TOKEN_UNKNOWN, "`");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 0);
}

TEST(LexerTests, OctalEscapesInCharLiteralsLex) {
  lexer lex;
  lexer_init(&lex, "'\\101' '\\0' '\\7'");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\101");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\0");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\7");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 0)
      << "an escape can be several characters wide; advancing exactly one "
         "past the backslash rejects every octal and hex escape";
}

TEST(LexerTests, HexEscapesInCharLiteralsLex) {
  lexer lex;
  lexer_init(&lex, "'\\x41' '\\xff' L'\\xAB'");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\x41");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\xff");
  expect_token(&lex, TOKEN_WIDE_CHAR, "\\xAB");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 0);
}

TEST(LexerTests, OctalEscapesStopAtThreeDigits) {
  lexer lex;
  lexer_init(&lex, "'\\1234'");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\1234");
  EXPECT_EQ(lex.error_count, 0);
  char bytes[16];
  int units = 0;
  EXPECT_EQ(decode_literal("\\1234", 0, bytes, &units), nullptr);
  ASSERT_EQ(units, 2) << "three octal digits max, so the trailing 4 is a character of its own";
  EXPECT_EQ(bytes[0], 0123);
  EXPECT_EQ(bytes[1], '4');
}

TEST(LexerTests, SingleCharacterEscapesStillLex) {
  lexer lex;
  lexer_init(&lex, "'\\n' '\\\\' '\\''");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\n");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\\\");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\'");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 0);
}

TEST(LexerTests, MultiCharacterConstantsLex) {
  lexer lex;
  lexer_init(&lex, "'ab' L'ab' '\\n\\0' 'abcde' '\\''");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "ab");
  expect_token(&lex, TOKEN_WIDE_CHAR, "ab");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\n\\0");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "abcde");
  expect_token(&lex, TOKEN_CHAR_LITERAL, "\\'");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 0);
}

TEST(LexerTests, EmptyStringsLex) {
  lexer lex;
  lexer_init(&lex, "\"\" L\"\"");
  expect_token(&lex, TOKEN_STRING, "");
  expect_token(&lex, TOKEN_WIDE_STRING, "");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 0) << "only a character constant must not be empty";
}

TEST(LexerTests, LiteralsEndAtTheEndOfTheirLine) {
  lexer lex;
  lexer_init(&lex, "\"abc\nint x;\n'ab\ny \"a\\\nb");
  expect_token(&lex, TOKEN_UNKNOWN);
  expect_token(&lex, TOKEN_INT, "int");
  expect_token(&lex, TOKEN_IDENTIFIER, "x");
  expect_token(&lex, TOKEN_SEMICOLON, ";");
  expect_token(&lex, TOKEN_UNKNOWN);
  expect_token(&lex, TOKEN_IDENTIFIER, "y");
  expect_token(&lex, TOKEN_UNKNOWN);
  expect_token(&lex, TOKEN_IDENTIFIER, "b");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 3) << "a backslash does not carry a literal onto the next line";
}

TEST(LexerTests, UniversalCharacterNamesExtendIdentifiers) {
  lexer lex;
  lexer_init(&lex, "\\u00e9t\\U000000E9 caf\\u00E9 x\\U0001F600 y\\uFFFF L\\u00e9 \\u0041 a\\u12");
  expect_token(&lex, TOKEN_IDENTIFIER, "\\u00e9t\\u00e9");
  expect_token(&lex, TOKEN_IDENTIFIER, "caf\\u00e9");
  expect_token(&lex, TOKEN_IDENTIFIER, "x\\U0001f600");
  expect_token(&lex, TOKEN_IDENTIFIER, "y\\uffff");
  expect_token(&lex, TOKEN_IDENTIFIER, "L\\u00e9");
  EXPECT_EQ(lex.error_count, 0);
  expect_token(&lex, TOKEN_IDENTIFIER, "\\u0041");
  EXPECT_EQ(lex.error_count, 1) << "a UCN below 00A0 names a basic character";
  expect_token(&lex, TOKEN_IDENTIFIER, "a");
  expect_token(&lex, TOKEN_UNKNOWN, "\\");
  expect_token(&lex, TOKEN_IDENTIFIER, "u12");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 1) << "the stray backslash of an incomplete UCN is a preprocessing "
                                   "token, not a lexer error";
}

TEST(LexerTests, LexingContinuesAfterAnError) {
  lexer lex;
  lexer_init(&lex, "int '' x;");
  expect_token(&lex, TOKEN_INT, "int");
  expect_token(&lex, TOKEN_UNKNOWN);
  expect_token(&lex, TOKEN_IDENTIFIER, "x");
  expect_token(&lex, TOKEN_SEMICOLON, ";");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 1);
}

TEST(LexerTests, EveryC99KeywordIsItsOwnTokenAndNearMissesAreIdentifiers) {
  const struct {
    const char *text;
    token_type type;
  } keywords[] = {
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
  for (const auto &keyword : keywords) {
    SCOPED_TRACE(keyword.text);
    lexer lex;
    lexer_init(&lex, keyword.text);
    expect_token(&lex, keyword.type, keyword.text);
    expect_token(&lex, TOKEN_EOF);
    EXPECT_TRUE(token_is_keyword(keyword.type));
  }

  for (const char *text : {"A", "_", "_Boo", "_Bool_", "__Bool", "Auto", "autos", "whil", "whilee",
                           "zzz", "Int", "sizeof1"}) {
    SCOPED_TRACE(text);
    lexer lex;
    lexer_init(&lex, text);
    expect_token(&lex, TOKEN_IDENTIFIER, text);
    expect_token(&lex, TOKEN_EOF);
  }

  for (token_type type :
       {TOKEN_EOF, TOKEN_IDENTIFIER, TOKEN_NUMBER, TOKEN_ELLIPSIS, TOKEN_PLUS, TOKEN_UNKNOWN})
    EXPECT_FALSE(token_is_keyword(type)) << type;
}

struct Decoded {
  std::vector<unsigned> units;
  std::string error;
};

static Decoded decode(const char *spelling, int wide) {
  int size = wide ? target_current()->wchar_size : 1;
  std::vector<char> bytes(4 * strlen(spelling) + 4, 'Z');
  int count = 0;
  const char *error = decode_literal(spelling, wide, bytes.data(), &count);
  Decoded out;
  for (int i = 0; i < count; i++) {
    unsigned unit = 0;
    for (int b = size - 1; b >= 0; b--)
      unit = unit << 8 | (unsigned char)bytes[i * size + b];
    out.units.push_back(unit);
  }
  out.error = error ? error : "";
  return out;
}

struct DecodeCase {
  const char *spelling;
  int wide;
  std::vector<unsigned> units;
  const char *error;
};

static const DecodeCase decode_cases[] = {
    {"ab", 0, {'a', 'b'}, ""},
    {"\\n\\t\\r\\a\\b\\f\\v\\\\\\'\\\"\\?", 0, {10, 9, 13, 7, 8, 12, 11, 92, 39, 34, 63}, ""},
    {"\\0\\101\\1234\\377", 0, {0, 65, 83, '4', 0xff}, ""},
    {"\\\t", 0, {'\t'}, "unknown escape sequence in literal"},
    {"\xc3\xa9", 0, {0xc3, 0xa9}, ""},
    {"\\777", 0, {0xff}, "octal escape sequence out of range"},
    {"\\x41\\x0000041\\xff", 0, {0x41, 0x41, 0xff}, ""},
    {"\\x100", 0, {0x00}, "hex escape sequence out of range"},
    {"\\xZ", 0, {'Z'}, "\\x used with no following hex digits"},
    {"\xff", 0, {0xff}, ""},
    {"\\u00e9\\U0001F600", 0, {0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80}, ""},
    {"\\u0024\\u0040\\u0060\\u00a0", 0, {0x24, 0x40, 0x60, 0xc2, 0xa0}, ""},
    {"\\u07ff\\u0800\\uffff", 0, {0xdf, 0xbf, 0xe0, 0xa0, 0x80, 0xef, 0xbf, 0xbf}, ""},
    {"\\U00010000\\U0010FFFF", 0, {0xf0, 0x90, 0x80, 0x80, 0xf4, 0x8f, 0xbf, 0xbf}, ""},
    {"\\ud7ff\\ue000", 0, {0xed, 0x9f, 0xbf, 0xee, 0x80, 0x80}, ""},
    {"\\u0041", 0, {}, "invalid universal character name"},
    {"\\u009f", 0, {}, "invalid universal character name"},
    {"\\ud800", 0, {}, "invalid universal character name"},
    {"\\udfff", 0, {}, "invalid universal character name"},
    {"\\U00110000", 0, {}, "invalid universal character name"},
    {"\\u12x", 0, {'1', '2', 'x'}, "incomplete universal character name"},
    {"\\U0000e9", 0, {'0', '0', '0', '0', 'e', '9'}, "incomplete universal character name"},
    {"\\q", 0, {'q'}, "unknown escape sequence in literal"},
    {"ab\\", 0, {'a', 'b'}, "stray backslash at end of literal"},
    {"\\q\\x", 0, {'q'}, "unknown escape sequence in literal"},
    {"ab", 1, {'a', 'b'}, ""},
    {"\\xffff\\x8000\\777", 1, {0xffff, 0x8000, 0x1ff}, ""},
    {"\\x10000", 1, {0x0000}, "hex escape sequence out of range"},
    {"\\U0001F600\\u00e9\\uffff", 1, {0xd83d, 0xde00, 0xe9, 0xffff}, ""},
    {"\\U00010000\\U0010FFFF", 1, {0xd800, 0xdc00, 0xdbff, 0xdfff}, ""},
    {"\\u0041", 1, {}, "invalid universal character name"},
    {"\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80", 1, {0xe9, 0x20ac, 0xd83d, 0xde00}, ""},
    {"\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80\xef\xbf\xbf", 1, {0x7f, 0x80, 0x7ff, 0x800, 0xffff}, ""},
    {"\xf0\x90\x80\x80\xf4\x8f\xbf\xbf", 1, {0xd800, 0xdc00, 0xdbff, 0xdfff}, ""},
    {"\xc0\x80", 1, {0xc0, 0x80}, "invalid UTF-8 in a wide literal"},
    {"\xc1\xbf", 1, {0xc1, 0xbf}, "invalid UTF-8 in a wide literal"},
    {"\xe0\x9f\xbf", 1, {0xe0, 0x9f, 0xbf}, "invalid UTF-8 in a wide literal"},
    {"\xf0\x8f\xbf\xbf", 1, {0xf0, 0x8f, 0xbf, 0xbf}, "invalid UTF-8 in a wide literal"},
    {"\xed\xa0\x80", 1, {0xed, 0xa0, 0x80}, "invalid UTF-8 in a wide literal"},
    {"\xed\x9f\xbf", 1, {0xd7ff}, ""},
    {"\xf4\x90\x80\x80", 1, {0xf4, 0x90, 0x80, 0x80}, "invalid UTF-8 in a wide literal"},
    {"\xf5\x80\x80\x80", 1, {0xf5, 0x80, 0x80, 0x80}, "invalid UTF-8 in a wide literal"},
    {"\xe2\x82", 1, {0xe2, 0x82}, "invalid UTF-8 in a wide literal"},
    {"\x80", 1, {0x80}, "invalid UTF-8 in a wide literal"},
    {"\xe2(\x82\xac", 1, {0xe2, '(', 0x82, 0xac}, "invalid UTF-8 in a wide literal"},
};

TEST(DecodeLiteralTest, EscapesUniversalCharacterNamesAndUtf8DecodeToCodeUnits) {
  for (const DecodeCase &test : decode_cases) {
    SCOPED_TRACE(std::string(test.wide ? "wide " : "narrow ") + test.spelling);
    Decoded got = decode(test.spelling, test.wide);
    EXPECT_EQ(got.units, test.units);
    EXPECT_EQ(got.error, test.error);
  }
}

TEST(DecodeLiteralTest, WideLiteralsOnLinuxAreUtf32) {
  TargetGuard guard(TARGET_LINUX_X64);
  const DecodeCase cases[] = {
      {"ab", 1, {'a', 'b'}, ""},
      {"\\U0001F600\\u00e9", 1, {0x1F600, 0xe9}, ""},
      {"\xf0\x9f\x98\x80", 1, {0x1F600}, ""},
      {"\\U00010000\\U0010FFFF", 1, {0x10000, 0x10FFFF}, ""},
      {"\\xffffffff\\x10000\\777", 1, {0xffffffff, 0x10000, 0x1ff}, ""},
      {"\\x100000000", 1, {0}, "hex escape sequence out of range"},
      {"\\x1ffffffff", 1, {0xffffffff}, "hex escape sequence out of range"},
      {"\\u00e9\\x41", 0, {0xc3, 0xa9, 0x41}, ""},
      {"\\x100", 0, {0x00}, "hex escape sequence out of range"},
  };
  for (const DecodeCase &test : cases) {
    SCOPED_TRACE(std::string(test.wide ? "wide " : "narrow ") + test.spelling);
    Decoded got = decode(test.spelling, test.wide);
    EXPECT_EQ(got.units, test.units);
    EXPECT_EQ(got.error, test.error);
  }
}

TEST(DecodeLiteralTest, DecodingAppendsAfterTheUnitsAlreadyThere) {
  char bytes[16];
  int units = 0;
  EXPECT_EQ(decode_literal("a", 1, bytes, &units), nullptr);
  EXPECT_EQ(decode_literal("\\u20ac", 1, bytes, &units), nullptr);
  ASSERT_EQ(units, 2);
  EXPECT_EQ(std::vector<char>(bytes, bytes + 4), std::vector<char>({'a', 0, '\xac', 0x20}));
}

TEST(DecodeLiteralTest, CharacterConstantValuesFollowGcc) {
  const struct {
    std::vector<unsigned char> bytes;
    int wide;
    long long value;
  } cases[] = {
      {{}, 0, 0},
      {{'a'}, 0, 97},
      {{0xff}, 0, -1},
      {{0x7f}, 0, 127},
      {{'a', 'b'}, 0, 24930},
      {{0x80, 0x00}, 0, 32768},
      {{0xff, 0xff, 0xff, 0xff}, 0, -1},
      {{0x7f, 0xff, 0xff, 0xff}, 0, 2147483647},
      {{'a', 'b', 'c', 'd', 'e'}, 0, 1650680933},
      {{}, 1, 0},
      {{'a', 0}, 1, 97},
      {{'a', 0, 'b', 0}, 1, 98},
      {{0xff, 0xff}, 1, 65535},
      {{0x00, 0x80}, 1, 32768},
      {{0x3d, 0xd8, 0x00, 0xde}, 1, 0xde00},
  };
  for (const auto &test : cases) {
    int units = (int)test.bytes.size() / (test.wide ? 2 : 1);
    EXPECT_EQ(char_constant_value((const char *)test.bytes.data(), units, test.wide), test.value)
        << "wide " << test.wide << ", " << units << " units";
  }
}

TEST(DecodeLiteralTest, WideCharacterConstantsOnLinuxAreInts) {
  TargetGuard guard(TARGET_LINUX_X64);
  const struct {
    std::vector<unsigned char> bytes;
    long long value;
  } cases[] = {
      {{}, 0},
      {{'a', 0, 0, 0}, 97},
      {{'a', 0, 0, 0, 'b', 0, 0, 0}, 98},
      {{0x00, 0xf6, 0x01, 0x00}, 0x1F600},
      {{0xff, 0xff, 0xff, 0x7f}, 2147483647},
      {{0xff, 0xff, 0xff, 0xff}, -1},
      {{0x00, 0x00, 0x00, 0x80}, -2147483647LL - 1},
  };
  for (const auto &test : cases) {
    int units = (int)test.bytes.size() / 4;
    EXPECT_EQ(char_constant_value((const char *)test.bytes.data(), units, 1), test.value)
        << units << " units";
  }
  EXPECT_EQ(char_constant_value("\xff", 1, 0), -1) << "narrow constants do not change";
}

TEST(LexerTests, NumbersArePreprocessingNumbers) {
  lexer lex;
  lexer_init(&lex, "1.2.3 0x1e+1 1e+5 1_x 1..2 .5e-3f 0x1p-3 12abc 1... 1+2 0xe-1 1p+2 a.1 "
                   "1\\u00e9 7-");
  for (const char *number :
       {"1.2.3", "0x1e+1", "1e+5", "1_x", "1..2", ".5e-3f", "0x1p-3", "12abc", "1..."})
    expect_token(&lex, TOKEN_NUMBER, number);
  expect_token(&lex, TOKEN_NUMBER, "1");
  expect_token(&lex, TOKEN_PLUS, "+");
  expect_token(&lex, TOKEN_NUMBER, "2");
  expect_token(&lex, TOKEN_NUMBER, "0xe-1");
  expect_token(&lex, TOKEN_NUMBER, "1p+2");
  expect_token(&lex, TOKEN_IDENTIFIER, "a");
  expect_token(&lex, TOKEN_NUMBER, ".1");
  expect_token(&lex, TOKEN_NUMBER, "1\\u00e9");
  expect_token(&lex, TOKEN_NUMBER, "7");
  expect_token(&lex, TOKEN_MINUS, "-");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 0) << "whether a pp-number is a valid constant is decided later";
}

TEST(ClassifyNumberTest, IntegerAndFloatingConstantsFollowC99) {
  for (const char *number :
       {"0",   "7",   "0777", "123", "1289", "0x0",  "0XfF",    "1u",   "1U",   "1l",
        "1L",  "1ll", "1LL",  "1ul", "1uL",  "1Ul",  "1UL",     "1ull", "1uLL", "1ULL",
        "1lu", "1lU", "1Lu",  "1LU", "1llu", "1LLU", "0x1fULL", "0777u"}) {
    const char *error = "unset";
    EXPECT_EQ(classify_number(number, &error), NUMBER_INTEGER) << number;
    EXPECT_EQ(error, nullptr) << number;
  }
  for (const char *number :
       {"1.",     ".5",     "1.5",    "1e5",     "1E+5",     "1e-5",   "1.5e10",
        ".5e-3f", "1.0F",   "1.0l",   "1.0L",    "09.5",     "09e1",   "0e0",
        "0x1p0",  "0x1P-3", "0x.8p1", "0x1.p+2", "0x1.8p3L", "0xAp1f", "00.5"}) {
    const char *error = "unset";
    EXPECT_EQ(classify_number(number, &error), NUMBER_FLOATING) << number;
    EXPECT_EQ(error, nullptr) << number;
  }
  const struct {
    const char *number;
    const char *error;
  } invalid[] = {
      {"08", "invalid digit in an octal constant"},
      {"0779", "invalid digit in an octal constant"},
      {"0x", "hexadecimal constant has no digits"},
      {"0Xu", "hexadecimal constant has no digits"},
      {"1lL", "invalid suffix on an integer constant"},
      {"1Ll", "invalid suffix on an integer constant"},
      {"1uu", "invalid suffix on an integer constant"},
      {"1lll", "invalid suffix on an integer constant"},
      {"1lul", "invalid suffix on an integer constant"},
      {"1f", "invalid suffix on an integer constant"},
      {"12abc", "invalid suffix on an integer constant"},
      {"0x1g", "invalid suffix on an integer constant"},
      {"0x1e+1", "invalid suffix on an integer constant"},
      {"1_000", "invalid suffix on an integer constant"},
      {"1p+2", "invalid suffix on an integer constant"},
      {"1e", "exponent has no digits"},
      {"1e+", "exponent has no digits"},
      {"1.5E-", "exponent has no digits"},
      {"0x1p", "exponent has no digits"},
      {"0x1.8", "hexadecimal floating constant requires an exponent"},
      {"0x.8", "hexadecimal floating constant requires an exponent"},
      {"0x.p1", "floating constant has no digits"},
      {"0xp1", "floating constant has no digits"},
      {"1.2.3", "invalid suffix on a floating constant"},
      {"1..2", "invalid suffix on a floating constant"},
      {"1.0q", "invalid suffix on a floating constant"},
      {"1.0fl", "invalid suffix on a floating constant"},
      {"1e5lf", "invalid suffix on a floating constant"},
  };
  for (const auto &test : invalid) {
    const char *error = nullptr;
    EXPECT_EQ(classify_number(test.number, &error), NUMBER_INVALID) << test.number;
    EXPECT_STREQ(error, test.error) << test.number;
    EXPECT_EQ(classify_number(test.number, nullptr), NUMBER_INVALID) << test.number;
  }
}

TEST(ClassifyNumberTest, IntegerSuffixesReportSignednessAndLength) {
  const struct {
    const char *suffix;
    int valid;
    int is_unsigned;
    int long_count;
  } cases[] = {
      {"", 1, 0, 0},    {"u", 1, 1, 0},   {"L", 1, 0, 1},  {"ll", 1, 0, 2}, {"ULL", 1, 1, 2},
      {"llu", 1, 1, 2}, {"lU", 1, 1, 1},  {"Lu", 1, 1, 1}, {"lL", 0, 0, 1}, {"uu", 0, 1, 0},
      {"lll", 0, 0, 2}, {"ulu", 0, 1, 1}, {"x", 0, 0, 0},
  };
  for (const auto &test : cases) {
    int is_unsigned = -1;
    int long_count = -1;
    EXPECT_EQ(integer_suffix(test.suffix, &is_unsigned, &long_count), test.valid) << test.suffix;
    EXPECT_EQ(is_unsigned, test.is_unsigned) << test.suffix;
    EXPECT_EQ(long_count, test.long_count) << test.suffix;
  }
}

TEST(LexerTests, TokensAndErrorsCarryTheLexersFileAndLineOffset) {
  lexer plain;
  lexer_init(&plain, "x");
  token t = lexer_next_token(&plain);
  EXPECT_EQ(t.file, nullptr);
  EXPECT_EQ(t.line, 1);
  free(t.value);

  lexer lex;
  lexer_init(&lex, "x\n  ''");
  lex.file = "a.c";
  lex.line_offset = 9;
  t = lexer_next_token(&lex);
  EXPECT_STREQ(t.file, "a.c");
  EXPECT_EQ(t.line, 10);
  free(t.value);
  testing::internal::CaptureStderr();
  t = lexer_next_token(&lex);
  std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_STREQ(t.file, "a.c");
  EXPECT_EQ(t.line, 11);
  EXPECT_EQ(t.column, 3);
  free(t.value);
  EXPECT_EQ(diagnostics, "a.c:11:3: error: empty character constant\n");
}

TEST(LexerTests, DigraphsAreTheirPunctuatorsWithTheirOwnSpelling) {
  lexer lex;
  lexer_init(&lex, "<: :> <% %> %: %:%: %:% <= %= << : %");
  expect_token(&lex, TOKEN_LBRACKET, "<:");
  expect_token(&lex, TOKEN_RBRACKET, ":>");
  expect_token(&lex, TOKEN_LBRACE, "<%");
  expect_token(&lex, TOKEN_RBRACE, "%>");
  expect_token(&lex, TOKEN_HASH, "%:");
  expect_token(&lex, TOKEN_HASH_HASH, "%:%:");
  expect_token(&lex, TOKEN_HASH, "%:");
  expect_token(&lex, TOKEN_PERCENT, "%");
  expect_token(&lex, TOKEN_LTE, "<=");
  expect_token(&lex, TOKEN_PERCENT_ASSIGN, "%=");
  expect_token(&lex, TOKEN_LSHIFT, "<<");
  expect_token(&lex, TOKEN_COLON, ":");
  expect_token(&lex, TOKEN_PERCENT, "%");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 0);
}

TEST(LexerTests, FormFeedAndVerticalTabAreWhitespace) {
  lexer lex;
  lexer_init(&lex, "int\fa\v;");
  expect_token(&lex, TOKEN_INT, "int");
  expect_token(&lex, TOKEN_IDENTIFIER, "a");
  expect_token(&lex, TOKEN_SEMICOLON, ";");
  expect_token(&lex, TOKEN_EOF);
  EXPECT_EQ(lex.error_count, 0);
}

TEST(LexerTests, VerticalWhitespaceIsCountedOnlyInsideALine) {
  lexer lex;
  lexer_init(&lex, "a\f b\v\nc\n\fd \f\n e");
  EXPECT_EQ(lex.vertical_spaces, 0);
  expect_token(&lex, TOKEN_IDENTIFIER, "a");
  expect_token(&lex, TOKEN_IDENTIFIER, "b");
  EXPECT_EQ(lex.vertical_spaces, 1) << "the form feed between 'a' and 'b' is inside the line";
  expect_token(&lex, TOKEN_IDENTIFIER, "c");
  EXPECT_EQ(lex.vertical_spaces, 2) << "the vertical tab before the newline is still inside it";
  expect_token(&lex, TOKEN_IDENTIFIER, "d");
  EXPECT_EQ(lex.vertical_spaces, 2) << "a form feed at the start of a line begins the line";
  expect_token(&lex, TOKEN_IDENTIFIER, "e");
  EXPECT_EQ(lex.vertical_spaces, 3);
}

TEST(LexerTests, EveryTokenRecordsWhetherWhitespacePrecededIt) {
  lexer lex;
  lexer_init(&lex, "a b(c)/**/d\ne");
  const struct {
    const char *value;
    int leading_space;
  } expected[] = {{"a", 0}, {"b", 1}, {"(", 0}, {"c", 0},
                  {")", 0}, {"d", 1}, {"e", 1}, {nullptr, 0}};
  for (int i = 0; expected[i].value != nullptr; i++) {
    token t = lexer_next_token(&lex);
    SCOPED_TRACE(expected[i].value);
    EXPECT_STREQ(t.value, expected[i].value);
    EXPECT_EQ(t.leading_space, expected[i].leading_space)
        << "a comment and a newline are whitespace, just like a space";
    free(t.value);
  }
}
