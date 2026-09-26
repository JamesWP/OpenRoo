/* process.h -- process-wide setup and teardown for our code.
 *
 * What karoo_hooks.dll's DllMain did on attach and detach: open the log,
 * install the VEH, the CRT probe, the launcher/auto-exit setup; then the
 * end-of-run state dump.  dllmain.cpp calls these for the DLL build and
 * exemain.cpp for our own executable, so both run the same code.
 */
#pragma once

void Process_Attach(const char *log_name);
void Process_Detach(void);
