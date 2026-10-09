#pragma once
#include <stdint.h>

/* An adapter's identity, opaque to game code: 16 bytes that name the same
 * display from run to run.  The launcher stores it in the config file as it
 * is. */
struct AdapterId {
    uint8_t bytes[16];
};
