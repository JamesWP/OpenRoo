/* The game's own strings: log formats, file names, action names, report layout
 * and the score overlay's labels, one array each, named GS_<GROUP>_<WHAT> by
 * the subsystem that uses them.
 *
 * `inline` gives each array a single address across every file, so the places
 * that compare these strings by pointer still work. */

#ifndef KAROO_GAMESTR_H
#define KAROO_GAMESTR_H

/* ANI: the animation loader. */
inline const char GS_ANI_LOADED[] = "ANI: %s loaded";

/* FMT: one-conversion formats shared by several files. */
inline const char GS_FMT_S[] = "%s";
inline const char GS_FMT_NEWLINE[] = "\012";
inline const char GS_FMT_TAB[] = "\011";
inline const char GS_FMT_D[] = "%d";

/* CD: the CD player. */
inline const char GS_CD_TRY_TRACK[] = "CDM: trying to play track %d caption:%s ";
inline const char GS_CD_TRACK9_LEN[] = "02:17:74";
inline const char GS_CD_TRACK8_LEN[] = "04:00:74";
inline const char GS_CD_TRACK7_LEN[] = "03:32:60";
inline const char GS_CD_TRACK6_LEN[] = "03:08:54";
inline const char GS_CD_TRACK5_LEN[] = "02:46:68";
inline const char GS_CD_TRACK4_LEN[] = "03:28:39";
inline const char GS_CD_TRACK3_LEN[] = "02:59:12";
inline const char GS_CD_TRACK2_LEN[] = "04:38:71";
inline const char GS_CD_TRACKFILE_PATH[] = "%s\134CDTracks\134%s.cdt";
inline const char GS_CD_TRACKFILE_DELIMS[] = " ,\011\012;";
inline const char GS_CD_TRACKFILE_MODE[] = "r+t";
inline const char GS_CD_TRACKFILE_MISSING[] = "CDM: warning - track-file named as %s was not found";
inline const char GS_CD_TRACKFILE_FOUND[] = "CDM: track-file named as %s was found";
inline const char GS_CD_THEME_TRACK[] = "CDM: theme %s is cd track %d";
inline const char GS_CD_TRACK_COUNT[] = "CDM: number of tracks %d";
inline const char GS_CD_TRACK_LENGTH[] = "CDM: track %d lenght:%s";
inline const char GS_CONTROL_SAVE_SETTINGS[] = "CONTROL: trying to save settings";
inline const char GS_HIGHSCORE_DEFAULT_NAME[] = "Open Roo";

