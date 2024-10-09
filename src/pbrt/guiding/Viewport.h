//
// Created by fengshi on 9/30/24.
//

#ifndef VIEWPORT_H
#define VIEWPORT_H

#include "View.h"
#include <pbrt/util/framebuffer.h>
#include <pbrt/util/shader.h>
#include <pbrt/util/color.h>
#include <pbrt/film.h>

class Viewport : public View {
public:
    struct Uniforms {
        float scale, offset, clipValue;
        GLuint cmapTex;
    };

    Viewport(pbrt::Application* parent, pbrt::Film film, const pstd::optional<pbrt::Image> &reference);

    ~Viewport();

    void UpdateCPUBufferFromFilm();

    void UpdateFramebuffer(SelectedChannel channel, const Uniforms &uniforms);

    void Draw() override;

    void RequestUpdate() { m_cpuBufferUpdated = true; }

    inline bool IsHovered() const { return m_isHovered; }

    inline pbrt::Point2i GetMousePixel() const { return m_mousePixel; }

    inline ImVec2 GetLeftTop() const { return m_leftTop; }

    inline float GetScale() const { return m_scale; }

    double UpdateMeanError();

    inline double GetMeanError() const { return m_meanError; }

    float GetErrorAtPixel(pbrt::Point2i pixel) const;

    void UpdateErrorImage();

    std::function<float(const pbrt::RGB&, const pbrt::RGB&)> errorFunc = GetErrorFunc(Metric_MRAE);

private:
    pbrt::Film m_film;
    bool m_isMultiChannel;
    bool m_hasReference = false;
    pbrt::Point2i m_resolution;

    struct {
        std::vector<pbrt::RGB> radiance;
        std::vector<pbrt::RGB> cacheID;
        std::vector<float> fluence;
        std::vector<float> ce;
        std::vector<float> samples;
        std::vector<float> zeroSamples;
        std::vector<float> depth;
        std::vector<pbrt::RGB> reference;  // stays constant
        std::vector<float> error;
    } m_cpuBuffer;  // CPU film buffer, written by the render thread, read by the GUI thread.
    std::atomic_bool m_cpuBufferUpdated = false;
    double m_meanError = 0.0;

    GLuint m_renderingTex = 0;  // stores the selected cpu buffer
    Framebuffer m_framebuffer;

    ImVec2 m_leftTop;
    float m_scale = 1.0f;
    bool m_isHovered = false;
    pbrt::Point2i m_mousePixel;  // coordinates in the image space
};


#endif //VIEWPORT_H
