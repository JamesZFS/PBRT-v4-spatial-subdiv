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

static void glfw_error_callback(int error, const char *description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

namespace pbrt {

const static std::map<GuidingViewerGUI::ControlCommand, std::pair<const char *, const char *>> commandNames = {
    {GuidingViewerGUI::Resume, {"Resume", "Continue rendering"}},
    {GuidingViewerGUI::Pause, {"Pause", "Pause the rendering"}},
    {GuidingViewerGUI::Forward, {"Forward", "Render the next wave of samples"}},
    {GuidingViewerGUI::Terminate, {"Terminate", "Terminate rendering"}},
    {GuidingViewerGUI::Restart, {"Restart", "Restart rendering"}},
};

static std::string controlButtonTexPath = PBRT_ROOT_DIR "images/control_buttons.png";

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

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    if (!LoadTextureFromFile(PBRT_ROOT_DIR "images/control_buttons.png", reinterpret_cast<GLuint&>(controlButtonTextureID), controlButtonTextureWidth, controlButtonTextureHeight))
        Error("Failed to load control_texture.png from disk");

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
            cv.wait(lock, [this] { return command != Pause; });
        }

        int waveEnd = waveStart + 1;
        if (command == Forward) {
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
            Error("Unexpected command \"%s\" in RenderThread", commandNames.at(command).first);
        }
        command = Pause;
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
    ImGuiIO& io = ImGui::GetIO();

    {  // Control buttons
        // ImGui::BeginDisabled(renderState == Rendering || renderState == Completed);  // Disable buttons during rendering
        ImTextureID my_tex_id = io.Fonts->TexID;

        ImVec2 size = ImVec2(20.0f, 20.0f);
        ImVec4 bg_col = ImVec4(0.15f, 0.25f, 0.30f, 1.00f);
        ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);           // No tint
        auto cmd2uv0 = [](ControlCommand i) {
            assert(i >= Play && i <= Restart);
            return ImVec2((float) i / (float) ControlCommandCount, 0.0f);
        };
        auto cmd2uv1 = [](ControlCommand i) {
            assert(i >= Play && i <= Restart);
            return ImVec2((float) (i + 1) / (float) ControlCommandCount, 1.0f);
        };

        for (int i = 0; i < (int) ControlCommandCount; ++i) {
            auto cmd = static_cast<ControlCommand>(i);
            auto nameTip = commandNames.at(cmd);
            if (ImGui::ImageButton(nameTip.first, controlButtonTextureID, size, cmd2uv0(cmd), cmd2uv1(cmd), bg_col, tint_col)) {
                std::lock_guard lock(mtx);
                command = cmd;
                cv.notify_one();  // Notify the render thread
            }
            ImGui::SetItemTooltip("%s", nameTip.second);
            ImGui::SameLine();
        }
        ImGui::NewLine();
        // ImGui::EndDisabled();
    }

    static std::map<RendererState, const char *> stateNames = {
        {Initial,   "Initial    "},
        {Rendering, "Rendering.."},
        {WaveEnd,     "Wave End   "},
        {Completed, "Completed! "},
    };
    ImGui::Text("%s", stateNames.at(renderState));

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
