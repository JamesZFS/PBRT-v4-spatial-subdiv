//
// Created by fengshi on 9/11/24.
//

#ifndef GUIDINGVIEWER_H
#define GUIDINGVIEWER_H

#include <pbrt/pbrt.h>
#include <pbrt/scene.h>
#include <pbrt/cameras.h>

namespace pbrt {

void RenderGuidingViewer(BasicScene &scene);

class GuidingViewerGUI {
public:
    GuidingViewerGUI(
        Camera camera, Primitive aggregate, int spp,
        std::function<void(int waveStart)> renderWave,
        std::function<void(int waveEnd)> postprocessWave);

    ~GuidingViewerGUI();

    void Launch();  // The GUI runs in the main thread, while the rendering runs in a separate thread.

    enum RendererState {
        Initial = 0,
        Rendering,
        WaveEnd,
        Completed
    };

    enum ControlCommand {
        Resume = 0,
        Pause,
        Forward,
        Terminate,
        Restart,
        ControlCommandCount
    };

private:
    void RenderThread();

    void UpdateFramebufferFromFilm();

    void Inspector();

    void StatusBar();

    void DrawRendering();

    Camera camera;
    Film film;
    Vector2i resolution;
    Primitive aggregate;
    const int spp;
    int waveStart;
    std::function<void(int waveStart)> renderWave;
    std::function<void(int waveEnd)> postprocessWave;
    RGB *cpuFramebuffer = nullptr;  // CPU framebuffer for display, written by the render thread, read by the GUI thread.

    constexpr static int inspectorWidth = 300, statusBarHeight = 30;
    int windowWidth, windowHeight;

    void* controlButtonTextureID = nullptr;
    int controlButtonTextureWidth = 0, controlButtonTextureHeight = 0;

    std::mutex mtx;
    std::condition_variable cv;
    ControlCommand command = Pause;
    RendererState renderState = Initial;
};

}

#endif //GUIDINGVIEWER_H
