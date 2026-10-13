/* A bot run: one level played by a policy (policy.h), ended by its outcome.
 *
 * KAROO_BOT=<policy> turns it on.  The app starts the level
 * (KAROO_START_LEVEL), adds the run as the exclusive key source and calls
 * frame() at each frame boundary.  The run presses Enter to skip the
 * flythrough, then holds the keys bound to the policy's actions.  It ends at
 * the first of:
 *   complete   the level's exit is reached;
 *   death      the player dies (death_cause says how: foe, drop, bomb,
 *              void, harakiri; botrun.cpp);
 *   timeout    the level's time limit runs out;
 *   stuck      no gem for KAROO_BOT_STUCK_S seconds of game time (60);
 *   cap        KAROO_BOT_MAX_S seconds of game time in the level (900).
 * and then writes KAROO_BOT_SUMMARY, a JSON object, and asks the app to end.
 *
 * KAROO_BOT_TRACE=<file> writes the BotView (botview.h) of every tick the
 * policy acts on, one JSON object a line: the whole grid on the first line,
 * then the cells that changed.
 *
 * KAROO_BOT_ACTIONS=<file> logs every move the player makes, rest to rest or
 * step to step, with the keys that started it, where it went and how long it
 * took, one JSON object a line (tools/bot/calibrate.py).
 *
 * KAROO_BOT_FOE_LOG=<file> checks the foe model (foemodel.h): every tenth of a
 * second it runs the sim from the views 0.1, 0.5, 1 and 2 s ago, with the
 * player where it really went since, and logs each foe's predicted cell beside
 * its real one (tools/bot/foecheck.py).
 *
 * KAROO_BOT_BOMB_LOG=<file> checks the bomb model (bombmodel.h): each bomb's
 * predicted blast cell, made when it is first seen, beside the cell it is on
 * when its fuse runs out (tools/bot/bombcheck.py). */
#pragma once

#include "keysource.h"

class Game;
class ProgableControl;

namespace bot {

/* Reads the environment: 0 when KAROO_BOT is unset, 1 when the run is on,
 * -1 (logged) when KAROO_BOT names no policy. */
int init(const Game *game, const ProgableControl *controls);

/* The run as a key source; null when there is none. */
input::KeySource *source();

/* Checks the end conditions once a frame.  True on the frame the run ends,
 * once the summary is written. */
bool frame();

}  // namespace bot
