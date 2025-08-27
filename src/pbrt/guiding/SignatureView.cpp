//
// Created by fengshi on 11/21/24.
//

#include "SignatureView.h"
#include <implot.h>

#include "Application.h"

using namespace pbrt;

constexpr float barSize = 0.4;

SignatureView::SignatureView(pbrt::Application *parent, const openpgl::cpp::Field &field, RadianceView &radianceView)
    : View(parent), m_field(field), m_radianceView(radianceView), m_integratedSignature(radianceView.integratedSignature) {
    m_barXs = 0;
    m_barRXs = barSize;
}

void SignatureView::Update(const pbrt::Point3f &pos) {
    m_prev = {true, pos};
    m_shouldUpdate = true;
}

// This should be called in the main thread
void SignatureView::Update() {
    pgl_point3f pglP = {m_prev.pos.x, m_prev.pos.y, m_prev.pos.z};
    m_cachedSignatureParent = m_field.GetDirectionalSignatures(pglP, 0, m_splitDim, m_isRight).first;
    m_cachedSignaturesLR = m_field.GetDirectionalSignatures(pglP, m_lookaheadDepth, m_splitDim, m_isRight);
    m_cachedSignatureChild = m_isRight ? m_cachedSignaturesLR.second : m_cachedSignaturesLR.first;
}

void SignatureView::Clear() {
    m_prev.valid = false;
    m_cachedSignatureParent = {};
    m_cachedSignaturesLR = {};
    m_cachedSignatureChild = {};
    m_storedSignature = {};
    m_hasStoredSignature = false;
    m_splitDim = 3;
    m_isRight = false;
}

void SignatureView::Draw() {
    if (m_shouldUpdate.exchange(false)) {
        Update();
    }
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
        ImGui::EndTabBar();
    }
}

// Needs to align with Signature.h
static float getDistanceSMAPE(const PGLDirectionalSignature &a, const PGLDirectionalSignature &b) {
    float num = 0, denom = 0;
    float ai = a.signature, bi = b.signature;
    num += std::abs(ai - bi);
    denom += ai;
    return denom == 0 ? 0 : num / denom;
}

