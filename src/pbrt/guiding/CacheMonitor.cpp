//
// Created by fengshi on 10/2/24.
//

#include "CacheMonitor.h"

#include "implot.h"

using namespace pbrt;

void CacheMonitor::Draw() {
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
    ImGui::Checkbox("Display Probe ID", &m_displayProbeID);

    ImPlotAxisFlags flags = ImPlotAxisFlags_NoLabel;
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.3));
    ImPlot::PushStyleVar(ImPlotStyleVar_Marker, ImPlotMarker_Cross);
    ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, m_markerSize);
    if (m_shouldFitAxes.exchange(false))
        ImPlot::SetNextAxesToFit();
    if (ImPlot::BeginPlot("CE vs. Iter", ImVec2(-1, ImGui::GetContentRegionAvail().y - 40))) {
        std::lock_guard lock(m_mutex);
        ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
        for (const auto &probe: m_probes)
            if (probe.active) {
                auto &data = probe.data;
                ImPlot::PlotLine(StringPrintf("#%d", probe.idx).c_str(), &data[0].x, &data[0].y, data.size(), 0, 0, sizeof(PlotEntry));
            }
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar(3);

    if (ImGui::Button("Clear"))
        Clear();
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
        Reset();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 2);
    ImGui::SliderFloat("Marker Size", &m_markerSize, 0, 5);
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
    m_shouldFitAxes = true;
}

void CacheMonitor::ForEachProbe(std::function<void(Probe &)> f) {
    std::lock_guard lock(m_mutex);
    for (auto &probe: m_probes)
        f(probe);
}

void CacheMonitor::Clear() {  // Clear the data but keep the probes
    std::lock_guard lock(m_mutex);
    for (auto &probe: m_probes)
        probe.data.clear();
    ImPlot::DestroyContext();
    ImPlot::CreateContext();
    m_shouldFitAxes = true;
}

void CacheMonitor::Reset() {
    std::lock_guard lock(m_mutex);
    m_probes.clear();
    ImPlot::DestroyContext();
    ImPlot::CreateContext();
    m_shouldFitAxes = true;
}
