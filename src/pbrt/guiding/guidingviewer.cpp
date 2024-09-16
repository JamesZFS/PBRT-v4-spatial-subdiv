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

#include "guidingviewer.h"

static void glfw_error_callback(int error, const char *description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

namespace pbrt {
GuidingViewerGUI::GuidingViewerGUI(Camera camera, Primitive aggregate, int spp,
                                   std::function<void(int waveStart)> renderWave,
                                   std::function<void(int waveEnd)> postprocessWave)
    : camera(camera), film(camera.GetFilm()), aggregate(aggregate), spp(spp), waveStart(0),
      renderWave(renderWave), postprocessWave(postprocessWave) {
    Bounds2i pixelBounds = film.PixelBounds();
    resolution = pixelBounds.Diagonal();
    windowWidth = resolution.x + inspectorWidth, windowHeight = resolution.y + statusBarHeight;
    cpuFramebuffer = new RGB[resolution.x * resolution.y];
    for (int i = 0; i < resolution.x * resolution.y; ++i)
        cpuFramebuffer[i] = RGB(0.0f, 0.0f, 0.0f);
}

GuidingViewerGUI::~GuidingViewerGUI() {
    delete[] cpuFramebuffer;
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
    GLFWwindow *window = glfwCreateWindow(windowWidth, windowHeight, "Guiding Viewer", nullptr, nullptr);
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

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

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

        Inspector();
        StatusBar();

        // GUI Render
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w,clear_color.w);
        DrawRendering();

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

void GuidingViewerGUI::RenderThread() {
    // This function runs in a separate thread than the GUI.
    // It listens for pending render commands from the GUI thread and calls the renderWave function until the rendering is completed.
    // Only this thread modifies the waveStart and renderState variable.
    renderState = Initial;

    while (waveStart < spp) {
        {
            std::unique_lock lock(mtx);
            cv.wait(lock, [this] { return command != None; });
        }

        int waveEnd = waveStart + 1;
        if (command == NextWave) {
            renderState = Rendering;
            renderWave(waveStart);
            UpdateFramebufferFromFilm();
            postprocessWave(waveEnd);
            waveStart = waveEnd;
            renderState = WaveEnd;
        } else if (command == Terminate) {
            std::cout << "Terminating rendering" << std::endl;
            break;
        } else {
            Error("Unexpected command in RenderThread");
        }
        command = None;
    }

    renderState = Completed;
    std::cout << "Rendering completed" << std::endl;
}

void GuidingViewerGUI::UpdateFramebufferFromFilm() {
    ParallelFor(0, resolution.x * resolution.y,
        PBRT_CPU_GPU_LAMBDA(int index) {
            Point2i p(index % resolution.x, index / resolution.x);
            cpuFramebuffer[index] = 1 * film.GetPixelRGB(p + film.PixelBounds().pMin);
        });
}

void GuidingViewerGUI::Inspector() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    ImGui::SetNextWindowSize(ImVec2(inspectorWidth, windowHeight));
    ImGui::SetNextWindowPos(ImVec2(resolution.x, 0));
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("Inspector", nullptr, flags);

    {
        ImGui::BeginDisabled(renderState == Rendering || renderState == Completed);
        if (ImGui::Button("Render Next Wave")) {
            // Simulating a render wave request
            std::lock_guard lock(mtx);
            command = NextWave;
            cv.notify_one();
        }
        ImGui::EndDisabled();
    }

    if (renderState != Completed)
        ImGui::Text("Progress:");
    else
        ImGui::Text("Done!");
    ImGui::SameLine();
    ImGui::ProgressBar((float) waveStart / (float) spp);

    ImGui::End();
}

void GuidingViewerGUI::StatusBar() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav;
    ImGui::SetNextWindowSize(ImVec2(resolution.x, statusBarHeight));
    ImGui::SetNextWindowPos(ImVec2(0, resolution.y));
    ImGui::SetNextWindowBgAlpha(0.6f);

    ImGui::Begin("StatusBar", nullptr, flags);

    ImGuiIO &io = ImGui::GetIO();
    std::string mouseInfo;
    if (ImGui::IsMousePosValid())
        mouseInfo = StringPrintf("Mouse: (%.0f, %.0f)", io.MousePos.x, io.MousePos.y);
    else
        mouseInfo = "Mouse: <invalid>";
    ImGui::Text("%.3f ms/frame (%.1f FPS) | %s", 1000.0f / io.Framerate, io.Framerate, mouseInfo.c_str());

    ImGui::End();
}

void GuidingViewerGUI::DrawRendering() {
    glViewport(0, statusBarHeight, resolution.x, resolution.y);
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw the current rendering result
    glEnable(GL_FRAMEBUFFER_SRGB);
    glRasterPos2f(-1, 1);

    glPixelZoom(1, -1);
    glDrawPixels(resolution.x, resolution.y, GL_RGB, GL_FLOAT, cpuFramebuffer);
}

} // namespace pbrt
