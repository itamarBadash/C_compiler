#include <algorithm>
#include <cstdio>
#include <cstring>
#include <direct.h>
#include <fstream>
#include <gtest/gtest.h>
#include <io.h>
#include <sstream>
#include <string>
#include <vector>

class StderrCapture {
public:
  StderrCapture() {
    fflush(stderr);
    saved_ = _dup(_fileno(stderr));
    file_ = fopen("pp_stderr_capture.txt", "w+");
    if (file_ != nullptr) {
      _dup2(_fileno(file_), _fileno(stderr));
    }
  }

  std::string finish() {
    fflush(stderr);
    if (saved_ != -1) {
      _dup2(saved_, _fileno(stderr));
      _close(saved_);
      saved_ = -1;
    }
    if (file_ != nullptr) {
      fclose(file_);
      file_ = nullptr;
    }
    std::ifstream in("pp_stderr_capture.txt");
    std::stringstream all;
    all << in.rdbuf();
    in.close();
    remove("pp_stderr_capture.txt");
    return all.str();
  }

private:
  int saved_ = -1;
  FILE *file_ = nullptr;
};

extern "C" {
#include "lexer.h"
#include "preprocessor.h"
#include "token_buf.h"
#include <stdlib.h>
}

class PreprocessorTest : public ::testing::Test {
protected:
  token_buf tb;
  bool initialised = false;

  int rc = 0;

  void run(const char *source) {
    rc = pp_run(&tb, source);
    initialised = true;
  }

  void lex_all(const char *source) {
    char *spliced = pp_splice_lines(source);
    token_buf_init(&tb);
    lexer lex;
    lexer_init(&lex, spliced);
    token t;
    do {
      t = lexer_next_token(&lex);
      token_buf_push(&tb, t);
    } while (t.type != TOKEN_EOF);
    free(spliced);
    initialised = true;
  }

  void TearDown() override {
    if (initialised) {
      token_buf_free(&tb);
    }
  }

  std::vector<token_type> types() {
    std::vector<token_type> out;
    for (int i = 0; i < tb.count; i++) {
      out.push_back(tb.tokens[i].type);
    }
    return out;
  }

  std::vector<int> line_starts() {
    std::vector<int> out;
    for (int i = 0; i < tb.count; i++) {
      if (tb.tokens[i].type == TOKEN_EOF)
        continue;
      out.push_back(tb.tokens[i].at_line_start);
    }
    return out;
  }

  std::vector<std::string> spellings() {
    std::vector<std::string> out;
    for (int i = 0; i < tb.count; i++) {
      if (tb.tokens[i].type == TOKEN_EOF)
        continue;
      out.push_back(tb.tokens[i].value ? tb.tokens[i].value : "<null>");
    }
    return out;
  }
};

TEST_F(PreprocessorTest, LexesASimpleDeclaration) {
  run("int x = 1;");

  std::vector<token_type> expected = {TOKEN_INT,    TOKEN_IDENTIFIER, TOKEN_ASSIGN,
                                      TOKEN_NUMBER, TOKEN_SEMICOLON,  TOKEN_EOF};
  EXPECT_EQ(types(), expected);
}

TEST_F(PreprocessorTest, PreservesSpellings) {
  run("int counter = 42;");

  std::vector<std::string> expected = {"int", "counter", "=", "42", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, BufferEndsWithExactlyOneEof) {
  run("int x;");

  ASSERT_GT(tb.count, 0);
  EXPECT_EQ(tb.tokens[tb.count - 1].type, TOKEN_EOF);
  for (int i = 0; i < tb.count - 1; i++) {
    EXPECT_NE(tb.tokens[i].type, TOKEN_EOF) << "extra EOF at index " << i;
  }
}

TEST_F(PreprocessorTest, EmptySourceStillProducesEof) {
  run("");

  ASSERT_EQ(tb.count, 1);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_EOF);
}

TEST_F(PreprocessorTest, NextWalksTheBufferInOrder) {
  run("a b c");

  EXPECT_STREQ(token_buf_next(&tb).value, "a");
  EXPECT_STREQ(token_buf_next(&tb).value, "b");
  EXPECT_STREQ(token_buf_next(&tb).value, "c");
  EXPECT_EQ(token_buf_next(&tb).type, TOKEN_EOF);
}

TEST_F(PreprocessorTest, EofIsSticky) {
  run("int x;");

  while (token_buf_next(&tb).type != TOKEN_EOF) {
  }
  for (int i = 0; i < 100; i++) {
    token t = token_buf_next(&tb);
    EXPECT_EQ(t.type, TOKEN_EOF);
    EXPECT_EQ(t.value, nullptr);
  }
}

TEST_F(PreprocessorTest, ReadCursorDoesNotRunPastCount) {
  run("int x;");

  for (int i = 0; i < 50; i++) {
    token_buf_next(&tb);
  }
  EXPECT_LE(tb.pos, tb.count);
}

TEST_F(PreprocessorTest, EofAtEndCarriesALocation) {
  run("int x;\nint y;\n");

  token t;
  do {
    t = token_buf_next(&tb);
  } while (t.type != TOKEN_EOF);

  EXPECT_GT(t.line, 0);
  EXPECT_GT(t.column, 0);
}

TEST_F(PreprocessorTest, GrowsPastTheInitialCapacity) {
  std::string source;
  for (int i = 0; i < 5000; i++) {
    source += "int a";
    source += std::to_string(i);
    source += " = ";
    source += std::to_string(i);
    source += ";\n";
  }
  run(source.c_str());

  EXPECT_EQ(tb.count, 5000 * 5 + 1);
  EXPECT_GE(tb.capacity, tb.count);
  EXPECT_EQ(tb.tokens[tb.count - 1].type, TOKEN_EOF);
  EXPECT_STREQ(tb.tokens[0].value, "int");
  EXPECT_STREQ(tb.tokens[1].value, "a0");
  EXPECT_STREQ(tb.tokens[tb.count - 2].value, ";");
}

TEST_F(PreprocessorTest, OwnsStringValuedTokens) {
  run("const char *msg = \"hello\"; int ident = 0x2A; char c = 'q';");

  bool saw_string = false, saw_number = false, saw_char = false;
  for (int i = 0; i < tb.count; i++) {
    if (tb.tokens[i].type == TOKEN_STRING)
      saw_string = true;
    if (tb.tokens[i].type == TOKEN_NUMBER)
      saw_number = true;
    if (tb.tokens[i].type == TOKEN_CHAR_LITERAL)
      saw_char = true;
  }
  EXPECT_TRUE(saw_string);
  EXPECT_TRUE(saw_number);
  EXPECT_TRUE(saw_char);
}

TEST_F(PreprocessorTest, TokensSurviveTheSourceBuffer) {
  char *source = strdup("int reachable = 7;");
  pp_run(&tb, source);
  initialised = true;

  free(source);

  EXPECT_STREQ(tb.tokens[0].value, "int");
  EXPECT_STREQ(tb.tokens[1].value, "reachable");
  EXPECT_STREQ(tb.tokens[3].value, "7");
}

TEST_F(PreprocessorTest, FreeResetsTheBuffer) {
  run("int x = 1;");
  token_buf_free(&tb);

  EXPECT_EQ(tb.tokens, nullptr);
  EXPECT_EQ(tb.count, 0);
  EXPECT_EQ(tb.capacity, 0);
  EXPECT_EQ(tb.pos, 0);

  EXPECT_EQ(token_buf_next(&tb).type, TOKEN_EOF);
}

TEST_F(PreprocessorTest, FreeIsIdempotent) {
  run("int x = 1;");
  token_buf_free(&tb);
  token_buf_free(&tb);
  SUCCEED();
}

TEST_F(PreprocessorTest, ManualPushTakesOwnership) {
  token_buf local;
  token_buf_init(&local);

  token a = {TOKEN_IDENTIFIER, strdup("alpha"), 1, 1};
  token b = {TOKEN_NUMBER, strdup("7"), 1, 7};
  token e = {TOKEN_EOF, NULL, 1, 8};

  token_buf_push(&local, a);
  token_buf_push(&local, b);
  token_buf_push(&local, e);

  ASSERT_EQ(local.count, 3);
  EXPECT_STREQ(local.tokens[0].value, "alpha");
  EXPECT_STREQ(local.tokens[1].value, "7");
  EXPECT_EQ(local.tokens[2].value, nullptr);

  token_buf_free(&local);
  EXPECT_EQ(local.count, 0);
}

TEST_F(PreprocessorTest, InitLeavesAnEmptyReadableBuffer) {
  token_buf local;
  token_buf_init(&local);

  EXPECT_EQ(local.tokens, nullptr);
  EXPECT_EQ(local.count, 0);
  EXPECT_EQ(local.capacity, 0);
  EXPECT_EQ(local.pos, 0);
  EXPECT_EQ(token_buf_next(&local).type, TOKEN_EOF);

  token_buf_free(&local);
}

TEST_F(PreprocessorTest, HandlesNullArgumentsWithoutCrashing) {
  token_buf_init(nullptr);
  token_buf_free(nullptr);
  token_buf_push(nullptr, {TOKEN_EOF, NULL, 0, 0});
  EXPECT_EQ(token_buf_next(nullptr).type, TOKEN_EOF);

  token_buf local;
  pp_run(&local, nullptr);
  EXPECT_EQ(local.count, 0);
  EXPECT_EQ(token_buf_next(&local).type, TOKEN_EOF);
  token_buf_free(&local);
}

TEST_F(PreprocessorTest, DefineDirectiveIsConsumedAndTheMacroExpands) {
  run("#define MAX 100\nint a = MAX;");

  std::vector<std::string> expected = {"int", "a", "=", "100", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, PushTakesOwnershipRatherThanCopying) {
  token_buf local;
  token_buf_init(&local);

  char *owned = strdup("alpha");
  token a = {TOKEN_IDENTIFIER, owned, 1, 1};
  token_buf_push(&local, a);

  ASSERT_EQ(local.count, 1);
  EXPECT_EQ(local.tokens[0].value, owned);

  token_buf_free(&local);
}

extern "C" {
int tb_free_calls = 0;
int tb_realloc_calls = 0;
int tb_realloc_allow = -1;
void __real_free(void *p);
void *__real_realloc(void *p, size_t n);
void __wrap_free(void *p) {
  tb_free_calls++;
  __real_free(p);
}
void *__wrap_realloc(void *p, size_t n) {
  tb_realloc_calls++;
  if (tb_realloc_allow >= 0) {
    if (tb_realloc_allow == 0)
      return NULL;
    tb_realloc_allow--;
  }
  return __real_realloc(p, n);
}
}

TEST_F(PreprocessorTest, FreeReleasesEveryTokenValueAndTheArray) {
  run("int alpha = 1; const char *s = \"txt\"; char c = 'z';");

  int expected = tb.count + 1;
  tb_free_calls = 0;
  token_buf_free(&tb);
  initialised = false;

  EXPECT_EQ(tb_free_calls, expected);
}

TEST_F(PreprocessorTest, GrowthDoublesRatherThanIncrementing) {
  std::string source;
  for (int i = 0; i < 5000; i++) {
    source += "int a" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
  }

  tb_realloc_calls = 0;
  run(source.c_str());

  EXPECT_GT(tb.count, 20000);
  EXPECT_LT(tb_realloc_calls, 20) << "growth looks linear, not geometric";
}

TEST_F(PreprocessorTest, FailedGrowthLeavesTheBufferConsistent) {
  token_buf local;
  token_buf_init(&local);

  token t = {TOKEN_IDENTIFIER, strdup("x"), 1, 1};
  tb_free_calls = 0;
  tb_realloc_allow = 0;
  token_buf_push(&local, t);
  tb_realloc_allow = -1;

  EXPECT_EQ(local.count, 0);
  EXPECT_EQ(local.capacity, 0);
  EXPECT_EQ(local.tokens, nullptr);
  EXPECT_EQ(tb_free_calls, 1) << "a rejected push must release the token it was handed";

  token_buf_free(&local);
}

TEST_F(PreprocessorTest, PpRunTerminatesWhenGrowthFails) {
  std::string source;
  for (int i = 0; i < 400; i++) {
    source += "int a" + std::to_string(i) + ";\n";
  }

  token_buf warmup;
  tb_realloc_calls = 0;
  pp_run(&warmup, "");
  int setup_reallocs = tb_realloc_calls;
  token_buf_free(&warmup);

  tb_realloc_allow = setup_reallocs + 1;
  pp_run(&tb, source.c_str());
  tb_realloc_allow = -1;
  initialised = true;

  EXPECT_GT(tb.count, 0);
  EXPECT_LE(tb.count, tb.capacity);
}

TEST_F(PreprocessorTest, HashLexesAsItsOwnToken) {
  lex_all("#define X 1");

  ASSERT_GT(tb.count, 1);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_HASH);
  EXPECT_STREQ(tb.tokens[0].value, "#");
  EXPECT_EQ(tb.tokens[1].type, TOKEN_IDENTIFIER);
  EXPECT_STREQ(tb.tokens[1].value, "define");
}

TEST_F(PreprocessorTest, HashHashLexesAsASingleToken) {
  run("a##b");

  std::vector<token_type> expected = {TOKEN_IDENTIFIER, TOKEN_HASH_HASH, TOKEN_IDENTIFIER,
                                      TOKEN_EOF};
  EXPECT_EQ(types(), expected);
  EXPECT_STREQ(tb.tokens[1].value, "##");
}

TEST_F(PreprocessorTest, SeparatedHashesStayTwoTokens) {
  lex_all("a # # b");

  std::vector<token_type> expected = {TOKEN_IDENTIFIER, TOKEN_HASH, TOKEN_HASH, TOKEN_IDENTIFIER,
                                      TOKEN_EOF};
  EXPECT_EQ(types(), expected);
}

