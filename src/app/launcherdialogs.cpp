/* The launcher window (a shaped window with bitmap buttons: play, setup, quit,
 * and a link to the publisher's site) and the display device dialog it opens.
 *
 * Neither test gate reaches this code: --skip-launcher and --headless both
 * skip the launcher (launcher.cpp).  It is checked by hand.
 *
 * KAROO_LAUNCHERDLG_FX=allaspect is a negative control: the device dialog
 * lists every aspect ratio instead of 4:3 only. */

#include <windows.h>
#include <mmsystem.h>
#include <ddraw.h>
#include <d3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "launcherdialogs.h"
#include "launcher.h"
#include "com_proxy.h"
#include "gameglobals.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "resources.h"

static const int IDC_PLAY       = 0x3f5;     // "spielen"
static const int IDC_SETUP      = 0x3f2;     // "setup"; the same id as the mode combo
static const int IDC_QUIT       = IDCANCEL;  // "ende"
static const int IDC_DRIVERS    = 0x3f0;
static const int IDC_HWCHECK    = 0x3f1;
static const int IDC_MODES      = 0x3f2;
static const int IDD_DEVICE     = 0x6e;
static const int IDR_REGION     = 0x6f;

static const char SND_SWITCH[] = "waves\\switch.wav";
static const char SND_IMPACT[] = "waves\\mineimpact.wav";
static const char SND_UGH[]    = "waves\\ugh.wav";

static BYTE *s_menuBmp;
static BYTE *s_playOff,  *s_playFoc;
static BYTE *s_setupOff, *s_setupFoc;
static BYTE *s_quitOff,  *s_quitFoc;
static int   s_modeCounter;

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

/* Reads a whole .bmp file; NULL unless the type is "BM" and the header's size
 * is the file's size.  Files over 4 GB are refused before allocating. */
static BYTE *load_bmp_file(const char *path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    DWORD hi = 0;
    DWORD size = GetFileSize(h, &hi);
    if (hi != 0) {
        CloseHandle(h);
        return NULL;
    }
    BYTE *buf = (BYTE *)malloc(size);
    if (!buf) {
        CloseHandle(h);
        return NULL;
    }
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, size, &got, NULL);
    CloseHandle(h);
    if (ok && got == size && *(WORD *)buf == 0x4d42  // "BM"
        && *(DWORD *)(buf + 2) == size)
        return buf;
    free(buf);
    return NULL;
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

static void starter_init(HWND hDlg)
{
    SetWindowTextA(hDlg, "Jumpin' John Starter");
    s_menuBmp  = load_bmp_file("bitmaps\\menu.bmp");
    s_playOff  = load_bmp_file("bitmaps\\spielen_off.bmp");
    s_playFoc  = load_bmp_file("bitmaps\\spielen_foc.bmp");
    s_setupOff = load_bmp_file("bitmaps\\setup_off.bmp");
    s_setupFoc = load_bmp_file("bitmaps\\setup_foc.bmp");
    s_quitOff  = load_bmp_file("bitmaps\\ende_off.bmp");
    s_quitFoc  = load_bmp_file("bitmaps\\ende_foc.bmp");

    // Centred, 400x400; SWP_NOZORDER makes HWND_TOPMOST moot.
    int y = GetSystemMetrics(SM_CYSCREEN) / 2 - 200;
    int x = GetSystemMetrics(SM_CXSCREEN) / 2 - 200;
    SetWindowPos(hDlg, HWND_TOPMOST, x, y, 400, 400, SWP_NOZORDER);

    MoveWindow(GetDlgItem(hDlg, IDC_PLAY),  0x47, 0x96,  0x106, 0x36, TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_SETUP), 0x47, 0xcb,  0x106, 0x35, TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_QUIT),  0x47, 0x101, 0x106, 0x32, TRUE);

    // The window shape: the RGN resource, scaled from its 150x163 design size
    // by what 100 dialog units come to on this system.
    RECT r = { 0, 0, 100, 100 };
    MapDialogRect(hDlg, &r);
    float sx = (float)r.right  * (1.0f / 150.0f);
    float sy = (float)r.bottom * (1.0f / 163.0f);
    GetClientRect(hDlg, &r);  // PRESERVED: result unused

    HRSRC res = FindResourceA(Resources_Module(), MAKEINTRESOURCEA(IDR_REGION), "RGN");
    HGLOBAL hg = LoadResource(Resources_Module(), res);
    if (!hg)
        return;
    const RGNDATA *rgn = (const RGNDATA *)LockResource(hg);
    if (rgn) {
        XFORM xf = { sx, 0.0f, 0.0f, sy, 0.0f, 0.0f };
        DWORD size = (rgn->rdh.nCount + 2) * 16;
        SetWindowRgn(hDlg, ExtCreateRegion(&xf, size, rgn), TRUE);
    }
    FreeResource(hg);
}

