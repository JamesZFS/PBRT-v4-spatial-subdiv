//
// Created by fengshi on 10/9/24.
//

#include "SamplingDistributionView.h"

#include <pbrt/util/error.h>
#include <pbrt/util/parallel.h>

using namespace pbrt;

SamplingDistributionView::SamplingDistributionView(pbrt::Application *parent, const openpgl::cpp::Field &field) :
    View(parent), m_field(field), m_ssd(&field), m_framebuffer(m_resolution.x, m_resolution.y, PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag") {
    m_stepPhi = (2.0f * M_PI) / (float) m_resolution.x;
    m_stepTheta = (M_PI) / (float) m_resolution.y;
    m_cpuBuffer.pdf.resize(m_resolution.x * m_resolution.y);
    m_colormaps[Buffer_PDF] = CMap_Viridis;
#ifdef OPENPGL_RADIANCE_CACHES
    m_cpuBuffer.Li.resize(m_resolution.x * m_resolution.y);
    m_cpuBuffer.Lo.resize(m_resolution.x * m_resolution.y);
    m_colormaps[Buffer_Li] = m_colormaps[Buffer_Lo] = CMap_None;
#endif
    glGenTextures(1, &m_renderingTex);
}

SamplingDistributionView::~SamplingDistributionView() {
    glDeleteTextures(1, &m_renderingTex);
}

void SamplingDistributionView::UpdateCPUBuffer(const pbrt::Point3f &pos, const pbrt::Normal3f &normal, bool lookahead) {
    m_prev = {true, pos, normal, lookahead};
    UpdateCPUBuffer();
}

void SamplingDistributionView::UpdateFramebuffer() {
    // Render to the tonemapped framebuffer if the CPU buffer has been updated
    if (m_cpuBufferUpdated.exchange(false)) {
        switch (m_selectedBuffer) {
            case Buffer_PDF:
                UpdateTextureFromFloatData((GLuint) (uintptr_t) m_renderingTex, m_cpuBuffer.pdf.data(), m_resolution.x, m_resolution.y, true);
                break;
#ifdef OPENPGL_RADIANCE_CACHES
            case Buffer_Li:
                UpdateTextureFromRGBData((GLuint) (uintptr_t) m_renderingTex, m_cpuBuffer.Li.data(), m_resolution.x, m_resolution.y, true);
                break;
            case Buffer_Lo:
                UpdateTextureFromRGBData((GLuint) (uintptr_t) m_renderingTex, m_cpuBuffer.Lo.data(), m_resolution.x, m_resolution.y, true);
                break;
#endif
            default:
                Error("Unknown buffer type %d", (int) m_selectedBuffer);
                break;
        }
    }

    m_framebuffer.bind();
    m_framebuffer.clear();

    Shader &shader = m_framebuffer.getShader();
    shader.bind();
    ConfigureTonemapShader(shader, m_renderingTex, m_selectedBuffer == Buffer_PDF, {
                               m_exposure, 0.0f, std::numeric_limits<float>::infinity(),
                               cmap_tex_ids[m_colormaps[m_selectedBuffer]]
                           });

    // Render!
    m_framebuffer.draw();
    m_framebuffer.unbind();
}

void SamplingDistributionView::Clear() {
    m_prev.valid = false;
    std::fill(m_cpuBuffer.pdf.begin(), m_cpuBuffer.pdf.end(), 0);
#ifdef OPENPGL_RADIANCE_CACHES
    std::fill(m_cpuBuffer.Li.begin(), m_cpuBuffer.Li.end(), RGB(0, 0, 0));
    std::fill(m_cpuBuffer.Lo.begin(), m_cpuBuffer.Lo.end(), RGB(0, 0, 0));
#endif
    m_cpuBufferUpdated = true;
}

