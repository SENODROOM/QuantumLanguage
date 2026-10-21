#pragma once
// The Quantum compiler written in Quantum (selfhost/compiler.sa), run from
// its bytecode, which the build embeds in the executable
// (selfhost/compiler.qbc, see cmake/EmbedBytecode.cmake).
//
// Errors surface as the exceptions the rest of the CLI reports: ParseError,
// QuantumError("LexError"), std::runtime_error for a compiler fault.

#include "AST.h"
#include <cstdint>
#include <string>
#include <vector>

namespace hosted
{
// Compiles a program's source text as read from `path` (the extension picks
// the dialect passes) to serialized bytecode. `dialect` false skips the
// dialect passes. `compiledOut`, when given, receives the text after the
// dialect passes — error reports quote its lines — even when it throws.
std::vector<uint8_t> compileSource(const std::string &source, const std::string &path, bool dialect = true,
                                   std::string *compiledOut = nullptr);

// Code generation for a program another front-end parsed (C/C++, Java).
std::vector<uint8_t> compileAst(const ASTNode &program);

// Dialect passes, lexer, parser and type warnings only (`qrun --check`).
void checkSource(const std::string &source, const std::string &path, std::string *compiledOut = nullptr);
} // namespace hosted
