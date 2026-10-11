#pragma once

/* A sound asset's file name and, immediately after it, its enabled flag.
 * The spawn's unbounded strcpy of `name` relies on the flag to stop it. */
struct SoundAssetName {
    char name[256]{};
    int  enabled{};
};
