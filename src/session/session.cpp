/* Session's game-file loader: the level names and the level count the play-through
 * reads from the game file.  Game_Boot calls it (core/gameboot.cpp). */

#include <string>
#include <stdio.h>
#include <string.h>

#include "session.h"
#include "gamestr.h"
#include "logger.h"
#include "sysdev.h"

/* "<gamedir>\<name>.gam", opened in text mode even for the binary form.
 * FORMAT: binary is a 6,6,6,<levels> header, then every byte minus 5 into the
 * flat level-name table.  Text is one name per line, up to a line starting
 * '*', which is stored and counted, then uncounted.  PRESERVED: the EOF test
 * comes before the read, so the last, failing read decodes the byte it left
 * behind a second time. */
int Session::loadGameFile(const char *gameDir, const char *name)
{
    char path[0x80] = { 0 };
    unsigned char hdr[4];

    levelCount_ = 0;
    snprintf(path, sizeof(path), GS_GAME_FILE_PATH, gameDir, name);
    g_logger.logMessage(2, "GAME: load game-file: %s", path);
    sysdev::TextFile in(path);
    if (!in)
        return 0;
    in.read(reinterpret_cast<char *>(hdr), 4);

    char *table = &levelNameTable_[0][0];
    if (hdr[0] == 6 && hdr[1] == 6 && hdr[2] == 6) {
        g_logger.logMessage(2, "GAME: load game-file as binary,coded file");
        levelCount_ = hdr[3];
        unsigned int n = 0;
        char c = 0;
        while (!in.eof()) {
            in.read(&c, 1);
            c = (char)(c - 5);
            table[n & 0xffff] = c;
            n++;
        }
        g_logger.logMessage(2, "GAME: game-file loaded %d levels included %d bytes loaded",
                           (unsigned)levelCount_, n & 0xffff);
        for (unsigned short i = 0; i < levelCount_; i++)
            g_logger.logMessage(2, "GAME: game-file: %s", levelNameTable_[i]);
        return 1;
    }

    g_logger.logMessage(2, "GAME: load game-file as text-file");
    unsigned int n = 0;
    in.clear();
    in.seekg(0);
    std::string line;
    while (!in.eof() && (line.empty() || line[0] != '*')) {
        if (!std::getline(in, line))
            line.clear();  // nothing read: an empty name
        strcpy(levelNameTable_[n & 0xffff], line.c_str());
        n++;
    }
    levelCount_ = (unsigned char)(n - 1);
    g_logger.logMessage(2, "GAME: game-file loaded %d levels included", (unsigned)levelCount_);
    return 1;
}
