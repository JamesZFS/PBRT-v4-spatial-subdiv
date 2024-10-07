//
// Created by fengshi on 10/7/24.
//

#include "CacheHistogram.h"

#include "implot.h"

using namespace pbrt;

void CacheHistogram::Hist::Draw() {
    if (m_isMain) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::Text("Displays the histogram of the cache data. (not just in screen space!)");
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto Fit", &m_parent.m_autoFitAxes);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 2);
        ImGui::SliderInt("N Bins", &m_parent.m_bins, 1, 100);
    }

    ImPlotAxisFlags flags = ImPlotAxisFlags_NoLabel;
    if (m_shouldFitAxes.exchange(false) && m_parent.m_autoFitAxes)
        ImPlot::SetNextAxesToFit();

    if (ImPlot::BeginPlot(m_title.c_str(), ImVec2(-1, ImGui::GetContentRegionAvail().y - (m_isMain ? 20.f : 0.f)), ImPlotFlags_NoTitle)) {
        std::lock_guard lock(m_parent.m_mutex);
        ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
        switch (m_type) {
            case PlotType_Fluence:
                ImPlot::PlotHistogram(m_title.c_str(), m_parent.m_data.fluence.data(), m_parent.m_data.fluence.size(), m_parent.m_bins);
                break;
            case PlotType_CE:
                ImPlot::PlotHistogram(m_title.c_str(), m_parent.m_data.ce.data(), m_parent.m_data.ce.size(), m_parent.m_bins);
                break;
            case PlotType_Depth:
                ImPlot::PlotHistogram(m_title.c_str(), m_parent.m_data.depth.data(), m_parent.m_data.depth.size(), m_parent.m_bins);
                break;
            case PlotType_Samples:
                ImPlot::PlotHistogram(m_title.c_str(), m_parent.m_data.samples.data(), m_parent.m_data.samples.size(), m_parent.m_bins);
                break;
        }
        ImPlot::EndPlot();
    }
}

CacheHistogram::CacheHistogram() {
    m_bins = ImPlotBin_Sturges;
    m_data.fluence.reserve(1000);
    m_data.ce.reserve(1000);
    m_data.depth.reserve(1000);
    m_data.samples.reserve(1000);
}

CacheHistogram::~CacheHistogram() {
    for (auto plot: m_plots)
        delete plot;
}

CacheHistogram::Hist & CacheHistogram::AddPlot(const std::string &title, PlotType type, bool isMain) {
    auto plot = new Hist(isMain, title, type, *this);
    m_plots.push_back(plot);
    return *plot;
}

void CacheHistogram::Update(std::function<void(Data &)> f) {
    std::lock_guard lock(m_mutex);
    f(m_data);
}

void CacheHistogram::RequestFitAxes() {
    for (auto plot: m_plots)
        plot->m_shouldFitAxes = true;
}

void CacheHistogram::Clear() {
    std::lock_guard lock(m_mutex);
    m_data.fluence.clear();
    m_data.ce.clear();
    m_data.depth.clear();
    m_data.samples.clear();
    m_bins = ImPlotBin_Sturges;
    RequestFitAxes();
}
