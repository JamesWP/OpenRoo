#pragma once
#include <stdint.h>

/* A sound asset's file name and, immediately after it, its enabled flag.
 * The spawn's unbounded strcpy of `name` relies on the flag to stop it. */
struct SoundAssetName {
    char name[256]{};
    int  enabled{};
    uint32_t unknown104{};   /* add()'s arg4; the .thm path passes 1 */
    uint32_t unknown108{};   /* add()'s arg3; the .thm path passes 1 */
};
