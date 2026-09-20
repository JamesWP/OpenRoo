/* LevelObjectBase -- the common base of every placed level object: the
 * BreakableTile, the LiftObject, the SlideObject, the BridgeObject and the
 * MovableEntity (and through it Bomb, Foe and Player).  Not to be confused
 * with levelobject.h, which is the scene-graph LevelObject the renderer
 * draws; this is the three-function base class TU 0x00401000-0x00401070:
 *
 *   0x00401000  ctor        vtable 0x45d290, then zero +0x25/+0x29/+0x2d
 *                           -- already DEAD-stubbed: all five callers are
 *                           replaced classes that do the field work inline
 *   0x00401040  scalar deleting dtor (vtable slot 0): the dtor body, then
 *                           FactAlloc::Free2 when bit 0 is set
 *   0x00401060  dtor body   vtable 0x45d290, and nothing else.  EAX is not
 *                           set; no caller reads a return value
 *
 * THE VTABLE IS OURS (ENDGAME_PLAN.md, "The vtable address of our objects
 * may be our own").  A byte scan of Karoo.exe.orig for the literal
 * 0x0045d290 finds exactly two occurrences, 0x1026 and 0x1062 -- inside the
 * two functions above.  Nothing else in the binary installs that table, so
 * making it ours is a local decision, and the game's table keeps pointing at
 * a UD2 stub as the tripwire.
 *
 * Which is why these are REPLACED and not dead: 0x401040 has no reference of
 * any kind (xref.py), and 0x401060's five JMP sites are all inside
 * UD2-stubbed dtors, so "dead" was available -- but only by ignoring that
 * our own MovableEntity base still installs the table that holds 0x401040.
 * Owning the slot answers that instead of arguing around it.
 */
#pragma once

/* Our one-slot table; installed by the dtor body below and by
 * MovableEntity::populateBaseForGame / destroyBaseForGame. */
extern "C" __declspec(dllexport) void *LevelObjBase_Vtable(void);

/* 0x00401060 -- re-install the base table.  __thiscall, no return. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
LevelObjBase_DtorBody(void *self);

/* 0x00401040 -- vtable slot 0.  Returns `this`; bit 0 of `flags` frees. */
extern "C" __declspec(dllexport) void *__attribute__((thiscall))
LevelObjBase_ScalarDtor(void *self, unsigned int flags);
