/* The particle gallery (particlegallery.h). */

#include "particlegallery.h"
#include <math.h>
#include <strings.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "camera.h"
#include "inputdev.h"
#include "logger.h"
#include "renderdevice.h"
#include "renderer.h"
#include "sysdev.h"
#include "themedraw.h"
#include "windev.h"
#include "clock.h"
#include "dbg.h"
#include <memory>

using namespace input;

namespace {

/* The themes that have particle systems (FINAL has none). */
const char *const kThemes[] = { "Forest", "Castle", "Egypt", "Space", "Water", "Candy" };
const int kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);

/* Each look's keyword in the theme file, in ThemeObjectType's order. */
const char *const kLookNames[THEME_OBJ_COUNT] = {
    "john", "catcher", "catcherfx", "thrower", "throwerfx", "plate", "side",
    "platform", "paraglide", "paraglidefx", "elevator", "exit", "glue",
    "destructfield", "destructfieldfx", "jumppad", "slide", "stair", "teleporter",
    "crystal", "crystalfx", "ammunition", "bomb", "explosion", "surprise",
    "freeze", "speed", "speedfx", "collfx", "life", "switch", "time", "ice",
    "obstacle", "obstaclefx", "protection", "protectionfx", "bridge",
};

const float kSpacing = 3.0f;       // between looks, in tiles
const double kBurstEvery = 1500.0; // ms between a burst look's bursts
const double kFireEvery = 3000.0;  // ms between a dying look's deaths
const double kFireFor = 400.0;     // and how long each one fires

/* How the game plays a look's particle systems. */
enum class Play {
    Continuous,  // emitting wherever the look stands
    Burst,       // spawnBurst / bursts: a second's puff at a point
    Trail,       // the speed trail, behind a moving player
    Fire,        // a dying or exploding look: emits while it dies
};

Play play_of(int look)
{
    switch (look) {
    case THEME_OBJ_CRYSTAL_PICKUP:
    case THEME_OBJ_ITEM_PICKUP:
    case THEME_OBJ_FALLING_DUST:
        return Play::Burst;
    case THEME_OBJ_PLAYER_SPEED_TRAIL:
        return Play::Trail;
    case THEME_OBJ_FOE_CATCHER_DYING:
    case THEME_OBJ_FOE_THROWER_DYING:
    case THEME_OBJ_BOMB_EXPLODING:
    case THEME_OBJ_BOMBABLE_EXPLODING:
        return Play::Fire;
    default:
        return Play::Continuous;
    }
}

bool has_particles(ThemeAssetBlock &theme, int look)
{
    ThemeObjectTypeSlot *slot = theme.slot(look);
    for (uint32_t r = 0; r < slot->instanceCount(); r++) {
        ThemeLevelObject &rec = slot->records()[r];
        if (rec.kind() == THEME_KIND_PARTICLESYSTEM && rec.particleSystems()[0])
            return true;
    }
    return false;
}

/* The projection the game draws with (render/renderstate.cpp). */
Mat4 projection()
{
    const double half = 0.7853981852531433;
    const float c = (float)cos(half), sn = (float)sin(half);
    const float q = (float)(sn * 1.001001000404358);
    Mat4 proj = {};
    proj.m[0] = c;
    proj.m[5] = c;
    proj.m[10] = q;
    proj.m[11] = sn;
    proj.m[14] = (float)(q * -0.10000000149011612);
    return proj;
}

/* Where world point p lands on the screen, in display pixels; false behind
 * the camera.  The device scales the projection's y by the aspect. */
bool to_screen(const Mat4 &view, const Mat4 &proj, Vec3 p, float w, float h,
               float *sx, float *sy)
{
    float v[4], c[4];
    for (int col = 0; col < 4; col++)
        v[col] = p.x * view.m[col] + p.y * view.m[4 + col] + p.z * view.m[8 + col]
               + view.m[12 + col];
    for (int col = 0; col < 4; col++)
        c[col] = v[0] * proj.m[col] + v[1] * proj.m[4 + col] + v[2] * proj.m[8 + col]
               + v[3] * proj.m[12 + col];
    if (c[3] <= 0.0f)
        return false;
    *sx = (c[0] / c[3] + 1.0f) * 0.5f * w;
    *sy = (1.0f - c[1] * (w / h) / c[3]) * 0.5f * h;
    return true;
}

class Gallery {
public:
    explicit Gallery(Renderer *r) : r_(r), dev_(r->device) {}
    ~Gallery() { theme_block_.release(&textures_, &models_); }

