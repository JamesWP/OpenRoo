/* Vec_Iterate: calls a thiscall member function on each of count elements of
 * an array, first to last.  A count of zero or less does nothing. */

#include <windows.h>

extern "C" {

typedef void (__attribute__((thiscall)) *vec_elem_fn)(void *elem);

__declspec(dllexport) void __stdcall
Vec_Iterate(void *base, int size, int count, void *fn)
{
    char *p = (char *)base;
    for (int i = 0; i < count; ++i) {
        ((vec_elem_fn)fn)(p);
        p += size;
    }
}

}
