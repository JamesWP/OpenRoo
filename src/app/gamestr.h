/* The game's own strings: log formats, file names, action names, report layout
 * and the score overlay's labels, one array each, named GS_<GROUP>_<WHAT> by
 * the subsystem that uses them.
 *
 * `inline` gives each array a single address across every file, so the places
 * that compare these strings by pointer still work. */

#ifndef KAROO_GAMESTR_H
#define KAROO_GAMESTR_H

/* FMT: one-conversion formats shared by several files. */
inline const char GS_FMT_S[] = "%s";
inline const char GS_FMT_NEWLINE[] = "\012";
inline const char GS_FMT_TAB[] = "\011";
inline const char GS_FMT_D[] = "%d";

/* CD: the CD player. */
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
inline const char GS_HIGHSCORE_DEFAULT_NAME[] = "Open Roo";

/* GAME: the game object's lifecycle. */
inline const char GS_GAME_DEMO_LEVEL[] = "DemoLevelForest";
inline const char GS_GAME_FILE_PATH[] = "%s\134%s.gam";
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

/* GAME: game flow. */
inline const char GS_GAME_HSFILE[] = "jj";
inline const char GS_GAME_MAIN[] = "Main";
inline const char GS_GAME_FINAL[] = "Final";
inline const char GS_GAME_FINAL_DIR[] = "Final\134%s";
inline const char GS_GAME_COMPLETED[] = "completed";
inline const char GS_GAME_GAMEOVER[] = "gameover";

/* OPEN: opening a level and its script. */
inline const char GS_OPEN_FMT_SCRIPTS[] = "%s\134InstructionScripts\134%s";
inline const char GS_OPEN_FMT_LEVELS[] = "%s\134Levels\134%s";
inline const char GS_OPEN_FMT_GAM[] = "%s.gam";

/* WAV: the seven fixed wave paths. */
inline const char GS_WAV_SPLAT[] = "%s\134waves\134splat.wav";
inline const char GS_WAV_LEVEL_COMPLETED[] = "%s\134waves\134LevelCompleted.wav";
inline const char GS_WAV_SWITCH[] = "%s\134waves\134Switch.wav";
inline const char GS_WAV_MENU_UP_DOWN[] = "%s\134waves\134MenuUpDown.wav";
inline const char GS_WAV_COUNT[] = "%s\134waves\134Count.wav";
inline const char GS_WAV_LAST_SECONDS[] = "%s\134waves\134LastSeconds.wav";
inline const char GS_WAV_TIME_OUT[] = "%s\134waves\134TimeOut.wav";

/* CHEAT: the typed cheat codes. */
inline const char GS_CHEAT_FMT_LVL_PATH[] = "%s\134Levels\134%s.jjm";

/* RPT: the level report. */
inline const char GS_RPT_FILE[] = "LevelReport.txt";
inline const char GS_RPT_HSC_NAME[] = "jj.hsc";
inline const char GS_RPT_SPLINES_IN[] = "\012Splines in Scripts:%d";
inline const char GS_RPT_TEXTS_IN[] = "\012Texts in Scripts:%d";
inline const char GS_RPT_TESTSCORES[] = "\012Testscores:%d";
inline const char GS_RPT_TALLY[] = "\011\011%d\011%d\011%d\012";
inline const char GS_RPT_LVL_NAME[] = "** Levelname: %s\012";
inline const char GS_RPT_LVL_FILE[] = "** Level %d  Filename:%s \012";
inline const char GS_RPT_STARS[] = "*********************************************************************\012";
inline const char GS_RPT_DEFAULT_NAME[] = "Open Roo";
inline const char GS_RPT_S_TAB[] = "%s\011";
inline const char GS_RPT_BLANK_TAB[] = " \011";
inline const char GS_RPT_X_TAB[] = "X\011";
inline const char GS_RPT_D_TAB[] = "%d\011";
inline const char GS_RPT_RULE[] = "---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\012";
inline const char GS_RPT_COLHDR2[] = "nr\011 \011level \012";
inline const char GS_RPT_COLHDR1[] = "level-\011world\011bonus\011IS\011LEO\011Catch\011Throw\011Cryst\011HCryst\011NCryst\011HLifes\011Fields\011Obst\011Elev\011Platf\011Destr\011Glue\011Jump\011Telep\011Slide\011Ice\011Brigde\011Lifes\011Freeze\011Protect\011Paragl\011Speed\011Bomb\011Time\011Scores\012";
inline const char GS_RPT_LEVELS[] = "Levels:%d\012\012";
inline const char GS_RPT_GAMEFILE[] = "gamefile:%s\012";
inline const char GS_RPT_TITLE[] = "***** level report ******\012\012";
inline const char GS_RPT_SCRIPTTEXTS[] = "ScriptTexts.txt";

/* SND: the level-based sounds. */
inline const char GS_SND_CANDY[] = "Candy";
inline const char GS_SND_SPACE[] = "Space";
inline const char GS_SND_EGYPT[] = "Egypt";

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
inline const char GS_TEX_DOT_TGA_UPPER[] = ".TGA";
inline const char GS_TEX_DOT_TGA_LOWER[] = ".tga";
inline const char GS_TEX_DOT_BMP_UPPER[] = ".BMP";
inline const char GS_TEX_DOT_BMP_LOWER[] = ".bmp";

/* PS: the particle-system loader. */
inline const char GS_PS_NAME_NULL[] = "NULL";
inline const char GS_PS_MSG_ENVLOAD[] = "PS: Error while loading Particlesystem, because Environment coud not loaded";
inline const char GS_PS_MSG_ENVNAME[] = "PS: Error while loading Particlesystem, because Environment of Typ: '%s' coud not created";
inline const char GS_PS_MSG_NOENV[] = "PS: Error while loading Particlesystem, because could not read Environment-ClassName";
inline const char GS_PS_MSG_GENLOAD[] = "PS: Error while loading Particlesystem, because Generator coud not loaded";
inline const char GS_PS_MSG_NOGEN[] = "PS: Error while loading Particlesystem, because Generator of Typ: '%s' coud not created";
inline const char GS_PS_MSG_NONAME[] = "PS: Error while loading Particlesystem, because could not read GeneratorclassName";

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
inline const char GS_THEME_SKY_UP[] = "%s_UP.tga";
inline const char GS_THEME_SKY_DN[] = "%s_DN.tga";
inline const char GS_THEME_SKY_FR[] = "%s_FR.tga";
inline const char GS_THEME_SKY_BK[] = "%s_BK.tga";
inline const char GS_THEME_SKY_LF[] = "%s_LF.tga";
inline const char GS_THEME_SKY_RT[] = "%s_RT.tga";
inline const char GS_THEME_SOUND_PATH[] = "%s\134%s";
inline const char GS_THEME_SOUND_NONE[] = "NONE";

#endif
