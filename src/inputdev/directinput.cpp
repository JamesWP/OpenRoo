#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <string.h>
#include "inputdev.h"

namespace inputdev {

static LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }
#define ID_LOG(...) do { if (g_log) g_log(__VA_ARGS__); } while (0)

short asyncKeyState(int virtualKey)
{
    return GetAsyncKeyState(virtualKey);
}

struct DevicesState {
    LPDIRECTINPUT8A       directinput;
    LPDIRECTINPUTDEVICE8A keyboard;
    LPDIRECTINPUTDEVICE8A mouse;
};

Devices::Devices() : state_(new DevicesState())
{
}

Devices::~Devices()
{
    destroy();
    delete state_;
}

void Devices::destroy()
{
    DevicesState *s = state_;
    if (s->keyboard) { s->keyboard->Unacquire(); s->keyboard->Release(); s->keyboard = NULL; }
    if (s->mouse)    { s->mouse->Unacquire();    s->mouse->Release();    s->mouse    = NULL; }
    if (s->directinput) { s->directinput->Release(); s->directinput = NULL; }
}

/* Creates one device on the window, in the given data format. */
static bool create_device(DevicesState *s, const char *what, REFGUID guid,
                          LPCDIDATAFORMAT format, HWND window,
                          LPDIRECTINPUTDEVICE8A *out)
{
    HRESULT hr = s->directinput->CreateDevice(guid, out, NULL);
    if (FAILED(hr)) {
        ID_LOG("inputdev: %s CreateDevice FAILED hr=0x%08lx\n", what, hr);
        return false;
    }
    hr = (*out)->SetDataFormat(format);
    if (FAILED(hr)) {
        ID_LOG("inputdev: %s SetDataFormat FAILED hr=0x%08lx\n", what, hr);
        goto fail;
    }
    hr = (*out)->SetCooperativeLevel(window, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) {
        ID_LOG("inputdev: %s SetCooperativeLevel FAILED hr=0x%08lx\n", what, hr);
        goto fail;
    }
    return true;
fail:
    (*out)->Release();
    *out = NULL;
    return false;
}

bool Devices::create(void *window)
{
    destroy();
    DevicesState *s = state_;

    HRESULT hr = DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION,
                                    IID_IDirectInput8A,
                                    reinterpret_cast<void**>(&s->directinput), NULL);
    if (FAILED(hr)) {
        ID_LOG("inputdev: DirectInput8Create FAILED hr=0x%08lx\n", hr);
        s->directinput = NULL;
        return false;
    }
    if (!create_device(s, "keyboard", GUID_SysKeyboard, &c_dfDIKeyboard,
                       (HWND)window, &s->keyboard)
        || !create_device(s, "mouse", GUID_SysMouse, &c_dfDIMouse,
                          (HWND)window, &s->mouse)) {
        destroy();
        return false;
    }
    return true;
}

bool Devices::acquire()
{
    if (state_->keyboard) {
        HRESULT hr = state_->keyboard->Acquire();
        if (FAILED(hr)) {
            ID_LOG("inputdev: keyboard Acquire FAILED hr=0x%08lx\n", hr);
            return false;
        }
    }
    if (state_->mouse) state_->mouse->Acquire();
    return true;
}

void Devices::unacquire()
{
    if (state_->keyboard) state_->keyboard->Unacquire();
    if (state_->mouse)    state_->mouse->Unacquire();
}

bool Devices::readKeyboard(unsigned char keys[256])
{
    LPDIRECTINPUTDEVICE8A kb = state_->keyboard;
    if (!kb) return false;
    HRESULT hr = kb->GetDeviceState(256, keys);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        kb->Acquire();
        hr = kb->GetDeviceState(256, keys);
    }
    return SUCCEEDED(hr);
}

bool Devices::keyName(int scanCode, char *buf, unsigned bufsz)
{
    if (!state_->keyboard || !buf || bufsz == 0) return false;
    DIDEVICEOBJECTINSTANCEA doi;
    doi.dwSize = sizeof(doi);
    HRESULT hr = state_->keyboard->GetObjectInfo(&doi, (DWORD)scanCode, DIPH_BYOFFSET);
    if (FAILED(hr)) {
        ID_LOG("inputdev: GetObjectInfo sc=0x%02X FAILED hr=0x%08lx\n", scanCode, hr);
        return false;
    }
    strncpy(buf, doi.tszName, bufsz - 1);
    buf[bufsz - 1] = '\0';
    return true;
}

bool Devices::setControllerRange(int, int, int)
{
    return true;
}

bool Devices::setControllerDeadzone(int, int)
{
    return true;
}

}  // namespace inputdev
