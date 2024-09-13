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

private:
    void RenderThread();

    void UpdateFramebufferFromFilm();

    Camera camera;
    Film film;
    Vector2i resolution;
    Primitive aggregate;
    const int spp;
    int waveStart;
    std::function<void(int waveStart)> renderWave;
    std::function<void(int waveEnd)> postprocessWave;
    RGB *cpuFramebuffer = nullptr;

    enum RendererState {
        Initial = 0,
        Rendering,
        WaveEnd,
        Completed
    };

    enum GUICommand {
        None = 0,
        NextWave,
        Terminate
    };

    std::mutex mtx;
    std::condition_variable cv;
    GUICommand command = None;
    RendererState renderState = Initial;
};

}

#endif //GUIDINGVIEWER_H
