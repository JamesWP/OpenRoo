#include "botrun.h"
#include <chrono>
#include <fstream>
#include <memory>
#include <set>
#include <stdlib.h>
#include <strings.h>
#include <string>
#include "binio.h"
#include "game.h"
#include "keycodes.h"
#include "logger.h"
#include "player.h"
#include "capture.h"
#include "checks.h"
#include "policies.h"
#include "progctrl.h"
#include "sysdev.h"
#include "dbg.h"

namespace bot {

namespace {

double env_seconds(const char *name, double fallback)
{
    const std::string s = sysdev::getEnv(name);
    return s.empty() ? fallback : atof(s.c_str());
}

/* The first key bound to a mode-1 action, or KEY_NONE. */
int bound_key(const ProgableControl &pc, const char *action)
{
    for (const ActionEntry *a = pc.actionTable(1).first(); a; a = a->chain)
        if (strcasecmp(a->name, action) == 0)
            return a->kbd ? a->kbd->scancode : input::KEY_NONE;
    return input::KEY_NONE;
}

class Run : public input::KeySource {
public:
    /* interactive: engaged from the debug UI.  Its end leaves the game
     * running, with the keyboard back; it has no summary or trace. */
    Run(const Game *game, const ProgableControl &pc, std::unique_ptr<Policy> policy, bool interactive)
        : game_(game), policy_(std::move(policy)), interactive_(interactive)
    {
        keyFor_[0] = bound_key(pc, "John_Move_Forward");
        keyFor_[1] = bound_key(pc, "John_Move_Back");
        keyFor_[2] = bound_key(pc, "John_Turn_Left");
        keyFor_[3] = bound_key(pc, "John_Turn_Right");
        keyFor_[4] = bound_key(pc, "John_Release_Bomb");
        stuckMs_ = env_seconds("KAROO_BOT_STUCK_S", 60) * 1000.0;
        capMs_   = env_seconds("KAROO_BOT_MAX_S", 900) * 1000.0;
        if (interactive)
            return;
        summary_ = sysdev::getEnv("KAROO_BOT_SUMMARY");
        const std::string trace = sysdev::getEnv("KAROO_BOT_TRACE");
        if (!trace.empty())
            trace_.open(sysdev::nativePath(trace));
    }

    bool exclusive() const override { return true; }
    bool done() const { return done_; }
    const std::string &outcome() const { return outcome_; }
    const Policy &policy() const { return *policy_; }
    /* Wall-clock ms the policy's act took: the last tick's, the slowest. */
    double actMs() const { return actMs_; }
    double actMostMs() const { return actMostMs_; }

    void sample(unsigned char keys[input::KEY_COUNT]) override
    {
        if (done_)
            return;
        if (game_->state() == GameState::Flythrough) {
            // Enter is a press: down one tick, up the next.
            if ((enterTick_++ & 1) == 0)
                keys[input::KEY_RETURN] = 0x80;
            return;
        }
        if (game_->state() != GameState::Playing || game_->world()->player()->moveState() != 0)
            return;
        BotView view = capture(*game_);
        if (trace_)
            trace_ << view.toJson(frames_, haveLast_ ? &last_ : nullptr) << '\n';
        checks_.observe(view, frames_, held_);
        const auto t0 = std::chrono::steady_clock::now();
        const unsigned acts = policy_->act(view);
        actMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        actMostMs_ = actMs_ > actMostMs_ ? actMs_ : actMostMs_;
        held_     = acts;
        last_     = std::move(view);
        haveLast_ = true;
        for (int i = 0; i < 5; i++)
            if ((acts & (1u << i)) && keyFor_[i] != input::KEY_NONE)
                keys[keyFor_[i]] = 0x80;
    }

