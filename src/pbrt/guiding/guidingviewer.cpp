//
// Created by fengshi on 9/11/24.
//

// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "helper.h"

#include <pbrt/cameras.h>
#include <pbrt/samplers.h>
#include <pbrt/film.h>
#include <pbrt/interaction.h>
#include <pbrt/shapes.h>
#include <pbrt/scene.h>
#include "guidingviewer.h"

#include <implot_internal.h>

#include "guiding.h"


namespace pbrt {

const static std::vector<std::pair<const char *, const char *>> commandNames = {
    {"AutoPlay", "Automatically resume the rendering"},
    {"Pause", "Pause the rendering"},
    {"Forward", "Render the next wave of samples"},
    {"Save", "Save the current rendering"},
    {"Restart", "Restart rendering"},
    {"Terminate", "Terminate rendering"},
    {"None", "No command"},
};

static std::vector<const char *> stateNames = {
    "Initial    ",
    "Rendering..",
    "Wave End   ",
    "Completed! ",
};

static std::vector<const char *> channelNames = {
    "Radiance (1)",
    "Cache ID (2)",
    "Fluence (3)",
    "CE (4)",
};

static const char* cmap_names[pbrt::GuidingViewerGUI::CMap_Count] = {
    "Cividis",
    "Inferno",
    "Magma",
    "Plasma",
    "Viridis"
};

static std::string controlButtonTexPath = PBRT_ROOT_DIR "images/control_buttons.png";

static inline ImU32 CacheID2ColorU32(uint32_t id) {
    return IM_COL32(Hash(id, 0) % 255, Hash(id, 1) % 255, Hash(id, 2) % 255, 255);
}

static inline ImVec4 CacheID2Color(uint32_t id) {
    return {(float) (Hash(id, 0) % 255) / 255.0f, (float) (Hash(id, 1) % 255) / 255.0f, (float) (Hash(id, 2) % 255) / 255.0f, 1.0f};
}

GuidingViewerGUI::GuidingViewerGUI(Camera camera, Primitive scene, openpgl::cpp::Field* field, int spp,
                                   const std::function<void(int waveStart)> &renderWave,
                                   const std::function<void(int waveEnd)> &postprocessWave,
                                   const std::function<void(int waveEnd)> &saveImage)
    : camera(camera), film(camera.GetFilm()), isMultiChannel(film.Is<GuidedGBufferFilm>()),
      scene(scene), field(field), spp(spp), waveStart(0),
      renderWave(renderWave), postprocessWave(postprocessWave), saveImage(saveImage) {
    Bounds2i pixelBounds = film.PixelBounds();
    resolution = pixelBounds.Diagonal();
    if (isMultiChannel) {
        tabHeight = 24;
    }
    windowSize = {resolution.x + inspectorWidth, tabHeight + resolution.y + statusBarHeight};

    cpuFramebuffer.radiance = new RGB[resolution.x * resolution.y];
    for (int i = 0; i < resolution.x * resolution.y; ++i)
        cpuFramebuffer.radiance[i] = RGB(0.0f, 0.0f, 0.0f);
    cpuFramebuffer.cacheID = new RGB[resolution.x * resolution.y];
    for (int i = 0; i < resolution.x * resolution.y; ++i)
        cpuFramebuffer.cacheID[i] = RGB(0.0f, 0.0f, 0.0f);
    cpuFramebuffer.fluence = new float[resolution.x * resolution.y];
    for (int i = 0; i < resolution.x * resolution.y; ++i)
        cpuFramebuffer.fluence[i] = 0.0f;
    cpuFramebuffer.ce = new float[resolution.x * resolution.y];
    for (int i = 0; i < resolution.x * resolution.y; ++i)
        cpuFramebuffer.ce[i] = 0.0f;

    shaderData[Channel_Radiance].firstNormalized = true;
    shaderData[Channel_CacheID].firstNormalized = true;
}

GuidingViewerGUI::~GuidingViewerGUI() {
    delete[] cpuFramebuffer.radiance;
    delete[] cpuFramebuffer.cacheID;
    delete[] cpuFramebuffer.fluence;
    delete[] cpuFramebuffer.ce;
}

void GuidingViewerGUI::Launch() {
    // Initiate the render thread
    std::thread renderThread(&GuidingViewerGUI::RenderThread, this);

    auto window = InitializeImGui("Guiding Viewer", windowSize.x, windowSize.y);
    if (window == nullptr) {
        Error("Failed to create window");
        return;
    }

    if (!LoadTextureFromFile(controlButtonTexPath.c_str(), reinterpret_cast<GLuint&>(controlButtonTexID), controlButtonTexWidth, controlButtonTexHeight, true))
        Error("Failed to load control_texture.png from disk");

    glGenTextures(1, reinterpret_cast<GLuint*>(&renderingTexID));
    UpdateGPUFramebufferFromCPU();
    InitializeTonemappedImageContext();
    glEnable(GL_FRAMEBUFFER_SRGB);

    // ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

    // Main GUI loop
    while (!glfwWindowShouldClose(window)) {
        if (InitializeFrame(window)) continue;

        // GUI Render
        glViewport(0, 0, windowSize.x, windowSize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(10, 10));
        // ImGui::ShowDemoWindow();
        // ImPlot::ShowDemoWindow();
        Canvas();
        Tab();
        Inspector();
        StatusBar();
        ImGui::PopStyleVar();

        RenderImGuiFrame(window);
    }

    // Terminate renderer
    {
        std::lock_guard lock(mtxCommand);
        command = Terminate;
        cv.notify_one();
    }
    renderThread.join();
    assert(renderState == Completed);
    if (waveStart > 0)
        postprocessWave(waveStart);

    DestroyImGui(window);
}

void GuidingViewerGUI::UpdateCPUFramebufferFromFilm() {
    std::lock_guard lock(mtxCPUFramebuffer);
    if (isMultiChannel) {
        auto *gFilm = film.Cast<GuidedGBufferFilm>();
        // Update all channels
        ParallelFor2D(film.PixelBounds(), [&](Point2i p) {
            size_t index = (p.y - film.PixelBounds().pMin.y) * resolution.x + (p.x - film.PixelBounds().pMin.x);
            auto &pixel = gFilm->GetPixel(p);
            cpuFramebuffer.radiance[index] = gFilm->GetPixelRGB(p);
            if (pixel.guidingId != -1) {
                IndependentSampler sampler(3, pixel.guidingId * pixel.guidingId);
                sampler.StartPixelSample(Point2i(0, 0), 0, 0);
                cpuFramebuffer.cacheID[index] = RGB(sampler.Get1D(), sampler.Get1D(), sampler.Get1D());
            }
            cpuFramebuffer.fluence[index] = pixel.fluence;
            cpuFramebuffer.ce[index] = pixel.ce;
        });
    } else {
        ParallelFor2D(film.PixelBounds(), [&](Point2i p) {
            size_t index = (p.y - film.PixelBounds().pMin.y) * resolution.x + (p.x - film.PixelBounds().pMin.x);
            cpuFramebuffer.radiance[index] = film.GetPixelRGB(p);
        });
    }

    shouldUpdateGPUFramebuffer = true;
}

std::pair<float, float> GuidingViewerGUI::GetMinMaxFromFilm(SelectedChannel c) {
    std::lock_guard lock(mtxCPUFramebuffer);
    float minVal = std::numeric_limits<float>::infinity(), maxVal = -std::numeric_limits<float>::infinity();
    if (isMultiChannel) {
        auto *gFilm = film.Cast<GuidedGBufferFilm>();
        for (int y = film.PixelBounds().pMin.y; y < film.PixelBounds().pMax.y; ++y) {
            for (int x = film.PixelBounds().pMin.x; x < film.PixelBounds().pMax.x; ++x) {
                auto &pixel = gFilm->GetPixel(Point2i(x, y));
                float val;
                switch (c) {
                    case Channel_Radiance:
                        val = gFilm->GetPixelRGB(Point2i(x, y)).Average();
                        break;
                    case Channel_Fluence:
                        val = pixel.fluence;
                        break;
                    case Channel_CE:
                        val = pixel.ce;
                        break;
                    case Channel_Count:
                        break;
                    default:
                        Error("Unknown channel type %d", (int) c);
                }
                minVal = std::min(minVal, val);
                maxVal = std::max(maxVal, val);
            }
        }
    } else {
        for (int y = film.PixelBounds().pMin.y; y < film.PixelBounds().pMax.y; ++y) {
            for (int x = film.PixelBounds().pMin.x; x < film.PixelBounds().pMax.x; ++x) {
                float val = film.GetPixelRGB(Point2i(x, y)).Average();
                minVal = std::min(minVal, val);
                maxVal = std::max(maxVal, val);
            }
        }
    }
    return {minVal, maxVal};
}

// This has to be called in the GUI thread
void GuidingViewerGUI::UpdateGPUFramebufferFromCPU() {
    switch (selectedChannel) {
        case Channel_Radiance:
            UpdateTextureFromRGBData((GLuint) (uintptr_t) renderingTexID, cpuFramebuffer.radiance, resolution.x, resolution.y, false);
            break;
        case Channel_CacheID:
            UpdateTextureFromRGBData((GLuint) (uintptr_t) renderingTexID, cpuFramebuffer.cacheID, resolution.x, resolution.y, false);
            break;
        case Channel_Fluence:
            UpdateTextureFromFloatData((GLuint) (uintptr_t) renderingTexID, cpuFramebuffer.fluence, resolution.x, resolution.y, false);
            break;
        case Channel_CE:
            UpdateTextureFromFloatData((GLuint) (uintptr_t) renderingTexID, cpuFramebuffer.ce, resolution.x, resolution.y, false);
            break;
    }
}

void GuidingViewerGUI::Tab() {
    // Add a channel selection bar for GuidedGBufferFilm
    if (isMultiChannel) {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(resolution.x, tabHeight));
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::Begin("Tab", nullptr, flags);
        SelectedChannel newlySelectedChannel = selectedChannel;
        if (ImGui::IsKeyPressed(ImGuiKey_1, false))
            newlySelectedChannel = Channel_Radiance;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false))
            newlySelectedChannel = Channel_CacheID;
        if (ImGui::IsKeyPressed(ImGuiKey_3, false))
            newlySelectedChannel = Channel_Fluence;
        if (ImGui::IsKeyPressed(ImGuiKey_4, false))
            newlySelectedChannel = Channel_CE;

        if (ImGui::BeginTabBar("ChannelSelector")) {
            for (int i = 0; i < Channel_Count; ++i) {
                if (newlySelectedChannel == i)
                    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.4f, 0.45f, 0.6f, 1.0f));
                else
                    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.1f, 0.15f, 0.3f, 1.0f));
                if (ImGui::TabItemButton(channelNames[i])) {
                    newlySelectedChannel = (SelectedChannel) i;
                }
                ImGui::PopStyleColor();
            }
            ImGui::EndTabBar();
        }

        if (newlySelectedChannel != selectedChannel) {
            selectedChannel = newlySelectedChannel;
            shouldUpdateGPUFramebuffer = true;
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }
}

