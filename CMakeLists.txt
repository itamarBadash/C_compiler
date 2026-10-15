cmake_minimum_required(VERSION 4.0)
# הוספנו CXX כדי ש-CMake יטעין גם את הקומפיילר של C++ עבור הטסטים
project(C_compiler C CXX)

set(CMAKE_C_STANDARD 11)
# מגדירים גם סטנדרט ל-C++ כדי שסביבת GoogleTest תעבוד חלק
set(CMAKE_CXX_STANDARD 14)

include_directories(include)

set(FRONT_END src/preprocessor.c src/lexer.c src/parser.c src/ast.c src/sema.c src/symbol_table.c
        src/token_buf.c src/target.c)

# 1. קובץ ההרצה המקורי שלנו - רק קבצי המקור, נקי לחלוטין מטסטים!
add_executable(C_compiler src/main.c src/driver.c src/host.c src/codegen.c ${FRONT_END})

set(TOOLCHAIN_DIR "${CMAKE_BINARY_DIR}/toolchain")
set(WINDOWS_TARGET_DIR "${TOOLCHAIN_DIR}/targets/x86_64-w64-windows-gnu")
set(LINUX_TARGET_DIR "${TOOLCHAIN_DIR}/targets/x86_64-pc-linux-gnu")
get_filename_component(MINGW_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(MINGW_DIR "${MINGW_DIR}" DIRECTORY)

if(NOT EXISTS "${WINDOWS_TARGET_DIR}/lib/crt2.o")
    file(MAKE_DIRECTORY "${TOOLCHAIN_DIR}/bin" "${WINDOWS_TARGET_DIR}/lib")
    foreach(tool clang ld.lld)
        file(CREATE_LINK "C:/Program Files/LLVM/bin/${tool}.exe" "${TOOLCHAIN_DIR}/bin/${tool}.exe"
                COPY_ON_ERROR)
    endforeach()
    file(GLOB MINGW_RUNTIME "${MINGW_DIR}/x86_64-w64-mingw32/lib/*.a"
            "${MINGW_DIR}/x86_64-w64-mingw32/lib/*.o")
    file(COPY ${MINGW_RUNTIME} DESTINATION "${WINDOWS_TARGET_DIR}/lib")
    file(GLOB GCC_RUNTIME "${MINGW_DIR}/lib/gcc/x86_64-w64-mingw32/*/crtbegin.o"
            "${MINGW_DIR}/lib/gcc/x86_64-w64-mingw32/*/crtend.o"
            "${MINGW_DIR}/lib/gcc/x86_64-w64-mingw32/*/libgcc.a"
            "${MINGW_DIR}/lib/gcc/x86_64-w64-mingw32/*/libgcc_eh.a")
    file(COPY ${GCC_RUNTIME} DESTINATION "${WINDOWS_TARGET_DIR}/lib")
endif()

if(NOT EXISTS "${LINUX_TARGET_DIR}/lib/crtn.o")
    file(MAKE_DIRECTORY "${LINUX_TARGET_DIR}/lib")
    execute_process(COMMAND wsl wslpath -u "${LINUX_TARGET_DIR}/lib"
            OUTPUT_VARIABLE LINUX_LIB OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND wsl sh -c "cd /usr/lib/x86_64-linux-gnu && cp Scrt1.o crti.o crtn.o libc_nonshared.a libpthread.a libdl.a librt.a '${LINUX_LIB}' && cp libc.so.6 '${LINUX_LIB}/libc.so' && cp libm.so.6 '${LINUX_LIB}/libm.so' && cp -L /lib64/ld-linux-x86-64.so.2 '${LINUX_LIB}' && cd $(dirname $(gcc -print-libgcc-file-name)) && cp crtbeginS.o crtendS.o libgcc.a '${LINUX_LIB}'"
            COMMAND_ERROR_IS_FATAL ANY)
endif()

add_custom_target(toolchain_headers
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/lib/include"
        "${TOOLCHAIN_DIR}/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/tests/sysroot/windows/include"
        "${WINDOWS_TARGET_DIR}/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/tests/sysroot/linux/include"
        "${LINUX_TARGET_DIR}/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/tests/sysroot/linux/include/x86_64-linux-gnu" "${LINUX_TARGET_DIR}/include"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${LINUX_TARGET_DIR}/include/x86_64-linux-gnu")
add_dependencies(C_compiler toolchain_headers)
add_custom_command(TARGET C_compiler POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:C_compiler>" "${TOOLCHAIN_DIR}/bin/")

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

add_executable(AstTests tests/test_ast.cpp src/ast.c src/parser.c src/lexer.c src/token_buf.c
        src/target.c)
target_link_libraries(AstTests gtest_main)
target_link_options(AstTests PRIVATE -Wl,--wrap=free)
gtest_discover_tests(AstTests)

add_executable(PreprocessorTests tests/test_preprocessor.cpp src/token_buf.c src/lexer.c
        src/preprocessor.c src/target.c)
target_link_libraries(PreprocessorTests gtest_main)
target_link_options(PreprocessorTests PRIVATE -Wl,--wrap=free,--wrap=realloc)
gtest_discover_tests(PreprocessorTests)

add_executable(SemaTests tests/test_sema.cpp ${FRONT_END})
target_link_libraries(SemaTests gtest_main)
gtest_discover_tests(SemaTests)

add_executable(TargetTests tests/test_target.cpp src/target.c)
target_link_libraries(TargetTests gtest_main)
gtest_discover_tests(TargetTests)

add_executable(HeaderTests tests/test_headers.cpp ${FRONT_END})
target_compile_definitions(HeaderTests PRIVATE
        LIB_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/lib/include"
        SYSROOT_DIR="${CMAKE_SOURCE_DIR}/tests/sysroot"
        HEADER_PROBES_DIR="${CMAKE_SOURCE_DIR}/tests/headers")
target_link_libraries(HeaderTests gtest_main)
gtest_discover_tests(HeaderTests)

add_executable(CodegenTests tests/test_codegen.cpp src/codegen.c ${FRONT_END})
target_compile_definitions(CodegenTests PRIVATE
        CLANG_PATH="C:/Program Files/LLVM/bin/clang.exe"
        GCC_PATH="${CMAKE_C_COMPILER}"
        CODEGEN_PROGRAMS_DIR="${CMAKE_SOURCE_DIR}/tests/codegen"
        LIB_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/lib/include"
        SYSROOT_DIR="${CMAKE_SOURCE_DIR}/tests/sysroot")
target_link_libraries(CodegenTests gtest_main)
gtest_discover_tests(CodegenTests)

add_executable(DriverTests tests/test_driver.cpp src/driver.c src/host.c src/codegen.c ${FRONT_END})
target_compile_definitions(DriverTests PRIVATE
        TOOLCHAIN_DIR="${TOOLCHAIN_DIR}"
        DRIVER_PROGRAMS_DIR="${CMAKE_SOURCE_DIR}/tests/driver")
target_link_libraries(DriverTests gtest_main)
add_dependencies(DriverTests C_compiler)
gtest_discover_tests(DriverTests)
