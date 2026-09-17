/* gamestr.h -- the game's own string bytes, named once.
 *
 * Every string this DLL hands to the game's sprintf/strcmp/logger is the
 * game's own bytes, at its own address, not a literal of ours: the game
 * compares some of them by pointer, and a literal would be a different
 * pointer with the same characters.  That practice is deliberate and does
 * not change here.  What changes is that each address is spelled once.
 *
 * Before this header, 237 sites re-#defined 221 addresses across 22 files,
 * with the meaning only in a trailing comment; three addresses carried two
 * names in different files ("%s", "completed", the InstructionScripts
 * path), six more carried one name in two files, and the game directory
 * buffer had seven definitions under two names.  A name defined twice is a
 * name that can drift.
 *
 * Names are GS_<GROUP>_<WHAT>.  The group is the subsystem the string
 * belongs to, which is also how the game's .data lays them out; the rest
 * is the name the owning file had already chosen, since whoever read that
 * code named it from its use.  Comments carry the bytes themselves --
 * that is the provenance, and it stays.
 *
 * These are typed constants rather than macros so a misuse is a type
 * error, and rather than `extern` so nothing is emitted: GCC folds an
 * integer-to-pointer cast in a static initialiser to an immediate, even
 * at -O0, and does not warn about an unused one.
 *
 * COHESION_PLAN.md Band 7b.  Addresses only -- no expression and no
 * behaviour changes, so patch.py and Karoo.exe are untouched.
 */
#ifndef KAROO_GAMESTR_H
#define KAROO_GAMESTR_H

/* --- ANI: ani.cpp -- the animation loader --- */
static const char *const GS_ANI_LOADED                    = (const char *)0x004640b0;  /* "ANI: %s loaded" */

/* --- FMT: shared one-conversion formats, used by several files --- */
static const char *const GS_FMT_S                         = (const char *)0x004641f8;  /* "%s" */
static const char *const GS_FMT_NEWLINE                   = (const char *)0x00465160;  /* "\n" */
static const char *const GS_FMT_TAB                       = (const char *)0x00465e00;  /* "\t" */
static const char *const GS_FMT_D                         = (const char *)0x004668b8;  /* "%d" */

/* --- CD: cdthemes.cpp -- the CD player --- */
static const char *const GS_CD_TRY_TRACK                  = (const char *)0x00464398;  /* "CDM: trying to play track %d caption:%s " */
static const char *const GS_CD_TRACK9_LEN                 = (const char *)0x004643c4;  /* "02:17:74" */
static const char *const GS_CD_TRACK8_LEN                 = (const char *)0x004643d0;  /* "04:00:74" */
static const char *const GS_CD_TRACK7_LEN                 = (const char *)0x004643dc;  /* "03:32:60" */
static const char *const GS_CD_TRACK6_LEN                 = (const char *)0x004643e8;  /* "03:08:54" */
static const char *const GS_CD_TRACK5_LEN                 = (const char *)0x004643f4;  /* "02:46:68" */
static const char *const GS_CD_TRACK4_LEN                 = (const char *)0x00464400;  /* "03:28:39" */
static const char *const GS_CD_TRACK3_LEN                 = (const char *)0x0046440c;  /* "02:59:12" */
static const char *const GS_CD_TRACK2_LEN                 = (const char *)0x00464418;  /* "04:38:71" */

/* --- KEY: keypress.cpp -- the configured action names --- */
static const char *const GS_KEY_CAM_MODE_DOWN             = (const char *)0x004644b4;  /* "CamModeDown" */
static const char *const GS_KEY_CAM_MODE_UP               = (const char *)0x004644c0;  /* "CamModeUp" */
static const char *const GS_KEY_CAM_MODE_LEFT             = (const char *)0x004644cc;  /* "CamModeLeft" */
static const char *const GS_KEY_CAM_MODE_RIGHT            = (const char *)0x004644d8;  /* "CamModeRight" */
static const char *const GS_KEY_OVERVIEW                  = (const char *)0x004644e8;  /* "John_OverView" */
static const char *const GS_KEY_HARAKIRI                  = (const char *)0x004644f8;  /* "John_Harakiri" */
static const char *const GS_KEY_RELEASE_BOMB              = (const char *)0x00464508;  /* "John_Release_Bomb" */
static const char *const GS_KEY_ZOOM_OUT                  = (const char *)0x0046451c;  /* "John_Zoom_Out" */
static const char *const GS_KEY_ZOOM_IN                   = (const char *)0x0046452c;  /* "John_Zoom_In" */
static const char *const GS_KEY_MOVE_BACK                 = (const char *)0x0046453c;  /* "John_Move_Back" */
static const char *const GS_KEY_MOVE_FORWARD              = (const char *)0x0046454c;  /* "John_Move_Forward" */
static const char *const GS_KEY_TURN_RIGHT                = (const char *)0x00464560;  /* "John_Turn_Right" */
static const char *const GS_KEY_TURN_LEFT                 = (const char *)0x00464570;  /* "John_Turn_Left" */

