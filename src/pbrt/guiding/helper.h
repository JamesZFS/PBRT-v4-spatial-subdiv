//
// Created by fengshi on 9/23/24.
//

#ifndef HELPER_H
#define HELPER_H

#include "imgui.h"

#include <pbrt/util/shader.h>  // Will include glad
#include <pbrt/util/color.h>  // Will include glad
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

enum SelectedChannel {
    Channel_Radiance = 0,
    Channel_CacheID,
    Channel_Fluence,
    Channel_CE,
    Channel_Samples,
    Channel_ZeroSamples,
    Channel_Depth,
    Channel_Reference,
    Channel_Error,
    Channel_Count,
};

enum Colormap {
    CMap_None = 0,
    CMap_Cividis,
    CMap_Inferno,
    CMap_Magma,
    CMap_Plasma,
    CMap_Viridis,
    CMap_Count,
};

enum ErrorMetric {
    Metric_MSE = 0,
    Metric_MAE,
    Metric_MRSE,
    Metric_MRAE,
    Metric_Count,
};

struct TonemapShaderUniforms {
    float scale, offset, clipValue;
    GLuint cmapTex;
};

extern const char* cmap_names[CMap_Count];

extern GLuint cmap_tex_ids[CMap_Count];

void ConfigureTonemapShader(Shader &shader, GLuint sourceTex, bool singleChannel, const TonemapShaderUniforms &uniforms);

bool IsSingleChannel(SelectedChannel channel);

template<ErrorMetric metric>
float CalcError(float x, float ref);

template<ErrorMetric metric>
float CalcError(const pbrt::RGB &x, const pbrt::RGB &ref) {
    pbrt::RGB error(CalcError<metric>(x.r, ref.r), CalcError<metric>(x.g, ref.g), CalcError<metric>(x.b, ref.b));
    return error.Average();
}

template<> float CalcError<Metric_MSE>(float x, float ref);
template<> float CalcError<Metric_MAE>(float x, float ref);
template<> float CalcError<Metric_MRSE>(float x, float ref);
template<> float CalcError<Metric_MRAE>(float x, float ref);

std::function<float(const pbrt::RGB&, const pbrt::RGB&)> GetErrorFunc(ErrorMetric metric);

std::string FormatInteger(int64_t v);

GLFWwindow *InitializeImGui(const char *title, int width, int height);

bool InitializeFrame(GLFWwindow *window);

void RenderImGuiFrame(GLFWwindow *window);

void DestroyImGui(GLFWwindow *window);

// Simple helper function to load an image into a OpenGL texture with common settings
bool LoadTextureFromFile(const char *filename, GLuint &out_texture, int &out_width, int &out_height, bool interpolate);

// Simple helper function to load an image from CPU framebuffer into a OpenGL texture with common settings
void UpdateTextureFromRGBData(GLuint image_texture, const pbrt::RGB *image_data, int image_width, int image_height,
                              bool interpolate);

void UpdateTextureFromFloatData(GLuint image_texture, const float *image_data, int image_width, int image_height,
                                bool interpolate);

void InitializeTonemaps();

void InitializeTonemappedImageContext();

// Create an ImGui::Image-like region at the screen_pos that displays image_tex_id tonemapped with cmap_tex_id, with the given size and UV coordinates
void DrawTonemappedImage(GLuint image_tex_id, GLuint cmap_tex_id,
                         ImVec2 screen_pos, ImVec2 image_size, ImVec2 window_size,
                         float scale, float offset, bool single_channel, bool tonemapped);

GLuint CreateExampleFramebuffer(int width, int height);

#endif //HELPER_H
