#include "debugui.h"
#include "imgui.h"
#include "image.h"
#include "renderdevice.h"
#include "windev.h"
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

    // ImGui lays out in the window's pixels; the device draws Screen vertices
    // in the display mode's.
    const float sx = (float)dev.width() / dd->DisplaySize.x;
    const float sy = (float)dev.height() / dd->DisplaySize.y;

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
            const float x1 = std::min((cmd.ClipRect.z - dd->DisplayPos.x) * sx, (float)dev.width());
            const float y1 = std::min((cmd.ClipRect.w - dd->DisplayPos.y) * sy, (float)dev.height());
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

static bool g_showDemo;

static void draw_panels(RenderDevice &dev)
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Open'Roo")) {
        const ImGuiIO &io = ImGui::GetIO();
        ImGui::Text("%.1f fps (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
        ImGui::Text("display mode %ux%u, %u-bit", dev.width(), dev.height(), dev.bitDepth());
        ImGui::Text("window %.0fx%.0f", io.DisplaySize.x, io.DisplaySize.y);
        ImGui::Checkbox("Dear ImGui demo", &g_showDemo);
        ImGui::TextDisabled("F10 hides this");
    }
    ImGui::End();
    if (g_showDemo)
        ImGui::ShowDemoWindow(&g_showDemo);
}

static void overlay(RenderDevice &dev)
{
    if (!windev::debugUiShown())
        return;
    windev::debugUiNewFrame();
    ImGui::NewFrame();
    draw_panels(dev);
    ImGui::Render();
    render_draw_data(dev, ImGui::GetDrawData());
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
