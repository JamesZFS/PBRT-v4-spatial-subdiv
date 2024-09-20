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
#include <pbrt/util/shader.h>  // Will include glad
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

#include <pbrt/cameras.h>
#include <pbrt/samplers.h>
#include <pbrt/film.h>
#include <pbrt/interaction.h>
#include <pbrt/shapes.h>
#include <pbrt/scene.h>
#include "guidingviewer.h"
#include "guiding.h"

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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

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

GLuint _vao_full_screen;
Shader _image_tonemapped_shader;

static const char* _cmap_names[pbrt::GuidingViewerGUI::CMap_Count] = {
    "Cividis",
    "Inferno",
    "Magma",
    "Plasma",
    "Viridis"
};
static std::string _cmap_paths[pbrt::GuidingViewerGUI::CMap_Count] = {
    PBRT_ROOT_DIR "images/cmaps/cividis.png",
    PBRT_ROOT_DIR "images/cmaps/inferno.png",
    PBRT_ROOT_DIR "images/cmaps/magma.png",
    PBRT_ROOT_DIR "images/cmaps/plasma.png",
    PBRT_ROOT_DIR "images/cmaps/viridis.png"
};
static GLuint _cmap_tex_ids[pbrt::GuidingViewerGUI::CMap_Count] = { 0 };
enum TonemappingMode {
    Mode_Original = 1,
    Mode_Tonemapped = 2
};

static void InitializeTonemappedImageContext() {
    //Create Quad covering the entire screen
    struct Vertex {
        float x, y, z;
        float u, v;
    };

    int quad_indices[6] = { 0,1,2, 0,2,3 };
    Vertex quad_vertices[4] = {
        Vertex { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f },
        Vertex { 1.0f, -1.0f, 0.0f, 1.0f, 0.0f },
        Vertex { 1.0f, 1.0f, 0.0f, 1.0f, 1.0f },
        Vertex { -1.0f, 1.0f, 0.0f, 0.0f, 1.0f }
    };

    _image_tonemapped_shader = ShaderBuilder()
        .addStage(GL_VERTEX_SHADER, PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.vert")
        .addStage(GL_FRAGMENT_SHADER, PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag").build();
        // .addStage(GL_VERTEX_SHADER, PBRT_ROOT_DIR "src/pbrt/shaders/debug.vert")
        // .addStage(GL_FRAGMENT_SHADER, PBRT_ROOT_DIR "src/pbrt/shaders/debug.frag").build();

    // Create vertex (vbo) and index (ibo) buffer objects and fill them with the data for the quad, this will be the only geometry we need.
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, 4 * sizeof(Vertex), quad_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    GLuint ibo;
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 6 * sizeof(int), quad_indices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    //Setup vertex array object so we dont have to mess with binding the buffers every time
    glGenVertexArrays(1, &_vao_full_screen);
    glBindVertexArray(_vao_full_screen);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

    // Enable pos and uv attributes
    GLuint ind_pos = _image_tonemapped_shader.getAttributeLocation("pos");
    GLuint ind_uv = _image_tonemapped_shader.getAttributeLocation("uv");
    glEnableVertexAttribArray(ind_pos);
    glEnableVertexAttribArray(ind_uv);
    glVertexAttribPointer(ind_pos, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    glVertexAttribPointer(ind_uv, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Load cmaps
    for (int i = 0; i < pbrt::GuidingViewerGUI::CMap_Count; i++) {
        int width, height;
        if (!LoadTextureFromFile(_cmap_paths[i].c_str(), _cmap_tex_ids[i], width, height))
            pbrt::Error("Failed to load colormap %s from disk", _cmap_paths[i].c_str());
        std::cout << "Loaded a " << width << "x" << height << " colormap from " << _cmap_paths[i] << std::endl;
    }

    std::cout << "Initialized tonemapped image context." << std::endl;
}

// Create an ImGui::Image-like region at the current cursor that displays image_tex_id tonemapped with cmap_tex_id, with the given size and UV coordinates
static void DrawTonemappedImage(GLuint image_tex_id, GLuint cmap_tex_id,
    ImVec2 screen_pos, ImVec2 image_size, ImVec2 window_size,
    float scale, float offset, bool single_channel, bool tonemapped) {
    glBindVertexArray(_vao_full_screen);
    // ImVec2 cursor_min = ImGui::GetCursorScreenPos();
    ImVec2 lower_left(screen_pos.x, window_size.y - screen_pos.y - image_size.y);
    ImVec2 upper_right(lower_left.x + image_size.x, lower_left.y + image_size.y);
    _image_tonemapped_shader.bind();
    _image_tonemapped_shader.setUniform2f("lower_left", &lower_left.x);
    _image_tonemapped_shader.setUniform2f("upper_right", &upper_right.x);
    _image_tonemapped_shader.setUniform2f("window_size", &window_size.x);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, image_tex_id);
    _image_tonemapped_shader.setUniform1i("image_tex", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, cmap_tex_id);
    _image_tonemapped_shader.setUniform1i("cmap_tex", 1);
    _image_tonemapped_shader.setUniform1f("scale", scale);
    _image_tonemapped_shader.setUniform1f("offset", offset);
    _image_tonemapped_shader.setUniform1i("single_channel", single_channel);
    _image_tonemapped_shader.setUniform1i("mode", tonemapped ? Mode_Tonemapped : Mode_Original);
    // Render!
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(6), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
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

    // GL 4.1
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    // Create window with graphics context
    GLFWwindow *window = glfwCreateWindow(windowSize.x, windowSize.y, "Guiding Viewer", nullptr, nullptr);
    if (window == nullptr) return;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwTerminate();
        std::cerr << "Could not initialize GLEW" << std::endl;
        exit(1);
    }

    int glVersionMajor, glVersionMinor;
    glGetIntegerv(GL_MAJOR_VERSION, &glVersionMajor);
    glGetIntegerv(GL_MINOR_VERSION, &glVersionMinor);
    std::cout << "Initialized OpenGL version " << glVersionMajor << "." << glVersionMinor << std::endl;

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
    ImGui_ImplOpenGL3_Init();

    if (!LoadTextureFromFile(controlButtonTexPath.c_str(), reinterpret_cast<GLuint&>(controlButtonTexID), controlButtonTexWidth, controlButtonTexHeight))
        Error("Failed to load control_texture.png from disk");

    glGenTextures(1, reinterpret_cast<GLuint*>(&renderingTexID));
    UpdateGPUFramebufferFromCPU();
    InitializeTonemappedImageContext();

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

        // GUI Render
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(10, 10));
        Tab();
        Canvas();
        Inspector();
        StatusBar();
        ImGui::PopStyleVar();

        // Draw GUI
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
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

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
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
                        Error("Unknown channel type %d", c);
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

        ImGui::End();
        ImGui::PopStyleVar();
    }
}

