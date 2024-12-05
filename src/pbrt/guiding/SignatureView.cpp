//
// Created by fengshi on 11/21/24.
//

#include "SignatureView.h"

using namespace pbrt;

SignatureView::SignatureView(pbrt::Application *parent, const openpgl::cpp::Field &field, RadianceView &radianceView)
    : View(parent), m_field(field), m_radianceView(radianceView), m_integratedSignature(radianceView.integratedSignature),
      m_cachedSignatureFramebuffer(PGL_SIGNATURE_SIZE, 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag"),
      m_integratedSignatureFramebuffer(PGL_SIGNATURE_SIZE, 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag"),
      m_selectionFramebuffer(PGL_SIGNATURE_SIZE, 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag") {
    glGenTextures(1, &m_cachedSignatureTex);
    glGenTextures(1, &m_integratedSignatureTex);
    glGenTextures(1, &m_selectionTex);
    memset(m_selectionBuffer, 0, sizeof(m_selectionBuffer));
}

SignatureView::~SignatureView() {
    glDeleteTextures(1, &m_cachedSignatureTex);
    glDeleteTextures(1, &m_integratedSignatureTex);
    glDeleteTextures(1, &m_selectionTex);
}

void SignatureView::Update(const pbrt::Point3f &pos, bool lookahead) {
    pgl_point3f pglP = {pos.x, pos.y, pos.z};
    m_cachedSignature = m_field.GetDirectionalSignature(pglP);
}

void SignatureView::Clear() {
    m_cachedSignature = {};
}

void SignatureView::Draw() {
    if (ImGui::Button("Reset")) {
        m_scale = 1.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Normalize")) {
        float emax = -std::numeric_limits<float>::infinity();
        for (const auto &e : m_cachedSignature.signature) {
            emax = std::max(emax, e);
        }
        m_scale = 1.0f / std::max(1e-6f, emax);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::DragFloat("Scale", &m_scale, 0.005f, 0, 0, "%.8f");
    ImGui::SameLine();
    ImGui::Checkbox("Integrated Signature", &m_showIntegratedSignature);
    // ImGui::SameLine();
    // ImGui::SetNextItemWidth(90);
    // ImGui::Combo("Tonemap", reinterpret_cast<int *>(&m_cmap), cmap_names, CMap_Count);

    UpdateFramebuffer();

    // Selection indicator
    ImGui::Image((ImTextureID) (uintptr_t) m_selectionFramebuffer.getTexture(), ImVec2(ImGui::GetColumnWidth(), 0.25f * ImGui::GetFrameHeight()));

    auto drawSignature = [&](const char *label, Framebuffer &fb, const PGLDirectionalSignature &signature) {
        auto leftTop = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID) (uintptr_t) fb.getTexture(), ImVec2(ImGui::GetColumnWidth(), 2 * ImGui::GetFrameHeight()));

        // Hovering: show value at the pixel
        // Also highlight the associated pixels in the radiance view
        if (ImGui::IsItemHovered()) {
            auto pos = ImGui::GetMousePos();
            float x = (pos.x - leftTop.x) / ImGui::GetColumnWidth();
            uint8_t idx = std::min((uint8_t) (x * PGL_SIGNATURE_SIZE), (uint8_t) (PGL_SIGNATURE_SIZE - 1));
            if (ImGui::BeginTooltip()) {
                ImGui::Text("Bin index: %d", idx);
                ImGui::Text("%s value: %.4f", label, signature.signature[idx]);
                ImGui::Text("Variance: %.4f", signature.variance[idx]);
                ImGui::EndTooltip();
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, true)) {
                m_radianceView.SetSelectedBinIndex(idx);
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                m_radianceView.ResetSelectedBinIndex();
            }
        }
    };

    // Cached Signature
    drawSignature("Cached", m_cachedSignatureFramebuffer, m_cachedSignature);

    // Integrated Signature
    if (m_showIntegratedSignature)
        drawSignature("Integrated", m_integratedSignatureFramebuffer, m_integratedSignature);

    if (m_radianceView.HasSelectedBinIndex()) {
        uint8_t idx = m_radianceView.GetSelectedBinIndex();
        ImGui::Text("Selected bin: %d  Cached value: %.4f  Integrated value: %.4f", idx, m_cachedSignature.signature[idx], m_integratedSignature.signature[idx]);
    }
}

void SignatureView::UpdateFramebuffer() {
    auto render = [&](Framebuffer &fb, GLuint tex, const PGLDirectionalSignature &signature) {
        UpdateTextureFromFloatData(tex, signature.signature, PGL_SIGNATURE_SIZE, 1, false);
        fb.bind();
        fb.clear();
        Shader &shader = fb.getShader();
        shader.bind();
        ConfigureTonemapShader(shader, tex, true, {
                                   m_scale, 0, std::numeric_limits<float>::infinity(),
                                   cmap_tex_ids[m_cmap]
                               });
        fb.draw();
        fb.unbind();
    };

    // Cached signature buffer
    render(m_cachedSignatureFramebuffer, m_cachedSignatureTex, m_cachedSignature);

    // Integrated signature buffer
    if (m_showIntegratedSignature)
        render(m_integratedSignatureFramebuffer, m_integratedSignatureTex, m_integratedSignature);

    // Selection buffer
    for (int i = 0; i < PGL_SIGNATURE_SIZE; ++i) {
        if (m_radianceView.GetSelectedBinIndex() == i) {
            m_selectionBuffer[i] = RGB(1, 0, 0);
        } else {
            m_selectionBuffer[i] = RGB(0, 0, 0);
        }
    }
    UpdateTextureFromRGBData((GLuint) (uintptr_t) m_selectionTex, m_selectionBuffer, PGL_SIGNATURE_SIZE, 1, false);

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

