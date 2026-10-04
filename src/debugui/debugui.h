/* The debug UI: Dear ImGui over the game, drawn through RenderDevice.
 *
 * It draws as RenderDevice's overlay, after the game's frame and before it is
 * shown, so it needs no part of the game's rendering and leaves the state the
 * game had set as it found it.  The window side (events, F10) is windev's. */
#pragma once

class RenderDevice;

namespace debugui {

/* Whether the run asked for it: KAROO_DEBUGUI=1, or 2 to start with it
 * shown rather than waiting for F10. */
bool requested();

/* Makes the ImGui context and puts the UI on `dev`; false if it could not.
 * After RenderDevice::Create, because Create forgets the overlay. */
bool init(RenderDevice &dev);

/* Before the device is deleted. */
void shutdown(RenderDevice &dev);

}  // namespace debugui
