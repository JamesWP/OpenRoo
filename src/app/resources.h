/* resources.h -- the module that holds the game's resources.
 *
 * The original loaded its dialogs, icon and launcher region from its own
 * image (hInstance / ModuleInstanceGlobal).  They are now compiled into our
 * binary from karoo.rc, so every resource load names this module instead.
 * When our code becomes the executable this is simply that executable.
 */
#pragma once
#include <windows.h>

HMODULE Resources_Module();
