// Differential test: lexer + parser + bytecode compiler + disassembler.
//   g++ -std=c++17 -Iinclude -x c++ src/selfhost/tests/stage_dis.cpp <TUs below> -o stage_dis
//   qrun src/selfhost/tests/stage_dis.cpp
#define QUANTUM_SELFHOST 1
#include "../../../include/Lexer.h"
#include "../../../include/Parser.h"
#include "../../../include/Compiler.h"
#include "../../../include/Disassembler.h"
#include <iostream>

static void show(const char *src)
{
    Lexer lx(src);
    Parser p(lx.tokenize());
    auto ast = p.parse();
    Compiler c;
    auto chunk = c.compile(*ast);
    disassembleChunk(*chunk, std::cout);
}

int main()
{
    show("let x = 5\n");
    show("fn fib(n) {\n  if n < 2 { return n }\n  return fib(n-1) + fib(n-2)\n}\nprint(\"hi\")\nprint(fib(10))\n");
    show("let a = [1, 2, 3]\nlet d = {\"k\": 1}\nfor v in a { print(v) }\nprint(d[\"k\"])\n");
    return 0;
}

#pragma quantum tu "../../Token.sa"
#pragma quantum tu "../../Value.sa"
#pragma quantum tu "../../Disassembler.sa"
#pragma quantum tu "../../lexer/LexerCore.sa"
#pragma quantum tu "../../lexer/LexerReaders.sa"
#pragma quantum tu "../../lexer/LexerTokenize.sa"
#pragma quantum tu "../../parser/ParserCore.sa"
#pragma quantum tu "../../parser/ParserExpressions.sa"
#pragma quantum tu "../../parser/ParserLiterals.sa"
#pragma quantum tu "../../parser/ParserStatements.sa"
#pragma quantum tu "../../compiler/CompilerCore.sa"
#pragma quantum tu "../../compiler/CompilerExpressions.sa"
#pragma quantum tu "../../compiler/CompilerFunctions.sa"
#pragma quantum tu "../../compiler/CompilerStatements.sa"
#pragma quantum tu "VmStub.cpp"