/* GAME: the game object's lifecycle. */
inline const char GS_GAME_CD_OK[] = "GAME: original CD is in the drive - OK";
inline const char GS_GAME_CD_MISSING[] = "GAME: * warning * - the original CD is not in drive";
inline const char GS_GAME_CREATED[] = "GAME: game-object created, game is named as:%s";
inline const char GS_GAME_FILE_FAILED[] = "GAME: ** error ** game-file named as %s.gam could not loaded - aborting!!!";
inline const char GS_GAME_NO_SAVES[] = "GAME: warning - no save-files for this game (maybe started a new game the first time\077), creating %d empty slots";
inline const char GS_GAME_CFG_DEFAULTS[] = "GAME: warning - no correct config-values (or no *.cfg-file), creating default-config";
inline const char GS_GAME_CFG_LOADED[] = "GAME: config-values loaded";
inline const char GS_GAME_NO_HIGHSCORES[] = "GAME: warning - no highscore-file for the game found! Creating a new one...";
inline const char GS_GAME_HIGHSCORES_LOADED[] = "GAME: highscore-file loaded";
inline const char GS_GAME_DEMO_ABORT[] = "GAME: ** error ** this is a DEMO, piracy alert, aborting!!!";
inline const char GS_GAME_COMMERCIAL[] = "GAME: this is a commercial version";
inline const char GS_GAME_DEMO_LEVEL[] = "DemoLevelForest";
inline const char GS_GAME_DTOR_START[] = "GAME: starting destructor";
inline const char GS_GAME_HIGHSCORES_SAVED[] = "GAME: highscore-files saved";
inline const char GS_GAME_SOUNDS_RELEASED[] = "GAME: all sounds successful released";
inline const char GS_GAME_FILE_PATH[] = "%s\134%s.gam";
inline const char GS_GAME_FILE_LOADING[] = "GAME: load game-file: %s";
inline const char GS_MODE_READ[] = "r";
inline const char GS_GAME_FILE_BINARY[] = "GAME: load game-file as binary,coded file";
inline const char GS_GAME_FILE_BINARY_DONE[] = "GAME: game-file loaded %d levels included %d bytes loaded";
inline const char GS_GAME_FILE_LEVEL[] = "GAME: game-file: %s";
inline const char GS_GAME_FILE_TEXT[] = "GAME: load game-file as text-file";
inline const char GS_GAME_FILE_TEXT_DONE[] = "GAME: game-file loaded %d levels included";
inline const char GS_ACT_TURN_LEFT[] = "John_Turn_Left";
inline const char GS_ACT_TURN_RIGHT[] = "John_Turn_Right";
inline const char GS_ACT_MOVE_FORWARD[] = "John_Move_Forward";
inline const char GS_ACT_MOVE_BACK[] = "John_Move_Back";
inline const char GS_ACT_ZOOM_IN[] = "John_Zoom_In";
inline const char GS_ACT_ZOOM_OUT[] = "John_Zoom_Out";
inline const char GS_ACT_RELEASE_BOMB[] = "John_Release_Bomb";
inline const char GS_ACT_HARAKIRI[] = "John_Harakiri";
inline const char GS_ACT_OVERVIEW[] = "John_OverView";
inline const char GS_ACT_CAM_RIGHT[] = "CamModeRight";
inline const char GS_ACT_CAM_LEFT[] = "CamModeLeft";
inline const char GS_ACT_CAM_UP[] = "CamModeUp";
inline const char GS_ACT_CAM_DOWN[] = "CamModeDown";
inline const char GS_CONTROL_NO_DEVICES[] = "Could not acquire the input devices.";
inline const char GS_CONTROL_NO_INPUT[] = "Could not set up the input devices.";
inline const char GS_CONTROL_ERROR_CAPTION[] = "Error!";

/* KEY: the configurable action names. */
inline const char GS_KEY_CAM_MODE_DOWN[] = "CamModeDown";
inline const char GS_KEY_CAM_MODE_UP[] = "CamModeUp";
inline const char GS_KEY_CAM_MODE_LEFT[] = "CamModeLeft";
inline const char GS_KEY_CAM_MODE_RIGHT[] = "CamModeRight";
inline const char GS_KEY_OVERVIEW[] = "John_OverView";
inline const char GS_KEY_HARAKIRI[] = "John_Harakiri";
inline const char GS_KEY_RELEASE_BOMB[] = "John_Release_Bomb";
inline const char GS_KEY_ZOOM_OUT[] = "John_Zoom_Out";
inline const char GS_KEY_ZOOM_IN[] = "John_Zoom_In";
inline const char GS_KEY_MOVE_BACK[] = "John_Move_Back";
inline const char GS_KEY_MOVE_FORWARD[] = "John_Move_Forward";
inline const char GS_KEY_TURN_RIGHT[] = "John_Turn_Right";
inline const char GS_KEY_TURN_LEFT[] = "John_Turn_Left";

