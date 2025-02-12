//
// Created by fengshi on 9/27/24.
//

#include "Application.h"

#include <pbrt/cameras.h>
#include <pbrt/samplers.h>
#include <pbrt/film.h>
#include <pbrt/interaction.h>
#include <pbrt/shapes.h>
#include <pbrt/scene.h>
#include <pbrt/util/progressreporter.h>

#include <iostream>
#include <imgui_internal.h>
#include <implot.h>
#include <implot_internal.h>
#include <ImGuiFileDialog.h>

static std::vector<const char *> channelNames = {
    "Radiance (1)",
    "Cache ID (2)",
    "Energy (3)",
    "Fluence (4)",
    "CE (5)",
    "Samples (6)",
    // "Zero Samples (6)",
    "Depth (7)",
    "Reference (8)",
    "Error (9)",
};

static std::vector<const char *> errorMetricNames = {
    "MSE",
    "MAE",
    "MRSE",
    "MRAE",
};

namespace pbrt {

Application::Application(Camera camera, Primitive scene, const std::vector<Light> &lights, pstd::optional<Image> &&reference,
    openpgl::cpp::Device *device, openpgl::cpp::Field *field, openpgl::cpp::SampleStorage &sampleStorage, const PGLKDTreeArguments &args,
    Sampler samplerPrototype, ThreadLocal<Sampler> &samplers, GuidedPathIntegrator::IntegratorSettings &integratorSettings, GuidedPathIntegrator::GuidingSettings &guideSettings,
    const std::function<void(int waveStart)> &renderWave,
    const std::function<void(int waveEnd)> &updateCache,
    const std::function<void(int waveEnd)> &saveImage)
    : View(this),
      m_camera(camera), m_film(camera.GetFilm()), m_reference(std::move(reference)), m_isMultiChannel(m_film.Is<GuidedGBufferFilm>()),
      m_scene(scene), m_lights(lights), m_device(*device), m_field(*field), m_sampleStorage(sampleStorage), m_subdivCfg(args), m_samplerPrototype(samplerPrototype), m_samplers(samplers),
      m_integratorSettings(integratorSettings), m_guideSettings(guideSettings),
      m_renderWave(renderWave), m_updateCache(updateCache), m_saveImage(saveImage),
      m_resolution(m_film.PixelBounds().Diagonal()) {
    m_spp = samplerPrototype.SamplesPerPixel();
    m_seed = Options->seed;
    m_channelCount = m_isMultiChannel ? (m_reference ? Channel_Count : Channel_Count - 2) : 1;
    m_maxMaxDepth = std::max(m_maxMaxDepth, integratorSettings.maxDepth);
}

Application::~Application() {
}

int Application::Run() {
    std::string title = StringPrintf("Guiding Viewer (%s)", SceneName.c_str());
    m_window = InitializeImGui(title.c_str(), m_windowSize.x, m_windowSize.y);
    if (m_window == nullptr) {
        std::cerr << "Failed to initialize ImGui" << std::endl;
        return 1;
    }
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    SetupRenderThread();
    InitializeTonemaps();
    m_controlPanel = std::make_unique<ControlPanel>(this, *m_renderThread);
    m_viewport = std::make_unique<Viewport>(this, m_film, m_reference);
    m_radianceView = std::make_unique<RadianceView>(this, m_scene, m_lights);
    m_samplingDistributionView = std::make_unique<SamplingDistributionView>(this, m_field, *m_radianceView);
    m_signatureView = std::make_unique<SignatureView>(this, m_field, *m_radianceView);
    m_colormapPanel = std::make_unique<ColormapPanel>(this, m_film, m_reference);

    m_cacheMonitor.object = std::make_unique<CacheMonitor>(this);
    m_cacheMonitor.ce = &m_cacheMonitor.object->AddPlot("CE vs. Iter", CacheMonitor::PlotType_CE, true);
    m_cacheMonitor.energy = &m_cacheMonitor.object->AddPlot("Energy vs. Iter", CacheMonitor::PlotType_Energy, true);
    m_cacheMonitor.fluence = &m_cacheMonitor.object->AddPlot("Fluence vs. Iter", CacheMonitor::PlotType_Fluence, false);
    m_cacheMonitor.depth = &m_cacheMonitor.object->AddPlot("Depth vs. Iter", CacheMonitor::PlotType_Depth, false);
    m_cacheMonitor.samples = &m_cacheMonitor.object->AddPlot("Samples vs. Iter", CacheMonitor::PlotType_Samples, false);

    m_cacheHistogram.object = std::make_unique<CacheHistogram>(this);
    m_cacheHistogram.fluence = &m_cacheHistogram.object->AddPlot("Fluence Histogram", CacheHistogram::PlotType_Fluence, true);
    // m_cacheHistogram.ce = &m_cacheHistogram.object->AddPlot("Energy Histogram", CacheHistogram::PlotType_CE, false);
    m_cacheHistogram.energy = &m_cacheHistogram.object->AddPlot("Energy Histogram", CacheHistogram::PlotType_Energy, false);
    m_cacheHistogram.depth = &m_cacheHistogram.object->AddPlot("Depth Histogram", CacheHistogram::PlotType_Depth, false);
    m_cacheHistogram.samples = &m_cacheHistogram.object->AddPlot("Samples Histogram", CacheHistogram::PlotType_Samples, false);

    m_plots.object = std::make_unique<PlotManager>(this);
    m_plots.regions = &m_plots.object->AddPlot("regions");
    m_plots.error = &m_plots.object->AddPlot("error");
    m_plots.renderingTime = &m_plots.object->AddPlot("rendering time");
    m_plots.trainingTime = &m_plots.object->AddPlot("training time");

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    // ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

    // Main GUI loop
    while (!glfwWindowShouldClose(m_window)) {
        if (InitializeFrame(m_window)) continue;

        SetupLayout();
        {
            int display_w, display_h;
            glfwGetWindowSize(m_window, &display_w, &display_h);
            m_windowSize = ImVec2(display_w, display_h);
        }
        glViewport(0, 0, m_windowSize.x, m_windowSize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        UpdateFramebuffer();
        RadianceViewRenderStep();
        Draw();

        RenderImGuiFrame(m_window);
    }

    m_renderThread->SendCommand(RenderThread::Terminate);
    m_renderThread->Join();

    if (int waveEnd = GetCurrentWave(); waveEnd > 0)
        UpdateField(waveEnd);

    DestroyImGui(m_window);
    return 0;
}

void Application::Draw() {
    UpdateRayCastingAtMouse();
    MainMenu();

    // Controls, Colormap, and RayCasting
    {
        ImGui::Begin("Controls");
        m_controlPanel->Draw();
        int wave = GetCurrentWave();
        ImGui::ProgressBar((float) wave / (float) m_spp, {ImGui::GetColumnWidth(), 0}, wave >= m_spp ? "Done" : StringPrintf("%d/%d SPP", wave, m_spp).c_str());
        ErrorMetricSelector();
        ViewportOptions();
        m_colormapPanel->Draw();
        RayCastingPanel();
        ImGui::End();
    }

    // Viewport, Channels, and Status Bar
    if (ImGui::Begin("Viewport")) {
        ChannelSelector();
        m_viewport->Draw();
        CacheProbesInteraction();
        SDREViewInteraction();
        ImGui::Separator();
        StatusBar();
    }
    ImGui::End();

    // Settings
    if (ImGui::Begin("Settings")) {
        IntegratorSettings();
        GuideSettings();
        SpatialSubdivisionSettings();
    }
    ImGui::End();


    if (m_enableImGuiDemo) ImGui::ShowDemoWindow(&m_enableImGuiDemo);
    if (m_enableImPlotDemo) ImPlot::ShowDemoWindow(&m_enableImPlotDemo);

    if (m_enableMonitor) CacheMonitorViews();
    if (m_enableHistogram) CacheHistogramViews();
    if (m_enablePlots) PlotsView();

    if (m_enableRadianceView) {
        if (ImGui::Begin("Radiance View", &m_enableRadianceView))
            m_radianceView->Draw();
        ImGui::End();
    }

    if (m_enableSamplingDistributionView) {
        if (ImGui::Begin("Sampling Distribution", &m_enableSamplingDistributionView))
            m_samplingDistributionView->Draw();
        ImGui::End();
    }

    if (m_enableSignatureView) {
        if (ImGui::Begin("Signature View", &m_enableSignatureView))
            m_signatureView->Draw();
        ImGui::End();
    }

    if (m_enableRayCastingHistory) {
        if (ImGui::Begin("Ray Casting History", &m_enableRayCastingHistory))
            RayCastingHistory();
        ImGui::End();
    }
}

void Application::SetSelectedChannel(SelectedChannel newChannel) {
    if (!m_isMultiChannel && newChannel != Channel_Radiance)
        return;
    if (newChannel != m_selectedChannel) {
        m_selectedChannel = newChannel;
        m_viewport->RequestUpdate();
    }
}

void Application::SetupLayout() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    switch (m_layout) {
        case Layout_Default:
            SetupLayoutDefault();
            break;
        case Layout_Compact:
            SetupLayoutCompact();
            break;
        case Layout_ProbeViews:
            SetupLayoutProbeViews();
            break;
        case Layout_CacheMonitor:
            SetupLayoutCacheMonitor();
            break;
        case Layout_Histograms:
            SetupLayoutHistograms();
            break;
        default:
            ErrorExit("Unsupported layout type");
    }
    ImGui::PopStyleVar();
}

void Application::SetupLayoutDefault() {
    if (!m_hasSetupLayout) {
        m_enableMonitor = false;
        m_enableHistogram = false;
        m_enableSamplingDistributionView = true;
        m_enableRadianceView = true;
        m_enableSignatureView = true;
        m_enableRayCastingHistory = false;
        m_enableImGuiDemo = false;
        m_enableImPlotDemo = false;
        // Figure out proper window size
        auto mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
        m_windowSize = ImVec2(std::min(m_resolution.x + 600, mode->width), std::max(800, std::min(m_resolution.y + 120, mode->height)));
        glfwSetWindowSize(m_window, m_windowSize.x, m_windowSize.y);
    }

    // ImGui::DockSpaceOverViewport();
    ImGuiViewport *iviewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(iviewport->WorkPos);
    ImGui::SetNextWindowSize(iviewport->WorkSize);
    ImGui::SetNextWindowViewport(iviewport->ID);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("MyDockSpace", nullptr, windowFlags);

    ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    // dockFlags |= ImGuiDockNodeFlags_AutoHideTabBar;
    ImGuiID dockSpaceID = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockSpaceID, ImVec2(0.0f, 0.0f), dockFlags);
    // ImGui::DockSpaceOverViewport(dockSpaceID, ImGui::GetMainViewport(), dockFlags);

