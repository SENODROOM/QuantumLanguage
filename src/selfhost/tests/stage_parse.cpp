// Differential test: lexer + parser. Prints the kind (variant index) of every top-level node.
#define QUANTUM_SELFHOST 1
#include "../../../include/Lexer.h"
#include "../../../include/Parser.h"
#include <iostream>

static void show(const char *src)
{
    Lexer lx(src);
    Parser p(lx.tokenize());
    auto ast = p.parse();
    std::cout << "root " << ast->node.index() << " line " << ast->line << "\n";
    if (ast->is<BlockStmt>())
    {
        auto &stmts = ast->as<BlockStmt>().statements;
        std::cout << "statements " << stmts.size() << "\n";
        for (auto &s : stmts)
            std::cout << "  node " << s->node.index() << " line " << s->line << "\n";
    }
}

int main()
{
    show("let x = 5\n");
    show("fn fib(n) {\n  if n < 2 { return n }\n  return fib(n-1) + fib(n-2)\n}\nprint(\"hi\")\nprint(fib(10))\n");
    show("let a = [1, 2, 3]\nlet d = {\"k\": 1}\nfor v in a { print(v) }\nprint(d[\"k\"])\n");
    return 0;
}

#pragma quantum tu "../../Token.sa"
#pragma quantum tu "../../lexer/LexerCore.sa"
#pragma quantum tu "../../lexer/LexerReaders.sa"
#pragma quantum tu "../../lexer/LexerTokenize.sa"
#pragma quantum tu "../../parser/ParserCore.sa"
#pragma quantum tu "../../parser/ParserExpressions.sa"
#pragma quantum tu "../../parser/ParserLiterals.sa"
#pragma quantum tu "../../parser/ParserStatements.sa"
