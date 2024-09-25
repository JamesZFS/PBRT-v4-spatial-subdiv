//
// Created by fengshi on 9/23/24.
//

#include "helper.h"

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <cstdio>
#include <iostream>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif

#define STBI_NO_PIC
#define STBI_ASSERT CHECK
#include <stb/stb_image.h>

inline static void glfw_error_callback(int error, const char *description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void InitializeGlad() {
    if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
        glfwTerminate();
        std::cerr << "Could not initialize GLEW" << std::endl;
        exit(1);
    }

    int glVersionMajor, glVersionMinor;
    glGetIntegerv(GL_MAJOR_VERSION, &glVersionMajor);
    glGetIntegerv(GL_MINOR_VERSION, &glVersionMinor);
    std::cout << "Initialized OpenGL version " << glVersionMajor << "." << glVersionMinor << std::endl;
}

static GLFWwindow *InitializeGLFW(const char *title, int width, int height) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        pbrt::Error("Failed to initialize GLFW");
        return nullptr;
    }

    // GL 4.1
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    // Create window with graphics context
    GLFWwindow *window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (window == nullptr) return nullptr;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync
    return window;
}

GLFWwindow *InitializeImGui(const char *title, int width, int height) {
    auto window = InitializeGLFW(title, width, height);
    if (window == nullptr) return nullptr;
    InitializeGlad();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init();

    return window;
}

bool InitializeFrame(GLFWwindow *window) {
    // Poll and handle events (inputs, window resize, etc.)
    // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
    // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
    // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
    // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
    glfwPollEvents();
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
        ImGui_ImplGlfw_Sleep(10);
        return true;
    }

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    return false;
}

void RenderImGuiFrame(GLFWwindow *window) {
    // Draw GUI
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

void DestroyImGui(GLFWwindow *window) {
    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}

bool LoadTextureFromFile(const char *filename, GLuint &out_texture, int &out_width, int &out_height, bool interpolate) {
    // Load from file
    int image_width = 0;
    int image_height = 0;
    unsigned char *image_data = stbi_load(filename, &image_width, &image_height, nullptr, 4);
    if (image_data == nullptr)
        return false;

    // Create a OpenGL texture identifier
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, interpolate ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, interpolate ? GL_LINEAR : GL_NEAREST);
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

void UpdateTextureFromRGBData(GLuint image_texture, const pbrt::RGB *image_data, int image_width,
                              int image_height, bool interpolate) {
    // Bind the texture
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, interpolate ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, interpolate ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Upload pixels into texture
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, image_width, image_height, 0, GL_RGB, GL_FLOAT, image_data);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void UpdateTextureFromFloatData(GLuint image_texture, const float *image_data, int image_width, int image_height,
                                bool interpolate) {
    // Bind the texture
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, interpolate ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, interpolate ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Upload pixels into texture
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, image_width, image_height, 0, GL_RED, GL_FLOAT, image_data);
    glBindTexture(GL_TEXTURE_2D, 0);
}


// ==== Tonemapped Image Shader Begin ====
static GLuint _vao_full_screen;
static Shader _image_tonemapped_shader;

static std::string _cmap_paths[pbrt::GuidingViewerGUI::CMap_Count] = {
    PBRT_ROOT_DIR "images/cmaps/cividis.png",
    PBRT_ROOT_DIR "images/cmaps/inferno.png",
    PBRT_ROOT_DIR "images/cmaps/magma.png",
    PBRT_ROOT_DIR "images/cmaps/plasma.png",
    PBRT_ROOT_DIR "images/cmaps/viridis.png"
};
GLuint cmap_tex_ids[pbrt::GuidingViewerGUI::CMap_Count] = {0};

void InitializeTonemappedImageContext() {
    //Create Quad covering the entire screen
    struct Vertex {
        float x, y, z;
        float u, v;
    };

    int quad_indices[6] = {0, 1, 2, 0, 2, 3};
    Vertex quad_vertices[4] = {
        Vertex{-1.0f, -1.0f, 0.0f, 0.0f, 0.0f},
        Vertex{1.0f, -1.0f, 0.0f, 1.0f, 0.0f},
        Vertex{1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        Vertex{-1.0f, 1.0f, 0.0f, 0.0f, 1.0f}
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
    glVertexAttribPointer(ind_pos, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *) offsetof(Vertex, x));
    glVertexAttribPointer(ind_uv, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *) offsetof(Vertex, u));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Load cmaps
    for (int i = 0; i < pbrt::GuidingViewerGUI::CMap_Count; i++) {
        int width, height;
        if (!LoadTextureFromFile(_cmap_paths[i].c_str(), cmap_tex_ids[i], width, height, true))
            pbrt::Error("Failed to load colormap %s from disk", _cmap_paths[i].c_str());
        std::cout << "Loaded a " << width << "x" << height << " colormap from " << _cmap_paths[i] << std::endl;
    }

    std::cout << "Initialized tonemapped image context." << std::endl;
}

void DrawTonemappedImage(GLuint image_tex_id, GLuint cmap_tex_id,
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
    _image_tonemapped_shader.setUniform1i("tonemapped", tonemapped);
    // Render!
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(6), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

// ==== Tonemapped Image Shader End ====
