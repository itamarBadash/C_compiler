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
set(COMPILER_SOURCES src/main.c src/driver.c src/host.c src/codegen.c ${FRONT_END})
add_executable(MMC99 ${COMPILER_SOURCES})
target_compile_options(MMC99 PRIVATE -O2)

file(STRINGS "${CMAKE_SOURCE_DIR}/include/driver.h" MMC99_VERSION REGEX "#define MMC99_VERSION ")
string(REGEX REPLACE ".*\"(.*)\".*" "\\1" MMC99_VERSION "${MMC99_VERSION}")

set(LLVM_INSTALL_DIR "C:/Program Files/LLVM")
get_filename_component(MINGW_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(MINGW_DIR "${MINGW_DIR}" DIRECTORY)
set(TOOLCHAIN_DIR "${CMAKE_BINARY_DIR}/MMC99")
set(WINDOWS_TARGET_DIR "${TOOLCHAIN_DIR}/targets/x86_64-w64-windows-gnu")
set(LINUX_TARGET_DIR "${TOOLCHAIN_DIR}/targets/x86_64-pc-linux-gnu")
set(TOOLCHAIN_STAMP "${CMAKE_BINARY_DIR}/MMC99-${MMC99_VERSION}-layout-1.stamp")
set(LINUX_VERSIONS "${CMAKE_BINARY_DIR}/linux-target-versions.txt")
set(PACKAGE_ZIP "${CMAKE_BINARY_DIR}/MMC99-${MMC99_VERSION}-windows-x86_64.zip")
set(LINUX_PACKAGE "${CMAKE_BINARY_DIR}/MMC99-${MMC99_VERSION}-linux-x86_64.tar.gz")
foreach(dir SOURCE BINARY)
    execute_process(COMMAND wsl wslpath -u "${CMAKE_${dir}_DIR}" OUTPUT_VARIABLE WSL_${dir}_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
endforeach()

if(NOT EXISTS "${TOOLCHAIN_STAMP}")
    file(REMOVE_RECURSE "${TOOLCHAIN_DIR}")
    file(MAKE_DIRECTORY "${TOOLCHAIN_DIR}/bin" "${WINDOWS_TARGET_DIR}/lib")
    foreach(tool clang ld.lld)
        file(CREATE_LINK "${LLVM_INSTALL_DIR}/bin/${tool}.exe" "${TOOLCHAIN_DIR}/bin/${tool}.exe"
                COPY_ON_ERROR)
    endforeach()
    file(COPY "${MINGW_DIR}/x86_64-w64-mingw32/include/" DESTINATION "${WINDOWS_TARGET_DIR}/include"
            PATTERN "zlib.h" EXCLUDE PATTERN "zconf.h" EXCLUDE)
    file(GLOB MINGW_RUNTIME "${MINGW_DIR}/x86_64-w64-mingw32/lib/*.a"
            "${MINGW_DIR}/x86_64-w64-mingw32/lib/*.o")
    list(FILTER MINGW_RUNTIME EXCLUDE REGEX "/lib(z|iconv)\\.a$")
    file(COPY ${MINGW_RUNTIME} DESTINATION "${WINDOWS_TARGET_DIR}/lib")
    file(GLOB GCC_RUNTIME "${MINGW_DIR}/lib/gcc/x86_64-w64-mingw32/*/crtbegin.o"
            "${MINGW_DIR}/lib/gcc/x86_64-w64-mingw32/*/crtend.o"
            "${MINGW_DIR}/lib/gcc/x86_64-w64-mingw32/*/libgcc.a"
            "${MINGW_DIR}/lib/gcc/x86_64-w64-mingw32/*/libgcc_eh.a")
    file(COPY ${GCC_RUNTIME} DESTINATION "${WINDOWS_TARGET_DIR}/lib")
    file(COPY "${MINGW_DIR}/licenses/crt" "${MINGW_DIR}/licenses/headers"
            "${MINGW_DIR}/licenses/winpthreads" "${MINGW_DIR}/licenses/manifest"
            DESTINATION "${TOOLCHAIN_DIR}/licenses/MinGW-w64")
    file(COPY "${MINGW_DIR}/licenses/gcc/COPYING3" "${MINGW_DIR}/licenses/gcc/COPYING.RUNTIME"
            DESTINATION "${TOOLCHAIN_DIR}/licenses/GCC")
    file(COPY "${CMAKE_SOURCE_DIR}/packaging/licenses/LLVM" DESTINATION "${TOOLCHAIN_DIR}/licenses")
    execute_process(COMMAND wsl sh "${WSL_SOURCE_DIR}/packaging/linux-target.sh"
            "${WSL_BINARY_DIR}/MMC99/targets/x86_64-pc-linux-gnu" "${WSL_BINARY_DIR}/MMC99/licenses"
            "${WSL_BINARY_DIR}/linux-target-versions.txt" COMMAND_ERROR_IS_FATAL ANY)
    file(TOUCH "${TOOLCHAIN_STAMP}")
endif()

execute_process(COMMAND "${LLVM_INSTALL_DIR}/bin/clang.exe" --version
        OUTPUT_VARIABLE LLVM_VERSION COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "clang version ([0-9.]+)" LLVM_VERSION "${LLVM_VERSION}")
set(LLVM_VERSION "${CMAKE_MATCH_1}")
file(STRINGS "${MINGW_DIR}/x86_64-w64-mingw32/include/_mingw_mac.h" MINGW_VERSION
        REGEX "#define __MINGW64_VERSION_(MAJOR|MINOR|BUGFIX) ")
string(REGEX REPLACE "[^;]* ([0-9]+);[^;]* ([0-9]+);[^;]* ([0-9]+)" "\\1.\\2.\\3" MINGW_VERSION
        "${MINGW_VERSION}")
set(GCC_VERSION "${CMAKE_C_COMPILER_VERSION}")
file(STRINGS "${LINUX_VERSIONS}" linux_versions)
foreach(line ${linux_versions})
    string(REGEX MATCH "^([a-z_]+)=(.*)$" unused "${line}")
    string(TOUPPER "${CMAKE_MATCH_1}" name)
    set(${name} "${CMAKE_MATCH_2}")
endforeach()
set(GLIBC_VERSION "${GLIBC}")
set(LINUX_VERSION "${LINUX_LIBC_DEV}")
set(LIBGCC_VERSION "${LIBGCC}")

set(COMPILER_FILE "bin/MMC99.exe")
set(LLVM_FILES "bin/clang.exe, bin/ld.lld.exe")
set(LLVM_ORIGIN "LLVM's Windows installer, unmodified")
configure_file("${CMAKE_SOURCE_DIR}/packaging/README.txt.in" "${TOOLCHAIN_DIR}/README.txt" @ONLY)
configure_file("${CMAKE_SOURCE_DIR}/packaging/SOURCES.txt.in" "${TOOLCHAIN_DIR}/SOURCES.txt" @ONLY)
configure_file("${CMAKE_SOURCE_DIR}/CONFORMANCE.md" "${TOOLCHAIN_DIR}/CONFORMANCE.md" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/LICENSE" "${TOOLCHAIN_DIR}/licenses/MMC99/LICENSE" COPYONLY)

add_custom_target(toolchain_headers
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/lib/include"
        "${TOOLCHAIN_DIR}/include")
add_dependencies(MMC99 toolchain_headers)
add_custom_command(TARGET MMC99 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:MMC99>" "${TOOLCHAIN_DIR}/bin/")

file(GLOB OWN_HEADERS "${CMAKE_SOURCE_DIR}/lib/include/*.h")
add_custom_command(OUTPUT "${PACKAGE_ZIP}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/lib/include"
        "${TOOLCHAIN_DIR}/include"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:MMC99>" "${TOOLCHAIN_DIR}/bin/"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${PACKAGE_ZIP}"
        COMMAND ${CMAKE_COMMAND} -E tar cf "${PACKAGE_ZIP}" --format=zip MMC99
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        DEPENDS MMC99 ${OWN_HEADERS} "${TOOLCHAIN_STAMP}" "${TOOLCHAIN_DIR}/README.txt"
        "${TOOLCHAIN_DIR}/SOURCES.txt" "${TOOLCHAIN_DIR}/CONFORMANCE.md"
        "${TOOLCHAIN_DIR}/licenses/MMC99/LICENSE"
        VERBATIM)
add_custom_target(dist ALL DEPENDS "${PACKAGE_ZIP}")

set(LINUX_LLVM_DIR "${CMAKE_BINARY_DIR}/llvm-${LLVM_VERSION}-linux" CACHE PATH
        "Linux clang and ld.lld, made by packaging/fetch_linux_llvm.py")
if(EXISTS "${LINUX_LLVM_DIR}/bin/clang" AND EXISTS "${LINUX_LLVM_DIR}/bin/ld.lld")
    file(STRINGS "${LINUX_LLVM_DIR}/VERSION" LINUX_LLVM_VERSION)
    if(NOT LINUX_LLVM_VERSION STREQUAL LLVM_VERSION)
        message(FATAL_ERROR "${LINUX_LLVM_DIR} holds LLVM ${LINUX_LLVM_VERSION}, but "
                "${LLVM_INSTALL_DIR} is LLVM ${LLVM_VERSION}: both downloads must ship one version")
    endif()
    set(LINUX_PACKAGE_DIR "${CMAKE_BINARY_DIR}/linux-package")
    set(COMPILER_FILE "bin/MMC99")
    set(LLVM_FILES "bin/clang, bin/ld.lld")
    string(REGEX MATCH "^[0-9]+" LLVM_MAJOR "${LLVM_VERSION}")
    set(LLVM_ORIGIN "LLVM's Linux release archive LLVM-${LLVM_VERSION}-Linux-X64.tar.zst:
    clang-${LLVM_MAJOR} and lld, renamed to clang and ld.lld and stripped of their symbol tables")
    configure_file("${CMAKE_SOURCE_DIR}/packaging/README-linux.txt.in"
            "${LINUX_PACKAGE_DIR}/README.txt" @ONLY)
    configure_file("${CMAKE_SOURCE_DIR}/packaging/SOURCES.txt.in" "${LINUX_PACKAGE_DIR}/SOURCES.txt"
            @ONLY)
    execute_process(COMMAND wsl wslpath -u "${LINUX_LLVM_DIR}" OUTPUT_VARIABLE WSL_LINUX_LLVM_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    set(WSL_COMPILER_SOURCES ${COMPILER_SOURCES})
    list(TRANSFORM WSL_COMPILER_SOURCES PREPEND "${WSL_SOURCE_DIR}/")
    file(GLOB COMPILER_HEADERS "${CMAKE_SOURCE_DIR}/include/*.h")
    add_custom_command(OUTPUT "${LINUX_PACKAGE_DIR}/MMC99"
            COMMAND wsl gcc -std=gnu11 -O2 -Wall -Wextra -I${WSL_SOURCE_DIR}/include
            ${WSL_COMPILER_SOURCES} -o ${WSL_BINARY_DIR}/linux-package/MMC99
            DEPENDS ${COMPILER_SOURCES} ${COMPILER_HEADERS}
            VERBATIM)
    add_custom_command(OUTPUT "${LINUX_PACKAGE}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/lib/include"
            "${TOOLCHAIN_DIR}/include"
            COMMAND wsl sh ${WSL_SOURCE_DIR}/packaging/linux-package.sh ${WSL_BINARY_DIR}/MMC99
            ${WSL_BINARY_DIR}/linux-package/MMC99 ${WSL_LINUX_LLVM_DIR}/bin
            ${WSL_BINARY_DIR}/linux-package/README.txt ${WSL_BINARY_DIR}/linux-package/SOURCES.txt
            ${WSL_BINARY_DIR}/MMC99-${MMC99_VERSION}-linux-x86_64.tar.gz
            DEPENDS "${LINUX_PACKAGE_DIR}/MMC99" ${OWN_HEADERS} "${TOOLCHAIN_STAMP}"
            "${LINUX_PACKAGE_DIR}/README.txt" "${LINUX_PACKAGE_DIR}/SOURCES.txt"
            "${TOOLCHAIN_DIR}/CONFORMANCE.md" "${TOOLCHAIN_DIR}/licenses/MMC99/LICENSE"
            "${LINUX_LLVM_DIR}/bin/clang" "${LINUX_LLVM_DIR}/bin/ld.lld"
            VERBATIM)
    add_custom_target(dist_linux ALL DEPENDS "${LINUX_PACKAGE}")
else()
    message(STATUS "The Linux download is not built: run "
            "\"python packaging/fetch_linux_llvm.py ${LINUX_LLVM_DIR}\" once, then reload CMake")
    set(LINUX_PACKAGE "")
endif()

set(RELEASE_DIR "${CMAKE_SOURCE_DIR}/release/${MMC99_VERSION}")
add_custom_target(release
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${RELEASE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RELEASE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "${PACKAGE_ZIP}" ${LINUX_PACKAGE} "${RELEASE_DIR}"
        COMMAND ${CMAKE_COMMAND} -DDIR=${RELEASE_DIR} -P "${CMAKE_SOURCE_DIR}/packaging/checksums.cmake"
        VERBATIM)
add_dependencies(release dist)
if(TARGET dist_linux)
    add_dependencies(release dist_linux)
endif()

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
add_dependencies(DriverTests MMC99)
gtest_discover_tests(DriverTests)

add_executable(ConformanceTests tests/test_conformance.cpp)
target_compile_definitions(ConformanceTests PRIVATE
        TOOLCHAIN_DIR="${TOOLCHAIN_DIR}"
        CORPUS_DIR="${CMAKE_BINARY_DIR}/corpus"
        CONFORMANCE_DIR="${CMAKE_SOURCE_DIR}/tests/conformance")
target_link_libraries(ConformanceTests gtest_main)
add_dependencies(ConformanceTests MMC99)
gtest_discover_tests(ConformanceTests)

add_executable(PackageTests tests/test_package.cpp)
target_compile_definitions(PackageTests PRIVATE
        PACKAGE_ZIP="${PACKAGE_ZIP}"
        LINUX_PACKAGE="${LINUX_PACKAGE}"
        PACKAGE_PROGRAMS_DIR="${CMAKE_SOURCE_DIR}/tests/package")
target_link_libraries(PackageTests gtest_main)
add_dependencies(PackageTests dist)
if(TARGET dist_linux)
    add_dependencies(PackageTests dist_linux)
endif()
gtest_discover_tests(PackageTests)
