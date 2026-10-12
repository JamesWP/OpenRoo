/* Level select, an addition of ours: a hidden page off the Load Game screen
 * (menu node 2).  Right opens it; left and right change theme, up and down
 * pick a level, Enter starts a fresh game there, Escape goes back.
 *
 * It is built from ordinary menu-tree nodes: node 0x60 + t is theme t's page,
 * whose children are its levels, each leading to LS_START (0x70), the action
 * node Enter descends to (session/levelthemes.h).  Levels are grouped by the
 * theme folder that prefixes their name in JJ.GAM ("Forest\Start" is Forest).
 * This file is the page's logic; what it draws is screens/levelselectview.h. */
#pragma once

class FrontEnd;
class Session;

/* Called on a press of right or left by the keypress handler (dir
 * +1 or -1): from node 2, right opens the page of the last-played level's
 * theme; on a theme page, either moves to the next or previous theme. */
void LevelSelect_Turn(FrontEnd *frontEnd, Session *session, int dir);

/* At LS_START: pops back to the theme page and returns the JJ.GAM index of the
 * row Enter was pressed on. */
int LevelSelect_Chosen(FrontEnd *frontEnd, Session *session);
