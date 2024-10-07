//
// Created by fengshi on 9/27/24.
//

#ifndef APPLICATION_H
#define APPLICATION_H

#include "ControlPanel.h"
#include "RenderThread.h"
#include "Viewport.h"
#include "ColormapPanel.h"
#include "CacheMonitor.h"
#include <pbrt/cpu/integrators.h>

namespace openpgl {
namespace cpp {
struct Field;
}
}

namespace pbrt {

class Application {
public:
    Application(Camera camera, Primitive scene, openpgl::cpp::Field* field, openpgl::cpp::SampleStorage &sampleStorage,
        const PGLKDTreeArguments &args, int spp,
        GuidedPathIntegrator::IntegratorSettings &integratorSettings, GuidedPathIntegrator::GuidingSettings &guideSettings,
        const std::function<void(int waveStart)> &renderWave,
        const std::function<void(int waveEnd)> &updateCache,
        const std::function<void(int waveEnd)> &saveImage);
    ~Application();
    int Run();

private:
    struct RayCastingData {
        Point2i pixel;  // in: pixel coordinate at the mouse position
        bool valid = false;   // if the hit is valid
        Point3f hit;  // hit point in world space
        Normal3f normal;
        Point2f uv;
        PGLRegionStatistics cache;
    };

    void SetupLayoutDefault();
    void SetupLayoutCacheMonitor();
    void SetupRenderThread();

    int GetCurrentWave() const;
    void CheckIsMainThread();
    RayCastingData RayCast(Point2i pixel) const;
    void UpdateFramebuffer();

    void CacheInfo(const PGLRegionStatistics &cache);

    void UpdateRayCastingAtMouse();
    void ProbesInteraction();

    // Callbacks from render thread
    void CheckIsRenderThread();
    void UpdateField(int waveEnd);
    void RenderWave(int waveStart);
    void ClearFilm();
    void UpdateCPUBufferFromFilm();
    void AppendToProbeData();

    // GUI components
    void RayCastingPanel();
    void ChannelSelector();
    void StatusBar();
    void IntegratorPanel();
    void GuidePanel();
    void SpatialSubdivisionPanel();

    Camera m_camera;
    Film m_film;
    const bool m_isMultiChannel;
    Vector2i m_resolution;  // resolution of the rendering
    Primitive m_scene;
    openpgl::cpp::Field& m_field;
    openpgl::cpp::SampleStorage& m_sampleStorage;
    PGLKDTreeArguments m_subdivCfg;  // config for spatial subdivision
    const int m_spp;
    GuidedPathIntegrator::IntegratorSettings &m_integratorSettings;  // from the integrator
    GuidedPathIntegrator::GuidingSettings &m_guideSettings;  // from the integrator
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
    std::unique_ptr<CacheMonitor> m_cacheMonitor;

    mutable struct {
        std::mutex field, subdivCfg;
    } m_mtx;

    struct {
        double renderMS = 0;
        double postprocessMS = 0;
        size_t trainingSamples = 0;
    } m_waveStats;

    // Ray Casting
    bool m_enableRayCastingAtMouse = false;
    RayCastingData m_rcMouse;  // ray casting result at current mouse position

    int m_maxMaxDepth = 15;
};

}


#endif //APPLICATION_H
