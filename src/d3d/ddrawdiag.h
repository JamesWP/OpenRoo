/* KAROO_DDRAW_DIAG: capture what the real driver answers.
 *
 * The headless null device (nullddraw.cpp) has to present the game with the
 * same DirectDraw/Direct3D environment stock Wine does, or the texture format
 * choice, the mode list and the device caps all shift underneath code that
 * reads them -- and a replay would then diverge for reasons that have nothing
 * to do with the change under test.
 *
 * So the null device's tables are not guessed: the backend logs the real
 * answers at each call that obtains them, and nullddraw.cpp replays them.
 * Set KAROO_DDRAW_DIAG=1 and read the log.  Read by value, never by
 * presence. */

#pragma once
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>

bool ddiag_on(void);

/* A pixel format, one line. */
void ddiag_pixfmt(const char *tag, const DDPIXELFORMAT *pf);

/* One enumerated display mode. */
void ddiag_mode(const DDSURFACEDESC2 *d);

/* The HAL FindDevice result (first call only). */
void ddiag_find_device(HRESULT hr, const D3DFINDDEVICERESULT *result);

/* The device's HAL/HEL descriptions (first call only). */
void ddiag_device_caps(HRESULT hr, const void *hal, const void *hel);

/* A CreateSurface request and its result. */
void ddiag_create_surface(HRESULT hr, const DDSURFACEDESC2 *d);

/* A GetSurfaceDesc / Lock answer (first eight of each). */
void ddiag_surface_desc(HRESULT hr, const DDSURFACEDESC2 *d);
void ddiag_lock(HRESULT hr, DWORD flags, const DDSURFACEDESC2 *d);

/* KAROO_TEXTURE_DUMP=<file>: one line per texture or image surface the loaders
 * produce -- kind, name, size, pixel format and an FNV-1a hash of the pixel
 * bytes (rows without pitch padding).  Diffing two runs' sorted files shows
 * whether a change to the loaders moved a single pixel. */
void ddiag_dump_surface(const char *kind, const char *name,
                        IDirectDrawSurface4 *surf);