/* --- D3D: createdevice.cpp -- device bring-up, in the authors' German --- */
static const char *const GS_D3D_ERR_CREATE_VP             = (const char *)0x00464e04;  /* "Fehler beim Anlegen des D3DViewport." */
static const char *const GS_D3D_ERR_CREATE_DEVICE         = (const char *)0x00464e2c;  /* "Fehler beim Anlegen des D3DDevice." */
static const char *const GS_D3D_ERR_ATTACH_ZBUF           = (const char *)0x00464e50;  /* "Fehler beim Binden des Z-Puffers an die Renderoberfläche." */
static const char *const GS_D3D_ERR_ZBUF_SURFACE          = (const char *)0x00464e8c;  /* "Fehler beim Anlegen des Z-Puffers." */
static const char *const GS_D3D_STENCIL_BITDEPTH          = (const char *)0x00464eb0;  /* "Stencil-Buffer-Bit-Depth: %d ()\n\n" */
static const char *const GS_D3D_ZBUF_BITDEPTH             = (const char *)0x00464ed4;  /* "Z-Buffer-Bit-Depth: %d ()\n\n" */
static const char *const GS_D3D_ERR_ZBUF_FORMAT           = (const char *)0x00464ef0;  /* "Kein Pixelformat für den Z-Puffer gefunden." */
static const char *const GS_D3D_ERR_D3D3_IFACE            = (const char *)0x00464f1c;  /* "Direct3D3-Schnittstelle nicht gefunden." */
static const char *const GS_D3D_ERR_BACKBUFFER            = (const char *)0x00464f44;  /* "Fehler beim Abfragen des BackBuffers." */
static const char *const GS_D3D_ERR_PRIMARY               = (const char *)0x00464f6c;  /* "Fehler beim Anlegen der primären Oberfläche." */
static const char *const GS_D3D_DONE                      = (const char *)0x00464f9c;  /* "...done\n\n" */
static const char *const GS_D3D_NO_MODE_SPECIFIED         = (const char *)0x00464fa8;  /* "no mode specified, trying to set first mode %dx%dx%d\n" */
static const char *const GS_D3D_ERR_SET_MODE              = (const char *)0x00464fe0;  /* "Fehler beim Setzen des Videomodus." */
static const char *const GS_D3D_TRYING_FIRST_MODE         = (const char *)0x00465004;  /* "trying to set first mode %dx%dx%d\n" */
static const char *const GS_D3D_FAILED_HR                 = (const char *)0x00465028;  /* "...failed (%x)\n" */
static const char *const GS_D3D_TRYING_MODE               = (const char *)0x00465038;  /* "trying to set mode %dx%dx%d " */
static const char *const GS_D3D_END_ENUMMODES             = (const char *)0x00465058;  /* "END ENUMDISPLAYMODES\n\n" */
static const char *const GS_D3D_ERR_ENUMMODES             = (const char *)0x00465070;  /* "Fehler beim Ermitteln der verfügbaren Videomodi." */
static const char *const GS_D3D_START_ENUMMODES           = (const char *)0x004650a4;  /* "START ENUMDISPLAYMODES\n" */
static const char *const GS_D3D_RENDER_BITDEPTH           = (const char *)0x004650bc;  /* "DeviceRenderBitDepthFlags:%d\n\n" */
static const char *const GS_D3D_ERR_COOP_LEVEL            = (const char *)0x004650dc;  /* "Fehler beim Setzen der Kooperationsebene." */
static const char *const GS_D3D_ERR_DD4_IFACE             = (const char *)0x00465108;  /* "DirectDraw4-Schnittstelle nicht gefunden." */
static const char *const GS_D3D_ERR_DDRAW_CREATE          = (const char *)0x00465134;  /* "Fehler beim Anlegen des DirectDraw-Objekts." */

