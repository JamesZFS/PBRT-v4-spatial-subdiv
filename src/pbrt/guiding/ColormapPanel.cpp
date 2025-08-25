//
// Created by fengshi on 10/1/24.
//

#include "ColormapPanel.h"
#include "Application.h"

using namespace pbrt;

ColormapPanel::ColormapPanel(pbrt::Application *parent, pbrt::Film film, const pstd::optional<pbrt::Image> &reference)
    : View(parent), film(film), reference(reference) {
    for (auto c: {Channel_Radiance, Channel_CacheID, Channel_Reference, Channel_Error}) {
        shaderData[c].firstNormalized = true;
    }
    for (auto c: {Channel_Energy, Channel_Fluence, Channel_Risk, Channel_Samples, /*Channel_ZeroSamples,*/ Channel_Depth}) {
        shaderData[c].cmap = CMap_Viridis;
    }
    shaderDataTValue.firstNormalized = true;
    shaderDataTValue.cmap = CMap_RdYlGn;
    shaderDataTValue.scale = (0.95f - 0.5f) / m_parent->GetTValueThreshold();  // Such that the threshold is at 0.95
    shaderDataTValue.offset = 0.5f / shaderDataTValue.scale;  // to make sure that white maps to 0
    for (auto c: {Channel_Radiance, Channel_Reference}) {
        shaderData[c].boundary = true;
    }
    shaderData[Channel_Error].cmap = CMap_Inferno;
}

void ColormapPanel::Draw() {
    ImGui::PushID("Colormap");
    isHovered = false;
    hoveringValue = std::numeric_limits<float>::infinity();
    auto c = m_parent->GetSelectedChannel();
    bool isID = c == Channel_CacheID;
    auto &io = ImGui::GetIO();
    if (c == Channel_Risk && m_parent->IsShowingTValue()) {  // t values are signed, so we need a special handling of scale and offset
        auto &sd = shaderDataTValue;
        auto reset = [&]() {
            sd.scale = 1.0f;
            sd.offset = 0.5f;
        };
        if (IsKeyPressed(ImGuiKey_E)) {
            if (!io.KeyShift) {
                sd.scale *= 1.1f;
            } else {
                sd.scale /= 1.1f;
            }
            sd.offset = 0.5f / sd.scale;  // to make sure that white maps to 0
        }
        if (IsKeyPressed(ImGuiKey_R, false)) reset();
        if (IsKeyPressed(ImGuiKey_B, false)) sd.boundary ^= true;
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Color Map")) {
            if (ImGui::DragFloat("Scale", &sd.scale, 0.01f, 0, 0, "%.8f")) {
                sd.offset = 0.5f / sd.scale;
            }
            if (ImGui::Button("Reset")) reset();
            ImGui::SameLine();
            ImGui::Checkbox("Boundaries", &sd.boundary);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            ImGui::Combo("Tonemap", reinterpret_cast<int *>(&sd.cmap), cmap_names, CMap_Count);
            if (sd.cmap != CMap_None) {
                ImGui::Image((void *) (uintptr_t) cmap_tex_ids[sd.cmap],
                             ImVec2(ImGui::GetColumnWidth(), ImGui::GetFrameHeight()));
                float xmin = ImGui::GetItemRectMin().x, xmax = ImGui::GetItemRectMax().x;
                isHovered |= ImGui::IsItemHovered();
                if (isHovered && ImGui::BeginTooltip()) {
                    float t = (io.MousePos.x - xmin) / (xmax - xmin);
                    hoveringValue = t / sd.scale - sd.offset;
                    ImGui::Text("Pos: %.2f", t);
                    ImGui::Text("Value: %.4f", hoveringValue);
                    ImGui::EndTooltip();
                }
            }
        }
    } else if (!isID) {
        auto &sd = shaderData[c];
        auto reset = [&]() {
            sd.scale = 1.0f;
            sd.offset = 0.0f;
        };
        auto normalize = [&]() {
            sd.firstNormalized = true;
            auto [minVal, maxVal] = GetMinMaxFromFilm(c);
            sd.scale = 1.0f / std::max(1e-6f, maxVal - minVal);
            sd.offset = -minVal;
        };
        if (IsKeyPressed(ImGuiKey_E)) {
            if (!io.KeyShift) {
                sd.scale *= 1.1f;
            } else {
                sd.scale /= 1.1f;
            }
        }
        if (IsKeyPressed(ImGuiKey_R, false)) reset();
        if (IsKeyPressed(ImGuiKey_N, false) || !sd.firstNormalized) normalize();
        if (IsKeyPressed(ImGuiKey_B, false)) sd.boundary ^= true;
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Color Map")) {
            ImGui::DragFloat("Scale", &sd.scale, 0.01f, 0, 0, "%.8f");
            ImGui::DragFloat("Offset", &sd.offset, 0.01f, 0, 0, "%.8f");
            if (ImGui::Button("Reset")) reset();
            ImGui::SameLine();
            if (ImGui::Button("Normalize")) normalize();
            ImGui::Checkbox("Boundaries", &sd.boundary);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            ImGui::Combo("Tonemap", reinterpret_cast<int *>(&sd.cmap), cmap_names, CMap_Count);
            if (sd.cmap != CMap_None) {
                ImGui::Image((void *) (uintptr_t) cmap_tex_ids[sd.cmap],
                             ImVec2(ImGui::GetColumnWidth(), ImGui::GetFrameHeight()));
                float xmin = ImGui::GetItemRectMin().x, xmax = ImGui::GetItemRectMax().x;
                isHovered |= ImGui::IsItemHovered();
                if (isHovered && ImGui::BeginTooltip()) {
                    float t = (io.MousePos.x - xmin) / (xmax - xmin);
                    hoveringValue = t / sd.scale - sd.offset;
                    ImGui::Text("Pos: %.2f", t);
                    ImGui::Text("Value: %.4f", hoveringValue);
                    ImGui::EndTooltip();
                }
            }
        }
    }
    ImGui::PopID();
}

