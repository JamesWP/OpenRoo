#include <windows.h>
#include "levelobjbase.h"
#include <stdlib.h>


static void *const g_LevelObjBaseVtable[1] = { (void *)&LevelObjBase_ScalarDtor };

void *LevelObjBase_Vtable(void)
{
    return (void *)g_LevelObjBaseVtable;
}

void LevelObjBase_DtorBody(void *self)
{
    *(void **)self = LevelObjBase_Vtable();
}

/* Nothing calls it: the vtable is the only way in, and no live object carries
 * the base table for longer than the base constructor and destructor. */
void *LevelObjBase_ScalarDtor(void *self, unsigned int flags)
{
    LevelObjBase_DtorBody(self);
    if (flags & 1)
        free(self);
    return self;
}

