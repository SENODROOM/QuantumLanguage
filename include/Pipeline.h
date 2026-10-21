#pragma once
// The shared compile path: program source -> bytecode. C/C++ within the
// typed front-end's subset (src/cpp/) and Java (src/java/) are parsed by
// their own front-ends; every other program — .sa .py .js .rb, and C/C++
// outside that subset — by the self-hosted compiler (selfhost/, run through
// src/HostedCompiler.cpp), which also generates the code for all of them.

#include "Opcode.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// `source` is the program text as read from `sourcePath` (the extension
// picks the front-end and dialect passes). `compiledOut`, when given,
// receives the text after the dialect passes, for error reports.
std::vector<uint8_t> compileSourceBytes(const std::string &source,
                                        const std::string &sourcePath = "<input>",
                                        std::string *compiledOut = nullptr);

std::shared_ptr<Chunk> compileSource(const std::string &source,
                                     const std::string &sourcePath = "<input>",
                                     bool debug = false,
                                     std::string *compiledOut = nullptr);

// Set while the batch test runner is active: makes input() return canned
// values instead of blocking on stdin. Read by src/vm/VmNatives.cpp.
extern bool g_testMode;

// --strict: a .c/.cpp file must be valid C/C++. The typed front-end's
// verdict is final — a program outside it is a compile error instead of
// falling back to the multi-syntax pipeline that accepts Python/JS/Ruby
// style code. (.java is always strict; .sa is never strict.)
extern bool g_strictMode;

// The running script's path followed by its command-line arguments
// (`qrun tool.sa --json` -> {"tool.sa", "--json"}). Set by main() before the
// VM is constructed; exposed to scripts as sys.argv by src/vm/VmNatives.cpp.
extern std::vector<std::string> g_scriptArgv;
