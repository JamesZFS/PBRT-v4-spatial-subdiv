//
// Created by fengshi on 9/30/24.
//

#ifndef VIEWPORT_H
#define VIEWPORT_H

#include "View.h"
#include <pbrt/util/framebuffer.h>
#include <pbrt/util/shader.h>
#include <pbrt/util/color.h>

class Viewport : public View {
public:
    Viewport(pbrt::Film film);

    ~Viewport();

    void UpdateCPUBufferFromFilm();

    void PossiblyUpdateFramebuffer(SelectedChannel channel, float scale, float offset, GLuint cmapTex);

    void Draw() override;

    void RequestUpdate() { m_cpuBufferUpdated = true; }

private:
    pbrt::Film m_film;
    bool m_isMultiChannel;
    pbrt::Vector2i m_resolution;

    struct {
        std::vector<pbrt::RGB> radiance;
        std::vector<pbrt::RGB> cacheID;
        std::vector<float> fluence;
        std::vector<float> ce;
    } m_cpuBuffer;  // CPU film buffer, written by the render thread, read by the GUI thread.
    std::atomic_bool m_cpuBufferUpdated = false;

    GLuint m_renderingTex = 0;  // stores the selected cpu buffer
    Framebuffer m_framebuffer;
};


#endif //VIEWPORT_H
