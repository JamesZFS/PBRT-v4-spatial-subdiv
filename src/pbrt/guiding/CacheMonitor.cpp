//
// Created by fengshi on 10/2/24.
//

#include "CacheMonitor.h"

#include "implot.h"

using namespace pbrt;

void CacheMonitor::Plot::Draw() {
    if (m_isMain) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::BulletText("Left click canvas to insert a guiding cache probe");
            ImGui::BulletText("Right click a probe to remove it");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Display Probe ID", &m_monitor.m_displayProbeID);
        ImGui::SameLine();
        ImGui::Checkbox("Auto Fit", &m_monitor.m_autoFitAxes);
    }

    ImPlotAxisFlags flags = ImPlotAxisFlags_NoLabel;
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.3));
    ImPlot::PushStyleVar(ImPlotStyleVar_Marker, ImPlotMarker_Cross);
    ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, m_monitor.m_markerSize);
    if (m_shouldFitAxes.exchange(false) && m_monitor.m_autoFitAxes)
        ImPlot::SetNextAxesToFit();

    if (ImPlot::BeginPlot(m_title.c_str(), ImVec2(-1, ImGui::GetContentRegionAvail().y - (m_isMain ? 40.f : 0.f)), ImPlotFlags_NoTitle)) {
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
        ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
        for (const auto &probe: m_monitor.m_probes) {
            const float *x = &probe.data[0].iter;
            ImPlot::PlotLine(StringPrintf("#%d", probe.idx).c_str(), x, x + m_yOffset, probe.data.size(), 0, 0, sizeof(PlotEntry));
        }
        ImPlot::EndPlot();
    }

    ImPlot::PopStyleVar(3);

    if (m_isMain) {
        if (ImGui::Button("Clear"))
            m_monitor.Clear();
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            m_monitor.Reset();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 2);
        ImGui::SliderFloat("Marker Size", &m_monitor.m_markerSize, 0, 5);
    }
}

CacheMonitor::~CacheMonitor() {
    for (auto plot: m_plots)
        delete plot;
}

CacheMonitor::Plot &CacheMonitor::AddPlot(const std::string &title, PlotType type, bool isMain) {
    auto plot = new Plot(isMain, title, 1 + (int) type, *this);
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
    ImPlot::DestroyContext();
    ImPlot::CreateContext();
    RequestFitAxes();
}

void CacheMonitor::Reset() {
    std::lock_guard lock(m_mutex);
    m_probes.clear();
    ImPlot::DestroyContext();
    ImPlot::CreateContext();
    RequestFitAxes();
}
