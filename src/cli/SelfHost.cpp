// Bytecode files: `qrun --emit-qbc` writes a program's compiled bytecode
// (how selfhost/compiler.qbc, the compiler embedded in qrun, is rebuilt
// from selfhost/compiler.sa) and `qrun --exec` runs such a file.
#include "Cli.h"
#include "Error.h"
#include "HostedCompiler.h"
#include "Pipeline.h"
#include "Serializer.h"
#include "Vm.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
bool readText(const std::string &path, std::string &out)
{
    std::ifstream in(path); // text mode, as runFile reads a program
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}
} // namespace

int emitBytecodeFile(const std::string &path, const std::string &outPath, bool dialect)
{
    std::string source;
    if (!readText(path, source))
    {
        std::cerr << "[Error] Cannot open file: " << path << "\n";
        return 1;
    }
    try
    {
        auto bytes = dialect ? compileSourceBytes(source, path) : hosted::compileSource(source, path, false);
        std::ofstream out(outPath, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return out ? 0 : 1;
    }
    catch (const ParseError &e)
    {
        std::cerr << path << ":" << e.line << ": ParseError: " << e.what() << "\n";
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Error] " << e.what() << "\n";
        return 1;
    }
}

int execBytecodeFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::cerr << "[Error] Cannot open bytecode file: " << path << "\n";
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    try
    {
        auto chunk = Serializer::deserialize(bytes);
        VM vm;
        vm.unlimitedSteps();
        vm.run(chunk);
        return 0;
    }
    catch (const QuantumError &e)
    {
        std::cerr << Colors::RED << Colors::BOLD << "\n  X " << e.kind << Colors::RESET;
        if (e.line > 0)
            std::cerr << " at line " << e.line;
        std::cerr << "\n    " << e.what() << "\n\n";
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Fatal] " << e.what() << "\n";
        return 1;
    }
}