/* --- CFG: fixedsounds.cpp -- Karoo.cfg --- */
static const char *const GS_CFG_FILE                      = (const char *)0x004652cc;  /* "Karoo.cfg" */
static const char *const GS_CFG_SAVE_ERR                  = (const char *)0x00465458;  /* "GAME: ** error ** while saving config-values (maybe write-..." */
static const char *const GS_CFG_SAVE_OK                   = (const char *)0x004654b0;  /* "GAME: config-values saved correctly" */
static const char *const GS_CFG_NO_SOUND                  = (const char *)0x00465bfc;  /* "GAME: warning - SoundManager not created, no wave and CD-s..." */

/* --- GAME: gametick.cpp / gamereset.cpp / cheatcode.cpp -- game flow --- */
static const char *const GS_GAME_HSFILE                   = (const char *)0x0046550c;  /* "jj" */
static const char *const GS_GAME_MAIN                     = (const char *)0x00465510;  /* "Main" */
static const char *const GS_GAME_FINAL                    = (const char *)0x00465518;  /* "Final" */
static const char *const GS_GAME_FINAL_DIR                = (const char *)0x00465520;  /* "Final\\%s" */
static const char *const GS_GAME_DONE_LOG                 = (const char *)0x0046552c;  /* "GAME: game completed %d %d" */
static const char *const GS_GAME_COMPLETED                = (const char *)0x00465548;  /* "completed" */
static const char *const GS_GAME_COMPLETED_AT_LEVEL       = (const char *)0x00465554;  /* "GAME: completed at level %d/%d" */
static const char *const GS_GAME_GAMEOVER                 = (const char *)0x00465574;  /* "gameover" */
static const char *const GS_GAME_SWITCH_TRIGGERED         = (const char *)0x00465580;  /* "GAME: switch triggered %d" */
static const char *const GS_GAME_JJ_GAME_END              = (const char *)0x0046559c;  /* "GAME: GameActions - JJ_GAME_END" */
static const char *const GS_GAME_GAMEFILE_ERR             = (const char *)0x004657a4;  /* "GAME: ** error ** game-file %s is not readable (maybe it n..." */
static const char *const GS_GAME_LEVEL_DONE_CONTINUE      = (const char *)0x00465a3c;  /* "level completed - continue" */

/* --- LVL: levelsetup.cpp -- SetupLevelObjects --- */
static const char *const GS_LVL_LEO_LOADED                = (const char *)0x004655bc;  /* "GAME: LEO-file %s loaded" */
static const char *const GS_LVL_LEO_FAILED                = (const char *)0x004655d8;  /* "GAME: could not load LEO:%s.leo no extra-objects in this l..." */
static const char *const GS_LVL_WARN_CRYSTALS             = (const char *)0x00465618;  /* "GAME: waring - not enough crystals to complete this level!..." */
static const char *const GS_LVL_CRYSTALS                  = (const char *)0x00465658;  /* "GAME: %d crystals in this level, %d needed" */
static const char *const GS_LVL_CD_MISSING                = (const char *)0x00465684;  /* "GAME: CD is not in drive! Crystal at %d,%d token!" */
static const char *const GS_LVL_FREEBOMB                  = (const char *)0x004656b8;  /* "GAME: init level - freebomb %d created" */
static const char *const GS_LVL_WARN_YBRIDGE              = (const char *)0x004656e0;  /* "GAME: waring - Y-bridge with an index lower than 1 !!!" */
static const char *const GS_LVL_WARN_XBRIDGE              = (const char *)0x00465718;  /* "GAME: waring - X-bridge with an index lower than 1 !!!" */
static const char *const GS_LVL_WARN_SWITCH               = (const char *)0x00465750;  /* "GAME: waring - switch with an index lower than 1 !!!" */
static const char *const GS_LVL_INIT_STARTED              = (const char *)0x00465788;  /* "GAME: init level started" */

