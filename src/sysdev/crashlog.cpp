#include <windows.h>
#include "sysdev.h"

namespace sysdev {

extern LogFn g_log;
#define SD_LOG(...) do { if (g_log) g_log(__VA_ARGS__); } while (0)

static LONG WINAPI veh_handler(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD  *er  = ep->ExceptionRecord;
    CONTEXT           *ctx = ep->ContextRecord;

    // A guard-page fault that nothing handles ends the process silently, so
    // log every one (capped: stack growth raises them legitimately).
    if (er->ExceptionCode == STATUS_GUARD_PAGE_VIOLATION) {
        static LONG guard_count = 0;
        if (InterlockedIncrement(&guard_count) <= 64)
            SD_LOG("=== GUARD PAGE ===  EIP=%08lX fault_addr=%08lX type=%s  (#%ld)\n",
                ctx->Eip, (DWORD)er->ExceptionInformation[1],
                er->ExceptionInformation[0] ? "write" : "read", guard_count);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    // Only faults inside the executable's image are ours.  Wine's own
    // page-fault handling passes through here first for faults elsewhere;
    // those are ignored.  The second range is unused.
    if (!((ctx->Eip >= 0x400000 && ctx->Eip < 0x500000) ||
          (ctx->Eip >= 0x10000000 && ctx->Eip < 0x10100000)))
        return EXCEPTION_CONTINUE_SEARCH;

    SD_LOG("\n=== ACCESS VIOLATION ===\n");
    SD_LOG("EIP=%08lX  fault_addr=%08lX  type=%s\n",
        ctx->Eip,
        (DWORD)er->ExceptionInformation[1],
        er->ExceptionInformation[0] ? "write" : "read");
    SD_LOG("EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX\n",
        ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx);
    SD_LOG("ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX\n",
        ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);

    SD_LOG("Stack at ESP (first 0x20 words):\n");  // 0x20 words
    DWORD *sp = (DWORD *)ctx->Esp;
    for (int i = 0; i < 0x20; i++)
        SD_LOG("  [ESP+%04X] %08lX\n", i * 4, sp[i]);

    return EXCEPTION_CONTINUE_SEARCH;
}

void installCrashLogger()
{
    AddVectoredExceptionHandler(0, veh_handler);
}

}  // namespace sysdev
