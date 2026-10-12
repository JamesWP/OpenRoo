#include <strings.h>
#include "sysdev.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "launcherdialogs.h"
#include "audiodev.h"
#include "windev.h"
#include "renderdevice.h"
#include "game.h"
#include "config.h"
#include "logger.h"

static const char SND_SWITCH[] = "waves\\switch.wav";
static const char SND_IMPACT[] = "waves\\mineimpact.wav";
static const char SND_UGH[]    = "waves\\ugh.wav";

/* The display adapters and modes, from the render device, and the choice into
 * the Config. */
class DisplayModel : public windev::LauncherModel {
public:
    explicit DisplayModel(const RenderDevice &dev) : dev_(dev) {}

    bool listAdapters(std::vector<std::string> &names) override
    {
        adapters_.clear();
        if (!dev_.EnumerateAdapters(adapters_))
            return false;
        for (size_t i = 0; i < adapters_.size(); i++)
            names.push_back(adapters_[i].name);
        return true;
    }

    int configuredAdapter() override
    {
        // The last adapter whose GUID matches; the primary one has none.
        const AdapterId *want = Game::instance()->config()->adapterId();
        int pick = -1;
        for (size_t i = 0; i < adapters_.size(); i++)
            if (adapters_[i].hasGuid
                && memcmp(&adapters_[i].id, want, sizeof(AdapterId)) == 0)
                pick = (int)i;
        return pick;
    }

    bool listModes(int adapter, std::vector<std::string> &names) override
    {
        const AdapterId *id = adapter >= 0 && adapters_[adapter].hasGuid
                            ? &adapters_[adapter].id : NULL;
        std::vector<DisplayMode> modes;
        if (!dev_.EnumerateDisplayModes(id, modes))
            return false;
        for (size_t i = 0; i < modes.size(); i++) {
            char text[64];
            snprintf(text, sizeof(text), "%ux%ux%u", (unsigned)modes[i].dwWidth,
                     (unsigned)modes[i].dwHeight, (unsigned)modes[i].dwBitDepth);
            names.push_back(text);
        }
        return true;
    }

    int configuredMode() override
    {
        return (int)Game::instance()->config()->displayModeIndex();
    }

    void choose(int adapter, int mode) override
    {
        Config *cfg = Game::instance()->config();
        if (adapter >= 0 && adapters_[adapter].hasGuid)
            *cfg->adapterId() = adapters_[adapter].id;
        else
            *cfg->adapterId() = AdapterId();
        cfg->setDisplayModeIndex((unsigned int)mode);
    }

    void playSound(Sound sound) override
    {
        switch (sound) {
        case Sound::Switch: audiodev::playSystemSound(SND_SWITCH, true);  break;
        case Sound::Impact: audiodev::playSystemSound(SND_IMPACT, true);  break;
        case Sound::Ugh:    audiodev::playSystemSound(SND_UGH, false);    break;
        }
    }

private:
    const RenderDevice  &dev_;
    std::vector<Adapter> adapters_;
};

bool LauncherDlg_Show(void *parent, const RenderDevice &dev)
{
    DisplayModel model(dev);
    return windev::showLauncher(parent, model);
}
