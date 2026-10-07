The build uses cmake; the full build process (native Linux and Windows/mingw) is documented in README.md. Install the toolchain it lists if you dont already have it.

To test you should build with cmake (see README.md) to check things compile. and then consider running a smoke test consisting of one of the pre recorded gameplay sessions. e.g. using `python3 tools/replaytest.py bombstart-crash`. Test using the whole suite (omit session name) only when completing a body of work since it takes a few minutes.

The code which forms the game here is not high quality, and isnt indicative of good examples. its mostly from a reverse engineer of the original and has lots of artifacts of that which we will be cleaning up as we go. We should be refactoring the code so that eventually its more robust and easy to work with and understand.

Dont edit claude.md yourself directly, instead suggest edits you think we should make. and if the user agrees, make the edit directly to the git history and dont change the file on disk (it causes the llm cache to be tainted) the changes will go live starting from the next session only.

For most sessions the user is present and can answer questions or give clarifying remarks if needed, this isnt an autonomous get as much done as we can in one go project.

The long term goal is to make this a cross platform game, maybe create our own license free assets. and add some novel features to the game or explore different engine architectures.

Unless otherwise stated, all completed work should be pushed as a PR so that the user can review. you should always work from origin/main in case that is ahead of the local checkout.

The game assets (which are licensed) are not included in the repo but instead are populated by a run of `python3 tools/import_assets.py` using a zip, the user can provide this, but you should check to see if some pre extracted assets are available in the main worktree under `game/` and you should prefer symlinking that.

The original game had some odd bits of code some of which are bugs, these have been preserved, however we should take the oppertunity to remove them if we see fit. very likely the bug is some edgecase which dosent occur, or isnt gameplay impacting. those should be removed when we are modifying the code.

Unless otherwise stated, test only on the linux build. don't test both platforms build or pass the tests unless explicitly given permission.
