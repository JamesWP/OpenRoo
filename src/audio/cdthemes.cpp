/* GAMETICK_PLAN.md Band B reopened — theme name -> theme index.
 *
 *   AutoClass5::FindThemeIndexByThemeName  0x00403240   5 E8 sites
 *     0x0040337F (theme loader), 0x00414FA0 (GameTick), 0x0041626A,
 *     0x0041639C, 0x0041A310 -- every one stores AL and nothing more.
 *
 * __thiscall(AutoClass5*, const char *name), RET 4.  Its only callee is the
 * CRT's _strlwr (0x004505b7 -- it LOWERcases, the old plate comment said
 * "uppercases").  _strlwr's locale branch is taken only when the code page
 * global at 0x004e0950 is non-zero; see the note on it below.
 *
 * Listing, in order:
 *   - the name is copied (unbounded) into a 128-byte buffer and lowercased;
 *   - for i in 0..count-1 (count = byte +0x11c, re-read every iteration):
 *       copy entry +0x11d + i*0xff into a second 128-byte buffer, lowercase
 *       it, read the result byte +0x1c+i BEFORE comparing, strcmp;
 *       equal -> return that byte;
 *   - no match -> AL = 0, indistinguishable from theme 0.
 * Both copies are unbounded into 128 bytes, a stack overrun for a name of
 * 128+ characters.  Here the buffers are the same size and the copies
 * equally unbounded, so the overrun is preserved in kind (it lands in our
 * frame rather than the original's).
 *
 * Control: KAROO_SIM_FX=themeoff -- a match returns the NEXT entry's index
 * byte, so every level loads its neighbour theme's index.
 */
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
#include "alloc.h"

static int s_fx = -1;