    if (!m_hasSetupLayout) {
        ImGui::DockBuilderRemoveNode(dockSpaceID);
        ImGui::DockBuilderAddNode(dockSpaceID, dockFlags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceID, iviewport->Size);

        ImGuiID leftDock, leftTopDock, leftBottomDock, midDock, rightDock, rightTopDock, rightBottomDock;
        ImGui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Left, 0.5f, &leftDock, &rightDock);
        ImGui::DockBuilderSplitNode(leftDock, ImGuiDir_Up, 0.5f, &leftTopDock, &leftBottomDock);
        ImGui::DockBuilderSplitNode(rightDock, ImGuiDir_Left, 0.5f, &midDock, &rightDock);
        ImGui::DockBuilderSplitNode(rightDock, ImGuiDir_Up, 0.5f, &rightTopDock, &rightBottomDock);
        ImGui::DockBuilderSetNodeSize(leftDock, ImVec2(239, iviewport->Size.y));
        float padding = ImGui::GetStyle().WindowPadding.x;
        ImGui::DockBuilderSetNodeSize(midDock, ImVec2(std::min(m_resolution.x + 2 * padding, m_windowSize.x - 239 - 350), iviewport->Size.y));

        ImGui::DockBuilderDockWindow("Controls", leftTopDock);
        for (auto s: {"Settings", "Fluence Histogram", "Energy Histogram", "Depth Histogram", "Samples Histogram"})
            ImGui::DockBuilderDockWindow(s, leftBottomDock);
        ImGui::DockBuilderDockWindow("Viewport", midDock);
        ImGui::DockBuilderDockWindow("Radiance View", rightTopDock);
        ImGui::DockBuilderDockWindow("Sampling Distribution", rightBottomDock);
        for (auto s: {"CE Curve", "Energy Curve", "Fluence Curve", "Depth Curve", "Samples Curve", "Signature View",
            "Regions Plot", "Error Plot", "Rendering Time Plot", "Training Time Plot"})
            ImGui::DockBuilderDockWindow(s, rightBottomDock);
        ImGui::DockBuilderFinish(dockSpaceID);

        m_hasSetupLayout = true;
    }

    ImGui::End();
}

void Application::SetupLayoutCompact() {
    if (!m_hasSetupLayout) {
        m_enableMonitor = false;
        m_enableHistogram = false;
        m_enableSamplingDistributionView = false;
        m_enableRadianceView = false;
        m_enableSignatureView = true;
        m_enableRayCastingHistory = false;
        m_enableImGuiDemo = false;
        m_enableImPlotDemo = false;
        // Figure out proper window size
        auto mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
        m_windowSize = ImVec2(std::min(m_resolution.x + 400, mode->width), std::min(m_resolution.y + 120, mode->height));
        glfwSetWindowSize(m_window, m_windowSize.x, m_windowSize.y);
    }

    // ImGui::DockSpaceOverViewport();
    ImGuiViewport *iviewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(iviewport->WorkPos);
    ImGui::SetNextWindowSize(iviewport->WorkSize);
    ImGui::SetNextWindowViewport(iviewport->ID);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("MyDockSpace", nullptr, windowFlags);

    ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    // dockFlags |= ImGuiDockNodeFlags_AutoHideTabBar;
    ImGuiID dockSpaceID = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockSpaceID, ImVec2(0.0f, 0.0f), dockFlags);
    // ImGui::DockSpaceOverViewport(dockSpaceID, ImGui::GetMainViewport(), dockFlags);

    if (!m_hasSetupLayout) {
        ImGui::DockBuilderRemoveNode(dockSpaceID);
        ImGui::DockBuilderAddNode(dockSpaceID, dockFlags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceID, iviewport->Size);

        ImGuiID leftDock, rightDock, rightTopDock, rightBottomDock;
        ImGui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Left, 0.7f, &leftDock, &rightDock);
        ImGui::DockBuilderSplitNode(rightDock, ImGuiDir_Up, 0.5f, &rightTopDock, &rightBottomDock);
        float padding = ImGui::GetStyle().WindowPadding.x;
        ImGui::DockBuilderSetNodeSize(leftDock, ImVec2(std::min(m_resolution.x + 2 * padding, m_windowSize.x - 400), iviewport->Size.y));
        ImGui::DockBuilderSetNodeSize(rightTopDock, ImVec2(400, m_windowSize.y * 0.5));
        ImGui::DockBuilderSetNodeSize(rightBottomDock, ImVec2(400, m_windowSize.y * 0.5));

        ImGui::DockBuilderDockWindow("Viewport", leftDock);
        ImGui::DockBuilderDockWindow("Controls", rightTopDock);
        ImGui::DockBuilderDockWindow("Settings", rightTopDock);
        ImGui::DockBuilderDockWindow("Sampling Distribution", rightBottomDock);
        ImGui::DockBuilderDockWindow("Radiance View", rightBottomDock);
        ImGui::DockBuilderDockWindow("CE Curve", rightBottomDock);
        ImGui::DockBuilderDockWindow("Energy Curve", rightBottomDock);
        ImGui::DockBuilderDockWindow("Fluence Curve", rightBottomDock);
        ImGui::DockBuilderDockWindow("Depth Curve", rightBottomDock);
        ImGui::DockBuilderDockWindow("Samples Curve", rightBottomDock);
        ImGui::DockBuilderDockWindow("Fluence Histogram", rightBottomDock);
        ImGui::DockBuilderDockWindow("Energy Histogram", rightBottomDock);
        ImGui::DockBuilderDockWindow("Depth Histogram", rightBottomDock);
        ImGui::DockBuilderDockWindow("Samples Histogram", rightBottomDock);
        ImGui::DockBuilderDockWindow("Signature View", rightBottomDock);
        ImGui::DockBuilderDockWindow("Regions Plot", rightBottomDock);
        ImGui::DockBuilderDockWindow("Error Plot", rightBottomDock);
        ImGui::DockBuilderDockWindow("Rendering Time Plot", rightBottomDock);
        ImGui::DockBuilderDockWindow("Training Time Plot", rightBottomDock);
        ImGui::DockBuilderFinish(dockSpaceID);

        m_hasSetupLayout = true;
    }

    ImGui::End();
}

