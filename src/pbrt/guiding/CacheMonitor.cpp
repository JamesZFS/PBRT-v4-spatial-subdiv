//
// Created by fengshi on 10/2/24.
//

#include "CacheMonitor.h"

#include "implot.h"

using namespace pbrt;

void CacheMonitor::Draw() {
    ImGui::BulletText("Left click canvas to insert a guiding cache probe");
    ImGui::BulletText("Right click a probe to remove it");

    ImPlotAxisFlags flags = ImPlotAxisFlags_NoLabel;
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.3));
    ImPlot::PushStyleVar(ImPlotStyleVar_Marker, ImPlotMarker_Cross);
    ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 2);
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

    if (ImGui::Button("Reset Data"))
        Reset();
    ImGui::SameLine();
    if (ImGui::Button("Reset All"))
        Clear();
    ImGui::SameLine();
    if (ImGui::Button("Fit"))
        m_shouldFitAxes = true;
}

bool CacheMonitor::AddProbe(pbrt::Point2i pixel) {
    std::lock_guard lock(m_mutex);
    for (auto &probe: m_probes)
        if (Distance(pixel, probe.pixel) < m_probeRadius) {  // Already exists
            probe.active = true;
            return false;
        }
    m_probes.push_back(Probe{true, pixel, (int) m_probes.size()});
    return true;
}

bool CacheMonitor::AddProbe(pbrt::Point2i pixel, PlotEntry entry) {
    std::lock_guard lock(m_mutex);
    for (auto &probe: m_probes)
        if (Distance(pixel, probe.pixel) < m_probeRadius) {  // Already exists
            probe.active = true;
            if (probe.data.empty() || probe.data.back().x < entry.x)
                probe.data.push_back(entry);
            return false;
        }
    m_probes.push_back(Probe{true, pixel, (int) m_probes.size(), {entry}});
    m_shouldFitAxes = true;
    return true;
}

void CacheMonitor::ForEachProbe(std::function<void(Probe &)> f) {
    std::lock_guard lock(m_mutex);
    for (auto &probe: m_probes)
        f(probe);
}

void CacheMonitor::Reset() {
    std::lock_guard lock(m_mutex);
    for (auto &probe: m_probes)
        probe.data.clear();
    ImPlot::DestroyContext();
    ImPlot::CreateContext();
    m_shouldFitAxes = true;
}

void CacheMonitor::Clear() {
    std::lock_guard lock(m_mutex);
    m_probes.clear();
    ImPlot::DestroyContext();
    ImPlot::CreateContext();
    m_shouldFitAxes = true;
}
