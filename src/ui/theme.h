#pragma once

/* ASSET_PLAN.md Phase 5 -- the .thm grammar, as a reader that only observes.
 *
 * theme.cpp owns the tokenizer and the four depth tables that Phase 5's
 * replacement of ThemeFileLoader 0x0040c110 will be built on.  Nothing here
 * writes to the game; it is switched on by KAROO_THEME_DIAG and is inert
 * otherwise.
 *
 * The tables are the same ones tools/thmparse.py carries, and
 * `python3 tools/thmparse.py --check-cpp` asserts the two agree keyword for
 * keyword.  Keep them in step or that check fails.
 */

/* Called from hooks_fopen for every open.  Does nothing unless the path names
 * a .thm and KAROO_THEME_DIAG is set. */
void theme_diag_on_open(const char *path);