void GuidingViewerGUI::Canvas() {
    ImGuiIO &io = ImGui::GetIO();
    Point2f mousePos(io.MousePos.x, io.MousePos.y);
    // Possibly update the GPU framebuffer
    if (shouldUpdateGPUFramebuffer) {
        UpdateGPUFramebufferFromCPU();
        shouldUpdateGPUFramebuffer = false;
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowSize(ImVec2(resolution.x, resolution.y));
    ImGui::SetNextWindowPos(ImVec2(0, tabHeight));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("Canvas", nullptr, flags);

    auto &sd = shaderData[selectedChannel];
    DrawTonemappedImage((GLuint) (uintptr_t) renderingTexID, cmap_tex_ids[selectedCMap],
        ImVec2(0, tabHeight), ImVec2(resolution.x, resolution.y), ImVec2(windowSize.x, windowSize.y),
        sd.scale, sd.offset, selectedChannel > Channel_CacheID, sd.tonemapped);

    {   // Handle CE Probes Interaction
        std::lock_guard lock(mtxCECurves);
        if (enableRayCasting && ImGui::IsWindowHovered()) {  // Ray trace mouse position when hovering over the rendering
            UpdateRayCastingResult();
            if (rcData.cacheId != -1) {
                if (ImGui::BeginTooltip()) {
                    ImGui::Text("Cache ID: %u", rcData.cacheId);
                    ImGui::Text("Fluence: %f", rcData.fluence);
                    ImGui::Text("CE: %f", rcData.ce);
                    ImGui::EndTooltip();
                }
                if (cacheCurvesNodeOpened) {
                    // Left click to insert a probe
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        std::vector<PlotDataEntry> data;
                        if (renderState != Rendering)
                            data.emplace_back(waveStart, rcData.ce);
                        auto [it, success] = ceCurves.emplace(rcData.cacheId, PlotData{true, (int) ceCurves.size(), mousePos, std::move(data)});
                        if (!success) {
                            it->second.active = true;
                            it->second.mousePos = mousePos;  // Update the mouse position
                        }
                    }
                }
            }
        }
        if (cacheCurvesNodeOpened) {
            // Draw all the clicked positions
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            for (auto &[id, curve] : ceCurves) if (curve.active) {
                bool isHovered = Distance(mousePos, curve.mousePos) < 10;
                // Right click to remove a probe
                if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    curve.active = false;
                }
                ImVec2 center(curve.mousePos.x, curve.mousePos.y);
                // ImU32 col = CacheID2ColorU32(id);
                ImU32 col = ImPlot::GetColormapColorU32(curve.order, -1);
                ImU32 border_col = isHovered ? IM_COL32_WHITE : IM_COL32_BLACK;
                draw_list->AddCircleFilled(center, 3, col);
                draw_list->AddCircle(center, 4, border_col);
            }
        }
    }

    ImGui::End();
}