/* D3D: device bring-up, with the authors' German error texts. */
inline const char GS_D3D_ERR_CREATE_VP[] = "Could not create the Direct3D viewport.";
inline const char GS_D3D_ERR_CREATE_DEVICE[] = "Could not create the Direct3D device.";
inline const char GS_D3D_ERR_ATTACH_ZBUF[] = "Could not attach the z-buffer to the render surface.";
inline const char GS_D3D_ERR_ZBUF_SURFACE[] = "Could not create the z-buffer.";
inline const char GS_D3D_STENCIL_BITDEPTH[] = "Stencil-Buffer-Bit-Depth: %d ()\012\012";
inline const char GS_D3D_ZBUF_BITDEPTH[] = "Z-Buffer-Bit-Depth: %d ()\012\012";
inline const char GS_D3D_ERR_ZBUF_FORMAT[] = "No pixel format found for the z-buffer.";
inline const char GS_D3D_ERR_D3D3_IFACE[] = "Could not get the Direct3D3 interface.";
inline const char GS_D3D_ERR_BACKBUFFER[] = "Could not get the back buffer.";
inline const char GS_D3D_ERR_PRIMARY[] = "Could not create the primary surface.";
inline const char GS_D3D_DONE[] = "...done\012\012";
inline const char GS_D3D_NO_MODE_SPECIFIED[] = "no mode specified, trying to set first mode %dx%dx%d\012";
inline const char GS_D3D_ERR_SET_MODE[] = "Could not set the display mode.";
inline const char GS_D3D_TRYING_FIRST_MODE[] = "trying to set first mode %dx%dx%d\012";
inline const char GS_D3D_FAILED_HR[] = "...failed (%x)\012";
inline const char GS_D3D_TRYING_MODE[] = "trying to set mode %dx%dx%d ";
inline const char GS_D3D_END_ENUMMODES[] = "END ENUMDISPLAYMODES\012\012";
inline const char GS_D3D_ERR_ENUMMODES[] = "Could not list the display modes.";
inline const char GS_D3D_START_ENUMMODES[] = "START ENUMDISPLAYMODES\012";
inline const char GS_D3D_RENDER_BITDEPTH[] = "DeviceRenderBitDepthFlags:%d\012\012";
inline const char GS_D3D_ERR_COOP_LEVEL[] = "Could not set the cooperative level.";
inline const char GS_D3D_ERR_DD4_IFACE[] = "Could not get the DirectDraw4 interface.";
inline const char GS_D3D_ERR_DDRAW_CREATE[] = "Could not create the DirectDraw object.";
inline const char GS_D3D_FOUND_MODE[] = "found mode %dx%dx%d\012";
inline const char GS_D3D_ZBUF_FMT[] = "Z:%d S:%d\012";

/* CFG: Karoo.cfg. */
inline const char GS_CFG_FILE[] = "Karoo.cfg";
inline const char GS_CFG_SAVE_ERR[] = "GAME: ** error ** while saving config-values (maybe write-protected or hd full\077) !!!";
inline const char GS_CFG_SAVE_OK[] = "GAME: config-values saved correctly";
inline const char GS_CFG_NO_SOUND[] = "GAME: warning - SoundManager not created, no wave and CD-sound !!!";

/* GAME: game flow. */
inline const char GS_GAME_HSFILE[] = "jj";
inline const char GS_GAME_MAIN[] = "Main";
inline const char GS_GAME_FINAL[] = "Final";
inline const char GS_GAME_FINAL_DIR[] = "Final\134%s";
inline const char GS_GAME_DONE_LOG[] = "GAME: game completed %d %d";
inline const char GS_GAME_COMPLETED[] = "completed";
inline const char GS_GAME_COMPLETED_AT_LEVEL[] = "GAME: completed at level %d/%d";
inline const char GS_GAME_GAMEOVER[] = "gameover";
inline const char GS_GAME_SWITCH_TRIGGERED[] = "GAME: switch triggered %d";
inline const char GS_GAME_JJ_GAME_END[] = "GAME: GameActions - JJ_GAME_END";
inline const char GS_GAME_GAMEFILE_ERR[] = "GAME: ** error ** game-file %s is not readable (maybe it not exists\077) aborting game!!!";
inline const char GS_GAME_LEVEL_DONE_CONTINUE[] = "level completed - continue";

/* LVL: setting up a level's objects. */
inline const char GS_LVL_LEO_LOADED[] = "GAME: LEO-file %s loaded";
inline const char GS_LVL_LEO_FAILED[] = "GAME: could not load LEO:%s.leo no extra-objects in this level";
inline const char GS_LVL_WARN_CRYSTALS[] = "GAME: waring - not enough crystals to complete this level!!!!";
inline const char GS_LVL_CRYSTALS[] = "GAME: %d crystals in this level, %d needed";
inline const char GS_LVL_CD_MISSING[] = "GAME: CD is not in drive! Crystal at %d,%d token!";
inline const char GS_LVL_FREEBOMB[] = "GAME: init level - freebomb %d created";
inline const char GS_LVL_WARN_YBRIDGE[] = "GAME: waring - Y-bridge with an index lower than 1 !!!";
inline const char GS_LVL_WARN_XBRIDGE[] = "GAME: waring - X-bridge with an index lower than 1 !!!";
inline const char GS_LVL_WARN_SWITCH[] = "GAME: waring - switch with an index lower than 1 !!!";
inline const char GS_LVL_INIT_STARTED[] = "GAME: init level started";

