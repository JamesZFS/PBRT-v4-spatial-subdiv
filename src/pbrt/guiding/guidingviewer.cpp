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

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <cstdio>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

#include <pbrt/cameras.h>
#include <pbrt/samplers.h>
#include <pbrt/film.h>
#include <pbrt/interaction.h>
#include <pbrt/shapes.h>
#include <pbrt/scene.h>
#include "guidingviewer.h"

#define STBI_NO_PIC
#define STBI_ASSERT CHECK
#include <stb/stb_image.h>

// Simple helper function to load an image into a OpenGL texture with common settings
static bool LoadTextureFromFile(const char *filename, GLuint &out_texture, int &out_width, int &out_height)
{
    // Load from file
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load(filename, &image_width, &image_height, nullptr, 4);
    if (image_data == nullptr)
        return false;

    // Create a OpenGL texture identifier
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Upload pixels into texture
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);

    out_texture = image_texture;
    out_width = image_width;
    out_height = image_height;

    return true;
}

// Simple helper function to load an image from CPU framebuffer into a OpenGL texture with common settings
static void UpdateTextureFromRGBData(GLuint image_texture, const pbrt::RGB *image_data, int image_width, int image_height)
{
    // Bind the texture
    glBindTexture(GL_TEXTURE_2D, image_texture);
    glEnable(GL_FRAMEBUFFER_SRGB);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Upload pixels into texture
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image_width, image_height, 0, GL_RGB, GL_FLOAT, image_data);
}

static void UpdateTextureFromFloatData(GLuint image_texture, const float *image_data, int image_width, int image_height)
{
    // Bind the texture
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Upload pixels into texture
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, image_width, image_height, 0, GL_RED, GL_FLOAT, image_data);
}

static void glfw_error_callback(int error, const char *description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

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

static std::vector<const char *> selectedChannelNames = {
    "Radiance (1)",
    "Cache ID (2)",
    "Fluence (3)",
    "CE (4)",
};

static std::string controlButtonTexPath = PBRT_ROOT_DIR "images/control_buttons.png";

GuidingViewerGUI::GuidingViewerGUI(Camera camera, Primitive aggregate, int spp,
                                   const std::function<void(int waveStart)> &renderWave,
                                   const std::function<void(int waveEnd)> &postprocessWave,
                                   const std::function<void(int waveEnd)> &saveImage)
    : camera(camera), film(camera.GetFilm()), isMultiChannel(film.Is<GuidedGBufferFilm>()),
      aggregate(aggregate), spp(spp), waveStart(0),
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

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        Error("Failed to initialize GLFW");
    }

    // GL 3.0 + GLSL 130
    const char *glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    // Create window with graphics context
    GLFWwindow *window = glfwCreateWindow(windowSize.x, windowSize.y, "Guiding Viewer", nullptr, nullptr);
    if (window == nullptr) return;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    if (!LoadTextureFromFile(controlButtonTexPath.c_str(), reinterpret_cast<GLuint&>(controlButtonTexID), controlButtonTexWidth, controlButtonTexHeight))
        Error("Failed to load control_texture.png from disk");

    glGenTextures(1, reinterpret_cast<GLuint*>(&renderingTexID));
    UpdateGPUFramebufferFromCPU();

    // ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

    // Main GUI loop
    while (!glfwWindowShouldClose(window)) {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        Canvas();
        Inspector();
        StatusBar();

        // GUI Render
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw GUI
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Terminate renderer
    {
        std::lock_guard lock(mtx);
        command = Terminate;
        cv.notify_one();
    }
    renderThread.join();
    assert(renderState == Completed);

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}

void GuidingViewerGUI::UpdateCPUFramebufferFromFilm() {
    if (isMultiChannel) {
        auto *gFilm = film.Cast<GuidedGBufferFilm>();
        // Update all channels
        ParallelFor2D(film.PixelBounds(), [&](Point2i p) {
            size_t index = (p.y - film.PixelBounds().pMin.y) * resolution.x + (p.x - film.PixelBounds().pMin.x);
            auto &pixel = gFilm->GetPixel(p);
            cpuFramebuffer.radiance[index] = gFilm->GetPixelRGB(p + film.PixelBounds().pMin);
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
            cpuFramebuffer.radiance[index] = film.GetPixelRGB(p + film.PixelBounds().pMin);
        });
    }

    shouldUpdateGPUFramebuffer = true;
}

// This has to be called in the GUI thread
void GuidingViewerGUI::UpdateGPUFramebufferFromCPU() {
    switch (selectedChannel) {
        case Channel_Radiance:
            UpdateTextureFromRGBData((GLuint) (uintptr_t) renderingTexID, cpuFramebuffer.radiance, resolution.x, resolution.y);
            break;
        case Channel_CacheID:
            UpdateTextureFromRGBData((GLuint) (uintptr_t) renderingTexID, cpuFramebuffer.cacheID, resolution.x, resolution.y);
            break;
        case Channel_Fluence:
            UpdateTextureFromFloatData((GLuint) (uintptr_t) renderingTexID, cpuFramebuffer.fluence, resolution.x, resolution.y);
            break;
        case Channel_CE:
            UpdateTextureFromFloatData((GLuint) (uintptr_t) renderingTexID, cpuFramebuffer.ce, resolution.x, resolution.y);
            break;
    }
}