static float getDistanceSMAPEComp(const PGLDirectionalSignature &a, const PGLDirectionalSignature &b) {
    float num = 0, denom = 0;
    float ai = a.signature, bi = b.signature;
    num += std::abs(ai - bi);
    denom += ai + bi;
    return denom == 0 ? 0 : 2.0f * num / denom;
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
    if (ImPlot::BeginPlot("Signature Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, 1.5*barSize, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::SetNextFillStyle(ImPlot::GetColormapColor(2));  // Green
        ImPlot::PlotBars("Current", &m_integratedSignature.signature, 1, barSize);
        ImPlot::SetNextFillStyle(ImPlot::GetColormapColor(1));  // Orange
        ImPlot::PlotBars("Stored", &m_storedSignature.signature, 1, barSize, barSize);

        ImPlot::EndPlot();
    }
    if (m_hasStoredSignature) {
        // Show the distance
        float distance = getDistanceSMAPEComp(m_integratedSignature, m_storedSignature);
        ImGui::Text("Distance: %.4f", distance);
    } else {
        float sum = 0.0f;
        sum += m_integratedSignature.signature;
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
    if (ImPlot::BeginPlot("Signature Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, 1.5*barSize, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::PlotBars("Cached", &m_cachedSignatureChild.signature, 1, barSize);
        if (m_showStd) {
            ImPlot::PlotErrorBars("Std", &m_barXs, &m_cachedSignatureChild.signature, &m_cachedSignatureChild.std, 1);
        }
        if (m_showIntegratedSignature)
            ImPlot::PlotBars("Integrated", &m_integratedSignature.signature, 1, barSize, barSize);

        ImPlot::EndPlot();
    }
    ImGui::Text("Cached: %.4f  Std: %.2e  Integrated: %.4f", m_cachedSignatureChild.signature, m_cachedSignatureChild.std, m_integratedSignature.signature);
}

void SignatureView::DrawPC() {
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("Depth", &m_lookaheadDepth, 1, 10)) {
        m_lookaheadDepth = std::clamp(m_lookaheadDepth, 0, (int) m_parent->GetSubdivCfg().lookaheadDepth);
        if (m_prev.valid) Update();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Std", &m_showStd);

    auto flags = ImPlotFlags_NoLegend | ImPlotFlags_NoTitle;

    PGLDirectionalSignature childSignature = m_lookaheadDepth == 0 ? PGLDirectionalSignature() : m_cachedSignatureChild;

    if (ImPlot::BeginPlot("Child/Parent Signatures Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - 2.f * ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, 1.5*barSize, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::PlotBars("Child", &childSignature.signature, 1, barSize);
        ImPlot::PlotBars("Parent", &m_cachedSignatureParent.signature, 1, barSize, barSize);
        if (m_showStd) {
            ImPlot::PlotErrorBars("Child-std", &m_barXs, &childSignature.signature, &childSignature.std, 1);
            ImPlot::PlotErrorBars("Parent-std", &m_barRXs, &m_cachedSignatureParent.signature, &m_cachedSignatureParent.std, 1);
        }

        ImPlot::EndPlot();
    }

    // Meta information
    m_radianceView.directionData[0].setFromPGLData(m_cachedSignatureChild);
    m_radianceView.directionData[1].setFromPGLData(m_cachedSignatureParent);
    ImGui::Text("Number of samples child / parent: %s / %s",
        FormatInteger((int) m_cachedSignatureChild.numSamples).c_str(),
        FormatInteger((int) m_cachedSignatureParent.numSamples).c_str());
    if (m_splitDim == 3) {
        ImGui::Text("Invalid");
    } else {
        static const char dim_ch[] = {'x', 'y', 'z'};
        ImGui::Text("Dimension: %c", dim_ch[m_splitDim]);
    }
    ImGui::Text("Mean direction of child / parent: (%.2f, %.2f, %.2f) / (%.2f, %.2f, %.2f)",
        m_cachedSignatureChild.meanDir.x, m_cachedSignatureChild.meanDir.y, m_cachedSignatureChild.meanDir.z,
        m_cachedSignatureParent.meanDir.x, m_cachedSignatureParent.meanDir.y, m_cachedSignatureParent.meanDir.z);
    ImGui::Text("VMF kappa of child / parent: %.2f / %.2f, sigma: %.2e / %.2e",
        m_cachedSignatureChild.kappa, m_cachedSignatureParent.kappa,
        m_cachedSignatureChild.sigmaDir, m_cachedSignatureParent.sigmaDir);
}

void SignatureView::DrawLR() {
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("Depth", &m_lookaheadDepth, 1, 10)) {
        m_lookaheadDepth = std::clamp(m_lookaheadDepth, 0, (int) m_parent->GetSubdivCfg().lookaheadDepth);
        if (m_prev.valid) Update();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Std", &m_showStd);

    auto flags = ImPlotFlags_NoLegend | ImPlotFlags_NoTitle;

    PGLDirectionalSignature left = m_lookaheadDepth == 0 ? PGLDirectionalSignature() : m_cachedSignaturesLR.first;
    PGLDirectionalSignature right = m_lookaheadDepth == 0 ? PGLDirectionalSignature() : m_cachedSignaturesLR.second;

    if (ImPlot::BeginPlot("Left/Right Signatures Plot", ImVec2(-1, ImGui::GetContentRegionAvail().y - 2.f * ImGui::GetFrameHeightWithSpacing()), flags)) {
        ImPlot::SetupAxisLimits(ImAxis_X1,-barSize/2, 1.5*barSize, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.0);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);

        ImPlot::PlotBars("Left", &left.signature, 1, barSize);
        ImPlot::PlotBars("Right", &right.signature, 1, barSize, barSize);
        if (m_showStd) {
            ImPlot::PlotErrorBars("Left-std", &m_barXs, &left.signature, &left.std, 1);
            ImPlot::PlotErrorBars("Right-std", &m_barRXs, &right.signature, &right.std, 1);
        }

        ImPlot::EndPlot();
    }

    // Meta information
    m_radianceView.directionData[0].setFromPGLData(m_cachedSignaturesLR.first);
    m_radianceView.directionData[1].setFromPGLData(m_cachedSignaturesLR.second);
    ImGui::Text("Number of samples left / right: %s / %s",
        FormatInteger((int) m_cachedSignaturesLR.first.numSamples).c_str(),
        FormatInteger((int) m_cachedSignaturesLR.second.numSamples).c_str());
    if (m_splitDim == 3) {
        ImGui::Text("Invalid");
    } else {
        static const char dim_ch[] = {'x', 'y', 'z'};
        ImGui::Text("Dimension: %c", dim_ch[m_splitDim]);
    }
    ImGui::Text("Mean direction of left / right: (%.2f, %.2f, %.2f) / (%.2f, %.2f, %.2f)",
        m_cachedSignaturesLR.first.meanDir.x, m_cachedSignaturesLR.first.meanDir.y, m_cachedSignaturesLR.first.meanDir.z,
        m_cachedSignaturesLR.second.meanDir.x, m_cachedSignaturesLR.second.meanDir.y, m_cachedSignaturesLR.second.meanDir.z);
    ImGui::Text("VMF kappa of left / right: %.2f / %.2f, sigma: %.2e / %.2e",
        m_cachedSignaturesLR.first.kappa, m_cachedSignaturesLR.second.kappa,
        m_cachedSignaturesLR.first.sigmaDir, m_cachedSignaturesLR.second.sigmaDir);
}