/* --- OPEN: levelparse.cpp -- opening a level and its script --- */
static const char *const GS_OPEN_SCRIPT_BAD_NUM           = (const char *)0x004657fc;  /* "GAME: could not load instruction-script:%s.jjs ,running in..." */
static const char *const GS_OPEN_SCRIPT_OK_NUM            = (const char *)0x00465850;  /* "GAME: instruction-script loaded:%s.jjs" */
static const char *const GS_OPEN_FMT_SCRIPTS              = (const char *)0x00465878;  /* "%s\\InstructionScripts\\%s" */
static const char *const GS_OPEN_FAILED_NUM               = (const char *)0x00465894;  /* "GAME: ** error ** could not load level by Number (%d) (may..." */
static const char *const GS_OPEN_LOADED_NUM               = (const char *)0x004658ec;  /* "GAME: level (Bonus=%d) loaded by Number (%d): %s.jjm" */
static const char *const GS_OPEN_FMT_LEVELS               = (const char *)0x00465924;  /* "%s\\Levels\\%s" */
static const char *const GS_OPEN_FMT_GAM                  = (const char *)0x00465934;  /* "%s.gam" */
static const char *const GS_OPEN_SCRIPT_BAD_NAME          = (const char *)0x0046593c;  /* "GAME: could not load instruction-script: %s.jjs ,running i..." */
static const char *const GS_OPEN_SCRIPT_OK_NAME           = (const char *)0x00465994;  /* "GAME: instruction-script loaded: %s.jjs" */
static const char *const GS_OPEN_FAILED_NAME              = (const char *)0x004659bc;  /* "GAME: ** error ** could not load level by name: %s.jjm (ma..." */
static const char *const GS_OPEN_LOADED_NAME              = (const char *)0x00465a0c;  /* "GAME: level (Bonus=%d) loaded by name: %s.jjm" */

/* --- WAV: fixedsounds.cpp -- the seven fixed wave paths --- */
static const char *const GS_WAV_SPLAT                     = (const char *)0x00465a58;  /* "%s\\waves\\splat.wav" */
static const char *const GS_WAV_LEVEL_COMPLETED           = (const char *)0x00465a6c;  /* "%s\\waves\\LevelCompleted.wav" */
static const char *const GS_WAV_SWITCH                    = (const char *)0x00465a88;  /* "%s\\waves\\Switch.wav" */
static const char *const GS_WAV_MENU_UP_DOWN              = (const char *)0x00465a9c;  /* "%s\\waves\\MenuUpDown.wav" */
static const char *const GS_WAV_COUNT                     = (const char *)0x00465ab4;  /* "%s\\waves\\Count.wav" */
static const char *const GS_WAV_LAST_SECONDS              = (const char *)0x00465ac8;  /* "%s\\waves\\LastSeconds.wav" */
static const char *const GS_WAV_TIME_OUT                  = (const char *)0x00465ae4;  /* "%s\\waves\\TimeOut.wav" */

/* --- CHEAT: cheatcode.cpp -- the typed cheat codes --- */
static const char *const GS_CHEAT_LC                      = (const char *)0x00465c6c;  /* "GAME: lc %s" */
static const char *const GS_CHEAT_LC_BY_NUMBER            = (const char *)0x00465c80;  /* "GAME: lc by number %d name:%s" */
static const char *const GS_CHEAT_FMT_LVL_PATH            = (const char *)0x00465ca0;  /* "%s\\Levels\\%s.jjm" */
static const char *const GS_CHEAT_C_SL                    = (const char *)0x00465cbc;  /* "GAME: c - sl" */

