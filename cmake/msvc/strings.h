/* MSVC has no <strings.h>: the case-insensitive comparisons live in <string.h>
 * under other names.  Found only on MSVC builds (see CMakeLists.txt). */
#pragma once
#include <string.h>
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