/* OPEN: opening a level and its script. */
inline const char GS_OPEN_SCRIPT_BAD_NUM[] = "GAME: could not load instruction-script:%s.jjs ,running in observation-mode only...";
inline const char GS_OPEN_SCRIPT_OK_NUM[] = "GAME: instruction-script loaded:%s.jjs";
inline const char GS_OPEN_FMT_SCRIPTS[] = "%s\134InstructionScripts\134%s";
inline const char GS_OPEN_FAILED_NUM[] = "GAME: ** error ** could not load level by Number (%d) (maybe it not exists\077): %s.jjm";
inline const char GS_OPEN_LOADED_NUM[] = "GAME: level (Bonus=%d) loaded by Number (%d): %s.jjm";
inline const char GS_OPEN_FMT_LEVELS[] = "%s\134Levels\134%s";
inline const char GS_OPEN_FMT_GAM[] = "%s.gam";
inline const char GS_OPEN_SCRIPT_BAD_NAME[] = "GAME: could not load instruction-script: %s.jjs ,running in observation-mode only...";
inline const char GS_OPEN_SCRIPT_OK_NAME[] = "GAME: instruction-script loaded: %s.jjs";
inline const char GS_OPEN_FAILED_NAME[] = "GAME: ** error ** could not load level by name: %s.jjm (maybe it not exists\077)";
inline const char GS_OPEN_LOADED_NAME[] = "GAME: level (Bonus=%d) loaded by name: %s.jjm";

/* WAV: the seven fixed wave paths. */
inline const char GS_WAV_SPLAT[] = "%s\134waves\134splat.wav";
inline const char GS_WAV_LEVEL_COMPLETED[] = "%s\134waves\134LevelCompleted.wav";
inline const char GS_WAV_SWITCH[] = "%s\134waves\134Switch.wav";
inline const char GS_WAV_MENU_UP_DOWN[] = "%s\134waves\134MenuUpDown.wav";
inline const char GS_WAV_COUNT[] = "%s\134waves\134Count.wav";
inline const char GS_WAV_LAST_SECONDS[] = "%s\134waves\134LastSeconds.wav";
inline const char GS_WAV_TIME_OUT[] = "%s\134waves\134TimeOut.wav";

/* CHEAT: the typed cheat codes. */
inline const char GS_CHEAT_LC[] = "GAME: lc %s";
inline const char GS_CHEAT_LC_BY_NUMBER[] = "GAME: lc by number %d name:%s";
inline const char GS_CHEAT_FMT_LVL_PATH[] = "%s\134Levels\134%s.jjm";
inline const char GS_CHEAT_C_SL[] = "GAME: c - sl";

/* RPT: the level report. */
inline const char GS_RPT_FILE[] = "LevelReport.txt";
inline const char GS_RPT_HSC_NAME[] = "jj.hsc";
inline const char GS_RPT_LOG_CREATED[] = "GAME: level report created";
inline const char GS_RPT_SPLINES_IN[] = "\012Splines in Scripts:%d";
inline const char GS_RPT_TEXTS_IN[] = "\012Texts in Scripts:%d";
inline const char GS_RPT_TESTSCORES[] = "\012Testscores:%d";
inline const char GS_RPT_TALLY[] = "\011\011%d\011%d\011%d\012";
inline const char GS_RPT_LVL_NAME[] = "** Levelname: %s\012";
inline const char GS_RPT_LVL_FILE[] = "** Level %d  Filename:%s \012";
inline const char GS_RPT_STARS[] = "*********************************************************************\012";
inline const char GS_RPT_DEFAULT_NAME[] = "Open Roo";
inline const char GS_RPT_S_TAB[] = "%s\011";
inline const char GS_RPT_LOG_TIME[] = "GAME: time:%d";
inline const char GS_RPT_BLANK_TAB[] = " \011";
inline const char GS_RPT_X_TAB[] = "X\011";
inline const char GS_RPT_D_TAB[] = "%d\011";
inline const char GS_RPT_RULE[] = "---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\012";
inline const char GS_RPT_COLHDR2[] = "nr\011 \011level \012";
inline const char GS_RPT_COLHDR1[] = "level-\011world\011bonus\011IS\011LEO\011Catch\011Throw\011Cryst\011HCryst\011NCryst\011HLifes\011Fields\011Obst\011Elev\011Platf\011Destr\011Glue\011Jump\011Telep\011Slide\011Ice\011Brigde\011Lifes\011Freeze\011Protect\011Paragl\011Speed\011Bomb\011Time\011Scores\012";
inline const char GS_RPT_LEVELS[] = "Levels:%d\012\012";
inline const char GS_RPT_GAMEFILE[] = "gamefile:%s\012";
inline const char GS_RPT_TITLE[] = "***** level report ******\012\012";
inline const char GS_RPT_LOG_CREATE[] = "GAME: create a level report";
inline const char GS_RPT_SCRIPTTEXTS[] = "ScriptTexts.txt";
inline const char GS_RPT_MODE_W[] = "w+t";

