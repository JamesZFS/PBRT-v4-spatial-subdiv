//
// Created by fengshi on 10/7/24.
//

#include "CacheHistogram.h"
#include "Application.h"

#include "implot.h"
#include "pbrt/util/error.h"

using namespace pbrt;

void CacheHistogram::Hist::Draw() {
    isHovered = false;
    hoveringValue = std::numeric_limits<float>::infinity();
    if (m_isMain) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::BulletText("Displays the histogram of the cache data. (not just in screen space!)");
            ImGui::BulletText("Hover over the plot of the selected channel with the tonemap enabled to see regions with lower values.");
            ImGui::BulletText("Click on the plot to select the channel.");
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto Fit", &m_object.m_autoFitAxes);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 2);
        ImGui::SliderInt("N Bins", &m_object.m_bins, -1, 100);
    }

    if (m_shouldFitAxes.exchange(false) && m_object.m_autoFitAxes)
        ImPlot::SetNextAxesToFit();

    // Use an eccentric color when the selected channel is same as the current histogram
    static const SelectedChannel histType2Channel[] = {Channel_Fluence, Channel_CE, Channel_Depth, Channel_Samples};
    static const ImU32 unselectedCol = ImGui::GetColorU32({0.2f, 0.4f, 0.6f, 1.f});
    static const ImU32 selectedCol = ImGui::GetColorU32({0.6f, 0.4f, 0.2f, 1.f});
    ImU32 col = m_parent->GetSelectedChannel() == histType2Channel[m_type] ? selectedCol : unselectedCol;
    ImPlot::PushStyleColor(ImPlotCol_Fill, ImGui::GetColorU32(col, 0.8f));
    ImPlot::PushStyleColor(ImPlotCol_Line, col);

    ImPlotFlags plot_flags = ImPlotFlags_NoLegend;
    if (!enableTitle) plot_flags |= ImPlotFlags_NoTitle;
    ImPlotHistogramFlags hist_flags = ImPlotHistogramFlags_None;
    if (ImPlot::BeginPlot(title.c_str(), ImVec2(-1, -1), plot_flags)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoLabel, ImPlotAxisFlags_NoLabel);
        isHovered |= ImPlot::IsPlotHovered();
        // Draw a vertical line at the mouse position
        if (isHovered) {
            ImDrawList *draw_list = ImPlot::GetPlotDrawList();
            ImPlotPoint mouse = ImPlot::GetPlotMousePos();
            hoveringValue = mouse.x;
            ImVec2 screen_pos = ImPlot::PlotToPixels(mouse);
            ImPlot::PushPlotClipRect();
            draw_list->AddLine(ImVec2(screen_pos.x, ImPlot::GetPlotPos().y), ImVec2(screen_pos.x, ImPlot::GetPlotPos().y + ImPlot::GetPlotSize().y), ImGui::GetColorU32(IM_COL32_WHITE, 0.8f));
            ImPlot::PopPlotClipRect();
        }
        // Handle left click
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_parent->SetSelectedChannel(histType2Channel[m_type]);
        }

        std::lock_guard lock(m_object.m_mutex);
        switch (m_type) {
            case PlotType_Fluence:
                ImPlot::PlotHistogram(title.c_str(), m_object.m_data.fluence.data(), m_object.m_data.fluence.size(), m_object.m_bins, 1, {}, hist_flags);
                break;
            case PlotType_CE:
                ImPlot::PlotHistogram(title.c_str(), m_object.m_data.ce.data(), m_object.m_data.ce.size(), m_object.m_bins, 1, {}, hist_flags);
                break;
            case PlotType_Depth:
                ImPlot::PlotHistogram(title.c_str(), m_object.m_data.depth.data(), m_object.m_data.depth.size(), m_object.m_bins, 1, {}, hist_flags);
                break;
            case PlotType_Samples:
                ImPlot::PlotHistogram(title.c_str(), m_object.m_data.samples.data(), m_object.m_data.samples.size(), m_object.m_bins, 1, {}, hist_flags);
                break;
            default:
                ErrorExit("Unhandled plot type");
        }
        ImPlot::EndPlot();
    }

    ImPlot::PopStyleColor(2);
}

CacheHistogram::CacheHistogram(pbrt::Application* parent) : m_parent(parent) {
    m_bins = ImPlotBin_Sqrt;
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
    auto plot = new Hist(m_parent, isMain, title, type, *this);
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
    m_bins = ImPlotBin_Sqrt;
    RequestFitAxes();
}