void GuidingViewerGUI::Inspector() {
    ImGuiIO &io = ImGui::GetIO();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    ImGui::SetNextWindowSize(ImVec2(inspectorWidth, windowSize.y));
    ImGui::SetNextWindowPos(ImVec2(resolution.x, 0));
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("Inspector", nullptr, flags);

    ImGui::SeparatorText("Playback Controls");
    {  // Control buttons
        ImVec2 size = ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight());
        ImVec4 bg_col = ImVec4(0.15f, 0.25f, 0.30f, 1.00f);
        ImVec4 accent_col = ImVec4(0.15f, 0.60f, 0.15f, 1.00f);
        ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);           // No tint
        auto cmd2uv0 = [](GUICommand i) {
            assert(i >= AutoPlay && i <= Restart);
            return ImVec2((float) i / 5, 0.0f);
        };
        auto cmd2uv1 = [](GUICommand i) {
            assert(i >= AutoPlay && i <= Restart);
            return ImVec2((float) (i + 1) / 5, 1.0f);
        };

        bool wasAutoPlayed = autoPlayed;
        for (int i = 0; i < 5; ++i) {
            ImGui::PushID(i);
            auto cmd = static_cast<GUICommand>(i);

            if (cmd == Forward) {
                ImGui::SetNextItemWidth(ImGui::GetTextLineHeight() * 6);
                ImGui::InputInt("", &forwardWaves, 1, 10);
                ImGui::SameLine();
            }

            auto nameTip = commandNames[cmd];
            ImVec4 color = (wasAutoPlayed && cmd == AutoPlay) || (!wasAutoPlayed && cmd == Pause) ? accent_col : bg_col;
            bool activate = ImGui::ImageButton(nameTip.first, controlButtonTexID, size, cmd2uv0(cmd), cmd2uv1(cmd), color, tint_col);
            activate |= ((wasAutoPlayed && cmd == Pause) || (!wasAutoPlayed && cmd == AutoPlay)) && ImGui::IsKeyPressed(ImGuiKey_Space, false);  // Space key for AutoPlay / Pause
            activate |= cmd == Forward && ImGui::IsKeyPressed(ImGuiKey_Enter, false);  // Enter key for Forward
            activate |= cmd == Restart && ImGui::IsKeyPressed(ImGuiKey_F5, false);  // F5 key for Restart
            activate |= cmd == Save && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false);  // Ctrl + S for Save
            if (activate) {
                std::cout << "Command: " << nameTip.first << std::endl;
                std::lock_guard lock(mtxCommand);
                command = cmd;
                cv.notify_one();  // Notify the render thread
            }
            ImGui::SetItemTooltip("%s", nameTip.second);
            ImGui::SameLine();

            if (cmd == Forward) {
                ImGui::NewLine();
            }
            ImGui::PopID();
        }
        ImGui::NewLine();
    }

    ImGui::ProgressBar((float) waveStart / (float) spp, {ImGui::GetColumnWidth(), 0}, waveStart == spp ? "Done" : StringPrintf("%d/%d SPP", waveStart, spp).c_str());

    rayCastingNodeOpened = ImGui::TreeNode("Ray Casting");
    if (rayCastingNodeOpened) {
        if (rcData.valid) {
            auto radiance = film.GetPixelRGB(rcData.pixel);
            ImGui::Text("Radiance: (%.2f, %.2f, %.2f)", radiance.r, radiance.g, radiance.b);
            ImGui::Text("Hit: (%.2f, %.2f, %.2f)", rcData.hit.x, rcData.hit.y, rcData.hit.z);
            ImGui::Text("Normal: (%.2f, %.2f, %.2f)", rcData.normal.x, rcData.normal.y, rcData.normal.z);
            ImGui::Text("UV: (%.2f, %.2f)", rcData.uv.x, rcData.uv.y);
            if (rcData.cacheId == -1)
                ImGui::Text("Cache ID: <invalid>");
            else {
                ImGui::Text("Cache ID: %u", rcData.cacheId);
                ImGui::Text("Fluence: %f", rcData.fluence);
                ImGui::Text("CE: %f", rcData.ce);
            }
        } else {
            ImGui::Text("No intersection");
        }
        ImGui::TreePop();
    }

    ColormapNode();
    CacheCurvesNode();

    ImGui::SeparatorText("Spatial Subdivision");
    {}

    ImGui::End();
    enableRayCasting = rayCastingNodeOpened || cacheCurvesNodeOpened;
}

