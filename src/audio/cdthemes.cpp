/* KAROO_SIM_FX=themeoff is a negative control: a match returns the next
 * theme's track, so every level gets its neighbour's music. */
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "log.h"
#include "gamelog.h"
#include "cdthemes.h"
#include "cdm.h"
#include "gamestr.h"
#include "gameglobals.h"
#include <stdlib.h>

static int s_fx = -1;

/* The C-locale lowercase: 'A'..'Z' compared as signed chars. */
static void crt_strlwr(char *p)
{
    for (; *p != '\0'; ++p)
        if ('@' < *p && *p < '[')
            *p = (char)(*p + ' ');
}

unsigned int Sim_FindThemeIndexByThemeName(CdThemes *self, const char *name)
{
    return self->findThemeIndex(name);
}

/* PRESERVED: both names are copied unbounded into 128-byte buffers.  The
 * result is read before the compare. */
unsigned int CdThemes::findThemeIndex(const char *name)
{
    char want[128];
    char have[128];

    if (s_fx < 0) {
        char b[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", b, sizeof(b));
        s_fx = (n > 0 && n < sizeof(b) && strcmp(b, "themeoff") == 0);
        if (s_fx)
            log_write("themeindex: KAROO_SIM_FX=themeoff -- neighbour index\n");
    }

    strcpy(want, name);
    crt_strlwr(want);
    for (int i = 0; i < (int)count_; ++i) {
        strcpy(have, names_[i]);
        crt_strlwr(have);
        unsigned char result = trackOf_[i + (s_fx ? 1 : 0)];
        if (strcmp(want, have) == 0)
            return result;
    }
    return 0;
}

struct CDM;
void CDM_StopTrack(CDM *self);
void CDM_PlayTrack(CDM *self, int track, bool loop);

unsigned int Sim_PlayCDStuf(CdThemes *self, const char *caption)
{
    return self->play(caption);
}

unsigned int CdThemes::play(const char *caption)
{
    unsigned int idx;

    if (count_ == 0)
        return 0;
    CDM_StopTrack(&g_cdAudio);
    idx = findThemeIndex(caption);
    currentTrack_ = (unsigned char)idx;
    GameLog_LogMessage(&g_logger, 2, GS_CD_TRY_TRACK, idx & 0xff, caption);
    if (currentTrack_ != 0)
        CDM_PlayTrack(&g_cdAudio, currentTrack_, true);
    return 1;
}

unsigned int Sim_PlayCDStuf_2(CdThemes *self)
{
    return self->replay();
}

unsigned int CdThemes::replay()
{
    if (count_ != 0) {
        CDM_StopTrack(&g_cdAudio);
        if (currentTrack_ != 0)
            CDM_PlayTrack(&g_cdAudio, currentTrack_, true);
    }
    return 0;
}

static const char *const k_trackLength[10] = {
    NULL, NULL,
    GS_CD_TRACK2_LEN, GS_CD_TRACK3_LEN,
    GS_CD_TRACK4_LEN, GS_CD_TRACK5_LEN,
    GS_CD_TRACK6_LEN, GS_CD_TRACK7_LEN,
    GS_CD_TRACK8_LEN, GS_CD_TRACK9_LEN,
};

/* PRESERVED: track 1's length is fetched but never compared. */

int CdThemes::validateTrackLengths()
{
    char *len;

    trackCount_ = CDM_GetTrackCount(&g_cdAudio);
    if (trackCount_ != 9)
        return 0;
    CDM_GetTrackLength(&g_cdAudio, &len, 1);
    for (int t = 2; t <= 9; ++t) {
        CDM_GetTrackLength(&g_cdAudio, &len, t);
        if (strcmp(len, k_trackLength[t]) != 0)
            return 0;
    }
    return 1;
}

int Sim_ValidateCDTrackLengths(CdThemes *self)
{
    return self->validateTrackLengths();
}

/* Construction and destruction only set the vtable.  PRESERVED: the theme
 * table is left uninitialised until it is read. */
void CdThemes::construct()
{
    vtable_ = const_cast<void*>(CDTHEMES_VTABLE);
}

void CdThemes::destruct()
{
    vtable_ = const_cast<void*>(CDTHEMES_VTABLE);
}

CdThemes *Sim_CdThemesConstruct(CdThemes *self)
{
    self->construct();
    return self;
}

void Sim_CdThemesDestruct(CdThemes *self)
{
    self->destruct();
}

/* Frees on bit 0; the only CdThemes is the Game's, so it never does. */
CdThemes *Sim_CdThemesScalarDeletingDtor(CdThemes *self, unsigned int flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}

/* Opened in text mode, and needlessly writable.  PRESERVED:
 *   - the line buffer starts empty and is parsed even when the first read
 *     gets nothing;
 *   - the end-of-file test comes before the read, so a file ending in a
 *     newline has one failed read whose buffer is parsed again, and the
 *     last theme is added twice;
 *   - the newline chop drops the last character unconditionally;
 *   - the index is a byte and unchecked: a 256th theme's track lands in
 *     currentTrack_ and its name past the object.  No shipped file comes
 *     close.
 * A missing file zeroes all 255 names and returns 0. */
unsigned char Sim_ReadCdTrackThemeTable(CdThemes *self, const char *name)
{
    return self->readTrackThemeTable(name);
}

unsigned char CdThemes::readTrackThemeTable(const char *name)
{
    char path[256];
    char line[256];
    char delims[8];

    sprintf(path, GS_CD_TRACKFILE_PATH, g_gameDir, name);
    memcpy(delims, GS_CD_TRACKFILE_DELIMS, 6);
    unsigned char n = 0;
    FILE *fp = fopen(path, GS_CD_TRACKFILE_MODE);
    count_ = 0;
    line[0] = '\0';

    if (fp == NULL) {
        GameLog_LogMessage(&g_logger, 3, GS_CD_TRACKFILE_MISSING, path);
        memset(names_, 0, sizeof(names_));
        return 0;
    }
    GameLog_LogMessage(&g_logger, 2, GS_CD_TRACKFILE_FOUND, path);
    while (!feof(fp)) {
        fgets(line, 0x100, fp);
        if (!feof(fp))
            line[strlen(line) - 1] = '\0';
        char *tok = strtok(line, delims);
        if (tok == NULL)
            continue;
        unsigned char track = (unsigned char)atoi(tok);
        tok = strtok(NULL, delims);
        if (tok == NULL)
            continue;
        if (n < THEME_MAX)
            trackOf_[n] = track;
        else
            currentTrack_ = track;  // PRESERVED: trackOf_[255], one past the array
        char *dst = names_[n];
        strcpy(dst, tok);
        GameLog_LogMessage(&g_logger, 2, GS_CD_THEME_TRACK, dst, (unsigned)track);
        n++;
    }
    count_ = n;
    fclose(fp);
    return count_;
}

int Sim_ListTrackLengths(CdThemes *self)
{
    return self->listTrackLengths();
}

int CdThemes::listTrackLengths()
{
    char *len = NULL;  // always written before it is used
    trackCount_ = CDM_GetTrackCount(&g_cdAudio);
    GameLog_LogMessage(&g_logger, 3, GS_CD_TRACK_COUNT, trackCount_);
    for (unsigned t = 1; (unsigned)trackCount_ != 0; t++) {
        CDM_GetTrackLength(&g_cdAudio, &len, t);
        GameLog_LogMessage(&g_logger, 3, GS_CD_TRACK_LENGTH, t, len);
        if (!(t < (unsigned)trackCount_))
            break;
    }
    return 1;
}

static void *const cdthemes_vtable_slots[1] = {
    (void *)&Sim_CdThemesScalarDeletingDtor,
};
extern const void *const CDTHEMES_VTABLE = cdthemes_vtable_slots;
