#include <windows.h>
#include <stdlib.h>
#include "log.h"
#include "launcher.h"

static int   g_skip_launcher    = 0;
static DWORD g_auto_exit_s = 0;

static BOOL CALLBACK find_main_window(HWND hwnd, LPARAM lp)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && !GetWindow(hwnd, GW_OWNER) && IsWindowVisible(hwnd)) {
        *(HWND *)lp = hwnd;
        return FALSE;
    }
    return TRUE;
}

void launcher_end_run(const char *why)
{
    static bool done = false;
    if (done) return;
    done = true;

    HWND hwnd = NULL;
    EnumWindows(find_main_window, (LPARAM)&hwnd);
    if (hwnd) {
        log_write("launcher: ending run (%s) — posting WM_CLOSE to hwnd %p\n",
                  why, (void *)hwnd);
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    } else {
        log_write("launcher: ending run (%s) — no main window, ExitProcess\n", why);
        ExitProcess(0);
    }
}

static DWORD WINAPI auto_exit_thread(LPVOID param)
{
    DWORD ms = (DWORD)(ULONG_PTR)param;
    log_write("launcher: auto-exit in %lu s\n", ms / 1000);
    Sleep(ms);

    HWND hwnd = NULL;
    EnumWindows(find_main_window, (LPARAM)&hwnd);
    if (hwnd) {
        log_write("launcher: posting WM_CLOSE to hwnd %p\n", (void *)hwnd);
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    } else {
        log_write("launcher: no main window found, calling ExitProcess\n");
        ExitProcess(0);
    }
    return 0;
}

void launcher_init(void)
{
    char buf[32];
    if (GetEnvironmentVariableA("KAROO_SKIP_LAUNCHER", buf, sizeof(buf)))
        g_skip_launcher = atoi(buf);
    if (GetEnvironmentVariableA("KAROO_AUTO_EXIT_SECS", buf, sizeof(buf)))
        g_auto_exit_s = (DWORD)atoi(buf);

    if (!g_skip_launcher)
        return;

    log_write("launcher: skipping the launcher dialog (window is still shown), auto_exit_secs=%lu\n", g_auto_exit_s);

    if (g_auto_exit_s > 0)
        CloseHandle(CreateThread(NULL, 0, auto_exit_thread,
                                 (LPVOID)(ULONG_PTR)(g_auto_exit_s * 1000), 0, NULL));
}

extern "C" __declspec(dllexport) INT_PTR WINAPI hooks_DialogBoxParamA(
        HINSTANCE hInstance, LPCSTR lpTemplate, HWND hWndParent,
        DLGPROC lpDialogFunc, LPARAM dwInitParam)
{
    if (g_skip_launcher) {
        log_write("launcher: DialogBoxParamA skipped (template=%p)\n", (void*)lpTemplate);
        return IDOK;
    }
    typedef INT_PTR (WINAPI *fn_t)(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM);
    static fn_t real_fn = NULL;
    if (!real_fn)
        real_fn = (fn_t)GetProcAddress(GetModuleHandleA("user32.dll"), "DialogBoxParamA");
    return real_fn(hInstance, lpTemplate, hWndParent, lpDialogFunc, dwInitParam);
}
