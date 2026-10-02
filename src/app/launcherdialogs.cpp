/* KAROO_LAUNCHERDLG_FX=allaspect is a negative control: the device dialog
 * lists every aspect ratio instead of 4:3 only. */

#include <windows.h>
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

static bool fx_allaspect()
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = GetEnvironmentVariableA("KAROO_LAUNCHERDLG_FX", buf, sizeof(buf))
                 && lstrcmpiA(buf, "allaspect") == 0;
        g_logger.write("launcherdlg: FX mode = %s\n", cached ? "allaspect" : "off");
    }
    return cached != 0;
}

/* The display adapters and modes, from the render device, and the choice into
 * the Config. */
class DisplayModel : public windev::LauncherModel {
public:
    bool listAdapters(std::vector<std::string> &names) override
    {
        adapters_.clear();
        if (!RenderDevice::EnumerateAdapters(adapters_))
            return false;
        for (size_t i = 0; i < adapters_.size(); i++)
            names.push_back(adapters_[i].name);
        return true;
    }

    int configuredAdapter() override
    {
        // The last adapter whose GUID matches; the primary one has none.
        const GUID *want = Game::instance()->config()->adapterGuid();
        int pick = -1;
        for (size_t i = 0; i < adapters_.size(); i++)
            if (adapters_[i].hasGuid
                && memcmp(&adapters_[i].guid, want, sizeof(GUID)) == 0)
                pick = (int)i;
        return pick;
    }

    bool listModes(int adapter, std::vector<std::string> &names) override
    {
        const GUID *guid = adapter >= 0 && adapters_[adapter].hasGuid
                         ? &adapters_[adapter].guid : NULL;
        std::vector<DisplayMode> modes;
        if (!RenderDevice::EnumerateDisplayModes(guid, modes, fx_allaspect()))
            return false;
        for (size_t i = 0; i < modes.size(); i++) {
            char text[64];
            snprintf(text, sizeof(text), "%lux%lux%lu", modes[i].dwWidth,
                     modes[i].dwHeight, modes[i].dwBitDepth);
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
            *cfg->adapterGuid() = adapters_[adapter].guid;
        else
            memset(cfg->adapterGuid(), 0, sizeof(GUID));
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
    std::vector<Adapter> adapters_;
};

bool LauncherDlg_Show(void *parent)
{
    DisplayModel model;
    return windev::showLauncher(parent, model);
}
