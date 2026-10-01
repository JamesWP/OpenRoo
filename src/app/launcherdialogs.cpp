/* The launcher window (a frameless rectangle painted from our own bitmaps,
 * with bitmap buttons: play, setup, quit) and the display device dialog it opens.
 *
 * Neither test gate reaches this code: --skip-launcher and --headless both
 * skip the launcher (launcher.cpp).  It is checked by hand.
 *
 * KAROO_LAUNCHERDLG_FX=allaspect is a negative control: the device dialog
 * lists every aspect ratio instead of 4:3 only. */

#include <windows.h>
#include "audiodev.h"
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "launcherdialogs.h"
#include "launcher.h"
#include "renderdevice.h"
#include "gameglobals.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "resources.h"
#include "launcher_layout.h"
#include "buildinfo.h"

static const int IDC_PLAY       = 0x3f5;     // "spielen"
static const int IDC_SETUP      = 0x3f2;     // "setup"; the same id as the mode combo
static const int IDC_QUIT       = IDCANCEL;  // "ende"
static const int IDC_DRIVERS    = 0x3f0;
static const int IDC_HWCHECK    = 0x3f1;
static const int IDC_MODES      = 0x3f2;
static const int IDD_DEVICE     = 0x6e;

// The launcher's bitmaps, RCDATA in openroo.rc.
static const int IDR_LAUNCHER_BG        = 200;
static const int IDR_LAUNCHER_PLAY_OFF  = 201;
static const int IDR_LAUNCHER_PLAY_FOC  = 202;
static const int IDR_LAUNCHER_SETUP_OFF = 203;
static const int IDR_LAUNCHER_SETUP_FOC = 204;
static const int IDR_LAUNCHER_QUIT_OFF  = 205;
static const int IDR_LAUNCHER_QUIT_FOC  = 206;

static const char SND_SWITCH[] = "waves\\switch.wav";
static const char SND_IMPACT[] = "waves\\mineimpact.wav";
static const char SND_UGH[]    = "waves\\ugh.wav";

static const BYTE *s_menuBmp;
static const BYTE *s_playOff,  *s_playFoc;
static const BYTE *s_setupOff, *s_setupFoc;
static const BYTE *s_quitOff,  *s_quitFoc;

/* The hardware checkbox's state on OK.  Nothing reads it. */
static unsigned char s_hwChecked;

static bool fx_allaspect()
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = GetEnvironmentVariableA("KAROO_LAUNCHERDLG_FX", buf, sizeof(buf))
                 && lstrcmpiA(buf, "allaspect") == 0;
        log_write("launcherdlg: FX mode = %s\n", cached ? "allaspect" : "off");
    }
    return cached != 0;
}

/* A .bmp file embedded as RCDATA, whole (file header included), or NULL.
 * Resource memory: never freed. */
static const BYTE *load_bmp_resource(int id)
{
    HMODULE mod = Resources_Module();
    HRSRC res = FindResourceA(mod, MAKEINTRESOURCEA(id), (LPCSTR)RT_RCDATA);
    HGLOBAL hg = res ? LoadResource(mod, res) : NULL;
    const BYTE *buf = hg ? (const BYTE *)LockResource(hg) : NULL;
    if (!buf || SizeofResource(mod, res) < 54 || *(const WORD *)buf != 0x4d42)  // "BM"
        return NULL;
    return buf;
}

/* A file's BITMAPINFO (after the 14-byte file header) and its size, OS/2 core
 * headers included; the height is taken absolute. */
static void dib_size(const BYTE *hdr, int *w, int *h)
{
    if (*(const DWORD *)hdr == sizeof(BITMAPCOREHEADER)) {
        *w = *(const WORD *)(hdr + 4);
        *h = *(const WORD *)(hdr + 6);
    } else {
        *w = *(const LONG *)(hdr + 4);
        *h = abs(*(const LONG *)(hdr + 8));
    }
}

static void stretch_bmp(HDC hdc, int destW, int destH, const BYTE *hdr,
                        const BYTE *bits)
{
    int w, h;
    dib_size(hdr, &w, &h);
    StretchDIBits(hdc, 0, 0, destW, destH, 0, 0, w, h, bits,
                  (const BITMAPINFO *)hdr, DIB_RGB_COLORS, SRCCOPY);
}

