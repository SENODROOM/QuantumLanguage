// Stand-ins for the few VM symbols the compiler stages reference.
#include "../../../include/Vm.h"

std::string jsDisplayString(const std::shared_ptr<Dict> &, bool &handled)
{
    handled = false;
    return "";
}
