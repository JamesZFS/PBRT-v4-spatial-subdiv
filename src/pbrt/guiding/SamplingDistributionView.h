//
// Created by fengshi on 10/9/24.
//

#ifndef SAMPLINGDISTRIBUTIONVIEW_H
#define SAMPLINGDISTRIBUTIONVIEW_H


#include "View.h"
#include "RadianceView.h"

#include <pbrt/util/framebuffer.h>
#include <atomic>
#include <openpgl/cpp/OpenPGL.h>

#include "pbrt/util/progressreporter.h"


class SamplingDistributionView : public View {
public:
    SamplingDistributionView(pbrt::Application *parent, const openpgl::cpp::Field &field, const RadianceView &radianceView);

    ~SamplingDistributionView();

    void UpdateCPUBuffer(const pbrt::Point3f &pos, const pbrt::Normal3f &normal, bool lookahead);

    void UpdateFramebuffer();

    void Clear();

    void Draw() override;

    double GetCrossEntropy() const { return m_crossEntropy; }

private:
    void UpdateCPUBuffer();

    void ComputeCrossEntropy();

    const openpgl::cpp::Field &m_field;
    const RadianceView &m_radianceView;
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
        bool lookahead = false;
        bool localFrame = true;
    } m_prev;

    bool &m_localFrame;
    float &m_exposure;
    bool m_enableCosineProduct = true;

    float m_stepPhi;
    float m_stepTheta;
    double m_normalizer = 1;   // pdf normalizer, should be close to 1
    double m_crossEntropy = 0;  // integrated cross entropy between the sampling pdf and the rendered radiance distribution

    pbrt::Timer m_ceUpdateTimer;

    GLuint m_renderingTex = 0;  // stores the selected cpu buffer
    Framebuffer m_framebuffer;
};



#endif //SAMPLINGDISTRIBUTIONVIEW_H
