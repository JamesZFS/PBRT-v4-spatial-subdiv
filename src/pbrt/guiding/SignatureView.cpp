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
      m_cachedSignatureFramebuffer(NumBins(), 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag"),
      m_integratedSignatureFramebuffer(NumBins(), 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag"),
      m_selectionFramebuffer(NumBins(), 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag") {
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
    m_cachedSignatureFramebuffer.rescale(NumBins(), 1);
    m_integratedSignatureFramebuffer.rescale(NumBins(), 1);
    m_selectionFramebuffer.rescale(NumBins(), 1);
}

void SignatureView::Update() {
    pgl_point3f pglP = {m_prev.pos.x, m_prev.pos.y, m_prev.pos.z};
    // TODO: support selecting different models
    m_cachedSignatureParent = m_field.GetDirectionalSignatures(pglP, 0, 0, m_splitDim, m_isRight).first;
    m_cachedSignaturesLR = m_field.GetDirectionalSignatures(pglP, m_lookaheadDepth, 0, m_splitDim, m_isRight);
    m_cachedSignature = m_isRight ? m_cachedSignaturesLR.second : m_cachedSignaturesLR.first;
}

void SignatureView::Clear() {
    m_prev.valid = false;
    m_cachedSignatureParent = {};
    m_cachedSignaturesLR = {};
    m_cachedSignature = {};
    m_storedSignature = {};
    m_hasStoredSignature = false;
    m_splitDim = 3;
    m_isRight = false;
}

void SignatureView::Draw() {
    if (ImGui::BeginTabBar("ViewMode")) {
        if (ImGui::BeginTabItem("PC")) {
            DrawPC();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Comp")) {
            DrawComp();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Bars")) {
            DrawBars();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("LR")) {
            DrawLR();
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
    float num = 0, denom = 0;
    for (uint8_t i = 0; i < PGL_SIGNATURE_MAX_SIZE; i++) {
        float ai = a.signature[i], bi = b.signature[i];
        float a_std = stdMultiplier * a.std[i], b_std = stdMultiplier * b.std[i];
        // accumulate when interval [ai-a_std, ai+a_std] and [bi-b_std, bi+b_std] not overlap
        if (ai - a_std > bi + b_std)
            num += ai - bi - a_std - b_std;
        else if (ai + a_std < bi - b_std)
            num += bi - ai - a_std - b_std;
        denom += ai + bi;
    }
    return denom == 0 ? 0 : 2.0f * num / denom;
}

static float getDistanceTTest(const PGLDirectionalSignature &a, const PGLDirectionalSignature &b, float stdMultiplier, float tvalueThreshold) {
    float num = 0, denom = 0;
    for (uint8_t i = 0; i < PGL_SIGNATURE_MAX_SIZE; i++) {
        float ai = a.signature[i], bi = b.signature[i];
        float a_std = stdMultiplier * a.std[i], b_std = stdMultiplier * b.std[i];
        float sigma = std::sqrt(a.std[i] * a.std[i] + b.std[i] * b.std[i]);
        float t = sigma == 0 ? 0 : (ai - bi) / sigma;
        if (std::abs(t) > tvalueThreshold) {
            // accumulate when interval [ai-a_std, ai+a_std] and [bi-b_std, bi+b_std] not overlap
            if (ai - a_std > bi + b_std)
                num += ai - bi - a_std - b_std;
            else if (ai + a_std < bi - b_std)
                num += bi - ai - a_std - b_std;
        }
        denom += ai + bi;
    }
    return denom == 0 ? 0 : 2.0f * num / denom;
}

static float getWelchT(const PGLDirectionalSignature &a, const PGLDirectionalSignature &b, uint8_t idx) {
    if ((a.signature[idx] == 0 && a.std[idx] == 0) || (b.signature[idx] == 0 || b.std[idx] == 0)) return 0;
    float num = a.signature[idx] - b.signature[idx];
    float denom = std::sqrt(a.std[idx] * a.std[idx] + b.std[idx] * b.std[idx]);
    return denom == 0 ? 0 : num / denom;
}

void SignatureView::DrawColored() {
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("Depth", &m_lookaheadDepth, 1, 10)) {
        m_lookaheadDepth = std::clamp(m_lookaheadDepth, 0, (int) m_parent->GetSubdivCfg().lookaheadDepth);
        if (m_prev.valid) Update();
    }
    ImGui::SameLine();
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
            uint8_t idx = std::min((uint8_t) (x * NumBins()), (uint8_t) (NumBins() - 1));
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

void SignatureView::DrawComp() {
    if (ImGui::Button("Store") || IsKeyPressed(ImGuiKey_F2, false)) {
        m_storedSignature = m_integratedSignature;
        m_hasStoredSignature = true;
    }
    ImGui::SetItemTooltip("(F2) This will update the orange bars");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        m_storedSignature = {};
        m_hasStoredSignature = false;
    }
    ImGui::SetItemTooltip("This will clear the orange bars");
    auto flags = ImPlotFlags_NoLegend | ImPlotFlags_NoTitle;
    uint8_t S = NumBins();
    if (ImPlot::BeginPlot("Signature Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, S - 1 + 1.5*barSize, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::SetNextFillStyle(ImPlot::GetColormapColor(2));  // Green
        ImPlot::PlotBars("Current", m_integratedSignature.signature, S, barSize);
        ImPlot::SetNextFillStyle(ImPlot::GetColormapColor(1));  // Orange
        ImPlot::PlotBars("Stored", m_storedSignature.signature, S, barSize, barSize);

        BinInteraction();
        ImPlot::EndPlot();
    }
    if (m_radianceView.HasSelectedBinIndex()) {
        uint8_t idx = m_radianceView.GetSelectedBinIndex();
        if (m_hasStoredSignature) {
            float distance = getDistanceSMAPE(m_integratedSignature, m_storedSignature, 0.0f);
            ImGui::Text("Current: %.4f  Stored: %.4f  Distance: %.4f", m_integratedSignature.signature[idx], m_storedSignature.signature[idx], distance);
        } else {
            ImGui::Text("Current: %.4f  Stored: %.4f", m_integratedSignature.signature[idx], m_storedSignature.signature[idx]);
        }
    } else if (m_hasStoredSignature) {
        // Show the distance
        float distance = getDistanceSMAPE(m_integratedSignature, m_storedSignature, 0.0f);
        ImGui::Text("Distance: %.4f", distance);
    } else {
        float sum = 0.0f;
        for (uint8_t i = 0; i < S; i++) {
            sum += m_integratedSignature.signature[i];
        }
        ImGui::Text("Sum of bin value: %.4f", sum);
    }
}

void SignatureView::DrawBars() {
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("Depth", &m_lookaheadDepth, 1, 10)) {
        m_lookaheadDepth = std::clamp(m_lookaheadDepth, 0, (int) m_parent->GetSubdivCfg().lookaheadDepth);
        if (m_prev.valid) Update();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Std", &m_showStd);
    ImGui::SameLine();
    ImGui::Checkbox("Integrated Signature", &m_showIntegratedSignature);
    auto flags = ImPlotFlags_NoLegend | ImPlotFlags_NoTitle;
    uint8_t S = NumBins();
    if (ImPlot::BeginPlot("Signature Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, S - 1 + 1.5*barSize, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::PlotBars("Cached", m_cachedSignature.signature, S, barSize);
        if (m_showStd) {
            ImPlot::PlotErrorBars("Std", m_barXs, m_cachedSignature.signature, m_cachedSignature.std, S);
        }
        if (m_showIntegratedSignature)
            ImPlot::PlotBars("Integrated", m_integratedSignature.signature, S, barSize, barSize);

        BinInteraction();
        ImPlot::EndPlot();
    }
    if (m_radianceView.HasSelectedBinIndex()) {
        uint8_t idx = m_radianceView.GetSelectedBinIndex();
        ImGui::Text("Cached: %.4f  Std: %.2e  Integrated: %.4f", m_cachedSignature.signature[idx], m_cachedSignature.std[idx], m_integratedSignature.signature[idx]);
    }
}

void SignatureView::DrawPC() {
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("Depth", &m_lookaheadDepth, 1, 10)) {
        m_lookaheadDepth = std::clamp(m_lookaheadDepth, 0, (int) m_parent->GetSubdivCfg().lookaheadDepth);
        if (m_prev.valid) Update();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Std", &m_showStd);
    ImGui::SameLine();
    ImGui::Checkbox("T Value", &m_showTValue);
    ImGui::SameLine();
    ImGui::Checkbox("Multiplied Std", &m_showMultipliedStd);

    static float child_std[PGL_SIGNATURE_MAX_SIZE], parent_std[PGL_SIGNATURE_MAX_SIZE];
    auto flags = ImPlotFlags_NoLegend | ImPlotFlags_NoTitle;
    uint8_t S = NumBins();
    PGLDirectionalSignature childSignature = m_lookaheadDepth == 0 ? PGLDirectionalSignature() : m_cachedSignature;
    if (ImPlot::BeginPlot("Child/Parent Signatures Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - 2 * ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, S - 1 + 1.5*barSize, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::PlotBars("Child", childSignature.signature, S, barSize);
        ImPlot::PlotBars("Parent", m_cachedSignatureParent.signature, S, barSize, barSize);
        if (m_showStd) {
            ImPlot::PlotErrorBars("Child-std", m_barXs, childSignature.signature, childSignature.std, S);
            ImPlot::PlotErrorBars("Parent-std", m_barRXs, m_cachedSignatureParent.signature, m_cachedSignatureParent.std, S);
        }
        if (m_showMultipliedStd) {
            float multiplier = m_parent->GetSignatureStdMultiplier();
            for (int i = 0; i < S; ++i) {
                child_std[i] = childSignature.std[i] * multiplier;
                parent_std[i] = m_cachedSignatureParent.std[i] * multiplier;
            }
            ImPlot::PushStyleVar(ImPlotStyleVar_ErrorBarSize, 8.0f);
            ImPlot::PushStyleColor(ImPlotCol_ErrorBar, ImVec4(1, 1, 0, 1));
            ImPlot::PlotErrorBars("Left-std-", m_barXs, childSignature.signature, child_std, S);
            ImPlot::PlotErrorBars("Right-std-", m_barRXs, m_cachedSignatureParent.signature, parent_std, S);
            ImPlot::PopStyleColor();
            ImPlot::PopStyleVar();
        }
        if (m_showTValue) {
            for (int i = 0; i < S; ++i) {
                float t = getWelchT(m_cachedSignature, m_cachedSignatureParent, i);
                ImVec4 col = (std::isnan(t) || std::abs(t) <= m_parent->GetTValueThreshold()) ? ImVec4(0, 0, 0, 0) : ImVec4(1, 1, 0, 0.7);
                ImPlot::Annotation(m_barXs[i], m_cachedSignature.signature[i], col, ImVec2(0, -10), false, "%.2f", t);
            }
        }

        BinInteraction();
        ImPlot::EndPlot();
    }
    // Compute distance
    ImGui::Text("Number of samples child / parent: %s / %s",
        FormatInteger((int) m_cachedSignature.numSamples).c_str(),
        FormatInteger((int) m_cachedSignatureParent.numSamples).c_str());
    bool isTTestPerBin = m_parent->GetSubdivCfg().confidenceType == PGL_SPATIAL_CONFIDENCE_TTEST_PER_BIN;
    bool isTTest = m_parent->GetSubdivCfg().confidenceType == PGL_SPATIAL_CONFIDENCE_TTEST;
    if (m_radianceView.HasSelectedBinIndex()) {
        uint8_t idx = m_radianceView.GetSelectedBinIndex();
        if (isTTest || isTTestPerBin)
            ImGui::Text("Child / std: %.4f / %.2e, parent / std: %.4f / %.2e,  T: %.3f",
                m_cachedSignature.signature[idx], m_cachedSignature.std[idx],
                m_cachedSignatureParent.signature[idx], m_cachedSignatureParent.std[idx],
                getWelchT(m_cachedSignature, m_cachedSignatureParent, idx));
        else
            ImGui::Text("Child / std: %.4f / %.2e, parent / std: %.4f / %.2e",
                m_cachedSignature.signature[idx], m_cachedSignature.std[idx],
                m_cachedSignatureParent.signature[idx], m_cachedSignatureParent.std[idx]);
    } else {
        float energy = isTTestPerBin ? getDistanceTTest(m_cachedSignature, m_cachedSignatureParent, m_parent->GetSignatureStdMultiplier(), m_parent->GetTValueThreshold()) : getDistanceSMAPE(m_cachedSignature, m_cachedSignatureParent, m_parent->GetSignatureStdMultiplier());
        if (m_splitDim == 3) {
            ImGui::Text("Invalid");
        } else {
            static const char dim_ch[] = {'x', 'y', 'z'};
            ImGui::Text("Dimension: %c  Energy: %.4f", dim_ch[m_splitDim], energy);
        }
    }
}

void SignatureView::DrawLR() {
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("Depth", &m_lookaheadDepth, 1, 10)) {
        m_lookaheadDepth = std::clamp(m_lookaheadDepth, 0, (int) m_parent->GetSubdivCfg().lookaheadDepth);
        if (m_prev.valid) Update();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Std", &m_showStd);
    ImGui::SameLine();
    ImGui::Checkbox("Multiplied Std", &m_showMultipliedStd);

    static float left_std[PGL_SIGNATURE_MAX_SIZE], right_std[PGL_SIGNATURE_MAX_SIZE];
    auto flags = ImPlotFlags_NoLegend | ImPlotFlags_NoTitle;
    uint8_t S = NumBins();
    if (ImPlot::BeginPlot("LR Signatures Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - 2 * ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, S - 1 + 1.5*barSize, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::PlotBars("Left", m_cachedSignaturesLR.first.signature, S, barSize);
        ImPlot::PlotBars("Right", m_cachedSignaturesLR.second.signature, S, barSize, barSize);
        if (m_showStd) {
            ImPlot::PlotErrorBars("Left-std", m_barXs, m_cachedSignaturesLR.first.signature, m_cachedSignaturesLR.first.std, S);
            ImPlot::PlotErrorBars("Right-std", m_barRXs, m_cachedSignaturesLR.second.signature, m_cachedSignaturesLR.second.std, S);
        }
        if (m_showMultipliedStd) {
            float multiplier = m_parent->GetSignatureStdMultiplier();
            for (int i = 0; i < S; ++i) {
                left_std[i] = m_cachedSignaturesLR.first.std[i] * multiplier;
                right_std[i] = m_cachedSignaturesLR.second.std[i] * multiplier;
            }
            ImPlot::PushStyleVar(ImPlotStyleVar_ErrorBarSize, 8.0f);
            ImPlot::PushStyleColor(ImPlotCol_ErrorBar, ImVec4(1, 1, 0, 1));
            ImPlot::PlotErrorBars("Left-std-", m_barXs, m_cachedSignaturesLR.first.signature, left_std, S);
            ImPlot::PlotErrorBars("Right-std-", m_barRXs, m_cachedSignaturesLR.second.signature, right_std, S);
            ImPlot::PopStyleColor();
            ImPlot::PopStyleVar();
        }

        BinInteraction();
        ImPlot::EndPlot();
    }
    // Compute distance
    ImGui::Text("Number of samples left / right: %s / %s",
        FormatInteger((int) m_cachedSignaturesLR.first.numSamples).c_str(),
        FormatInteger((int) m_cachedSignaturesLR.second.numSamples).c_str());
    if (m_radianceView.HasSelectedBinIndex()) {
        uint8_t idx = m_radianceView.GetSelectedBinIndex();
        ImGui::Text("Left / std: %.4f / %.2e, right / std: %.4f / %.2e",
            m_cachedSignaturesLR.first.signature[idx], m_cachedSignaturesLR.first.std[idx],
            m_cachedSignaturesLR.second.signature[idx], m_cachedSignaturesLR.second.std[idx]);
    } else {
        float energy = getDistanceSMAPE(m_cachedSignaturesLR.first, m_cachedSignaturesLR.second, m_parent->GetSignatureStdMultiplier());
        if (m_splitDim == 3) {
            ImGui::Text("Invalid");
        } else {
            static const char dim_ch[] = {'x', 'y', 'z'};
            ImGui::Text("Dimension: %c  Energy: %.4f", dim_ch[m_splitDim], energy);
        }
    }
}

void SignatureView::BinInteraction() {
    // Interaction: display a vertical marker at the clicked bin and select it from the radiance view
    if (ImPlot::IsAxisHovered(ImAxis_X1) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        double x = ImPlot::GetPlotMousePos().x + barSize / 2;
        if (x >= 0 && x < NumBins()) {
            if (m_radianceView.GetSelectedBinIndex() == (uint8_t) x) {
                m_radianceView.ResetSelectedBinIndex();
            } else {
                m_radianceView.SetSelectedBinIndex((uint8_t) x);
            }
        }
    }
    if (m_radianceView.HasSelectedBinIndex()) {
        double x = m_radianceView.GetSelectedBinIndex();
        ImPlot::TagX(x, ImVec4(1, 0, 0, 0.5));
    }
}

void SignatureView::UpdateFramebuffer() {
    auto render = [&](Framebuffer &fb, GLuint tex, const PGLDirectionalSignature &signature) {
        UpdateTextureFromFloatData(tex, signature.signature, NumBins(), 1, false);
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
    for (int i = 0; i < NumBins(); ++i) {
        if (m_radianceView.GetSelectedBinIndex() == i) {
            m_selectionBuffer[i] = RGB(1, 0, 0);
        } else {
            m_selectionBuffer[i] = RGB(0, 0, 0);
        }
    }
    UpdateTextureFromRGBData((GLuint) (uintptr_t) m_selectionTex, m_selectionBuffer, NumBins(), 1, false);

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

uint8_t SignatureView::NumBins() const {  // TODO: just an adhoc solution
    return m_parent->GetSubdivCfg().signatureEnsembleConfig[0].numBins;
}
