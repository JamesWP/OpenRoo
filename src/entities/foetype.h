/* What a foe does: its type, the level file's parameter byte on the foe's
 * cell (or a timed spawner's).  It decides what the foe makes for
 * (Foe::chooseTarget, Foe::step), what it leaves when it dies (Foe::spawn
 * sets its drop) and which cells its path search avoids (the search's mode is
 * the type, FoePath::passable).  A type with no behaviour of its own below
 * chases the player. */
#pragma once

enum FoeType : unsigned char {
    // Chases the player.
    FOE_CHASER        = 0x00,
    // Makes for the level exit, with eight times the chaser's search budget,
    // and stands guard there.
    FOE_EXIT_GUARD    = 0x01,
    // Makes for the nearest raised switch cell within 7, else the player.
    // Its search avoids a switch not yet thrown, and it never drops a bomb.
    FOE_SWITCH_RUNNER = 0x02,
    // Takes the gems within 5 of it (stepping onto one eats it), else
    // chases the player.
    FOE_GEM_THIEF     = 0x03,
    // With no way to the player, goes back to where it last chased to.
    FOE_RETURNER      = 0x04,
    // Follows the last catcher in the list; held while there is none.
    FOE_FOLLOWER      = 0x05,
    // Carries a gem and runs to the free cell farthest from the player.  Its
    // search avoids the player's cell.
    FOE_GEM_RUNNER    = 0x07,
    // Carries a gem, which the level counts among its crystals.
    FOE_GEM_CARRIER   = 0x0b,
    // Carries an extra life.
    FOE_LIFE_CARRIER  = 0x4d,
    // Above this: a chaser whose starting facing is the type less this (the
    // timed spawners make them).  PRESERVED: the test is strictly above, so a
    // type of exactly this is a type of its own, which chases the player.
    FOE_FACING_BASE   = 0x64,
};