    bool frame()
    {
        if (done_)
            return false;
        frames_++;
        const World  *w  = game_->world();
        const Player *pl = w->player();
        // A level can end in the tick it is completed, leaving play for the
        // tally at once.
        if (playing_ && game_->state() == GameState::LevelComplete) {
            done_ = true;
            policy_->end(capture(*game_));
            trace_.close();
            write("complete", "");
            return !interactive_;
        }
        if (game_->state() != GameState::Playing)
            return false;
        if (!playing_) {
            playing_     = true;
            startMs_     = w->now();
            startFrame_  = frames_;
            lastProgressMs_ = w->now();
        }
        /* Progress is a gem, a foe fewer, or a cell never stood on before: a
         * level with a long way to its exit (lift after lift) is not stuck,
         * nor a bot waiting while its foes die, a bot going round the same
         * cells is. */
        const int cell = (pl->cellU() * 256 + pl->cellV()) * 256 + pl->heightCell();
        const size_t foes = w->foes().size();
        if (pl->gemsCollected() != lastGems_ || foes < lastFoes_ || visited_.insert(cell).second) {
            lastGems_       = pl->gemsCollected();
            lastProgressMs_ = w->now();
        }
        lastFoes_ = foes;

        const char *outcome = nullptr;
        const char *cause   = "";
        if (pl->held() != 0) {
            outcome = "complete";
        } else if (pl->moveState() == 3) {
            outcome = "timeout";
        } else if (pl->moveState() != 0) {
            outcome = "death";
            cause   = capture(*game_).deathCause();
        } else if (w->now() - lastProgressMs_ > stuckMs_) {
            outcome = "stuck";
        } else if (w->now() - startMs_ > capMs_) {
            outcome = "cap";
        }
        if (!outcome)
            return false;
        done_ = true;
        policy_->end(capture(*game_));
        trace_.close();
        write(outcome, cause);
        return !interactive_;
    }

private:
    void write(const char *outcome, const char *cause)
    {
        outcome_ = std::string(outcome) + (*cause ? " by " : "") + cause;
        const World   *w  = game_->world();
        const Player  *pl = w->player();
        const Session *s  = game_->session();
        g_logger.write("bot: %s%s%s after %lu frames, gems %d/%d\n", outcome,
                       *cause ? " by " : "", cause, (unsigned long)(frames_ - startFrame_),
                       pl->gemsCollected(), w->gemsRequired());
        if (summary_.empty())
            return;
        std::ofstream fp(sysdev::nativePath(summary_));
        if (!fp) {
            g_logger.write("bot: cannot write %s\n", summary_.c_str());
            return;
        }
        std::string name;
        for (const char *c = s->levelNameTableEntry(s->levelIndex()); *c; c++) {
            if (*c == '"' || *c == '\\')
                name += '\\';
            name += *c;
        }
        printTo(fp, "{\n");
        printTo(fp, "  \"outcome\": \"%s\",\n", outcome);
        printTo(fp, "  \"death_cause\": \"%s\",\n", cause);
        std::string note;
        for (char c : policy_->note()) {
            if (c == '"' || c == '\\')
                note += '\\';
            note += c;
        }
        printTo(fp, "  \"note\": \"%s\",\n", note.c_str());
        printTo(fp, "  \"level_index\": %u,\n", (unsigned)s->levelIndex());
        printTo(fp, "  \"level\": \"%s\",\n", name.c_str());
        printTo(fp, "  \"frames\": %lu,\n", (unsigned long)(frames_ - startFrame_));
        printTo(fp, "  \"game_ms\": %.0f,\n", w->now() - startMs_);
        printTo(fp, "  \"gems_collected\": %d,\n", pl->gemsCollected());
        printTo(fp, "  \"gems_required\": %d,\n", w->gemsRequired());
        printTo(fp, "  \"time_limit_s\": %d,\n", w->timeLimit());
        printTo(fp, "  \"cell\": [%d, %d, %d]\n", pl->cellU(), pl->cellV(), pl->heightCell());
        printTo(fp, "}\n");
    }

