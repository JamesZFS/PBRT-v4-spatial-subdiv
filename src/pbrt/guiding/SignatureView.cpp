//
// Created by fengshi on 11/21/24.
//

#include "SignatureView.h"
#include <implot.h>

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
    std::iota(m_barXs, m_barXs + PGL_SIGNATURE_SIZE, 0);
    std::iota(m_barRXs, m_barRXs + PGL_SIGNATURE_SIZE, 0.5f);
}

SignatureView::~SignatureView() {
    glDeleteTextures(1, &m_cachedSignatureTex);
    glDeleteTextures(1, &m_integratedSignatureTex);
    glDeleteTextures(1, &m_selectionTex);
}

void SignatureView::Update(const pbrt::Point3f &pos) {
    m_prev = {true, pos};
    Update();
}

void SignatureView::Update() {
    pgl_point3f pglP = {m_prev.pos.x, m_prev.pos.y, m_prev.pos.z};
    m_cachedSignature = m_field.GetDirectionalSignature(pglP);
    m_cachedSignaturesLR = m_field.GetLRDirectionalSignatures(pglP, m_splitDimension);
}

void SignatureView::Clear() {
    m_prev.valid = false;
    m_cachedSignature = {};
    m_cachedSignaturesLR = {};
}

void SignatureView::Draw() {
    if (ImGui::BeginTabBar("ViewMode")) {
        if (ImGui::BeginTabItem("LR")) {
            DrawLR();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Bars")) {
            DrawBars();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Colored")) {
            DrawColored();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void SignatureView::DrawColored() {
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
                ImGui::Text("Std: %.2e", signature.std[idx]);
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
        ImGui::Text("Cached: %.4f  Std: %.2e  Integrated: %.4f", m_cachedSignature.signature[idx], m_cachedSignature.std[idx], m_integratedSignature.signature[idx]);
    }
}

void SignatureView::DrawBars() {
    ImGui::Checkbox("Variance", &m_showVariance);
    ImGui::SameLine();
    ImGui::Checkbox("Integrated Signature", &m_showIntegratedSignature);
    auto flags = ImPlotFlags_NoLegend | ImPlotFlags_NoTitle;
    if (ImPlot::BeginPlot("Signature Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-0.25, PGL_SIGNATURE_SIZE - 0.25, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::PlotBars("Cached", m_cachedSignature.signature, PGL_SIGNATURE_SIZE, 0.5);
        if (m_showVariance) {
            ImPlot::PlotErrorBars("Std", m_barXs, m_cachedSignature.signature, m_cachedSignature.std, PGL_SIGNATURE_SIZE);
        }
        if (m_showIntegratedSignature)
            ImPlot::PlotBars("Integrated", m_integratedSignature.signature, PGL_SIGNATURE_SIZE, 0.5, 0.5);

        // Interaction: display a vertical marker at the clicked bin and select it from the radiance view
        if (ImPlot::IsAxisHovered(ImAxis_X1) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            double x = ImPlot::GetPlotMousePos().x + 0.25;
            if (x >= 0 && x < PGL_SIGNATURE_SIZE) {
                m_radianceView.SetSelectedBinIndex((uint8_t) x);
            }
        }
        if (m_radianceView.HasSelectedBinIndex()) {
            double x = m_radianceView.GetSelectedBinIndex();
            ImPlot::TagX(x, ImVec4(1, 0, 0, 0.5));
        }
        ImPlot::EndPlot();
    }
    if (m_radianceView.HasSelectedBinIndex()) {
        uint8_t idx = m_radianceView.GetSelectedBinIndex();
        ImGui::Text("Cached: %.4f  Std: %.2e  Integrated: %.4f", m_cachedSignature.signature[idx], m_cachedSignature.std[idx], m_integratedSignature.signature[idx]);
    }
}

void SignatureView::DrawLR() {
    ImGui::Checkbox("Variance", &m_showVariance);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    if (ImGui::Combo("Dimension", &m_splitDimension, "x\0y\0z\0best")) {
        if (m_prev.valid) Update();
    }

    auto flags = ImPlotFlags_NoLegend | ImPlotFlags_NoTitle;
    if (ImPlot::BeginPlot("LR Signatures Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - 2 * ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-0.25, PGL_SIGNATURE_SIZE - 0.25, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::PlotBars("Left", m_cachedSignaturesLR.first.signature, PGL_SIGNATURE_SIZE, 0.5);
        ImPlot::PlotBars("Right", m_cachedSignaturesLR.second.signature, PGL_SIGNATURE_SIZE, 0.5, 0.5);
        if (m_showVariance) {
            ImPlot::PlotErrorBars("Left-std", m_barXs, m_cachedSignaturesLR.first.signature, m_cachedSignaturesLR.first.std, PGL_SIGNATURE_SIZE);
            ImPlot::PlotErrorBars("Right-std", m_barRXs, m_cachedSignaturesLR.second.signature, m_cachedSignaturesLR.second.std, PGL_SIGNATURE_SIZE);
        }

        // Interaction: display a vertical marker at the clicked bin and select it from the radiance view
        if (ImPlot::IsAxisHovered(ImAxis_X1) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            double x = ImPlot::GetPlotMousePos().x + 0.25;
            if (x >= 0 && x < PGL_SIGNATURE_SIZE) {
                m_radianceView.SetSelectedBinIndex((uint8_t) x);
            }
        }
        if (m_radianceView.HasSelectedBinIndex()) {
            double x = m_radianceView.GetSelectedBinIndex();
            ImPlot::TagX(x, ImVec4(1, 0, 0, 0.5));
        }
        ImPlot::EndPlot();
    }
    ImGui::Text("Number of samples left / right: %s / %s",
        FormatInteger((int) m_cachedSignaturesLR.first.numSamples).c_str(),
        FormatInteger((int) m_cachedSignaturesLR.second.numSamples).c_str());
    if (m_radianceView.HasSelectedBinIndex()) {
        uint8_t idx = m_radianceView.GetSelectedBinIndex();
        ImGui::Text("Left / std: %.4f / %.2e, right / std: %.4f / %.3e",
            m_cachedSignaturesLR.first.signature[idx], m_cachedSignaturesLR.first.std[idx],
            m_cachedSignaturesLR.second.signature[idx], m_cachedSignaturesLR.second.std[idx]);
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