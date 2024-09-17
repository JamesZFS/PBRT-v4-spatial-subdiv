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
        const std::function<void(int waveStart)> &renderWave,
        const std::function<void(int waveEnd)> &postprocessWave,
        const std::function<void(int waveEnd)> &saveImage);

    ~GuidingViewerGUI();

    void Launch();  // The GUI runs in the main thread, while the rendering runs in a separate thread.

    int RenderedWaves() const { return waveStart; }

    void ClearFilm();

    enum RendererState {
        Initial = 0,
        Rendering,
        WaveEnd,
        Completed
    };

    enum GUICommand {
        // Control buttons begin
        AutoPlay = 0,
        Pause,
        Forward,
        Save,
        Restart,
        // Control buttons end
        Terminate,
        None,
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
    std::function<void(int waveEnd)> saveImage;
    RGB *cpuFramebuffer = nullptr;  // CPU framebuffer for display, written by the render thread, read by the GUI thread.

    constexpr static int inspectorWidth = 300, statusBarHeight = 30;
    int windowWidth, windowHeight;

    void* controlButtonTextureID = nullptr;
    int controlButtonTextureWidth = 0, controlButtonTextureHeight = 0;

    std::mutex mtx;
    std::condition_variable cv;
    GUICommand command = None;
    RendererState renderState = Initial;
    bool autoPlayed = false;
    int forwardWaves = 1;
};

}

#endif //GUIDINGVIEWER_H
