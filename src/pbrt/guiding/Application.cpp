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
#include "guiding.h"

#include <iostream>
#include <imgui_internal.h>
#include <implot.h>
#include <implot_internal.h>

static std::vector<const char *> channelNames = {
    "Radiance (1)",
    "Cache ID (2)",
    "Fluence (3)",
    "CE (4)",
    "Samples (5)",
    "Zero Samples (6)",
    "Depth (7)",
};

namespace pbrt {

Application::Application(Camera camera, Primitive scene, openpgl::cpp::Field *field, openpgl::cpp::SampleStorage &sampleStorage, const PGLKDTreeArguments &args, int spp,
                         GuidedPathIntegrator::IntegratorSettings &integratorSettings, GuidedPathIntegrator::GuidingSettings &guideSettings,
                         const std::function<void(int waveStart)> &renderWave,
                         const std::function<void(int waveEnd)> &updateCache,
                         const std::function<void(int waveEnd)> &saveImage)
    : m_camera(camera), m_film(camera.GetFilm()), m_isMultiChannel(m_film.Is<GuidedGBufferFilm>()),
      m_scene(scene), m_field(*field), m_sampleStorage(sampleStorage), m_subdivCfg(args), m_spp(spp),
      m_integratorSettings(integratorSettings), m_guideSettings(guideSettings),
      m_renderWave(renderWave), m_updateCache(updateCache), m_saveImage(saveImage),
      m_resolution(m_film.PixelBounds().Diagonal()) {
    std::cout << "Subdivision Config: \n"
        << "  KNN Lookup: " << m_subdivCfg.knnLookup << "\n"
        << "  ISNN Lookup: " << m_subdivCfg.isKnnLookup << "\n"
        << "  MaxSamples: " << m_subdivCfg.maxSamples << "\n"
        << "  MinSamples: " << m_subdivCfg.minSamples << "\n"
        << "  MaxDepth: " << m_subdivCfg.maxDepth << "\n"
        << "  CE Threshold: " << m_subdivCfg.ceThreshold << std::endl;

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
    m_viewport = std::make_unique<Viewport>(this, m_film);
    m_colormapPanel = std::make_unique<ColormapPanel>(this, m_film);

    m_cacheMonitor = std::make_unique<CacheMonitor>(this);
    auto &ceCurve = m_cacheMonitor->AddPlot("CE vs. Iter", CacheMonitor::PlotType_CE, true);
    auto &depthCurve = m_cacheMonitor->AddPlot("Depth vs. Iter", CacheMonitor::PlotType_Depth, false);
    auto &samplesCurve = m_cacheMonitor->AddPlot("Samples vs. Iter", CacheMonitor::PlotType_Samples, false);

    m_cacheHistogram.object = std::make_unique<CacheHistogram>(this);
    m_cacheHistogram.fluence = &m_cacheHistogram.object->AddPlot("Fluence Histogram", CacheHistogram::PlotType_Fluence, true);
    m_cacheHistogram.ce = &m_cacheHistogram.object->AddPlot("CE Histogram", CacheHistogram::PlotType_CE, false);
    m_cacheHistogram.depth = &m_cacheHistogram.object->AddPlot("Depth Histogram", CacheHistogram::PlotType_Depth, false);
    m_cacheHistogram.samples = &m_cacheHistogram.object->AddPlot("Samples Histogram", CacheHistogram::PlotType_Samples, false);

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
        UpdateRayCastingAtMouse();
        MainMenu();

        // Controls, Colormap, and RayCasting
        {
            ImGui::Begin("Controls");
            m_controlPanel->Draw();
            int wave = GetCurrentWave();
            ImGui::ProgressBar((float) wave / (float) m_spp, {ImGui::GetColumnWidth(), 0}, wave == m_spp ? "Done" : StringPrintf("%d/%d SPP", wave, m_spp).c_str());
            m_colormapPanel->Draw();
            RayCastingPanel();
            ImGui::End();
        }

        // Viewport, Channels, and Status Bar
        if (ImGui::Begin("Viewport")) {
            ChannelSelector();
            m_viewport->Draw();
            ProbesInteraction();
            ImGui::Separator();
            StatusBar();
        }
        ImGui::End();

        // Settings
        if (ImGui::Begin("Settings")) {
            IntegratorPanel();
            GuidePanel();
            SpatialSubdivisionPanel();
        }
        ImGui::End();

        {   // Cache Monitor
            if (ImGui::Begin("CE Curve"))
                ceCurve.Draw();
            ImGui::End();

            if (ImGui::Begin("Depth Curve"))
                depthCurve.Draw();
            ImGui::End();

            if (ImGui::Begin("Samples Curve"))
                samplesCurve.Draw();
            ImGui::End();
        }

        CacheHistogramPanel();

        // ImGui::ShowDemoWindow();
        // ImPlot::ShowDemoWindow();
        RenderImGuiFrame(m_window);
    }

