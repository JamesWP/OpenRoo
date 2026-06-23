#pragma once
#define DIRECTDRAW_VERSION 0x0100
#include <windows.h>
#include <ddraw.h>

typedef struct ComProxy {
    void     **vtable;  /* must be first — COM ABI */
    IUnknown  *real;
} ComProxy;

ComProxy *make_proxy(void **vtable, IUnknown *real);
