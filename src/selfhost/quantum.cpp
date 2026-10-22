// Self-hosting driver: the whole compiler, minus the VM, as one program for
// Quantum's own C++ front-end (src/cpp/). Not part of the g++ build.
//
//   qrun --dis src/selfhost/quantum.cpp      bytecode of the compiler
//
// The VM (src/vm/) and qpm stay native: they are the seed runtime the compiled
// program runs on. Platform services go through the __host_* intrinsics
// (see QUANTUM_SELFHOST in the sources).
#define QUANTUM_SELFHOST 1
#define QUANTUM_MODE_COMPILER 1


#include "../main.sa"

#pragma quantum tu "../Disassembler.sa"
#pragma quantum tu "../ModuleResolver.sa"
#pragma quantum tu "../Pipeline.sa"
#pragma quantum tu "../Serializer.sa"
#pragma quantum tu "../Token.sa"
#pragma quantum tu "../TypeChecker.sa"
#pragma quantum tu "../Value.sa"
#pragma quantum tu "../cli/Bundler.sa"
#pragma quantum tu "../cli/Cli.sa"
#pragma quantum tu "../cli/Repl.sa"
#pragma quantum tu "../cli/TestRunner.sa"
#pragma quantum tu "../compiler/CompilerCore.sa"
#pragma quantum tu "../compiler/CompilerExpressions.sa"
#pragma quantum tu "../compiler/CompilerFunctions.sa"
#pragma quantum tu "../compiler/CompilerStatements.sa"
#pragma quantum tu "../cpp/CppExpr.sa"
#pragma quantum tu "../cpp/CppLex.sa"
#pragma quantum tu "../cpp/CppLib.sa"
#pragma quantum tu "../cpp/CppLower.sa"
#pragma quantum tu "../cpp/CppParse.sa"
#pragma quantum tu "../dialect/CDialect.sa"
#pragma quantum tu "../dialect/CppDialect.sa"
#pragma quantum tu "../dialect/Dialect.sa"
#pragma quantum tu "../dialect/DialectScan.sa"
#pragma quantum tu "../dialect/RubyDialect.sa"
#pragma quantum tu "../dialect/RubyStdlib.sa"
#pragma quantum tu "../lexer/LexerCore.sa"
#pragma quantum tu "../lexer/LexerReaders.sa"
#pragma quantum tu "../lexer/LexerTokenize.sa"
#pragma quantum tu "../parser/ParserCore.sa"
#pragma quantum tu "../parser/ParserExpressions.sa"
#pragma quantum tu "../parser/ParserLiterals.sa"
#pragma quantum tu "../parser/ParserStatements.sa"
#pragma quantum tu "../qpm/QpmJson.sa"
#pragma quantum tu "../qpm/QpmManifest.sa"
#pragma quantum tu "../qpm/QpmSemver.sa"
#pragma quantum tu "VmHost.cpp"
