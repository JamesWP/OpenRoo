/* Construction and destruction of the game's global objects.  Main_WinMain
 * calls StaticInit_Construct first and StaticInit_Destruct on the way out, so
 * they live exactly as long as the game runs. */
#pragma once

void StaticInit_Construct();
void StaticInit_Destruct();
