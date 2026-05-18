#pragma once

void log_open(const char *path);
void log_close(void);
void log_write(const char *fmt, ...);