void Application::SetupLayoutProbeViews() {
    if (!m_hasSetupLayout) {
        m_enableMonitor = false;
        m_enableHistogram = false;
        m_enableSamplingDistributionView = true;
        m_enableRadianceView = true;
        m_enableSignatureView = false;
        m_enableRayCastingHistory = false;
        m_enableImGuiDemo = false;
        m_enableImPlotDemo = false;
        // Figure out proper window size
        auto mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
        m_windowSize = ImVec2(std::min(m_resolution.x + 600, mode->width), std::min(m_resolution.y + 120, mode->height));
        glfwSetWindowSize(m_window, m_windowSize.x, m_windowSize.y);
    }

    // ImGui::DockSpaceOverViewport();
    ImGuiViewport *iviewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(iviewport->WorkPos);
    ImGui::SetNextWindowSize(iviewport->WorkSize);
    ImGui::SetNextWindowViewport(iviewport->ID);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("MyDockSpace", nullptr, windowFlags);

    ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    // dockFlags |= ImGuiDockNodeFlags_AutoHideTabBar;
    ImGuiID dockSpaceID = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockSpaceID, ImVec2(0.0f, 0.0f), dockFlags);
    // ImGui::DockSpaceOverViewport(dockSpaceID, ImGui::GetMainViewport(), dockFlags);

    if (!m_hasSetupLayout) {
        ImGui::DockBuilderRemoveNode(dockSpaceID);
        ImGui::DockBuilderAddNode(dockSpaceID, dockFlags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceID, iviewport->Size);

        ImGuiID leftDock, leftTopDock, leftBottomDock, midDock, rightDock, rightTopDock, rightBottomDock;
        ImGui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Left, 0.5f, &leftDock, &rightDock);
        ImGui::DockBuilderSplitNode(leftDock, ImGuiDir_Up, 0.5f, &leftTopDock, &leftBottomDock);
        ImGui::DockBuilderSplitNode(rightDock, ImGuiDir_Left, 0.5f, &midDock, &rightDock);
        ImGui::DockBuilderSplitNode(rightDock, ImGuiDir_Up, 0.5f, &rightTopDock, &rightBottomDock);
        ImGui::DockBuilderSetNodeSize(leftDock, ImVec2(239, iviewport->Size.y));
        float padding = ImGui::GetStyle().WindowPadding.x;
        ImGui::DockBuilderSetNodeSize(midDock, ImVec2(std::min(m_resolution.x + 2 * padding, m_windowSize.x - 239 - 350), iviewport->Size.y));

        ImGui::DockBuilderDockWindow("Controls", leftTopDock);
        for (auto s: {"Settings",
            "Fluence Histogram", "Energy Histogram", "Depth Histogram", "Samples Histogram",
            "CE Curve", "Energy Curve", "Fluence Curve", "Depth Curve", "Samples Curve",
            "Regions Plot", "Error Plot", "Rendering Time Plot", "Training Time Plot"
        })
            ImGui::DockBuilderDockWindow(s, leftBottomDock);
        ImGui::DockBuilderDockWindow("Viewport", midDock);
        ImGui::DockBuilderDockWindow("Radiance View", rightTopDock);
        ImGui::DockBuilderDockWindow("Sampling Distribution", rightBottomDock);
        ImGui::DockBuilderFinish(dockSpaceID);

        m_hasSetupLayout = true;
    }

    ImGui::End();
}

void Application::SetupLayoutCacheMonitor() {
    if (!m_hasSetupLayout) {
        m_enableMonitor = true;
        m_enableHistogram = false;
        m_enableSamplingDistributionView = false;
        m_enableRadianceView = false;
        m_enableSignatureView = false;
        m_enableRayCastingHistory = false;
        m_enableImGuiDemo = false;
        m_enableImPlotDemo = false;
        SetFullScreen();
    }

    ImGuiViewport *iviewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(iviewport->WorkPos);
    ImGui::SetNextWindowSize(iviewport->WorkSize);
    ImGui::SetNextWindowViewport(iviewport->ID);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("MyDockSpace", nullptr, windowFlags);

    ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    // dockFlags |= ImGuiDockNodeFlags_AutoHideTabBar;
    ImGuiID dockSpaceID = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockSpaceID, ImVec2(0.0f, 0.0f), dockFlags);

    if (!m_hasSetupLayout) {
        ImGui::DockBuilderRemoveNode(dockSpaceID);
        ImGui::DockBuilderAddNode(dockSpaceID, dockFlags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceID, iviewport->Size);

        ImGuiID left, leftTopDock, leftMidDock, leftBottomDock;
        ImGuiID midDock;
        ImGuiID right, rightTopDock, rightMidDock, rightBottomDock;
        ImGui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Left, 0.5f, &left, &right);
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.3f, &leftTopDock, &leftBottomDock);
        ImGui::DockBuilderSplitNode(leftBottomDock, ImGuiDir_Up, 0.5f, &leftMidDock, &leftBottomDock);
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Left, 0.5f, &midDock, &right);
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.5f, &rightTopDock, &rightBottomDock);
        ImGui::DockBuilderSplitNode(rightBottomDock, ImGuiDir_Up, 0.5f, &rightMidDock, &rightBottomDock);
        ImGui::DockBuilderSetNodeSize(left, ImVec2(239, iviewport->Size.y));
        float padding = ImGui::GetStyle().WindowPadding.x;
        ImGui::DockBuilderSetNodeSize(midDock, ImVec2(std::min(m_resolution.x + 2 * padding, m_windowSize.x - 239 - 350), iviewport->Size.y));
        ImGui::DockBuilderSetNodeSize(rightTopDock, ImVec2(350, m_windowSize.y * 0.25));
        ImGui::DockBuilderSetNodeSize(rightMidDock, ImVec2(350, m_windowSize.y * 0.25));
        ImGui::DockBuilderSetNodeSize(rightBottomDock, ImVec2(350, m_windowSize.y * 0.5));

        ImGui::DockBuilderDockWindow("Controls", leftTopDock);
        ImGui::DockBuilderDockWindow("Settings", leftMidDock);
        ImGui::DockBuilderDockWindow("Signature View", leftMidDock);
        ImGui::DockBuilderDockWindow("Fluence Histogram", leftBottomDock);
        ImGui::DockBuilderDockWindow("Energy Histogram", leftBottomDock);
        ImGui::DockBuilderDockWindow("Depth Histogram", leftBottomDock);
        ImGui::DockBuilderDockWindow("Samples Histogram", leftBottomDock);
        ImGui::DockBuilderDockWindow("Sampling Distribution", leftBottomDock);
        ImGui::DockBuilderDockWindow("Radiance View", leftBottomDock);
        ImGui::DockBuilderDockWindow("Regions Plot", leftBottomDock);
        ImGui::DockBuilderDockWindow("Error Plot", leftBottomDock);
        ImGui::DockBuilderDockWindow("Rendering Time Plot", leftBottomDock);
        ImGui::DockBuilderDockWindow("Training Time Plot", leftBottomDock);
        ImGui::DockBuilderDockWindow("Viewport", midDock);
        ImGui::DockBuilderDockWindow("Energy Curve", rightBottomDock);
        ImGui::DockBuilderDockWindow("CE Curve", rightBottomDock);
        ImGui::DockBuilderDockWindow("Depth Curve", rightTopDock);
        ImGui::DockBuilderDockWindow("Samples Curve", rightMidDock);
        ImGui::DockBuilderDockWindow("Fluence Curve", rightMidDock);
        ImGui::DockBuilderFinish(dockSpaceID);

        m_hasSetupLayout = true;
    }

    ImGui::End();
}

void Application::SetupLayoutHistograms() {
    if (!m_hasSetupLayout) {
        m_enableMonitor = false;
        m_enableHistogram = true;
        m_enableSamplingDistributionView = false;
        m_enableRadianceView = false;
        m_enableSignatureView = false;
        m_enableRayCastingHistory = false;
        m_enableImGuiDemo = false;
        m_enableImPlotDemo = false;
        SetFullScreen();
    }

    ImGuiViewport *iviewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(iviewport->WorkPos);
    ImGui::SetNextWindowSize(iviewport->WorkSize);
    ImGui::SetNextWindowViewport(iviewport->ID);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("MyDockSpace", nullptr, windowFlags);

    ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    // dockFlags |= ImGuiDockNodeFlags_AutoHideTabBar;
    ImGuiID dockSpaceID = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockSpaceID, ImVec2(0.0f, 0.0f), dockFlags);

    if (!m_hasSetupLayout) {
        ImGui::DockBuilderRemoveNode(dockSpaceID);
        ImGui::DockBuilderAddNode(dockSpaceID, dockFlags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceID, iviewport->Size);

        ImGuiID left, leftTopDock, leftMidDock, leftBottomDock;
        ImGuiID midDock;
        ImGuiID right;
        ImGui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Left, 0.5f, &left, &right);
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.3f, &leftTopDock, &leftBottomDock);
        ImGui::DockBuilderSplitNode(leftBottomDock, ImGuiDir_Up, 0.5f, &leftMidDock, &leftBottomDock);
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Left, 0.5f, &midDock, &right);
        ImGui::DockBuilderSetNodeSize(left, ImVec2(239, iviewport->Size.y));
        float padding = ImGui::GetStyle().WindowPadding.x;
        ImGui::DockBuilderSetNodeSize(midDock, ImVec2(std::min(m_resolution.x + 2 * padding, m_windowSize.x - 239 - 500), iviewport->Size.y));

        ImGui::DockBuilderDockWindow("Controls", leftTopDock);
        ImGui::DockBuilderDockWindow("Settings", leftMidDock);
        ImGui::DockBuilderDockWindow("Signature View", leftMidDock);
        ImGui::DockBuilderDockWindow("CE Curve", leftBottomDock);
        ImGui::DockBuilderDockWindow("Energy Curve", leftBottomDock);
        ImGui::DockBuilderDockWindow("Fluence Curve", leftBottomDock);
        ImGui::DockBuilderDockWindow("Depth Curve", leftBottomDock);
        ImGui::DockBuilderDockWindow("Samples Curve", leftBottomDock);
        ImGui::DockBuilderDockWindow("Sampling Distribution", leftBottomDock);
        ImGui::DockBuilderDockWindow("Radiance View", leftBottomDock);
        ImGui::DockBuilderDockWindow("Regions Plot", leftBottomDock);
        ImGui::DockBuilderDockWindow("Error Plot", leftBottomDock);
        ImGui::DockBuilderDockWindow("Rendering Time Plot", leftBottomDock);
        ImGui::DockBuilderDockWindow("Training Time Plot", leftBottomDock);
        ImGui::DockBuilderDockWindow("Viewport", midDock);
        ImGui::DockBuilderDockWindow("Histograms", right);
        ImGui::DockBuilderFinish(dockSpaceID);

        m_hasSetupLayout = true;
    }

    ImGui::End();
}

