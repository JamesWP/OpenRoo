#pragma once
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>

typedef struct {
    void    **vtable;  /* must be first — COM ABI requires vtable at offset 0 */
    IUnknown *real;    /* the real COM pointer this proxy forwards to */
} ComProxy;