/* The link hot spot, in client pixels (unsigned 16-bit compares). */
static bool over_link(LPARAM lParam)
{
    WORD x = LOWORD(lParam), y = HIWORD(lParam);
    return x > 0x52 && x < 0x13e && y > 0x16a && y < 0x184;
}

static void open_device_dialog(HWND hDlg)
{
    hooks_DialogBoxParamA(Resources_Module(), MAKEINTRESOURCEA(IDD_DEVICE),
                          hDlg, LauncherDlg_DeviceSelectProc, 0);
}

extern "C" __declspec(dllexport) INT_PTR CALLBACK
LauncherDlg_Proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        starter_init(hDlg);
        return 0;

    case WM_DESTROY:
        // Freed, not cleared: WM_INITDIALOG reloads them all.
        free(s_playOff);  free(s_playFoc);
        free(s_setupOff); free(s_setupFoc);
        free(s_quitOff);  free(s_quitFoc);
        free(s_menuBmp);
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
                sndPlaySoundA(SND_IMPACT, SND_ASYNC | SND_NODEFAULT);
                EndDialog(hDlg, 1);
            }
            if (GetDlgItem(hDlg, IDC_QUIT) == focus) {
                sndPlaySoundA(SND_UGH, SND_NODEFAULT);
                EndDialog(hDlg, 0);
            }
            if (GetDlgItem(hDlg, IDC_SETUP) == focus) {
                sndPlaySoundA(SND_IMPACT, SND_ASYNC | SND_NODEFAULT);
                open_device_dialog(hDlg);
            }
            return 0;
        }
        if (id != IDC_PLAY && id != IDC_SETUP && id != IDC_QUIT)
            return 0;
        if (code == BN_CLICKED) {
            if (id == IDC_PLAY) {
                sndPlaySoundA(SND_IMPACT, SND_ASYNC | SND_NODEFAULT);
                EndDialog(hDlg, 1);
            } else if (id == IDC_SETUP) {
                sndPlaySoundA(SND_IMPACT, SND_ASYNC | SND_NODEFAULT);
                open_device_dialog(hDlg);
            } else {
                sndPlaySoundA(SND_UGH, SND_NODEFAULT);
                EndDialog(hDlg, 0);
            }
        } else if (code == BN_SETFOCUS) {
            sndPlaySoundA(SND_SWITCH, SND_ASYNC | SND_NODEFAULT);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
        SetCursor(LoadCursorA(NULL, over_link(lParam) ? IDC_HAND : IDC_ARROW));
        return 0;

    case WM_LBUTTONDOWN:
        if (over_link(lParam)) {
            // PRESERVED: the process handles are never closed.
            char cmd[] = "explorer.exe http:\\\\www.fakt-software.de";
            STARTUPINFOA si;
            PROCESS_INFORMATION pi;
            memset(&si, 0, sizeof(si));
            si.cb = sizeof(si);
            CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        }
        return 0;
    }
    return 0;
}

/* One combo entry per DirectDraw driver; its item data is a heap copy of the
 * GUID (NULL for the primary driver).  PRESERVED: the copies are never freed.
 */
static BOOL WINAPI driver_enum_cb(GUID *guid, LPSTR desc, LPSTR, LPVOID ctx)
{
    HWND combo = (HWND)ctx;
    LRESULT idx = SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)desc);
    if (idx == CB_ERR)
        return DDENUMRET_CANCEL;
    GUID *copy = NULL;
    if (guid) {
        copy = (GUID *)malloc(sizeof(GUID));
        if (!copy)
            return DDENUMRET_CANCEL;
        *copy = *guid;
    }
    SendMessageA(combo, CB_SETITEMDATA, idx, (LPARAM)copy);
    return DDENUMRET_OK;
}

struct ModeEnumCtx {
    HWND  combo;
    DWORD renderDepths;  // the HAL device's dwDeviceRenderBitDepth
};

/* Lists the modes the HAL device can render to, 4:3 only.  The item data is a
 * running count, which must agree with the device creation's own mode index.
 */