void GuidingViewerGUI::StatusBar() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav;
    ImGui::SetNextWindowSize(ImVec2(resolution.x, statusBarHeight));
    ImGui::SetNextWindowPos(ImVec2(0, tabHeight + resolution.y));
    ImGui::SetNextWindowBgAlpha(0.6f);

    ImGui::Begin("StatusBar", nullptr, flags);

    ImGuiIO &io = ImGui::GetIO();
    std::string mouseInfo;
    if (ImGui::IsMousePosValid()) {
        mouseInfo = StringPrintf("Mouse: (%d, %d)", (int) io.MousePos.x, (int) io.MousePos.y);
    }
    else
        mouseInfo = "Mouse: <invalid>";
    ImGui::Text("%s | %.3f ms/frame (%.1f FPS) | %s", stateNames[renderState], 1000.0f / io.Framerate, io.Framerate, mouseInfo.c_str());

    ImGui::End();
}

void GuidingViewerGUI::ColormapNode() {
    bool disableColorMap = selectedChannel == Channel_CacheID;
    ImGui::BeginDisabled(disableColorMap);
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    auto &io = ImGui::GetIO();
    if (ImGui::TreeNode("Color Map")) {
        auto &sd = shaderData[selectedChannel];
        if (!disableColorMap && ImGui::IsKeyPressed(ImGuiKey_E)) {
            if (!io.KeyShift) {
                sd.scale *= 1.1f;
            } else {
                sd.scale /= 1.1f;
            }
        }
        if (!disableColorMap && ImGui::IsKeyPressed(ImGuiKey_M)) {
            sd.tonemapped ^= true;
        }
        ImGui::InputFloat("Scale", &sd.scale, 0.1f, 1.0f);
        ImGui::InputFloat("Offset", &sd.offset, 0.1f, 1.0f);
        if (ImGui::Button("Reset") || (!disableColorMap && ImGui::IsKeyPressed(ImGuiKey_R, false))) {
            sd.scale = 1.0f;
            sd.offset = 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Normalize") || !sd.firstNormalized
            || (!disableColorMap && ImGui::IsKeyPressed(ImGuiKey_N, false))) {
            sd.firstNormalized = true;
            auto [minVal, maxVal] = GetMinMaxFromFilm(selectedChannel);
            sd.scale = 1.0f / std::max(1e-6f, maxVal - minVal);
            sd.offset = -minVal;
            }
        ImGui::SetNextItemWidth(90);
        ImGui::Combo("Tonemap", reinterpret_cast<int*>(&selectedCMap), cmap_names, CMap_Count);
        ImGui::SameLine();
        ImGui::Checkbox("##check_tonemap", &sd.tonemapped);
        if (sd.tonemapped)
            ImGui::Image((void*) (uintptr_t) cmap_tex_ids[selectedCMap], ImVec2(ImGui::GetColumnWidth(), ImGui::GetFrameHeight()));
        float xmin = ImGui::GetItemRectMin().x, xmax = ImGui::GetItemRectMax().x;
        if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
            float t = (io.MousePos.x - xmin) / (xmax - xmin);
            ImGui::Text("Pos: %.2f", t);
            ImGui::Text("Value: %.4f", t / sd.scale - sd.offset);
            ImGui::EndTooltip();
        }
        ImGui::TreePop();
    }
    ImGui::EndDisabled();
}

