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
        include/lexer.h)

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
add_executable(LexerTests tests/test_lexer.cpp src/lexer.c src/lex_keywords.c)

# 3. חיבור ספריית GoogleTest לקובץ ההרצה שלנו
target_link_libraries(LexerTests gtest_main)

# 4. חשיפת הטסטים ל-CLion כדי שנוכל להריץ אותם מהממשק הגרפי
include(GoogleTest)
gtest_discover_tests(LexerTests)