/* --- RPT: reportwriter.cpp -- the level report --- */
static const char *const GS_RPT_FILE                      = (const char *)0x00465afc;  /* "LevelReport.txt" */
static const char *const GS_RPT_HSC_NAME                  = (const char *)0x00465cdc;  /* "jj.hsc" */
static const char *const GS_RPT_LOG_CREATED               = (const char *)0x00465ce4;  /* "GAME: level report created" */
static const char *const GS_RPT_SPLINES_IN                = (const char *)0x00465d00;  /* "\nSplines in Scripts:%d" */
static const char *const GS_RPT_TEXTS_IN                  = (const char *)0x00465d18;  /* "\nTexts in Scripts:%d" */
static const char *const GS_RPT_TESTSCORES                = (const char *)0x00465d30;  /* "\nTestscores:%d" */
static const char *const GS_RPT_TALLY                     = (const char *)0x00465d40;  /* "\t\t%d\t%d\t%d\n" */
static const char *const GS_RPT_LVL_NAME                  = (const char *)0x00465d5c;  /* "** Levelname: %s\n" */
static const char *const GS_RPT_LVL_FILE                  = (const char *)0x00465d70;  /* "** Level %d  Filename:%s \n" */
static const char *const GS_RPT_STARS                     = (const char *)0x00465d8c;  /* "**********************************************************..." */
static const char *const GS_RPT_BERNIE                    = (const char *)0x00465dd4;  /* "Bernie Boulder" */
static const char *const GS_RPT_S_TAB                     = (const char *)0x00465de4;  /* "%s\t" */
static const char *const GS_RPT_LOG_TIME                  = (const char *)0x00465de8;  /* "GAME: time:%d" */
static const char *const GS_RPT_BLANK_TAB                 = (const char *)0x00465df8;  /* " \t" */
static const char *const GS_RPT_X_TAB                     = (const char *)0x00465dfc;  /* "X\t" */
static const char *const GS_RPT_D_TAB                     = (const char *)0x00465e04;  /* "%d\t" */
static const char *const GS_RPT_RULE                      = (const char *)0x00465e08;  /* "----------------------------------------------------------..." */
static const char *const GS_RPT_COLHDR2                   = (const char *)0x00465f04;  /* "nr\t \tlevel \n" */
static const char *const GS_RPT_COLHDR1                   = (const char *)0x00465f14;  /* "level-\tworld\tbonus\tIS\tLEO\tCatch\tThrow\tCryst\tHCryst..." */
static const char *const GS_RPT_LEVELS                    = (const char *)0x00465fc8;  /* "Levels:%d\n\n" */
static const char *const GS_RPT_GAMEFILE                  = (const char *)0x00465fd4;  /* "gamefile:%s\n" */
static const char *const GS_RPT_TITLE                     = (const char *)0x00465fe4;  /* "***** level report ******\n\n" */
static const char *const GS_RPT_LOG_CREATE                = (const char *)0x00466000;  /* "GAME: create a level report" */
static const char *const GS_RPT_SCRIPTTEXTS               = (const char *)0x0046601c;  /* "ScriptTexts.txt" */
static const char *const GS_RPT_MODE_W                    = (const char *)0x0046602c;  /* "w+t" */

/* --- SND: levelsounds.cpp -- the level-based sounds --- */
static const char *const GS_SND_INIT_DONE                 = (const char *)0x00466030;  /* "GAME: level-based sounds initialized" */
static const char *const GS_SND_LEO_SOUND                 = (const char *)0x00466058;  /* "GAME: try to play LEO sound %s" */
static const char *const GS_SND_TRY_LEO                   = (const char *)0x00466078;  /* "GAME: try to play level-based LEO sounds" */
static const char *const GS_SND_CANDY                     = (const char *)0x004660a4;  /* "Candy" */
static const char *const GS_SND_SPACE                     = (const char *)0x004660ac;  /* "Space" */
static const char *const GS_SND_EGYPT                     = (const char *)0x004660b4;  /* "Egypt" */
static const char *const GS_SND_TRY_INIT                  = (const char *)0x004660bc;  /* "GAME: trying to init level-based sounds" */