    void start(int theme)
    {
        theme_ = (theme % kThemeCount + kThemeCount) % kThemeCount;
        char path[0x100];
        snprintf(path, sizeof(path), "themes\\%s.thm", kThemes[theme_]);
        std::vector<ThemeSoundLine> sounds;
        if (!theme_block_.load(dev_, &textures_, &models_, path, sounds))
            g_logger.write("gallery: could not load %s\n", path);
        looks_.clear();
        for (int look = 0; look < THEME_OBJ_COUNT; look++)
            if (has_particles(theme_block_, look))
                looks_.push_back(look);
        g_logger.write("gallery: %s, %u looks with particles\n", kThemes[theme_],
                       (unsigned)looks_.size());
        started_ = now_;
    }

    void frame()
    {
        const unsigned ms = sysdev::tickMs();
        const double elapsed = last_ ? std::min(100.0, (double)(ms - last_)) : 0.0;
        last_ = ms;
        now_ += elapsed;
        keys();
        if (!paused_)
            orbit_ += elapsed * 0.00015;

        const unsigned cols = columns();
        const unsigned rows = looks_.empty() ? 1 : (unsigned)(looks_.size() + cols - 1) / cols;
        const Vec3 centre = { (cols - 1) * kSpacing * 0.5f, 0.5f, (rows - 1) * kSpacing * 0.5f };
        const float dist = (2.5f + std::max(cols, rows) * kSpacing * 0.45f) * zoom_;
        const Vec3 eye = { centre.x + (float)sin(orbit_) * dist, centre.y + dist * 0.6f,
                           centre.z - (float)cos(orbit_) * dist };
        Mat4 view;
        Camera_BuildLookAt(&view, eye.x, eye.y, eye.z, centre.x, centre.y, centre.z,
                           0.0f, 1.0f, 0.0f, 0.0f);
        const Mat4 proj = projection();

        dev_->Clear(ClearFlag::Color | ClearFlag::Depth);
        if (!dev_->BeginFrame())
            return;
        dev_->SetView(view);
        dev_->SetProjection(proj);
        FogState fog = dev_->fog();
        fog.enable = false;
        dev_->SetFog(fog);

        draw::Frame f;
        f.dev = dev_;
        f.theme = &theme_block_;
        f.shadow = &r_->texShadow;
        f.eye = eye;
        f.target = centre;
        f.opts = { true, true, 0, 2 };
        f.now = now_;
        f.elapsed = elapsed;

        std::vector<draw::Instance> at(looks_.size());
        for (size_t i = 0; i < looks_.size(); i++)
            at[i].pos = place(i);
        for (size_t i = 0; i < looks_.size(); i++)
            if (looks_[i] == THEME_OBJ_EXIT)
                at[i].conditions = draw::COND_ACTIVE;  // open: it glows and emits

        draw::beginPass(f, draw::Pass::Opaque);
        for (size_t i = 0; i < looks_.size(); i++) {
            draw::objects(f, THEME_OBJ_FLOOR, &at[i]);
            if (play_of(looks_[i]) != Play::Burst && play_of(looks_[i]) != Play::Trail)
                draw::objects(f, (ThemeObjectType)looks_[i], &at[i]);
        }

        draw::beginPass(f, draw::Pass::Particles);
        const double t = now_ - started_;
        for (size_t i = 0; i < looks_.size(); i++) {
            const ThemeObjectType look = (ThemeObjectType)looks_[i];
            const draw::Instance &in = at[i];
            switch (play_of(look)) {
            case Play::Continuous:
                draw::particles(f, look, &in);
                break;
            case Play::Burst: {
                if (fmod(t, kBurstEvery) < elapsed)
                    draw::spawnBurst(f, look, in.pos);
                const BlendState additive = BlendState::additive();
                draw::bursts(f, look, look == THEME_OBJ_FALLING_DUST ? nullptr : &additive);
                break;
            }
            case Play::Trail: {
                const float a = (float)(t * 0.002);
                const Vec3 p = { in.pos.x + sinf(a), 0.3f, in.pos.z + cosf(a) };
                draw::trail(f, look, true, p, a + 1.5707964f);
                break;
            }
            case Play::Fire:
                draw::fireEffects(f, look, fmod(t, kFireEvery) < kFireFor);
                if (fmod(t, kFireEvery) < elapsed)
                    draw::explode(f, look);
                draw::particles(f, look, &in);
                break;
            }
        }

        labels(view, proj);
        dev_->EndFrame();
        dev_->Present();
    }

private:
    /* The looks stand in a square grid, a floor tile each. */
    unsigned columns() const
    {
        return std::max(1u, (unsigned)ceil(sqrt((double)looks_.size())));
    }

    Vec3 place(size_t i) const
    {
        return { (float)(i % columns()) * kSpacing, 0.0f, (float)(i / columns()) * kSpacing };
    }