void GuidingViewerGUI::CacheCurvesNode() {
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    cacheCurvesNodeOpened = ImGui::TreeNode("Cache Curves");
    if (cacheCurvesNodeOpened) {
        ImGui::BulletText("Left click canvas to insert a guiding cache probe");
        ImGui::BulletText("Right click a probe to remove it");

        ImPlotAxisFlags flags = ImPlotAxisFlags_NoLabel;
        if (ImPlot::BeginPlot("CE vs. Iter", ImVec2(-1, 200))) {
            std::lock_guard lock(mtxCECurves);
            ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
            for (const auto &[id, curve] : ceCurves) if (curve.active) {
                auto &data = curve.data;
                ImPlot::PlotLine(std::to_string(id).c_str(), &data[0].x, &data[0].y, data.size(), 0, 0, sizeof(PlotDataEntry));
            }
            ImPlot::EndPlot();
        }

        if (ImGui::Button("Reset All"))
            ResetCECurves();
        ImGui::TreePop();
    }
}

void GuidingViewerGUI::UpdateRayCastingResult() {
    rcData.valid = false;
    rcData.cacheId = -1;
    static openpgl::cpp::SurfaceSamplingDistribution ssd(field);
    static ScratchBuffer scratchBuffer;
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsMousePosValid()) {
        rcData.pixel = {(int) io.MousePos.x, (int) io.MousePos.y - tabHeight};
        if (enableRayCasting) {
            IndependentSampler _sampler(spp, 0);
            Sampler sampler(&_sampler);
            Filter filter = camera.GetFilm().GetFilter();
            CameraSample cameraSample = GetCameraSample(sampler, rcData.pixel, filter);
            SampledWavelengths lambda = camera.GetFilm().SampleWavelengths(sampler.Get1D());
            auto cameraRay = camera.GenerateRayDifferential(cameraSample, lambda);
            if (cameraRay) {
                auto sit = scene.Intersect(cameraRay->ray);
                if (sit) {
                    // Intersection found
                    rcData.valid = true;
                    rcData.hit = cameraRay->ray(sit->tHit);
                    rcData.normal = sit->intr.n;
                    rcData.uv = sit->intr.uv;
                    auto bsdf = sit->intr.GetBSDF(cameraRay->ray, lambda, camera, scratchBuffer, sampler);
                    if (bsdf) {
                        GuidedBSDF gbsdf(&sampler, field, &ssd, true, EGuideMIS);
                        float rnd = 0.0f;
                        if (gbsdf.init(&bsdf, cameraRay->ray, sit, rnd)) {
                            // Guiding region available
                            rcData.cacheId = gbsdf.getId();
                            rcData.fluence = gbsdf.getFluence();
                            rcData.ce = gbsdf.getCE();
                        }
                    }
                    scratchBuffer.Reset();
                }
            }
        }
    }
}

