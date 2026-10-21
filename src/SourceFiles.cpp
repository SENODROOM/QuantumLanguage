#include "SourceFiles.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace fs = std::filesystem;

std::string fileExtLower(const std::string &path)
{
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

bool hasSupportedExt(const std::string &path)
{
    std::string ext = fileExtLower(path);
    return ext == ".sa" || ext == ".js" || ext == ".py" || ext == ".rb" || ext == ".c" || ext == ".cpp" ||
           ext == ".java";
}

namespace
{
bool isHeaderExt(const fs::path &p)
{
    std::string ext = fileExtLower(p.string());
    return ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".h++";
}

// A folder that holds a whole project: include lookups stop climbing here.
bool isProjectRoot(const fs::path &dir)
{
    std::error_code ec;
    for (const char *marker : {"CMakeLists.txt", "Makefile", "makefile", "package.json", "package.qon", ".git"})
        if (fs::exists(dir / marker, ec))
            return true;
    return false;
}
} // namespace

// Next to the including file, then (for the usual include/ + src/ layout,
// `#include "minidb/value.hpp"` from tests/) in each folder above it and
// that folder's include/, up to the project root. selfhost/dialect_cpp.sa
// (cdFindInclude) looks up includes the same way.
fs::path cppResolveInclude(const std::string &name, const fs::path &from)
{
    std::error_code ec;
    const fs::path rel = fs::u8path(name);
    fs::path dir = fs::absolute(from, ec).parent_path();
    if (fs::is_regular_file(dir / rel, ec))
        return dir / rel;
    for (int up = 0; up < 8 && !dir.empty(); ++up)
    {
        if (up > 0 && fs::is_regular_file(dir / rel, ec))
            return dir / rel;
        if (fs::is_regular_file(dir / "include" / rel, ec))
            return dir / "include" / rel;
        fs::path parent = dir.parent_path();
        if (isProjectRoot(dir) || parent == dir)
            break;
        dir = parent;
    }
    return {};
}

// Next to the header (local.h -> local.cpp), or for include/<lib>/name.hpp
// in the project's src/ (src/name.cpp, src/<lib>/name.cpp).
fs::path cppImplementationOf(const fs::path &header)
{
    if (!isHeaderExt(header))
        return {};
    static const char *const exts[] = {".sa", ".cpp", ".cc", ".cxx", ".c"};
    std::error_code ec;
    for (const char *ext : exts)
    {
        fs::path impl = header;
        impl.replace_extension(ext);
        if (fs::is_regular_file(impl, ec))
            return impl;
    }
    // Climb to the project root, remembering the path below an include/ folder.
    const fs::path abs = fs::absolute(header, ec);
    fs::path dir = abs.parent_path(), root, belowInclude;
    for (int up = 0; up < 8 && !dir.empty(); ++up)
    {
        if (belowInclude.empty() && dir.filename() == "include")
            belowInclude = abs.lexically_relative(dir);
        fs::path parent = dir.parent_path();
        if (isProjectRoot(dir) || parent == dir)
        {
            root = dir;
            break;
        }
        dir = parent;
    }
    if (root.empty() || belowInclude.empty())
        return {};
    std::vector<fs::path> stems = {belowInclude, belowInclude.filename()};
    for (const char *base : {"src", "source", "lib", ""})
        for (fs::path stem : stems)
            for (const char *ext : exts)
            {
                fs::path impl = (*base ? root / base : root) / stem.replace_extension(ext);
                if (fs::is_regular_file(impl, ec))
                    return impl;
            }
    return {};
}