void Application::SetFullScreen() {
    auto mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    m_windowSize = ImVec2(mode->width, mode->height);
    glfwSetWindowSize(m_window, mode->width, mode->height);
}

void Application::SetupRenderThread() {
    if (m_renderThread)
        ErrorExit("RenderThread already initialized");
    m_renderThread = std::make_unique<RenderThread>(
        this,
        [&](int waveStart) {
            CheckIsRenderThread();
            if (m_recordSamples) SaveSamplesNpy(m_recordSamplesDir);
            UpdateField(waveStart);
            UpdateCacheCurves();
            UpdateCacheHistograms();
            UpdatePlots();
            UpdateSamplingDistributionView();
            UpdateSignatureView();
            RenderWave(waveStart);
            UpdateCPUBufferFromFilm();
        }, m_saveImage);
    m_renderThread->SetCmdCallback(RenderThread::Restart, [&] {
        CheckIsRenderThread();
        RestartRendering(true);
        return true;
    });
}

int Application::GetCurrentWave() const {
    return m_renderThread->GetWaveStart();
}

void Application::CheckIsMainThread() {
    if (std::this_thread::get_id() != m_renderThread->GetMainThreadID())
        ErrorExit("This function should be called from the main thread");
}

void Application::CheckIsRenderThread() {
    if (std::this_thread::get_id() != m_renderThread->GetRenderThreadID())
        ErrorExit("This function should be called from the render thread");
}

Application::RayCastingData Application::RayCast(Point2i pixel) const {
    RayCastingData rc{pixel};
    static ScratchBuffer scratchBuffer;
    if (rc.pixel.x >= 0 && rc.pixel.x < m_resolution.x && rc.pixel.y >= 0 && rc.pixel.y < m_resolution.y) {
        IndependentSampler _sampler(m_spp, 0);
        Sampler sampler(&_sampler);
        Filter filter = m_camera.GetFilm().GetFilter();
        CameraSample cameraSample = GetCameraSample(sampler, rc.pixel, filter);
        SampledWavelengths lambda = m_camera.GetFilm().SampleWavelengths(sampler.Get1D());
        if (auto cameraRay = m_camera.GenerateRayDifferential(cameraSample, lambda)) {
            RayDifferential ray = cameraRay->ray;
            while (true) {  // Ray tracing until hitting a diffuse surface
                auto sit = m_scene.Intersect(ray);
                if (!sit) break;
                auto bsdf = sit->intr.GetBSDF(ray, lambda, m_camera, scratchBuffer, sampler);
                if (!bsdf) {
                    sit->intr.SkipIntersection(&ray, sit->tHit);
                    continue;
                }
                auto flags = bsdf.Flags();
                if (IsSpecular(flags)) {
                    BxDFReflTransFlags sFlags = IsTransmissive(flags) ? BxDFReflTransFlags::Transmission : BxDFReflTransFlags::Reflection;
                    auto bs = bsdf.Sample_f(-ray.d, 0, {0, 0}, TransportMode::Radiance, sFlags);
                    if (!bs) break;
                    // ray = sit->intr.SpawnRay(ray, bsdf, bs->wi, bs->flags, bs->eta);
                    ray = sit->intr.SpawnRay(bs->wi);  // Continue tracing
                } else {
                    // Diffuse surface. Good
                    rc.valid = true;
                    rc.hit = ray(sit->tHit);
                    if (Dot(ray.d, sit->intr.shading.n) > 0) sit->intr.shading.n *= -1;  // flip normal when backfacing
                    rc.normal = sit->intr.shading.n;
                    rc.uv = sit->intr.uv;
                    // Query the guiding cache
                    pgl_point3f pglP = {rc.hit.x, rc.hit.y, rc.hit.z};
                    std::lock_guard lock(m_mtx.field);  // avoid race condition when the field is updated
                    std::tie(rc.coarse, rc.fine) = m_field.GetCoarseFineRegionStatisticsSurface(pglP);
                    break;
                }
            }
        }
        scratchBuffer.Reset();
    }
    return rc;
}

void Application::UpdateFramebuffer() {
    // Update the framebuffer when the film is updated
    SelectedChannel c = m_selectedChannel;
    bool isDiffCE = c == Channel_CE && m_showDiff;
    auto &sd = isDiffCE ? m_colormapPanel->shaderDataDiffCE : m_colormapPanel->shaderData[c];
    float clipValue = std::numeric_limits<float>::infinity();
    if (m_colormapPanel->isHovered) {
        clipValue = m_colormapPanel->hoveringValue;
    } else if (c == Channel_Fluence && m_cacheHistogram.fluence->isHovered) {
        clipValue = m_cacheHistogram.fluence->hoveringValue;
    } else if (c == Channel_Energy && m_cacheHistogram.energy->isHovered) {
        clipValue = m_cacheHistogram.energy->hoveringValue;
    } else if (c == Channel_Depth && m_cacheHistogram.depth->isHovered) {
        clipValue = m_cacheHistogram.depth->hoveringValue;
    } else if (c == Channel_Samples && m_cacheHistogram.samples->isHovered) {
        clipValue = m_cacheHistogram.samples->hoveringValue;
    }
    m_viewport->UpdateFramebuffer({sd.scale, sd.offset, clipValue, cmap_tex_ids[sd.cmap]}, m_selectedChannel, m_showFine, m_showDiff, sd.boundary);
}

void Application::SaveRendering(std::string path) {
    ImageMetadata meta;
    Image image = m_film.GetImage(&meta, 1.f / GetCurrentWave());
    if (image.Write(path, meta))
        std::cout << "Save image to " << path << std::endl;
    else
        Error("Failed to save image to %s", path);
}

void Application::SaveField(std::string path) {
    std::lock_guard lock(m_mtx.field);
    if (m_field.Store(path))
        std::cout << "Save field to " << path << std::endl;
    else
        Error("Failed to save field to %s", path);
}

void Application::LoadField(std::string path) {
    std::lock_guard lock(m_mtx.field);
    if (m_field.Load(&m_device, path))
        std::cout << "Load field from " << path << " with " << m_field.GetRegionCountSurface(false) << " regions" << std::endl;
    else
        Error("Failed to load field from %s", path);
    m_field.LoadSubdivConfig(m_subdivCfg);
}

void Application::SaveSamples(std::string path) {
    std::lock_guard lock(m_mtx.field);
    if (m_sampleStorage.Store(path))
        std::cout << "Save samples to " << path << std::endl;
    else
        Error("Failed to save samples to %s", path);
}

void Application::LoadSamples(std::string path) {
    std::lock_guard lock(m_mtx.field);
    if (m_sampleStorage.Load(path))
        std::cout << "Load " << m_sampleStorage.GetSizeSurface() + m_sampleStorage.GetSizeVolume() << " samples from " << path << std::endl;
    else
        Error("Failed to load samples from %s", path);
}

void Application::SaveSamplesNpy(std::string dir) {
    std::lock_guard lock(m_mtx.field);
    dumpSampleStorage(dir, &m_sampleStorage, GetCurrentWave() - 1);
}

void Application::CacheInfo(const PGLRegionStatistics &coarse, const PGLRegionStatistics &fine) {
    bool coarseIsValid = coarse.id != -1, fineIsValid = fine.id != -1;
    if (!coarseIsValid) {
        ImGui::Text("Cache: <invalid>");
        return;
    }
    CHECK(!coarse.removed);
    CHECK(!fineIsValid || !fine.removed);
    if (fineIsValid)
        ImGui::Text("Cache ID Parent/Child: %u/%u", coarse.id, fine.id);
    else
        ImGui::Text("Cache ID: %u", coarse.id);
    auto f = [](const PGLRegionStatistics &stats) {
        ImGui::Text("Energy: %f", stats.energy);
        ImGui::Text("Fluence: %f", stats.fluence);
        ImGui::Text("CE: %f", stats.crossEntropy);
        ImGui::Text("Nonzero/Zero Samples: %s/%s", FormatInteger(stats.numSamples).c_str(), FormatInteger(stats.numZeroValueSamples).c_str());
        ImGui::Text("Depth: %d", (int) stats.depth);
        if (stats.splitDim < 3) {
            static const char dim_ch[] = {'x', 'y', 'z'};
            ImGui::Text("Candidate Split Dim: %c", dim_ch[stats.splitDim]);
            ImGui::Text("Candidate Split Pos: %f", stats.splitPos);
        }
    };
    if (IsShowingFine() && fineIsValid) f(fine);
    else f(coarse);
    // ImGui::Text("Sample Mean: (%.4f, %.4f, %.4f)", coarse.sampleMean[0], coarse.sampleMean[1], coarse.sampleMean[2]);
    // ImGui::Text("Sample Variance: (%.4f, %.4f, %.4f)", coarse.sampleVariance[0], coarse.sampleVariance[1], coarse.sampleVariance[2]);
}

