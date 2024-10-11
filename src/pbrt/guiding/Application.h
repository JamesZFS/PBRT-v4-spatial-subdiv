//
// Created by fengshi on 9/27/24.
//

#ifndef APPLICATION_H
#define APPLICATION_H

#include "ControlPanel.h"
#include "RenderThread.h"
#include "View.h"
#include "Viewport.h"
#include "SamplingDistributionView.h"
#include "ColormapPanel.h"
#include "CacheMonitor.h"
#include "CacheHistogram.h"
#include <pbrt/cpu/integrators.h>

namespace openpgl {
namespace cpp {
struct Field;
}
}

namespace pbrt {

class Application : public View {
public:
    Application(Camera camera, Primitive scene, pstd::optional<Image> &&reference,
        openpgl::cpp::Device *device, openpgl::cpp::Field *field, openpgl::cpp::SampleStorage &sampleStorage,
        const PGLKDTreeArguments &args, Sampler samplerPrototype, ThreadLocal<Sampler> &samplers,
        GuidedPathIntegrator::IntegratorSettings &integratorSettings, GuidedPathIntegrator::GuidingSettings &guideSettings,
        const std::function<void(int waveStart)> &renderWave,
        const std::function<void(int waveEnd)> &updateCache,
        const std::function<void(int waveEnd)> &saveImage);
    ~Application() override;
    int Run();
    SelectedChannel GetSelectedChannel() const { return m_selectedChannel; }
    void SetSelectedChannel(SelectedChannel newChannel);
    bool ShortcutEnabled() const { return m_enableShortcuts; }
    void Draw() override;
    int GetCurrentWave() const;
    int GetSPP() const { return m_spp; }

private:
    struct RayCastingData {
        Point2i pixel;  // in: pixel coordinate at the mouse position
        bool valid = false;   // if the hit is valid
        Point3f hit;  // hit point in world space
        Normal3f normal;
        Point2f uv;
        PGLRegionStatistics cache;
    };

    enum LayoutType {
        Layout_Default,
        Layout_CacheMonitor,
        Layout_Compact,
        Layout_Histograms,
        Layout_Count,
    };

    void SetupLayout();
    void SetupLayoutDefault();
    void SetupLayoutCacheMonitor();
    void SetupLayoutCompact();
    void SetupLayoutHistograms();

    void SetupRenderThread();

    void CheckIsMainThread();
    RayCastingData RayCast(Point2i pixel) const;
    void UpdateFramebuffer();
    void SaveRendering(std::string path);
    void SaveField(std::string path);
    void LoadField(std::string path);
    void SaveSamples(std::string path);
    void LoadSamples(std::string path);

    void CacheInfo(const PGLRegionStatistics &cache);

    void UpdateRayCastingAtMouse();
    void SamplingDistributionInteraction();
    void ProbesInteraction();

    // Callbacks from render thread
    void CheckIsRenderThread();
    void UpdateField(int waveEnd);
    void RenderWave(int waveStart);
    void ClearFilm();
    void ResetCache();
    void RestartRendering(bool resetCache);
    void UpdateCPUBufferFromFilm();
    void UpdateCacheCurves();
    void UpdateCacheHistograms();
    void UpdateSamplingDistributionView();

    // GUI components
    void MainMenu();
    void ErrorMetricSelector();
    void RayCastingPanel();

    void ChannelSelector();
    void StatusBar();
    void IntegratorSettings();
    void GuideSettings();
    void SpatialSubdivisionSettings();
    void CacheMonitorViews();
    void CacheHistogramViews();

    Camera m_camera;
    Film m_film;
    pstd::optional<Image> m_reference;
    const bool m_isMultiChannel;
    int m_channelCount;
    Point2i m_resolution;  // resolution of the rendering
    Primitive m_scene;
    openpgl::cpp::Device& m_device;
    openpgl::cpp::Field& m_field;
    openpgl::cpp::SampleStorage& m_sampleStorage;
    PGLKDTreeArguments m_subdivCfg;  // config for spatial subdivision
    int m_spp;
    int m_seed;
    Sampler m_samplerPrototype;
    ThreadLocal<Sampler> &m_samplers;
    GuidedPathIntegrator::IntegratorSettings &m_integratorSettings;  // from the integrator
    GuidedPathIntegrator::GuidingSettings &m_guideSettings;  // from the integrator
    std::function<void(int waveStart)> m_renderWave;
    std::function<void(int waveEnd)> m_updateCache;
    std::function<void(int waveEnd)> m_saveImage;

    GLFWwindow *m_window = nullptr;
    ImVec2 m_windowSize{1500, 800};
    bool m_hasSetupLayout = false;
    LayoutType m_layout = Layout_CacheMonitor;
    RayCastingData m_rcMouse;  // ray casting result at current mouse position
    RayCastingData m_rcSDV;  // ray casting result at the sampling distribution view
    int m_maxMaxDepth = 15;
    SelectedChannel m_selectedChannel = Channel_Radiance;
    bool m_enableShortcuts = true;
    bool m_enableRayCastingAtMouse = false;
    bool m_enableProbes = false;
    bool m_enableHistogram = false;
    bool m_enableSamplingDistributionView = false;
    ErrorMetric m_errorMetric = Metric_MRAE;

    // Components and views
    std::unique_ptr<RenderThread> m_renderThread;
    std::unique_ptr<ControlPanel> m_controlPanel;
    std::unique_ptr<Viewport> m_viewport;
    std::unique_ptr<SamplingDistributionView> m_samplingDistributionView;
    std::unique_ptr<ColormapPanel> m_colormapPanel;
    struct {
        std::unique_ptr<CacheMonitor> object;
        CacheMonitor::Plot *ce, *fluence, *depth, *samples;
    } m_cacheMonitor;
    struct {
        std::unique_ptr<CacheHistogram> object;
        CacheHistogram::Hist *fluence, *ce, *depth, *samples;
    } m_cacheHistogram;

    mutable struct {
        std::mutex field, subdivCfg;
    } m_mtx;

    struct {
        double renderMS = 0;
        double postprocessMS = 0;
        size_t trainingSamples = 0;
        size_t numRegions = 0;
    } m_waveStats;
};

}


#endif //APPLICATION_H