/* --- HUD: scoreoverlay.cpp -- the score overlay --- */
static const char *const GS_HUD_PRESS_ENTER               = (const char *)0x00466898;  /* "...press Enter" */
static const char *const GS_HUD_TOTAL_SCORE               = (const char *)0x00466c4c;  /* "total score:" */
static const char *const GS_HUD_LEVEL_SCORE               = (const char *)0x00466c5c;  /* "level score:" */
static const char *const GS_HUD_TIMES_1                   = (const char *)0x00466c6c;  /* "x  1 =" */
static const char *const GS_HUD_VITALITY                  = (const char *)0x00466c74;  /* "vitality:" */
static const char *const GS_HUD_SISYPHUS_BONUS            = (const char *)0x00466c80;  /* "Sisyphus bonus:" */
static const char *const GS_HUD_TIMES_2                   = (const char *)0x00466c90;  /* "x  2 =" */
static const char *const GS_HUD_TIME_LEFT                 = (const char *)0x00466c98;  /* "time left:" */
static const char *const GS_HUD_TIMES_50                  = (const char *)0x00466ca4;  /* "x 50 =" */
static const char *const GS_HUD_DESTROYED_ENEMIES         = (const char *)0x00466cac;  /* "destroyed enemies:" */
static const char *const GS_HUD_TIMES_10                  = (const char *)0x00466cc0;  /* "x 10 =" */
static const char *const GS_HUD_EXTRA_CRYSTALS            = (const char *)0x00466cc8;  /* "extra crystals:" */
static const char *const GS_HUD_TIMES_5                   = (const char *)0x00466cd8;  /* "x  5 =" */
static const char *const GS_HUD_CRYSTALS                  = (const char *)0x00466ce0;  /* "crystals:" */
static const char *const GS_HUD_GAME_OVER                 = (const char *)0x00466cfc;  /* "GAME OVER" */

/* --- TEX: scenetexture.cpp / texturedib.cpp / texturetga.cpp --- */
static const char *const GS_TEX_GETDC_FAILED              = (const char *)0x0046717c;  /* "GetDC Failed\n" */
static const char *const GS_TEX_CREATESURFACE_FAILED      = (const char *)0x0046718c;  /* "CreateSurface Failed\n" */
static const char *const GS_TEX_LOCK_FAILED               = (const char *)0x004671a4;  /* "Lock Failed\n" */
static const char *const GS_TEX_NO_TEXTURE_IFACE          = (const char *)0x00467200;  /* "no Texture-Interface\n" */
static const char *const GS_TEX_NO_TGA_COPY               = (const char *)0x00467218;  /* "couldn't copy TGA\n" */
static const char *const GS_TEX_NO_TEXTURE_SURFACE        = (const char *)0x0046722c;  /* "couldn't create Texture-Surface\n" */
static const char *const GS_TEX_FMT_Y_SIZE                = (const char *)0x00467250;  /* "y-Size:%d(%g) ...ok\n" */
static const char *const GS_TEX_FMT_X_SIZE                = (const char *)0x00467268;  /* "x-Size:%d(%g) ...ok\n" */
static const char *const GS_TEX_TYPE_OK                   = (const char *)0x00467280;  /* "Type ok\n" */
static const char *const GS_TEX_DOT_TGA_UPPER             = (const char *)0x0046728c;  /* ".TGA" */
static const char *const GS_TEX_DOT_TGA_LOWER             = (const char *)0x00467294;  /* ".tga" */
static const char *const GS_TEX_DOT_BMP_UPPER             = (const char *)0x0046729c;  /* ".BMP" */
static const char *const GS_TEX_DOT_BMP_LOWER             = (const char *)0x004672a4;  /* ".bmp" */

