/* resources.cpp -- see resources.h. */
#include "resources.h"

HMODULE Resources_Module()
{
    static HMODULE s_module;
    if (!s_module)
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&Resources_Module, &s_module);
    return s_module;
}
