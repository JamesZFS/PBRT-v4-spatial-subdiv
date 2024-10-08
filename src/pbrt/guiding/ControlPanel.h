//
// Created by fengshi on 9/30/24.
//

#ifndef CONTROLPANEL_H
#define CONTROLPANEL_H

#include "View.h"
#include "RenderThread.h"

class ControlPanel : public View {
public:
    using Command = RenderThread::Command;

    ControlPanel(pbrt::Application* parent, RenderThread &renderThread);

    void Draw() override;

private:
    RenderThread &m_renderThread;
    ImTextureID m_btnTex = nullptr;
};

#endif //CONTROLPANEL_H
