#include <windows.h>
#include "windev.h"
#include "resources.h"

namespace windev {

static LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }
#define WD_LOG(...) do { if (g_log) g_log(__VA_ARGS__); } while (0)

bool messageBox(void *parent, const char *text, const char *title,
                Buttons buttons, Icon icon)
{
    UINT flags = (buttons == Buttons::OkCancel ? MB_OKCANCEL : MB_OK)
               | (icon == Icon::Error ? MB_ICONHAND : 0);
    return MessageBoxA((HWND)parent, text, title, flags) != IDCANCEL;
}

static WindowHandler *g_handler;

static LRESULT CALLBACK window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_handler) {
        switch (msg) {
        case WM_DESTROY:
            g_handler->onDestroyed();
            break;
        case WM_ACTIVATE:
            // Exactly WA_ACTIVE: a click activation (2) counts as losing focus.
            g_handler->onActivate((WORD)wParam == WA_ACTIVE);
            break;
        case WM_KEYUP:
            g_handler->onKeyUp();
            break;
        }
        if (g_handler->onNativeMessage(msg, wParam, lParam))
            return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

Window::Window() : handle_(NULL)
{
}

Window::~Window()
{
}

bool Window::create(WindowHandler *handler,
                    const WindowConfig &config)
{
    g_handler = handler;
    HINSTANCE instance = GetModuleHandle(NULL);

    WNDCLASSA wc;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = window_proc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = instance;
    wc.hIcon         = LoadIconA(Resources_Module(), MAKEINTRESOURCEA(0x6a));
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = "Karoo";
    if (RegisterClassA(&wc) == 0)
        return false;

    HWND hwnd;
    if (config.messageOnly) {
        // A window parented to HWND_MESSAGE never goes near the display
        // driver: no frame, position or visibility, but a handle, a window
        // procedure and a message queue.  That is what a run with no display
        // needs, and the only kind that can be created when no display
        // server is running.
        hwnd = CreateWindowExA(0, "Karoo", config.title, WS_POPUP,
                               0, 0, config.width, config.height,
                               HWND_MESSAGE, NULL, instance, NULL);
        WD_LOG("windev: message-only window hwnd=%p\n", (void *)hwnd);
    } else {
        hwnd = CreateWindowExA(WS_EX_APPWINDOW, "Karoo", config.title, WS_POPUP,
                               0, 0, config.width, config.height,
                               NULL, NULL, instance, NULL);
    }
    handle_ = hwnd;
    return hwnd != NULL;
}

void Window::destroy()
{
    if (handle_)
        DestroyWindow((HWND)handle_);
    handle_ = NULL;
}

void Window::show(bool visible)
{
    ShowWindow((HWND)handle_, visible ? SW_SHOW : SW_HIDE);
    if (!visible)
        UpdateWindow((HWND)handle_);
}

/* Every pending message is handled before each idle() call.  WM_QUIT is only
 * delivered once the queue is otherwise empty, so handling one message per
 * frame meant a quit could run any number of extra frames behind whatever else
 * was queued.  Stopping the music posts such a message (MM_MCINOTIFY), which
 * made the frame count of a run depend on whether music was on. */
int runMessageLoop(void (*idle)())
{
    MSG msg;
    for (;;) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                return (int)msg.wParam;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        idle();
    }
}

void quit(int exitCode)
{
    PostQuitMessage(exitCode);
}

/* The process's visible, unowned top-level window. */
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

void requestClose()
{
    HWND hwnd = NULL;
    EnumWindows(find_main_window, (LPARAM)&hwnd);
    if (hwnd) {
        WD_LOG("windev: posting WM_CLOSE to hwnd %p\n", (void *)hwnd);
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    } else {
        WD_LOG("windev: no main window found, calling ExitProcess\n");
        ExitProcess(0);
    }
}

static DWORD WINAPI close_thread(LPVOID param)
{
    Sleep((DWORD)(ULONG_PTR)param);
    requestClose();
    return 0;
}

void requestCloseAfter(unsigned milliseconds)
{
    CloseHandle(CreateThread(NULL, 0, close_thread,
                             (LPVOID)(ULONG_PTR)milliseconds, 0, NULL));
}

}  // namespace windev
