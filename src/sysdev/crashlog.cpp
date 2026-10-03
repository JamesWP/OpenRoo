#include <windows.h>
#include "sysdev.h"

namespace sysdev {

extern LogFn g_log;
#define SD_LOG(...) do { if (g_log) g_log(__VA_ARGS__); } while (0)

#ifdef _WIN64
#define CTX_IP(c) ((c)->Rip)
#define CTX_SP(c) ((c)->Rsp)
#define CTX_REGS(c) (void *)(c)->Rax, (void *)(c)->Rbx, (void *)(c)->Rcx, (void *)(c)->Rdx, (void *)(c)->Rsi, (void *)(c)->Rdi, (void *)(c)->Rbp, (void *)(c)->Rsp
#else
#define CTX_IP(c) ((c)->Eip)
#define CTX_SP(c) ((c)->Esp)
#define CTX_REGS(c) (void *)(c)->Eax, (void *)(c)->Ebx, (void *)(c)->Ecx, (void *)(c)->Edx, (void *)(c)->Esi, (void *)(c)->Edi, (void *)(c)->Ebp, (void *)(c)->Esp
#endif

/* True when ip lies inside the running executable's image. */
static bool in_exe_image(ULONG_PTR ip)
{
    const BYTE *base = (const BYTE *)GetModuleHandleA(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    return ip >= (ULONG_PTR)base && ip < (ULONG_PTR)base + nt->OptionalHeader.SizeOfImage;
}

static LONG WINAPI veh_handler(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD  *er  = ep->ExceptionRecord;
    CONTEXT           *ctx = ep->ContextRecord;

    // A guard-page fault that nothing handles ends the process silently, so
    // log every one (capped: stack growth raises them legitimately).
    if (er->ExceptionCode == STATUS_GUARD_PAGE_VIOLATION) {
        static LONG guard_count = 0;
        if (InterlockedIncrement(&guard_count) <= 64)
            SD_LOG("=== GUARD PAGE ===  EIP=%p fault_addr=%p type=%s  (#%ld)\n",
                (void *)CTX_IP(ctx), (void *)er->ExceptionInformation[1],
                er->ExceptionInformation[0] ? "write" : "read", guard_count);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    // Only faults inside the executable's image are ours.  Wine's own
    // page-fault handling passes through here first for faults elsewhere;
    // those are ignored.
    if (!in_exe_image(CTX_IP(ctx)))
        return EXCEPTION_CONTINUE_SEARCH;

    SD_LOG("\n=== ACCESS VIOLATION ===\n");
    SD_LOG("IP=%p  fault_addr=%p  type=%s\n", (void *)CTX_IP(ctx),
        (void *)er->ExceptionInformation[1],
        er->ExceptionInformation[0] ? "write" : "read");
    SD_LOG("AX=%p BX=%p CX=%p DX=%p\nSI=%p DI=%p BP=%p SP=%p\n",
        CTX_REGS(ctx));

    SD_LOG("Stack (first 0x20 words):\n");
    const ULONG_PTR *sp = (const ULONG_PTR *)CTX_SP(ctx);
    for (int i = 0; i < 0x20; i++)
        SD_LOG("  [SP+%04X] %p\n", (unsigned)(i * sizeof(ULONG_PTR)), (void *)sp[i]);

    return EXCEPTION_CONTINUE_SEARCH;
}

void installCrashLogger()
{
    AddVectoredExceptionHandler(0, veh_handler);
}

}  // namespace sysdev