/* SND: the level-based sounds. */
inline const char GS_SND_INIT_DONE[] = "GAME: level-based sounds initialized";
inline const char GS_SND_LEO_SOUND[] = "GAME: try to play LEO sound %s";
inline const char GS_SND_TRY_LEO[] = "GAME: try to play level-based LEO sounds";
inline const char GS_SND_CANDY[] = "Candy";
inline const char GS_SND_SPACE[] = "Space";
inline const char GS_SND_EGYPT[] = "Egypt";
inline const char GS_SND_TRY_INIT[] = "GAME: trying to init level-based sounds";

/* HUD: the score overlay. */
inline const char GS_HUD_PRESS_ENTER[] = "...press Enter";
inline const char GS_HUD_TOTAL_SCORE[] = "total score:";
inline const char GS_HUD_LEVEL_SCORE[] = "level score:";
inline const char GS_HUD_TIMES_1[] = "x  1 =";
inline const char GS_HUD_VITALITY[] = "vitality:";
inline const char GS_HUD_SISYPHUS_BONUS[] = "Sisyphus bonus:";
inline const char GS_HUD_TIMES_2[] = "x  2 =";
inline const char GS_HUD_TIME_LEFT[] = "time left:";
inline const char GS_HUD_TIMES_50[] = "x 50 =";
inline const char GS_HUD_DESTROYED_ENEMIES[] = "destroyed enemies:";
inline const char GS_HUD_TIMES_10[] = "x 10 =";
inline const char GS_HUD_EXTRA_CRYSTALS[] = "extra crystals:";
inline const char GS_HUD_TIMES_5[] = "x  5 =";
inline const char GS_HUD_CRYSTALS[] = "crystals:";
inline const char GS_HUD_GAME_OVER[] = "GAME OVER";

/* TEX: texture loading. */
inline const char GS_TEX_GETDC_FAILED[] = "GetDC Failed\012";
inline const char GS_TEX_CREATESURFACE_FAILED[] = "CreateSurface Failed\012";
inline const char GS_TEX_LOCK_FAILED[] = "Lock Failed\012";
inline const char GS_TEX_FMT_PIXELFORMAT[] = ": Flags: %d, RGBBitCount: %d, RMask: %x, GMask: %x, BMask: %x, AMask: %x\012";
inline const char GS_TEX_NO_TEXTURE_IFACE[] = "no Texture-Interface\012";
inline const char GS_TEX_NO_TGA_COPY[] = "couldn't copy TGA\012";
inline const char GS_TEX_NO_TEXTURE_SURFACE[] = "couldn't create Texture-Surface\012";
inline const char GS_TEX_FMT_Y_SIZE[] = "y-Size:%d(%g) ...ok\012";
inline const char GS_TEX_FMT_X_SIZE[] = "x-Size:%d(%g) ...ok\012";
inline const char GS_TEX_TYPE_OK[] = "Type ok\012";
inline const char GS_TEX_DOT_TGA_UPPER[] = ".TGA";
inline const char GS_TEX_DOT_TGA_LOWER[] = ".tga";
inline const char GS_TEX_DOT_BMP_UPPER[] = ".BMP";
inline const char GS_TEX_DOT_BMP_LOWER[] = ".bmp";

