/* The level select's theme table: the game file's levels grouped by the theme
 * folder that prefixes their names in JJ.GAM ("Forest\Start" is Forest), and
 * the page's scroll position.  flow/levelselect.cpp builds it when the page
 * opens; the level select's menu nodes are 0x60 + t for theme t. */
#pragma once

class Session;

/* The game's own tree uses nodes 0..0x50 and 200 up, so this range is free.
 * Node LS_THEME_NODE + t is theme t's page, whose children are its levels,
 * each leading to LS_START, the action node Enter descends to. */
static const unsigned char LS_THEME_NODE = 0x60;  // 0x60 .. 0x6f
static const int           LS_MAX_THEMES = 16;
static const unsigned char LS_START      = 0x70;

/* Rebuilds the table from the session's levels.  JJ.GAM lists each theme's
 * levels together, so a theme is a run of equal folder names; a folder that
 * recurred later would get a second page. */
void LevelThemes_Build(Session *session);
/* The number of themes in the table. */
int  LevelThemes_Count(void);
/* The JJ.GAM index theme t's first level has, and how many levels it holds. */
int  LevelThemes_First(int t);
int  LevelThemes_Size(int t);
/* Theme t's folder name. */
const char *LevelThemes_Name(int t);
/* The first visible row of the page. */
int  LevelThemes_Top(void);
void LevelThemes_SetTop(int top);
/* Node 0x60 + t, for t below the theme count. */
bool LevelThemes_IsThemeNode(unsigned char node);