/* WM_DRAWITEM for one owner-drawn button.  Pressed and focused both show the
 * focus bitmap.  PRESERVED: pressed takes its pixel offset from the off bitmap
 * and applies it to the focus one; harmless while both files share a header
 * layout, which they do. */
static void draw_button(const DRAWITEMSTRUCT *di, const BYTE *off, const BYTE *foc)
{
    if (!off || !foc)
        return;
    const BYTE *hdr, *bits;
    if (di->itemState & ODS_SELECTED) {
        bits = foc + *(const DWORD *)(off + 10);
        hdr  = foc + 14;
    } else if (di->itemState & ODS_FOCUS) {
        bits = foc + *(const DWORD *)(foc + 10);
        hdr  = foc + 14;
    } else {
        bits = off + *(const DWORD *)(off + 10);
        hdr  = off + 14;
    }
    stretch_bmp(di->hDC, di->rcItem.right - di->rcItem.left,
                di->rcItem.bottom - di->rcItem.top, hdr, bits);
}

/* The corner text: a 3x5 pixel font, each glyph 5 rows of 3 bits (high bit
 * leftmost).  Lower case draws as upper; anything unknown as a blank. */
static const char FONT_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-'";
static const unsigned char FONT[][5] = {
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},
    {3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
    {5,7,7,5,5},{6,5,5,5,5},{2,5,5,5,2},{6,5,6,4,4},{2,5,5,6,3},{6,5,6,5,5},
    {3,4,2,1,6},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
    {5,5,2,2,2},{7,1,2,4,7},
    {7,5,5,5,7},{2,6,2,2,7},{6,1,2,4,7},{6,1,2,1,6},{5,5,7,1,1},{7,4,6,1,6},
    {3,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,6},
    {0,0,0,0,2},{0,0,7,0,0},{2,2,0,0,0},
};
static const int FONT_SCALE = 2;  // screen pixels per font pixel

static void draw_text_px(HDC hdc, int x, int y, const char *text, HBRUSH brush)
{
    for (; *text; ++text, x += 4 * FONT_SCALE) {
        const char *at = strchr(FONT_CHARS, toupper((unsigned char)*text));
        if (!at)
            continue;
        const unsigned char *g = FONT[at - FONT_CHARS];
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 3; ++col)
                if (g[row] & (4 >> col)) {
                    RECT r = { x + col * FONT_SCALE, y + row * FONT_SCALE,
                               x + (col + 1) * FONT_SCALE, y + (row + 1) * FONT_SCALE };
                    FillRect(hdc, &r, brush);
                }
    }
}

/* One string from our VERSIONINFO, or "" if absent. */
static void version_string(const char *name, char *out, size_t size)
{
    out[0] = 0;
    char path[MAX_PATH];
    if (!GetModuleFileNameA(Resources_Module(), path, sizeof(path)))
        return;
    DWORD dummy, len = GetFileVersionInfoSizeA(path, &dummy);
    void *info = len ? malloc(len) : NULL;
    if (info && GetFileVersionInfoA(path, 0, len, info)) {
        char key[64];
        snprintf(key, sizeof(key), "\\StringFileInfo\\000004b0\\%s", name);
        char *val;
        UINT vlen;
        if (VerQueryValueA(info, key, (void **)&val, &vlen) && vlen)
            snprintf(out, size, "%s", val);
    }
    free(info);
}

/* The corner text's extent, shadow included; clicking it opens the project. */
static RECT s_linkRect;
static const char LINK_URL[] = "https://github.com/jameswp/OpenRoo";

