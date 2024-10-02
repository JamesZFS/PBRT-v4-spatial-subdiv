//
// Created by fengshi on 9/30/24.
//

#ifndef VIEW_H
#define VIEW_H

#include "helper.h"

class View {
public:
    virtual ~View() = default;

    virtual void Draw() = 0;
    // void Update(Data *data);
};

#endif //VIEW_H