    m_renderThread->SendCommand(RenderThread::Terminate);
    m_renderThread->Join();

    if (int waveEnd = GetCurrentWave(); waveEnd > 0)
        UpdateField(waveEnd);

    DestroyImGui(m_window);
    return 0;
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
        case Layout_CacheMonitor:
            SetupLayoutCacheMonitor();
            break;
        case Layout_Compact:
            SetupLayoutCompact();
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

        ImGuiID leftDock, leftTopDock, leftBottomDock, midDock, rightTopDock, rightBottomDock;
        ImGui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Left, 0.5f, &leftDock, &rightTopDock);
        ImGui::DockBuilderSplitNode(leftDock, ImGuiDir_Up, 0.5f, &leftTopDock, &leftBottomDock);
        ImGui::DockBuilderSplitNode(rightTopDock, ImGuiDir_Left, 0.5f, &midDock, &rightTopDock);
        ImGui::DockBuilderSplitNode(rightTopDock, ImGuiDir_Up, 0.3f, &rightTopDock, &rightBottomDock);
        ImGui::DockBuilderSetNodeSize(leftDock, ImVec2(239, -1));
        float padding = ImGui::GetStyle().WindowPadding.x;
        ImGui::DockBuilderSetNodeSize(midDock, ImVec2(std::min(m_resolution.x + 2 * padding, m_windowSize.x - 239 - 350), -1));
        // ImGui::DockBuilderSetNodeSize(rightBottomDock, ImVec2(-1, 600));

        ImGui::DockBuilderDockWindow("Controls", leftTopDock);
        ImGui::DockBuilderDockWindow("Fluence Histogram", leftBottomDock);
        ImGui::DockBuilderDockWindow("CE Histogram", leftBottomDock);
        ImGui::DockBuilderDockWindow("Depth Histogram", leftBottomDock);
        ImGui::DockBuilderDockWindow("Samples Histogram", leftBottomDock);
        ImGui::DockBuilderDockWindow("Viewport", midDock);
        ImGui::DockBuilderDockWindow("Settings", rightTopDock);
        ImGui::DockBuilderDockWindow("CE Curve", rightBottomDock);
        ImGui::DockBuilderDockWindow("Depth Curve", rightBottomDock);
        ImGui::DockBuilderDockWindow("Samples Curve", rightBottomDock);
        ImGui::DockBuilderFinish(dockSpaceID);

        m_hasSetupLayout = true;
    }

    ImGui::End();
}

void Application::SetupLayoutCacheMonitor() {
    if (!m_hasSetupLayout) {
        // Figure out proper window size
        auto mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
        m_windowSize = ImVec2(std::min(m_resolution.x + 700, mode->width), std::min(m_resolution.y + 220, mode->height));
        glfwSetWindowSize(m_window, m_windowSize.x, m_windowSize.y);
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

        ImGuiID left, leftTopDock, leftBottomDock;
        ImGuiID midDock;
        ImGuiID right, rightTopDock, rightMidDock, rightBottomDock;
        ImGui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Left, 0.5f, &left, &right);
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.5f, &leftTopDock, &leftBottomDock);
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Left, 0.5f, &midDock, &right);
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.5f, &rightTopDock, &rightBottomDock);
        ImGui::DockBuilderSplitNode(rightBottomDock, ImGuiDir_Up, 0.5f, &rightMidDock, &rightBottomDock);
        ImGui::DockBuilderSetNodeSize(left, ImVec2(239, -1));
        float padding = ImGui::GetStyle().WindowPadding.x;
        ImGui::DockBuilderSetNodeSize(midDock, ImVec2(std::min(m_resolution.x + 2 * padding, m_windowSize.x - 239 - 350), -1));
        ImGui::DockBuilderSetNodeSize(rightTopDock, ImVec2(-1, m_windowSize.y * 0.25));
        ImGui::DockBuilderSetNodeSize(rightMidDock, ImVec2(-1, m_windowSize.y * 0.25));
        ImGui::DockBuilderSetNodeSize(rightBottomDock, ImVec2(-1, m_windowSize.y * 0.5));

        ImGui::DockBuilderDockWindow("Controls", leftTopDock);
        ImGui::DockBuilderDockWindow("Fluence Histogram", leftBottomDock);
        ImGui::DockBuilderDockWindow("CE Histogram", leftBottomDock);
        ImGui::DockBuilderDockWindow("Depth Histogram", leftBottomDock);
        ImGui::DockBuilderDockWindow("Samples Histogram", leftBottomDock);
        ImGui::DockBuilderDockWindow("Settings", leftBottomDock);
        ImGui::DockBuilderDockWindow("Viewport", midDock);
        ImGui::DockBuilderDockWindow("CE Curve", rightBottomDock);
        ImGui::DockBuilderDockWindow("Depth Curve", rightTopDock);
        ImGui::DockBuilderDockWindow("Samples Curve", rightMidDock);
        ImGui::DockBuilderFinish(dockSpaceID);

        m_hasSetupLayout = true;
    }

    ImGui::End();
}

