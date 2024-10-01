//
// Created by fengshi on 9/27/24.
//

#ifndef APPLICATION_H
#define APPLICATION_H

#include "ControlPanel.h"
#include "RenderThread.h"
#include "Viewport.h"
#include "ColormapPanel.h"

namespace openpgl {
namespace cpp {
struct Field;
}
}

namespace pbrt {

class Application {
public:
    Application(Camera camera, Primitive scene, openpgl::cpp::Field* field, const PGLKDTreeArguments &args, int spp,
        const std::function<void(int waveStart)> &renderWave,
        const std::function<void(int waveEnd)> &updateCache,
        const std::function<void(int waveEnd)> &saveImage);
    ~Application();
    int Run();

private:
    void SetupDockSpace();
    void SetupRenderThread();

    void CheckIsMainThread();

    // Callbacks from render thread
    void CheckIsRenderThread();
    void UpdateCache(int waveEnd);
    void RenderWave(int waveStart);
    void ClearFilm();
    void UpdateCPUBufferFromFilm();

    // Small components
    void ChannelSelector();

    Camera m_camera;
    Film m_film;
    const bool m_isMultiChannel;
    Vector2i m_resolution;  // resolution of the rendering
    Primitive m_scene;
    openpgl::cpp::Field& m_field;
    PGLKDTreeArguments m_subdivCfg;  // config for spatial subdivision
    const int m_spp;
    std::function<void(int waveStart)> m_renderWave;
    std::function<void(int waveEnd)> m_updateCache;
    std::function<void(int waveEnd)> m_saveImage;

    GLFWwindow *m_window = nullptr;
    ImVec2 m_windowSize{1500, 800};
    bool m_hasSetupDock = false;

    // Components and views
    std::unique_ptr<RenderThread> m_renderThread;
    std::unique_ptr<ControlPanel> m_controlPanel;
    std::unique_ptr<Viewport> m_viewport;
    std::unique_ptr<ColormapPanel> m_colormapPanel;

    std::mutex m_mtxField, m_mtxSubdivCfg;

    struct {
        double renderMS = 0;
        double postprocessMS = 0;
    } m_waveTimeStats;
};

}


#endif //APPLICATION_H
