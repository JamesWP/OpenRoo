/* LevelObjectBase: the common base of every placed level object (the breakable
 * tile, lift, slide, bridge, and the movable entities: bomb, foe, player).
 * Not levelobject.h, the theme records the renderer draws.  Its vtable has one
 * slot, the deleting destructor. */
#pragma once

/* The one-slot table, installed by the destructor body and by the movable
 * entities' base construction. */
extern "C" __declspec(dllexport) void *LevelObjBase_Vtable(void);

/* Reinstalls the base table. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
LevelObjBase_DtorBody(void *self);

/* The deleting destructor: returns this; bit 0 of flags frees. */
extern "C" __declspec(dllexport) void *__attribute__((thiscall))
LevelObjBase_ScalarDtor(void *self, unsigned int flags);
