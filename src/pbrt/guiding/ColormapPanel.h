//
// Created by fengshi on 10/1/24.
//

#ifndef COLORMAPPANEL_H
#define COLORMAPPANEL_H


#include "View.h"
#include <pbrt/film.h>


struct ColormapPanel : public View {
    ColormapPanel(pbrt::Application *parent, pbrt::Film film);

    void Draw() override;

    std::pair<float, float> GetMinMaxFromFilm(SelectedChannel c) const;

    pbrt::Film film;
    Colormap selectedCMap = CMap_Viridis;
    struct {
        float scale = 1.0f;
        float offset = 0.0f;
        bool tonemapped = false;
        bool firstNormalized = false;
    } shaderData[Channel_Count];

    bool isHovered = false;
    float hoveringValue = std::numeric_limits<float>::infinity();
};



#endif //COLORMAPPANEL_H