/* "<name> <version> <git sha>" at the bottom left, with a one-pixel shadow. */
static void draw_build_text(HDC hdc)
{
    char name[64], ver[32], text[160];
    version_string("ProductName", name, sizeof(name));
    version_string("ProductVersion", ver, sizeof(ver));
    snprintf(text, sizeof(text), "%s %s %s", name, ver, BUILD_GIT_SHA);
    int x = 11, y = LAUNCHER_H - 11 - 5 * FONT_SCALE;
    HBRUSH shadow = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH fore   = CreateSolidBrush(RGB(255, 236, 200));
    s_linkRect.left   = x;
    s_linkRect.top    = y;
    s_linkRect.right  = x + (int)strlen(text) * 4 * FONT_SCALE;
    s_linkRect.bottom = y + 6 * FONT_SCALE;
    draw_text_px(hdc, x + FONT_SCALE, y + FONT_SCALE, text, shadow);
    draw_text_px(hdc, x, y, text, fore);
    DeleteObject(shadow);
    DeleteObject(fore);
}

static void place(HWND hDlg, int id, int x, int y, int w, int h)
{
    MoveWindow(GetDlgItem(hDlg, id), x, y, w, h, TRUE);
}

static void starter_init(HWND hDlg)
{
    SetWindowTextA(hDlg, "Open'Roo");
    s_menuBmp  = load_bmp_resource(IDR_LAUNCHER_BG);
    s_playOff  = load_bmp_resource(IDR_LAUNCHER_PLAY_OFF);
    s_playFoc  = load_bmp_resource(IDR_LAUNCHER_PLAY_FOC);
    s_setupOff = load_bmp_resource(IDR_LAUNCHER_SETUP_OFF);
    s_setupFoc = load_bmp_resource(IDR_LAUNCHER_SETUP_FOC);
    s_quitOff  = load_bmp_resource(IDR_LAUNCHER_QUIT_OFF);
    s_quitFoc  = load_bmp_resource(IDR_LAUNCHER_QUIT_FOC);

    // Centred, the size of the background; SWP_NOZORDER makes HWND_TOPMOST moot.
    int y = GetSystemMetrics(SM_CYSCREEN) / 2 - LAUNCHER_H / 2;
    int x = GetSystemMetrics(SM_CXSCREEN) / 2 - LAUNCHER_W / 2;
    SetWindowPos(hDlg, HWND_TOPMOST, x, y, LAUNCHER_W, LAUNCHER_H, SWP_NOZORDER);

    place(hDlg, IDC_PLAY,  LAUNCHER_PLAY_RECT);
    place(hDlg, IDC_SETUP, LAUNCHER_SETUP_RECT);
    place(hDlg, IDC_QUIT,  LAUNCHER_QUIT_RECT);
}

