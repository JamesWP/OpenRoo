#include "debugui.h"
#include "imgui.h"
#include "image.h"
#include "cheatcode.h"
#include "game.h"
#include "gamestate.h"
#include "renderdevice.h"
#include "windev.h"
#include "worldstate.h"
#include <algorithm>
#include <math.h>
#include <string.h>
#include <vector>

namespace debugui {

// ── The renderer: ImDrawData through RenderDevice ──

/* Brings one of ImGui's textures up to date: the font atlas, which it grows
 * and rewrites as glyphs are needed. */
static void update_texture(RenderDevice &dev, ImTextureData *tex)
{
    DeviceTexture *t = (DeviceTexture *)(uintptr_t)tex->TexID;
    switch (tex->Status) {
    case ImTextureStatus_WantCreate:
    case ImTextureStatus_WantUpdates: {
        Image img;
        img.width      = tex->Width;
        img.height     = tex->Height;
        img.sourceBits = 32;
        img.hasAlpha   = true;
        strcpy(img.name, "imgui");
        img.rgba.resize((size_t)tex->Width * tex->Height * 4);
        const uint8_t *src = (const uint8_t *)tex->GetPixels();
        if (tex->Format == ImTextureFormat_RGBA32) {
            memcpy(img.rgba.data(), src, img.rgba.size());
        } else {  // Alpha8: white, with the glyph coverage as alpha
            for (size_t i = 0; i < img.rgba.size() / 4; i++) {
                uint8_t *p = &img.rgba[i * 4];
                p[0] = p[1] = p[2] = 255;
                p[3] = src[i];
            }
        }
        if (tex->Status == ImTextureStatus_WantCreate) {
            t = dev.CreateTexture(img, TextureFlag::Alpha, 32);
            tex->SetTexID((ImTextureID)(uintptr_t)t);
        } else if (t) {
            dev.UpdateTexture(t, img);
        }
        tex->SetStatus(ImTextureStatus_OK);
        break;
    }
    case ImTextureStatus_WantDestroy:
        if (tex->UnusedFrames > 0) {
            RenderDevice::DestroyTexture(t);
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
        break;
    default:
        break;
    }
}

/* ImGui's colours are 0xAABBGGRR; the device's are 0xAARRGGBB. */
static uint32_t device_color(ImU32 c)
{
    return (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16);
}

static void render_draw_data(RenderDevice &dev, ImDrawData *dd)
{
    if (dd->Textures)
        for (ImTextureData *tex : *dd->Textures)
            if (tex->Status != ImTextureStatus_OK)
                update_texture(dev, tex);
    if (dd->DisplaySize.x <= 0.0f || dd->DisplaySize.y <= 0.0f)
        return;

    // ImGui lays out in the window's units; the overlay draws in the window's
    // native pixels.
    const float sx = (float)dev.overlayWidth() / dd->DisplaySize.x;
    const float sy = (float)dev.overlayHeight() / dd->DisplaySize.y;

    dev.SetBlend(BlendState::alpha());
    dev.SetDepth({ false, false });
    dev.SetStencil(StencilState());
    dev.SetRaster({ CullMode::None });
    SamplerState sampler;
    sampler.mag = sampler.min = Filter::Linear;
    sampler.u = sampler.v = AddressMode::Clamp;
    dev.SetSampler(0, sampler);

    std::vector<ScreenVertex> verts;
    std::vector<uint16_t> indices;
    for (const ImDrawList *list : dd->CmdLists) {
        verts.resize(list->VtxBuffer.Size);
        for (int i = 0; i < list->VtxBuffer.Size; i++) {
            const ImDrawVert &v = list->VtxBuffer[i];
            // Screen vertices put whole pixels at half-pixel coordinates.
            verts[i] = { (v.pos.x - dd->DisplayPos.x) * sx - 0.5f,
                         (v.pos.y - dd->DisplayPos.y) * sy - 0.5f,
                         0.0f, 1.0f, device_color(v.col), 0, v.uv.x, v.uv.y };
        }
        for (const ImDrawCmd &cmd : list->CmdBuffer) {
            if (cmd.UserCallback || cmd.ElemCount == 0)
                continue;
            const float x0 = std::max((cmd.ClipRect.x - dd->DisplayPos.x) * sx, 0.0f);
            const float y0 = std::max((cmd.ClipRect.y - dd->DisplayPos.y) * sy, 0.0f);
            const float x1 = std::min((cmd.ClipRect.z - dd->DisplayPos.x) * sx, (float)dev.overlayWidth());
            const float y1 = std::min((cmd.ClipRect.w - dd->DisplayPos.y) * sy, (float)dev.overlayHeight());
            if (x1 <= x0 || y1 <= y0)
                continue;
            dev.SetScissor({ true, (int)floorf(x0), (int)floorf(y0),
                             (int)ceilf(x1) - (int)floorf(x0), (int)ceilf(y1) - (int)floorf(y0) });
            dev.SetTexture(0, (const DeviceTexture *)(uintptr_t)cmd.GetTexID());

            // Only the vertices this command names go to the device, with
            // its indices counted from the first of them.
            const ImDrawIdx *idx = list->IdxBuffer.Data + cmd.IdxOffset;
            ImDrawIdx lo = idx[0], hi = idx[0];
            for (unsigned i = 1; i < cmd.ElemCount; i++) {
                lo = std::min(lo, idx[i]);
                hi = std::max(hi, idx[i]);
            }
            indices.resize(cmd.ElemCount);
            for (unsigned i = 0; i < cmd.ElemCount; i++)
                indices[i] = (uint16_t)(idx[i] - lo);
            dev.DrawIndexed(Prim::TriangleList, VertexFormat::Screen, &verts[lo],
                            (uint32_t)hi - lo + 1, indices.data(), cmd.ElemCount);
        }
    }
    dev.SetScissor(ScissorState());
    dev.SetTexture(0, nullptr);
}

// ── The panels ──

static bool g_holdFreeze;

/* The level to load, from the same table the level select reads. */
static void draw_level_picker(Game *g)
{
    if (ImGui::Button("Load level..."))
        ImGui::OpenPopup("levels");
    if (ImGui::BeginPopup("levels")) {
        ImGui::BeginChild("list", ImVec2(260, 300));
        for (unsigned i = 0; i < g->levelCount(); i++) {
            ImGui::PushID((int)i);
            char label[300];
            snprintf(label, sizeof(label), "%3u  %s", i + 1, g->levelNameTableEntry((unsigned char)i));
            if (ImGui::Selectable(label, i == g->levelIndex())) {
                Cheat_LoadLevel(g, (unsigned char)i);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
}

static void draw_overview(RenderDevice &dev)
{
    Game *g = Game::instance();
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::Text("%.1f fps  %.2f ms", io.Framerate, 1000.0f / io.Framerate);
    ImGui::TextDisabled("display %ux%u, %u-bit", dev.width(), dev.height(), dev.bitDepth());
    if (!g)
        return;
    ImGui::Separator();
    ImGui::Text("level %u/%u: %s", g->levelIndex() + 1, g->levelCount(), g->levelName());
    draw_level_picker(g);

    if (gamestate_mode() == 0)
        return;
    static Observation obs;  // large; a read-only view of the live Game
    if (obs.observe()) {
        ImGui::Text("gems %d / %d   lives %u   foes killed %u", obs.gems_collected,
                    obs.gems_required, obs.lives, obs.foes_killed);
        ImGui::Text("player U%u V%u H%u   exit U%u V%u H%u", obs.player_cell[0],
                    obs.player_cell[1], obs.player_cell[2], obs.exit_cell[0],
                    obs.exit_cell[1], obs.exit_cell[2]);
        ImGui::Text("%u foes, %u bombs%s", obs.n_foes, obs.n_enemies,
                    obs.level_complete ? "   [complete]" : "");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Cheats");
    if (ImGui::Button("+life"))      Cheat_AddLife(g, 1);
    ImGui::SameLine();
    if (ImGui::Button("+10 bombs"))  Cheat_AddBombs(g);
    ImGui::SameLine();
    if (ImGui::Button("+glide"))     Cheat_AddGlide(g);
    if (ImGui::Button("invulnerable")) Cheat_Invulnerable(g);
    ImGui::SameLine();
    if (ImGui::Button("kill foes"))  Cheat_KillFoes(g);
    ImGui::Checkbox("hold foes frozen", &g_holdFreeze);
    if (g_holdFreeze)
        Cheat_FreezeFoes(g);
}

static void draw_panels(RenderDevice &dev)
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Open'Roo")) {
        if (ImGui::CollapsingHeader("Overview", ImGuiTreeNodeFlags_DefaultOpen))
            draw_overview(dev);
        ImGui::TextDisabled("F10 hides this");
    }
    ImGui::End();
}

static bool overlay(RenderDevice &dev)
{
    if (!windev::debugUiShown())
        return false;
    windev::debugUiNewFrame();
    ImGui::NewFrame();
    draw_panels(dev);
    ImGui::Render();
    render_draw_data(dev, ImGui::GetDrawData());
    return true;
}

// ── Life cycle ──

bool init(RenderDevice &dev)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    io.BackendRendererName = "RenderDevice";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    if (!windev::debugUiInit()) {
        ImGui::DestroyContext();
        return false;
    }
    dev.SetOverlay(overlay);
    return true;
}

void shutdown(RenderDevice &dev)
{
    if (!ImGui::GetCurrentContext())
        return;
    dev.SetOverlay(NULL);
    windev::debugUiShutdown();
    for (ImTextureData *tex : ImGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1) {
            RenderDevice::DestroyTexture((DeviceTexture *)(uintptr_t)tex->TexID);
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    ImGui::DestroyContext();
}

}  // namespace debugui
