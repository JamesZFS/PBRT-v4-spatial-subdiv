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
      m_cachedSignatureFramebuffer(8, 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag"),
      m_integratedSignatureFramebuffer(8, 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag"),
      m_selectionFramebuffer(8, 1,PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag") {
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
    m_cachedSignatureFramebuffer.rescale(m_numBins, 1);
    m_integratedSignatureFramebuffer.rescale(m_numBins, 1);
    m_selectionFramebuffer.rescale(m_numBins, 1);
}

void SignatureView::Update() {
    pgl_point3f pglP = {m_prev.pos.x, m_prev.pos.y, m_prev.pos.z};
    int numSignatures = m_parent->GetSubdivCfg().signatureEnsembleConfig.size();
    int newNumBins = m_parent->GetSubdivCfg().signatureEnsembleConfig[m_parent->SelectedModelIndex()].numBins;
    if (newNumBins != m_numBins) {
        m_numBins = newNumBins;
        Rescale();
    }
    m_cachedSignatureParent.resize(numSignatures);
    m_cachedSignaturesLR.resize(numSignatures);
    m_cachedSignatureChild.resize(numSignatures);
    for (int i = 0; i < numSignatures; ++i) {
        m_cachedSignatureParent[i] = m_field.GetDirectionalSignatures(pglP, 0, i, m_splitDim, m_isRight).first;
        m_cachedSignaturesLR[i] = m_field.GetDirectionalSignatures(pglP, m_lookaheadDepth, i, m_splitDim, m_isRight);
        m_cachedSignatureChild[i] = m_isRight ? m_cachedSignaturesLR[i].second : m_cachedSignaturesLR[i].first;
    }
}

void SignatureView::Clear() {
    m_prev.valid = false;
    m_cachedSignatureParent.resize(1);
    m_cachedSignaturesLR.resize(1);
    m_cachedSignatureChild.resize(1);
    m_cachedSignatureParent[0] = {};
    m_cachedSignaturesLR[0] = {};
    m_cachedSignatureChild[0] = {};
    m_storedSignature = {};
    m_hasStoredSignature = false;
    m_splitDim = 3;
    m_isRight = false;
    m_numBins = 0;
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
    int i = m_parent->SelectedModelIndex();
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
        for (const auto &e : m_cachedSignatureChild[i].signature) {
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
            uint8_t idx = std::min((uint8_t) (x * m_numBins), (uint8_t) (m_numBins - 1));
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
    drawSignature("Cached", m_cachedSignatureFramebuffer, m_cachedSignatureChild[i]);

    // Integrated Signature
    if (m_showIntegratedSignature)
        drawSignature("Integrated", m_integratedSignatureFramebuffer, m_integratedSignature);

    if (m_radianceView.HasSelectedBinIndex()) {
        uint8_t idx = m_radianceView.GetSelectedBinIndex();
        ImGui::Text("Cached: %.4f  Std: %.2e  Integrated: %.4f", m_cachedSignatureChild[i].signature[idx], m_cachedSignatureChild[i].std[idx], m_integratedSignature.signature[idx]);
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
    uint8_t S = m_numBins;
    if (ImPlot::BeginPlot("Signature Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, S - 1 + 1.5*barSize, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::SetNextFillStyle(ImPlot::GetColormapColor(2));  // Green
        ImPlot::PlotBars("Current", m_integratedSignature.signature, S, barSize);
        ImPlot::SetNextFillStyle(ImPlot::GetColormapColor(1));  // Orange
        ImPlot::PlotBars("Stored", m_storedSignature.signature, S, barSize, barSize);

        BinInteraction(m_parent->SelectedModelIndex());
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
    uint8_t S = m_numBins;
    int i = m_parent->SelectedModelIndex();
    if (ImPlot::BeginPlot("Signature Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, S - 1 + 1.5*barSize, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::PlotBars("Cached", m_cachedSignatureChild[i].signature, S, barSize);
        if (m_showStd) {
            ImPlot::PlotErrorBars("Std", m_barXs, m_cachedSignatureChild[i].signature, m_cachedSignatureChild[i].std, S);
        }
        if (m_showIntegratedSignature)
            ImPlot::PlotBars("Integrated", m_integratedSignature.signature, S, barSize, barSize);

        BinInteraction(i);
        ImPlot::EndPlot();
    }
    if (m_radianceView.HasSelectedBinIndex()) {
        uint8_t idx = m_radianceView.GetSelectedBinIndex();
        ImGui::Text("Cached: %.4f  Std: %.2e  Integrated: %.4f", m_cachedSignatureChild[i].signature[idx], m_cachedSignatureChild[i].std[idx], m_integratedSignature.signature[idx]);
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

    bool isTTestPerBin = m_parent->GetSubdivCfg().confidenceType == PGL_SPATIAL_CONFIDENCE_TTEST_PER_BIN;
    bool isTTest = m_parent->GetSubdivCfg().confidenceType == PGL_SPATIAL_CONFIDENCE_TTEST;
    auto flags = ImPlotFlags_NoLegend | ImPlotFlags_NoTitle;

    // Plot all signatures in a table
    int numSignatures = m_cachedSignatureParent.size();
    float plotVSize = ImGui::GetContentRegionAvail().y;
    ImVec2 padding = ImGui::GetStyle().CellPadding;
    plotVSize = (plotVSize - padding.y * 2 * numSignatures - 2.5 * ImGui::GetFrameHeightWithSpacing()) / numSignatures;
    
    if (ImGui::BeginTable("##PC-Table", 3, ImGuiTableFlags_BordersV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Energy", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Signature");
        ImGui::TableHeadersRow();

        for (int i = 0; i < numSignatures; ++i) {
            uint8_t S = m_parent->GetSubdivCfg().signatureEnsembleConfig[i].numBins;
            PGLDirectionalSignature childSignature = m_lookaheadDepth == 0 ? PGLDirectionalSignature() : m_cachedSignatureChild[i];

            ImGui::TableNextColumn();
            ImGui::Text("%d", i+1);

            ImGui::TableNextColumn();
            // Compute energy of the current signature
            float energy = isTTestPerBin ? 
                getDistanceTTest(m_cachedSignatureChild[i], m_cachedSignatureParent[i], m_parent->GetSignatureStdMultiplier(), m_parent->GetTValueThreshold()) :
                getDistanceSMAPE(m_cachedSignatureChild[i], m_cachedSignatureParent[i], m_parent->GetSignatureStdMultiplier());

            if (m_splitDim == 3)
                ImGui::Text(" "); // invalid
            else
                ImGui::Text("%.4f", energy);

            ImGui::TableNextColumn();
            ImGui::PushID(i);
            if (ImPlot::BeginPlot("Child/Parent Signatures Plot", ImVec2(-1, plotVSize), flags)) {
                ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, S - 1 + 1.5*barSize, ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
                ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

                ImPlot::PlotBars("Child", childSignature.signature, S, barSize);
                ImPlot::PlotBars("Parent", m_cachedSignatureParent[i].signature, S, barSize, barSize);
                if (m_showStd) {
                    ImPlot::PlotErrorBars("Child-std", m_barXs, childSignature.signature, childSignature.std, S);
                    ImPlot::PlotErrorBars("Parent-std", m_barRXs, m_cachedSignatureParent[i].signature, m_cachedSignatureParent[i].std, S);
                }
                if (m_showMultipliedStd) {
                    float multiplier = m_parent->GetSignatureStdMultiplier();
                    static float child_std[PGL_SIGNATURE_MAX_SIZE], parent_std[PGL_SIGNATURE_MAX_SIZE];
                    for (int j = 0; j < S; ++j) {
                        child_std[j] = childSignature.std[j] * multiplier;
                        parent_std[j] = m_cachedSignatureParent[i].std[j] * multiplier;
                    }
                    ImPlot::PushStyleVar(ImPlotStyleVar_ErrorBarSize, 8.0f);
                    ImPlot::PushStyleColor(ImPlotCol_ErrorBar, ImVec4(1, 1, 0, 1));
                    ImPlot::PlotErrorBars("Child-std-", m_barXs, childSignature.signature, child_std, S);
                    ImPlot::PlotErrorBars("Parent-std-", m_barRXs, m_cachedSignatureParent[i].signature, parent_std, S);
                    ImPlot::PopStyleColor();
                    ImPlot::PopStyleVar();
                }
                if (m_showTValue) {
                    for (int j = 0; j < S; ++j) {
                        float t = getWelchT(m_cachedSignatureChild[i], m_cachedSignatureParent[i], j);
                        ImVec4 col = (std::isnan(t) || std::abs(t) <= m_parent->GetTValueThreshold()) ? ImVec4(0, 0, 0, 0) : ImVec4(1, 1, 0, 0.7);
                        ImPlot::Annotation(m_barXs[j], m_cachedSignatureChild[i].signature[j], col, ImVec2(0, -10), false, "%.2f", t);
                    }
                }

                BinInteraction(i);
                ImPlot::EndPlot();
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
    
    // Meta information
    int i = m_parent->SelectedModelIndex();
    ImGui::Text("Number of samples child / parent: %s / %s",
        FormatInteger((int) m_cachedSignatureChild[i].numSamples).c_str(),
        FormatInteger((int) m_cachedSignatureParent[i].numSamples).c_str());
    if (m_radianceView.HasSelectedBinIndex()) {
        uint8_t idx = m_radianceView.GetSelectedBinIndex();
        if (isTTest || isTTestPerBin)
            ImGui::Text("Child / std: %.4f / %.2e, parent / std: %.4f / %.2e,  T: %.3f",
                m_cachedSignatureChild[i].signature[idx], m_cachedSignatureChild[i].std[idx],
                m_cachedSignatureParent[i].signature[idx], m_cachedSignatureParent[i].std[idx],
                getWelchT(m_cachedSignatureChild[i], m_cachedSignatureParent[i], idx));
        else
            ImGui::Text("Child / std: %.4f / %.2e, parent / std: %.4f / %.2e",
                m_cachedSignatureChild[i].signature[idx], m_cachedSignatureChild[i].std[idx],
                m_cachedSignatureParent[i].signature[idx], m_cachedSignatureParent[i].std[idx]);
    } else {
        if (m_splitDim == 3) {
            ImGui::Text("Invalid");
        } else {
            static const char dim_ch[] = {'x', 'y', 'z'};
            ImGui::Text("Dimension: %c", dim_ch[m_splitDim]);
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

    bool isTTestPerBin = m_parent->GetSubdivCfg().confidenceType == PGL_SPATIAL_CONFIDENCE_TTEST_PER_BIN;
    bool isTTest = m_parent->GetSubdivCfg().confidenceType == PGL_SPATIAL_CONFIDENCE_TTEST;
    auto flags = ImPlotFlags_NoLegend | ImPlotFlags_NoTitle;

    // Plot all signatures in a table
    int numSignatures = m_cachedSignatureParent.size();
    float plotVSize = ImGui::GetContentRegionAvail().y;
    ImVec2 padding = ImGui::GetStyle().CellPadding;
    plotVSize = (plotVSize - padding.y * 2 * numSignatures - 2.5 * ImGui::GetFrameHeightWithSpacing()) / numSignatures;
    
    if (ImGui::BeginTable("##PC-Table", 3, ImGuiTableFlags_BordersV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Energy", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Signature");
        ImGui::TableHeadersRow();

        for (int i = 0; i < numSignatures; ++i) {
            uint8_t S = m_parent->GetSubdivCfg().signatureEnsembleConfig[i].numBins;
            PGLDirectionalSignature left = m_lookaheadDepth == 0 ? PGLDirectionalSignature() : m_cachedSignaturesLR[i].first;
            PGLDirectionalSignature right = m_lookaheadDepth == 0 ? PGLDirectionalSignature() : m_cachedSignaturesLR[i].second;

            ImGui::TableNextColumn();
            ImGui::Text("%d", i+1);

            ImGui::TableNextColumn();
            // Compute energy of the current signature
            float energy = isTTestPerBin ? 
                getDistanceTTest(left, right, m_parent->GetSignatureStdMultiplier(), m_parent->GetTValueThreshold()) :
                getDistanceSMAPE(left, right, m_parent->GetSignatureStdMultiplier());

            if (m_splitDim == 3)
                ImGui::Text(" "); // invalid
            else
                ImGui::Text("%.4f", energy);

            ImGui::TableNextColumn();
            ImGui::PushID(i);
            if (ImPlot::BeginPlot("Left/Right Signatures Plot", ImVec2(-1, plotVSize), flags)) {
                ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, S - 1 + 1.5*barSize, ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
                ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

                ImPlot::PlotBars("Left", left.signature, S, barSize);
                ImPlot::PlotBars("Right", right.signature, S, barSize, barSize);
                if (m_showStd) {
                    ImPlot::PlotErrorBars("Left-std", m_barXs, left.signature, left.std, S);
                    ImPlot::PlotErrorBars("Right-std", m_barRXs, right.signature, right.std, S);
                }
                if (m_showMultipliedStd) {
                    float multiplier = m_parent->GetSignatureStdMultiplier();
                    static float left_std[PGL_SIGNATURE_MAX_SIZE], right_std[PGL_SIGNATURE_MAX_SIZE];
                    for (int j = 0; j < S; ++j) {
                        left_std[j] = left.std[j] * multiplier;
                        right_std[j] = right.std[j] * multiplier;
                    }
                    ImPlot::PushStyleVar(ImPlotStyleVar_ErrorBarSize, 8.0f);
                    ImPlot::PushStyleColor(ImPlotCol_ErrorBar, ImVec4(1, 1, 0, 1));
                    ImPlot::PlotErrorBars("Left-std-", m_barXs, left.signature, left_std, S);
                    ImPlot::PlotErrorBars("Right-std-", m_barRXs, right.signature, right_std, S);
                    ImPlot::PopStyleColor();
                    ImPlot::PopStyleVar();
                }
                if (m_showTValue) {
                    for (int j = 0; j < S; ++j) {
                        float t = getWelchT(left, right, j);
                        ImVec4 col = (std::isnan(t) || std::abs(t) <= m_parent->GetTValueThreshold()) ? ImVec4(0, 0, 0, 0) : ImVec4(1, 1, 0, 0.7);
                        ImPlot::Annotation(m_barXs[j], left.signature[j], col, ImVec2(0, -10), false, "%.2f", t);
                    }
                }

                BinInteraction(i);
                ImPlot::EndPlot();
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
    
    // Meta information
    int i = m_parent->SelectedModelIndex();
    ImGui::Text("Number of samples left / right: %s / %s",
        FormatInteger((int) m_cachedSignaturesLR[i].first.numSamples).c_str(),
        FormatInteger((int) m_cachedSignaturesLR[i].second.numSamples).c_str());
    if (m_radianceView.HasSelectedBinIndex()) {
        uint8_t idx = m_radianceView.GetSelectedBinIndex();
        if (isTTest || isTTestPerBin)
            ImGui::Text("Left / std: %.4f / %.2e, right / std: %.4f / %.2e,  T: %.3f",
                m_cachedSignaturesLR[i].first.signature[idx], m_cachedSignaturesLR[i].first.std[idx],
                m_cachedSignaturesLR[i].second.signature[idx], m_cachedSignaturesLR[i].second.std[idx],
                getWelchT(m_cachedSignatureChild[i], m_cachedSignatureParent[i], idx));
        else
            ImGui::Text("Left / std: %.4f / %.2e, right / std: %.4f / %.2e",
                m_cachedSignaturesLR[i].first.signature[idx], m_cachedSignaturesLR[i].first.std[idx],
                m_cachedSignaturesLR[i].second.signature[idx], m_cachedSignaturesLR[i].second.std[idx]);
    } else {
        if (m_splitDim == 3) {
            ImGui::Text("Invalid");
        } else {
            static const char dim_ch[] = {'x', 'y', 'z'};
            ImGui::Text("Dimension: %c", dim_ch[m_splitDim]);
        }
    }
}

void SignatureView::BinInteraction(int modelIndex) {
    // Interaction: display a vertical marker at the clicked bin and select it from the radiance view
    if (ImPlot::IsAxisHovered(ImAxis_X1) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        double x = ImPlot::GetPlotMousePos().x + barSize / 2;
        int oldModelIndex = m_parent->SelectedModelIndex();
        if (x >= 0 && x < m_parent->GetSubdivCfg().signatureEnsembleConfig[modelIndex].numBins) {
            // Valid click
            if (oldModelIndex == modelIndex && m_radianceView.GetSelectedBinIndex() == (uint8_t) x) {
                m_radianceView.ResetSelectedBinIndex();
            } else {
                m_parent->SetSelectedModelIndex(modelIndex);
                m_radianceView.SetSelectedBinIndex((uint8_t) x);
            }
        }
    }
    if (m_radianceView.HasSelectedBinIndex() && modelIndex == m_parent->SelectedModelIndex()) {
        double x = m_radianceView.GetSelectedBinIndex();
        ImPlot::TagX(x, ImVec4(1, 0, 0, 0.5));
    }
}

void SignatureView::UpdateFramebuffer() {
    auto render = [&](Framebuffer &fb, GLuint tex, const PGLDirectionalSignature &signature) {
        UpdateTextureFromFloatData(tex, signature.signature, m_numBins, 1, false);
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
    int i = m_parent->SelectedModelIndex();
    render(m_cachedSignatureFramebuffer, m_cachedSignatureTex, m_cachedSignatureChild[i]);

    // Integrated signature buffer
    if (m_showIntegratedSignature)
        render(m_integratedSignatureFramebuffer, m_integratedSignatureTex, m_integratedSignature);

    // Selection buffer
    for (int i = 0; i < m_numBins; ++i) {
        if (m_radianceView.GetSelectedBinIndex() == i) {
            m_selectionBuffer[i] = RGB(1, 0, 0);
        } else {
            m_selectionBuffer[i] = RGB(0, 0, 0);
        }
    }
    UpdateTextureFromRGBData((GLuint) (uintptr_t) m_selectionTex, m_selectionBuffer, m_numBins, 1, false);

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
