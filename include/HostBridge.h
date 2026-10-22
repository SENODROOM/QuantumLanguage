#pragma once
// Host services for the self-hosted compiler: the seed VM (native code) provides
// these to the compiler when the compiler itself runs as Quantum bytecode.
// The compiler sources reach them as __host_*() intrinsics (QUANTUM_SELFHOST);
// the natives are registered in src/vm/VmCpp.sa.

#include <cstdint>
#include <string>
#include <vector>

// Start `cmdline`, wait for it, return its exit code (-1 when it can't start).
int hostSpawn(const std::string &cmdline);

// Run `cmd` through the shell and return its merged stdout+stderr; exitCode = its status.
std::string hostCapture(const std::string &cmd, int &exitCode);

// Append raw bytes to a file; false on failure.
bool hostAppendBytes(const std::string &path, const std::vector<uint8_t> &bytes);

// Deserialize `chunkBytes` and run it on a fresh VM, with g_scriptArgv / g_testMode set.
int hostRunChunk(const std::vector<uint8_t> &chunkBytes, const std::vector<std::string> &argv, bool testMode);
