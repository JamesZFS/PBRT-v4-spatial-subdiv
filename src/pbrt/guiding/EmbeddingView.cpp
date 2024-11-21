//
// Created by fengshi on 11/21/24.
//

#include "EmbeddingView.h"

using namespace pbrt;

EmbeddingView::EmbeddingView(pbrt::Application *parent)
    : View(parent), m_framebuffer(PGL_EMBEDDING_SIZE, 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag") {
    glGenTextures(1, &m_renderingTex);
}

EmbeddingView::~EmbeddingView() {
    glDeleteTextures(1, &m_renderingTex);
}

void EmbeddingView::Draw() {
    ImGui::PushID("EmbeddingView");
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if ((m_isActive = ImGui::CollapsingHeader("Embedding View"))) {
        ImGui::DragFloat("Scale", &m_scale, 0.01f, 0, 0, "%.8f");
        if (ImGui::Button("Reset")) {
            m_scale = 1.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Normalize")) {
            float emax = -std::numeric_limits<float>::infinity();
            for (const auto &e : m_embedding.embedding) {
                emax = std::max(emax, e);
            }
            m_scale = 1.0f / std::max(1e-6f, emax);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::Combo("Tonemap", reinterpret_cast<int *>(&m_cmap), cmap_names, CMap_Count);

        UpdateFramebuffer();

        auto leftTop = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID) (uintptr_t) m_framebuffer.getTexture(), ImVec2(ImGui::GetColumnWidth(), ImGui::GetFrameHeight()));

        // Hovering: show value at the pixel
        if (ImGui::IsItemHovered()) {
            auto pos = ImGui::GetMousePos();
            float x = (pos.x - leftTop.x) / ImGui::GetColumnWidth();
            uint8_t idx = std::min((uint8_t) (x * PGL_EMBEDDING_SIZE), (uint8_t) (PGL_EMBEDDING_SIZE - 1));
            if (ImGui::BeginTooltip()) {
                ImGui::Text("Index: %d", idx);
                ImGui::Text("Value: %.4f", m_embedding.embedding[idx]);
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::PopID();
}

void EmbeddingView::UpdateFramebuffer() {
    UpdateTextureFromFloatData((GLuint) (uintptr_t) m_renderingTex, m_embedding.embedding, PGL_EMBEDDING_SIZE, 1, false);
    m_framebuffer.bind();
    m_framebuffer.clear();

    Shader &shader = m_framebuffer.getShader();
    shader.bind();
    ConfigureTonemapShader(shader, m_renderingTex, true, {
                               m_scale, 0, std::numeric_limits<float>::infinity(),
                               cmap_tex_ids[m_cmap]
                           });

    // Render!
    m_framebuffer.draw();
    m_framebuffer.unbind();
}

