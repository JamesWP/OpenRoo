/* Process-wide setup and teardown around the game: open our log, install the
 * exception logger and the test launcher's hooks at start; dump the end state
 * and close the log at exit. */
#pragma once

void Process_Attach(const char *log_name);
void Process_Detach(void);
