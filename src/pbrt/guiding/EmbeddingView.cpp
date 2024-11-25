//
// Created by fengshi on 11/21/24.
//

#include "EmbeddingView.h"

using namespace pbrt;

EmbeddingView::EmbeddingView(pbrt::Application *parent, const openpgl::cpp::Field &field, RadianceView &radianceView)
    : View(parent), m_field(field), m_radianceView(radianceView),
      m_embeddingFramebuffer(PGL_EMBEDDING_SIZE, 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag"),
      m_selectionFramebuffer(PGL_EMBEDDING_SIZE, 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag") {
    glGenTextures(1, &m_embeddingTex);
    glGenTextures(1, &m_selectionTex);
    memset(m_selectionBuffer, 0, sizeof(m_selectionBuffer));
}

EmbeddingView::~EmbeddingView() {
    glDeleteTextures(1, &m_embeddingTex);
    glDeleteTextures(1, &m_selectionTex);
}

void EmbeddingView::Update(const pbrt::Point3f &pos, bool lookahead) {
    pgl_point3f pglP = {pos.x, pos.y, pos.z};
    auto [coarse, fine] = m_field.GetCoarseFineRegionStatisticsSurface(pglP);
    if (lookahead && fine.id != -1) {
        m_embedding = m_field.GetDirectionalEmbedding(fine.id);
    } else if (!lookahead && coarse.id != -1) {
        m_embedding = m_field.GetDirectionalEmbedding(coarse.id);
    } else {
        m_embedding = {};
    }
    m_embeddingUpdated = true;
}

void EmbeddingView::Clear() {
    m_embedding = {};
    m_embeddingUpdated = true;
}

void EmbeddingView::Draw() {
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
    ImGui::DragFloat("Scale", &m_scale, 0.005f, 0, 0, "%.8f");
    // ImGui::SameLine();
    // ImGui::SetNextItemWidth(90);
    // ImGui::Combo("Tonemap", reinterpret_cast<int *>(&m_cmap), cmap_names, CMap_Count);

    UpdateFramebuffer();

    auto leftTop = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID) (uintptr_t) m_embeddingFramebuffer.getTexture(), ImVec2(ImGui::GetColumnWidth(), 2 * ImGui::GetFrameHeight()));

    // Hovering: show value at the pixel
    // Also highlight the associated pixels in the radiance view
    if (ImGui::IsItemHovered()) {
        auto pos = ImGui::GetMousePos();
        float x = (pos.x - leftTop.x) / ImGui::GetColumnWidth();
        uint8_t idx = std::min((uint8_t) (x * PGL_EMBEDDING_SIZE), (uint8_t) (PGL_EMBEDDING_SIZE - 1));
        if (ImGui::BeginTooltip()) {
            ImGui::Text("Index: %d", idx);
            ImGui::Text("Value: %.4f", m_embedding.embedding[idx]);
            ImGui::EndTooltip();
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, true)) {
            m_radianceView.SetSelectedBinIndex(idx);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_radianceView.ResetSelectedBinIndex();
        }
    }

    // Selection indicator
    ImGui::Image((ImTextureID) (uintptr_t) m_selectionFramebuffer.getTexture(), ImVec2(ImGui::GetColumnWidth(), 0.25f * ImGui::GetFrameHeight()));
    if (m_radianceView.HasSelectedBinIndex()) {
        uint8_t idx = m_radianceView.GetSelectedBinIndex();
        ImGui::Text("Selected bin: %d  Value: %f", idx, m_embedding.embedding[idx]);
    }
}

void EmbeddingView::UpdateFramebuffer() {
    // Embedding buffer
    if (m_embeddingUpdated.exchange(false))
        UpdateTextureFromFloatData((GLuint) (uintptr_t) m_embeddingTex, m_embedding.embedding, PGL_EMBEDDING_SIZE, 1, false);

    m_embeddingFramebuffer.bind();
    m_embeddingFramebuffer.clear();
    Shader &shader = m_embeddingFramebuffer.getShader();
    shader.bind();
    ConfigureTonemapShader(shader, m_embeddingTex, true, {
                               m_scale, 0, std::numeric_limits<float>::infinity(),
                               cmap_tex_ids[m_cmap]
                           });
    m_embeddingFramebuffer.draw();
    m_embeddingFramebuffer.unbind();

    // Selection buffer
    for (int i = 0; i < PGL_EMBEDDING_SIZE; ++i) {
        if (m_radianceView.GetSelectedBinIndex() == i) {
            m_selectionBuffer[i] = RGB(1, 0, 0);
        } else {
            m_selectionBuffer[i] = RGB(0, 0, 0);
        }
    }
    UpdateTextureFromRGBData((GLuint) (uintptr_t) m_selectionTex, m_selectionBuffer, PGL_EMBEDDING_SIZE, 1, false);

    m_selectionFramebuffer.bind();
    m_selectionFramebuffer.clear();
    Shader &selectionShader = m_selectionFramebuffer.getShader();
    selectionShader.bind();
    ConfigureTonemapShader(selectionShader, m_selectionTex, false, {
                               1, 0, std::numeric_limits<float>::infinity(),
                               0
                           });
    m_selectionFramebuffer.draw();
    m_selectionFramebuffer.unbind();
}

