#include "profpanel.h"
#include "imgui.h"
#include "prof.h"
#include <float.h>

namespace prof {

static void draw_prof_node(const Node *nodes, unsigned count, int idx, double frameMs)
{
    const Node &n = nodes[idx];
    bool leaf = true;
    for (unsigned i = 0; i < count; i++)
        if (nodes[i].parent == idx && recent(nodes[i])) {
            leaf = false;
            break;
        }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (leaf)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    const bool idle = n.calls == 0;
    if (idle)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool open = ImGui::TreeNodeEx(n.name, flags);
    ImGui::TableNextColumn();
    ImGui::Text("%.2f", n.avgMs);
    ImGui::TableNextColumn();
    if (!leaf)
        ImGui::Text("%.2f", n.selfAvgMs);
    ImGui::TableNextColumn();
    ImGui::Text("%.2f", n.maxMs);
    ImGui::TableNextColumn();
    if (n.calls > 1)
        ImGui::Text("x%u", n.calls);
    ImGui::TableNextColumn();
    ImGui::ProgressBar(frameMs > 0.0 ? (float)(n.avgMs / frameMs) : 0.0f, ImVec2(-FLT_MIN, 8), "");
    if (idle)
        ImGui::PopStyleColor();
    if (open && !leaf) {
        for (unsigned i = 0; i < count; i++)
            if (nodes[i].parent == idx && recent(nodes[i]))
                draw_prof_node(nodes, count, (int)i, frameMs);
        ImGui::TreePop();
    }
}

void drawPanel()
{
    unsigned count;
    const Node *list = prof::nodes(&count);
    if (count == 0) {
        ImGui::TextDisabled("waiting for a frame");
        return;
    }
    if (ImGui::Button("reset"))
        reset();
    ImGui::SameLine();
    ImGui::TextDisabled("ms; self excludes child scopes");
    // Opaque, so the stripes cannot wash out to white.
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
    if (ImGui::BeginTable("prof", 6, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("scope", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("avg", ImGuiTableColumnFlags_WidthFixed, 44);
        ImGui::TableSetupColumn("self", ImGuiTableColumnFlags_WidthFixed, 44);
        ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 44);
        ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthFixed, 36);
        ImGui::TableSetupColumn("share", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();
        for (unsigned i = 0; i < count; i++)
            if (list[i].parent < 0)
                draw_prof_node(list, count, (int)i, list[i].avgMs);
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(2);
}

}  // namespace prof
