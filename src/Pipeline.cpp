#include "Pipeline.h"
#include "Diagnostics.h"
#include "Disassembler.h"
#include "Error.h"
#include "HostedCompiler.h"
#include "Serializer.h"
#include "SourceFiles.h"
#include "cpp/Cpp.h"
#include "java/JavaSema.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

// Shared with src/vm/VmNatives.cpp (declared extern there).
bool g_testMode = false;
std::vector<std::string> g_scriptArgv;
bool g_strictMode = false;

// ─── Strict C/C++ diagnostics ─────────────────────────────────────────────────

namespace
{
std::string readWhole(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The typed front-end's verdict on a program it rejects, phrased the way
// gcc reports the same mistake: "expected ';' near 'std'" → "expected ';'
// before 'std'", "unknown name 'y'" → "'y' was not declared in this scope".
std::string strictCppReport(const cpp::Unsupported &u, const std::string &path, const std::string &source)
{
    const bool isC = fileExtLower(path) == ".c";
    std::string file = u.file.empty() || u.file == "?" ? path : u.file;
    std::string text = file == path ? source : readWhole(file);
    std::string msg = cpp::cPlainName(u.what());
    std::string name; // the token / identifier the caret points at
    std::string severity = "error";
    std::string note;   // gcc's follow-up "note:" line, if any
    if (msg.rfind("fatal: ", 0) == 0)
    {
        msg = msg.substr(7); // "<header>: No such file or directory"
        severity = "fatal error";
        name = "<" + msg.substr(0, msg.find(':')) + ">";
        note = "compilation terminated.\n";
    }
    else if (msg.rfind("implicit declaration of function '", 0) == 0 && msg.find("; include <") != std::string::npos)
    {
        name = diag::quotedName(msg);
        std::string header = msg.substr(msg.find("; include <") + 11);
        header = header.substr(0, header.find('>'));
        msg = "implicit declaration of function '" + name + "' [-Wimplicit-function-declaration]";
        note = file + ":1:1: note: include '<" + header + ">' or provide a declaration of '" + name + "'\n";
    }
    else if (msg.rfind("parse: ", 0) == 0)
    {
        msg = msg.substr(7);
        size_t near = msg.rfind(" near '");
        if (near != std::string::npos)
        {
            name = diag::quotedName(msg.substr(near + 1));
            msg = msg.substr(0, near) + (name.empty() ? " at end of input" : " before '" + name + "'");
        }
    }
    else if (msg.rfind("unknown name '", 0) == 0 || msg.rfind("unknown function '", 0) == 0)
    {
        name = diag::quotedName(msg);
        const bool fn = msg.rfind("unknown function", 0) == 0;
        msg = isC ? (fn ? "implicit declaration of function '" + name + "' [-Wimplicit-function-declaration]"
                        : "'" + name + "' undeclared (first use in this function)")
                  : "'" + name + "' was not declared in this scope";
    }
    else if (msg.rfind("unknown type '", 0) == 0)
    {
        name = diag::quotedName(msg);
        msg = isC ? "unknown type name '" + name + "'" : "'" + name + "' does not name a type";
    }
    else if (msg.rfind("unknown std member '", 0) == 0)
    {
        name = diag::quotedName(msg);
        msg = "'" + name + "' is not a member of 'std'";
    }
    else if (msg.rfind("unsupported: ", 0) == 0)
    {
        msg = msg.substr(13);
        name = diag::quotedName(msg);
    }
    else if (msg.rfind("unsupported", 0) != 0)
        name = diag::quotedName(msg);
    int col = u.col;
    if (col <= 0 && !name.empty())
        col = diag::findWord(diag::sourceLine(text, u.line), name);
    int width = name.empty() ? 1 : (int)name.size();
    return diag::gcc(file, text, u.line, col, msg, width, severity) + note;
}
} // namespace

// ─── Compile source → bytecode ────────────────────────────────────────────────

namespace
{
// `multiSyntax` reports whether the self-hosted compiler parsed the program
// (rather than the C/C++ or Java front-end).
std::vector<uint8_t> compileToBytes(const std::string &source, const std::string &sourcePath,
                                    std::string *compiledOut, bool &multiSyntax)
{
    multiSyntax = false;
    const std::string ext = fileExtLower(sourcePath);
    // Java: its own front-end (src/java/), always strict — a program javac
    // rejects is a compile error, reported the way javac reports it.
    if (ext == ".java")
    {
        std::string text = source;
        if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
            (unsigned char)text[2] == 0xBF)
            text.erase(0, 3);
        ASTNodePtr program = java::compileProgram(sourcePath, text);
        return hosted::compileAst(*program);
    }
    // C/C++: the typed front-end (src/cpp/) when the program is within its
    // subset; otherwise the multi-syntax pipeline below.
    if ((ext == ".cpp" || ext == ".c" || ext == ".cc" || ext == ".cxx") && !std::getenv("QUANTUM_OLD_CPP"))
    {
        std::ifstream in(sourcePath, std::ios::binary);
        if (in)
        {
            std::ostringstream ss;
            ss << in.rdbuf();
            try
            {
                ASTNodePtr program = cpp::compileProgram(sourcePath, ss.str());
                return hosted::compileAst(*program);
            }
            catch (const cpp::LinkError &l)
            {
                if (g_strictMode)
                    throw CompileError(sourcePath + ": error: " + l.what() + "\n");
                throw QuantumError("LinkError", l.what());
            }
            catch (const cpp::Unsupported &u)
            {
                if (g_strictMode)
                    throw CompileError(strictCppReport(u, sourcePath, ss.str()));
                if (std::getenv("QUANTUM_DEBUG_CPPFE"))
                    std::cerr << "[cppfe] falling back: " << u.what() << " (" << (u.file.empty() ? sourcePath : u.file)
                              << ":" << u.line << ")\n";
            }
        }
        else if (g_strictMode)
            throw CompileError(sourcePath + ": error: cannot open source file\n");
    }
    // Everything else: the self-hosted compiler — dialect passes, lexer,
    // parser, @use / import resolution, type warnings, code generation.
    multiSyntax = true;
    return hosted::compileSource(source, sourcePath, true, compiledOut);
}
} // namespace

std::vector<uint8_t> compileSourceBytes(const std::string &source, const std::string &sourcePath,
                                        std::string *compiledOut)
{
    bool multiSyntax = false;
    return compileToBytes(source, sourcePath, compiledOut, multiSyntax);
}

std::shared_ptr<Chunk> compileSource(const std::string &source, const std::string &sourcePath, bool debug,
                                     std::string *compiledOut)
{
    bool multiSyntax = false;
    auto chunk = Serializer::deserialize(compileToBytes(source, sourcePath, compiledOut, multiSyntax));
    if (debug)
    {
        if (multiSyntax)
            std::cerr << Colors::CYAN << "[DEBUG] Bytecode — " << sourcePath << "\n" << Colors::RESET;
        disassembleChunk(*chunk, std::cerr);
    }
    return chunk;
}
