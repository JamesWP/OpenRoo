/* Attaching a foe's sounds from the level's sound table. */
#pragma once

class Game;
class Foe;

/* Loads each sound whose name is set into the foe; catchers and
 * throwers each get their own three. */
void Sim_AcquireFoeSounds(Game *g, Foe &foe);