/* LOG: the game's log file and the DirectSound error names. */
inline const char GS_LOG_MB_TEXT[] = "Could not open Protofile";
inline const char GS_LOG_MB_CAPT[] = "CProto::CProto(...)";
inline const char GS_LOG_BANNER[] = "\012***************** Log started on %s ***********************\012";
inline const char GS_LOG_MODE_WC[] = "wc";
inline const char GS_LOG_LINE[] = "%s : %s\015\012";
inline const char GS_LOG_SRCLINE[] = "%s : File: %s, Line: %d: %s \015\012";
inline const char GS_LOG_HR_UNKNOWN[] = "Unknown HRESULT";
inline const char GS_LOG_DSERR_UNINITIALIZED[] = "DSERR_UNINITIALIZED";
inline const char GS_LOG_DSERR_OTHERAPPHASPRIO[] = "DSERR_OTHERAPPHASPRIO";
inline const char GS_LOG_DSERR_BUFFERLOST[] = "DSERR_BUFFERLOST";
inline const char GS_LOG_DSERR_ALREADYINITIALIZED[] = "DSERR_ALREADYINITIALIZED";
inline const char GS_LOG_DSERR_NODRIVER[] = "DSERR_NODRIVER";
inline const char GS_LOG_DSERR_BADFORMAT[] = "DSERR_BADFORMAT";
inline const char GS_LOG_DSERR_PRIOLEVELNEEDED[] = "DSERR_PRIOLEVELNEEDED";
inline const char GS_LOG_DSERR_INVALIDCALL[] = "DSERR_INVALIDCALL";
inline const char GS_LOG_DSERR_INVALIDPARAM[] = "DSERR_INVALIDPARAM";
inline const char GS_LOG_DSERR_ALLOCATED[] = "DSERR_ALLOCATED";
inline const char GS_LOG_DSERR_CONTROLUNAVAIL[] = "DSERR_CONTROLUNAVAIL";
inline const char GS_LOG_DSERR_OUTOFMEMORY[] = "DSERR_OUTOFMEMORY";
inline const char GS_LOG_DSERR_NOAGGREGATION[] = "DSERR_NOAGGREGATION";
inline const char GS_LOG_DSERR_GENERIC[] = "DSERR_GENERIC";
inline const char GS_LOG_DSERR_UNSUPPORTED[] = "DSERR_UNSUPPORTED";
inline const char GS_LOG_DSERR_NOINTERFACE[] = "DSERR_NOINTERFACE";
inline const char GS_LOG_ERRLINE[] = "%s : Error %s: %s \015\012";

/* PS: the particle-system loader. */
inline const char GS_PS_NAME_NULL[] = "NULL";
inline const char GS_PS_MSG_ENVLOAD[] = "PS: Error while loading Particlesystem, because Environment coud not loaded";
inline const char GS_PS_MSG_ENVNAME[] = "PS: Error while loading Particlesystem, because Environment of Typ: '%s' coud not created";
inline const char GS_PS_MSG_NOENV[] = "PS: Error while loading Particlesystem, because could not read Environment-ClassName";
inline const char GS_PS_MSG_GENLOAD[] = "PS: Error while loading Particlesystem, because Generator coud not loaded";
inline const char GS_PS_MSG_NOGEN[] = "PS: Error while loading Particlesystem, because Generator of Typ: '%s' coud not created";
inline const char GS_PS_MSG_NONAME[] = "PS: Error while loading Particlesystem, because could not read GeneratorclassName";
inline const char GS_PS_MSG_NOLEN[] = "PS: Error while loading Particlesystem, because could not read Data";
inline const char GS_PS_MSG_NORING[] = "PS: Error while loading Particlesystem, because ParticleList could not created";
inline const char GS_PS_SRC_FILE[] = "src/render/particles.cpp";
inline const char GS_PS_MSG_NOCOUNT[] = "PS: Error while loading Particlesystem, because ParticleCount could not read";
inline const char GS_PS_OPENSAVE_FILE[] = "src/render/particles.cpp";
inline const char GS_PS_MSG_NOCLOSE[] = "PS: ParticleSystem could not read, because File %s could not closed";
inline const char GS_PS_MSG_NOOPEN[] = "PS: Particlesystem could not read, because could not Open File %s";
inline const char GS_PS_MSG_STARTREAD[] = "Starting, to Read Particlesystem from File %s ...";
inline const char GS_PS_MSG_NOSYSTEM[] = "PS: ParticleSystem could not read, because could create System : '%s'";
inline const char GS_PS_MSG_NAMEREAD[] = "PS: ParticleSystem could not read, because Data could not read";
inline const char GS_PS_MSG_NODATA[] = "PS: ParticleSystem could not read, because could not read Data";
inline const char GS_PS_SUB_FILE[] = "src/render/particles.cpp";
inline const char GS_PS_MSG_PTVERTS[] = "PS: Loading PointParticleSystem failed, because could not create VertexArray";
inline const char GS_PS_MSG_SAVESIZE[] = "PS: Save FaceParticleSystem failed, because could save FaceSize";
inline const char GS_PS_MSG_VERTARR[] = "PS: Loading ParticleSystem failed, because could not create VertexArray";
inline const char GS_PS_MSG_FACESIZE[] = "PS: Loading FaceParticleSystem failed, because could not read FaceSize";
inline const char GS_PS_MSG_XSAVE[] = "PS: Save XFaceParticleSystem failed, because could save Attributes";
inline const char GS_PS_MSG_XLOAD[] = "PS: Load XFaceParticleSystem failed, because could read Attributes";

