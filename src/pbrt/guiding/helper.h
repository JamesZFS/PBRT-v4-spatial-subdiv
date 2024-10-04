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
    Channel_Count,
};

enum Colormap {
    CMap_Cividis = 0,
    CMap_Inferno,
    CMap_Magma,
    CMap_Plasma,
    CMap_Viridis,
    CMap_Count,
};

extern GLuint cmap_tex_ids[CMap_Count];

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
