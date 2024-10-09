//
// Created by fengshi on 9/30/24.
//

#ifndef VIEW_H
#define VIEW_H

#include "helper.h"

namespace pbrt {
class Application;
}

class View {
public:
    explicit View(pbrt::Application *parent) : m_parent(parent) {}

    virtual ~View() = default;

    virtual void Draw() = 0;
    // void Update(Data *data);

protected:
    bool IsKeyPressed(ImGuiKey key, bool repeat = true);

    pbrt::Application *m_parent = nullptr;
};

#endif //VIEW_H
