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
    Viewport(pbrt::Application* parent, pbrt::Film film, const pstd::optional<pbrt::Image> &reference);

    ~Viewport();

    void UpdateCPUBufferFromFilm();

    void UpdateFramebuffer(const TonemapShaderUniforms &uniforms);

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
        struct {
            std::vector<pbrt::RGB> coarse;
            std::vector<pbrt::RGB> fine;
            std::vector<pbrt::RGB> diff;
        } cacheID;
        std::vector<float> fluence;
        struct {
            std::vector<float> coarse;
            std::vector<float> fine;
            std::vector<float> diff;
        } ce;
        std::vector<float> embeddingDist;
        std::vector<float> samples;
        std::vector<float> zeroSamples;
        std::vector<float> depth;
        std::vector<pbrt::RGB> reference;  // stays constant
        std::vector<float> error;
    } m_cpuBuffer;  // CPU film buffer, written by the render thread, read by the GUI thread.
    std::atomic_bool m_cpuBufferUpdated = false;
    double m_meanError = 0.0;

    GLuint m_renderingTex = 0;  // stores the selected cpu buffer
    GLuint m_cacheIDTex = 0;
    GLuint m_fineIDTex = 0;
    Framebuffer m_framebuffer;
    Framebuffer m_overlayFineFramebuffer;
    Framebuffer m_overlayCoarseFramebuffer;

    ImVec2 m_leftTop;
    float m_scale = 1.0f;
    bool m_isHovered = false;
    pbrt::Point2i m_mousePixel;  // coordinates in the image space
};


#endif //VIEWPORT_H