void Application::AppendToRayCastingHistory(const RayCastingData &rc) {
    auto radiance = m_film.GetPixelRGB(rc.pixel);
    float error = m_viewport->GetErrorAtPixel(rc.pixel);
    m_rcHistory += StringPrintf(
        "Pixel: (%d, %d)\n"
        "Radiance: (%f, %f, %f)\n"
        "Error: %f\n",
        rc.pixel.x, rc.pixel.y,
        radiance[0], radiance[1], radiance[2], error);
    if (rc.valid) {
        m_rcHistory += StringPrintf(
            "Hit: (%f, %f, %f)\n"
            "Normal: (%f, %f, %f)\n"
            "UV: (%f, %f)\n",
            rc.hit.x, rc.hit.y, rc.hit.z,
            rc.normal.x, rc.normal.y, rc.normal.z,
            rc.uv.x, rc.uv.y);
        auto printDS = [](const PGLDirectionalSignature &ds) -> std::string {
            std::string s = StringPrintf("(%.4f", ds.signature[0]);
            for (int i = 1; i < (int) pglGetSignatureSize(); ++i)
                s += StringPrintf(", %.4f", ds.signature[i]);
            s += ")";
            return s;
        };
        auto printDSV = [](const PGLDirectionalSignature &ds) -> std::string {
            std::string s = StringPrintf("(%.2e", ds.std[0]);
            for (int i = 1; i < (int) pglGetSignatureSize(); ++i)
                s += StringPrintf(", %.2e", ds.std[i]);
            s += ")";
            return s;
        };
        auto printCache = [&](const PGLRegionStatistics &s) -> std::string {
            if (s.id == -1) return "  <invalid>\n";
            std::lock_guard lock(m_mtx.field);
            return StringPrintf(
                "  ID: %u\n"
                "  Samples: %d\n"
                "  Zero Samples: %d\n"
                "  Depth: %d\n"
                "  Energy:  %f\n"
                "  Fluence: %f\n"
                "  CE:      %f\n"
                "  Bounds: (%f, %f, %f) - (%f, %f, %f)\n",
                s.id, s.numSamples, s.numZeroValueSamples, (int) s.depth, s.energy, s.fluence, s.crossEntropy,
                s.lowerBounds.x, s.lowerBounds.y, s.lowerBounds.z,
                s.upperBounds.x, s.upperBounds.y, s.upperBounds.z);
        };
        m_rcHistory += "Parent Cache:\n";
        m_rcHistory += printCache(rc.coarse);
        m_rcHistory += "Child Cache:\n";
        m_rcHistory += printCache(rc.fine);
        pgl_point3f pglP{rc.hit.x, rc.hit.y, rc.hit.z};
        uint8_t splitDim;
        bool isRight;
        auto signatures = m_field.GetDirectionalSignatures(pglP, 1, splitDim, isRight);
        auto ds = isRight ? signatures.second : signatures.first;
        m_rcHistory += "Directional Signature:\n"
            "  Mean: " + printDS(ds) + "\n"
            "  Std:  " + printDSV(ds) + "\n";
    } else {
        m_rcHistory += "<no intersection>\n";
    }
    m_rcHistory += "\n";
}

void Application::UpdateRayCastingAtMouse() {
    if (m_enableRayCastingAtMouse) {
        if (m_viewport->IsHovered()) {
            m_rcMouse = RayCast(m_viewport->GetMousePixel());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                AppendToRayCastingHistory(m_rcMouse);
            }
        } else {
            m_rcMouse.valid = false;
            m_rcMouse.coarse.id = m_rcMouse.fine.id = -1;
        }
        if (m_rcMouse.coarse.id != -1) {
            // Tooltip next to the mouse
            if (ImGui::BeginTooltip()) {
                CacheInfo(m_rcMouse.coarse, m_rcMouse.fine);
                ImGui::EndTooltip();
            }
        }
    }
}

// Sampling distribution and radiance view
void Application::SDREViewInteraction() {
    if (!m_enableSamplingDistributionView && !m_enableRadianceView && !m_enableSignatureView) return;
    // Left click to update the sampling distribution
    if (m_viewport->IsHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left, true)) {
        Point2i pixel = m_viewport->GetMousePixel();
        m_rcSDRE = RayCast(pixel);
        UpdateSamplingDistributionView();
        NewRadianceViewRendering();
        UpdateSignatureView();
    }

    // Draw the view location
    if (m_rcSDRE.valid) {
        ImVec2 leftTop = m_viewport->GetLeftTop();
        float scale = m_viewport->GetScale();
        ImVec2 center(leftTop.x + m_rcSDRE.pixel.x * scale, leftTop.y + m_rcSDRE.pixel.y * scale);
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        float a = 4;
        bool isHovered = m_viewport->IsHovered() && Distance(m_viewport->GetMousePixel(), m_rcSDRE.pixel) < 2 * a;
        draw_list->AddTriangleFilled(ImVec2(center.x - a, center.y + a), ImVec2(center.x + a, center.y + a), center, IM_COL32(255, 0, 0, 255));
        if (isHovered)
            draw_list->AddTriangle(ImVec2(center.x - a, center.y + a), ImVec2(center.x + a, center.y + a), center, IM_COL32_WHITE, 2);

        // Right click to clear the views
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_rcSDRE.valid = false;
            m_samplingDistributionView->Clear();
            m_radianceView->Clear();
            m_signatureView->Clear();
        }
    }
}

void Application::CacheProbesInteraction() {
    if (!m_enableMonitor) return;
    // Draw all probes
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 leftTop = m_viewport->GetLeftTop();
    float scale = m_viewport->GetScale();
    m_cacheMonitor.object->ForEachProbe([&](CacheMonitor::Probe &probe) {
        std::string label = StringPrintf("#%d", probe.idx);
        bool isHovered = m_viewport->IsHovered() && Distance(m_viewport->GetMousePixel(), probe.pixel) < m_cacheMonitor.object->GetProbeRadius();
        // Right click to remove a probe
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            probe.active = false;
        ImVec2 center(leftTop.x + probe.pixel.x * scale, leftTop.y + probe.pixel.y * scale);
        ImU32 col = ImPlot::GetColormapColorU32(probe.idx, -1);
        ImU32 border_col = isHovered ? IM_COL32_WHITE : IM_COL32_BLACK;
        if (!probe.active) {
            col = ImGui::GetColorU32(col, 0.2f);
            border_col = ImGui::GetColorU32(border_col, 0.5f);
        }
        draw_list->AddCircleFilled(center, 5, col);
        draw_list->AddCircle(center, 5, border_col, 0, 1.2);
        if (isHovered || m_cacheMonitor.object->DisplayProbeID()) {
            ImVec2 pos(center.x - 8, center.y - 18);
            draw_list->AddText(pos, border_col, label.c_str());
        }
    });

    // Middle click to add/activate a probe
    if (m_viewport->IsHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        Point2i pixel = m_viewport->GetMousePixel();
        m_cacheMonitor.object->AddProbe(pixel);
        if (m_renderThread->GetState() != RenderThread::Rendering) {  // Add a probe with the current CE
            m_cacheMonitor.object->UpdateProbe(pixel, [&](CacheMonitor::Probe &probe) {
                float x = GetCurrentWave();
                const float nan = std::numeric_limits<float>::quiet_NaN();
                if (probe.data.empty() || probe.data.back().iter < x) {
                    auto rc = RayCast(pixel);
                    bool coarseValid = rc.coarse.id != -1;
                    bool fineValid = rc.fine.id != -1;
                    probe.data.push_back({x,
                        coarseValid ? (float) rc.coarse.depth : nan,
                        coarseValid ? (float) rc.coarse.numSamples : nan,
                        fineValid ? rc.fine.fluence : (coarseValid ? rc.coarse.fluence : nan),
                        coarseValid ? rc.coarse.crossEntropy : nan,
                        fineValid ? rc.fine.energy : (coarseValid ? rc.coarse.energy : nan),
                        coarseValid && fineValid ? 0 : nan,
                        coarseValid && fineValid ? rc.fine.crossEntropy - rc.coarse.crossEntropy : nan,
                        coarseValid ? rc.coarse.crossEntropy/* - m_subdivCfg.ceThreshold*/ : nan,
                    });
                }
            });
        }
        // Else: Data will be added when the wave ends
    }
}

void Application::UpdateField(int waveEnd) {
    // CheckIsRenderThread();
    {
        std::lock_guard lock(m_mtx.subdivCfg);
        m_field.UpdateSubdivConfig(m_subdivCfg);
    }
    std::lock_guard lock(m_mtx.field);
    Timer timer;
    if (waveEnd > 0)
        m_updateCache(waveEnd);  // calls GuidedPathIntegrator::PostProcessWave()
    m_waveStats.postprocessMS = timer.ElapsedSeconds() * 1e3;
    m_waveStats.numRegions = m_field.GetRegionCountSurface(false);
}