    void labels(const Mat4 &view, const Mat4 &proj)
    {
        const float w = (float)dev_->width(), h = (float)dev_->height();
        dev_->SetDepth(DepthState{ false, false });
        const float cw = w * 0.012f, ch = w * 0.016f;
        char title[160];
        snprintf(title, sizeof(title), "%s  -  %u particle looks  -  left/right theme, up/down zoom",
                 kThemes[theme_], (unsigned)looks_.size());
        r_->fontMain.drawCentered(w * 0.5f, h * 0.03f, cw, ch, 0.75f, title, dev_, 0,
                                  0xffffffff, 0xffc0c0c0);
        for (size_t i = 0; i < looks_.size(); i++) {
            float sx, sy;
            const Vec3 p = place(i);
            if (to_screen(view, proj, { p.x, -0.6f, p.z }, w, h, &sx, &sy))
                r_->fontMain.drawCentered(sx, sy, cw, ch, 0.75f, kLookNames[looks_[i]], dev_, 0,
                                          0xffffff80, 0xffc0a040);
        }
        dev_->SetDepth(DepthState());
    }

    /* Each key acts on the frame it goes down. */
    void keys()
    {
        unsigned char k[KEY_COUNT] = {};
        inputdev::keyboard().sample(k);
        auto pressed = [&](int key) { return (k[key] & 0x80) && !(held_[key] & 0x80); };
        if (pressed(KEY_ESCAPE))
            closing_ = true;
        if (pressed(KEY_RIGHT))
            start(theme_ + 1);
        if (pressed(KEY_LEFT))
            start(theme_ - 1);
        if (pressed(KEY_SPACE))
            paused_ = !paused_;
        if (k[KEY_UP] & 0x80)
            zoom_ = std::max(0.3f, zoom_ * 0.98f);
        if (k[KEY_DOWN] & 0x80)
            zoom_ = std::min(3.0f, zoom_ * 1.02f);
        memcpy(held_, k, sizeof(held_));
    }

public:
    /* The debug UI's controls, in the gallery's own section. */
    void panel()
    {
        for (int i = 0; i < kThemeCount; i++) {
            if (i)
                ImGui::SameLine();
            if (ImGui::RadioButton(kThemes[i], theme_ == i) && theme_ != i)
                start(i);
        }
        ImGui::Checkbox("pause orbit", &paused_);
        ImGui::SliderFloat("zoom", &zoom_, 0.3f, 3.0f);
        if (ImGui::Button("back to the game"))
            closing_ = true;
    }

    bool closing() const { return closing_; }

private:
    Renderer     *r_;
    RenderDevice *dev_;
    int           theme_ = 0;
    std::vector<int> looks_;
    unsigned      last_ = 0;
    double        now_ = 0.0, started_ = 0.0;
    double        orbit_ = 0.0;
    float         zoom_ = 1.0f;
    bool          paused_ = false;
    bool          closing_ = false;
    ThemeAssetBlock theme_block_;  // its own: the level's stays loaded
    TextureManager  textures_;
    ModelManager    models_;
    unsigned char held_[KEY_COUNT] = {};
};

std::unique_ptr<Gallery> s_gallery;
bool s_fromEnv = false;

int theme_named(const std::string &name)
{
    for (int i = 0; i < kThemeCount; i++)
        if (strcasecmp(name.c_str(), kThemes[i]) == 0)
            return i;
    return 0;
}

}  // namespace

bool ParticleGallery_StartFromEnv(Renderer *renderer)
{
    const std::string want = sysdev::getEnv("KAROO_PARTICLE_GALLERY");
    if (want.empty() || renderer->device->headless())
        return false;
    s_fromEnv = true;
    s_gallery.reset(new Gallery(renderer));
    s_gallery->start(theme_named(want));
    return true;
}

bool ParticleGallery_Frame(Renderer *renderer)
{
    if (dbg::active()) {
        dbg::Section s("Particle gallery");
        if (s) {
            if (s_gallery)
                s_gallery->panel();
            else if (ImGui::Button("open the particle gallery")) {
                s_gallery.reset(new Gallery(renderer));
                s_gallery->start(0);
            }
        }
    }
    if (s_gallery && s_gallery->closing()) {
        s_gallery.reset();
        if (s_fromEnv)
            windev::quit(0);
        // The game's clock ran on while the gallery showed; don't make it
        // catch up in one tick.
        renderer->lastTickMs = clock_advance() * 1000.0;
    }
    if (!s_gallery)
        return false;
    s_gallery->frame();
    return true;
}

void ParticleGallery_Shutdown()
{
    s_gallery.reset();
}
