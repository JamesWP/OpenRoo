/* A vectored exception handler that logs access violations in our own code
 * (registers and the top of the stack) and guard-page faults, then lets the
 * exception continue to the normal handlers. */

#pragma once

void install_veh(void);