/* Whether a client point (WM_LBUTTONUP's lParam) is in a painted box. */
static bool in_box(LPARAM lParam, int x, int y, int w, int h)
{
    int px = (short)LOWORD(lParam), py = (short)HIWORD(lParam);
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void open_device_dialog(HWND hDlg)
{
    hooks_DialogBoxParamA(Resources_Module(), MAKEINTRESOURCEA(IDD_DEVICE),
                          hDlg, LauncherDlg_DeviceSelectProc, 0);
}

  INT_PTR CALLBACK
LauncherDlg_Proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        starter_init(hDlg);
        return 0;

    case WM_DESTROY:
        return 0;

    case WM_PAINT: {
        if (!s_menuBmp)
            return 0;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hDlg, &ps);
        RECT rc;
        GetClientRect(hDlg, &rc);
        stretch_bmp(hdc, rc.right - rc.left, rc.bottom - rc.top,
                    s_menuBmp + 14, s_menuBmp + *(DWORD *)(s_menuBmp + 10));
        draw_build_text(hdc);
        EndPaint(hDlg, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *di = (const DRAWITEMSTRUCT *)lParam;
        if (di->CtlID == (UINT)IDC_QUIT)       draw_button(di, s_quitOff,  s_quitFoc);
        else if (di->CtlID == (UINT)IDC_SETUP) draw_button(di, s_setupOff, s_setupFoc);
        else if (di->CtlID == (UINT)IDC_PLAY)  draw_button(di, s_playOff,  s_playFoc);
        return 1;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        if (id == IDOK) {
            // Enter acts on whichever button has focus.  The tests are not
            // exclusive: each runs even after an earlier one ended the dialog.
            HWND focus = GetFocus();
            if (GetDlgItem(hDlg, IDC_PLAY) == focus) {
                audiodev::playSystemSound(SND_IMPACT, true);
                EndDialog(hDlg, 1);
            }
            if (GetDlgItem(hDlg, IDC_QUIT) == focus) {
                audiodev::playSystemSound(SND_UGH, false);
                EndDialog(hDlg, 0);
            }
            if (GetDlgItem(hDlg, IDC_SETUP) == focus) {
                audiodev::playSystemSound(SND_IMPACT, true);
                open_device_dialog(hDlg);
            }
            return 0;
        }
        if (id != IDC_PLAY && id != IDC_SETUP && id != IDC_QUIT)
            return 0;
        if (code == BN_CLICKED) {
            if (id == IDC_PLAY) {
                audiodev::playSystemSound(SND_IMPACT, true);
                EndDialog(hDlg, 1);
            } else if (id == IDC_SETUP) {
                audiodev::playSystemSound(SND_IMPACT, true);
                open_device_dialog(hDlg);
            } else {
                audiodev::playSystemSound(SND_UGH, false);
                EndDialog(hDlg, 0);
            }
        } else if (code == BN_SETFOCUS) {
            audiodev::playSystemSound(SND_SWITCH, true);
        }
        return 0;
    }

    // The window has no frame; its title bar and boxes are painted in the
    // background.  The title bar drags it, the boxes minimise and quit.
    case WM_NCHITTEST: {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        ScreenToClient(hDlg, &pt);
        LPARAM cl = MAKELPARAM(pt.x, pt.y);
        LRESULT hit = pt.y < LAUNCHER_TITLE_H && !in_box(cl, LAUNCHER_MIN_RECT)
                      && !in_box(cl, LAUNCHER_CLOSE_RECT) ? HTCAPTION : HTCLIENT;
        SetWindowLongPtrA(hDlg, DWLP_MSGRESULT, hit);
        return 1;
    }

    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hDlg, &pt);
        if (LOWORD(lParam) != HTCLIENT || !PtInRect(&s_linkRect, pt))
            return 0;
        SetCursor(LoadCursorA(NULL, IDC_HAND));
        SetWindowLongPtrA(hDlg, DWLP_MSGRESULT, TRUE);
        return 1;
    }

    case WM_LBUTTONUP:
        if (in_box(lParam, s_linkRect.left, s_linkRect.top,
                   s_linkRect.right - s_linkRect.left,
                   s_linkRect.bottom - s_linkRect.top)) {
            ShellExecuteA(hDlg, "open", LINK_URL, NULL, NULL, SW_SHOWNORMAL);
        } else if (in_box(lParam, LAUNCHER_MIN_RECT)) {
            ShowWindow(hDlg, SW_MINIMIZE);
        } else if (in_box(lParam, LAUNCHER_CLOSE_RECT)) {
            audiodev::playSystemSound(SND_UGH, false);
            EndDialog(hDlg, 0);
        }
        return 0;
    }
    return 0;
}

/* The mode list for the selected driver.  Each entry's item data is its
 * index in RenderDevice::EnumerateDisplayModes' list, which is the index
 * RenderDevice::Create takes.  Returns false where the dialog should fail. */
static bool fill_modes(HWND hDlg)
{
    LRESULT sel = SendDlgItemMessageA(hDlg, IDC_DRIVERS, CB_GETCURSEL, 0, 0);
    const GUID *guid =
        (const GUID *)SendDlgItemMessageA(hDlg, IDC_DRIVERS, CB_GETITEMDATA, sel, 0);

    std::vector<DisplayMode> modes;
    if (!RenderDevice::EnumerateDisplayModes(guid, modes, fx_allaspect()))
        return false;

    HWND combo = GetDlgItem(hDlg, IDC_MODES);
    for (size_t i = 0; i < modes.size(); i++) {
        char text[64];
        snprintf(text, sizeof(text), "%lux%lux%lu", modes[i].dwWidth,
                 modes[i].dwHeight, modes[i].dwBitDepth);
        LRESULT idx = SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)text);
        if (idx == CB_ERR)
            return false;
        SendMessageA(combo, CB_SETITEMDATA, idx, (LPARAM)i);
    }
    return true;
}

