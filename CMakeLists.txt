cmake_minimum_required(VERSION 4.0)
# הוספנו CXX כדי ש-CMake יטעין גם את הקומפיילר של C++ עבור הטסטים
project(C_compiler C CXX)

set(CMAKE_C_STANDARD 11)
# מגדירים גם סטנדרט ל-C++ כדי שסביבת GoogleTest תעבוד חלק
set(CMAKE_CXX_STANDARD 14)

include_directories(include)
file(GLOB SOURCES "src/*.c")

# 1. קובץ ההרצה המקורי שלנו - רק קבצי המקור, נקי לחלוטין מטסטים!
add_executable(C_compiler ${SOURCES}
        include/token.h
        include/lexer.h
        include/parser.h
        src/parser.c
        include/ast.h
        src/ast.c
        include/symbol_table.h
        src/symbol_table.c
        include/token_buf.h
        src/token_buf.c
        include/preprocessor.h
        src/preprocessor.c
        include/sema.h
        src/sema.c
        include/target.h
        src/target.c
        include/target.h
        src/target.c
)

# ==========================================
# --- תשתית הטסטים (GoogleTest) ---
# ==========================================
enable_testing()

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) # מונע התנגשויות
FetchContent_MakeAvailable(googletest)

# 2. יצירת קובץ ההרצה של הטסטים (כאן ורק כאן נמצא קובץ ה-cpp)
add_executable(LexerTests tests/test_lexer.cpp src/lexer.c src/target.c)

# 3. חיבור ספריית GoogleTest לקובץ ההרצה שלנו
target_link_libraries(LexerTests gtest_main)

# 4. חשיפת הטסטים ל-CLion כדי שנוכל להריץ אותם מהממשק הגרפי
include(GoogleTest)
gtest_discover_tests(LexerTests)

# ==========================================
# 5. טסטים עבור ה-Parser
# ==========================================
add_executable(ParserTests tests/test_parser.cpp src/lexer.c src/parser.c src/ast.c
        src/token_buf.c src/preprocessor.c src/target.c)
target_link_libraries(ParserTests gtest_main)
gtest_discover_tests(ParserTests)

# ==========================================
# 6. טסטים עבור טבלת הסמלים (Symbol Table)
# ==========================================
add_executable(SymbolTableTests tests/test_symbol_table.cpp src/symbol_table.c src/ast.c)
target_link_libraries(SymbolTableTests gtest_main)
target_link_options(SymbolTableTests PRIVATE -Wl,--wrap=free)
gtest_discover_tests(SymbolTableTests)

add_executable(AstTests tests/test_ast.cpp src/ast.c src/parser.c src/lexer.c
        src/token_buf.c src/target.c)
target_link_libraries(AstTests gtest_main)
target_link_options(AstTests PRIVATE -Wl,--wrap=free)
gtest_discover_tests(AstTests)

add_executable(PreprocessorTests tests/test_preprocessor.cpp src/token_buf.c src/lexer.c
        include/preprocessor.h
        src/preprocessor.c src/target.c)
target_link_libraries(PreprocessorTests gtest_main)
target_link_options(PreprocessorTests PRIVATE -Wl,--wrap=free,--wrap=realloc)
gtest_discover_tests(PreprocessorTests)

add_executable(SemaTests tests/test_sema.cpp src/sema.c src/symbol_table.c src/parser.c src/lexer.c
        src/ast.c src/token_buf.c src/preprocessor.c src/target.c)
target_link_libraries(SemaTests gtest_main)
gtest_discover_tests(SemaTests)

add_executable(TargetTests tests/test_target.cpp src/target.c)
target_link_libraries(TargetTests gtest_main)
gtest_discover_tests(TargetTests)

add_executable(HeaderTests tests/test_headers.cpp src/preprocessor.c src/lexer.c src/parser.c
        src/ast.c src/sema.c src/symbol_table.c src/token_buf.c src/target.c)
target_compile_definitions(HeaderTests PRIVATE
        LIB_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/lib/include"
        SYSROOT_DIR="${CMAKE_SOURCE_DIR}/tests/sysroot"
        HEADER_PROBES_DIR="${CMAKE_SOURCE_DIR}/tests/headers")
target_link_libraries(HeaderTests gtest_main)
gtest_discover_tests(HeaderTests)
