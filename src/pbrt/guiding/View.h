//
// Created by fengshi on 9/30/24.
//

#ifndef VIEW_H
#define VIEW_H

#include "helper.h"

enum SelectedChannel {
    Channel_Radiance = 0,
    Channel_CacheID,
    Channel_Fluence,
    Channel_CE,
    Channel_Count,
};

enum Colormap {
    CMap_Cividis = 0,
    CMap_Inferno,
    CMap_Magma,
    CMap_Plasma,
    CMap_Viridis,
    CMap_Count,
};

class View {
public:
    virtual ~View() = default;

    virtual void Draw() = 0;
    // void Update(Data *data);
};

#endif //VIEW_H