void Application::SetupLayoutCompact() {
    if (!m_hasSetupLayout) {
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
        ImGui::DockBuilderSplitNode(rightDock, ImGuiDir_Up, 0.4f, &rightTopDock, &rightBottomDock);
        float padding = ImGui::GetStyle().WindowPadding.x;
        ImGui::DockBuilderSetNodeSize(leftDock, ImVec2(std::min(m_resolution.x + 2 * padding, m_windowSize.x - 400), -1));

        ImGui::DockBuilderDockWindow("Viewport", leftDock);
        ImGui::DockBuilderDockWindow("Controls", rightTopDock);
        ImGui::DockBuilderDockWindow("Settings", rightTopDock);
        ImGui::DockBuilderDockWindow("CE Curve", rightBottomDock);
        ImGui::DockBuilderDockWindow("Depth Curve", rightBottomDock);
        ImGui::DockBuilderDockWindow("Samples Curve", rightBottomDock);
        ImGui::DockBuilderDockWindow("Fluence Histogram", rightBottomDock);
        ImGui::DockBuilderDockWindow("CE Histogram", rightBottomDock);
        ImGui::DockBuilderDockWindow("Depth Histogram", rightBottomDock);
        ImGui::DockBuilderDockWindow("Samples Histogram", rightBottomDock);
        ImGui::DockBuilderFinish(dockSpaceID);

        m_hasSetupLayout = true;
    }

    ImGui::End();
}

void Application::SetupLayoutHistograms() {
    if (!m_hasSetupLayout) {
        // Figure out proper window size
        auto mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
        m_windowSize = ImVec2(std::min(m_resolution.x + 900, mode->width), std::min(m_resolution.y + 120, mode->height));
        glfwSetWindowSize(m_window, m_windowSize.x, m_windowSize.y);
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

        ImGuiID left, leftTopDock, leftBottomDock;
        ImGuiID midDock;
        ImGuiID right;
        ImGui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Left, 0.5f, &left, &right);
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.5f, &leftTopDock, &leftBottomDock);
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Left, 0.5f, &midDock, &right);
        ImGui::DockBuilderSetNodeSize(left, ImVec2(239, -1));
        float padding = ImGui::GetStyle().WindowPadding.x;
        ImGui::DockBuilderSetNodeSize(midDock, ImVec2(std::min(m_resolution.x + 2 * padding, m_windowSize.x - 239 - 500), -1));

        ImGui::DockBuilderDockWindow("Controls", leftTopDock);
        ImGui::DockBuilderDockWindow("CE Curve", leftBottomDock);
        ImGui::DockBuilderDockWindow("Depth Curve", leftBottomDock);
        ImGui::DockBuilderDockWindow("Samples Curve", leftBottomDock);
        ImGui::DockBuilderDockWindow("Settings", leftBottomDock);
        ImGui::DockBuilderDockWindow("Viewport", midDock);
        ImGui::DockBuilderDockWindow("Histograms", right);
        ImGui::DockBuilderFinish(dockSpaceID);

        m_hasSetupLayout = true;
    }

    ImGui::End();
}