void Application::RenderWave(int waveStart) {
    CheckIsRenderThread();
    Timer timer;
    m_renderWave(waveStart);
    m_waveStats.renderMS = timer.ElapsedSeconds() * 1e3;
    m_waveStats.trainingSamples = m_sampleStorage.GetSizeSurface() + m_sampleStorage.GetSizeVolume();
}

void Application::ClearFilm() {
    ParallelFor2D(m_film.PixelBounds(), [&](Point2i p) {
        m_film.ResetPixel(p);
    });
}

void Application::ResetCache() {
    std::lock_guard lock(m_mtx.field);
    m_field.Reset();
    m_sampleStorage.Clear();
}

void Application::RestartRendering(bool resetCache) {
    ClearFilm();
    UpdateCPUBufferFromFilm();
    if (resetCache)
        ResetCache();
    m_cacheMonitor.object->Clear();
    m_cacheHistogram.object->Clear();
    m_samplingDistributionView->Clear();
    m_signatureView->Clear();
    m_plots.object->ClearCurrent();
}

void Application::UpdateCPUBufferFromFilm() {
    Timer timer;
    m_viewport->UpdateCPUBufferFromFilm();
    std::cout << "Update CPU buffer: " << timer.ElapsedSeconds() * 1e3 << " ms" << std::endl;
}

void Application::UpdateCacheCurves() {
    float x = (float) GetCurrentWave();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    m_cacheMonitor.object->ForEachProbe([&](CacheMonitor::Probe &probe) {
        if (probe.active) {
            auto rc = RayCast(probe.pixel);
            bool coarseValid = rc.coarse.id != -1;
            bool fineValid = rc.fine.id != -1;
            probe.data.push_back({x,
                coarseValid ? (float) rc.coarse.depth : nan,
                coarseValid ? (float) rc.coarse.numSamples : nan,
                fineValid ? rc.fine.fluence : (coarseValid ? rc.coarse.fluence : nan),
                coarseValid ? rc.coarse.crossEntropy : nan,
                fineValid ? rc.fine.energy : (coarseValid ? rc.coarse.energy : nan),
                coarseValid && fineValid ? 0 : nan,
                coarseValid && fineValid ? rc.fine.crossEntropy - rc.coarse.crossEntropy : nan,
                coarseValid ? rc.coarse.crossEntropy/* - m_subdivCfg.ceThreshold*/ : nan,
            });
        }
    });
    m_cacheMonitor.object->RequestFitAxes();
}

void Application::UpdateCacheHistograms() {
    m_cacheHistogram.object->Update([&](CacheHistogram::Data &data) {
        size_t numRegions = m_field.GetRegionCountSurface();
        data.fluence.clear();
        data.ce.clear();
        data.energy.clear();
        data.depth.clear();
        data.samples.clear();
        for (size_t i = 0; i < numRegions; ++i) {
            auto cache = m_field.GetRegionStatisticsSurface(i);
            if (cache.removed) continue;
            data.fluence.push_back(cache.fluence);
            data.ce.push_back(cache.crossEntropy);
            data.energy.push_back(cache.energy);
            data.depth.push_back(cache.depth);
            data.samples.push_back(cache.numSamples);
        }
    });
    m_cacheHistogram.object->RequestFitAxes();
}

void Application::UpdatePlots() {
    float x = GetCurrentWave();
    m_plots.object->AppendData("regions", x, m_waveStats.numRegions);
    m_plots.object->AppendData("error", x, m_viewport->GetMeanError());
    m_plots.object->AppendData("rendering time", x, m_waveStats.renderMS);
    m_plots.object->AppendData("training time", x, m_waveStats.postprocessMS);
    m_plots.object->RequestFitAxes();
}

void Application::UpdateSamplingDistributionView() {
    if (m_rcSDRE.valid) {
        std::lock_guard lock(m_mtx.field);  // avoid race condition when the field is updated
        m_samplingDistributionView->UpdateCPUBuffer(m_rcSDRE.hit, m_rcSDRE.normal, m_showFine);
    } else {
        m_samplingDistributionView->Clear();
    }
}

void Application::NewRadianceViewRendering() {
    if (m_rcSDRE.valid) {
        // Launch a new rendering task at the clicked point
        m_radianceView->RenderStart(m_rcSDRE.hit, m_rcSDRE.normal);
        RadianceViewRenderStep();
        // Later rendering steps are performed in the main loop when the render thread is not busy
    } else {
        m_radianceView->Clear();
    }
}

void Application::RadianceViewRenderStep() {
    if (m_enableRadianceView && m_radianceView->IsRendering() && m_renderThread->GetState() != RenderThread::Rendering) {
        m_radianceView->RenderStep();
    }
}

void Application::UpdateSignatureView() {
    if (m_rcSDRE.valid) {
        std::lock_guard lock(m_mtx.field);
        m_signatureView->Update(m_rcSDRE.hit);
    } else {
        m_signatureView->Clear();
    }
}

