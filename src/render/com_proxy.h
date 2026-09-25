#pragma once
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>

typedef struct {
    void    **vtable;  /* must be first — COM ABI requires vtable at offset 0 */
    IUnknown *real;    /* the real COM pointer this proxy forwards to */
} ComProxy;

/* The DirectDrawCreate everything goes through: the null device when
 * headless, else the real one wrapped in our proxy (com_proxy.cpp). */
extern "C" __declspec(dllexport) HRESULT WINAPI hooks_DirectDrawCreate(
        GUID *lpGUID, LPDIRECTDRAW *lplpDD, IUnknown *pUnkOuter);