static bool device_init(HWND hDlg)
{
    SetWindowPos(hDlg, HWND_TOPMOST, 400, 300, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    // One combo entry per adapter; its item data is a heap copy of the GUID
    // (NULL for the primary adapter).  PRESERVED: the copies are never freed.
    std::vector<Adapter> adapters;
    if (!RenderDevice::EnumerateAdapters(adapters))
        return false;
    HWND drivers = GetDlgItem(hDlg, IDC_DRIVERS);
    for (size_t i = 0; i < adapters.size(); i++) {
        LRESULT idx = SendMessageA(drivers, CB_ADDSTRING, 0, (LPARAM)adapters[i].name);
        if (idx == CB_ERR)
            break;
        GUID *copy = NULL;
        if (adapters[i].hasGuid) {
            copy = (GUID *)malloc(sizeof(GUID));
            if (!copy)
                break;
            *copy = adapters[i].guid;
        }
        SendMessageA(drivers, CB_SETITEMDATA, idx, (LPARAM)copy);
    }

    // Select the configured driver: the last entry whose GUID matches.
    const GUID *want = Game::instance()->config()->adapterGuid();
    LRESULT count = SendDlgItemMessageA(hDlg, IDC_DRIVERS, CB_GETCOUNT, 0, 0);
    LRESULT pick = 0;
    for (LRESULT i = 0; i < count; i++) {
        const GUID *g = (const GUID *)SendDlgItemMessageA(hDlg, IDC_DRIVERS,
                                                          CB_GETITEMDATA, i, 0);
        if (g && memcmp(g, want, sizeof(GUID)) == 0)
            pick = i;
    }
    SendDlgItemMessageA(hDlg, IDC_DRIVERS, CB_SETCURSEL, pick, 0);

    // REVIEW: the mode numbering used to carry over from an earlier opening
    // of the dialog; each list now numbers from 0.
    if (!fill_modes(hDlg))
        return false;
    SendDlgItemMessageA(hDlg, IDC_MODES, CB_SETCURSEL,
                        Game::instance()->config()->displayModeIndex(), 0);
    SendDlgItemMessageA(hDlg, IDC_HWCHECK, BM_SETCHECK, BST_CHECKED, 0);
    return true;
}

static void device_ok(HWND hDlg)
{
    Config *cfg = Game::instance()->config();
    LRESULT sel = SendDlgItemMessageA(hDlg, IDC_DRIVERS, CB_GETCURSEL, 0, 0);
    const GUID *g = (const GUID *)SendDlgItemMessageA(hDlg, IDC_DRIVERS,
                                                      CB_GETITEMDATA, sel, 0);
    if (g)
        *cfg->adapterGuid() = *g;
    else
        memset(cfg->adapterGuid(), 0, sizeof(GUID));
    s_hwChecked = SendDlgItemMessageA(hDlg, IDC_HWCHECK, BM_GETCHECK, 0, 0) == BST_CHECKED;
    sel = SendDlgItemMessageA(hDlg, IDC_MODES, CB_GETCURSEL, 0, 0);
    cfg->setDisplayModeIndex((unsigned int)SendDlgItemMessageA(
        hDlg, IDC_MODES, CB_GETITEMDATA, sel, 0));
    EndDialog(hDlg, 1);
}

  INT_PTR CALLBACK
LauncherDlg_DeviceSelectProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM)
{
    if (msg == WM_INITDIALOG)
        return device_init(hDlg) ? 1 : 0;
    if (msg != WM_COMMAND)
        return 0;

    switch (LOWORD(wParam)) {
    case IDOK:
        device_ok(hDlg);
        break;
    case IDCANCEL:
        EndDialog(hDlg, 0);
        break;
    case IDC_DRIVERS:
        if (HIWORD(wParam) != CBN_SELCHANGE)
            break;
        SendDlgItemMessageA(hDlg, IDC_MODES, CB_RESETCONTENT, 0, 0);
        if (!fill_modes(hDlg))
            return 0;
        SendDlgItemMessageA(hDlg, IDC_MODES, CB_SETCURSEL, 0, 0);
        SendDlgItemMessageA(hDlg, IDC_HWCHECK, BM_SETCHECK, BST_CHECKED, 0);
        break;
    }
    return 1;
}
