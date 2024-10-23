//
// Created by fengshi on 10/2/24.
//

#include "CacheMonitor.h"
#include "Application.h"
#include <implot.h>
#include <implot_internal.h>

using namespace pbrt;

void CacheMonitor::Plot::Draw() {
    if (m_isMain) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::BulletText("Middle click viewport to insert a guiding cache probe");
            ImGui::BulletText("Right click a probe to remove it");
            ImGui::BulletText("The lower edges of shaded area represent the split CE");
            ImGui::BulletText("The white error bars represent the lookahead child's CE");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Display Probe ID", &m_monitor.m_displayProbeID);
        ImGui::SameLine();
        ImGui::Checkbox("Auto Fit", &m_monitor.m_autoFitAxes);
        ImGui::SameLine();
        ImGui::Checkbox("Lookaheads", &m_monitor.m_plotLookahead);
    }

    ImPlotAxisFlags flags = ImPlotAxisFlags_NoLabel;
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.3));
    ImPlot::PushStyleVar(ImPlotStyleVar_Marker, ImPlotMarker_Cross);
    ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, m_monitor.m_markerSize);
    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, m_monitor.m_lineWeight);
    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, m_monitor.m_alpha);
    if (m_shouldFitAxes.exchange(false) && m_monitor.m_autoFitAxes)
        ImPlot::SetNextAxesToFit();

    ImPlotFlags plot_flags = ImPlotFlags_NoTitle;
    const int yOffset = (int) m_type + 1;
    if (ImPlot::BeginPlot(m_title.c_str(), ImVec2(-1, ImGui::GetContentRegionAvail().y - (m_isMain ? ImGui::GetFrameHeightWithSpacing() : 0.f)), plot_flags)) {
        ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
        // Hovering behavior: draw a vertical line for all plots at the same x position
        ImDrawList *draw_list = ImPlot::GetPlotDrawList();
        if (ImPlot::IsPlotHovered()) {
            m_monitor.m_mouse.hoveringID = this->ID();
            ImPlotPoint mouse = ImPlot::GetPlotMousePos();
            m_monitor.m_mouse.x = mouse.x, m_monitor.m_mouse.y = mouse.y;
            ImVec2 screen_pos = ImPlot::PlotToPixels(mouse);
            ImPlot::PushPlotClipRect();
            draw_list->AddLine(ImVec2(screen_pos.x, ImPlot::GetPlotPos().y), ImVec2(screen_pos.x, ImPlot::GetPlotPos().y + ImPlot::GetPlotSize().y), ImGui::GetColorU32(IM_COL32_WHITE, 0.8f));
            ImPlot::PopPlotClipRect();
        }
        else if (m_monitor.m_mouse.hoveringID == this->ID()) {  // Was hovering on this plot
            m_monitor.m_mouse.hoveringID = 0;
        }
        else if (m_monitor.m_mouse.hoveringID != 0) {  // Hovering on other plot
            ImVec2 screen_pos = ImPlot::PlotToPixels(m_monitor.m_mouse.x, m_monitor.m_mouse.y);
            ImPlot::PushPlotClipRect();
            draw_list->AddLine(ImVec2(screen_pos.x, ImPlot::GetPlotPos().y), ImVec2(screen_pos.x, ImPlot::GetPlotPos().y + ImPlot::GetPlotSize().y), ImGui::GetColorU32(IM_COL32_WHITE, 0.4f));
            ImPlot::PopPlotClipRect();
        }

        std::lock_guard lock(m_monitor.m_mutex);
        for (int i = 0; i < m_monitor.m_probes.size(); ++i) {
            const auto &probe = m_monitor.m_probes[i];
            const float *x = &probe.data[0].iter;
            auto label = StringPrintf("#%d", probe.idx);
            auto lineColor = ImPlot::GetColormapColor(i);
            if (m_type == PlotType_CE && m_monitor.m_plotLookahead) {  // Plot the child CE and split CE
                auto barColor = ImPlot::GetStyleColorVec4(ImPlotCol_ErrorBar);
                barColor.w = 0.5f + 0.5f * m_monitor.m_alpha;
                float barSize = 2 * m_monitor.m_markerSize;
                ImPlot::SetNextErrorBarStyle(barColor, barSize);
                ImPlot::PlotErrorBars(label.c_str(), x, &probe.data[0].coarseCE, &probe.data[0].negerr, &probe.data[0].poserr, probe.data.size(), 0, 0, sizeof(PlotEntry));
                // ImPlot::SetNextLineStyle(ImVec4(lineColor.x, lineColor.y, lineColor.z, m_monitor.m_alpha));
                // ImPlot::PlotLine(label.c_str(), x, &probe.data[0].splitCE, probe.data.size(), 0, 0, sizeof(PlotEntry));
                ImPlot::PlotShaded(label.c_str(), x, &probe.data[0].coarseCE, &probe.data[0].splitCE, probe.data.size(), 0, 0, sizeof(PlotEntry));
            }
            ImPlot::SetNextLineStyle(lineColor);
            ImPlot::PlotLine(label.c_str(), x, x + yOffset, probe.data.size(), 0, 0, sizeof(PlotEntry));
        }
        ImPlot::EndPlot();
    }

    ImPlot::PopStyleVar(5);

    if (m_isMain) {
        if (ImGui::Button("Clear"))
            m_monitor.Clear();
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            m_monitor.Reset();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(50);
        ImGui::DragFloat("Marker", &m_monitor.m_markerSize, 0.03f, 0, 5);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(50);
        ImGui::DragFloat("Line", &m_monitor.m_lineWeight, 0.03f, 0, 5);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(50);
        ImGui::DragFloat("Alpha", &m_monitor.m_alpha,0.01f,0,1);
    }
}

CacheMonitor::~CacheMonitor() {
    for (auto plot: m_plots)
        delete plot;
}

CacheMonitor::Plot &CacheMonitor::AddPlot(const std::string &title, PlotType type, bool isMain) {
    auto plot = new Plot(m_parent, isMain, title, type, *this);
    m_plots.push_back(plot);
    return *plot;
}

bool CacheMonitor::AddProbe(pbrt::Point2i &pixel) {
    std::lock_guard lock(m_mutex);
    for (auto &probe: m_probes)
        if (Distance(pixel, probe.pixel) < m_probeRadius) {  // Already exists
            probe.active = true;
            pixel = probe.pixel;
            return false;
        }
    m_probes.push_back(Probe{true, pixel, (int) m_probes.size()});
    return true;
}

void CacheMonitor::UpdateProbe(pbrt::Point2i pixel, std::function<void(Probe &)> f) {
    std::lock_guard lock(m_mutex);
    for (auto &probe: m_probes)
        if (Distance(pixel, probe.pixel) < m_probeRadius) {  // Already exists
            f(probe);
            break;
        }
    RequestFitAxes();
}

void CacheMonitor::ForEachProbe(std::function<void(Probe &)> f) {
    std::lock_guard lock(m_mutex);
    for (auto &probe: m_probes)
        f(probe);
}

void CacheMonitor::RequestFitAxes() {
    for (auto &plot: m_plots)
        plot->m_shouldFitAxes = true;
}

void CacheMonitor::Clear() {  // Clear the data but keep the probes
    std::lock_guard lock(m_mutex);
    for (auto &probe: m_probes)
        probe.data.clear();
    RequestFitAxes();
}

void CacheMonitor::Reset() {
    std::lock_guard lock(m_mutex);
    m_probes.clear();
    ImPlot::DestroyContext();
    ImPlot::CreateContext();
    RequestFitAxes();
}