TEST_F(PreprocessorTest, TripleHashIsPasteThenHash) {
  run("a###b");

  std::vector<token_type> expected = {TOKEN_IDENTIFIER, TOKEN_HASH_HASH, TOKEN_HASH,
                                      TOKEN_IDENTIFIER, TOKEN_EOF};
  EXPECT_EQ(types(), expected);
}

TEST_F(PreprocessorTest, NoTokenIsUnknownInADirective) {
  run("#define CAT(a,b) a##b");

  for (int i = 0; i < tb.count; i++) {
    EXPECT_NE(tb.tokens[i].type, TOKEN_UNKNOWN) << "token " << i << " is UNKNOWN";
  }
}

TEST_F(PreprocessorTest, FirstTokenOfFileStartsALine) {
  run("int x;");

  ASSERT_GT(tb.count, 0);
  EXPECT_EQ(tb.tokens[0].at_line_start, 1);
}

TEST_F(PreprocessorTest, OnlyTheFirstTokenOnALineIsFlagged) {
  run("int x = 1;");

  std::vector<int> expected = {1, 0, 0, 0, 0};
  EXPECT_EQ(line_starts(), expected);
}

TEST_F(PreprocessorTest, SpacesAndTabsDoNotStartALine) {
  run("int \t  x \t = \t 1 ;");

  std::vector<int> expected = {1, 0, 0, 0, 0};
  EXPECT_EQ(line_starts(), expected);
}

TEST_F(PreprocessorTest, EachNewLineFlagsItsFirstToken) {
  run("int a;\nint b;\nint c;");

  std::vector<int> expected = {1, 0, 0, 1, 0, 0, 1, 0, 0};
  EXPECT_EQ(line_starts(), expected);
}

TEST_F(PreprocessorTest, BlankLinesDoNotDoubleFlag) {
  run("int a;\n\n\n   \n int b;");

  std::vector<int> expected = {1, 0, 0, 1, 0, 0};
  EXPECT_EQ(line_starts(), expected);
}

TEST_F(PreprocessorTest, LeadingWhitespaceBeforeAHashStillCountsAsLineStart) {
  lex_all("int a;\n    #define X 1");

  ASSERT_EQ(tb.count, 8);
  EXPECT_EQ(tb.tokens[3].type, TOKEN_HASH);
  EXPECT_EQ(tb.tokens[3].at_line_start, 1);
  EXPECT_EQ(tb.tokens[4].at_line_start, 0);
}

TEST_F(PreprocessorTest, LineCommentEndsTheLine) {
  run("int a; // trailing\nint b;");

  std::vector<int> expected = {1, 0, 0, 1, 0, 0};
  EXPECT_EQ(line_starts(), expected);
}

TEST_F(PreprocessorTest, HashAfterALineCommentIsALineStart) {
  lex_all("// note\n#define X 1");

  ASSERT_GT(tb.count, 0);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_HASH);
  EXPECT_EQ(tb.tokens[0].at_line_start, 1);
}

TEST_F(PreprocessorTest, BlockCommentSpanningLinesLeavesAHashAtLineStart) {
  lex_all("/* c\n*/ #define X 1");

  ASSERT_GT(tb.count, 0);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_HASH);
  EXPECT_EQ(tb.tokens[0].at_line_start, 1);
}

TEST_F(PreprocessorTest, BlockCommentDoesNotEndADirectiveLine) {
  lex_all("#define Y /* c\n*/ 2\nY");

  ASSERT_EQ(tb.count, 6);
  EXPECT_EQ(tb.tokens[0].at_line_start, 1);
  EXPECT_EQ(tb.tokens[1].at_line_start, 0);
  EXPECT_EQ(tb.tokens[2].at_line_start, 0);
  EXPECT_STREQ(tb.tokens[3].value, "2");
  EXPECT_EQ(tb.tokens[3].at_line_start, 0);
  EXPECT_STREQ(tb.tokens[4].value, "Y");
  EXPECT_EQ(tb.tokens[4].at_line_start, 1);
}

TEST_F(PreprocessorTest, CodeBeforeABlockCommentKeepsTheHashOffLineStart) {
  run("int q; /* c\n*/ #define Z 3");

  int hash = -1;
  for (int i = 0; i < tb.count; i++) {
    if (tb.tokens[i].type == TOKEN_HASH)
      hash = i;
  }
  ASSERT_GE(hash, 0);
  EXPECT_EQ(tb.tokens[hash].at_line_start, 0);
}

TEST_F(PreprocessorTest, MidLineHashIsNotALineStart) {
  lex_all("#define STR(a) #a");

  ASSERT_EQ(tb.count, 9);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_HASH);
  EXPECT_EQ(tb.tokens[0].at_line_start, 1);
  EXPECT_EQ(tb.tokens[6].type, TOKEN_HASH);
  EXPECT_EQ(tb.tokens[6].at_line_start, 0);
}

TEST_F(PreprocessorTest, CarriageReturnLineEndingsFlagOnlyOnce) {
  run("int a;\r\nint b;");

  std::vector<int> expected = {1, 0, 0, 1, 0, 0};
  EXPECT_EQ(line_starts(), expected);
}

TEST_F(PreprocessorTest, LineStartSurvivesTheTokenBuffer) {
  lex_all("int a;\n#define X 1");

  token t;
  std::vector<int> seen;
  do {
    t = token_buf_next(&tb);
    if (t.type != TOKEN_EOF)
      seen.push_back(t.at_line_start);
  } while (t.type != TOKEN_EOF);

  std::vector<int> expected = {1, 0, 0, 1, 0, 0, 0};
  EXPECT_EQ(seen, expected);
}

class SpliceTest : public ::testing::Test {
protected:
  std::string splice(const char *source) {
    char *out = pp_splice_lines(source);
    std::string s = out ? out : "<null>";
    free(out);
    return s;
  }
};

TEST_F(SpliceTest, JoinsAnIdentifier) {
  EXPECT_EQ(splice("int ab\\\nc = 1;"), "int abc = 1;\n")
      << "the swallowed newline reappears once the logical line ends, so the "
         "lines after it keep their real numbers";
}

TEST_F(SpliceTest, JoinsAStringLiteral) {
  EXPECT_EQ(splice("\"he\\\nllo\""), "\"hello\"\n");
}

TEST_F(SpliceTest, RemovesChainedSplices) {
  EXPECT_EQ(splice("int a\\\n\\\nb;"), "int ab;\n\n") << "two continuations defer two newlines";
}

TEST_F(SpliceTest, HandlesCarriageReturnLineFeed) {
  EXPECT_EQ(splice("int x\\\r\ny;"), "int xy;\n")
      << "a CRLF continuation defers a plain newline; the lexer counts only \\n";
}

TEST_F(SpliceTest, PreservesTheTotalNewlineCount) {
  auto newlines = [](const std::string &s) { return std::count(s.begin(), s.end(), '\n'); };
  EXPECT_EQ(newlines(splice("a\nb\nc")), 2);
  EXPECT_EQ(newlines(splice("a\\\nb\nc")), 2);
  EXPECT_EQ(newlines(splice("a\\\n\\\nb\nc")), 3);
  EXPECT_EQ(newlines(splice("a\\\nb")), 1);
  EXPECT_EQ(newlines(splice("no continuations here")), 0);
}

TEST_F(SpliceTest, KeepsABareCarriageReturnThatIsNotASplice) {
  EXPECT_EQ(splice("int x\\\ry;"), "int x\\\ry;");
}

TEST_F(SpliceTest, KeepsATrailingBackslashAtEndOfInput) {
  EXPECT_EQ(splice("int x = 1;\\"), "int x = 1;\\");
}

TEST_F(SpliceTest, KeepsOrdinaryEscapeSequences) {
  EXPECT_EQ(splice("s = \"a\\nb\";"), "s = \"a\\nb\";");
  EXPECT_EQ(splice("c = '\\\\';"), "c = '\\\\';");
  EXPECT_EQ(splice("q = '\\'';"), "q = '\\'';");
}

TEST_F(SpliceTest, DoesNotSpliceBackslashSeparatedFromNewlineBySpaces) {
  EXPECT_EQ(splice("int x\\   \ny;"), "int x\\   \ny;");
}

TEST_F(SpliceTest, LeavesSourceWithoutSplicesUnchanged) {
  EXPECT_EQ(splice("int x = 1;\nint y = 2;\n"), "int x = 1;\nint y = 2;\n");
}

TEST_F(SpliceTest, HandlesEmptyInput) {
  EXPECT_EQ(splice(""), "");
}

TEST_F(SpliceTest, ASpliceOnItsOwnStillYieldsItsNewline) {
  EXPECT_EQ(splice("\\\n"), "\n") << "the newline is deferred to the end of input, not discarded";
}

TEST_F(SpliceTest, KeepsNewlinesThatAreNotPrecededByABackslash) {
  EXPECT_EQ(splice("a\nb\nc"), "a\nb\nc");
}

TEST_F(SpliceTest, ReturnsNullForNullInput) {
  EXPECT_EQ(pp_splice_lines(nullptr), nullptr);
}

TEST_F(SpliceTest, ReturnsAFreshBufferNotTheInput) {
  const char *src = "int x = 1;";
  char *out = pp_splice_lines(src);
  ASSERT_NE(out, nullptr);
  EXPECT_NE(out, src);
  EXPECT_STREQ(out, src);
  free(out);
}

TEST_F(SpliceTest, OutputIsNulTerminatedAfterShrinking) {
  char *out = pp_splice_lines("ab\\\ncd");
  ASSERT_NE(out, nullptr);
  EXPECT_EQ(strlen(out), 5u);
  EXPECT_EQ(out[4], '\n') << "the deferred newline lands after the logical line";
  EXPECT_EQ(out[5], '\0');
  free(out);
}

