//
// Created by fengshi on 10/1/24.
//

#ifndef COLORMAPPANEL_H
#define COLORMAPPANEL_H


#include "View.h"
#include <pbrt/film.h>


struct ColormapPanel : public View {
    ColormapPanel(pbrt::Application *parent, pbrt::Film film, const pstd::optional<pbrt::Image> &reference);

    void Draw() override;

    std::pair<float, float> GetMinMaxFromFilm(SelectedChannel c) const;

    pbrt::Film film;
    const pstd::optional<pbrt::Image> &reference;
    Colormap selectedCMap = CMap_Viridis;
    struct {
        float scale = 1.0f;
        float offset = 0.0f;
        bool tonemapped = false;
        bool firstNormalized = false;
    } shaderData[Channel_Count];
    std::function<float(const pbrt::RGB&, const pbrt::RGB&)> errorFunc = GetErrorFunc(Metric_MRAE);

    bool isHovered = false;
    float hoveringValue = std::numeric_limits<float>::infinity();
};



#endif //COLORMAPPANEL_H
