#pragma once
// Source-file helpers shared by the CLI and the typed C/C++ front-end.

#include <filesystem>
#include <string>

// Lower-cased file extension including the leading dot (".rb", ".sa", ...).
std::string fileExtLower(const std::string &path);

// True for a file type this compiler runs natively: .sa .js .py .rb .c .cpp .java
bool hasSupportedExt(const std::string &path);

// `#include "name"` from `from`: next to it, then include/ folders up to the
// project root; an empty path when there is no such file.
std::filesystem::path cppResolveInclude(const std::string &name, const std::filesystem::path &from);

// The implementation file of a header (x.h -> x.cpp, include/l/x.hpp ->
// src/x.cpp), or an empty path.
std::filesystem::path cppImplementationOf(const std::filesystem::path &header);
