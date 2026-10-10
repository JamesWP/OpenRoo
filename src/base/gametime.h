/* The game's time(): seconds, also stored through out when it is not NULL.
 * DETERMINISM: the particle samplers and the level builder seed rand() from
 * it; KAROO_SEED fixes it. */
#pragma once

int hooks_GameTime(int *out);
