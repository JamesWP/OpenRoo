#include <windows.h>
#include "log.h"
#include "veh.h"

static LONG WINAPI veh_handler(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD  *er  = ep->ExceptionRecord;
    CONTEXT           *ctx = ep->ContextRecord;

    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    // Only log crashes in game code; Wine's own page-fault handling (EIP in DLL range)
    // fires this VEH first — ignore those.
    if (ctx->Eip < 0x400000 || ctx->Eip >= 0x500000)
        return EXCEPTION_CONTINUE_SEARCH;

    log_write("\n=== ACCESS VIOLATION ===\n");
    log_write("EIP=%08lX  fault_addr=%08lX  type=%s\n",
        ctx->Eip,
        (DWORD)er->ExceptionInformation[1],
        er->ExceptionInformation[0] ? "write" : "read");
    log_write("EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX\n",
        ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx);
    log_write("ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX\n",
        ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);

    log_write("Stack at ESP (first 0x20 words):\n");
    DWORD *sp = (DWORD *)ctx->Esp;
    for (int i = 0; i < 0x20; i++)
        log_write("  [ESP+%04X] %08lX\n", i * 4, sp[i]);

    return EXCEPTION_CONTINUE_SEARCH;
}

void install_veh(void)
{
    AddVectoredExceptionHandler(0, veh_handler);
}
