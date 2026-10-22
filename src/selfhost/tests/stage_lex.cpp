// Differential test of one compiler stage: the lexer. The same file is built
// with g++ (reference) and run by Quantum's C++ front-end; outputs must match.
//   g++ -std=c++17 -Iinclude -x c++ src/selfhost/tests/stage_lex.cpp src/lexer/*.sa src/Token.sa -o stage_lex
//   qrun src/selfhost/tests/stage_lex.cpp
#define QUANTUM_SELFHOST 1
#include "../../../include/Lexer.h"
#include <iostream>

int main()
{
    const char *srcs[] = {
        "let x = 5\n",
        "fn fib(n) {\n  if n < 2 { return n }\n  return fib(n-1) + fib(n-2)\n}\nprint(\"hi\")\n",
        "x = [1, 2.5, 0xFF]  # comment\nprint(x[0]) /* c */ y = 'q' + \"w\"\n",
        "class A { def f(self): return 1 }\nfor i in range(3): print(i)\n",
    };
    for (const char *s : srcs)
    {
        Lexer lx(s);
        std::vector<Token> toks = lx.tokenize();
        std::cout << "tokens " << toks.size() << "\n";
        for (const Token &t : toks)
            std::cout << (int)t.type << " [" << t.value << "] " << t.line << ":" << t.col << "\n";
    }
    return 0;
}

#pragma quantum tu "../../Token.sa"
#pragma quantum tu "../../lexer/LexerCore.sa"
#pragma quantum tu "../../lexer/LexerReaders.sa"
#pragma quantum tu "../../lexer/LexerTokenize.sa"
