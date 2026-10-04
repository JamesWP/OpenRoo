/* The platform system layer: clocks, the local date and time, the executable's
 * directory and the crash logger.  The only code that may call the system's own
 * timing, time-of-day and exception APIs; the header is opaque and free of
 * platform headers, so a port replaces the .cpp files beside it. */
#pragma once

#include <stddef.h>
#include <sstream>
#include <string>

namespace sysdev {

/* Where the layer reports problems.  printf-style; may stay unset. */
typedef void (*LogFn)(const char *fmt, ...);
void setLog(LogFn fn);

/* Milliseconds since the system started; wraps after about 49 days. */
unsigned tickMs();

/* Milliseconds from the multimedia timer: the same scale as tickMs but a
 * finer resolution where the system offers one. */
unsigned timerMs();

/* Gives the processor away for about this long. */
void sleepMs(unsigned ms);

/* A high-resolution counter and its ticks per second. */
unsigned long long perfCounter();
unsigned long long perfFrequency();

struct LocalTime {
    int year, month, day, hour, minute, second;
};
LocalTime localTime();

/* Copies the environment variable name into buf.  Returns its length without
 * the terminator, or 0 if it is unset; if buf is too small, returns the size
 * needed including the terminator and leaves buf unspecified. */
unsigned getEnv(const char *name, char *buf, unsigned size);

/* The environment variable name, or an empty string if it is unset or empty. */
std::string getEnv(const char *name);

/* The directory the executable lives in, with a trailing separator, or an
 * empty string if it is unknown. */
std::string executableDir();

/* ── File names ──
 * The game names its files the way the original did: relative to the game
 * directory, with backslash separators and the case the original shipped
 * (which is not the case on disk).  nativePath turns such a name into one the
 * host's file calls open.  Pass every game file name through it, to read and
 * to write; a file that does not exist yet keeps its last component as given. */
std::string nativePath(const char *path);
inline std::string nativePath(const std::string &path) { return nativePath(path.c_str()); }

/* A game file read the way the original read it, in text mode: each CR LF
 * pair arrives as one LF and a Ctrl-Z ends the file.  Several of the game's
 * file formats depend on it, binary data included.  Used like a std::ifstream
 * (test it for failure, then read from it); the file is read whole on opening.
 * Pass the game's name for the file, not nativePath's. */
class TextFile : public std::istringstream {
public:
    explicit TextFile(const char *path);
    bool is_open() const { return open_; }
private:
    bool open_;
};

/* The platform half of TextFile: the file's contents with text-mode
 * translation; false if it cannot be read. */
bool readTextFile(const char *path, std::string &contents);

/* strcmp and strncmp ignoring ASCII case. */
int compareNoCase(const char *a, const char *b);
int compareNoCase(const char *a, const char *b, size_t n);

}  // namespace sysdev