void SamplingDistributionView::Draw() {
#ifdef OPENPGL_RADIANCE_CACHES
    static const char *bufferNames[Buffer_Count] = {"PDF", "Incident Radiance", "Outgoing Radiance"};
    ImGui::SetNextItemWidth(90);
    if (ImGui::Combo("Type", reinterpret_cast<int *>(&m_selectedBuffer), bufferNames, Buffer_Count)) {
        m_cpuBufferUpdated = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
#endif
    ImGui::Combo("Tonemap", reinterpret_cast<int *>(&m_colormaps[m_selectedBuffer]), cmap_names, CMap_Count);
    ImGui::SetNextItemWidth(150);
    ImGui::DragFloat("Exposure", &m_exposure, 0.01f, 0, 0, "%.4f");
    m_exposure = std::max(m_exposure, 0.0f);
    bool needsUpdate = false;
    needsUpdate |= ImGui::Checkbox("Cosine Product", &m_enableCosineProduct);
    ImGui::SameLine();
    needsUpdate |= ImGui::Checkbox("Local Frame", &m_localFrame);

    if (needsUpdate) {
        if (m_prev.valid) UpdateCPUBuffer();
    }
    UpdateFramebuffer();

    ImVec2 size{(float) m_resolution.x, (float) m_resolution.y};
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // Scale the image to fit the available space
    float scale = std::min(avail.x / size.x, avail.y / size.y);
    size = {size.x * scale, size.y * scale};
    ImVec2 leftTop = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID) (uintptr_t) m_framebuffer.getTexture(), size);

    // Hovering: show value at the pixel
    if (ImGui::IsItemHovered()) {
        auto pos = ImGui::GetMousePos();
        Point2i pixel((int) ((pos.x - leftTop.x) / scale), (int) ((pos.y - leftTop.y) / scale));
        int idx = pixel.y * m_resolution.x + pixel.x;
        if (ImGui::BeginTooltip()) {
            float theta = m_stepTheta * (0.5f + float(pixel.y));
            float phi = m_stepPhi * (0.5f + float(pixel.x));
            ImGui::Text("Omega: (%.1f, %.1f) deg", Degrees(theta), Degrees(phi));
            switch (m_selectedBuffer) {
                case Buffer_PDF: {
                    float value = m_cpuBuffer.pdf[idx];
                    ImGui::Text("PDF: %.6f", value);
                    break;
                }
#ifdef OPENPGL_RADIANCE_CACHES
                case Buffer_Li: {
                    RGB li = m_cpuBuffer.Li[idx];
                    ImGui::Text("Li: (%.4f, %.4f, %.4f)", li.r, li.g, li.b);
                    break;
                }
                case Buffer_Lo: {
                    RGB lo = m_cpuBuffer.Lo[idx];
                    ImGui::Text("Lo: (%.4f, %.4f, %.4f)", lo.r, lo.g, lo.b);
                    break;
                }
#endif
                default:
                    Error("Unknown buffer type %d", (int) m_selectedBuffer);
                    break;
            }
            ImGui::EndTooltip();
        }
    }
}

void SamplingDistributionView::UpdateCPUBuffer() {
    auto pos = m_prev.pos;
    auto normal = m_prev.normal;
    pgl_point3f pglP = {pos.x, pos.y, pos.z};
    float rnd = -1;
    bool success = false;
    if (m_prev.lookahead)
        success = m_ssd.Init<true>(&m_field, pglP, rnd);
    else
        success = m_ssd.Init<false>(&m_field, pglP, rnd);
    if (success) {
        if (m_enableCosineProduct) {
            pgl_vec3f pglN = {normal.x, normal.y, normal.z};
            m_ssd.ApplyCosineProduct(pglN);
        }
        auto frame = Frame::FromZ(normal);

        ParallelFor2D(Bounds2i({0, 0}, m_resolution), [&](Point2i p) {
            int idx = (p.y * m_resolution.x) + p.x;
            float theta = m_stepTheta * (0.5f + float(p.y));
            float phi = m_stepPhi * (0.5f + float(p.x));
            Vector3f dir = SphericalDirection(std::sin(theta), std::cos(theta), phi);
            if (m_localFrame)
                dir = frame.FromLocal(dir);
            pgl_vec3f pglDir{dir.x, dir.y, dir.z};

            float pdf = m_ssd.PDF(pglDir);
            m_cpuBuffer.pdf[idx] = pdf;
#ifdef OPENPGL_RADIANCE_CACHES
            pgl_vec3f li = m_ssd.IncomingRadiance(pglDir, false);
            pgl_vec3f lo = m_ssd.OutgoingRadiance(pglDir);
            m_cpuBuffer.Li[idx] = RGB(li.x, li.y, li.z);
            m_cpuBuffer.Lo[idx] = RGB(lo.x, lo.y, lo.z);
#endif
        });

        m_cpuBufferUpdated = true;
    } else {
        Clear();
    }
}
