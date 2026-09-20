/* VectorIterate 0x00402610 -- "run this __thiscall member over N array
 * elements", the non-throwing cousin of MSVC's vector constructor iterator.
 *
 *   00402610  MOV EAX,[ESP+0xc]; DEC EAX; JS out      -- count <= 0: nothing
 *             ... ECX = ptr; CALL fn; ptr += size; until count exhausted
 *             RET 0x10                                 -- __stdcall, 4 args
 *
 * (ptr, size, count, fn), forward order, no SEH frame and no unwind dtor --
 * which is what distinguishes it from the CRT's 0x00451db5 / 0x00451e37 pair
 * that the VoicePool and CStaticSoundbuffer work met.  Nothing here can
 * throw, so there is nothing to unwind.
 *
 * Three CALL sites, in three different TUs (0x42357d, 0x42c5ff, 0x44de91),
 * and no reference of any other kind: it is a shared helper, so stubbing it
 * is legitimate only because CALL_PATCHES covers every one of the three in
 * this same batch (CLAUDE.md's rule about shared helpers).
 *
 * It has its own file because it has no owner: it sits inside the SplinePath
 * address range without belonging to the class, which is the one thing
 * tu_map.txt cannot express as a boundary (patch.py says the same at the
 * SplinePath CALL_PATCHES block).
 *
 * All three sites push the SAME element function, 0x00424de0, as `fn`.  We
 * call it through the pointer the caller supplied -- dispatch, not a callback
 * by address, the same argument VoicePoolWipe's virtual call makes.  When
 * 0x424de0 is replaced, those three `68 e0 4d 42 00` pushes become
 * PUSH_PATCHES; until then they are the game's own function reaching its own
 * array, and nothing here needs to know that.
 *
 * The loop is the original's, including the two edge cases that are
 * semantics rather than shape: `count <= 0` does nothing at all (the DEC/JS
 * pair tests count-1 as SIGNED), and the pointer advances AFTER the call, so
 * the first element is the one passed in.
 */
#include <windows.h>

extern "C" {

typedef void (__attribute__((thiscall)) *vec_elem_fn)(void *elem);

__declspec(dllexport) void __stdcall
Vec_Iterate(void *base, int size, int count, void *fn)
{
    char *p = (char *)base;
    for (int i = 0; i < count; ++i) {   /* count <= 0: no iterations */
        ((vec_elem_fn)fn)(p);
        p += size;
    }
}

} // extern "C"
