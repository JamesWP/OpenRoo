/* GAMETICK_PLAN.md Band B reopened — the text-entry widget tick.
 *
 *   PollTextEntryKeys  0x004209a0   3 E8 sites (0x00415041 GameTick,
 *                                   0x00418E6F HandleKeypress,
 *                                   0x0041ACBB HandleTypedCheatCode)
 *
 * __thiscall(widget, uint phase), RET 4.  Its only callee is the
 * GetAsyncKeyState import, hoisted into EBP -- which patch.py already
 * redirects to hooks_GetAsyncKeyState (record.cpp).  This replacement calls
 * that hook directly, NOT user32: calling the DLL's own import would bypass
 * replay input and the recording would diverge.
 *
 * Widget: +4 char *buffer, +8 BYTE debounce key, +9 BYTE cursor, +0xa BYTE
 * max, +0xb DWORD active.  Transcribed from the LISTING:
 *
 *  - The cursor blink is FILD qword{phase,0} * float 0.01f, FSIN, all at
 *    80 bits, then FCOMP against double 0.0 with TEST AH,0x41: '_' only when
 *    strictly greater (and not unordered), ' ' otherwise.  Done in x87 asm so
 *    the precision is the original's -- sinl() is not guaranteed to be FSIN.
 *  - RETURN, ESCAPE, BACKSPACE, SPACE, then A..[ (0x41..0x5b -- '[' is
 *    included, as in the original), then 0..: (0x30..0x3a -- ':' included).
 *  - Shift is polled only after a letter is accepted; unshifted adds 0x20.
 *  - Backspace clears buffer[cursor+1] after the decrement, i.e. the byte
 *    that held the blink character -- not the one the cursor now sits on.
 *  - The debounce release test runs even when inactive.
 *
 * Control: KAROO_SIM_FX=entrycase -- shift sense inverted (letters come out
 * uppercase unshifted).  Changes what is TYPED, so a cheat or name compare
 * downstream sees a different string.
 */
#include <windows.h>
#include <string.h>
#include "log.h"
#include "textentry.h"

extern "C" __declspec(dllexport) SHORT WINAPI hooks_GetAsyncKeyState(int vKey);

static int s_fx = -1;

static int blink_positive(unsigned int phase)
{
    unsigned long long q = phase;       /* high dword zeroed, as the listing */
    static const float k = 0.0099999998f;   /* DAT_0045d400 */
    unsigned short sw;

    __asm__ volatile(
        "fildq %1\n\t"
        "fmuls %2\n\t"
        "fsin\n\t"
        "fldz\n\t"
        "fcompp\n\t"                    /* compares ST0(0.0) with ST1(sin)  */
        "fnstsw %0\n\t"
        : "=a"(sw) : "m"(q), "m"(k) : "st", "st(1)");
    /* fcompp above compared 0.0 against sin: C0 set when 0.0 < sin.  The
     * original's FCOMP sin-vs-0.0 jumps to ' ' on (C0|C3), i.e. sin <= 0 or
     * unordered; '_' exactly when sin > 0 and ordered.  With the operands
     * swapped that is: C0 set and C2 clear. */
    return (sw & 0x0100) && !(sw & 0x0400);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PollTextEntryKeys(void *self, unsigned int phase)
{
    ((TextEntry *)self)->poll(phase);
}

void TextEntry::poll(unsigned int phase)
{
#define BUF    ((unsigned char *)buffer_)
#define LAST   lastKey_
#define CUR    cursor_
#define MAXL   maxLength_
#define ACTIVE active_

    if (s_fx < 0) {
        char b[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", b, sizeof(b));
        s_fx = (n > 0 && n < sizeof(b) && strcmp(b, "entrycase") == 0);
        if (s_fx)
            log_write("textentry: KAROO_SIM_FX=entrycase -- shift inverted\n");
    }

    if (ACTIVE != 0) {
        BUF[CUR] = blink_positive(phase) ? 0x5f : 0x20;

        if (hooks_GetAsyncKeyState(0x0d) != 0 && LAST != 0x0d) {
            ACTIVE = 0;
            BUF[CUR] = 0;
            LAST = 0x0d;
        }
        if (hooks_GetAsyncKeyState(0x1b) != 0 && LAST != 0x1b) {
            ACTIVE = 0;
            BUF[CUR] = 0;
            LAST = 0x1b;
        }
        if (hooks_GetAsyncKeyState(0x08) != 0 && LAST != 0x08 && CUR != 0) {
            CUR = (unsigned char)(CUR - 1);
            BUF[CUR + 1] = 0;
            LAST = 0x08;
        }
        if (hooks_GetAsyncKeyState(0x20) != 0 && LAST != 0x20 && CUR < MAXL) {
            BUF[CUR] = 0x20;
            LAST = 0x20;
            CUR = (unsigned char)(CUR + 1);
        }
        for (unsigned char k = 0x41; k < 0x5c; ++k) {
            if (hooks_GetAsyncKeyState(k) != 0 && LAST != k && CUR < MAXL) {
                int shifted = hooks_GetAsyncKeyState(0x10) != 0;
                if (s_fx)
                    shifted = !shifted;
                BUF[CUR] = shifted ? k : (unsigned char)(k + 0x20);
                LAST = k;
                CUR = (unsigned char)(CUR + 1);
            }
        }
        for (unsigned char k = 0x30; k < 0x3b; ++k) {
            if (hooks_GetAsyncKeyState(k) != 0 && LAST != k && CUR < MAXL) {
                BUF[CUR] = k;
                LAST = k;
                CUR = (unsigned char)(CUR + 1);
            }
        }
    }
    if (hooks_GetAsyncKeyState(LAST) == 0)
        LAST = 0;
#undef BUF
#undef LAST
#undef CUR
#undef MAXL
#undef ACTIVE
}
