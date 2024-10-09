//
// Created by fengshi on 10/9/24.
//

#include "View.h"
#include "Application.h"


bool View::IsKeyPressed(ImGuiKey key, bool repeat) {
    if (m_parent->ShortcutEnabled()) {
        return ImGui::IsKeyPressed(key, repeat);
    } else {
        return false;
    }
}
