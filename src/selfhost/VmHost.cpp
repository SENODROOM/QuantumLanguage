// The VM, seen from the self-hosted compiler: the real VM is the seed's native
// code. run() serializes the chunk (same bytes as a bundled .exe payload) and
// the host deserializes and executes it on its own VM.
#include "../../include/Vm.h"
#include "../../include/Serializer.h"
#include "../../include/Pipeline.h"

VM::VM() {}

void VM::run(std::shared_ptr<Chunk> chunk)
{
    std::vector<uint8_t> bytes = Serializer::serialize(chunk);
    __host_run_chunk(bytes, g_scriptArgv, g_testMode);
}

// Defined in the VM (VmJsRuntime): how a tagged JS runtime dict (Date, URL)
// prints. The compiler only prints ordinary values, so nothing is "handled".
std::string jsDisplayString(const std::shared_ptr<Dict> &, bool &handled)
{
    handled = false;
    return "";
}