void GuidingViewerGUI::Canvas() {
    // Draw the current rendering result
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(resolution.x, tabHeight + resolution.y));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::Begin("Rendering", nullptr, flags);

    // Add a channel selection bar for GuidedGBufferFilm
    if (isMultiChannel) {
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
            for (int i = 0; i < selectedChannelNames.size(); ++i) {
                if (newlySelectedChannel == i)
                    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.4f, 0.45f, 0.6f, 1.0f));
                else
                    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.1f, 0.15f, 0.3f, 1.0f));
                if (ImGui::TabItemButton(selectedChannelNames[i])) {
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
    }
    // Possibly update the GPU framebuffer
    if (shouldUpdateGPUFramebuffer) {
        UpdateGPUFramebufferFromCPU();
        shouldUpdateGPUFramebuffer = false;
    }

    ImGui::Image(renderingTexID, ImVec2(resolution.x, resolution.y));

    ImGui::End();
    ImGui::PopStyleVar();
}

void GuidingViewerGUI::Inspector() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    ImGui::SetNextWindowSize(ImVec2(inspectorWidth, windowSize.y));
    ImGui::SetNextWindowPos(ImVec2(resolution.x, 0));
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("Inspector", nullptr, flags);
    ImGuiIO& io = ImGui::GetIO();

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
            if (activate) {
                std::cout << "Command: " << nameTip.first << std::endl;
                std::lock_guard lock(mtx);
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

    ImGui::ProgressBar((float) waveStart / (float) spp, {ImGui::GetColumnWidth(), 0}, waveStart == spp ? "Done" : StringPrintf("%d/%d SPP", waveStart+1, spp).c_str());

    ImGui::SeparatorText("Ray Tracing");
    {
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            rayTracingPixel = !rayTracingPixel;
        }
        ImGui::Checkbox("Ray Tracing Mouse Pixel", &rayTracingPixel);
        if (rayTracingPixel) {
            bool valid = false;
            Point3f hit;
            Normal3f normal;
            Point2i pixel;
            Point2f uv;
            if (ImGui::IsMousePosValid()) {
                pixel = Point2i((int) io.MousePos.x, (int) io.MousePos.y);
                if (rayTracingPixel) {
                    IndependentSampler sampler(spp, 0);
                    Filter filter = camera.GetFilm().GetFilter();
                    CameraSample cameraSample = GetCameraSample(sampler, pixel, filter);
                    SampledWavelengths lambda = camera.GetFilm().SampleWavelengths(sampler.Get1D());
                    auto cameraRay = camera.GenerateRay(cameraSample, lambda);
                    if (cameraRay) {
                        auto sit = aggregate.Intersect(cameraRay->ray);
                        if (sit) {
                            valid = true;
                            hit = cameraRay->ray(sit->tHit);
                            normal = sit->intr.n;
                            uv = sit->intr.uv;
                        }
                    }
                }
            }
            if (valid) {
                ImGui::Text("Hit: (%.2f, %.2f, %.2f)", hit.x, hit.y, hit.z);
                ImGui::Text("Normal: (%.2f, %.2f, %.2f)", normal.x, normal.y, normal.z);
                ImGui::Text("UV: (%.2f, %.2f)", uv.x, uv.y);
            } else {
                ImGui::Text("No intersection");
            }
        }
    }

    ImGui::End();
}

void GuidingViewerGUI::StatusBar() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav;
    ImGui::SetNextWindowSize(ImVec2(resolution.x, statusBarHeight));
    ImGui::SetNextWindowPos(ImVec2(0, tabHeight + resolution.y));
    ImGui::SetNextWindowBgAlpha(0.6f);

    ImGui::Begin("StatusBar", nullptr, flags);

    ImGuiIO &io = ImGui::GetIO();
    std::string mouseInfo;
    Point2i pixel;
    if (ImGui::IsMousePosValid()) {
        pixel = Point2i((int) io.MousePos.x, (int) io.MousePos.y);
        mouseInfo = StringPrintf("Mouse: (%d, %d)", pixel.x, pixel.y);
    }
    else
        mouseInfo = "Mouse: <invalid>";
    ImGui::Text("%s | %.3f ms/frame (%.1f FPS) | %s", stateNames[renderState], 1000.0f / io.Framerate, io.Framerate, mouseInfo.c_str());

    ImGui::End();
}

void GuidingViewerGUI::ClearFilm() {
    for (int x = 0; x < resolution.x; ++x) {
        for (int y = 0; y < resolution.y; ++y) {
            film.ResetPixel(Point2i(x, y));
        }
    }
}

void GuidingViewerGUI::RenderThread() {
    // This function runs in a separate thread than the GUI.
    // It listens for pending render commands from the GUI thread and calls the renderWave function until the rendering is completed.
    // Only this thread modifies the waveStart and renderState variable.
    renderState = Initial;

    while (true) {
        if (!autoPlayed || waveStart == spp) {
            // Listen for commands from the GUI
            std::unique_lock lock(mtx);
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
                UpdateCPUFramebufferFromFilm();
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
                int waveEnd = waveStart + 1;
                renderWave(waveStart);
                UpdateCPUFramebufferFromFilm();
                postprocessWave(waveEnd);
                waveStart = waveEnd;
                renderedSomething = true;
            }
        } else if (oldCommand == Forward) {
            renderState = Rendering;
            int wavesLeft = forwardWaves;
            while (waveStart < spp && wavesLeft > 0) {
                --wavesLeft;
                renderWave(waveStart++);
                UpdateCPUFramebufferFromFilm();
                postprocessWave(waveStart);
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
