//
// Created by fengshi on 10/23/24.
//

#ifndef RADIANCEVIEW_H
#define RADIANCEVIEW_H

#include "View.h"
#include <pbrt/cpu/integrators.h>
#include <pbrt/util/framebuffer.h>
#include <atomic>


class RadianceView : public View {
public:
    RadianceView(pbrt::Application *parent, const pbrt::Primitive &scene, const std::vector<pbrt::Light> &lights);

    ~RadianceView();

    void RenderStart(const pbrt::Point3f &pos, const pbrt::Normal3f &normal);

    void RenderStep();

    void UpdateFramebuffer();

    void Clear();

    void Draw() override;

    bool IsRendering() const { return m_prev.valid && m_numSamples < m_spp; }

    double GetPDF(const pbrt::Point2i &p) const;

    pbrt::Point2i GetResolution() const { return m_resolution; }

    void SetResolution(const pbrt::Point2i &resolution);

private:
    void EvaluatePixelSample(pbrt::Point2i pPixel, int sampleIndex, pbrt::Sampler sampler, pbrt::ScratchBuffer &scratchBuffer);

    void RenderStart();

    pbrt::Primitive m_scene;
    const std::vector<pbrt::Light> &m_lights;
    std::unique_ptr<pbrt::PathIntegrator> m_integrator;
    std::unique_ptr<pbrt::IndependentSampler> m_sampler;
    pbrt::ThreadLocal<pbrt::ScratchBuffer> m_scratchBuffers;
    pbrt::CameraBaseParameters m_cbp;
    std::unique_ptr<pbrt::SphericalCamera> m_camera;
    pbrt::Point2i m_resolution{640, 320};

    std::vector<pbrt::RGB> m_cpuBuffer;
    double m_normalizer = 1;
    int m_numSamples;
    int m_spp = 64;
    std::atomic_bool m_cpuBufferUpdated = false;

    struct {
        bool valid = false;
        pbrt::Point3f pos;
        pbrt::Normal3f normal;
        bool localFrame = true;
    } m_prev;
    pbrt::Frame m_frame;

    bool m_pdf = true;
    bool &m_localFrame;
    float &m_exposure;
    float m_rayEps = 1e-3f;
    int m_maxDepth;

    float m_stepPhi;
    float m_stepTheta;

    GLuint m_renderingTex = 0;  // stores the selected cpu buffer
    Framebuffer m_framebuffer;
};

#endif //RADIANCEVIEW_H
