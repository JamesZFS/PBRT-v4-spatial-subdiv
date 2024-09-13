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

#define GL_CHECK(call)                                                   \
    do {                                                                 \
        call;                                                            \
        if (GLenum err = glGetError(); err != GL_NO_ERROR)               \
            LOG_FATAL("GL error: %s for " #call, getGLErrorString(err)); \
    } while (0)

#define GL_CHECK_ERRORS()                                     \
    do {                                                      \
        if (GLenum err = glGetError(); err != GL_NO_ERROR)    \
            LOG_FATAL("GL error: %s", getGLErrorString(err)); \
    } while (0)

const char *getGLErrorString(GLenum error) {
    switch (error) {
        case GL_NO_ERROR:
            return "No error";
        case GL_INVALID_ENUM:
            return "Invalid enum";
        case GL_INVALID_VALUE:
            return "Invalid value";
        case GL_INVALID_OPERATION:
            return "Invalid operation";
        case GL_OUT_OF_MEMORY:
            return "Out of memory";
        default:
            return "Unknown GL error";
    }
}

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

    // Create window with graphics context
    GLFWwindow *window = glfwCreateWindow(resolution.x, resolution.y, "Guiding Viewer", nullptr, nullptr);
    if (window == nullptr) return;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void) io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Our state
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);

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

        // A GUI window
        {
            ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!" and append into it.

            ImGui::BeginDisabled(renderState == Rendering || renderState == Completed);
            if (ImGui::Button("Render Next Wave")) {
                // Simulating a render wave request
                std::lock_guard lock(mtx);
                command = NextWave;
                cv.notify_one();
            }
            ImGui::EndDisabled();

            if (renderState != Completed)
                ImGui::Text("Progress:");
            else
                ImGui::Text("Done!");
            ImGui::SameLine();
            ImGui::ProgressBar((float) waveStart / (float) spp);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        // GUI Render
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        int windowWidth, windowHeight;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        float pixelScales[2] = {(float)display_w / (float)windowWidth,
                                (float)display_h / (float)windowHeight};

        GL_CHECK(glViewport(0, 0, display_w, display_h));
        GL_CHECK(
            glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w,
                clear_color.w));
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw the current rendering result
        GL_CHECK(glEnable(GL_FRAMEBUFFER_SRGB));
        GL_CHECK(glRasterPos2f(-1, 1));
        GL_CHECK(glPixelZoom(pixelScales[0], -pixelScales[1]));
        GL_CHECK(glDrawPixels(resolution.x, resolution.y, GL_RGB, GL_FLOAT, cpuFramebuffer));

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

} // namespace pbrt
