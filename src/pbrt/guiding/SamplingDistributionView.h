//
// Created by fengshi on 10/9/24.
//

#ifndef SAMPLINGDISTRIBUTIONVIEW_H
#define SAMPLINGDISTRIBUTIONVIEW_H


#include "View.h"
#include <pbrt/util/framebuffer.h>
#include <atomic>
#include <mutex>
#include <openpgl/cpp/OpenPGL.h>


class SamplingDistributionView : public View {
public:
    SamplingDistributionView(pbrt::Application *parent, const openpgl::cpp::Field &field);

    ~SamplingDistributionView();

    void UpdateCPUBuffer(const pbrt::Point3f &pos, const pbrt::Normal3f &normal);

    void UpdateFramebuffer();

    void Clear();

    void Draw() override;

private:
    void UpdateCPUBuffer();

    const openpgl::cpp::Field &m_field;
    openpgl::cpp::SurfaceSamplingDistribution m_ssd;
    pbrt::Point2i m_resolution{640, 320};

    struct {
        std::vector<float> pdf;
#ifdef OPENPGL_RADIANCE_CACHES
        std::vector<pbrt::RGB> Li;
        std::vector<pbrt::RGB> Lo;
#endif
    } m_cpuBuffer;
    std::atomic_bool m_cpuBufferUpdated = false;

    enum SelectedBuffer {
        Buffer_PDF,
#ifdef OPENPGL_RADIANCE_CACHES
        Buffer_Li,
        Buffer_Lo,
#endif
        Buffer_Count
    } m_selectedBuffer = Buffer_PDF;

    struct {
        bool valid = false;
        pbrt::Point3f pos;
        pbrt::Normal3f normal;
    } m_prev;
    bool m_enableCosineProduct = true;
    float m_exposure = 1.0f;
    Colormap m_colormap = CMap_Viridis;

    float m_stepPhi;
    float m_stepTheta;

    GLuint m_renderingTex = 0;  // stores the selected cpu buffer
    Framebuffer m_framebuffer;
};



#endif //SAMPLINGDISTRIBUTIONVIEW_H
