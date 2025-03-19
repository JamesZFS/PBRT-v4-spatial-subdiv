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
#include "RadianceView.h"
#include "SignatureView.h"
#include "ColormapPanel.h"
#include "CacheMonitor.h"
#include "CacheHistogram.h"
#include "Plots.h"
#include <pbrt/cpu/integrators.h>

namespace openpgl {
namespace cpp {
struct Field;
}
}

namespace pbrt {

class Application : public View {
public:
    Application(Camera camera, Primitive scene, const std::vector<Light> &lights, pstd::optional<Image> &&reference,
        openpgl::cpp::Device *device, openpgl::cpp::Field *field, openpgl::cpp::SampleStorage &sampleStorage,
        const PGLKDTreeArguments &args, Sampler samplerPrototype, ThreadLocal<Sampler> &samplers,
        GuidedPathIntegrator::IntegratorSettings &integratorSettings, GuidedPathIntegrator::GuidingSettings &guideSettings,
        const std::function<void(int waveStart)> &renderWave,
        const std::function<void(int waveEnd)> &updateCache,
        const std::function<void(int waveEnd)> &saveImage,
        const std::function<float()> &getAvgPathLength);
    ~Application() override;
    int Run();
    SelectedChannel GetSelectedChannel() const { return m_selectedChannel; }
    void SetSelectedChannel(SelectedChannel newChannel);
    bool ShortcutEnabled() const { return m_enableShortcuts; }
    void SetShortcutEnabled(bool val) { m_enableShortcuts = val; }
    void Draw() override;
    int GetCurrentWave() const;
    int GetSPP() const { return m_spp; }
    bool IsShowingFine() const { return m_showFine; }
    bool IsShowingDiff() const { return m_showDiff; }
    double GetCrossEntropySDRE() const { return m_samplingDistributionView->GetCrossEntropy(); }
    float GetEnergyThreshold() const { return m_subdivCfg.signatureDistanceThreshold; }
    float GetRiskTolerance() const { return m_subdivCfg.riskTolerance; }
    const GuidedPathIntegrator::IntegratorSettings &GetIntegratorSettings() const { return m_integratorSettings; }
    const GuidedPathIntegrator::GuidingSettings &GetGuideSettings() const { return m_guideSettings; }
    const PGLKDTreeArguments &GetSubdivCfg() const { return m_subdivCfg; }
    float GetSignatureStdMultiplier() const { return m_subdivCfg.stdMultiplier; }

    bool sdrLocalFrame = false;
    float sdrExposure = 1.0f;

private:
    struct RayCastingData {
        Point2i pixel;  // in: pixel coordinate at the mouse position
        bool valid = false;   // if the hit is valid
        Point3f hit;  // hit point in world space
        Normal3f normal;
        Point2f uv;
        PGLRegionStatistics coarse;
        PGLRegionStatistics fine;
    };

    enum LayoutType {
        Layout_Default,
        Layout_Compact,
        Layout_ProbeViews,
        Layout_CacheMonitor,
        Layout_Histograms,
        Layout_Count,
    };

    void SetupLayout();
    void SetupLayoutDefault();
    void SetupLayoutCompact();
    void SetupLayoutProbeViews();
    void SetupLayoutCacheMonitor();
    void SetupLayoutHistograms();
    void SetFullScreen();

    void SetupRenderThread();

    void CheckIsMainThread();
    RayCastingData RayCast(Point2i pixel) const;
    void UpdateFramebuffer();
    void SaveRendering(std::string path);
    void SaveField(std::string path);
    void LoadField(std::string path);
    void SaveSamples(std::string path);
    void LoadSamples(std::string path);
    void SaveSamplesNpy(std::string dir);

    void CacheInfo(const PGLRegionStatistics &coarse, const PGLRegionStatistics &fine);
    void AppendToRayCastingHistory(const RayCastingData &rc);

