//
// Created by fengshi on 9/30/24.
//

#include "Viewport.h"

using namespace pbrt;

Viewport::Viewport(pbrt::Application* parent, pbrt::Film film)
    : View(parent), m_film(film), m_isMultiChannel(film.Is<GuidedGBufferFilm>()),
      m_resolution(film.PixelBounds().Diagonal()),
      m_framebuffer(m_resolution.x, m_resolution.y, PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag") {
    m_cpuBuffer.radiance.resize(m_resolution.x * m_resolution.y);
    m_cpuBuffer.cacheID.resize(m_resolution.x * m_resolution.y);
    m_cpuBuffer.fluence.resize(m_resolution.x * m_resolution.y);
    m_cpuBuffer.ce.resize(m_resolution.x * m_resolution.y);
    m_cpuBuffer.samples.resize(m_resolution.x * m_resolution.y);
    m_cpuBuffer.zeroSamples.resize(m_resolution.x * m_resolution.y);
    m_cpuBuffer.depth.resize(m_resolution.x * m_resolution.y);

    glGenTextures(1, &m_renderingTex);
}

Viewport::~Viewport() {
    glDeleteTextures(1, &m_renderingTex);
}

void Viewport::UpdateCPUBufferFromFilm() {
    if (m_isMultiChannel) {
        auto *gFilm = m_film.Cast<GuidedGBufferFilm>();
        // Update all channels
        ParallelFor2D(m_film.PixelBounds(), [&](Point2i p) {
            size_t index = (p.y - m_film.PixelBounds().pMin.y) * m_resolution.x + (p.x - m_film.PixelBounds().pMin.x);
            auto &pixel = gFilm->GetPixel(p);
            m_cpuBuffer.radiance[index] = gFilm->GetPixelRGB(p);
            if (pixel.guidingData.id != -1) {
                m_cpuBuffer.cacheID[index] = RGB(HashFloat(pixel.guidingData.id, 0), HashFloat(pixel.guidingData.id, 1), HashFloat(pixel.guidingData.id, 2));
            }
            m_cpuBuffer.fluence[index] = pixel.guidingData.fluence;
            m_cpuBuffer.ce[index] = pixel.guidingData.crossEntropy;
            m_cpuBuffer.samples[index] = (float) pixel.guidingData.numSamples;
            m_cpuBuffer.zeroSamples[index] = (float) pixel.guidingData.numZeroValueSamples;
            m_cpuBuffer.depth[index] = (float) pixel.guidingData.depth;
        });
    } else {
        ParallelFor2D(m_film.PixelBounds(), [&](Point2i p) {
            size_t index = (p.y - m_film.PixelBounds().pMin.y) * m_resolution.x + (p.x - m_film.PixelBounds().pMin.x);
            m_cpuBuffer.radiance[index] = m_film.GetPixelRGB(p);
        });
    }
    m_cpuBufferUpdated = true;
}

void Viewport::UpdateFramebuffer(SelectedChannel channel, const Uniforms &uniforms) {
    // Render to the tonemapped framebuffer if the CPU buffer has been updated
    if (m_cpuBufferUpdated.exchange(false)) {
        // Update the rendering texture
        switch (channel) {
            case Channel_Radiance:
                UpdateTextureFromRGBData((GLuint) (uintptr_t) m_renderingTex, m_cpuBuffer.radiance.data(), m_resolution.x, m_resolution.y, false);
                break;
            case Channel_CacheID:
                CHECK(m_isMultiChannel);
                UpdateTextureFromRGBData((GLuint) (uintptr_t) m_renderingTex, m_cpuBuffer.cacheID.data(), m_resolution.x, m_resolution.y, false);
                break;
            case Channel_Fluence:
                CHECK(m_isMultiChannel);
                UpdateTextureFromFloatData((GLuint) (uintptr_t) m_renderingTex, m_cpuBuffer.fluence.data(), m_resolution.x, m_resolution.y, false);
                break;
            case Channel_CE:
                CHECK(m_isMultiChannel);
                UpdateTextureFromFloatData((GLuint) (uintptr_t) m_renderingTex, m_cpuBuffer.ce.data(), m_resolution.x, m_resolution.y, false);
                break;
            case Channel_Samples:
                CHECK(m_isMultiChannel);
                UpdateTextureFromFloatData((GLuint) (uintptr_t) m_renderingTex, m_cpuBuffer.samples.data(), m_resolution.x, m_resolution.y, false);
                break;
            case Channel_ZeroSamples:
                CHECK(m_isMultiChannel);
                UpdateTextureFromFloatData((GLuint) (uintptr_t) m_renderingTex, m_cpuBuffer.zeroSamples.data(), m_resolution.x, m_resolution.y, false);
                break;
            case Channel_Depth:
                CHECK(m_isMultiChannel);
                UpdateTextureFromFloatData((GLuint) (uintptr_t) m_renderingTex, m_cpuBuffer.depth.data(), m_resolution.x, m_resolution.y, false);
                break;
            default:
                Error("Unknown channel %d", channel);
                break;
        }
    }
    // glEnable(GL_FRAMEBUFFER_SRGB);
    m_framebuffer.bind();
    m_framebuffer.clear();

    Shader &shader = m_framebuffer.getShader();
    shader.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_renderingTex);
    shader.setUniform1i("image_tex", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, uniforms.cmapTex);
    shader.setUniform1i("cmap_tex", 1);
    shader.setUniform1f("scale", uniforms.scale);
    shader.setUniform1f("offset", uniforms.offset);
    shader.setUniform1f("clip_val", uniforms.clipValue);
    shader.setUniform1i("single_channel", channel > Channel_CacheID);
    shader.setUniform1i("tonemapped", uniforms.cmapTex > 0);

    // Render!
    m_framebuffer.draw();
    m_framebuffer.unbind();
}

void Viewport::Draw() {
    // ImGui::SeparatorText("Viewport");
    ImVec2 size{(float) m_resolution.x, (float) m_resolution.y};
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y -= ImGui::GetFrameHeightWithSpacing() * 2;  // reserved for the status bar
    // Scale the image to fit the available space
    m_scale = std::min(avail.x / size.x, avail.y / size.y);
    size = {size.x * m_scale, size.y * m_scale};
    m_leftTop = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID) (uintptr_t) m_framebuffer.getTexture(), size);
    // ImGui::Image((ImTextureID) (uintptr_t) cmap_tex_ids[1], size);

    if ((m_isHovered = ImGui::IsItemHovered())) {
        m_mousePixel = Point2i((int) ((ImGui::GetMousePos().x - m_leftTop.x) / m_scale),
                               (int) ((ImGui::GetMousePos().y - m_leftTop.y) / m_scale));
    }
}