/* --- LOG: gamelog.cpp -- the protocol file and the DSERR names --- */
static const char *const GS_LOG_MB_TEXT                   = (const char *)0x00467360;  /* "Could not open Protofile" */
static const char *const GS_LOG_MB_CAPT                   = (const char *)0x0046737c;  /* "CProto::CProto(...)" */
static const char *const GS_LOG_BANNER                    = (const char *)0x00467390;  /* "\n***************** Protokollierung gestartet am : %s ****..." */
static const char *const GS_LOG_MODE_WC                   = (const char *)0x004673e0;  /* "wc" */
static const char *const GS_LOG_LINE                      = (const char *)0x004673fc;  /* "%s : %s\r\n" */
static const char *const GS_LOG_SRCLINE                   = (const char *)0x00467438;  /* "%s : File: %s, Line: %d: %s \r\n" */
static const char *const GS_LOG_HR_UNKNOWN                = (const char *)0x00467458;  /* "Unknown HRESULT" */
static const char *const GS_LOG_DSERR_UNINITIALIZED       = (const char *)0x00467468;  /* "DSERR_UNINITIALIZED" */
static const char *const GS_LOG_DSERR_OTHERAPPHASPRIO     = (const char *)0x0046747c;  /* "DSERR_OTHERAPPHASPRIO" */
static const char *const GS_LOG_DSERR_BUFFERLOST          = (const char *)0x00467494;  /* "DSERR_BUFFERLOST" */
static const char *const GS_LOG_DSERR_ALREADYINITIALIZED  = (const char *)0x004674a8;  /* "DSERR_ALREADYINITIALIZED" */
static const char *const GS_LOG_DSERR_NODRIVER            = (const char *)0x004674c4;  /* "DSERR_NODRIVER" */
static const char *const GS_LOG_DSERR_BADFORMAT           = (const char *)0x004674d4;  /* "DSERR_BADFORMAT" */
static const char *const GS_LOG_DSERR_PRIOLEVELNEEDED     = (const char *)0x004674e4;  /* "DSERR_PRIOLEVELNEEDED" */
static const char *const GS_LOG_DSERR_INVALIDCALL         = (const char *)0x004674fc;  /* "DSERR_INVALIDCALL" */
static const char *const GS_LOG_DSERR_INVALIDPARAM        = (const char *)0x00467510;  /* "DSERR_INVALIDPARAM" */
static const char *const GS_LOG_DSERR_ALLOCATED           = (const char *)0x00467524;  /* "DSERR_ALLOCATED" */
static const char *const GS_LOG_DSERR_CONTROLUNAVAIL      = (const char *)0x00467534;  /* "DSERR_CONTROLUNAVAIL" */
static const char *const GS_LOG_DSERR_OUTOFMEMORY         = (const char *)0x0046754c;  /* "DSERR_OUTOFMEMORY" */
static const char *const GS_LOG_DSERR_NOAGGREGATION       = (const char *)0x00467560;  /* "DSERR_NOAGGREGATION" */
static const char *const GS_LOG_DSERR_GENERIC             = (const char *)0x00467574;  /* "DSERR_GENERIC" */
static const char *const GS_LOG_DSERR_UNSUPPORTED         = (const char *)0x00467584;  /* "DSERR_UNSUPPORTED" */
static const char *const GS_LOG_DSERR_NOINTERFACE         = (const char *)0x00467598;  /* "DSERR_NOINTERFACE" */
static const char *const GS_LOG_ERRLINE                   = (const char *)0x004675ac;  /* "%s : Error %s: %s \r\n" */

/* --- PS: particles.cpp -- the particle-system loader --- */
static const char *const GS_PS_NAME_NULL                  = (const char *)0x00468a60;  /* "NULL" */
static const char *const GS_PS_MSG_ENVLOAD                = (const char *)0x00468a68;  /* "PS: Error while loading Particlesystem, because Environmen..." */
static const char *const GS_PS_MSG_ENVNAME                = (const char *)0x00468ab4;  /* "PS: Error while loading Particlesystem, because Environmen..." */
static const char *const GS_PS_MSG_NOENV                  = (const char *)0x00468b10;  /* "PS: Error while loading Particlesystem, because could not ..." */
static const char *const GS_PS_MSG_GENLOAD                = (const char *)0x00468b68;  /* "PS: Error while loading Particlesystem, because Generator ..." */
static const char *const GS_PS_MSG_NOGEN                  = (const char *)0x00468bb4;  /* "PS: Error while loading Particlesystem, because Generator ..." */
static const char *const GS_PS_MSG_NONAME                 = (const char *)0x00468c0c;  /* "PS: Error while loading Particlesystem, because could not ..." */
static const char *const GS_PS_MSG_NOLEN                  = (const char *)0x00468c60;  /* "PS: Error while loading Particlesystem, because could not ..." */
static const char *const GS_PS_MSG_NORING                 = (const char *)0x00468ca4;  /* "PS: Error while loading Particlesystem, because ParticleLi..." */
static const char *const GS_PS_SRC_FILE                   = (const char *)0x00468cf4;  /* "E:\\WORK\\VC++\\JumpinJohn\\Ra\\ParticleSystem\\ParticleSy..." */
static const char *const GS_PS_MSG_NOCOUNT                = (const char *)0x00468d34;  /* "PS: Error while loading Particlesystem, because ParticleCo..." */
static const char *const GS_PS_OPENSAVE_FILE              = (const char *)0x00468e64;  /* "E:\\WORK\\VC++\\JumpinJohn\\Ra\\ParticleSystem\\OpenSave.c..." */
static const char *const GS_PS_MSG_NOCLOSE                = (const char *)0x00468e9c;  /* "PS: ParticleSystem could not read, because File %s could n..." */
static const char *const GS_PS_MSG_NOOPEN                 = (const char *)0x00468ee0;  /* "PS: Particlesystem could not read, because could not Open ..." */
static const char *const GS_PS_MSG_STARTREAD              = (const char *)0x00468f24;  /* "Starting, to Read Particlesystem from File %s ..." */
static const char *const GS_PS_MSG_NOSYSTEM               = (const char *)0x00468f58;  /* "PS: ParticleSystem could not read, because could create Sy..." */
static const char *const GS_PS_MSG_NAMEREAD               = (const char *)0x00468fa0;  /* "PS: ParticleSystem could not read, because Data could not ..." */
static const char *const GS_PS_MSG_NODATA                 = (const char *)0x00468fe0;  /* "PS: ParticleSystem could not read, because could not read ..." */
static const char *const GS_PS_SUB_FILE                   = (const char *)0x004690dc;  /* "E:\\WORK\\VC++\\JumpinJohn\\Ra\\ParticleSystem\\ParticleSy..." */
static const char *const GS_PS_MSG_PTVERTS                = (const char *)0x0046911c;  /* "PS: Loading PointParticleSystem failed, because could not ..." */
static const char *const GS_PS_MSG_SAVESIZE               = (const char *)0x0046916c;  /* "PS: Save FaceParticleSystem failed, because could save Fac..." */
static const char *const GS_PS_MSG_VERTARR                = (const char *)0x004691ac;  /* "PS: Loading ParticleSystem failed, because could not creat..." */
static const char *const GS_PS_MSG_FACESIZE               = (const char *)0x004691f4;  /* "PS: Loading FaceParticleSystem failed, because could not r..." */
static const char *const GS_PS_MSG_XSAVE                  = (const char *)0x0046923c;  /* "PS: Save XFaceParticleSystem failed, because could save At..." */
static const char *const GS_PS_MSG_XLOAD                  = (const char *)0x00469280;  /* "PS: Load XFaceParticleSystem failed, because could read At..." */

