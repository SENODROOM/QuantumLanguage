#include "ModuleResolver.h"
#include "Dialect.h"
#include "Pipeline.h"
#include "Lexer.h"
#include "Parser.h"
#include "Compiler.h"
#include "TypeChecker.h"
#include "Disassembler.h"
#include "Error.h"
#include <iostream>

// Shared with src/vm/VmNatives.cpp (declared extern there).
bool g_testMode = false;
std::vector<std::string> g_scriptArgv;

// ─── Compile source → Chunk ───────────────────────────────────────────────────

std::shared_ptr<Chunk> compileSource(const std::string &source,
                                     const std::string &sourcePath,
                                     bool debug)
{
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    const bool python = fileExtLower(sourcePath) == ".py";
    parser.setPythonSource(python);
    auto ast = parser.parse();

    // A Python program prints Python's way (True / None / 'str' in lists).
    // Done by an instruction rather than a runner flag so that a bundled
    // .exe behaves the same.
    if (python && ast->is<BlockStmt>())
    {
        CallExpr call;
        call.callee = std::make_unique<ASTNode>(Identifier{"__python_repr__"}, 0);
        auto &stmts = ast->as<BlockStmt>().statements;
        stmts.insert(stmts.begin(), std::make_unique<ASTNode>(
                                        ExprStmt{std::make_unique<ASTNode>(std::move(call), 0)}, 0));
    }

    resolveUseDirectives(*ast, source, sourcePath);
    resolveImports(*ast, sourcePath);

    try
    {
        TypeChecker tc;
        tc.check(ast);
    }
    catch (const StaticTypeError &e)
    {
        std::cerr << Colors::YELLOW << "[TypeWarning] " << Colors::RESET
                  << e.what() << " (line " << e.line << ")\n";
    }

    Compiler compiler;
    auto chunk = compiler.compile(*ast);

    if (debug)
    {
        std::cerr << Colors::CYAN << "[DEBUG] Bytecode — " << sourcePath << "\n"
                  << Colors::RESET;
        disassembleChunk(*chunk, std::cerr);
    }
    return chunk;
}

// Dialect front-end (Ruby/C/C++ source-to-source passes) lives in
// src/dialect/ and is reached through applyDialect() in Dialect.h.