void GuidingViewerGUI::ResetCECurves() {
    std::lock_guard lock(mtxCECurves);
    ceCurves.clear();
    ImPlot::DestroyContext();
    ImPlot::CreateContext();
}

// Called by the render thread
void GuidingViewerGUI::AppendToCECurves() {
    std::lock_guard lock(mtxCECurves);
    for (auto &[id, curve] : ceCurves) if (curve.active) {
        curve.data.emplace_back(waveStart, field->GetCESurface(id));
    }
    ImPlot::SetNextAxisToFit(ImAxis_X1);
}

void GuidingViewerGUI::ClearFilm() {
    ParallelFor2D(film.PixelBounds(), [&](Point2i p) {
        film.ResetPixel(p);
    });
}

void GuidingViewerGUI::RenderThread() {
    // This function runs in a separate thread than the GUI.
    // It listens for pending render commands from the GUI thread and calls the renderWave function until the rendering is completed.
    // Only this thread modifies the waveStart and renderState variable.
    renderState = Initial;

    while (true) {
        if (!autoPlayed || waveStart == spp) {
            // Listen for commands from the GUI
            std::unique_lock lock(mtxCommand);
            cv.wait(lock, [this] { return command != None; });
        }

        GUICommand oldCommand = command;
        command = None;
        // Process commands from the GUI
        switch (oldCommand) {
            case AutoPlay:
                std::cout << "Resuming rendering" << std::endl;
                autoPlayed = true;
                break;
            case Pause:
                std::cout << "Pausing rendering" << std::endl;
                autoPlayed = false;
                break;
            case Forward:
                std::cout << "Rendering next waves" << std::endl;
                autoPlayed = false;
                break;
            case Save:
                std::cout << "Saving rendering" << std::endl;
                saveImage(waveStart);
                continue;
            case Restart:
                std::cout << "Restarting rendering" << std::endl;
                waveStart = 0;
                renderState = Initial;
                ClearFilm();
                field->Reset();
                UpdateCPUFramebufferFromFilm();
                ResetCECurves();
                continue;
            case Terminate:
                std::cout << "Terminating rendering" << std::endl;
                renderState = Completed;
                return;
            case None:
                break;
            default:
                Error("Unexpected command \"%s\" in RenderThread", commandNames[oldCommand].first);
        }

        // Process AutoPlay, Pause, and Forward
        bool renderedSomething = false;
        if (autoPlayed) {
            if (waveStart < spp) {
                renderState = Rendering;
                if (waveStart > 0)
                    postprocessWave(waveStart);
                AppendToCECurves();
                renderWave(waveStart++);
                UpdateCPUFramebufferFromFilm();
                renderedSomething = true;
            }
        } else if (oldCommand == Forward) {
            renderState = Rendering;
            int wavesLeft = forwardWaves;
            while (waveStart < spp && wavesLeft-- > 0) {
                if (waveStart > 0)
                    postprocessWave(waveStart);
                AppendToCECurves();
                renderWave(waveStart++);
                UpdateCPUFramebufferFromFilm();
                renderedSomething = true;
            }
        }
        if (renderedSomething && (Options->writePartialImages || waveStart == spp)) {
            saveImage(waveStart);
        }
        renderState = WaveEnd;
    }
}

} // namespace pbrt
