/* The diagnostic log, karoo_hooks.log in the run directory: our own messages,
 * each prefixed with the tick count.  Separate from the game's log
 * (gamelog.h). */

#pragma once

/* Does nothing until log_open; every line is flushed as it is written. */
void log_open(const char *path);
void log_close(void);
void log_write(const char *fmt, ...);
