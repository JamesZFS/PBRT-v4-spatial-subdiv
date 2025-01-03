//
// Created by fengshi on 11/21/24.
//

#include "SignatureView.h"
#include <implot.h>

#include "Application.h"

using namespace pbrt;

constexpr float barSize = 0.4;

SignatureView::SignatureView(pbrt::Application *parent, const openpgl::cpp::Field &field, RadianceView &radianceView)
    : View(parent), m_field(field), m_radianceView(radianceView), m_integratedSignature(radianceView.integratedSignature),
      m_cachedSignatureFramebuffer(pglGetSignatureSize(), 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag"),
      m_integratedSignatureFramebuffer(pglGetSignatureSize(), 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag"),
      m_selectionFramebuffer(pglGetSignatureSize(), 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag") {
    glGenTextures(1, &m_cachedSignatureTex);
    glGenTextures(1, &m_integratedSignatureTex);
    glGenTextures(1, &m_selectionTex);
    memset(m_selectionBuffer, 0, sizeof(m_selectionBuffer));
    std::iota(m_barXs, m_barXs + PGL_SIGNATURE_MAX_SIZE, 0);
    std::iota(m_barRXs, m_barRXs + PGL_SIGNATURE_MAX_SIZE, barSize);
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

void SignatureView::Rescale() {
    m_cachedSignatureFramebuffer.rescale(pglGetSignatureSize(), 1);
    m_integratedSignatureFramebuffer.rescale(pglGetSignatureSize(), 1);
    m_selectionFramebuffer.rescale(pglGetSignatureSize(), 1);
}

void SignatureView::Update() {
    pgl_point3f pglP = {m_prev.pos.x, m_prev.pos.y, m_prev.pos.z};
    m_cachedSignature = m_field.GetDirectionalSignature(pglP);
    m_cachedSignaturesLR = m_field.GetLRDirectionalSignatures(pglP, m_splitDimension);
    m_bestDimension = m_field.GetDirectionalSignatureBestDim(pglP);
}

void SignatureView::Clear() {
    m_prev.valid = false;
    m_cachedSignature = {};
    m_cachedSignaturesLR = {};
    m_bestDimension = 3;
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

// Needs to align with Signature.h
static float getDistanceSMAPE(const PGLDirectionalSignature &a, const PGLDirectionalSignature &b, float stdMultiplier) {
    float sum = 0;
    for (uint8_t i = 0; i < pglGetSignatureSize(); i++) {
        float ai = a.signature[i], bi = b.signature[i];
        float a_std = stdMultiplier * a.std[i], b_std = stdMultiplier * b.std[i];
        // accumulate when interval [ai-a_std, ai+a_std] and [bi-b_std, bi+b_std] not overlap
        if (ai - a_std > bi + b_std)
            sum += 2.0f * (ai - bi - a_std - b_std) / (ai + bi);
        else if (ai + a_std < bi - b_std)
            sum += 2.0f * (bi - ai - a_std - b_std) / (ai + bi);
    }
    return sum / (float) pglGetSignatureSize();
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
            uint8_t idx = std::min((uint8_t) (x * pglGetSignatureSize()), (uint8_t) (pglGetSignatureSize() - 1));
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
    ImGui::Checkbox("Std", &m_showStd);
    ImGui::SameLine();
    ImGui::Checkbox("Integrated Signature", &m_showIntegratedSignature);
    auto flags = ImPlotFlags_NoLegend | ImPlotFlags_NoTitle;
    if (ImPlot::BeginPlot("Signature Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, pglGetSignatureSize() - 1 + 1.5*barSize, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::PlotBars("Cached", m_cachedSignature.signature, pglGetSignatureSize(), barSize);
        if (m_showStd) {
            ImPlot::PlotErrorBars("Std", m_barXs, m_cachedSignature.signature, m_cachedSignature.std, pglGetSignatureSize());
        }
        if (m_showIntegratedSignature)
            ImPlot::PlotBars("Integrated", m_integratedSignature.signature, pglGetSignatureSize(), barSize, barSize);

        // Interaction: display a vertical marker at the clicked bin and select it from the radiance view
        if (ImPlot::IsAxisHovered(ImAxis_X1) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            double x = ImPlot::GetPlotMousePos().x + barSize / 2;
            if (x >= 0 && x < pglGetSignatureSize()) {
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
    ImGui::Checkbox("Std", &m_showStd);
    ImGui::SameLine();
    ImGui::Checkbox("Multiplied Std", &m_showMultipliedStd);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    if (ImGui::Combo("Dimension", &m_splitDimension, "x\0y\0z\0best\0")) {
        if (m_prev.valid) Update();
    }

    static float left_std[PGL_SIGNATURE_MAX_SIZE], right_std[PGL_SIGNATURE_MAX_SIZE];
    auto flags = ImPlotFlags_NoLegend | ImPlotFlags_NoTitle;
    if (ImPlot::BeginPlot("LR Signatures Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - 2 * ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, pglGetSignatureSize() - 1 + 1.5*barSize, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::PlotBars("Left", m_cachedSignaturesLR.first.signature, pglGetSignatureSize(), barSize);
        ImPlot::PlotBars("Right", m_cachedSignaturesLR.second.signature, pglGetSignatureSize(), barSize, barSize);
        if (m_showStd) {
            ImPlot::PlotErrorBars("Left-std", m_barXs, m_cachedSignaturesLR.first.signature, m_cachedSignaturesLR.first.std, pglGetSignatureSize());
            ImPlot::PlotErrorBars("Right-std", m_barRXs, m_cachedSignaturesLR.second.signature, m_cachedSignaturesLR.second.std, pglGetSignatureSize());
        }
        if (m_showMultipliedStd) {
            float multiplier = m_parent->GetSignatureStdMultiplier();
            for (int i = 0; i < pglGetSignatureSize(); ++i) {
                left_std[i] = m_cachedSignaturesLR.first.std[i] * multiplier;
                right_std[i] = m_cachedSignaturesLR.second.std[i] * multiplier;
            }
            ImPlot::PushStyleVar(ImPlotStyleVar_ErrorBarSize, 8.0f);
            ImPlot::PushStyleColor(ImPlotCol_ErrorBar, ImVec4(1, 1, 0, 1));
            ImPlot::PlotErrorBars("Left-std-", m_barXs, m_cachedSignaturesLR.first.signature, left_std, pglGetSignatureSize());
            ImPlot::PlotErrorBars("Right-std-", m_barRXs, m_cachedSignaturesLR.second.signature, right_std, pglGetSignatureSize());
            ImPlot::PopStyleColor();
            ImPlot::PopStyleVar();
        }

        // Interaction: display a vertical marker at the clicked bin and select it from the radiance view
        if (ImPlot::IsAxisHovered(ImAxis_X1) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            double x = ImPlot::GetPlotMousePos().x + barSize / 2;
            if (x >= 0 && x < pglGetSignatureSize()) {
                m_radianceView.SetSelectedBinIndex((uint8_t) x);
            }
        }
        if (m_radianceView.HasSelectedBinIndex()) {
            double x = m_radianceView.GetSelectedBinIndex();
            ImPlot::TagX(x, ImVec4(1, 0, 0, 0.5));
        }
        ImPlot::EndPlot();
    }
    // Compute distance
    ImGui::Text("Number of samples left / right: %s / %s",
        FormatInteger((int) m_cachedSignaturesLR.first.numSamples).c_str(),
        FormatInteger((int) m_cachedSignaturesLR.second.numSamples).c_str());
    if (m_radianceView.HasSelectedBinIndex()) {
        uint8_t idx = m_radianceView.GetSelectedBinIndex();
        ImGui::Text("Left / std: %.4f / %.2e, right / std: %.4f / %.3e",
            m_cachedSignaturesLR.first.signature[idx], m_cachedSignaturesLR.first.std[idx],
            m_cachedSignaturesLR.second.signature[idx], m_cachedSignaturesLR.second.std[idx]);
    } else {
        float energy = getDistanceSMAPE(m_cachedSignaturesLR.first, m_cachedSignaturesLR.second, m_parent->GetSignatureStdMultiplier());
        if (m_splitDimension == 3) {
            if (m_bestDimension == 3) {
                ImGui::Text("Invalid");
            } else {
                static const char dim_ch[] = {'x', 'y', 'z'};
                ImGui::Text("Best Dimension: %c  Energy: %.4f", dim_ch[m_bestDimension], energy);
            }
        } else {
            ImGui::Text("Energy: %.4f", energy);
        }
    }
}

void SignatureView::UpdateFramebuffer() {
    auto render = [&](Framebuffer &fb, GLuint tex, const PGLDirectionalSignature &signature) {
        UpdateTextureFromFloatData(tex, signature.signature, pglGetSignatureSize(), 1, false);
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
    for (int i = 0; i < pglGetSignatureSize(); ++i) {
        if (m_radianceView.GetSelectedBinIndex() == i) {
            m_selectionBuffer[i] = RGB(1, 0, 0);
        } else {
            m_selectionBuffer[i] = RGB(0, 0, 0);
        }
    }
    UpdateTextureFromRGBData((GLuint) (uintptr_t) m_selectionTex, m_selectionBuffer, pglGetSignatureSize(), 1, false);

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