    const Game              *game_;
    std::unique_ptr<Policy>  policy_;
    bool        interactive_;
    std::string outcome_;   // set as the run ends
    int         keyFor_[5];
    double      stuckMs_, capMs_;
    std::string summary_;
    std::ofstream trace_;   // KAROO_BOT_TRACE: the view each tick it acts
    BotView     last_;
    Checks      checks_;
    unsigned    held_ = 0;
    double      actMs_ = 0, actMostMs_ = 0;
    bool        haveLast_ = false;
    bool        done_    = false;
    bool        playing_ = false;
    unsigned    enterTick_ = 0;
    unsigned long frames_ = 0, startFrame_ = 0;
    double      startMs_ = 0, lastProgressMs_ = 0;
    int         lastGems_ = 0;
    size_t      lastFoes_ = (size_t)-1;
    std::set<int> visited_;
};

std::unique_ptr<Run> s_run;
const Game             *s_game;
const ProgableControl  *s_controls;
bool                    s_fromEnv;
unsigned                s_runLevel;   // the level an interactive run started on
std::string             s_lastOutcome;

/* Stands in for the run in the app's list of sources, so a run can come and
 * go from the debug UI after the sources are set up. */
class Proxy : public input::KeySource {
public:
    bool exclusive() const override { return s_run && !s_run->done(); }
    void sample(unsigned char keys[input::KEY_COUNT]) override
    {
        if (s_run)
            s_run->sample(keys);
    }
} s_proxy;

void engage(bool on)
{
    s_run.reset();
    if (!on)
        return;
    s_run = std::make_unique<Run>(s_game, *s_controls, makePolicy("plan"), true);
    s_runLevel = s_game->session()->levelIndex();
    s_lastOutcome.clear();
    g_logger.logMessage(1, "BOT: engaged from the debug UI");
}

/* The debug UI's toggle: the plan policy plays the level in place of the
 * keyboard until it ends or is untoggled. */
void debug_panel()
{
    if (!dbg::active() || s_fromEnv)
        return;
    dbg::Section s("Bot");
    if (!s)
        return;
    bool on = s_run && !s_run->done();
    if (ImGui::Checkbox("bot plays (plan)", &on))
        engage(on);
    if (!s_lastOutcome.empty())
        ImGui::TextDisabled("last run: %s", s_lastOutcome.c_str());
    if (!s_run || s_run->done())
        return;
    ImGui::Text("act: %.2f ms, slowest %.2f ms", s_run->actMs(), s_run->actMostMs());
    const std::vector<Thought> thoughts = s_run->policy().thoughts();
    if (thoughts.empty() || !ImGui::BeginTable("thoughts", 2, ImGuiTableFlags_SizingFixedFit))
        return;
    // The text column takes the rest of the width: a fixed-fit column has
    // none to wrap in, and wraps at every character.
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("text", ImGuiTableColumnFlags_WidthStretch);
    for (const Thought &t : thoughts) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", t.label.c_str());
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", t.text.c_str());
    }
    ImGui::EndTable();
}

}  // namespace

int init(const Game *game, const ProgableControl *controls)
{
    s_game     = game;
    s_controls = controls;
    const std::string name = sysdev::getEnv("KAROO_BOT");
    if (name.empty())
        return 0;
    std::unique_ptr<Policy> policy = makePolicy(name);
    if (!policy) {
        g_logger.logMessage(4, "BOT: KAROO_BOT=%s names no policy", name.c_str());
        return -1;
    }
    g_logger.logMessage(1, "BOT: playing with policy %s", name.c_str());
    s_run     = std::make_unique<Run>(game, *controls, std::move(policy), false);
    s_fromEnv = true;
    return 1;
}

input::KeySource *source() { return &s_proxy; }

bool active() { return s_fromEnv; }

bool frame()
{
    if (s_fromEnv)
        return s_run->frame();
    if (s_run) {
        // A level loaded under the run is a fresh one: the run starts again.
        if (s_game->session()->levelIndex() != s_runLevel)
            engage(true);
        s_run->frame();
        if (s_run->done()) {
            s_lastOutcome = s_run->outcome();
            s_run.reset();
        }
    }
    debug_panel();
    return false;
}

}  // namespace bot