void GuidingViewerGUI::Canvas() {
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
    DrawTonemappedImage((GLuint) (uintptr_t) renderingTexID, _cmap_tex_ids[selectedCMap],
        ImVec2(0, tabHeight), ImVec2(resolution.x, resolution.y), ImVec2(windowSize.x, windowSize.y),
        sd.scale, sd.offset, selectedChannel > Channel_CacheID, sd.tonemapped);

    if (enableRayCasting && ImGui::IsWindowHovered()) {  // Ray trace mouse position when hovering over the rendering
        UpdateRayCastingResult();
        if (rcData.cacheId != -1 && ImGui::BeginTooltip()) {
            ImGui::Text("Cache ID: %u", rcData.cacheId);
            ImGui::Text("Fluence: %f", rcData.fluence);
            ImGui::Text("CE: %f", rcData.ce);
            ImGui::EndTooltip();
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

    ImGui::SeparatorText("Ray Casting");
    {
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            enableRayCasting = !enableRayCasting;
        }
        ImGui::Checkbox("Ray Casting Mouse Pixel", &enableRayCasting);
        if (enableRayCasting) {
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
        }
    }

    if (selectedChannel != Channel_CacheID) {
        ImGui::SeparatorText("Color Map");
        ImGui::PushID(selectedChannel);
        if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            if (!io.KeyShift) {
                shaderData[selectedChannel].scale *= 1.1f;
            } else {
                shaderData[selectedChannel].scale /= 1.1f;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_M)) {
            shaderData[selectedChannel].tonemapped ^= true;
        }
        ImGui::InputFloat("Scale", &shaderData[selectedChannel].scale, 0.1f, 1.0f);
        ImGui::InputFloat("Offset", &shaderData[selectedChannel].offset, 0.1f, 1.0f);
        if (ImGui::Button("Reset") || ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            shaderData[selectedChannel].scale = 1.0f;
            shaderData[selectedChannel].offset = 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Normalize") || ImGui::IsKeyPressed(ImGuiKey_N, false)) {
            auto [minVal, maxVal] = GetMinMaxFromFilm(selectedChannel);
            shaderData[selectedChannel].scale = 1.0f / std::max(1e-6f, maxVal - minVal);
            shaderData[selectedChannel].offset = -minVal;
        }
        ImGui::SetNextItemWidth(80);
        ImGui::Combo("Color Map", reinterpret_cast<int*>(&selectedCMap), _cmap_names, CMap_Count);
        ImGui::SameLine();
        ImGui::Checkbox("", &shaderData[selectedChannel].tonemapped);
        ImGui::PopID();
    }

    ImGui::SeparatorText("Guiding");
    {}

    ImGui::SeparatorText("Spatial Subdivision");
    {}

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
    if (ImGui::IsMousePosValid()) {
        mouseInfo = StringPrintf("Mouse: (%d, %d)", (int) io.MousePos.x, (int) io.MousePos.y);
    }
    else
        mouseInfo = "Mouse: <invalid>";
    ImGui::Text("%s | %.3f ms/frame (%.1f FPS) | %s", stateNames[renderState], 1000.0f / io.Framerate, io.Framerate, mouseInfo.c_str());

    ImGui::End();
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
