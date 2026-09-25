/* staticinit.h -- construction and destruction of the game's global objects.
 *
 * The original built these through its C++ initialiser table (0x00464004 ..
 * 0x00464074, run by its CRT's _initterm before WinMain) and registered each
 * destructor with atexit, so they ran in reverse after WinMain returned.
 * The objects are ours now (tools/gamedata.txt), so their lifetime is too:
 * Main_WinMain calls StaticInit_Construct first and StaticInit_Destruct on
 * the way out.  patch.py clears the original table's slots.
 */
#pragma once

void StaticInit_Construct();
void StaticInit_Destruct();