/* --- PSNAME: the particle system's own class names, generators.cpp and
 * particles.cpp.  Same .data block as the GS_PS_ messages above, but a
 * different kind: these are stored in each object's `char *pName` and
 * compared by the loader's strcmp chain, so they are `char *`, not
 * `const char *` -- the field they are assigned to is the game's. --- */
static char *const GS_PSNAME_SYSTEM             = (char *)0x00468a38;  /* "ParticleSystem" */
static char *const GS_PSNAME_GENERATOR          = (char *)0x00468a48;  /* "Generator" */
static char *const GS_PSNAME_ENVIRONMENT        = (char *)0x00468a54;  /* "Environment" */
static char *const GS_PSNAME_POINT_SYSTEM       = (char *)0x00468e40;  /* "PointParticleSystem" */
static char *const GS_PSNAME_POINT_GEN          = (char *)0x00469024;  /* "PointGenerator" */
static char *const GS_PSNAME_BOX_GEN            = (char *)0x00469034;  /* "BoxGenerator" */
static char *const GS_PSNAME_STD_GEN            = (char *)0x00469044;  /* "StdGenerator" */
static char *const GS_PSNAME_XSTD_GEN           = (char *)0x00469054;  /* "XStdGenerator" */
static char *const GS_PSNAME_CYL_GEN            = (char *)0x00469064;  /* "CylinderGenerator" */
static char *const GS_PSNAME_GRAVITY_ENV        = (char *)0x00469078;  /* "GravityEnvironment" */
static char *const GS_PSNAME_MAGNET_ENV         = (char *)0x0046908c;  /* "MagnetEnvironment" */
static char *const GS_PSNAME_FACE_SYSTEM        = (char *)0x004690b4;  /* "FaceParticleSystem" */
static char *const GS_PSNAME_XFACE_SYSTEM       = (char *)0x004690c8;  /* "XFaceParticleSystem" */

/* --- not a literal: the game's own mutable buffers --- */

/* The install directory, filled by WinMain; every path format above is
 * printed against it.  Was GAMEDIR in two files and GAME_DIR in five. */
static const char *const GS_GAME_DIR = (const char *)0x004e01c4;

#endif  /* KAROO_GAMESTR_H */
