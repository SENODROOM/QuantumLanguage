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
# The compiler's translation units use the .sa extension (the language's own
# extension) but are C++17 — tell CMake so it passes -x c++ to the compiler.
file(GLOB_RECURSE QUANTUM_SOURCES CONFIGURE_DEPENDS "src/*.sa")
# qpm is a standalone tool (its own main(), no VM/lexer/parser/compiler needed)
# built from src/qpm/ separately — keep it out of the other 3 targets' sources.
list(FILTER QUANTUM_SOURCES EXCLUDE REGEX ".*/src/qpm/.*\\.sa$")
# ...except its self-contained manifest readers: `# @use` package resolution
# (src/ModuleResolver.sa) reads package.qon / package.json / requirements.txt.
list(APPEND QUANTUM_SOURCES
    "${CMAKE_SOURCE_DIR}/src/qpm/QpmJson.sa"
    "${CMAKE_SOURCE_DIR}/src/qpm/QpmManifest.sa"
    "${CMAKE_SOURCE_DIR}/src/qpm/QpmSemver.sa")
file(GLOB QPM_SOURCES CONFIGURE_DEPENDS "src/qpm/*.sa")
set_source_files_properties(${QUANTUM_SOURCES} ${QPM_SOURCES} PROPERTIES LANGUAGE CXX)
if(NOT MSVC)
    # g++ does not know .sa: without this it treats the file as linker input.
    set_source_files_properties(${QUANTUM_SOURCES} ${QPM_SOURCES} PROPERTIES COMPILE_OPTIONS "-x;c++")
endif()

# ── quantum  — compiler + bundler ─────────────────────────────────────────────
add_executable(quantum ${QUANTUM_SOURCES})
target_compile_definitions(quantum PRIVATE QUANTUM_MODE_COMPILER=1)
if(WIN32)
    target_link_libraries(quantum advapi32)
endif()

# ── qrun  — pure interpreter ──────────────────────────────────────────────────
add_executable(qrun ${QUANTUM_SOURCES})
target_compile_definitions(qrun PRIVATE QRUN_MODE=1)
if(WIN32)
    target_link_libraries(qrun advapi32)
endif()

# ── quantum_stub  — standalone runtime template ───────────────────────────────
add_executable(quantum_stub ${QUANTUM_SOURCES})
if(WIN32)
    target_link_libraries(quantum_stub advapi32)
endif()

# ── qpm  — standalone npm-compatible package manager ──────────────────────────
add_executable(qpm ${QPM_SOURCES})
if(WIN32)
    # z: zlib (statically linked via the -static link flag above)
    # winhttp/crypt32/ws2_32/shell32: Windows system DLLs, present on every install
    target_link_libraries(qpm z winhttp crypt32 ws2_32 shell32)
endif()

message(STATUS "Quantum v2.0.0 — Bytecode VM  (static linking enabled)")
message(STATUS "  Binaries : quantum  qrun  quantum_stub  qpm")
