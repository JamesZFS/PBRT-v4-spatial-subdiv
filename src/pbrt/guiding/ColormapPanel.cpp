//
// Created by fengshi on 10/1/24.
//

#include "ColormapPanel.h"

using namespace pbrt;

static const char* cmap_names[CMap_Count] = {
    "Cividis",
    "Inferno",
    "Magma",
    "Plasma",
    "Viridis"
};

ColormapPanel::ColormapPanel(pbrt::Film film) : film(film) {
    shaderData[Channel_Radiance].firstNormalized = true;
    shaderData[Channel_CacheID].firstNormalized = true;
}

void ColormapPanel::Draw() {
    bool disableColorMap = selectedChannel == Channel_CacheID;
    ImGui::BeginDisabled(disableColorMap);
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    auto &io = ImGui::GetIO();
    auto &sd = shaderData[selectedChannel];
    auto reset = [&]() {
        sd.scale = 1.0f;
        sd.offset = 0.0f;
    };
    auto normalize = [&]() {
        sd.firstNormalized = true;
        auto [minVal, maxVal] = GetMinMaxFromFilm(selectedChannel);
        sd.scale = 1.0f / std::max(1e-6f, maxVal - minVal);
        sd.offset = -minVal;
    };
    if (!disableColorMap) {  // Key maps
        if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            if (!io.KeyShift) {
                sd.scale *= 1.1f;
            } else {
                sd.scale /= 1.1f;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_M, false)) {
            sd.tonemapped ^= true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) reset();
        if (ImGui::IsKeyPressed(ImGuiKey_N, false) || !sd.firstNormalized) normalize();
    }
    if (ImGui::CollapsingHeader("Color Map")) {

        ImGui::InputFloat("Scale", &sd.scale, 0.1f, 1.0f);
        ImGui::InputFloat("Offset", &sd.offset, 0.1f, 1.0f);
        if (ImGui::Button("Reset")) reset();
        ImGui::SameLine();
        if (ImGui::Button("Normalize")) normalize();
        ImGui::SetNextItemWidth(90);
        ImGui::Combo("Tonemap", reinterpret_cast<int *>(&selectedCMap), cmap_names, CMap_Count);
        ImGui::SameLine();
        ImGui::Checkbox("##check_tonemap", &sd.tonemapped);
        hoveringValue = std::numeric_limits<float>::infinity();
        if (sd.tonemapped) {
            ImGui::Image((void *) (uintptr_t) cmap_tex_ids[selectedCMap], ImVec2(ImGui::GetColumnWidth(), ImGui::GetFrameHeight()));
            float xmin = ImGui::GetItemRectMin().x, xmax = ImGui::GetItemRectMax().x;
            if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
                float t = (io.MousePos.x - xmin) / (xmax - xmin);
                hoveringValue = t / sd.scale - sd.offset;
                ImGui::Text("Pos: %.2f", t);
                ImGui::Text("Value: %.4f", hoveringValue);
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::EndDisabled();
}

std::pair<float, float> ColormapPanel::GetMinMaxFromFilm(SelectedChannel c) const {
    float minVal = std::numeric_limits<float>::infinity(), maxVal = -std::numeric_limits<float>::infinity();
    if (film.Is<GuidedGBufferFilm>()) {
        auto *gFilm = film.Cast<GuidedGBufferFilm>();
        for (int y = film.PixelBounds().pMin.y; y < film.PixelBounds().pMax.y; ++y) {
            for (int x = film.PixelBounds().pMin.x; x < film.PixelBounds().pMax.x; ++x) {
                auto &pixel = gFilm->GetPixel(Point2i(x, y));
                float val;
                switch (c) {
                    case Channel_Radiance:
                        val = gFilm->GetPixelRGB(Point2i(x, y)).Average();
                        break;
                    case Channel_Fluence:
                        val = pixel.guidingData.fluence;
                        break;
                    case Channel_CE:
                        val = pixel.guidingData.crossEntropy;
                        break;
                    case Channel_Samples:
                        val = (float) pixel.guidingData.numSamples;
                        break;
                    case Channel_ZeroSamples:
                        val = (float) pixel.guidingData.numZeroValueSamples;
                        break;
                    case Channel_Depth:
                        val = (float) pixel.guidingData.depth;
                        break;
                    case Channel_Count:
                        break;
                    default:
                        Error("Unknown channel type %d", c);
                }
                minVal = std::min(minVal, val);
                maxVal = std::max(maxVal, val);
            }
        }
    } else {
        for (int y = film.PixelBounds().pMin.y; y < film.PixelBounds().pMax.y; ++y) {
            for (int x = film.PixelBounds().pMin.x; x < film.PixelBounds().pMax.x; ++x) {
                float val = film.GetPixelRGB(Point2i(x, y)).Average();
                minVal = std::min(minVal, val);
                maxVal = std::max(maxVal, val);
            }
        }
    }
    return {minVal, maxVal};
}