/* PSNAME: the particle classes' names.  Each object's name field points at one
 * and the loader compares them with strcmp, so they are mutable `char`, as
 * that field is. */
inline char GS_PSNAME_SYSTEM[] = "ParticleSystem";
inline char GS_PSNAME_GENERATOR[] = "Generator";
inline char GS_PSNAME_ENVIRONMENT[] = "Environment";
inline char GS_PSNAME_POINT_SYSTEM[] = "PointParticleSystem";
inline char GS_PSNAME_POINT_GEN[] = "PointGenerator";
inline char GS_PSNAME_BOX_GEN[] = "BoxGenerator";
inline char GS_PSNAME_STD_GEN[] = "StdGenerator";
inline char GS_PSNAME_XSTD_GEN[] = "XStdGenerator";
inline char GS_PSNAME_CYL_GEN[] = "CylinderGenerator";
inline char GS_PSNAME_GRAVITY_ENV[] = "GravityEnvironment";
inline char GS_PSNAME_MAGNET_ENV[] = "MagnetEnvironment";
inline char GS_PSNAME_FACE_SYSTEM[] = "FaceParticleSystem";
inline char GS_PSNAME_XFACE_SYSTEM[] = "XFaceParticleSystem";

/* THEME: the theme loader and the caches it fills. */
inline const char GS_THEME_SKY_LOADED[] = "SKY: %s loaded";
inline const char GS_THEME_SKY_FAILED[] = "SKY: *ERROR* failed loading %s";
inline const char GS_THEME_SKY_UP[] = "%s_UP.tga";
inline const char GS_THEME_SKY_DN[] = "%s_DN.tga";
inline const char GS_THEME_SKY_FR[] = "%s_FR.tga";
inline const char GS_THEME_SKY_BK[] = "%s_BK.tga";
inline const char GS_THEME_SKY_LF[] = "%s_LF.tga";
inline const char GS_THEME_SKY_RT[] = "%s_RT.tga";
inline const char GS_THEME_VECTOR[] = "VECTOR(%f, %f, %f)\012";
inline const char GS_THEME_SOUND_PATH[] = "%s\134%s";
inline const char GS_THEME_SOUND_NONE[] = "NONE";
inline const char GS_THEME_SOUND_ADD[] = "TSM: add called (Index=%d/fn=%s)";
inline const char GS_THEME_SOUND_RELEASING[] = "TSM: trying to release all sounds";
inline const char GS_THEME_SOUND_RELEASED[] = "TSM: all sounds released";
inline const char GS_SOUNDMGR_LOG_NAME[] = "SoundManager.log";
inline const char GS_TM_LOADED[] = "TM: %s loaded";
inline const char GS_TM_FOUND[] = "TM: %s found";
inline const char GS_TM_FAILED[] = "TM: *ERROR* failed loading %s";
inline const char GS_MM_LOADED[] = "MM: %s loaded";
inline const char GS_MM_FOUND[] = "MM: %s found";
inline const char GS_MM_FAILED[] = "MM: *ERROR* failed loading %s";

#endif
