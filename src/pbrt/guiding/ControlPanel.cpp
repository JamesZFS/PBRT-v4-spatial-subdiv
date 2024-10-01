//
// Created by fengshi on 9/30/24.
//

#include "ControlPanel.h"

using namespace pbrt;

ControlPanel::ControlPanel(RenderThread &renderThread) : m_renderThread(renderThread) {
    int width, height;
    if (!LoadTextureFromFile(PBRT_ROOT_DIR "images/control_buttons.png", reinterpret_cast<GLuint&>(m_btnTex), width, height, true))
        ErrorExit("Failed to load control_texture.png from disk");
}

void ControlPanel::Draw() {
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 size = ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight());
    ImVec4 bg_col = ImVec4(0.15f, 0.25f, 0.30f, 1.00f);
    ImVec4 accent_col = ImVec4(0.15f, 0.60f, 0.15f, 1.00f);
    ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);           // No tint
    auto cmd2uv0 = [](Command i) {
        assert(i >= Command::AutoPlay && i <= Command::Restart);
        return ImVec2((float) i / 5, 0.0f);
    };
    auto cmd2uv1 = [](Command i) {
        assert(i >= Command::AutoPlay && i <= Command::Restart);
        return ImVec2((float) (i + 1) / 5, 1.0f);
    };

    bool wasAutoPlayed = m_renderThread.IsAutoPlayed();
    for (int i = 0; i < 5; ++i) {
        ImGui::PushID(i);
        auto cmd = (Command) i;

        if (cmd == Command::Forward) {
            ImGui::SetNextItemWidth(ImGui::GetTextLineHeight() * 6);
            ImGui::InputInt("", &m_renderThread.m_forwardWaves, 1, 10);
            ImGui::SameLine();
        }

        auto nameTip = commandNames[cmd];
        ImVec4 color = (wasAutoPlayed && cmd == Command::AutoPlay) || (!wasAutoPlayed && cmd == Command::Pause) ? accent_col : bg_col;
        bool activate = ImGui::ImageButton(nameTip.first, m_btnTex, size, cmd2uv0(cmd), cmd2uv1(cmd), color, tint_col);
        activate |= ((wasAutoPlayed && cmd == Command::Pause) || (!wasAutoPlayed && cmd == Command::AutoPlay)) && ImGui::IsKeyPressed(ImGuiKey_Space, false);  // Space key for AutoPlay / Pause
        activate |= cmd == Command::Forward && ImGui::IsKeyPressed(ImGuiKey_Enter, false);  // Enter key for Forward
        activate |= cmd == Command::Restart && ImGui::IsKeyPressed(ImGuiKey_F5, false);  // F5 key for Restart
        activate |= cmd == Command::Save && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false);  // Ctrl + S for Save
        if (activate) {
            m_renderThread.SendCommand(cmd);
        }
        ImGui::SetItemTooltip("%s", nameTip.second);
        ImGui::SameLine();

        if (cmd == Command::Forward) {
            ImGui::NewLine();
        }
        ImGui::PopID();
    }
    ImGui::NewLine();
}