void Application::SetupRenderThread() {
    if (m_renderThread)
        ErrorExit("RenderThread already initialized");
    m_renderThread = std::make_unique<RenderThread>(
        m_spp,
        [&](int waveStart) {
            UpdateField(waveStart);
            AppendToProbeData();
            UpdateCacheHistogram();
            RenderWave(waveStart);
            UpdateCPUBufferFromFilm();
        }, m_saveImage);
    m_renderThread->SetCmdCallback(RenderThread::Restart, [&] {
        ClearFilm();
        UpdateCPUBufferFromFilm();
        {
            std::lock_guard lock(m_mtx.field);
            m_field.Reset();
        }
        m_cacheMonitor->Clear();
        m_cacheHistogram.object->Clear();
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
    static openpgl::cpp::SurfaceSamplingDistribution ssd(&m_field);
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
                    rc.normal = sit->intr.n;
                    rc.uv = sit->intr.uv;
                    // Query the guiding cache
                    GuidedBSDF gbsdf(&sampler, &m_field, &ssd, true, EGuideMIS);
                    float rnd = -1.0f;
                    std::lock_guard lock(m_mtx.field);  // avoid race condition when the field is updated
                    if (gbsdf.init(&bsdf, ray, sit, rnd)) {
                        // Guiding region available
                        uint32_t id = gbsdf.getId();
                        rc.cache = m_field.GetRegionStatisticsSurface(id);
                    }
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
    auto &sd = m_colormapPanel->shaderData[c];
    float clipValue = std::numeric_limits<float>::infinity();
    if (m_colormapPanel->isHovered) {
        clipValue = m_colormapPanel->hoveringValue;
    } else if (c == Channel_Fluence && m_cacheHistogram.fluence->isHovered) {
        clipValue = m_cacheHistogram.fluence->hoveringValue;
    } else if (c == Channel_CE && m_cacheHistogram.ce->isHovered) {
        clipValue = m_cacheHistogram.ce->hoveringValue;
    } else if (c == Channel_Depth && m_cacheHistogram.depth->isHovered) {
        clipValue = m_cacheHistogram.depth->hoveringValue;
    } else if (c == Channel_Samples && m_cacheHistogram.samples->isHovered) {
        clipValue = m_cacheHistogram.samples->hoveringValue;
    }
    m_viewport->UpdateFramebuffer(c, {sd.scale, sd.offset, clipValue, sd.tonemapped ? cmap_tex_ids[m_colormapPanel->selectedCMap] : 0});
}

void Application::CacheInfo(const PGLRegionStatistics &cache) {
    ImGui::Text("Cache ID: %u", cache.id);
    ImGui::Text("Fluence: %f", cache.fluence);
    ImGui::Text("CE: %f", cache.crossEntropy);
    ImGui::Text("Nonzero/Zero Samples: %s/%s", FormatInteger(cache.numSamples).c_str(), FormatInteger(cache.numZeroValueSamples).c_str());
    ImGui::Text("Depth: %d", (int) cache.depth);
}

void Application::UpdateRayCastingAtMouse() {
    if (m_enableRayCastingAtMouse) {
        if (m_viewport->IsHovered()) {
            m_rcMouse = RayCast(m_viewport->GetMousePixel());
        } else {
            m_rcMouse.valid = false;
            m_rcMouse.cache.id = -1;
        }
        if (m_rcMouse.cache.id != -1) {
            // Tooltip next to the mouse
            if (ImGui::BeginTooltip()) {
                CacheInfo(m_rcMouse.cache);
                ImGui::EndTooltip();
            }
        }
    }
}

void Application::ProbesInteraction() {
    // Draw all probes
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 leftTop = m_viewport->GetLeftTop();
    float scale = m_viewport->GetScale();
    m_cacheMonitor->ForEachProbe([&](CacheMonitor::Probe &probe) {
        std::string label = StringPrintf("#%d", probe.idx);
        bool isHovered = m_viewport->IsHovered() && Distance(m_viewport->GetMousePixel(), probe.pixel) < m_cacheMonitor->GetProbeRadius();
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
        if (isHovered || m_cacheMonitor->DisplayProbeID()) {
            ImVec2 pos(center.x - 8, center.y - 18);
            draw_list->AddText(pos, border_col, label.c_str());
        }
    });

    // Left click to add/activate a probe
    if (m_viewport->IsHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Point2i pixel = m_viewport->GetMousePixel();
        m_cacheMonitor->AddProbe(pixel);
        if (m_renderThread->GetState() != RenderThread::Rendering) {  // Add a probe with the current CE
            m_cacheMonitor->UpdateProbe(pixel, [&](auto &probe) {
                float iter = GetCurrentWave();
                if (probe.data.empty() || probe.data.back().iter < iter) {
                    auto rc = RayCast(pixel);
                    probe.data.push_back({iter, rc.cache.id != -1 ? rc.cache.crossEntropy : std::numeric_limits<float>::quiet_NaN()});
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
    m_waveStats.numRegions = m_field.GetRegionCountSurface();
}

void Application::RenderWave(int waveStart) {
    CheckIsRenderThread();
    Timer timer;
    m_renderWave(waveStart);
    m_waveStats.renderMS = timer.ElapsedSeconds() * 1e3;
    m_waveStats.trainingSamples = m_sampleStorage.GetSizeSurface() + m_sampleStorage.GetSizeVolume();
}

void Application::ClearFilm() {
    CheckIsRenderThread();
    ParallelFor2D(m_film.PixelBounds(), [&](Point2i p) {
        m_film.ResetPixel(p);
    });
}

void Application::UpdateCPUBufferFromFilm() {
    CheckIsRenderThread();
    Timer timer;
    m_viewport->UpdateCPUBufferFromFilm();
    std::cout << "Update CPU buffer: " << timer.ElapsedSeconds() * 1e3 << " ms" << std::endl;
}

void Application::AppendToProbeData() {
    CheckIsRenderThread();
    float x = (float) GetCurrentWave();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    m_cacheMonitor->ForEachProbe([&](CacheMonitor::Probe &probe) {
        if (probe.active) {
            auto rc = RayCast(probe.pixel);
            bool cacheValid = rc.cache.id != -1;
            probe.data.push_back({x,
                cacheValid ? rc.cache.crossEntropy : nan,
                cacheValid ? (float) rc.cache.depth : nan,
                cacheValid ? (float) rc.cache.numSamples : nan
            });
        }
    });
    m_cacheMonitor->RequestFitAxes();
}

void Application::UpdateCacheHistogram() {
    CheckIsRenderThread();
    if (m_enableHistogram) {
        Timer timer;
        m_cacheHistogram.object->Update([&](CacheHistogram::Data &data) {
            size_t numRegions = m_field.GetRegionCountSurface();
            data.fluence.resize(numRegions);
            data.ce.resize(numRegions);
            data.depth.resize(numRegions);
            data.samples.resize(numRegions);
            for (size_t i = 0; i < numRegions; ++i) {
                auto cache = m_field.GetRegionStatisticsSurface(i);
                data.fluence[i] = cache.fluence;
                data.ce[i] = cache.crossEntropy;
                data.depth[i] = cache.depth;
                data.samples[i] = cache.numSamples;
            }
        });
        m_cacheHistogram.object->RequestFitAxes();
        std::cout << "Update Cache Histogram: " << timer.ElapsedSeconds() * 1e3 << " ms" << std::endl;
    }
}

void Application::MainMenu() {
    static std::string layoutNames[Layout_Count] = {"Default", "Cache Monitor", "Compact", "Histograms"};
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
        ImGui::EndMainMenuBar();
    }
}

void Application::RayCastingPanel() {
    if (ImGui::IsKeyPressed(ImGuiKey_C, false))
        m_enableRayCastingAtMouse ^= true;
    ImGui::SetNextItemOpen(m_enableRayCastingAtMouse);
    if ((m_enableRayCastingAtMouse = ImGui::CollapsingHeader("Ray Casting"))) {
        if (m_rcMouse.valid) {
            auto radiance = m_film.GetPixelRGB(m_rcMouse.pixel);
            ImGui::Text("Radiance: (%.2f, %.2f, %.2f)", radiance.r, radiance.g, radiance.b);
            ImGui::Text("Hit: (%.2f, %.2f, %.2f)", m_rcMouse.hit.x, m_rcMouse.hit.y, m_rcMouse.hit.z);
            ImGui::Text("Normal: (%.2f, %.2f, %.2f)", m_rcMouse.normal.x, m_rcMouse.normal.y, m_rcMouse.normal.z);
            ImGui::Text("UV: (%.2f, %.2f)", m_rcMouse.uv.x, m_rcMouse.uv.y);
            if (m_rcMouse.cache.id == -1)
                ImGui::Text("Cache ID: <invalid>");
            else {
                CacheInfo(m_rcMouse.cache);
            }
        } else {
            ImGui::Text("No intersection");
        }
    }
}

void Application::ChannelSelector() {
    // Add a channel selection bar for GuidedGBufferFilm
    if (m_isMultiChannel) {
        SelectedChannel newChannel = m_selectedChannel;
        for (int i = 0; i < Channel_Count; ++i) {
            if (ImGui::IsKeyPressed((ImGuiKey) (ImGuiKey_1 + i), false))
                newChannel = (SelectedChannel) i;
        }

        if (ImGui::BeginTabBar("ChannelSelector")) {
            for (int i = 0; i < Channel_Count; ++i) {
                if (newChannel == i)
                    ImGui::PushStyleColor(ImGuiCol_Tab, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
                if (ImGui::TabItemButton(channelNames[i])) {
                    newChannel = (SelectedChannel) i;
                }
                if (newChannel == i)
                    ImGui::PopStyleColor();
            }
            ImGui::EndTabBar();
        }

        SetSelectedChannel(newChannel);
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

void Application::IntegratorPanel() {
    ImGui::PushID("Integrator Panel");
    // ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    ImGui::BeginDisabled(m_renderThread->GetState() == RenderThread::Rendering);
    if (ImGui::CollapsingHeader("Integrator Settings")) {
        ImGui::InputInt("Max Depth", &m_integratorSettings.maxDepth);
        m_integratorSettings.maxDepth = std::max(0, std::min(m_integratorSettings.maxDepth, m_maxMaxDepth));
        ImGui::InputInt("Min RR Depth", &m_integratorSettings.minRRDepth);
        m_integratorSettings.minRRDepth = std::max(0, m_integratorSettings.minRRDepth);
        ImGui::Checkbox("Use NEE", &m_integratorSettings.useNEE);
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

void Application::GuidePanel() {
    static const std::vector guidingTypes = {"MIS", "RIS"};
    ImGui::PushID("Guide Panel");
    // ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    ImGui::BeginDisabled(m_renderThread->GetState() == RenderThread::Rendering);
    if (ImGui::CollapsingHeader("Guide Settings")) {
        ImGui::Checkbox("Enable Guiding", &m_guideSettings.enableGuiding);
        ImGui::Combo("Guiding Type", reinterpret_cast<int *>(&m_guideSettings.surfaceGuidingType), guidingTypes.data(), guidingTypes.size());
        ImGui::Checkbox("KNN Lookup", &m_guideSettings.knnLookup);
        ImGui::InputInt("Training Waves", &m_guideSettings.guideNumTrainingWaves);
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

void Application::SpatialSubdivisionPanel() {
    ImGui::PushID("Spatial Subdivision Panel");
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    ImGui::BeginDisabled(m_renderThread->GetState() == RenderThread::Rendering);
    if (ImGui::CollapsingHeader("Spatial Subdivision")) {
        std::lock_guard lock(m_mtx.subdivCfg);
        int maxDepth = (int) m_subdivCfg.maxDepth;
        int maxSamples = (int) m_subdivCfg.maxSamples;
        int minSamples = (int) m_subdivCfg.minSamples;
        ImGui::InputInt("Max Depth", &maxDepth);
        ImGui::InputInt("Max Samples", &maxSamples, 1000, 5000);
        ImGui::InputInt("Min Samples", &minSamples, 1000, 5000);
        m_subdivCfg.maxDepth = std::max(1, maxDepth);
        m_subdivCfg.maxSamples = std::max(0, maxSamples);
        m_subdivCfg.minSamples = std::max(0, minSamples);
        ImGui::InputFloat("CE Threshold", &m_subdivCfg.ceThreshold, 0.1f, 1.0f);
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    // Field will update from subdivCfg in the render thread
}

void Application::CacheHistogramPanel() {
    if (m_layout == Layout_Histograms) {
        // 2x2 Table
        if ((m_enableHistogram = ImGui::Begin("Histograms"))) {
            ImGuiTableFlags flags = ImGuiTableFlags_None;
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImVec2 padding = ImGui::GetStyle().CellPadding;
            avail.x -= padding.x, avail.y -= padding.y * 4;
            if (ImGui::BeginTable("##Histograms", 2, flags)) {
                for (auto *hist : {m_cacheHistogram.fluence, m_cacheHistogram.ce, m_cacheHistogram.depth, m_cacheHistogram.samples}) {
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
        bool enableHistogram = false;
        for (auto *hist : {m_cacheHistogram.fluence, m_cacheHistogram.ce, m_cacheHistogram.depth, m_cacheHistogram.samples}) {
            if (ImGui::Begin(hist->title.c_str())) {
                enableHistogram = true;
                hist->enableTitle = false;
                hist->Draw();
            }
            ImGui::End();
        }
        m_enableHistogram = enableHistogram; // Atomic update
    }
}

}