TEST_F(PreprocessorTest, SplicedIdentifierBecomesOneToken) {
  run("int ab\\\nc = 1;");

  std::vector<std::string> expected = {"int", "abc", "=", "1", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, SplicedStringLiteralBecomesOneToken) {
  run("const char *s = \"he\\\nllo\";");

  int idx = -1;
  for (int i = 0; i < tb.count; i++) {
    if (tb.tokens[i].type == TOKEN_STRING) {
      idx = i;
      break;
    }
  }
  ASSERT_GE(idx, 0) << "no string token produced";
  EXPECT_STREQ(tb.tokens[idx].value, "hello");
}

TEST_F(PreprocessorTest, ChainedSplicesJoinOneIdentifier) {
  run("int a\\\n\\\nb = 1;");

  std::vector<std::string> expected = {"int", "ab", "=", "1", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, CrlfSpliceJoinsAnIdentifier) {
  run("int x\\\r\ny = 1;");

  std::vector<std::string> expected = {"int", "xy", "=", "1", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, EscapesInsideStringTokensSurviveSplicing) {
  run("const char *s = \"a\\nb\";");

  int idx = -1;
  for (int i = 0; i < tb.count; i++) {
    if (tb.tokens[i].type == TOKEN_STRING) {
      idx = i;
      break;
    }
  }
  ASSERT_GE(idx, 0) << "no string token produced";
  EXPECT_STREQ(tb.tokens[idx].value, "a\\nb");
}

TEST_F(PreprocessorTest, MultiLineDefineIsOneLogicalLine) {
  lex_all("#define SWAP(a,b) do { \\\n"
          "    int t=(a); \\\n"
          "} while (0)\n"
          "int z;");

  int flagged = 0;
  int directive_end = -1;
  for (int i = 0; i < tb.count; i++) {
    if (tb.tokens[i].type == TOKEN_EOF)
      continue;
    if (tb.tokens[i].at_line_start) {
      flagged++;
      if (i > 0 && directive_end < 0)
        directive_end = i;
    }
  }
  EXPECT_EQ(flagged, 2) << "directive line plus the following line";
  ASSERT_GT(directive_end, 0);
  EXPECT_STREQ(tb.tokens[directive_end].value, "int");
  EXPECT_EQ(tb.tokens[0].type, TOKEN_HASH);
  EXPECT_EQ(tb.tokens[0].at_line_start, 1);
}

TEST_F(PreprocessorTest, SplicedLineCommentSwallowsTheFollowingLine) {
  run("// note \\\nint hidden = 1;\nint visible = 2;");

  std::vector<std::string> expected = {"int", "visible", "=", "2", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, TrailingBackslashStillReachesTheLexer) {
  run("int x = 1;\\");

  ASSERT_GT(tb.count, 5);
  EXPECT_EQ(tb.tokens[tb.count - 2].type, TOKEN_UNKNOWN);
  EXPECT_EQ(tb.tokens[tb.count - 1].type, TOKEN_EOF);
}

TEST_F(PreprocessorTest, SplicingDoesNotDisturbUnsplicedSources) {
  lex_all("int a;\n#define X 1\nint b;");

  std::vector<int> expected = {1, 0, 0, 1, 0, 0, 0, 1, 0, 0};
  EXPECT_EQ(line_starts(), expected);
}

TEST_F(PreprocessorTest, LineNumbersSurviveSplices) {
  run("int a\\\n\\\nb = 1;\nint c;");

  int c_index = -1;
  for (int i = 0; i < tb.count; i++) {
    if (tb.tokens[i].value && std::string(tb.tokens[i].value) == "c")
      c_index = i;
  }
  ASSERT_GE(c_index, 0);
  EXPECT_EQ(tb.tokens[c_index].line, 4)
      << "'c' is physically on line 4; deferring each swallowed newline to the "
         "end of its logical line keeps that true";
}

TEST_F(PreprocessorTest, TokensInsideASplicedLineShareItsStartingLine) {
  run("int ab\\\nc = 1;\nint d;");

  ASSERT_GE(tb.count, 6);
  EXPECT_EQ(tb.tokens[0].line, 1);
  EXPECT_EQ(tb.tokens[1].line, 1) << "the joined identifier belongs to line 1";
  EXPECT_STREQ(tb.tokens[1].value, "abc");
  EXPECT_EQ(tb.tokens[5].line, 3) << "and the next line is still line 3";
  EXPECT_STREQ(tb.tokens[5].value, "int");
}

TEST_F(PreprocessorTest, LineNumbersSurviveACrlfSplice) {
  run("int a\\\r\nb;\nint c;");

  int c_index = -1;
  for (int i = 0; i < tb.count; i++) {
    if (tb.tokens[i].value && std::string(tb.tokens[i].value) == "c")
      c_index = i;
  }
  ASSERT_GE(c_index, 0);
  EXPECT_EQ(tb.tokens[c_index].line, 3);
}

TEST_F(PreprocessorTest, PpRunFreesTheSplicedBuffer) {
  const char *src = "int alpha = 1; const char *s = \"txt\";";

  char *manual = pp_splice_lines(src);
  ASSERT_NE(manual, nullptr);
  token_buf local;
  token_buf_init(&local);
  lexer lex;
  lexer_init(&lex, manual);
  tb_free_calls = 0;
  token t;
  do {
    t = lexer_next_token(&lex);
    token_buf_push(&local, t);
  } while (t.type != TOKEN_EOF);
  int lexing_only = tb_free_calls;
  free(manual);
  token_buf_free(&local);

  token_buf baseline;
  tb_free_calls = 0;
  pp_run(&baseline, "");
  int empty_run = tb_free_calls;
  token_buf_free(&baseline);

  tb_free_calls = 0;
  run(src);
  EXPECT_EQ(tb_free_calls, lexing_only + empty_run)
      << "a run costs exactly the lexing of its source plus the fixed "
         "teardown of the source frame and the predefined macro table; "
         "anything less means pp_run leaked one of them";
}

TEST_F(PreprocessorTest, ObjectLikeMacroExpands) {
  run("#define MAX 100\nint a = MAX;");
  std::vector<std::string> expected = {"int", "a", "=", "100", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, MacroExpandsAtEveryUse) {
  run("#define N 7\nN N N");
  std::vector<std::string> expected = {"7", "7", "7"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, MultiTokenBodyExpands) {
  run("#define PAIR 1 + 2\nint x = PAIR;");
  std::vector<std::string> expected = {"int", "x", "=", "1", "+", "2", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, EmptyMacroBodyExpandsToNothing) {
  run("#define EMPTY\na EMPTY b");
  std::vector<std::string> expected = {"a", "b"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, TwoEmptyMacrosInARow) {
  run("#define E1\n#define E2\na E1 E2 b");
  std::vector<std::string> expected = {"a", "b"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ExpansionIsRescanned) {
  run("#define ONE 1\n#define TWO ONE ONE\nTWO");
  std::vector<std::string> expected = {"1", "1"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, SelfReferentialMacroTerminates) {
  run("#define X X\nX");
  std::vector<std::string> expected = {"X"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, MutuallyRecursiveMacrosTerminate) {
  run("#define P Q\n#define Q P\nP");
  std::vector<std::string> expected = {"P"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, SelfReferenceIsReEnabledAfterTheExpansionEnds) {
  run("#define X X\nX X");
  std::vector<std::string> expected = {"X", "X"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, MacroBodyIsStoredRawAndResolvedAtUse) {
  run("#define A 1\n#define B A\n#undef A\n#define A 2\nB");
  std::vector<std::string> expected = {"2"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, UndefStopsExpansion) {
  run("#define G 5\n#undef G\nG");
  std::vector<std::string> expected = {"G"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, UndefOfAnUndefinedNameIsHarmless) {
  run("#undef NEVER_DEFINED\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, RedefinitionReplacesTheBody) {
  run("#define V 1\n#define V 2\nV");
  std::vector<std::string> expected = {"2"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, MacroUsedBeforeItsDefinitionIsNotExpanded) {
  run("MAX\n#define MAX 100\nMAX");
  std::vector<std::string> expected = {"MAX", "100"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, MacroNameInsideAStringIsNotExpanded) {
  run("#define A B\nconst char *s = \"A\";");
  std::vector<std::string> got = spellings();
  ASSERT_FALSE(got.empty());
  EXPECT_EQ(got[got.size() - 2], "A");
}

TEST_F(PreprocessorTest, MacroNameInsideALongerIdentifierIsNotExpanded) {
  run("#define A B\nint xAy = 1;");
  std::vector<std::string> expected = {"int", "xAy", "=", "1", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, KeywordsAreNotTreatedAsMacros) {
  run("#define x 9\nint x;");
  std::vector<std::string> expected = {"int", "9", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ExpansionKeepsTheTokenType) {
  run("#define N 42\nN");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_NUMBER);
  EXPECT_STREQ(tb.tokens[0].value, "42");
}

TEST_F(PreprocessorTest, MacroExpandingToAKeywordKeepsItsKeywordType) {
  run("#define INTEGER int\nINTEGER x;");
  ASSERT_EQ(tb.count, 4);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_INT);
}

TEST_F(PreprocessorTest, MultiLineDefineBodyIsSpliced) {
  run("#define LIST 1, \\\n2, \\\n3\nLIST");
  std::vector<std::string> expected = {"1", ",", "2", ",", "3"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, DefineDoesNotLeakIntoTheNextLine) {
  run("#define A 1\nint b;");
  std::vector<std::string> expected = {"int", "b", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, NullDirectiveIsIgnored) {
  run("#\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, AMissingSystemHeaderDoesNotCorruptTheStream) {
  run("#include <stdio.h>\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1) << "no search path is configured, so the header is missing";
}

TEST_F(PreprocessorTest, PragmaTokensAreNotMacroExpanded) {
  run("#define A 1\n#pragma A A A\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0) << "an unrecognised pragma is ignored, not diagnosed";
}

TEST_F(PreprocessorTest, HashNotAtLineStartIsNotADirective) {
  run("int a; # 1 2 3");
  std::vector<std::string> expected = {"int", "a", ";", "#", "1", "2", "3"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, FunctionLikeDefineIsSkippedNotMangled) {
  run("#define SQ(x) x*x\nint y = 1;");
  std::vector<std::string> expected = {"int", "y", "=", "1", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, DefineWithSpaceBeforeParenIsObjectLike) {
  run("#define F (x)\nF");
  std::vector<std::string> expected = {"(", "x", ")"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, DefineWithNoNameIsIgnored) {
  run("#define\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, UndefWithNoNameIsIgnored) {
  run("#undef\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, DefineAtEndOfFileWithoutTrailingNewline) {
  run("#define A 1");
  ASSERT_EQ(tb.count, 1);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_EOF);
}

TEST_F(PreprocessorTest, MacroAtEndOfFileExpands) {
  run("#define A 1\nA");
  std::vector<std::string> expected = {"1"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, IndentedDirectiveIsRecognised) {
  run("    #define A 1\nA");
  std::vector<std::string> expected = {"1"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ExpandedTokensCarryALineStartFlagForTheFirstOne) {
  run("#define A 1 2\nA");
  ASSERT_EQ(tb.count, 3);
  EXPECT_STREQ(tb.tokens[0].value, "1");
  EXPECT_STREQ(tb.tokens[1].value, "2");
}

TEST_F(PreprocessorTest, ChainOfThreeMacros) {
  run("#define C 3\n#define B C\n#define A B\nA");
  std::vector<std::string> expected = {"3"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ManyExpansionsDoNotLeak) {
  std::string src = "#define A 1 2 3\n";
  for (int i = 0; i < 500; i++)
    src += "A ";
  run(src.c_str());
  EXPECT_EQ(tb.count, 500 * 3 + 1);
}

TEST_F(PreprocessorTest, PpDestroyFreesTheMacroTable) {
  std::string one = "#define M0 1\n";
  std::string many = one;
  for (int i = 1; i < 100; i++) {
    many += "#define M" + std::to_string(i) + " 1\n";
  }

  token_buf a;
  tb_free_calls = 0;
  pp_run(&a, one.c_str());
  int frees_one = tb_free_calls;
  token_buf_free(&a);

  token_buf b;
  tb_free_calls = 0;
  pp_run(&b, many.c_str());
  int frees_many = tb_free_calls;
  token_buf_free(&b);

  EXPECT_GT(frees_many - frees_one, 800)
      << "99 extra macros must each free a name, a body token, "
         "the body array and the struct; a leaked table measures ~594 not ~990";
}

TEST_F(PreprocessorTest, RedefinitionDoesNotLeaveTheOldMacroBehind) {
  run("#define V 1\n#define V 2\n#undef V\nV");
  std::vector<std::string> expected = {"V"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, SkippedFunctionLikeMacroIsNotDefinedAtAll) {
  run("#define SQ(x) x*x\nSQ");
  std::vector<std::string> expected = {"SQ"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, FunctionLikeMacroInvocationExpands) {
  run("#define SQ(x) x*x\nint y = SQ(3);");
  std::vector<std::string> expected = {"int", "y", "=", "3", "*", "3", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, MalformedParameterListIsIgnored) {
  run("#define F(1) x\nint y;");
  std::vector<std::string> expected = {"int", "y", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, MalformedParameterListDoesNotDefineTheMacro) {
  run("#define F(1) x\nF");
  std::vector<std::string> expected = {"F"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ParameterListWithMissingCommaIsIgnored) {
  run("#define F(a b) x\nint y;");
  std::vector<std::string> expected = {"int", "y", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, UnterminatedParameterListAtEndOfFile) {
  run("#define F(a");
  ASSERT_EQ(tb.count, 1);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_EOF);
}

TEST_F(PreprocessorTest, ParameterListRunningOffTheLineIsIgnored) {
  run("#define F(a,\nint y;");
  std::vector<std::string> expected = {"int", "y", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ZeroParameterMacroIsStoredAsFunctionLike) {
  run("#define Z() 42\nZ");
  std::vector<std::string> expected = {"Z"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, RedefiningAFunctionLikeMacroDoesNotLeakParams) {
  std::string one = "#define M0(a,b,c) a b c\n";
  std::string many = one;
  for (int i = 1; i < 100; i++) {
    many += "#define M" + std::to_string(i) + "(a,b,c) a b c\n";
  }

  token_buf a;
  tb_free_calls = 0;
  pp_run(&a, one.c_str());
  int frees_one = tb_free_calls;
  token_buf_free(&a);

  token_buf b;
  tb_free_calls = 0;
  pp_run(&b, many.c_str());
  int frees_many = tb_free_calls;
  token_buf_free(&b);

  EXPECT_GT(frees_many - frees_one, 2000)
      << "99 extra function-like macros must free three parameter names each "
         "on top of names, bodies and structs";
}

TEST_F(PreprocessorTest, FunctionLikeMacroExpands) {
  run("#define SQ(x) ((x)*(x))\nSQ(3)");
  std::vector<std::string> expected = {"(", "(", "3", ")", "*", "(", "3", ")", ")"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, FunctionLikeMacroWithTwoParameters) {
  run("#define ADD(a,b) ((a)+(b))\nADD(1,2)");
  std::vector<std::string> expected = {"(", "(", "1", ")", "+", "(", "2", ")", ")"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ParameterUsedTwiceIsSubstitutedTwice) {
  run("#define D(x) x x\nD(9)");
  std::vector<std::string> expected = {"9", "9"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ZeroParameterMacroInvocationExpands) {
  run("#define Z() 42\nZ()");
  std::vector<std::string> expected = {"42"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, CommaInsideNestedParensIsNotASeparator) {
  run("#define F(x) [x]\nF((1,2))");
  std::vector<std::string> expected = {"[", "(", "1", ",", "2", ")", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ArgumentContainingAnotherInvocationExpands) {
  run("#define G(a,b) a-b\n#define F(x) [x]\nF(G(1,2))");
  std::vector<std::string> expected = {"[", "1", "-", "2", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, EmptyArgumentsAreLegal) {
  run("#define F(a,b) [a][b]\nF(,x)");
  std::vector<std::string> expected = {"[", "]", "[", "x", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ArgumentThatIsAMacroExpandsByRescanning) {
  run("#define A 1\n#define F(x) [x]\nF(A)");
  std::vector<std::string> expected = {"[", "1", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, MacroNameWithoutParensIsNotAnInvocation) {
  run("#define F(x) [x]\nF");
  std::vector<std::string> expected = {"F"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, MacroNameFollowedByOtherTokensIsNotAnInvocation) {
  run("#define F(x) [x]\nF + 1");
  std::vector<std::string> expected = {"F", "+", "1"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, SpaceBeforeTheParenStillInvokes) {
  run("#define F(x) [x]\nF (3)");
  std::vector<std::string> expected = {"[", "3", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, NewlineBeforeTheParenStillInvokes) {
  run("#define F(x) [x]\nF\n(3)");
  std::vector<std::string> expected = {"[", "3", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, InvocationSpanningLinesWorks) {
  run("#define ADD(a,b) a+b\nADD(1,\n2)");
  std::vector<std::string> expected = {"1", "+", "2"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, TooFewArgumentsEmitsOnlyTheMacroName) {
  run("#define F(a,b) a b\nF(1)");
  std::vector<std::string> expected = {"F"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, TooManyArgumentsEmitsOnlyTheMacroName) {
  run("#define F(a) [a]\nF(1,2)");
  std::vector<std::string> expected = {"F"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, UnterminatedInvocationDoesNotHang) {
  run("#define F(x) [x]\nF(1");
  std::vector<std::string> expected = {"F"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, SelfRecursiveFunctionLikeMacroTerminates) {
  run("#define F(x) F(x)\nF(1)");
  std::vector<std::string> expected = {"F", "(", "1", ")"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, MutuallyRecursiveFunctionLikeMacrosTerminate) {
  run("#define F(x) G(x)\n#define G(x) F(x)\nF(1)");
  std::vector<std::string> expected = {"F", "(", "1", ")"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, FunctionLikeMacroWithEmptyBodyExpandsToNothing) {
  run("#define NOP(x)\na NOP(1) b");
  std::vector<std::string> expected = {"a", "b"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, UnusedArgumentIsDiscarded) {
  run("#define IG(x) 5\nIG(1+2)");
  std::vector<std::string> expected = {"5"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ObjectLikeMacroFollowedByParensIsNotAnInvocation) {
  run("#define F (x)\nF(3)");
  std::vector<std::string> expected = {"(", "x", ")", "(", "3", ")"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, StringArgumentKeepsItsTokenType) {
  run("#define S(x) [x]\nS(\"hi\")");
  ASSERT_EQ(tb.count, 4);
  EXPECT_EQ(tb.tokens[1].type, TOKEN_STRING);
  EXPECT_STREQ(tb.tokens[1].value, "hi");
}

TEST_F(PreprocessorTest, DifferentMacrosNestedInArgumentsExpand) {
  run("#define A(x) [x]\n#define B(x) {x}\nA(B(7))");
  std::vector<std::string> expected = {"[", "{", "7", "}", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, NestedSameMacroExpandsViaArgumentPreExpansion) {
  run("#define I(x) x\nI(I(7))");
  std::vector<std::string> expected = {"7"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ManyInvocationsDoNotLeak) {
  std::string src = "#define T(a,b,c) a b c\n";
  for (int i = 0; i < 300; i++)
    src += "T(1,2,3) ";
  run(src.c_str());
  EXPECT_EQ(tb.count, 300 * 3 + 1);
}

TEST_F(PreprocessorTest, StringizeSingleToken) {
  run("#define S(x) #x\nS(hello)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_STRING);
  EXPECT_STREQ(tb.tokens[0].value, "hello");
}

TEST_F(PreprocessorTest, StringizeInsertsOneSpaceWhereSourceHadWhitespace) {
  run("#define S(x) #x\nS(a + b)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "a + b");
}

TEST_F(PreprocessorTest, StringizeCollapsesRunsOfWhitespace) {
  run("#define S(x) #x\nS(  a   +   b  )");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "a + b");
}

TEST_F(PreprocessorTest, StringizeAddsNoSpaceBetweenAdjacentTokens) {
  run("#define S(x) #x\nS(a+b)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "a+b");
}

TEST_F(PreprocessorTest, StringizeEmptyArgumentGivesEmptyString) {
  run("#define S(x) #x\nS()");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_STRING);
  ASSERT_NE(tb.tokens[0].value, nullptr);
  EXPECT_STREQ(tb.tokens[0].value, "");
}

TEST_F(PreprocessorTest, StringizeEscapesQuotesAroundAStringLiteral) {
  run("#define S(x) #x\nS(\"hi\")");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "\\\"hi\\\"");
}

TEST_F(PreprocessorTest, StringizeEscapesBackslashesInsideAStringLiteral) {
  run("#define S(x) #x\nS(\"a\\nb\")");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "\\\"a\\\\nb\\\"");
}

TEST_F(PreprocessorTest, StringizeKeepsSingleQuotesUnescaped) {
  run("#define S(x) #x\nS('q')");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "'q'");
}

TEST_F(PreprocessorTest, StringizeMultiTokenArgumentWithParens) {
  run("#define S(x) #x\nS((1,2))");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "(1,2)");
}

TEST_F(PreprocessorTest, StringizeAppliesOnlyToTheParameterAfterTheHash) {
  run("#define S(a,b) #a b\nS(x,y)");
  ASSERT_EQ(tb.count, 3);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_STRING);
  EXPECT_STREQ(tb.tokens[0].value, "x");
  EXPECT_EQ(tb.tokens[1].type, TOKEN_IDENTIFIER);
  EXPECT_STREQ(tb.tokens[1].value, "y");
}

TEST_F(PreprocessorTest, StringizeUsesTheRawArgumentNotTheExpandedOne) {
  run("#define VER 3\n#define S(x) #x\nS(VER)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_STRING);
  EXPECT_STREQ(tb.tokens[0].value, "VER");
}

TEST_F(PreprocessorTest, StringizeParameterUsedTwice) {
  run("#define S(x) #x #x\nS(a)");
  ASSERT_EQ(tb.count, 3);
  EXPECT_STREQ(tb.tokens[0].value, "a");
  EXPECT_STREQ(tb.tokens[1].value, "a");
}

TEST_F(PreprocessorTest, StringizeAndPlainUseOfTheSameParameter) {
  run("#define S(x) #x x\nS(7)");
  ASSERT_EQ(tb.count, 3);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_STRING);
  EXPECT_STREQ(tb.tokens[0].value, "7");
  EXPECT_EQ(tb.tokens[1].type, TOKEN_NUMBER);
  EXPECT_STREQ(tb.tokens[1].value, "7");
}

TEST_F(PreprocessorTest, HashInObjectLikeMacroIsJustAToken) {
  run("#define H #\nH");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_HASH);
}

TEST_F(PreprocessorTest, HashNotFollowedByAParameterIsEmittedLiterally) {
  run("#define S(x) #y x\nS(1)");
  std::vector<std::string> expected = {"#", "y", "1"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, HashAtEndOfMacroBodyIsEmittedLiterally) {
  run("#define S(x) x #\nS(1)");
  std::vector<std::string> expected = {"1", "#"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, StringizedResultIsAStringNotRescanned) {
  run("#define A 1\n#define S(x) #x\nS(A)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_STRING);
  EXPECT_STREQ(tb.tokens[0].value, "A");
}

TEST_F(PreprocessorTest, ManyStringizationsDoNotLeak) {
  std::string src = "#define S(x) #x\n";
  for (int i = 0; i < 300; i++)
    src += "S(a + b) ";
  run(src.c_str());
  EXPECT_EQ(tb.count, 301);
  EXPECT_STREQ(tb.tokens[0].value, "a + b");
  EXPECT_STREQ(tb.tokens[299].value, "a + b");
}

TEST_F(PreprocessorTest, StringizeArgumentSpanningLinesGetsASpace) {
  run("#define S(x) #x\nS(a\n   b)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_STRING);
  EXPECT_STREQ(tb.tokens[0].value, "a b")
      << "the continuation token starts at the same column the previous one ended, "
         "so adjacency must also compare line numbers";
}

TEST_F(PreprocessorTest, PasteJoinsTwoIdentifiers) {
  run("#define CAT(a,b) a##b\nCAT(foo,bar)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_IDENTIFIER);
  EXPECT_STREQ(tb.tokens[0].value, "foobar");
}

TEST_F(PreprocessorTest, PasteResultIsRescannedForMacros) {
  run("#define AB 99\n#define CAT(a,b) a##b\nCAT(A,B)");
  std::vector<std::string> expected = {"99"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, PasteUsesTheRawArgument) {
  run("#define N 2\n#define CAT(a,b) a##b\nCAT(x,N)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "xN");
}

TEST_F(PreprocessorTest, PasteCanProduceAPunctuator) {
  run("#define P(a,b) a##b\nP(+,+)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_PLUS_PLUS);
  EXPECT_STREQ(tb.tokens[0].value, "++");
}

TEST_F(PreprocessorTest, PasteCanProduceANumber) {
  run("#define CAT(a,b) a##b\nCAT(1,e)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_NUMBER);
  EXPECT_STREQ(tb.tokens[0].value, "1e");
}

TEST_F(PreprocessorTest, PasteWithEmptyLeftOperandKeepsTheRight) {
  run("#define CAT(a,b) a##b\n[CAT(,b)]");
  std::vector<std::string> expected = {"[", "b", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, PasteWithEmptyRightOperandKeepsTheLeft) {
  run("#define CAT(a,b) a##b\n[CAT(a,)]");
  std::vector<std::string> expected = {"[", "a", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, PasteWithBothOperandsEmptyProducesNothing) {
  run("#define CAT(a,b) a##b\n[CAT(,)]");
  std::vector<std::string> expected = {"[", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, PasteWithALiteralSuffix) {
  run("#define X(a) a##_end\nX(name)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "name_end");
}

TEST_F(PreprocessorTest, PasteWithALiteralPrefix) {
  run("#define X(a) pre##a\nX(name)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "prename");
}

TEST_F(PreprocessorTest, ThreeWayPasteRunsLeftToRight) {
  run("#define C3(a,b,c) a##b##c\nC3(x,y,z)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "xyz");
}

TEST_F(PreprocessorTest, PasteOnlyTouchesTheBoundaryTokens) {
  run("#define CAT(a,b) a##b\nCAT(x y,z)");
  std::vector<std::string> expected = {"x", "yz"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, StringizeAndPasteInTheSameBody) {
  run("#define M(a,b) #a b##_z\nM(p,q)");
  ASSERT_EQ(tb.count, 3);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_STRING);
  EXPECT_STREQ(tb.tokens[0].value, "p");
  EXPECT_EQ(tb.tokens[1].type, TOKEN_IDENTIFIER);
  EXPECT_STREQ(tb.tokens[1].value, "q_z");
}

TEST_F(PreprocessorTest, InvalidPasteKeepsBothOperands) {
  run("#define CAT(a,b) a##b\nCAT(+,x)");
  std::vector<std::string> expected = {"+", "x"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, PasteWorksInObjectLikeMacros) {
  run("#define J 1##2\nJ");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_NUMBER);
  EXPECT_STREQ(tb.tokens[0].value, "12");
}

TEST_F(PreprocessorTest, PasteAtTheStartOfABodyIsDropped) {
  run("#define S(a) ##a\n[S(x)]");
  std::vector<std::string> expected = {"[", "x", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, PasteAtTheEndOfABodyIsDropped) {
  run("#define S(a) a##\n[S(x)]");
  std::vector<std::string> expected = {"[", "x", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, PastedTokenKeepsTheLeftOperandPosition) {
  run("#define CAT(a,b) a##b\nCAT(foo,bar)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].line, 2);
  EXPECT_GT(tb.tokens[0].column, 0);
}

TEST_F(PreprocessorTest, PasteBuildingAMacroNameThatThenExpands) {
  run("#define prefix_x 5\n#define MK(n) prefix_##n\nMK(x)");
  std::vector<std::string> expected = {"5"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ManyPastesDoNotLeak) {
  std::string src = "#define CAT(a,b) a##b\n";
  for (int i = 0; i < 300; i++)
    src += "CAT(x,y) ";
  run(src.c_str());
  EXPECT_EQ(tb.count, 301);
  EXPECT_STREQ(tb.tokens[0].value, "xy");
  EXPECT_STREQ(tb.tokens[299].value, "xy");
}

TEST_F(PreprocessorTest, ObjectLikeMacroWithoutPasteStillExpands) {
  run("#define A 1 2 3\nA");
  std::vector<std::string> expected = {"1", "2", "3"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, EmptyMiddleOperandJoinsTheTokensAroundIt) {
  run("#define Q(a) x ## a ## y\nQ()");
  std::vector<std::string> expected = {"xy"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, NoLiteralPasteOperatorEscapesIntoTheOutput) {
  run("#define Q(a) x ## a ## y\nQ()");
  for (int i = 0; i < tb.count; i++) {
    EXPECT_NE(tb.tokens[i].type, TOKEN_HASH_HASH) << "a ## survived substitution at index " << i;
  }
}

TEST_F(PreprocessorTest, NonEmptyMiddleOperandPastesBothSides) {
  run("#define Q(a) x ## a ## y\nQ(Z)");
  std::vector<std::string> expected = {"xZy"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, SeveralEmptyOperandsInARowCollapse) {
  run("#define T(a,b) x ## a ## b ## y\nT(,)");
  std::vector<std::string> expected = {"xy"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, EmptyOperandBetweenTwoRealOnesStillJoins) {
  run("#define T(a,b) a ## b ## c\nT(q,)");
  std::vector<std::string> expected = {"qc"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ArgumentAndBodyGrowthIsGeometric) {
  std::string src = "#define ID(x) x\nID(";
  for (int i = 0; i < 2000; i++)
    src += "a ";
  src += ")";

  tb_realloc_calls = 0;
  run(src.c_str());

  EXPECT_EQ(tb.count, 2001);
  EXPECT_LT(tb_realloc_calls, 200)
      << "argument, expansion and substitution lists look like they grow "
         "by one element at a time; linear growth measures ~8000 here";
}

TEST_F(PreprocessorTest, PasteOfTwoEmptySpellingTokensIsRejected) {
  run("#define CAT(a,b) a##b\n[CAT(\\,\\)] tail");

  ASSERT_GT(tb.count, 1);
  EXPECT_EQ(tb.tokens[tb.count - 1].type, TOKEN_EOF);
  for (int i = 0; i < tb.count - 1; i++) {
    EXPECT_NE(tb.tokens[i].type, TOKEN_EOF)
        << "an EOF token was injected into the stream at index " << i;
  }
  EXPECT_STREQ(tb.tokens[tb.count - 2].value, "tail")
      << "tokens after the failed paste must still be emitted";
}

TEST_F(PreprocessorTest, TripleNestedSameMacroExpands) {
  run("#define I(x) x\nI(I(I(7)))");
  std::vector<std::string> expected = {"7"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ArgumentIsExpandedBeforeSubstitution) {
  run("#define VER 3\n#define STR(x) #x\n#define XSTR(x) STR(x)\nXSTR(VER)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_STRING);
  EXPECT_STREQ(tb.tokens[0].value, "3");
}

TEST_F(PreprocessorTest, StringizeStillUsesTheRawArgument) {
  run("#define VER 3\n#define STR(x) #x\nSTR(VER)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_STRING);
  EXPECT_STREQ(tb.tokens[0].value, "VER");
}

TEST_F(PreprocessorTest, PasteThroughAnExtraLevelUsesTheExpandedArgument) {
  run("#define N 2\n#define CAT(a,b) a##b\n#define XCAT(a,b) CAT(a,b)\nXCAT(x,N)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "x2");
}

TEST_F(PreprocessorTest, PasteStillUsesTheRawArgument) {
  run("#define N 2\n#define CAT(a,b) a##b\nCAT(x,N)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "xN");
}

TEST_F(PreprocessorTest, ExpandedArgumentSubstitutedAtEveryUse) {
  run("#define A 1\n#define D(x) x x\nD(A)");
  std::vector<std::string> expected = {"1", "1"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ArgumentThatExpandsToNothing) {
  run("#define E\n#define F(x) [x]\nF(E)");
  std::vector<std::string> expected = {"[", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, MacroNameAsTheLastArgumentDoesNotEatFollowingSource) {
  run("#define F(x) [x]\n#define G(a) a\nG(F)(1)");
  std::vector<std::string> expected = {"[", "1", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ArgumentPreExpansionDoesNotBreakRecursionGuard) {
  run("#define F(x) G(x)\n#define G(x) F(x)\nF(1)");
  std::vector<std::string> expected = {"F", "(", "1", ")"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ArgumentPreExpansionTerminatesOnSelfReference) {
  run("#define F(x) F(x)\nF(1)");
  std::vector<std::string> expected = {"F", "(", "1", ")"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, NestedFunctionLikeInsideAnArgument) {
  run("#define G(a,b) a-b\n#define F(x) [x]\nF(G(1,2))");
  std::vector<std::string> expected = {"[", "1", "-", "2", "]"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ArgumentPreExpansionSurvivesManyInvocations) {
  std::string src = "#define A 1\n#define F(x) [x]\n";
  for (int i = 0; i < 300; i++)
    src += "F(A) ";
  run(src.c_str());
  EXPECT_EQ(tb.count, 300 * 3 + 1);
  EXPECT_STREQ(tb.tokens[1].value, "1");
}

TEST_F(PreprocessorTest, MacroAsLeftPasteOperandStaysRaw) {
  run("#define N 2\n#define CAT(a,b) a##b\nCAT(N,x)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "Nx")
      << "the parameter before ## must use the raw argument, not the expanded one";
}

TEST_F(PreprocessorTest, MacrosOnBothSidesOfPasteStayRaw) {
  run("#define N 2\n#define M 3\n#define CAT(a,b) a##b\nCAT(N,M)");
  ASSERT_EQ(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "NM");
}

TEST_F(PreprocessorTest, IfdefWithAnUndefinedNameSkipsTheBody) {
  run("#ifdef NOPE\nint dead;\n#endif\nint live;");
  std::vector<std::string> expected = {"int", "live", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, IfdefWithADefinedNameKeepsTheBody) {
  run("#define YES 1\n#ifdef YES\nint live;\n#endif");
  std::vector<std::string> expected = {"int", "live", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, IfndefIsTheInverseOfIfdef) {
  run("#define YES 1\n#ifndef YES\nint dead;\n#endif\n#ifndef NOPE\nint live;\n#endif");
  std::vector<std::string> expected = {"int", "live", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, IncludeGuardAdmitsTheBodyOnlyOnce) {
  run("#ifndef G\n#define G\nint once;\n#endif\n#ifndef G\nint twice;\n#endif");
  std::vector<std::string> expected = {"int", "once", ";"};
  EXPECT_EQ(spellings(), expected) << "the second pass must see G defined and skip its body";
}

TEST_F(PreprocessorTest, ElseRunsWhenTheIfWasFalse) {
  run("#ifdef NOPE\nint a;\n#else\nint b;\n#endif");
  std::vector<std::string> expected = {"int", "b", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ElseIsSkippedWhenTheIfWasTaken) {
  run("#define Y\n#ifdef Y\nint a;\n#else\nint b;\n#endif");
  std::vector<std::string> expected = {"int", "a", ";"};
  EXPECT_EQ(spellings(), expected)
      << "#else lexes as TOKEN_ELSE, not TOKEN_IDENTIFIER; if the dispatch "
         "checked the token type it would fall through to skip_directive_line "
         "and both branches would be emitted";
}

TEST_F(PreprocessorTest, NestedLiveGroupsBothEmit) {
  run("#define A\n#define B\n#ifdef A\n#ifdef B\nint ab;\n#endif\n#endif");
  std::vector<std::string> expected = {"int", "ab", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, LiveInnerGroupStaysDeadInsideADeadOuterGroup) {
  run("#define B\n#ifdef NOPE\n#ifdef B\nint ab;\n#endif\n#endif\nint after;");
  std::vector<std::string> expected = {"int", "after", ";"};
  EXPECT_EQ(spellings(), expected)
      << "parent_emitting must veto a nested group whose own condition is true";
}

TEST_F(PreprocessorTest, DeadInnerGroupDoesNotKillTheLiveOuterGroup) {
  run("#define A\n#ifdef A\n#ifdef NOPE\nint x;\n#endif\nint y;\n#endif");
  std::vector<std::string> expected = {"int", "y", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, DeeplyNestedGroupsStayBalanced) {
  run("#define A\n#ifdef A\n#ifdef A\n#ifdef A\nint deep;\n"
      "#endif\n#endif\n#endif\nint after;");
  std::vector<std::string> expected = {"int", "deep", ";", "int", "after", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, DefineInsideASkippedGroupDoesNotTakeEffect) {
  run("#ifdef NOPE\n#define M 9\n#endif\nM");
  std::vector<std::string> expected = {"M"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, UndefInsideASkippedGroupDoesNotTakeEffect) {
  run("#define M 1\n#ifdef NOPE\n#undef M\n#endif\nM");
  std::vector<std::string> expected = {"1"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, MacrosAreNotExpandedInsideASkippedGroup) {
  run("#define M 1\n#ifdef NOPE\nM M M\n#endif\nM");
  std::vector<std::string> expected = {"1"};
  EXPECT_EQ(spellings(), expected) << "the emitting check must sit before try_expand, not after";
}

TEST_F(PreprocessorTest, SkippedGroupsDoNoExpansionWork) {
  std::string src = "#define M 1 2 3 4 5 6 7 8\n#ifdef NOPE\n";
  for (int i = 0; i < 500; i++)
    src += "M ";
  src += "\n#endif\nint ok;";

  tb_realloc_calls = 0;
  run(src.c_str());

  std::vector<std::string> expected = {"int", "ok", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_LT(tb_realloc_calls, 50)
      << "the emitting check must come before try_expand: expanding 500 "
         "macros and throwing the result away produces the right tokens but "
         "does thousands of allocations of pointless work";
}

TEST_F(PreprocessorTest, MalformedCodeInsideASkippedGroupIsIgnored) {
  run("#ifdef NOPE\nF(1,2,3) ## ## #\n#endif\nint ok;");
  std::vector<std::string> expected = {"int", "ok", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, UnknownDirectiveInsideASkippedGroupIsIgnored) {
  run("#ifdef NOPE\n#pragma once\n#nonsense here\n#endif\nint ok;");
  std::vector<std::string> expected = {"int", "ok", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, EndifWithoutIfIsAnError) {
  run("#endif\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, ElseWithoutIfIsAnError) {
  run("#else\nint x;");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, ElifWithoutIfIsAnError) {
  run("#elif 1\nint x;");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, SecondElseIsAnErrorAndItsBranchIsSkipped) {
  run("#ifdef NOPE\nint a;\n#else\nint b;\n#else\nint c;\n#endif");
  std::vector<std::string> expected = {"int", "b", ";"};
  EXPECT_EQ(spellings(), expected)
      << "after reporting the duplicate #else its branch must be dead, "
         "not emitted alongside the branch already taken";
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, ElifAfterElseIsAnErrorAndItsBranchIsSkipped) {
  run("#ifdef NOPE\nint a;\n#else\nint b;\n#elif 1\nint c;\n#endif");
  std::vector<std::string> expected = {"int", "b", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1) << "the #elif must be rejected before its condition is evaluated, "
                      "so the unsupported-#if stub must not also fire";
}

TEST_F(PreprocessorTest, UnterminatedIfIsAnError) {
  run("#ifdef NOPE\nint dead;");
  EXPECT_TRUE(spellings().empty());
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, EveryUnterminatedGroupIsReported) {
  run("#ifdef A\n#ifdef B\nint dead;");
  EXPECT_EQ(rc, 2) << "one diagnostic per open group, not one for the file";
}

TEST_F(PreprocessorTest, IfdefWithoutANameIsAnError) {
  run("#ifdef\nint x;\n#endif\nint after;");
  std::vector<std::string> expected = {"int", "after", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1) << "the group must still be pushed so its #endif does not report a "
                      "second, spurious error";
}

TEST_F(PreprocessorTest, IfdefWithANonIdentifierIsAnError) {
  run("#ifdef 123\nint x;\n#endif\nint after;");
  std::vector<std::string> expected = {"int", "after", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, IfWithATrueConstantKeepsTheBody) {
  run("#if 1\nint x;\n#endif\nint after;");
  std::vector<std::string> expected = {"int", "x", ";", "int", "after", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, DeadIfIsNotEvaluated) {
  run("#ifdef NOPE\n#if 1\nint x;\n#endif\n#endif\nint after;");
  std::vector<std::string> expected = {"int", "after", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0) << "a #if inside a skipped group must not evaluate its condition, so "
                      "the unsupported-#if stub must not fire";
}

TEST_F(PreprocessorTest, ACleanFileReportsNoDiagnostics) {
  run("#define M 1\n#ifndef G\nint x = M;\n#endif");
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, PpRunRejectsNullArgumentsWithMinusOne) {
  EXPECT_EQ(pp_run(nullptr, "int x;"), -1);

  token_buf local;
  EXPECT_EQ(pp_run(&local, nullptr), -1);
  token_buf_free(&local);
}

TEST_F(PreprocessorTest, UnterminatedGroupsDoNotLeakTheirStack) {
  std::string one = "#ifdef A\n";
  std::string many;
  for (int i = 0; i < 100; i++)
    many += "#ifdef A\n";

  token_buf a;
  tb_free_calls = 0;
  pp_run(&a, one.c_str());
  int frees_one = tb_free_calls;
  token_buf_free(&a);

  token_buf b;
  tb_free_calls = 0;
  pp_run(&b, many.c_str());
  int frees_many = tb_free_calls;
  token_buf_free(&b);

  EXPECT_EQ(frees_many - frees_one, 99 * 6)
      << "each extra '#ifdef A' line frees six things: the '#' token, the "
         "'ifdef' and 'A' tokens, the scratch buffer lexer_collect_identifier "
         "frees after strdup for each of those two identifiers, and the cond "
         "node. A delta of 99*5 means pp_destroy is not draining the cond stack";
}

TEST_F(PreprocessorTest, IfWithAFalseConstantSkipsTheBody) {
  run("#if 0\nint x;\n#endif\nint after;");
  std::vector<std::string> expected = {"int", "after", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, AnyNonZeroValueIsTrue) {
  run("#if 2\nA\n#endif\n#if -1\nB\n#endif");
  std::vector<std::string> expected = {"A", "B"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ArithmeticAndPrecedenceAreEvaluated) {
  run("#if 2 + 3 * 4 == 14\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, ParenthesesOverridePrecedence) {
  run("#if (2 + 3) * 4 == 20\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, UnaryOperatorsWork) {
  run("#if !0\nA\n#endif\n#if ~0 == -1\nB\n#endif\n#if - -1 == 1\nC\n#endif");
  std::vector<std::string> expected = {"A", "B", "C"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ShiftOperatorsWork) {
  run("#if (1 << 4) == 16 && (32 >> 2) == 8\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, BitwiseOperatorsWork) {
  run("#if (6 & 3) == 2 && (6 | 3) == 7 && (6 ^ 3) == 5\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, DivisionAndModuloWork) {
  run("#if 7 / 2 == 3 && 7 % 2 == 1\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, RelationalOperatorsWork) {
  run("#if 1 < 2 && 2 <= 2 && 3 > 2 && 3 >= 3 && 1 != 2\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ConditionalOperatorPicksTheRightArm) {
  run("#if 1 ? 0 : 1\nA\n#endif\n#if 0 ? 0 : 1\nB\n#endif");
  std::vector<std::string> expected = {"B"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ConditionalOperatorIsRightAssociative) {
  run("#if (0 ? 1 : 0 ? 2 : 3) == 3\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, HexAndOctalConstantsAreUnderstood) {
  run("#if 0x10 == 16 && 010 == 8\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, HexDigitEIsNotAnExponent) {
  run("#if 0xE == 14\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected)
      << "the float check must look for p in hex and e only in decimal, or "
         "every hex constant containing E is rejected as a float";
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, IntegerSuffixesAreAccepted) {
  run("#if 1u == 1 && 2UL == 2 && 3ll == 3\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, FloatingConstantsAreRejected) {
  run("#if 1.5\nA\n#endif");
  EXPECT_TRUE(spellings().empty());
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, DecimalExponentsAreRejected) {
  run("#if 1e3\nA\n#endif");
  EXPECT_TRUE(spellings().empty());
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, DefinedWorksWithAndWithoutParentheses) {
  run("#define X 1\n#if defined X\nA\n#endif\n#if defined(X)\nB\n#endif");
  std::vector<std::string> expected = {"A", "B"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, DefinedIsFalseForAnUnknownName) {
  run("#if defined NOPE\nA\n#endif");
  EXPECT_TRUE(spellings().empty());
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, DefinedIsResolvedBeforeMacroExpansion) {
  run("#define FOO 0\n#if defined FOO\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected)
      << "FOO expands to 0, so expanding first would leave 'defined 0'. "
         "defined must be resolved while the name is still a name";
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, DefinedFollowsUndef) {
  run("#define X 1\n#undef X\n#if defined X\nA\n#endif");
  EXPECT_TRUE(spellings().empty());
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, DefinedWithoutAnIdentifierIsAnError) {
  run("#if defined 1\nA\n#endif");
  EXPECT_GT(rc, 0);
}

TEST_F(PreprocessorTest, DefinedWithAnUnclosedParenIsAnError) {
  run("#if defined(X\nA\n#endif");
  EXPECT_GT(rc, 0);
}

TEST_F(PreprocessorTest, MacrosInTheConditionAreExpanded) {
  run("#define N 5\n#if N > 3\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, AMacroCanExpandToAWholeExpression) {
  run("#define E 1 + 1\n#if E == 2\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, FunctionLikeMacrosWorkInTheCondition) {
  run("#define SQ(x) ((x) * (x))\n#if SQ(3) == 9\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, AMacroExpandingToNothingIsAnError) {
  run("#define E\n#if E\nA\n#endif");
  EXPECT_TRUE(spellings().empty());
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, AnUnknownIdentifierEvaluatesToZero) {
  run("#if NOPE\nA\n#endif\n#if NOPE == 0\nB\n#endif");
  std::vector<std::string> expected = {"B"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, AKeywordEvaluatesToZeroLikeAnyIdentifier) {
  run("#if int\nA\n#endif\n#if int == 0\nB\n#endif");
  std::vector<std::string> expected = {"B"};
  EXPECT_EQ(spellings(), expected)
      << "int lexes as TOKEN_INT, but at preprocessing time it is just a "
         "name, so it must evaluate to 0 rather than raise an error";
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, LogicalAndShortCircuitsAwayADivisionByZero) {
  run("#if defined SIZE && 100 / SIZE > 2\nA\n#endif");
  EXPECT_TRUE(spellings().empty());
  EXPECT_EQ(rc, 0) << "the right operand of a false && must be parsed but not evaluated, "
                      "so 100/0 must not report";
}

TEST_F(PreprocessorTest, LogicalOrShortCircuitsAwayADivisionByZero) {
  run("#if 1 || 1 / 0\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, ConditionalDoesNotEvaluateTheUntakenArm) {
  run("#if 1 ? 1 : 1 / 0\nA\n#endif\n#if 0 ? 1 / 0 : 1\nB\n#endif");
  std::vector<std::string> expected = {"A", "B"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, ALiveDivisionByZeroIsStillAnError) {
  run("#if 1 / 0\nA\n#endif");
  EXPECT_EQ(rc, 1) << "short-circuiting must not silence a division that is "
                      "actually evaluated";
}

TEST_F(PreprocessorTest, ShortCircuitDoesNotHideALiveRightOperand) {
  run("#if 0 || 1 / 0\nA\n#endif");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, ModuloByZeroIsAnError) {
  run("#if 1 % 0\nA\n#endif");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, ShiftCountOutOfRangeIsAnError) {
  run("#if 1 << 64\nA\n#endif");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, ShiftCountOutOfRangeIsNotCheckedWhenDead) {
  run("#if 0 && (1 << 64)\nA\n#endif");
  EXPECT_TRUE(spellings().empty());
  EXPECT_EQ(rc, 0) << "the shift guard must be gated on live like the division guard, or "
                      "a short-circuited operand reports an error it never evaluated";
}

TEST_F(PreprocessorTest, AValueWithZeroLowBitsIsStillTrue) {
  run("#if 4294967296\nA\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected)
      << "the condition must be tested as value != 0 over the full long long; "
         "truncating to int makes every multiple of 2^32 read as false";
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, AnInvalidIntegerSuffixIsAnError) {
  run("#if 1abc\nA\n#endif");
  EXPECT_EQ(rc, 1) << "only u/U/l/L may follow the digits; anything else left over means "
                      "the constant was not fully consumed";
}

TEST_F(PreprocessorTest, AnEmptyIfIsAnError) {
  run("#if\nA\n#endif");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, ExtraTokensAfterTheExpressionAreAnError) {
  run("#if 1 2\nA\n#endif");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, AnUnbalancedParenIsAnError) {
  run("#if (1\nA\n#endif");
  EXPECT_GT(rc, 0);
}

TEST_F(PreprocessorTest, AMissingColonIsAnError) {
  run("#if 1 ? 2\nA\n#endif");
  EXPECT_GT(rc, 0);
}

TEST_F(PreprocessorTest, AStringLiteralInTheConditionIsAnError) {
  run("#if \"x\"\nA\n#endif");
  EXPECT_GT(rc, 0);
}

TEST_F(PreprocessorTest, ElifTakesTheFirstTrueBranch) {
  run("#if 0\nA\n#elif 1\nB\n#elif 1\nC\n#endif");
  std::vector<std::string> expected = {"B"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, ElifAfterATakenBranchIsNotEvaluated) {
  run("#if 1\nA\n#elif 1 / 0\nB\n#endif");
  std::vector<std::string> expected = {"A"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0) << "once a branch is taken, later #elif conditions must not be "
                      "evaluated at all";
}

TEST_F(PreprocessorTest, ElifChainFallsThroughToElse) {
  run("#if 0\nA\n#elif 0\nB\n#else\nC\n#endif");
  std::vector<std::string> expected = {"C"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, NestedIfsWithExpressions) {
  run("#define N 5\n#if N > 3\n#if N > 10\nA\n#else\nB\n#endif\n#endif");
  std::vector<std::string> expected = {"B"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, TheConditionDoesNotSwallowTheFollowingLine) {
  run("#if 1\nA B C\n#endif");
  std::vector<std::string> expected = {"A", "B", "C"};
  EXPECT_EQ(spellings(), expected)
      << "the line terminator must be ungot after expand_token_list runs, "
         "not before, or the expansion loop takes it from the pending slot "
         "and the body loses its first token";
}

TEST_F(PreprocessorTest, ManyConditionsDoNotLeak) {
  std::string src;
  for (int i = 0; i < 300; i++)
    src += "#if 1 + 1 == 2\nA\n#endif\n";
  run(src.c_str());
  EXPECT_EQ(tb.count, 301);
  EXPECT_EQ(rc, 0);
}

class IncludeTest : public ::testing::Test {
protected:
  token_buf tb;
  bool initialised = false;
  int rc = 0;

  static void make_dir(const char *path) {
    _mkdir(path);
  }

  static void write_file(const char *path, const char *text) {
    std::ofstream f(path, std::ios::binary);
    f << text;
  }

  void SetUp() override {
    make_dir("pp_test_inc");
    make_dir("pp_test_inc/sub");
    make_dir("pp_test_src");

    write_file("pp_test_inc/simple.h", "int from_inc;\n");
    write_file("pp_test_inc/same.h", "int from_inc_dir;\n");
    write_file("pp_test_inc/guard.h", "#ifndef G\n#define G\nint guarded;\n#endif\n");
    write_file("pp_test_inc/macros.h", "#define HDR_VALUE 42\n");
    write_file("pp_test_inc/nested.h", "#include \"simple.h\"\nint nested;\n");
    write_file("pp_test_inc/ends_with_directive.h", "#define ENDS 7\n");
    write_file("pp_test_inc/sub/deep.h", "int deep;\n");
    write_file("pp_test_inc/cyc1.h", "#include \"cyc2.h\"\n");
    write_file("pp_test_inc/cyc2.h", "#include \"cyc1.h\"\n");
    write_file("pp_test_src/same.h", "int from_src_dir;\n");
    write_file("pp_test_src/standalone.c", "#include \"same.h\"\nint tail;\n");
  }

  void TearDown() override {
    if (initialised) {
      token_buf_free(&tb);
    }
    static const char *files[] = {
        "pp_test_inc/simple.h",   "pp_test_inc/same.h",      "pp_test_inc/guard.h",
        "pp_test_inc/macros.h",   "pp_test_inc/nested.h",    "pp_test_inc/ends_with_directive.h",
        "pp_test_inc/sub/deep.h", "pp_test_inc/cyc1.h",      "pp_test_inc/cyc2.h",
        "pp_test_src/same.h",     "pp_test_src/standalone.c"};
    for (const char *f : files) {
      remove(f);
    }
    _rmdir("pp_test_inc/sub");
    _rmdir("pp_test_inc");
    _rmdir("pp_test_src");
  }

  void run(const char *source) {
    static const char *dirs[] = {"pp_test_inc"};
    rc = pp_run_ex(&tb, source, "pp_test_src/main.c", dirs, 1);
    initialised = true;
  }

  std::vector<std::string> spellings() {
    std::vector<std::string> out;
    for (int i = 0; i < tb.count; i++) {
      if (tb.tokens[i].type == TOKEN_EOF)
        continue;
      out.push_back(tb.tokens[i].value ? tb.tokens[i].value : "<null>");
    }
    return out;
  }
};

TEST_F(IncludeTest, QuotedIncludeInsertsTheFileContents) {
  run("#include \"simple.h\"\nint after;");
  std::vector<std::string> expected = {"int", "from_inc", ";", "int", "after", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(IncludeTest, TokensAfterTheDirectiveFollowTheIncludedContent) {
  run("#include \"simple.h\"\nint after;");
  ASSERT_GE(tb.count, 2);
  EXPECT_STREQ(tb.tokens[1].value, "from_inc")
      << "the pending token belongs to the including file, so it must not be "
         "emitted before the included content";
}

TEST_F(IncludeTest, AngledIncludeFindsTheFileOnTheSearchPath) {
  run("#include <simple.h>\nint after;");
  std::vector<std::string> expected = {"int", "from_inc", ";", "int", "after", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(IncludeTest, QuotedIncludeSearchesTheIncludingFilesDirectoryFirst) {
  run("#include \"same.h\"");
  std::vector<std::string> expected = {"int", "from_src_dir", ";"};
  EXPECT_EQ(spellings(), expected)
      << "a quoted include must prefer the directory of the including file";
}

TEST_F(IncludeTest, AngledIncludeIgnoresTheIncludingFilesDirectory) {
  run("#include <same.h>");
  std::vector<std::string> expected = {"int", "from_inc_dir", ";"};
  EXPECT_EQ(spellings(), expected)
      << "an angled include must only search the configured directories";
}

TEST_F(IncludeTest, NestedIncludesWork) {
  run("#include \"nested.h\"\nint after;");
  std::vector<std::string> expected = {"int", "from_inc", ";",     "int", "nested",
                                       ";",   "int",      "after", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(IncludeTest, IncludeGuardsWorkAcrossFiles) {
  run("#include \"guard.h\"\n#include \"guard.h\"\nint after;");
  std::vector<std::string> expected = {"int", "guarded", ";", "int", "after", ";"};
  EXPECT_EQ(spellings(), expected) << "the second include must see G defined and skip the body";
  EXPECT_EQ(rc, 0);
}

TEST_F(IncludeTest, MacrosFromAHeaderAreVisibleAfterIt) {
  run("#include \"macros.h\"\nint v = HDR_VALUE;");
  std::vector<std::string> expected = {"int", "v", "=", "42", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(IncludeTest, AHeaderEndingInADirectiveDoesNotTruncate) {
  run("#include \"ends_with_directive.h\"\nint v = ENDS;");
  std::vector<std::string> expected = {"int", "v", "=", "7", ";"};
  EXPECT_EQ(spellings(), expected)
      << "the frame must not be popped while a directive in it is still "
         "being processed";
  EXPECT_EQ(rc, 0);
}

TEST_F(IncludeTest, SubdirectoryHeaderNamesReassemble) {
  run("#include <sub/deep.h>\nint after;");
  std::vector<std::string> expected = {"int", "deep", ";", "int", "after", ";"};
  EXPECT_EQ(spellings(), expected) << "<sub/deep.h> lexes as several tokens and must be rejoined";
  EXPECT_EQ(rc, 0);
}

TEST_F(IncludeTest, TwoIncludesInARow) {
  run("#include \"simple.h\"\n#include \"macros.h\"\nint v = HDR_VALUE;");
  std::vector<std::string> expected = {"int", "from_inc", ";", "int", "v", "=", "42", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(IncludeTest, AMissingFileIsReportedOnceAndParsingContinues) {
  run("#include \"nope.h\"\nint after;");
  std::vector<std::string> expected = {"int", "after", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1);
}

TEST_F(IncludeTest, AMalformedHeaderNameIsAnError) {
  run("#include 123\nint after;");
  std::vector<std::string> expected = {"int", "after", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1);
}

TEST_F(IncludeTest, AnUnterminatedAngleBracketIsAnError) {
  run("#include <simple.h\nint after;");
  EXPECT_EQ(rc, 1);
}

TEST_F(IncludeTest, IncludeInADeadBranchIsNotOpened) {
  run("#ifdef NOPE\n#include \"nope.h\"\n#endif\nint after;");
  std::vector<std::string> expected = {"int", "after", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0) << "a skipped #include must not even try to open the file";
}

TEST_F(IncludeTest, IncludeInALiveBranchIsOpened) {
  run("#define YES\n#ifdef YES\n#include \"simple.h\"\n#endif\nint after;");
  std::vector<std::string> expected = {"int", "from_inc", ";", "int", "after", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(IncludeTest, CyclicIncludesStopAtTheDepthCap) {
  run("#include \"cyc1.h\"\nint after;");
  std::vector<std::string> expected = {"int", "after", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1) << "exactly one depth diagnostic; more than one means frames are being "
                      "popped mid-directive and the nesting is not actually growing";
}

TEST_F(IncludeTest, PpRunFileReadsFromDisk) {
  static const char *dirs[] = {"pp_test_inc"};
  rc = pp_run_file(&tb, "pp_test_src/standalone.c", dirs, 1);
  initialised = true;
  std::vector<std::string> expected = {"int", "from_src_dir", ";", "int", "tail", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(IncludeTest, PpRunFileOnAMissingPathFails) {
  rc = pp_run_file(&tb, "pp_test_src/does_not_exist.c", nullptr, 0);
  initialised = true;
  EXPECT_EQ(rc, -1);
}

TEST_F(IncludeTest, ManyIncludesDoNotLeak) {
  std::string src;
  for (int i = 0; i < 50; i++)
    src += "#include \"simple.h\"\n";
  src += "int after;";
  run(src.c_str());
  EXPECT_EQ(tb.count, 50 * 3 + 3 + 1);
  EXPECT_EQ(rc, 0);
}

class PredefinedTest : public ::testing::Test {
protected:
  token_buf tb;
  bool initialised = false;
  int rc = 0;

  void run(const char *source, const char *filename = "demo.c") {
    rc = pp_run_ex(&tb, source, filename, nullptr, 0);
    initialised = true;
  }

  void TearDown() override {
    if (initialised) {
      token_buf_free(&tb);
    }
  }

  const token *first_of(token_type type) {
    for (int i = 0; i < tb.count; i++) {
      if (tb.tokens[i].type == type)
        return &tb.tokens[i];
    }
    return nullptr;
  }

  std::vector<std::string> spellings() {
    std::vector<std::string> out;
    for (int i = 0; i < tb.count; i++) {
      if (tb.tokens[i].type == TOKEN_EOF)
        continue;
      out.push_back(tb.tokens[i].value ? tb.tokens[i].value : "<null>");
    }
    return out;
  }
};

TEST_F(PredefinedTest, StdcIsOne) {
  run("__STDC__");
  std::vector<std::string> expected = {"1"};
  EXPECT_EQ(spellings(), expected);
  ASSERT_GE(tb.count, 1);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_NUMBER);
}

TEST_F(PredefinedTest, StdcVersionIsC99) {
  run("__STDC_VERSION__");
  std::vector<std::string> expected = {"199901L"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_NUMBER);
}

TEST_F(PredefinedTest, StdcHostedIsOne) {
  run("__STDC_HOSTED__");
  std::vector<std::string> expected = {"1"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PredefinedTest, DateIsAStringLiteralInTheC99Format) {
  run("__DATE__");
  ASSERT_GE(tb.count, 1);
  ASSERT_EQ(tb.tokens[0].type, TOKEN_STRING)
      << "__DATE__ must be a string literal, not an identifier";
  std::string date = tb.tokens[0].value;
  ASSERT_EQ(date.size(), 11u) << "C99 requires exactly \"Mmm dd yyyy\"";
  EXPECT_EQ(date[3], ' ');
  EXPECT_EQ(date[6], ' ');
  EXPECT_NE(date[4], '0') << "a single-digit day is padded with a space, not a zero";
}

TEST_F(PredefinedTest, TimeIsAStringLiteralInTheC99Format) {
  run("__TIME__");
  ASSERT_GE(tb.count, 1);
  ASSERT_EQ(tb.tokens[0].type, TOKEN_STRING);
  std::string t = tb.tokens[0].value;
  ASSERT_EQ(t.size(), 8u) << "C99 requires exactly \"hh:mm:ss\"";
  EXPECT_EQ(t[2], ':');
  EXPECT_EQ(t[5], ':');
}

TEST_F(PredefinedTest, FileIsTheCurrentFileNameAsAString) {
  run("__FILE__", "src/demo.c");
  ASSERT_GE(tb.count, 1);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_STRING);
  EXPECT_STREQ(tb.tokens[0].value, "src/demo.c");
}

TEST_F(PredefinedTest, FileFallsBackWhenThereIsNoFileName) {
  rc = pp_run(&tb, "__FILE__");
  initialised = true;
  ASSERT_GE(tb.count, 1);
  EXPECT_STREQ(tb.tokens[0].value, "<source>");
}

TEST_F(PredefinedTest, FileEscapesBackslashesInThePath) {
  run("__FILE__", "dir\\demo.c");
  ASSERT_GE(tb.count, 1);
  EXPECT_STREQ(tb.tokens[0].value, "dir\\\\demo.c")
      << "the token holds a string-literal spelling, so a path separator "
         "must be escaped or phase 5 decoding sees an unknown escape";
}

TEST_F(PredefinedTest, LineIsANumberThatTracksTheSourceLine) {
  run("__LINE__\n__LINE__\n\n__LINE__");
  std::vector<std::string> expected = {"1", "2", "4"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_NUMBER);
}

TEST_F(PredefinedTest, LineWorksInsideAConditional) {
  run("#if __LINE__ == 1\nint yes;\n#endif");
  std::vector<std::string> expected = {"int", "yes", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PredefinedTest, LineReachedThroughAnotherMacroIsTheInvocationLine) {
  run("#define L __LINE__\nint a;\nint b;\nL");
  std::vector<std::string> expected = {"int", "a", ";", "int", "b", ";", "4"};
  EXPECT_EQ(spellings(), expected)
      << "the __LINE__ token lives in a body defined on line 1, but the line "
         "that counts is where the outermost macro was invoked";
}

TEST_F(PredefinedTest, LineSurvivesTwoLevelsOfIndirection) {
  run("#define INNER __LINE__\n#define OUTER INNER\n\n\nOUTER");
  std::vector<std::string> expected = {"5"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PredefinedTest, LineIsRecordedFreshForEachInvocation) {
  run("#define L __LINE__\nL\nL\n\nL");
  std::vector<std::string> expected = {"2", "3", "5"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PredefinedTest, PredefinedMacrosAnswerDefined) {
  run("#if defined(__FILE__) && defined(__LINE__) && defined(__STDC__)\n"
      "int yes;\n#endif");
  std::vector<std::string> expected = {"int", "yes", ";"};
  EXPECT_EQ(spellings(), expected)
      << "dynamic macros must live in the macro table so defined() finds them";
  EXPECT_EQ(rc, 0);
}

TEST_F(PredefinedTest, PredefinedMacrosAnswerIfdef) {
  run("#ifdef __LINE__\nint yes;\n#endif");
  std::vector<std::string> expected = {"int", "yes", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PredefinedTest, StdcVersionDrivesFeatureTests) {
  run("#if __STDC_VERSION__ >= 199901L\nint c99;\n#else\nint older;\n#endif");
  std::vector<std::string> expected = {"int", "c99", ";"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PredefinedTest, OrdinaryMacrosAreNotTreatedAsDynamic) {
  run("#define M 7\nM");
  std::vector<std::string> expected = {"7"};
  EXPECT_EQ(spellings(), expected) << "do_define must zero the dynamic marker, or every user macro "
                                      "expands as if it were __FILE__";
}

TEST_F(PredefinedTest, RedefiningAPredefinedMacroIsAnError) {
  run("#define __LINE__ 5\nint a;");
  std::vector<std::string> expected = {"int", "a", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1);
}

TEST_F(PredefinedTest, UndefiningAPredefinedMacroIsAnError) {
  run("#undef __FILE__\n__FILE__");
  EXPECT_EQ(rc, 1);
  ASSERT_GE(tb.count, 1);
  EXPECT_EQ(tb.tokens[0].type, TOKEN_STRING) << "the rejected #undef must leave the macro working";
}

TEST_F(PredefinedTest, DefiningTheDefinedOperatorIsAnError) {
  run("#define defined 1\nint a;");
  std::vector<std::string> expected = {"int", "a", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1);
}

TEST_F(PredefinedTest, EveryReservedNameIsProtected) {
  run("#define __FILE__ 1\n#define __LINE__ 1\n#define __DATE__ 1\n"
      "#define __TIME__ 1\n#define __STDC__ 1\n#define __STDC_VERSION__ 1\n"
      "#define __STDC_HOSTED__ 1\n#define defined 1\nint a;");
  std::vector<std::string> expected = {"int", "a", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 8) << "one diagnostic per reserved name";
}

TEST_F(PredefinedTest, StringizeUsesTheMacroNameNotItsValue) {
  run("#define S(x) #x\nS(__FILE__)");
  std::vector<std::string> expected = {"__FILE__"};
  EXPECT_EQ(spellings(), expected)
      << "# uses the raw argument, so the name must not be expanded first";
}

TEST_F(PredefinedTest, ManyDynamicExpansionsDoNotLeak) {
  std::string src;
  for (int i = 0; i < 200; i++)
    src += "__LINE__ __FILE__\n";
  run(src.c_str());
  EXPECT_EQ(tb.count, 400 + 1);
  EXPECT_EQ(rc, 0);
}

TEST_F(IncludeTest, FileNameFollowsIntoAnIncludedHeaderAndBack) {
  write_file("pp_test_inc/where.h", "__FILE__\n");
  run("#include \"where.h\"\n__FILE__");
  ASSERT_GE(tb.count, 2);
  EXPECT_STREQ(tb.tokens[0].value, "pp_test_inc/where.h")
      << "__FILE__ inside a header is the header, not the includer";
  EXPECT_STREQ(tb.tokens[1].value, "pp_test_src/main.c") << "and it reverts when the frame pops";
  remove("pp_test_inc/where.h");
}

TEST_F(IncludeTest, LineNumbersAreRelativeToTheirOwnFile) {
  write_file("pp_test_inc/lines.h", "int a;\n__LINE__\n");
  run("#include \"lines.h\"\n__LINE__");
  std::vector<std::string> expected = {"int", "a", ";", "2", "2"};
  EXPECT_EQ(spellings(), expected)
      << "the header's __LINE__ is line 2 of the header; the outer one is "
         "line 2 of the including file";
  remove("pp_test_inc/lines.h");
}

TEST_F(PreprocessorTest, VariadicMacroPassesOneTrailingArgument) {
  run("#define L(f, ...) g(f, __VA_ARGS__)\nL(1, 2)");
  std::vector<std::string> expected = {"g", "(", "1", ",", "2", ")"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, VaArgsKeepsTheCommasBetweenTrailingArguments) {
  run("#define L(f, ...) g(f, __VA_ARGS__)\nL(1, 2, 3, 4)");
  std::vector<std::string> expected = {"g", "(", "1", ",", "2", ",", "3", ",", "4", ")"};
  EXPECT_EQ(spellings(), expected) << "trailing arguments substitute as one unit, commas included";
}

TEST_F(PreprocessorTest, VaArgsWithNoTrailingArgumentsIsEmpty) {
  run("#define L(f, ...) g(f, __VA_ARGS__)\nL(1)");
  std::vector<std::string> expected = {"g", "(", "1", ",", ")"};
  EXPECT_EQ(spellings(), expected)
      << "C99 has no comma swallowing, so the comma written in the body "
         "stays; that is exactly why the GNU , ## __VA_ARGS__ exists";
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, VariadicMacroWithNoNamedParameters) {
  run("#define E(...) g(__VA_ARGS__)\nE(1, 2)");
  std::vector<std::string> expected = {"g", "(", "1", ",", "2", ")"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, VariadicMacroWithNoNamedParametersAndNoArguments) {
  run("#define E(...) g(__VA_ARGS__)\nE()");
  std::vector<std::string> expected = {"g", "(", ")"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, CommasInsideParenthesesStayInOneTrailingArgument) {
  run("#define E(...) g(__VA_ARGS__)\nE(f(1, 2), 3)");
  std::vector<std::string> expected = {"g", "(", "f", "(", "1", ",", "2", ")", ",", "3", ")"};
  EXPECT_EQ(spellings(), expected) << "the depth check must still apply inside trailing arguments";
}

TEST_F(PreprocessorTest, StringizeVaArgsIncludesTheCommas) {
  run("#define S(...) #__VA_ARGS__\nS(1, 2, 3)");
  std::vector<std::string> expected = {"1, 2, 3"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, VaArgsCanBePasted) {
  run("#define P(a, ...) a##__VA_ARGS__\nP(x, y)");
  std::vector<std::string> expected = {"xy"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, NestedInvocationsInTrailingArgumentsExpand) {
  run("#define I(x) [x]\n#define E(...) g(__VA_ARGS__)\nE(I(1), I(2))");
  std::vector<std::string> expected = {"g", "(", "[", "1", "]", ",", "[", "2", "]", ")"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, TrailingArgumentsArePreExpanded) {
  run("#define N 9\n#define E(...) g(__VA_ARGS__)\nE(N, N)");
  std::vector<std::string> expected = {"g", "(", "9", ",", "9", ")"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, TooFewArgumentsForAVariadicMacroIsStillRejected) {
  run("#define L(a, b, ...) x\nL(1)");
  std::vector<std::string> expected = {"L"};
  EXPECT_EQ(spellings(), expected)
      << "the empty-va_args top-up covers one missing argument, not two";
}

TEST_F(PreprocessorTest, OrdinaryMacrosAreUnaffectedByTheVariadicPath) {
  run("#define M(a, b) a+b\nM(1, 2)");
  std::vector<std::string> expected = {"1", "+", "2"};
  EXPECT_EQ(spellings(), expected)
      << "a non-variadic macro must still split on every top-level comma";
}

TEST_F(PreprocessorTest, EllipsisMustBeTheLastParameter) {
  run("#define B(a, ..., b) x\nint ok;");
  std::vector<std::string> expected = {"int", "ok", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, VaArgsCannotBeAParameterName) {
  run("#define B(__VA_ARGS__) x\nint ok;");
  std::vector<std::string> expected = {"int", "ok", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, VaArgsInANonVariadicBodyIsAnError) {
  run("#define B(a) __VA_ARGS__\nint ok;");
  std::vector<std::string> expected = {"int", "ok", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, VaArgsIsAReservedMacroName) {
  run("#define __VA_ARGS__ 1\nint ok;");
  EXPECT_EQ(rc, 1);
  run("#undef __VA_ARGS__\nint ok;");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, AVariadicMacroWorksInAConditional) {
  run("#define V(...) 1\n#if V(1, 2)\nint yes;\n#endif");
  std::vector<std::string> expected = {"int", "yes", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, AVariadicMacroObeysTheRecursionGuard) {
  run("#define R(...) R(__VA_ARGS__)\nR(1, 2)");
  std::vector<std::string> expected = {"R", "(", "1", ",", "2", ")"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ManyVariadicInvocationsDoNotLeak) {
  std::string src = "#define E(...) g(__VA_ARGS__)\n";
  for (int i = 0; i < 300; i++)
    src += "E(1, 2) ";
  run(src.c_str());
  EXPECT_EQ(tb.count, 300 * 6 + 1);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, UnsignedComparisonConvertsTheSignedOperand) {
  run("#if -1 > 0u\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected)
      << "-1 converts to a huge unsigned value; this is the whole point of "
         "the usual arithmetic conversions";
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, SignedComparisonIsUnaffected) {
  run("#if -1 > 0\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"NO"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, UnsignednessPropagatesOutOfSubexpressions) {
  run("#if (1 + 0u) > -1\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"NO"};
  EXPECT_EQ(spellings(), expected)
      << "the sum is unsigned, so -1 converts and becomes the larger value";
}

TEST_F(PreprocessorTest, UnsignednessSurvivesParentheses) {
  run("#if ((1u)) > -1\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"NO"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ConditionalResultTypeConvertsBothArms) {
  run("#if (0 ? 1 : 2u) > -1\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"NO"};
  EXPECT_EQ(spellings(), expected)
      << "the type of ?: comes from both arms even though one supplies the "
         "value, so an unsigned arm makes the whole thing unsigned";
}

TEST_F(PreprocessorTest, ConditionalWithTwoSignedArmsStaysSigned) {
  run("#if (0 ? 1 : 2) > -1\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, ConditionalConvertsTheSelectedArmWhenTheOtherIsUnsigned) {
  run("#if (0 ? 1u : 2) > -1\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"NO"};
  EXPECT_EQ(spellings(), expected)
      << "the unsigned arm is the one NOT selected, so the conversion has to "
         "reach the arm that supplies the value";
}

TEST_F(PreprocessorTest, ConditionalConvertsTheTrueArmToo) {
  run("#if (1 ? 1 : 2u) > -1\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"NO"};
  EXPECT_EQ(spellings(), expected)
      << "the same rule in the other direction: a signed true arm must be "
         "converted by an unsigned false arm";
}

TEST_F(PreprocessorTest, DivisionDiffersBySignedness) {
  run("#if (-1) / 2 == 0 && (-1u) / 2 == 0x7FFFFFFFFFFFFFFF\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, RightShiftDiffersBySignedness) {
  run("#if (-1) >> 1 == -1 && (-1u) >> 1 == 0x7FFFFFFFFFFFFFFF\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected) << "signed shifts in the sign bit, unsigned shifts in zeros";
}

TEST_F(PreprocessorTest, ShiftTakesItsTypeFromTheLeftOperandOnly) {
  run("#if (-1 >> 1u) == -1\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected) << "a shift does not apply the usual arithmetic conversions; an "
                                      "unsigned count must not make the left operand unsigned";
}

TEST_F(PreprocessorTest, ComparisonsYieldASignedResult) {
  run("#if (1u < 2u) - 2 < 0\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected)
      << "a comparison has type int; if it stayed unsigned, 1 - 2 would wrap";
}

TEST_F(PreprocessorTest, ALargeHexConstantIsUnsignedWithoutASuffix) {
  run("#if 0xFFFFFFFFFFFFFFFF > 0\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, ALargeHexConstantHasTheSameBitsAsMinusOne) {
  run("#if 0xFFFFFFFFFFFFFFFF == -1\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected) << "equality compares bit patterns, so it needs no conversion";
}

TEST_F(PreprocessorTest, AllIntegerSuffixCombinationsParse) {
  run("#if 1u == 1 && 1UL == 1 && 1ULL == 1 && 1L == 1\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, UnaryOperatorsOnUnsignedValues) {
  run("#if !0u && ~0u > 0 && -1u > 0\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, UnsignedDivisionByZeroIsStillAnError) {
  run("#if 1u / 0u\nA\n#endif");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, ShortCircuitStillHoldsWithUnsignedOperands) {
  run("#if 0u && 1u / 0u\nA\n#endif");
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, CharacterConstantsHaveTheirCharacterValue) {
  run("#if 'A' == 65 && 'a' < 'b'\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, SimpleCharacterEscapesEvaluate) {
  run("#if '\\n' == 10 && '\\t' == 9 && '\\\\' == 92 && '\\'' == 39\n"
      "YES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, OctalAndHexCharacterEscapesEvaluate) {
  run("#if '\\101' == 65 && '\\x41' == 65\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, ANarrowCharacterConstantIsSigned) {
  run("#if '\\xff' == -1\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected) << "plain char is signed on this target, so 0xff is -1";
}

TEST_F(PreprocessorTest, AWideCharacterConstantIsNotNarrowed) {
  run("#if L'\\xff' == 255\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected);
}

TEST_F(PreprocessorTest, AWideOctalEscapeIsNotNarrowedEither) {
  run("#if L'\\377' == 255\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected)
      << "the octal branch needs the same wide handling as the hex branch";
}

TEST_F(PreprocessorTest, APlainHighByteCharacterConstantIsSigned) {
  run("#if '\xff' == -1\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"YES"};
  EXPECT_EQ(spellings(), expected)
      << "a raw high byte takes the non-escape path, which needs the same "
         "signed-char reading as the escape paths";
}

TEST_F(PreprocessorTest, ANulCharacterConstantIsFalse) {
  run("#if '\\0'\nYES\n#else\nNO\n#endif");
  std::vector<std::string> expected = {"NO"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, AnUnknownCharacterEscapeIsAnError) {
  run("#if '\\q'\nA\n#endif");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, AHexCharacterEscapeWithNoDigitsIsAnError) {
  run("#if '\\x'\nA\n#endif");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, WideStringsAreRejectedInAConditional) {
  run("#if L\"x\"\nA\n#endif");
  EXPECT_GT(rc, 0) << "a wide string must be rejected like a narrow one, not fall through "
                      "to the identifier branch and silently evaluate to 0";
}

TEST_F(PreprocessorTest, ErrorDirectiveIsReported) {
  run("#error bad platform\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1) << "#error must not be silently skipped";
}

TEST_F(PreprocessorTest, ErrorDirectiveKeepsStringQuotes) {
  run("#error \"bad platform\"\nint x;");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, ErrorDirectiveIsNotMacroExpanded) {
  StderrCapture capture;
  run("#define M 1\n#error M\nint x;");
  std::string text = capture.finish();

  EXPECT_EQ(rc, 1);
  EXPECT_NE(text.find("#error M"), std::string::npos)
      << "the message is the raw token sequence, not its expansion; got: " << text;
  EXPECT_EQ(text.find("#error 1"), std::string::npos)
      << "M must not be expanded to 1 in the diagnostic";
}

TEST_F(PreprocessorTest, ErrorDirectiveReportsItsMessageText) {
  StderrCapture capture;
  run("#error bad platform\nint x;");
  std::string text = capture.finish();

  EXPECT_EQ(rc, 1);
  EXPECT_NE(text.find("#error bad platform"), std::string::npos)
      << "C99 requires the diagnostic to include the token sequence; got: " << text;
}

TEST_F(PreprocessorTest, ErrorDirectiveInADeadBranchIsSilent) {
  run("#ifdef NOPE\n#error boom\n#endif\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0) << "writing #error inside a conditional is the whole point";
}

TEST_F(PreprocessorTest, PragmaIsIgnored) {
  run("#pragma once\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, AnInvalidDirectiveIsDiagnosed) {
  run("#frobnicate\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1) << "a typo like #endfi must not vanish in silence";
}

TEST_F(PreprocessorTest, AnInvalidDirectiveInASkippedGroupIsIgnored) {
  run("#ifdef NOPE\n#frobnicate\n#endif\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0) << "only group-structure directives are processed while skipping";
}

TEST_F(PreprocessorTest, TheNullDirectiveIsStillAccepted) {
  run("#\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, PragmaOperatorProducesNoTokens) {
  run("int a; _Pragma(\"x\") int b;");
  std::vector<std::string> expected = {"int", "a", ";", "int", "b", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, PragmaOperatorWorksInsideAMacroBody) {
  run("#define P _Pragma(\"x\") int\nP b;");
  std::vector<std::string> expected = {"int", "b", ";"};
  EXPECT_EQ(spellings(), expected)
      << "_Pragma is an operator, so it must work wherever a token can appear";
}

TEST_F(PreprocessorTest, ABarePragmaIdentifierIsNotTheOperator) {
  run("int _Pragma;");
  std::vector<std::string> expected = {"int", "_Pragma", ";"};
  EXPECT_EQ(spellings(), expected)
      << "without a following ( it is an ordinary identifier and the caller "
         "still owns the token";
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, PragmaOperatorWithoutAStringIsAnError) {
  run("_Pragma(1)\nint x;");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, LineDirectiveMovesTheReportedLine) {
  run("#line 100\n__LINE__");
  std::vector<std::string> expected = {"100"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, LineDirectiveAppliesToEveryFollowingLine) {
  run("#line 10\n__LINE__\n__LINE__");
  std::vector<std::string> expected = {"10", "11"};
  EXPECT_EQ(spellings(), expected) << "the offset shifts all later lines, it is not a one-shot";
}

TEST_F(PreprocessorTest, LineDirectiveCanAlsoSetTheFileName) {
  run("#line 100 \"gen.c\"\n__LINE__ __FILE__");
  std::vector<std::string> expected = {"100", "gen.c"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, LineOperandIsMacroExpanded) {
  run("#define N 42\n#line N\n__LINE__");
  std::vector<std::string> expected = {"42"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, LineOperandCanBeLineItself) {
  run("int a;\nint b;\n#line __LINE__\n__LINE__");
  std::vector<std::string> expected = {"int", "a", ";", "int", "b", ";", "3"};
  EXPECT_EQ(spellings(), expected)
      << "the operand expands to the directive's own line, so the following "
         "line keeps its physical number";
  EXPECT_EQ(rc, 0);
}

TEST_F(PreprocessorTest, LineWithoutANumberIsAnError) {
  run("#line abc\nint x;");
  std::vector<std::string> expected = {"int", "x", ";"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, LineRejectsAnythingButPlainDigits) {
  run("#line 0x10\nint x;");
  EXPECT_EQ(rc, 1) << "C99 requires a plain digit sequence";
}

TEST_F(PreprocessorTest, LineRejectsZero) {
  run("#line 0\nint x;");
  EXPECT_EQ(rc, 1);
}

TEST_F(PreprocessorTest, LineRejectsASuffixedNumber) {
  run("#line 5L\n__LINE__");
  EXPECT_EQ(rc, 1) << "strtol would happily read 5 and stop at L, so only a per-character "
                      "digit check catches this";
}

TEST_F(PreprocessorTest, TheFileNameFormOfLineCostsTheExpectedAllocations) {
  token_buf plain;
  tb_free_calls = 0;
  pp_run_ex(&plain, "#line 1\nint x;", "demo.c", nullptr, 0);
  int without_name = tb_free_calls;
  token_buf_free(&plain);

  token_buf named;
  tb_free_calls = 0;
  pp_run_ex(&named, "#line 1 \"gen.c\"\nint x;", "demo.c", nullptr, 0);
  int with_name = tb_free_calls;
  token_buf_free(&named);

  EXPECT_EQ(with_name - without_name, 5)
      << "the string token costs three value-frees because expand_token_list "
         "clones it once into the expansion frame and once into the output, "
         "plus the lexer's scratch buffer and the presumed_name strdup";
}

TEST_F(PreprocessorTest, LineInADeadBranchHasNoEffect) {
  run("#ifdef NOPE\n#line 900\n#endif\n__LINE__");
  std::vector<std::string> expected = {"4"};
  EXPECT_EQ(spellings(), expected);
  EXPECT_EQ(rc, 0);
}

TEST_F(IncludeTest, LineDirectiveInAHeaderDoesNotLeakToTheIncluder) {
  write_file("pp_test_inc/lined.h", "#line 500 \"virtual.c\"\n__LINE__ __FILE__\n");
  run("#include \"lined.h\"\n__LINE__ __FILE__");
  std::vector<std::string> expected = {"500", "virtual.c", "2", "pp_test_src/main.c"};
  EXPECT_EQ(spellings(), expected)
      << "the offset and presumed name belong to the frame, so they must be "
         "gone once it pops";
  remove("pp_test_inc/lined.h");
}

TEST_F(IncludeTest, PresumedNamesDoNotLeakAcrossManyFrames) {
  write_file("pp_test_inc/named.h", "#line 1 \"virtual.h\"\nint v;\n");
  static const char *dirs[] = {"pp_test_inc"};

  std::string one = "#include \"named.h\"\n";
  std::string many;
  for (int i = 0; i < 51; i++)
    many += "#include \"named.h\"\n";

  token_buf a;
  tb_free_calls = 0;
  pp_run_ex(&a, one.c_str(), "pp_test_src/main.c", dirs, 1);
  int frees_one = tb_free_calls;
  token_buf_free(&a);

  token_buf b;
  tb_free_calls = 0;
  pp_run_ex(&b, many.c_str(), "pp_test_src/main.c", dirs, 1);
  int frees_many = tb_free_calls;
  token_buf_free(&b);

  remove("pp_test_inc/named.h");

  EXPECT_EQ((frees_many - frees_one) % 50, 0)
      << "each extra include must cost the same fixed amount";
  EXPECT_EQ(frees_many - frees_one, 50 * 33)
      << "one of the 33 frees per included frame is the presumed_name that "
         "source_pop releases; 50*32 means it is leaked. A delta test on a "
         "single frame cannot see this, because free(NULL) is still a call";
}

TEST_F(IncludeTest, LineDirectiveDoesNotAffectIncludeResolution) {
  run("#line 1 \"nowhere/fake.c\"\n#include \"simple.h\"");
  std::vector<std::string> expected = {"int", "from_inc", ";"};
  EXPECT_EQ(spellings(), expected)
      << "a presumed name is a fiction for reporting; includes resolve "
         "against the real path on disk";
  EXPECT_EQ(rc, 0);
}