std::pair<float, float> ColormapPanel::GetMinMaxFromFilm(SelectedChannel c) const {
    float minVal = std::numeric_limits<float>::infinity(), maxVal = -std::numeric_limits<float>::infinity();
    bool showTValue = m_parent->IsShowingTValue();
    bool showAngular = m_parent->IsShowingAngularDistance();
    if (film.Is<GuidedGBufferFilm>()) {
        ImageChannelDesc desc;
        if (c == Channel_Reference || c == Channel_Error) {
            CHECK(reference);
            CHECK_GE(reference->NChannels(), 3);
            desc = reference->GetChannelDesc({"R", "G", "B"});
        }
        auto *gFilm = film.Cast<GuidedGBufferFilm>();
        for (int y = film.PixelBounds().pMin.y; y < film.PixelBounds().pMax.y; ++y) {
            for (int x = film.PixelBounds().pMin.x; x < film.PixelBounds().pMax.x; ++x) {
                auto &pixel = gFilm->GetPixel(Point2i(x, y));
                float val;
                switch (c) {
                    case Channel_Radiance:
                        val = gFilm->GetPixelRGB(Point2i(x, y)).Average();
                        break;
                    case Channel_Energy:
                        val = pixel.guidingData.energy;
                        break;
                    case Channel_Fluence:
                        val = pixel.guidingData.fluence;
                        break;
                    case Channel_Risk:
                        val = showTValue ? pixel.guidingData.tValue : showAngular ? pixel.guidingData.angularEnergy : pixel.guidingData.risk;
                        break;
                    case Channel_Samples:
                        val = (float) pixel.guidingData.numSamples;
                        break;
                    // case Channel_ZeroSamples:
                    //     val = (float) pixel.guidingData.numZeroValueSamples;
                    //     break;
                    case Channel_Depth:
                        val = (float) pixel.guidingData.depth;
                        break;
                    case Channel_Reference:
                        val = reference->GetChannels(Point2i(x, y), desc).Average();
                        break;
                    case Channel_Error: {
                        auto ref = reference->GetChannels(Point2i(x, y), desc);
                        val = errorFunc(gFilm->GetPixelRGB(Point2i(x, y)), RGB(ref[0], ref[1], ref[2]));
                        break;
                    }
                    default:
                        Error("Unknown channel type %d", (int) c);
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
    std::cout << "minVal: " << minVal << " maxVal: " << maxVal << std::endl;
    return {minVal, maxVal};
}