void Application::MainMenu() {
    static std::string layoutNames[Layout_Count] = {"Default", "Compact", "Probe Views", "Cache Monitor", "Histograms"};
    bool openChangeResolutionPopup = false;
    auto openFileDialog = [&](bool write, const std::string &defaultPath, const std::string &title, const std::string &ext, const std::function<void(const std::string&, const std::string&)> &action) {
        IGFD::FileDialogConfig config;
        config.filePathName = defaultPath;
        config.flags = ImGuiFileDialogFlags_Default;
        if (!write) config.flags &= ~ImGuiFileDialogFlags_ConfirmOverwrite;
        ImGuiFileDialog::Instance()->OpenDialog("fileDialog", title, ext.empty() ? nullptr : ext.c_str(), config);
        m_enableShortcuts = false;  // Disable shortcuts when the modal dialog is open
        m_fileDialogCallback = action;
    };
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Layout")) {
            for (int i = 0; i < Layout_Count; ++i) {
                if (ImGui::MenuItem(layoutNames[i].c_str(), nullptr, m_layout == i)) {
                    m_layout = (LayoutType) i;
                    m_hasSetupLayout = false;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Rendering", m_renderThread->GetState() != RenderThread::Rendering)) {
            if (ImGui::MenuItem("Restart (resetting the cache)")) {
                m_renderThread->SetInitial();
                RestartRendering(true);
            }
            if (ImGui::MenuItem("Restart (keeping the cache)")) {
                m_renderThread->SetInitial();
                RestartRendering(false);
            }
            if (ImGui::MenuItem("Save Image")) {
                m_renderThread->SendCommand(RenderThread::Save);
            }
            if (ImGui::MenuItem("Save Image To...")) {
                openFileDialog(true, m_film.GetFilename(), "Save Image To...", ".exr", [this](auto dir, auto path) { SaveRendering(path); });
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Cache", m_renderThread->GetState() != RenderThread::Rendering)) {
            if (ImGui::MenuItem("Reset Field")) {
                ResetCache();
                m_cacheMonitor.object->Clear();
                m_cacheHistogram.object->Clear();
            }
            if (ImGui::MenuItem("Save Field", 0, nullptr, !m_guideSettings.guidingCacheFileName.empty())) {
                SaveField(m_guideSettings.guidingCacheFileName);
            }
            if (ImGui::MenuItem("Save Field To...")) {
                openFileDialog(true, m_guideSettings.guidingCacheFileName.empty() ? "." : m_guideSettings.guidingCacheFileName,
                    "Save Field To...", ".field,.*", [this](auto dir, auto path) { SaveField(path); });
            }
            if (ImGui::MenuItem("Load Field", 0, nullptr, !m_guideSettings.guidingCacheFileName.empty())) {
                LoadField(m_guideSettings.guidingCacheFileName);
            }
            if (ImGui::MenuItem("Load Field From...")) {
                openFileDialog(false, m_guideSettings.guidingCacheFileName, "Load Field From...", ".field,.*", [this](auto dir, auto path) { LoadField(path); });
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Samples")) {
                std::lock_guard lock(m_mtx.field);
                m_sampleStorage.Clear();
            }
            if (ImGui::MenuItem("Save Samples To...", 0, nullptr, m_sampleStorage.GetSizeSurface() > 0)) {
                openFileDialog(true, ".", "Save Samples To...", ".samples,.*", [this](auto dir, auto path) { SaveSamples(path); });
            }
            if (ImGui::MenuItem("Load Samples From...")) {
                openFileDialog(false, ".", "Load Samples From...", ".samples,.*", [this](auto dir, auto path) { LoadSamples(path); });
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Record Samples", "F4", m_recordSamples)) {
                m_recordSamples ^= true;
            }
            if (ImGui::MenuItem("Set Recording Directory...", 0, nullptr)) {
                openFileDialog(true, m_recordSamplesDir, "Select directory to dump samples in NumPy format...", "",
                               [this](auto dir, auto path) { m_recordSamplesDir = dir; });
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("SDRE View", m_renderThread->GetState() != RenderThread::Rendering)) {
            if (ImGui::MenuItem("Clear")) {
                m_rcSDRE.valid = false;
                m_samplingDistributionView->Clear();
                m_radianceView->Clear();
                m_signatureView->Clear();
            }
            if (ImGui::MenuItem("Set Resolution")) {
                openChangeResolutionPopup = true;
                m_enableShortcuts = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Cache Curves", 0, m_enableMonitor)) m_enableMonitor ^= true;
            if (ImGui::MenuItem("Cache Histograms", 0, m_enableHistogram)) m_enableHistogram ^= true;
            if (ImGui::MenuItem("Plots", 0, m_enablePlots)) m_enablePlots ^= true;
            ImGui::Separator();
            if (ImGui::MenuItem("Radiance View", 0, m_enableRadianceView)) m_enableRadianceView ^= true;
            if (ImGui::MenuItem("Sampling Distribution", 0, m_enableSamplingDistributionView)) m_enableSamplingDistributionView ^= true;
            if (ImGui::MenuItem("Signature View", 0, m_enableSignatureView)) m_enableSignatureView ^= true;
            if (ImGui::MenuItem("Ray Casting History", 0, m_enableRayCastingHistory)) m_enableRayCastingHistory ^= true;
            ImGui::Separator();
            if (ImGui::MenuItem("ImGui Demo", 0, m_enableImGuiDemo)) m_enableImGuiDemo ^= true;
            if (ImGui::MenuItem("ImPlot Demo", 0, m_enableImPlotDemo)) m_enableImPlotDemo ^= true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    // Dialogs
    if (ImGuiFileDialog::Instance()->Display("fileDialog")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            // action if OK
            std::string dir = ImGuiFileDialog::Instance()->GetCurrentPath();
            std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
            if (!m_fileDialogCallback)
                ErrorExit("No file dialog callback");
            m_fileDialogCallback(dir, path);
            m_fileDialogCallback = nullptr;
        }
        // close
        ImGuiFileDialog::Instance()->Close();
        m_enableShortcuts = true;
    }
    if (openChangeResolutionPopup) ImGui::OpenPopup("Change Resolution");
    if (ImGui::BeginPopupModal("Change Resolution", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static Point2i resolution;
        if (!m_hasOpenedChangeResolutionPopup) {
            resolution = m_samplingDistributionView->GetResolution();
            m_hasOpenedChangeResolutionPopup = true;
        }
        ImGui::InputInt("Width", &resolution.x, 0);
        ImGui::InputInt("Height", &resolution.y, 0);
        resolution.x = std::max(1, resolution.x);
        resolution.y = std::max(1, resolution.y);
        if (ImGui::Button("OK")) {
            m_radianceView->SetResolution(resolution);
            m_samplingDistributionView->SetResolution(resolution);
            ImGui::CloseCurrentPopup();
            m_hasOpenedChangeResolutionPopup = false;
            m_enableShortcuts = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            m_hasOpenedChangeResolutionPopup = false;
            m_enableShortcuts = true;
        }
        ImGui::EndPopup();
    }
    if (IsKeyPressed(ImGuiKey_F4, false)) {
        m_recordSamples ^= true;
    }
}

void Application::ErrorMetricSelector() {
    if (m_reference) {
        ErrorMetric oldMetric = m_errorMetric;
        ImGui::SetNextItemWidth(90);
        ImGui::Combo("Error Metric", reinterpret_cast<int *>(&m_errorMetric), errorMetricNames.data(), Metric_Count);
        if (oldMetric != m_errorMetric) {
            m_colormapPanel->errorFunc = m_viewport->errorFunc = GetErrorFunc(m_errorMetric);
            m_viewport->UpdateErrorImage();
        }
        ImGui::Text("Mean Error: %lf", m_viewport->GetMeanError());
    }
}

void Application::RayCastingPanel() {
    if (IsKeyPressed(ImGuiKey_C, false))
        m_enableRayCastingAtMouse ^= true;
    ImGui::SetNextItemOpen(m_enableRayCastingAtMouse);
    if ((m_enableRayCastingAtMouse = ImGui::CollapsingHeader("Ray Casting"))) {
        if (m_rcMouse.valid) {
            auto radiance = m_film.GetPixelRGB(m_rcMouse.pixel);
            ImGui::Text("Radiance: (%.2f, %.2f, %.2f)", radiance.r, radiance.g, radiance.b);
            ImGui::Text("Hit: (%.2f, %.2f, %.2f)", m_rcMouse.hit.x, m_rcMouse.hit.y, m_rcMouse.hit.z);
            ImGui::Text("Normal: (%.2f, %.2f, %.2f)", m_rcMouse.normal.x, m_rcMouse.normal.y, m_rcMouse.normal.z);
            ImGui::Text("UV: (%.2f, %.2f)", m_rcMouse.uv.x, m_rcMouse.uv.y);
            if (m_reference)
                ImGui::Text("Error: %f", m_viewport->GetErrorAtPixel(m_rcMouse.pixel));
            CacheInfo(m_rcMouse.coarse, m_rcMouse.fine);
        } else {
            ImGui::Text("No intersection");
        }
    }
}

void Application::RayCastingHistory() {
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_ReadOnly;
    ImGui::InputTextMultiline("##Ray-Casting-History", m_rcHistory.data(), m_rcHistory.size(),
        ImVec2(-1, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing()), flags);
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::BulletText("Open the ray casting panel or press 'C'");
        ImGui::BulletText("Hover the mouse over the viewport and left click to append result to the history");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        m_rcHistory.clear();
    }
}

void Application::ChannelSelector() {
    // Add a channel selection bar for GuidedGBufferFilm
    if (m_isMultiChannel) {
        SelectedChannel newChannel = m_selectedChannel;
        for (int i = 0; i < m_channelCount; ++i) {
            if (IsKeyPressed((ImGuiKey) (ImGuiKey_1 + i), false) && (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)))
                newChannel = (SelectedChannel) i;
        }

        if (ImGui::BeginTabBar("ChannelSelector")) {
            for (int i = 0; i < m_channelCount; ++i) {
                if (m_selectedChannel == i)
                    ImGui::PushStyleColor(ImGuiCol_Tab, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
                if (ImGui::TabItemButton(channelNames[i])) {
                    newChannel = (SelectedChannel) i;
                }
                if (m_selectedChannel == i)
                    ImGui::PopStyleColor();
            }
            ImGui::EndTabBar();
        }

        SetSelectedChannel(newChannel);
    }
}

void Application::ViewportOptions() {
    bool showFineOld = m_showFine, showDiffOld = m_showDiff;
    if (IsKeyPressed(ImGuiKey_F, false)) m_showFine ^= true;
    if (IsKeyPressed(ImGuiKey_D, false)) m_showDiff ^= true;
    ImGui::Checkbox("Show Lookaheads", &m_showFine);
    ImGui::SetItemTooltip("(F) Works for Cache ID, CE channels, and sampling distribution view.");
    ImGui::Checkbox("Show Difference", &m_showDiff);
    ImGui::SetItemTooltip("(D) Only works for Cache ID and CE channels.");
    if (m_showFine != showFineOld || m_showDiff != showDiffOld) {
        m_viewport->RequestUpdate();
    }
    if (m_showFine != showFineOld) {
        UpdateSamplingDistributionView();
    }
}

void Application::StatusBar() {
    static std::vector<const char *> stateNames = {
        "Initial    ",
        "Rendering..",
        "Wave End   ",
        "Completed! ",
    };
    ImGuiIO &io = ImGui::GetIO();
    std::string mouseInfo;
#if 0
    if (ImGui::IsMousePosValid())
        mouseInfo = StringPrintf("(%d, %d)", (int) io.MousePos.x, (int) io.MousePos.y);
#else
    if (m_viewport->IsHovered())
        mouseInfo = StringPrintf("(%d, %d)", m_viewport->GetMousePixel().x, m_viewport->GetMousePixel().y);
#endif
    else
        mouseInfo = "<invalid>";
    if (ImGui::GetColumnWidth() > 850)
        ImGui::Text("%s | Wave Render / Training Time: %.1f / %.1f ms | Training Samples: %s | Regions: %s | Mouse: %s",
            stateNames[m_renderThread->GetState()],
            m_waveStats.renderMS, m_waveStats.postprocessMS,
            FormatInteger(m_waveStats.trainingSamples).c_str(), FormatInteger(m_waveStats.numRegions).c_str(),
            mouseInfo.c_str());
    else {
        ImGui::Text("%s | Wave Render / Training Time: %.1f / %.1f ms",
            stateNames[m_renderThread->GetState()],
            m_waveStats.renderMS, m_waveStats.postprocessMS);
        ImGui::Text("Training Samples: %s | Regions: %s | Mouse: %s",
            FormatInteger(m_waveStats.trainingSamples).c_str(), FormatInteger(m_waveStats.numRegions).c_str(),
            mouseInfo.c_str());
    }
}

void Application::IntegratorSettings() {
    ImGui::PushID("Integrator Panel");
    // ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    ImGui::BeginDisabled(m_renderThread->GetState() == RenderThread::Rendering);
    if (ImGui::CollapsingHeader("Integrator Settings")) {
        ImGui::InputInt("Max Depth", &m_integratorSettings.maxDepth);
        m_integratorSettings.maxDepth = std::max(0, std::min(m_integratorSettings.maxDepth, m_maxMaxDepth));
        ImGui::InputInt("Min RR Depth", &m_integratorSettings.minRRDepth);
        m_integratorSettings.minRRDepth = std::max(0, m_integratorSettings.minRRDepth);
        ImGui::Checkbox("Use NEE", &m_integratorSettings.useNEE);
        if (m_samplerPrototype.Is<IndependentSampler>()) {
            auto *sampler = m_samplerPrototype.Cast<IndependentSampler>();
            int spp = m_spp;
            if (ImGui::InputInt("SPP", &spp, 10, 100)) {
                // Ctrl + click "+" => double SPP
                if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) {
                    if (spp > m_spp) spp = 2 * m_spp;
                    else spp = m_spp >> 1;
                }
                m_spp = std::max(1, spp);
                sampler->SetSamplesPerPixel(m_spp);
                m_samplers.ForAll([&](Sampler s) {
                    s.Cast<IndependentSampler>()->SetSamplesPerPixel(m_spp);
                });
            }
            if (ImGui::InputInt("Seed", &m_seed)) {
                sampler->SetSeed(m_seed);
                m_samplers.ForAll([&](Sampler s) {
                    s.Cast<IndependentSampler>()->SetSeed(m_seed);
                });
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

void Application::GuideSettings() {
    static const std::vector guidingTypes = {"MIS", "RIS"};
    ImGui::PushID("Guide Panel");
    // ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    ImGui::BeginDisabled(m_renderThread->GetState() == RenderThread::Rendering);
    if (ImGui::CollapsingHeader("Guide Settings")) {
        ImGui::Checkbox("Enable Guiding", &m_guideSettings.enableGuiding);
        ImGui::Checkbox("KNN Lookup", &m_guideSettings.knnLookup);
        ImGui::Checkbox("Enable Training", &m_guideSettings.enableTraining);
        ImGui::Checkbox("Evaluate Only", &m_guideSettings.evaluateOnly);
        ImGui::InputInt("Training Waves", &m_guideSettings.guideNumTrainingWaves);
        ImGui::Combo("Guiding Type", reinterpret_cast<int *>(&m_guideSettings.surfaceGuidingType), guidingTypes.data(), guidingTypes.size());
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

void Application::SpatialSubdivisionSettings() {
    ImGui::PushID("Spatial Subdivision Panel");
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    ImGui::BeginDisabled(m_renderThread->GetState() == RenderThread::Rendering);
    const float inputWidth = std::max(80.0f, ImGui::GetColumnWidth() * 0.5f);
    auto _ = [inputWidth] { ImGui::SetNextItemWidth(inputWidth); };
    if (ImGui::CollapsingHeader("Spatial Subdivision")) {
        std::lock_guard lock(m_mtx.subdivCfg);
        int maxDepth = (int) m_subdivCfg.maxDepth;
        int maxDepthWithSampleCount = (int) m_subdivCfg.maxDepthWithSampleCount;
        int sampleCountThreshold = (int) m_subdivCfg.sampleCountThreshold;
        int minSamplesCandidateSplit = (int) m_subdivCfg.minSamplesCandidateSplit;
        int minSamplesPromotion = (int) m_subdivCfg.minSamplesPromotion;
        _(), ImGui::InputInt("Max Depth", &maxDepth, 1, 10);
        _(), ImGui::InputInt("Max Depth with Sample Count", &maxDepthWithSampleCount, 1, 10);
        _(), ImGui::InputInt("Samples Count Threshold", &sampleCountThreshold, 0, 0);
        _(), ImGui::InputInt("Min Samples Candidate Split", &minSamplesCandidateSplit, 0, 0);
        _(), ImGui::InputInt("Min Samples Promotion", &minSamplesPromotion, 0, 0);
        m_subdivCfg.maxDepth = std::max(1, std::min(32, maxDepth));
        m_subdivCfg.maxDepthWithSampleCount = std::max(1, std::min(32, maxDepthWithSampleCount));
        m_subdivCfg.sampleCountThreshold = std::max(0, sampleCountThreshold);
        m_subdivCfg.minSamplesCandidateSplit = std::max(0, minSamplesCandidateSplit);
        m_subdivCfg.minSamplesPromotion = std::max(0, minSamplesPromotion);
        ImGui::Checkbox("Enable Promotion", &m_subdivCfg.enablePromotion);
        ImGui::Checkbox("Multiply Cosine", &m_subdivCfg.multiplyCosine);
        ImGui::Combo("Contrib Type", reinterpret_cast<int *>(&m_subdivCfg.contribType), "Determ\0Jitter\0Splat\0Reproject\0Splat+Reproject\0");
        int lookaheadDepth = (int) m_subdivCfg.lookaheadDepth;
        _(), ImGui::InputInt("Lookahead Depth", &lookaheadDepth, 1, 3);
        m_subdivCfg.lookaheadDepth = std::max(1, std::min(10, lookaheadDepth));
        _(), ImGui::InputFloat("Energy Threshold", &m_subdivCfg.signatureDistanceThreshold);
        // _(), ImGui::InputFloat("CE Clamp Value", &m_subdivCfg.ceClampValue, 0, 0, "%.3e");
        // _(), ImGui::SliderFloat("CE Decay", &m_subdivCfg.ceDecay, 0.0f, 1.0f);
        _(), ImGui::SliderFloat("VMM Decay", &m_subdivCfg.vmmDecay, 0.0f, 1.0f);
        _(), ImGui::SliderFloat("Signature Decay", &m_subdivCfg.signatureDecay, 0.0f, 1.0f);
        _();
        int octahedralRes = (int) pglGetOctahedralResolution();
        if (ImGui::SliderInt("Octahedral Resolution", &octahedralRes, 1, 1024, "%d", ImGuiSliderFlags_Logarithmic)) {
            pglSetOctahedralResolution(octahedralRes);
            if (m_enableRadianceView && m_radianceView->HasStarted()) {
                m_radianceView->UpdateBinIndexBuffer();
            }
        }
        _();
        int signatureSize = (int) pglGetSignatureSize();
        if (ImGui::SliderInt("Signature Size", &signatureSize, 1, PGL_SIGNATURE_MAX_SIZE)) {
            pglSetSignatureSize(signatureSize);
            m_signatureView->Rescale();
            if (m_enableRadianceView && m_radianceView->HasStarted()) {
                m_radianceView->UpdateBinIndexBuffer();
            }
        }
        _(), ImGui::DragFloat("Std Multiplier", &m_subdivCfg.stdMultiplier, 0.2f, 0, 10);
        if (ImGui::Button("Clear CE Statistics")) {
            std::lock_guard lock_(m_mtx.field);
            m_field.ClearCEStatistics();
        }
        if (ImGui::Button("Clear Signatures")) {
            std::lock_guard lock_(m_mtx.field);
            m_field.ClearSignatures();
        }
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    // Field will update from subdivCfg in the render thread
}

void Application::CacheMonitorViews() {
    if (ImGui::Begin("CE Curve"))
        m_cacheMonitor.ce->Draw();
    ImGui::End();

    if (ImGui::Begin("Energy Curve"))
        m_cacheMonitor.energy->Draw();
    ImGui::End();

    if (ImGui::Begin("Fluence Curve"))
        m_cacheMonitor.fluence->Draw();
    ImGui::End();

    if (ImGui::Begin("Depth Curve"))
        m_cacheMonitor.depth->Draw();
    ImGui::End();

    if (ImGui::Begin("Samples Curve"))
        m_cacheMonitor.samples->Draw();
    ImGui::End();
}

void Application::CacheHistogramViews() {
    if (m_layout == Layout_Histograms) {
        // 2x2 Table
        if (ImGui::Begin("Histograms")) {
            ImGuiTableFlags flags = ImGuiTableFlags_None;
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImVec2 padding = ImGui::GetStyle().CellPadding;
            avail.x -= padding.x, avail.y -= padding.y * 4;
            if (ImGui::BeginTable("##Histograms", 2, flags)) {
                for (auto *hist : {m_cacheHistogram.fluence, m_cacheHistogram.energy, m_cacheHistogram.depth, m_cacheHistogram.samples}) {
                    ImGui::TableNextColumn();
                    ImGui::BeginChild(hist->title.c_str(), ImVec2(avail.x * 0.5f, avail.y * 0.5f));
                    hist->enableTitle = true;
                    hist->Draw();
                    ImGui::EndChild();
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
    else {
        for (auto *hist : {m_cacheHistogram.fluence, m_cacheHistogram.energy, m_cacheHistogram.depth, m_cacheHistogram.samples}) {
            if (ImGui::Begin(hist->title.c_str())) {
                hist->enableTitle = false;
                hist->Draw();
            }
            ImGui::End();
        }
    }
}

void Application::PlotsView() {
    if (ImGui::Begin("Regions Plot"))
        m_plots.regions->Draw();
    ImGui::End();

    if (ImGui::Begin("Error Plot"))
        m_plots.error->Draw();
    ImGui::End();

    if (ImGui::Begin("Rendering Time Plot"))
        m_plots.renderingTime->Draw();
    ImGui::End();

    if (ImGui::Begin("Training Time Plot"))
        m_plots.trainingTime->Draw();
    ImGui::End();
}

}