    void UpdateRayCastingAtMouse();
    void SDREViewInteraction();
    void CacheProbesInteraction();

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
    void UpdatePlots();
    void UpdateSamplingDistributionView();
    void NewRadianceViewRendering();
    void RadianceViewRenderStep();
    void UpdateSignatureView();

    // GUI components
    void MainMenu();
    void ErrorMetricSelector();
    void RayCastingPanel();
    void RayCastingHistory();

    void ChannelSelector();
    void ViewportOptions();
    void StatusBar();
    void IntegratorSettings();
    void GuideSettings();
    void SpatialSubdivisionSettings();
    void CacheMonitorViews();
    void CacheHistogramViews();
    void PlotsView();

    Camera m_camera;
    Film m_film;
    pstd::optional<Image> m_reference;
    const bool m_isMultiChannel;
    int m_channelCount;
    Point2i m_resolution;  // resolution of the rendering
    Primitive m_scene;
    const std::vector<Light> &m_lights;
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
    std::function<float()> m_getAvgPathLength;

    GLFWwindow *m_window = nullptr;
    ImVec2 m_windowSize{1500, 800};
    bool m_hasSetupLayout = false;
    bool m_hasOpenedChangeResolutionPopup = false;
    LayoutType m_layout = Layout_Default;
    RayCastingData m_rcMouse;  // ray casting result at current mouse position
    RayCastingData m_rcSDRE;  // ray casting result at the sampling distribution / radiance view
    std::vector<RayCastingData> m_rcSDREHistory;
    std::string m_rcHistory;
    int m_maxMaxDepth = 15;
    SelectedChannel m_selectedChannel = Channel_Radiance;
    bool m_showFine = false;  // show the fine cache ID and CE
    bool m_showDiff = false;  // show the difference between fine and coarse
    bool m_enableShortcuts = true;
    bool m_enableRayCastingAtMouse = false;

    bool m_enableMonitor = false;
    bool m_enableHistogram = false;
    bool m_enableSamplingDistributionView = false;
    bool m_enableRadianceView = false;
    bool m_enableSignatureView = false;
    bool m_enableRayCastingHistory = false;
    bool m_enablePlots = false;
    bool m_enableImGuiDemo = false;
    bool m_enableImPlotDemo = false;

    bool m_recordSamples = false;
    bool m_trackSRDEHistory = false;
    ErrorMetric m_errorMetric = Metric_MRAE;

    // Components and views
    std::unique_ptr<RenderThread> m_renderThread;
    std::unique_ptr<ControlPanel> m_controlPanel;
    std::unique_ptr<Viewport> m_viewport;
    std::unique_ptr<RadianceView> m_radianceView;
    std::unique_ptr<SamplingDistributionView> m_samplingDistributionView;
    std::unique_ptr<SignatureView> m_signatureView;
    std::unique_ptr<ColormapPanel> m_colormapPanel;
    struct {
        std::unique_ptr<CacheMonitor> object;
        CacheMonitor::Plot *risk, *energy, *fluence, *depth, *samples;
    } m_cacheMonitor;
    struct {
        std::unique_ptr<CacheHistogram> object;
        CacheHistogram::Hist *fluence, *energy, *depth, *samples;
    } m_cacheHistogram;
    struct {
        std::unique_ptr<PlotManager> object;
        PlotManager::Plot *regions, *error, *renderingTime, *trainingTime, *samples, *avgPathLength;
    } m_plots;

    mutable struct {
        std::mutex field, subdivCfg;
    } m_mtx;

    struct {
        double renderMS = 0;
        double postprocessMS = 0;
        size_t trainingSamples = 0;
        size_t numRegions = 0;
        float avgPathLength = 0;
    } m_waveStats;

    std::string m_recordSamplesDir = "./samples";
    std::function<void(const std::string&, const std::string&)> m_fileDialogCallback;
};

}


#endif //APPLICATION_H
