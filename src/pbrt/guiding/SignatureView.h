//
// Created by fengshi on 11/21/24.
//

#ifndef SIGNATUREVIEW_H
#define SIGNATUREVIEW_H

#include "View.h"
#include "RadianceView.h"
#include <openpgl/cpp/OpenPGL.h>
#include <pbrt/util/framebuffer.h>


class SignatureView : public View {
public:
    SignatureView(pbrt::Application *parent, const openpgl::cpp::Field &field, RadianceView &radianceView);

    ~SignatureView();

    void Update(const pbrt::Point3f &pos);

    void Clear();

    void Draw() override;

    void DrawColored();

    void DrawBars();

    void DrawLR();

    void UpdateFramebuffer();

private:
    void Update();

    const openpgl::cpp::Field &m_field;
    RadianceView &m_radianceView;
    PGLDirectionalSignature m_cachedSignature{};
    std::pair<PGLDirectionalSignature, PGLDirectionalSignature> m_cachedSignaturesLR{};
    PGLDirectionalSignature &m_integratedSignature;
    pbrt::RGB m_selectionBuffer[PGL_SIGNATURE_SIZE];

    float m_scale = 1.0f;
    Colormap m_cmap = CMap_Inferno;
    bool m_showIntegratedSignature = false;
    bool m_showStd = true;
    bool m_showMultipliedStd = true;
    int m_splitDimension = 3;

    struct {
        bool valid = false;
        pbrt::Point3f pos;
    } m_prev;

    GLuint m_cachedSignatureTex = 0;  // stores the cache signature vector
    Framebuffer m_cachedSignatureFramebuffer;

    GLuint m_integratedSignatureTex = 0;  // stores the integrated signature vector
    Framebuffer m_integratedSignatureFramebuffer;

    GLuint m_selectionTex = 0;
    Framebuffer m_selectionFramebuffer;

    float m_barXs[PGL_SIGNATURE_SIZE];
    float m_barRXs[PGL_SIGNATURE_SIZE];
};

#endif //SIGNATUREVIEW_H
