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

    std::pair<float, float> GetMinMaxFromFilm(SelectedChannel c, bool showFine) const;

    pbrt::Film film;
    const pstd::optional<pbrt::Image> &reference;
    struct ShaderData {
        float scale = 1.0f;
        float offset = 0.0f;
        Colormap cmap = CMap_None;
        bool firstNormalized = false;
    };
    ShaderData shaderData[Channel_Count];
    ShaderData shaderDataDiffCE {1.0f, 0.5f, CMap_RdYlGn, true};
    std::function<float(const pbrt::RGB&, const pbrt::RGB&)> errorFunc = GetErrorFunc(Metric_MRAE);

    bool isHovered = false;
    float hoveringValue = std::numeric_limits<float>::infinity();
};



#endif //COLORMAPPANEL_H
