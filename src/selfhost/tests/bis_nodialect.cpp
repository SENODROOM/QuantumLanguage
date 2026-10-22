// Differential test: Quantum's own C++ front-end (src/cpp/) as a guest program.
// Compiles the C++ file named on the command line to bytecode and disassembles it.
//   g++ -std=c++17 -Iinclude -x c++ stage_cpp.cpp <TUs> -o stage_cpp && stage_cpp prog.cpp
//   qrun stage_cpp.cpp prog.cpp
#define QUANTUM_SELFHOST 1
#include "../../../include/Compiler.h"
#include "../../../include/Disassembler.h"
#include "../../cpp/Cpp.h"
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: stage_cpp prog.cpp [pre|parse|full]\n";
        return 2;
    }
    const std::string mode = argc > 2 ? argv[2] : "full";
    std::ifstream in(argv[1], std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    if (mode == "pre" || mode == "parse")
    {
        cpp::Preprocessed pp = cpp::preprocess(argv[1], ss.str());
        if (mode == "pre")
        {
            for (const auto &t : pp.tokens)
                std::cout << (int)t.kind << " " << t.text << " " << t.line << " " << t.tu << "\n";
            return 0;
        }
        cpp::Program prog = cpp::parse(pp);
        std::cout << "decls " << prog.decls.size() << "\n";
        for (const auto &d : prog.decls)
        {
            std::cout << (int)d->k << " ";
            for (const auto &n : d->name)
                std::cout << n << "::";
            std::cout << " params=" << d->params.size() << " body=" << (d->body ? 1 : 0) << " line=" << d->line << "\n";
        }
        return 0;
    }
    ASTNodePtr program = cpp::compileProgram(argv[1], ss.str());
    Compiler c;
    auto chunk = c.compile(*program);
    disassembleChunk(*chunk, std::cout);
    return 0;
}

#pragma quantum tu "../../Token.sa"
#pragma quantum tu "../../Value.sa"
#pragma quantum tu "../../Disassembler.sa"
#pragma quantum tu "../../compiler/CompilerCore.sa"
#pragma quantum tu "../../compiler/CompilerExpressions.sa"
#pragma quantum tu "../../compiler/CompilerFunctions.sa"
#pragma quantum tu "../../compiler/CompilerStatements.sa"
#pragma quantum tu "../../cpp/CppExpr.sa"
#pragma quantum tu "../../cpp/CppLex.sa"
#pragma quantum tu "../../cpp/CppLib.sa"
#pragma quantum tu "../../cpp/CppLower.sa"
#pragma quantum tu "../../cpp/CppParse.sa"
#pragma quantum tu "../../lexer/LexerCore.sa"
#pragma quantum tu "../../lexer/LexerReaders.sa"
#pragma quantum tu "../../lexer/LexerTokenize.sa"
#pragma quantum tu "../../parser/ParserCore.sa"
#pragma quantum tu "../../parser/ParserExpressions.sa"
#pragma quantum tu "../../parser/ParserLiterals.sa"
#pragma quantum tu "../../parser/ParserStatements.sa"
#pragma quantum tu "VmStub.cpp"
