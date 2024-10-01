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

static std::vector<const char *> channelNames = {
    "Radiance (1)",
    "Cache ID (2)",
    "Fluence (3)",
    "CE (4)",
};

namespace pbrt {

Application::Application(Camera camera, Primitive scene, openpgl::cpp::Field *field, const PGLKDTreeArguments &args,
                         int spp,
                         const std::function<void(int waveStart)> &renderWave,
                         const std::function<void(int waveEnd)> &updateCache,
                         const std::function<void(int waveEnd)> &saveImage)
    : m_camera(camera), m_film(camera.GetFilm()), m_isMultiChannel(m_film.Is<GuidedGBufferFilm>()),
      m_scene(scene), m_field(*field), m_subdivCfg(args), m_spp(spp), m_renderWave(renderWave),
      m_updateCache(updateCache), m_saveImage(saveImage), m_resolution(m_film.PixelBounds().Diagonal()) {
    std::cout << "Subdivision Config: \n"
        << "  KNN Lookup: " << m_subdivCfg.knnLookup << "\n"
        << "  IS KNN Lookup: " << m_subdivCfg.isKnnLookup << "\n"
        << "  MaxSamples: " << m_subdivCfg.maxSamples << "\n"
        << "  MinSamples: " << m_subdivCfg.minSamples << "\n"
        << "  MaxDepth: " << m_subdivCfg.maxDepth << "\n"
        << "  CE Threshold: " << m_subdivCfg.ceThreshold << std::endl;
}

Application::~Application() {
}

int Application::Run() {
    m_window = InitializeImGui("Test", m_windowSize.x, m_windowSize.y);
    if (m_window == nullptr) {
        std::cerr << "Failed to initialize ImGui" << std::endl;
        return 1;
    }

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    SetupRenderThread();
    InitializeTonemaps();
    m_controlPanel = std::make_unique<ControlPanel>(*m_renderThread);
    m_viewport = std::make_unique<Viewport>(m_film);
    m_colormapPanel = std::make_unique<ColormapPanel>(m_film);

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    // ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

    // Main GUI loop
    while (!glfwWindowShouldClose(m_window)) {
        if (InitializeFrame(m_window)) continue;

        {
            int display_w, display_h;
            glfwGetWindowSize(m_window, &display_w, &display_h);
            m_windowSize = ImVec2(display_w, display_h);
        }
        glViewport(0, 0, m_windowSize.x, m_windowSize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        SetupDockSpace();

        {
            SelectedChannel c = m_colormapPanel->selectedChannel;
            auto &sd = m_colormapPanel->shaderData[c];
            m_viewport->PossiblyUpdateFramebuffer(c, sd.scale, sd.offset, sd.tonemapped ? cmap_tex_ids[m_colormapPanel->selectedCMap] : 0);
        }

        // Left Pane
        {
            ImGui::Begin("Left Pane");
            m_controlPanel->Draw();
            int waveStart = m_renderThread->GetWaveStart();
            ImGui::ProgressBar((float) waveStart / (float) m_spp, {ImGui::GetColumnWidth(), 0}, waveStart == m_spp ? "Done" : StringPrintf("%d/%d SPP", waveStart, m_spp).c_str());
            ImGui::End();
        }

        // Middle Pane
        {
            ImGui::Begin("Middle Pane");
            ChannelSelector();
            m_viewport->Draw();

            // Status bar
            ImGui::Separator();
            ImGui::Text("Mouse Pos: (%.1f, %.1f)", io.MousePos.x, io.MousePos.y);
            ImGui::End();
        }

        // Right Pane
        {
            ImGui::Begin("Right Pane");
            m_colormapPanel->Draw();
            ImGui::End();
        }

        // ImGui::ShowDemoWindow();

        RenderImGuiFrame(m_window);
    }

    m_renderThread->SendCommand(RenderThread::Terminate);
    m_renderThread->Join();

    if (int waveEnd = m_renderThread->GetWaveStart(); waveEnd > 0)
        UpdateCache(waveEnd);

    DestroyImGui(m_window);
    return 0;
}

void Application::SetupDockSpace() {
    // ImGui::DockSpaceOverViewport();
    ImGuiViewport *iviewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(iviewport->WorkPos);
    ImGui::SetNextWindowSize(iviewport->WorkSize);
    ImGui::SetNextWindowViewport(iviewport->ID);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("MyDockSpace", nullptr, windowFlags);

    ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    dockFlags |= ImGuiDockNodeFlags_AutoHideTabBar;
    ImGuiID dockSpaceID = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockSpaceID, ImVec2(0.0f, 0.0f), dockFlags);
    // ImGui::DockSpaceOverViewport(dockSpaceID, ImGui::GetMainViewport(), dockFlags);

    if (!m_hasSetupDock) {
        ImGui::DockBuilderRemoveNode(dockSpaceID);
        ImGui::DockBuilderAddNode(dockSpaceID, dockFlags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceID, iviewport->Size);

        ImGuiID leftDock, midDock, rightDock;
        ImGui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Left, 0.15f, &leftDock, &rightDock);
        ImGui::DockBuilderSplitNode(rightDock, ImGuiDir_Left, 0.75f, &midDock, &rightDock);

        ImGui::DockBuilderDockWindow("Left Pane", leftDock);
        ImGui::DockBuilderDockWindow("Middle Pane", midDock);
        ImGui::DockBuilderDockWindow("Right Pane", rightDock);
        ImGui::DockBuilderDockWindow("Dear ImGui Demo", rightDock);
        ImGui::DockBuilderFinish(dockSpaceID);

        m_hasSetupDock = true;
    }

