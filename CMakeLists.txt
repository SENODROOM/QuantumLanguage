cmake_minimum_required(VERSION 3.16)
project(Quantum CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(MSVC)
    add_compile_options(/O2 /W3)
else()
    add_compile_options(-O2 -Wall -Wextra -Wno-unused-parameter)
    # Static-link the MinGW/GCC runtime so the .exe works without any DLLs
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

# ── Collect sources ────────────────────────────────────────────────────────────
file(GLOB_RECURSE QUANTUM_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(FILTER QUANTUM_SOURCES EXCLUDE REGEX ".*main_vm\\.cpp$")
# qpm is a standalone tool (its own main(), no VM/lexer/parser/compiler needed)
# built from src/qpm/ separately — keep it out of the other 3 targets' sources.
list(FILTER QUANTUM_SOURCES EXCLUDE REGEX ".*/src/qpm/.*\\.cpp$")
# ...except its self-contained manifest readers: the batch test runner
# (src/cli/TestRunner.cpp) looks for package.qon / package.json / requirements.txt.
list(APPEND QUANTUM_SOURCES
    "${CMAKE_SOURCE_DIR}/src/qpm/QpmJson.cpp"
    "${CMAKE_SOURCE_DIR}/src/qpm/QpmManifest.cpp"
    "${CMAKE_SOURCE_DIR}/src/qpm/QpmSemver.cpp")

# ── Java standard library: src/java/prelude/**/*.jpl, embedded as C++ ────────
file(GLOB_RECURSE JAVA_PRELUDE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/java/prelude/*.jpl")
set(JAVA_PRELUDE_CPP "${CMAKE_BINARY_DIR}/generated/JavaPreludeData.cpp")
add_custom_command(
    OUTPUT ${JAVA_PRELUDE_CPP}
    COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_SOURCE_DIR}/src/java/prelude -DOUT=${JAVA_PRELUDE_CPP}
            -P ${CMAKE_SOURCE_DIR}/cmake/EmbedJavaPrelude.cmake
    DEPENDS ${JAVA_PRELUDE_SOURCES} ${CMAKE_SOURCE_DIR}/cmake/EmbedJavaPrelude.cmake
    COMMENT "Embedding the Java prelude")
# one target owns the generation, so parallel builds of the executables share it
add_custom_target(java_prelude DEPENDS ${JAVA_PRELUDE_CPP})
list(APPEND QUANTUM_SOURCES ${JAVA_PRELUDE_CPP})

# ── The compiler, written in Quantum (selfhost/), embedded as bytecode ────────
# selfhost/compiler.qbc is the compiler compiled by itself; regenerate it
# with `qrun --emit-qbc selfhost/compiler.sa selfhost/compiler.qbc` after
# changing selfhost/*.sa (see selfhost/README.md).
set(HOSTED_COMPILER_QBC "${CMAKE_SOURCE_DIR}/selfhost/compiler.qbc")
set(HOSTED_COMPILER_CPP "${CMAKE_BINARY_DIR}/generated/HostedCompilerData.cpp")
add_custom_command(
    OUTPUT ${HOSTED_COMPILER_CPP}
    COMMAND ${CMAKE_COMMAND} -DIN=${HOSTED_COMPILER_QBC} -DOUT=${HOSTED_COMPILER_CPP}
            -P ${CMAKE_SOURCE_DIR}/cmake/EmbedBytecode.cmake
    DEPENDS ${HOSTED_COMPILER_QBC} ${CMAKE_SOURCE_DIR}/cmake/EmbedBytecode.cmake
    COMMENT "Embedding the self-hosted compiler")
add_custom_target(hosted_compiler DEPENDS ${HOSTED_COMPILER_CPP})
list(APPEND QUANTUM_SOURCES ${HOSTED_COMPILER_CPP})

# ── quantum  — compiler + bundler ─────────────────────────────────────────────
add_executable(quantum ${QUANTUM_SOURCES})
add_dependencies(quantum java_prelude hosted_compiler)
target_compile_definitions(quantum PRIVATE QUANTUM_MODE_COMPILER=1)
if(WIN32)
    target_link_libraries(quantum advapi32)
endif()

# ── qrun  — pure interpreter ──────────────────────────────────────────────────
add_executable(qrun ${QUANTUM_SOURCES})
add_dependencies(qrun java_prelude hosted_compiler)
target_compile_definitions(qrun PRIVATE QRUN_MODE=1)
if(WIN32)
    target_link_libraries(qrun advapi32)
endif()

# ── quantum_stub  — standalone runtime template ───────────────────────────────
add_executable(quantum_stub ${QUANTUM_SOURCES})
add_dependencies(quantum_stub java_prelude hosted_compiler)
if(WIN32)
    target_link_libraries(quantum_stub advapi32)
endif()

# ── qpm  — standalone npm-compatible package manager ──────────────────────────
file(GLOB QPM_SOURCES CONFIGURE_DEPENDS "src/qpm/*.cpp")
add_executable(qpm ${QPM_SOURCES})
if(WIN32)
    # z: zlib (statically linked via the -static link flag above, same as the
    #    other 3 targets already get a DLL-free libstdc++/libgcc).
    # winhttp/crypt32/ws2_32/shell32: Windows system DLLs, present on every
    #    install — linked the same way `advapi32` already is above.
    target_link_libraries(qpm z winhttp crypt32 ws2_32 shell32)
endif()

message(STATUS "Quantum v2.0.0 — Bytecode VM  (static linking enabled)")
message(STATUS "  Binaries : quantum  qrun  quantum_stub  qpm")