#pragma once
#include <windows.h>
#include <stddef.h>

/* CFaktMesh — mesh object drawn by DrawMeshBuffer / DrawFramedModel.
 * Layout from HOOKS.md § CFaktMesh, confirmed against the disassembly of
 * both draw methods (0x437b40 / 0x437b80).  Vertex data is a flat array of
 * 0x28-byte FVF 0x212 vertices, all animation frames concatenated. */
struct CFaktMesh {
    void  *unknown00;     // +0x00 (maybe vtable; not read by the draw path)
    void  *pVertexData;   // +0x04 flat vertex array, stride 0x28
    DWORD  dwVertexCount; // +0x08 vertices per animation frame
    DWORD  unknown0c;     // +0x0c
    WORD   wFrameCount;   // +0x10 frame index clamps to 0 when >= this
};

static_assert(offsetof(CFaktMesh, pVertexData)   == 0x04, "CFaktMesh layout mismatch");
static_assert(offsetof(CFaktMesh, dwVertexCount) == 0x08, "CFaktMesh layout mismatch");
static_assert(offsetof(CFaktMesh, wFrameCount)   == 0x10, "CFaktMesh layout mismatch");