static HRESULT WINAPI mode_enum_cb(LPDDSURFACEDESC2 d, LPVOID ctxp)
{
    const ModeEnumCtx *ctx = (const ModeEnumCtx *)ctxp;
    DWORD w = d->dwWidth, h = d->dwHeight;
    DWORD bpp = d->ddpfPixelFormat.dwRGBBitCount;
    // Unsigned width over signed height, stored as float.  Only whether it
    // lies between 1.3 and 1.4 matters, so double precision is enough.
    float aspect = (float)((double)w / (double)(int)h);

    if (bpp == 32 && !(ctx->renderDepths & DDBD_32)) return DDENUMRET_OK;
    if (bpp == 24 && !(ctx->renderDepths & DDBD_24)) return DDENUMRET_OK;
    if (bpp == 16 && !(ctx->renderDepths & DDBD_16)) return DDENUMRET_OK;
    if (bpp < 16) return DDENUMRET_OK;
    if (!fx_allaspect() && !(aspect < 1.4f && aspect > 1.3f))
        return DDENUMRET_OK;

    char text[256];
    snprintf(text, sizeof(text), "%dx%dx%d", (int)w, (int)h, (int)bpp);
    LRESULT idx = SendMessageA(ctx->combo, CB_ADDSTRING, 0, (LPARAM)text);
    if (idx == CB_ERR)
        return DDENUMRET_CANCEL;
    SendMessageA(ctx->combo, CB_SETITEMDATA, idx, s_modeCounter);
    s_modeCounter++;
    return DDENUMRET_OK;
}

/* The mode list for the selected driver: create it (falling back to the
 * primary driver), find its HAL device, and enumerate the modes it can render.
 * Returns false where the dialog should fail.  PRESERVED: on that path the
 * interfaces already obtained are leaked. */
static bool fill_modes(HWND hDlg)
{
    LRESULT sel = SendDlgItemMessageA(hDlg, IDC_DRIVERS, CB_GETCURSEL, 0, 0);
    GUID *guid = (GUID *)SendDlgItemMessageA(hDlg, IDC_DRIVERS, CB_GETITEMDATA, sel, 0);

    LPDIRECTDRAW dd = NULL;
    if (FAILED(hooks_DirectDrawCreate(guid, &dd, NULL))
        && FAILED(hooks_DirectDrawCreate(NULL, &dd, NULL)))
        return false;
    IDirectDraw4 *dd4 = NULL;
    if (FAILED(dd->QueryInterface(IID_IDirectDraw4, (void **)&dd4)))
        return false;
    dd->Release();
    IDirect3D3 *d3d = NULL;
    if (FAILED(dd4->QueryInterface(IID_IDirect3D3, (void **)&d3d)))
        return false;

    D3DFINDDEVICESEARCH search;
    D3DFINDDEVICERESULT found;
    memset(&found, 0, sizeof(found));
    memset(&search, 0, sizeof(search));
    found.dwSize   = sizeof(found);
    search.dwSize  = sizeof(search);
    search.dwFlags = D3DFDS_GUID;
    search.guid    = IID_IDirect3DHALDevice;
    if (FAILED(d3d->FindDevice(&search, &found)))
        return false;
    // Without a HAL description the depths are zero, which lists no mode.
    DWORD depths = found.ddHwDesc.dwFlags ? found.ddHwDesc.dwDeviceRenderBitDepth : 0;
    d3d->Release();

    ModeEnumCtx ctx = { GetDlgItem(hDlg, IDC_MODES), depths };
    if (FAILED(dd4->EnumDisplayModes(0, NULL, &ctx, mode_enum_cb)))
        return false;
    dd4->Release();
    return true;
}

static bool device_init(HWND hDlg)
{
    SetWindowPos(hDlg, HWND_TOPMOST, 400, 300, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    // LoadLibrary, not GetModuleHandle: the executable does not import
    // ddraw.dll, so it may not be loaded yet.
    typedef HRESULT (WINAPI *enum_fn)(LPDDENUMCALLBACKA, LPVOID);
    enum_fn enumerate = (enum_fn)(void (*)(void))
        GetProcAddress(LoadLibraryA("ddraw.dll"), "DirectDrawEnumerateA");
    if (!enumerate
        || FAILED(enumerate(driver_enum_cb, GetDlgItem(hDlg, IDC_DRIVERS))))
        return false;

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

    // PRESERVED: the mode counter is reset only on a driver change, so opening
    // the dialog twice numbers the second list from where the first stopped.
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

extern "C" __declspec(dllexport) INT_PTR CALLBACK
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
        s_modeCounter = 0;
        if (!fill_modes(hDlg))
            return 0;
        SendDlgItemMessageA(hDlg, IDC_MODES, CB_SETCURSEL, 0, 0);
        SendDlgItemMessageA(hDlg, IDC_HWCHECK, BM_SETCHECK, BST_CHECKED, 0);
        break;
    }
    return 1;
}
