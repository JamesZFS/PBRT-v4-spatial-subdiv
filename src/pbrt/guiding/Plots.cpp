//
// Created by fengshi on 1/21/25.
//

#include "Plots.h"
#include "Application.h"
#include <implot.h>


PlotManager::~PlotManager() {
    for (auto &[yAxis, plot]: m_plots)
        delete plot;
}

PlotManager::Plot::Plot(pbrt::Application *parent, const std::string &xAxisName, const std::string &yAxisName,
                        PlotManager &plotManager) : View(parent), m_manager(plotManager) {
    m_xAxisName = xAxisName;
    m_yAxisName = yAxisName;
}

void PlotManager::Plot::Draw() {
    // Curve ID
    ImGui::SetNextItemWidth(90);
    ImGui::InputText("##Curve-ID", m_manager.m_curveId.data(), m_manager.m_curveId.size(), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    static bool once = false;
    if (ImGui::Button("Change Curve ID")) {
        ImGui::OpenPopup("Change Curve ID...");
        once = true;
        m_parent->SetShortcutEnabled(false);
    }
    if (ImGui::BeginPopupModal("Change Curve ID...", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char buf[256];
        if (once) {
            std::fill(buf, buf + IM_ARRAYSIZE(buf), 0);
            std::copy(m_manager.m_curveId.begin(), m_manager.m_curveId.end(), buf);
            once = false;
        }
        bool shouldClose = false;
        ImGui::InputText("New Curve ID", buf, IM_ARRAYSIZE(buf));
        if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            shouldClose = true;
            m_manager.m_curveId = buf;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            shouldClose = true;
        }
        if (shouldClose) {
            ImGui::CloseCurrentPopup();
            m_parent->SetShortcutEnabled(true);
        }
        ImGui::EndPopup();
    }

    // Draw plots
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.3));
    ImPlot::PushStyleVar(ImPlotStyleVar_Marker, ImPlotMarker_Cross);
    ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, m_manager.m_markerSize);
    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, m_manager.m_lineWeight);
    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, m_manager.m_alpha);
    ImPlotFlags plot_flags = ImPlotFlags_NoTitle;
    if (m_shouldFitAxes.exchange(false) && m_manager.m_autoFitAxes)
        ImPlot::SetNextAxesToFit();

    if (ImPlot::BeginPlot(m_yAxisName.c_str(), ImVec2(-1, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing()), plot_flags)) {
        ImPlot::SetupAxes(m_xAxisName.c_str(), m_yAxisName.c_str(), ImPlotAxisFlags_NoLabel, ImPlotAxisFlags_NoLabel);
        // Hovering behavior: draw a vertical line for all plots at the same x position
        ImDrawList *draw_list = ImPlot::GetPlotDrawList();
        if (ImPlot::IsPlotHovered()) {
            m_manager.m_mouse.hoveringID = this->ID();
            ImPlotPoint mouse = ImPlot::GetPlotMousePos();
            m_manager.m_mouse.x = mouse.x, m_manager.m_mouse.y = mouse.y;
            ImVec2 screen_pos = ImPlot::PlotToPixels(mouse);
            ImPlot::PushPlotClipRect();
            draw_list->AddLine(ImVec2(screen_pos.x, ImPlot::GetPlotPos().y), ImVec2(screen_pos.x, ImPlot::GetPlotPos().y + ImPlot::GetPlotSize().y), ImGui::GetColorU32(IM_COL32_WHITE, 0.8f));
            ImPlot::PopPlotClipRect();
        }
        else if (m_manager.m_mouse.hoveringID == this->ID()) {  // Was hovering on this plot
            m_manager.m_mouse.hoveringID = 0;
        }
        else if (m_manager.m_mouse.hoveringID != 0) {  // Hovering on other plot
            ImVec2 screen_pos = ImPlot::PlotToPixels(m_manager.m_mouse.x, m_manager.m_mouse.y);
            ImPlot::PushPlotClipRect();
            draw_list->AddLine(ImVec2(screen_pos.x, ImPlot::GetPlotPos().y), ImVec2(screen_pos.x, ImPlot::GetPlotPos().y + ImPlot::GetPlotSize().y), ImGui::GetColorU32(IM_COL32_WHITE, 0.4f));
            ImPlot::PopPlotClipRect();
        }

        std::lock_guard lock(m_manager.m_mutex);
        for (int i = 0; i < m_curves.size(); ++i) {
            const auto &curve = m_curves[i];
            auto lineColor = ImPlot::GetColormapColor(i);
            ImPlot::SetNextLineStyle(lineColor);
            ImPlot::PlotLine(curve.id.c_str(), &curve.xData[0], &curve.yData[0], (int) curve.xData.size());
        }
        ImPlot::EndPlot();
    }

    ImPlot::PopStyleVar(5);

    // Plot controls
    if (ImGui::Button("Clear All")) {
        m_manager.ClearAll();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Current")) {
        m_manager.ClearCurrent();
    }
    ImGui::SameLine();

    // Plot options
    ImGui::Checkbox("Auto Fit", &m_manager.m_autoFitAxes);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50);
    ImGui::DragFloat("Marker", &m_manager.m_markerSize, 0.03f, 0, 5);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50);
    ImGui::DragFloat("Line", &m_manager.m_lineWeight, 0.03f, 0, 5);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50);
    ImGui::DragFloat("Alpha", &m_manager.m_alpha,0.01f,0,1);
}

PlotManager::Plot &PlotManager::AddPlot(const std::string &yAxisName) {
    auto [it, success] = m_plots.emplace(yAxisName, new Plot(m_parent, "iter", yAxisName, *this));
    CHECK(success);
    return *it->second;
}

void PlotManager::AppendData(const std::string &yAxisName, float x, float y) {
    std::lock_guard lock(m_mutex);
    auto &plot = *m_plots.at(yAxisName);

    // Find the curve with the id == m_curveId
    Plot::Curve *curve = nullptr;
    for (auto &c: plot.m_curves) {
        if (c.id == m_curveId) {
            curve = &c;
            break;
        }
    }

    if (!curve) {  // Create a new curve
        plot.m_curves.emplace_back(m_curveId);
        curve = &plot.m_curves.back();
    }

    // Append data
    curve->xData.push_back(x);
    curve->yData.push_back(y);
}

void PlotManager::ClearAll() {
    std::lock_guard lock(m_mutex);
    for (auto &[yAxis, plot]: m_plots) {
        plot->m_curves.clear();
    }
}

void PlotManager::ClearCurrent() {
    std::lock_guard lock(m_mutex);
    for (auto &[yAxis, plot]: m_plots) {
        // Find the curve with the id == m_curveId
        Plot::Curve *curve = nullptr;
        for (auto &c: plot->m_curves) {
            if (c.id == m_curveId) {
                curve = &c;
                break;
            }
        }
        if (curve) {
            curve->xData.clear();
            curve->yData.clear();
        }
    }
}

void PlotManager::RequestFitAxes() {
    for (auto &[yAxis, plot]: m_plots) {
        plot->m_shouldFitAxes = true;
    }
}
