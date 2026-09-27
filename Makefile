CXX = i686-w64-mingw32-g++

WARNFLAGS = -Wall -Wextra
# One DirectDraw header version for every file.  It only selects what the
# name DDCAPS means, and nothing here takes sizeof(DDCAPS); defining it per
# header let <ddraw.h>'s own default (0x0700) win whenever it came first.
CXXFLAGS = $(WARNFLAGS) -DDIRECTDRAW_VERSION=0x0100

# Every folder under src/ is on the include path, so a file names another
# module's header by its bare name wherever the two live.
SRCDIRS = $(sort $(dir $(wildcard src/*/*.cpp)))
CXXFLAGS += $(addprefix -I,$(SRCDIRS))

SRCS = src/app/process.cpp src/app/log.cpp src/app/veh.cpp src/audio/cdm.cpp \
       src/audio/movie.cpp src/core/stream.cpp src/audio/static.cpp src/audio/cfaktsound.cpp \
       src/input/progctrl.cpp src/render/com_proxy.cpp src/render/nullddraw.cpp src/app/launcher.cpp \
       src/render/faktmesh.cpp src/render/particles.cpp src/entities/generators.cpp src/entities/factory.cpp \
       src/core/clock.cpp src/core/determinism.cpp src/core/gamestate.cpp src/testing/record.cpp \
       src/render/scenequad.cpp src/render/direct3d.cpp src/render/quadbatch.cpp src/render/sky.cpp \
       src/render/scenelight.cpp src/render/scenematerial.cpp src/render/texture.cpp src/render/texturedib.cpp \
       src/render/texturetga.cpp src/render/scenetexture.cpp src/render/dsoscene.cpp src/render/d3dmath_common.cpp \
       src/render/d3dmath_std.cpp src/render/d3dmath_mode.cpp src/render/meshbatch.cpp src/core/worldstate.cpp \
       src/testing/policy.cpp src/ui/menu.cpp src/testing/plan.cpp src/level/levelreport.cpp \
       src/ui/scoreoverlay.cpp src/entities/bridgesurf.cpp src/render/createdevice.cpp src/core/assetio.cpp \
       src/level/levelmap.cpp src/ui/highscores.cpp src/app/config.cpp src/ui/saveslots.cpp \
       src/render/model.cpp src/ui/scriptplayer.cpp src/level/extraobjects.cpp src/render/ani.cpp \
       src/level/reportwriter.cpp src/app/gamelog.cpp src/level/levelscore.cpp src/entities/breakabletile.cpp \
       src/entities/entitymath.cpp src/audio/voicepool.cpp src/entities/bomb.cpp src/entities/foe.cpp \
       src/entities/movableentity.cpp src/entities/foepath.cpp src/entities/player.cpp src/entities/bridgeobject.cpp \
       src/entities/liftobject.cpp src/audio/soundmanager.cpp src/render/textrenderer.cpp src/entities/slideobject.cpp \
       src/entities/objectremove.cpp src/level/tilequery.cpp src/core/gamereset.cpp src/ui/menutree.cpp \
       src/level/levelparse.cpp src/level/levelsetup.cpp src/level/gridrestore.cpp src/ui/textentry.cpp \
       src/ui/scoretally.cpp src/audio/cdthemes.cpp src/ui/cheatcode.cpp src/audio/soundobj.cpp \
       src/entities/checkpoint.cpp src/level/levelsounds.cpp src/ui/menunav.cpp src/input/keypress.cpp \
       src/audio/fixedsounds.cpp src/core/gametick.cpp src/core/linkedlist.cpp src/audio/doublesoundbuff.cpp \
       src/render/scene.cpp src/level/levelplacements.cpp src/level/levelentry.cpp src/core/namedlist.cpp \
       src/entities/splinepath.cpp src/level/levelobjbase.cpp src/entities/wrapperobject.cpp \
       src/entities/explodedebris.cpp src/render/objectshadows.cpp src/ui/theme.cpp src/ui/menuscreens.cpp \
       src/ui/levelselect.cpp src/input/inputsetup.cpp src/input/camerainput.cpp src/render/camera.cpp \
       src/entities/framepose.cpp src/render/themedraw.cpp src/core/game.cpp src/render/sceneobjects.cpp \
       src/render/renderstate.cpp src/render/rendergameframe.cpp src/app/main.cpp src/app/launcherdialogs.cpp \
       src/app/resources.cpp src/core/gameglobals.cpp src/app/staticinit.cpp

# ─── Build ────────────────────────────────────────────────────────────────
#
# One object per source in obj/, with header dependencies (-MMD), so an edit
# rebuilds only what it touches -- and a header edit DOES rebuild its
# includers (the old single-command build silently ignored headers).  Runs
# in parallel by default.
MAKEFLAGS += -j$(shell nproc)

BUILD   = build
OBJDIR  = $(BUILD)/obj
OBJS    = $(SRCS:src/%.cpp=$(OBJDIR)/%.o)
EXE     = $(BUILD)/KarooOwn.exe
RESOBJ  = $(OBJDIR)/karoo_rc.o
LIBS    = -lgdi32 -lwinmm -ldsound -ldinput8 -ldxguid -static-libgcc -static-libstdc++

all: $(EXE)

# The game: our objects, the resources, and exemain.cpp's entry point.
$(EXE): $(OBJS) $(OBJDIR)/app/exemain.o $(RESOBJ)
	$(CXX) $(CXXFLAGS) -mwindows -o $@ $(OBJS) $(OBJDIR)/app/exemain.o $(RESOBJ) $(LIBS) -static

$(OBJDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

# The game's own version block is not in the repository:
# tools/import_assets.py extracts them from your copy of Ka'roo into
# game/res/, which data/karoo.rc's "res/..." names resolve against.
GAMERES = $(wildcard game/res/*.bin)

game/res/version_1.bin:
	@echo "ERROR: game/res/ missing -- run tools/import_assets.py --from <your Ka'roo>" >&2; exit 1

# The launcher's window shape is ours: a mask PNG, turned into RGNDATA.
$(OBJDIR)/rgn_111.bin: data/launcher_region.png tools/png_to_rgn.py | $(OBJDIR)
	python3 tools/png_to_rgn.py $< $@

$(RESOBJ): data/karoo.rc data/karoo.ico game/res/version_1.bin $(GAMERES) $(OBJDIR)/rgn_111.bin | $(OBJDIR)
	i686-w64-mingw32-windres --include-dir=game --include-dir=data --include-dir=$(OBJDIR) $< -O coff -o $@

$(OBJDIR):
	mkdir -p $@

# A header's out-of-line functions are defined in the .cpp of the same name
# (COHESION_PLAN.md template point 11).  Reads the objects, so build first.
check-homes: $(OBJS)
	python3 tools/check_homes.py src $(OBJDIR)

-include $(OBJS:.o=.d) $(OBJDIR)/app/exemain.d

.PHONY: all check-homes clean

clean:
	rm -rf $(BUILD)
