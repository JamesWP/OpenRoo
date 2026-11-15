/* The windows' icon, private to src/windev. */
#pragma once
#include <SDL3/SDL.h>
#include <string.h>
#include "launcher_layout.h"

namespace windev {

/* Gives window the program's icon (data/openroo.ico, 48x48).  On macOS that
 * sets the Dock's icon for the whole app, so it is left alone when running
 * from OpenRoo.app (tools/make_macos_app.py), whose bundle's icon is sharper. */
inline void set_app_icon(SDL_Window *window)
{
#ifdef __APPLE__
    const char *base = SDL_GetBasePath();  // .../OpenRoo.app/Contents/Resources/
    if (base && strstr(base, ".app/Contents/"))
        return;
#endif
    SDL_Surface *icon = SDL_CreateSurfaceFrom(LAUNCHER_ICON_SIZE, LAUNCHER_ICON_SIZE,
                                              SDL_PIXELFORMAT_RGBA32,
                                              (void *)launcher_icon_rgba,
                                              LAUNCHER_ICON_SIZE * 4);
    if (icon) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }
}

}  // namespace windev