/* _strlwr's C-locale path: 'A'..'Z' tested as SIGNED chars, +0x20. */
static void crt_strlwr(char *p)
{
    for (; *p != '\0'; ++p)
        if ('@' < *p && *p < '[')
            *p = (char)(*p + ' ');
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_FindThemeIndexByThemeName(CdThemes *self, const char *name)
{
    return self->findThemeIndex(name);
}

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

/* ─── AutoClass5::PlayCDStuf 0x00403360 / PlayCDStuf_2 0x004033e0 ─────────
 *
 * The CD-music pair.  Both were listed in GAMETICK_PLAN.md's "already ours"
 * table as cdm.cpp -- they are NOT: patch.py never took them.  cdm.cpp owns
 * the CDM class they call (Stop 0x00402d50 and PlayTrack 0x00402cb0, both
 * exported there); these two AutoClass5 wrappers stayed in the binary.
 *
 * PlayCDStuf(this, caption), __thiscall RET 4, 8 E8 sites:
 *   count byte +0x11c == 0 -> return 0 (EAX cleared);
 *   CDM::Stop(&CdAudio 0x004dc640);
 *   +0x11b = FindThemeIndexByThemeName(caption);
 *   Log_Message(2, "CDM: trying to play track %d caption:%s ", idx, caption)
 *     -- the format is the game's own .rdata string, passed by address;
 *   if +0x11b != 0: CDM::PlayTrack(+0x11b, 1);  return 1.
 * PlayCDStuf_2(this), __thiscall bare RET, 2 E8 sites: the same minus the
 * lookup and log -- replays the remembered track; always returns 0.
 */
struct CDM;
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CDM_StopTrack(CDM *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CDM_PlayTrack(CDM *self, int track, bool loop);


extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_PlayCDStuf(CdThemes *self, const char *caption)
{
    return self->play(caption);
}

unsigned int CdThemes::play(const char *caption)
{
    unsigned int idx;

    if (count_ == 0)
        return 0;
    CDM_StopTrack(GG_CDAUDIO);
    idx = findThemeIndex(caption);
    currentTrack_ = (unsigned char)idx;
    GameLog_LogMessage(GG_LOGGER, 2, GS_CD_TRY_TRACK, idx & 0xff, caption);
    if (currentTrack_ != 0)
        CDM_PlayTrack(GG_CDAUDIO, currentTrack_, true);
    return 1;
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_PlayCDStuf_2(CdThemes *self)
{
    return self->replay();
}

unsigned int CdThemes::replay()
{
    if (count_ != 0) {
        CDM_StopTrack(GG_CDAUDIO);
        if (currentTrack_ != 0)
            CDM_PlayTrack(GG_CDAUDIO, currentTrack_, true);
    }
    return 0;
}

/* ─── ValidateCDTrackLengths 0x00403420 ──────────────────────────────────
 *
 * __fastcall, `this` in ECX, bare RET; 4 E8 sites (Game::Load 0x414817,
 * SetupLevelObjects 0x41706B, WinMain 0x42D252/0x42D288) -- all rerouted
 * by patch.py, the original UD2-stubbed.  Brought in at James's request
 * (the callback census of 2026-09-14).  Transcribed from the LISTING:
 *
 *   n = CDM::GetTrackCount(&CdAudio);  this->trackCount = n   (stored first)
 *   if n != 9: return 0
 *   TrackLength(&s, 1)                  -- fetched, NEVER compared
 *   for t in 2..9: TrackLength(&s, t); if strcmp(s, expected[t]) != 0:
 *                  return 0
 *   return 1
 *
 * The expected strings are the game's own .rdata, read in place (DATA
 * reads, not calls).  The inline compare is a byte strcmp whose only use
 * is == 0, so strcmp is exact.  Track 1's read is kept: it is a call into
 * the CD driver and its order is observable to it.
 */
static const char *const k_trackLength[10] = {
    NULL, NULL,
    GS_CD_TRACK2_LEN, GS_CD_TRACK3_LEN,
    GS_CD_TRACK4_LEN, GS_CD_TRACK5_LEN,
    GS_CD_TRACK6_LEN, GS_CD_TRACK7_LEN,
    GS_CD_TRACK8_LEN, GS_CD_TRACK9_LEN,
};

int CdThemes::validateTrackLengths()
{
    char *len;

    trackCount_ = CDM_GetTrackCount(GG_CDAUDIO);
    if (trackCount_ != 9)
        return 0;
    CDM_GetTrackLength(GG_CDAUDIO, &len, 1);
    for (int t = 2; t <= 9; ++t) {
        CDM_GetTrackLength(GG_CDAUDIO, &len, t);
        if (strcmp(len, k_trackLength[t]) != 0)
            return 0;
    }
    return 1;
}

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_ValidateCDTrackLengths(CdThemes *self)
{
    return self->validateTrackLengths();
}

/* ─── Lifecycle — 0x403000 / 0x403030 / 0x403010 ────────────────────────────
 *
 * Three functions that do nothing but move the vtable pointer, and that is
 * the whole class's construction: Game::Load 0x004145f4 calls the ctor on
 * Game+0x2223f and no field below +0x18 is ever written again.  The theme
 * table is left uninitialised until ReadCdTrackThemeTable fills it — a defect kept,
 * not a gap in this reimplementation.
 */
void CdThemes::construct()
{
    vtable_ = const_cast<void*>(CDTHEMES_VTABLE);
}

void CdThemes::destruct()
{
    vtable_ = const_cast<void*>(CDTHEMES_VTABLE);
}

extern "C" __declspec(dllexport) CdThemes * __attribute__((thiscall))
Sim_CdThemesConstruct(CdThemes *self)
{
    self->construct();
    return self;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_CdThemesDestruct(CdThemes *self)
{
    self->destruct();
}

/* Vtable slot 0.  The only CdThemes is Game's embedded member, so the free
   branch never runs; kept faithful, and the block would be the game heap's. */
extern "C" __declspec(dllexport) CdThemes * __attribute__((thiscall))
Sim_CdThemesScalarDeletingDtor(CdThemes *self, unsigned int flags)
{
    self->destruct();
    if (flags & 1)
        game_free2(self);
    return self;
}

/* ─── ReadCdTrackThemeTable 0x403040 (thiscall, RET 4) ───────────────────
 *
 * Opens the .cdt with the original's mode "r+t" (text, and -- needlessly --
 * writable).  Defects kept:
 *   - the line buffer starts empty (a copy of the never-written empty
 *     string at 0x46c290) and is parsed even when the first fgets reads
 *     nothing;
 *   - the EOF test comes before fgets, so a file ending in a newline has one
 *     failed fgets whose unchanged buffer is parsed again: strtok resumes
 *     after the first token's NUL and the LAST THEME IS ADDED TWICE;
 *   - the newline chop drops the last character unconditionally (a line cut
 *     at 0xff chars loses a real one), and runs on an empty buffer's [-1];
 *   - the index is a byte and unchecked: a 256th theme stores its track in
 *     currentTrack_ (the byte after trackOf_) and its name one row past
 *     names_, beyond the object.  Kept; no shipped .cdt comes close.
 * The missing-file path zeroes all 255 names (0xfe01 bytes) and returns 0
 * without touching count_ past the initial zero. */
extern "C" __declspec(dllexport) unsigned char __attribute__((thiscall))
Sim_ReadCdTrackThemeTable(CdThemes *self, const char *name)
{
    return self->readTrackThemeTable(name);
}

unsigned char CdThemes::readTrackThemeTable(const char *name)
{
    char path[256];
    char line[256];
    char delims[8];

    sprintf(path, GS_CD_TRACKFILE_PATH, GS_GAME_DIR, name);
    memcpy(delims, GS_CD_TRACKFILE_DELIMS, 6);
    unsigned char n = 0;
    FILE *fp = fopen(path, GS_CD_TRACKFILE_MODE);
    count_ = 0;
    line[0] = '\0';   /* the original strcpy's the empty .data string 0x46c290 */

    if (fp == NULL) {
        GameLog_LogMessage(GG_LOGGER, 3, GS_CD_TRACKFILE_MISSING, path);
        memset(names_, 0, sizeof(names_));
        return 0;
    }
    GameLog_LogMessage(GG_LOGGER, 2, GS_CD_TRACKFILE_FOUND, path);
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
            currentTrack_ = track;       /* trackOf_[255] in the original */
        char *dst = names_[n];
        strcpy(dst, tok);
        GameLog_LogMessage(GG_LOGGER, 2, GS_CD_THEME_TRACK, dst, (unsigned)track);
        n++;
    }
    count_ = n;
    fclose(fp);
    return count_;
}

/* ─── ListTrackLengths 0x4036f0 (thiscall, RET 0) ───────────────────────── */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_ListTrackLengths(CdThemes *self)
{
    return self->listTrackLengths();
}

int CdThemes::listTrackLengths()
{
    char *len = NULL;   /* always written by CDM_GetTrackLength before use */
    trackCount_ = CDM_GetTrackCount(GG_CDAUDIO);
    GameLog_LogMessage(GG_LOGGER, 3, GS_CD_TRACK_COUNT, trackCount_);
    for (unsigned t = 1; (unsigned)trackCount_ != 0; t++) {
        CDM_GetTrackLength(GG_CDAUDIO, &len, t);
        GameLog_LogMessage(GG_LOGGER, 3, GS_CD_TRACK_LENGTH, t, len);
        if (!(t < (unsigned)trackCount_))
            break;
    }
    return 1;
}
