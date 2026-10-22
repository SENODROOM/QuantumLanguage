// Differential test: what the lexer + parser make of a file (class members in detail).
//   qrun stage_ast.cpp file.sa
#define QUANTUM_SELFHOST 1
#include "../../../include/Lexer.h"
#include "../../../include/Parser.h"
#include <fstream>
#include <iostream>
#include <sstream>

static void dump(const ASTNodePtr &n, int depth)
{
    std::string pad(depth * 2, ' ');
    if (!n)
    {
        std::cout << pad << "(null)\n";
        return;
    }
    std::cout << pad << "node " << n->node.index() << " line " << n->line << "\n";
    if (n->is<BlockStmt>())
        for (auto &s : n->as<BlockStmt>().statements)
            dump(s, depth + 1);
    else if (n->is<ClassDecl>())
    {
        auto &c = n->as<ClassDecl>();
        std::cout << pad << " class " << c.name << " base=" << c.base << " methods=" << c.methods.size()
                  << " static=" << c.staticMethods.size() << " fields=" << c.fields.size() << "\n";
        for (auto &f : c.fields)
            dump(f, depth + 2);
    }
    else if (n->is<VarDecl>())
    {
        auto &v = n->as<VarDecl>();
        std::cout << pad << " var " << v.name << " hint=" << v.typeHint << " init=" << (v.initializer ? 1 : 0)
                  << " ptr=" << v.isPointer << "\n";
        if (v.initializer)
            dump(v.initializer, depth + 2);
    }
}

int main(int argc, char **argv)
{
    std::ifstream in(argv[1], std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    Lexer lx(ss.str());
    Parser p(lx.tokenize());
    dump(p.parse(), 0);
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
