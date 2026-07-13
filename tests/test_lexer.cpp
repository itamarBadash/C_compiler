
#include <gtest/gtest.h>

extern "C" {
#include "lexer.h"
#include <stdlib.h>
}

// פונקציית עזר לבדיקת טוקנים שמונעת דליפות זיכרון ושכפול קוד
void expect_token(lexer* lex, token_type expected_type, const char* expected_value = nullptr) {
  token t = lexer_next_token(lex);
  EXPECT_EQ(t.type, expected_type) << "Mismatch in token type!";

  if (expected_value != nullptr) {
    ASSERT_NE(t.value, nullptr) << "Expected value '" << expected_value << "' but token value is NULL";
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
const char* source = "int main_123 return 0";
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
const char* source = "+ - * / ( ) { } ;";
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
const char* source = "= == ! != < <= > >= && ||";
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
const char* source =
    "// this is a line comment\n"
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
const char* source = "\"hello\" \"escaped \\\" quote\"";
lexer lex;
lexer_init(&lex, source);

expect_token(&lex, TOKEN_STRING, "hello");
expect_token(&lex, TOKEN_STRING, "escaped \\\" quote");
expect_token(&lex, TOKEN_EOF);
}

TEST(LexerTests, UnterminatedStringError) {
// מחרוזת שאין לה מרכאות סוגרות
const char* source = "\"forgot to close";
lexer lex;
lexer_init(&lex, source);

// מצפים שהלקסר יזהה את זה בתור שגיאה ולא יקרוס
expect_token(&lex, TOKEN_UNKNOWN, "Unterminated string");
}

// ==========================================
// 6. תווים לא חוקיים (Unknown Tokens)
// ==========================================
TEST(LexerTests, UnknownCharacters) {
// תווים כמו @ או # שלא קיימים בשפה שלנו כרגע
const char* source = "int @ test #";
lexer lex;
lexer_init(&lex, source);

expect_token(&lex, TOKEN_INT, "int");
expect_token(&lex, TOKEN_UNKNOWN, NULL); // מצפים להיכשל על ה-@
expect_token(&lex, TOKEN_IDENTIFIER, "test");
expect_token(&lex, TOKEN_UNKNOWN, NULL); // מצפים להיכשל על ה-#
expect_token(&lex, TOKEN_EOF);
}

// ==========================================
// 7. בדיקת מעקב שורות ועמודות (Line/Column Tracking)
// ==========================================
TEST(LexerTests, LineAndColumnTracking) {
const char* source =
    "int x = 1;\n"
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
const char* source = "++ -- += -= *= /= %=";
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
const char* source = ", : ? ~ % [ ] .";
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
const char* source = "a.b c->d";
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
const char* source = "& | ~";
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
const char* source =
    "int void char float double long short unsigned signed "
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
const char* source = "x ? 1 : 0";
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
const char* source = "- -- -= ->";
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
const char* source = "for (int i = 0; i < 10; i++) { arr[i] *= 2; }";
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
const char* source =
    "typedef struct {\n"
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
const char* source = "'a' 'Z' '0'";
lexer lex;
lexer_init(&lex, source);

expect_token(&lex, TOKEN_CHAR_LITERAL, "a");
expect_token(&lex, TOKEN_CHAR_LITERAL, "Z");
expect_token(&lex, TOKEN_CHAR_LITERAL, "0");
expect_token(&lex, TOKEN_EOF);
}

TEST(LexerTests, CharLiteralEscapeSequences) {
const char* source = "'\\n' '\\\\' '\\'' '\\t' '\\0'";
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
const char* source1 = "''";
lexer lex1;
lexer_init(&lex1, source1);
expect_token(&lex1, TOKEN_UNKNOWN, "Empty char literal");

// multi-char literal
const char* source2 = "'ab'";
lexer lex2;
lexer_init(&lex2, source2);
expect_token(&lex2, TOKEN_UNKNOWN, "Invalid char literal");

// unterminated literal
const char* source3 = "'a";
lexer lex3;
lexer_init(&lex3, source3);
expect_token(&lex3, TOKEN_UNKNOWN, "Unterminated char literal");
}

TEST(LexerTests, CharLiteralInContext) {
const char* source = "char c = 'x';";
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
const char* source = "int x; /* this comment never ends";
lexer lex;
lexer_init(&lex, source);

expect_token(&lex, TOKEN_INT, "int");
expect_token(&lex, TOKEN_IDENTIFIER, "x");
expect_token(&lex, TOKEN_SEMICOLON, ";");
expect_token(&lex, TOKEN_UNKNOWN, "Unterminated block comment");
}