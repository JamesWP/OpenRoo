# A pure engine: design

The game's rules as a plain, copyable state and a tick function, apart from
everything that presents them.  The game runs on it; the bot copies it and
steps the copy forward to see what its plans lead to.

## 1. Why

The bot predicts the game with its own copies of the rules (`botmodel`: the
nav model, the foe model, the bomb, falling tile and lift models).  Those
copies drift from the engine, leave things out, and the planner and tactics
patch over the gaps with special cases (docs/BOT_DESIGN.md).  The engine is
the authority, so the bot should run the engine.

Today it cannot: the rules are tangled with sound, drawing, the camera, the
menus and the logger, and the entities point into the world around them, so
the world cannot be copied.

## 2. The shape

```
            presses                       events
  input ─────────────▶ tick(state) ──────────────────▶ audio, log, bot
                          │   ▲
                          ▼   │        copy = a clone to look ahead with
                       State (plain data)
                          │
                          ▼ read, with the frame's time
                       renderer ◀── theme assets (models, textures, sounds)
```

- **State**: plain data that copies with `=`.  The map, the player, foes,
  bombs, bridges, lifts, platforms, falling tiles, spawners; the clock; the
  random state; the gameplay counters (foes killed, gems, time used); the
  level's status (playing, complete, timed out).  No pointers, no sound
  handles, no theme.
- **tick(state, presses, events)**: the rules.  The state and the presses
  in; the next state and this tick's events out.  It calls nothing outside.
- **Renderer**: draws from the state and the frame's time alone.
- **Audio**: plays from the events alone.
- **Shell** (`Game`): menus, camera, music and the score tally, reacting to
  the level's status and to events.

## 3. Rules

1. **The renderer keeps no memory.**  Everything a draw needs is in the
   state or follows from it and the time: closed form.  A thing that starts
   at a moment (a death, a blast, a pickup) keeps its start time in the
   state, and the draw works from its age.  Particles and texture animators
   are functions of (entity id, start time, now), not systems stepped each
   frame.
2. **Audio only from events, and continuous sounds start and stop.**  A
   one-shot sound is an event.  A looping sound (sliding, rolling) gets a
   start event and a stop event, keyed by entity and sound.  Every start is
   matched by a stop: the rules emit the stops when an entity is removed and
   when the level ends or restarts.
3. **One event stream.**  Sound, visual triggers, the log, the score and the
   bot read the same events.  The rules tell the outside world nothing any
   other way.
4. **Entities have stable ids.**  A sound, an effect or the bot names an
   entity by id, not by its place in a list, which shifts as entities are
   removed.
5. **The rules' randomness is in the state, and only there.**  Cosmetic
   randomness (particles) has its own seeded source, so drawing can never
   change play.
6. **Every step of the change is byte-identical.**  The replay tests
   (`tools/replaytest.py`) must pass unchanged at every commit.  Behaviour
   changes, such as tick-count time in place of millisecond doubles, are
   separate and deliberate.

## 4. Where it stands, and what is in the way

- **Drawing is nearly pure already.**  Each entity's `draw()` is `const` and
  reads its own fields; `drawhelpers/themedraw` knows nothing of the game.
  But the draw code lives on the rule classes (`Foe::draw`, `Bomb::draw`),
  the death effects fire from a "last drawn" count the renderer holds, the
  particle systems and UV animators step frame by frame, and the tick calls
  `debugDraw`.
- **Sound is in the rules.**  Each `MovableEntity` holds twelve
  `SoundVoice*`, acquired at spawn (`Sim_AcquireFoeSounds` reaches into
  `Game`) and played mid-tick; a slide starts a loop and stepping off stops
  it.
- **Entities point into the world.**  `EntityContext` hands each the clock,
  tick step, map, death counter and sound library as raw pointers.
- **Randomness is global.**  The player's random pickup calls `crt_rand()`,
  shared with level setup.
- **Rules live in `Game` and the session.**  `Play_TickFoes` reads the game
  state to hold the foes, and the session's kill count to choose drops;
  `Play_CheckLevelEnd` mixes the level's end with the camera, menus, score
  and music.
- **Removal shifts the lists**, so nothing outside can follow an entity.

Themes, models and textures are assets, loaded per level; the state holds
only tile kinds and object types, which the renderer looks up.  They need no
change.

## 5. The order

One branch; each commit builds, passes the replay tests, and is one
reviewable change.

1. Stable entity ids.
2. Events, and an audio director in place of the sound calls in the tick:
   the one-shots, then the loops' start and stop.
3. Drawing off the rule classes; the debug draws out of the tick.
4. Closed-form drawing: death and blast effects from start times, then the
   particle systems and the UV animators.
5. `EntityContext`'s pointers become arguments; the rules' random state goes
   into the state.
6. The rules in `Game` and the session move into the state: the foes' hold,
   the kill count, the level's end.
7. The state copies; a check steps a copy beside the live game and requires
   them identical.
8. The bot's world sim (`src/botworld`) runs on it, measured by
   `tools/bot/simcheck.py`.

## 6. Measuring

- **The replay tests** prove each step changes nothing.  Where the human
  corpus has gaps, bot runs (`tools/bot/backtest.py`) record replays that can
  be blessed into it.
- **The clone check** (step 7) proves a copy predicts the game exactly.
- **simcheck** (`KAROO_BOT_SIM_LOG`) measures the bot's sim against the game
  by part of the world and look-ahead, from the baseline of nothing moving.