    ImGui::End();
}

void Application::SetupRenderThread() {
    if (m_renderThread)
        ErrorExit("RenderThread already initialized");
    m_renderThread = std::make_unique<RenderThread>(
        m_spp,
        [&](int waveStart) {
            UpdateCache(waveStart);
            // AppendToCECurves();
            RenderWave(waveStart);
            UpdateCPUBufferFromFilm();
        }, m_saveImage);
    m_renderThread->SetCmdCallback(RenderThread::Restart, [&] {
        ClearFilm();
        UpdateCPUBufferFromFilm();
        {
            std::lock_guard lock(m_mtxField);
            m_field.Reset();
        }
        // ResetCECurves();
        return true;
    });
}

void Application::CheckIsMainThread() {
    if (std::this_thread::get_id() != m_renderThread->GetMainThreadID())
        ErrorExit("This function should be called from the main thread");
}

void Application::CheckIsRenderThread() {
    if (std::this_thread::get_id() != m_renderThread->GetRenderThreadID())
        ErrorExit("This function should be called from the render thread");
}

void Application::UpdateCache(int waveEnd) {
    // CheckIsRenderThread();
    std::lock_guard lock(m_mtxField);
    {
        std::lock_guard lock_(m_mtxSubdivCfg);
        m_field.UpdateSubdivConfig(m_subdivCfg);
    }
    Timer timer;
    if (waveEnd > 0)
        m_updateCache(waveEnd);
    m_waveTimeStats.postprocessMS = timer.ElapsedSeconds() * 1e3;
}

void Application::RenderWave(int waveStart) {
    CheckIsRenderThread();
    Timer timer;
    m_renderWave(waveStart);
    m_waveTimeStats.renderMS = timer.ElapsedSeconds() * 1e3;
}

void Application::ClearFilm() {
    CheckIsRenderThread();
    ParallelFor2D(m_film.PixelBounds(), [&](Point2i p) {
        m_film.ResetPixel(p);
    });
}

void Application::UpdateCPUBufferFromFilm() {
    CheckIsRenderThread();
    m_viewport->UpdateCPUBufferFromFilm();
}

void Application::ChannelSelector() {
    // Add a channel selection bar for GuidedGBufferFilm
    if (m_isMultiChannel) {
        // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        SelectedChannel newChannel = m_colormapPanel->selectedChannel;
        if (ImGui::IsKeyPressed(ImGuiKey_1, false))
            newChannel = Channel_Radiance;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false))
            newChannel = Channel_CacheID;
        if (ImGui::IsKeyPressed(ImGuiKey_3, false))
            newChannel = Channel_Fluence;
        if (ImGui::IsKeyPressed(ImGuiKey_4, false))
            newChannel = Channel_CE;

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

        if (newChannel != m_colormapPanel->selectedChannel) {
            m_colormapPanel->selectedChannel = newChannel;
            m_viewport->RequestUpdate();
        }

        // ImGui::PopStyleVar();
    }
}

}
