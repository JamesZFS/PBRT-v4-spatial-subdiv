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

    void Update(const pbrt::Point3f &pos);

    void Clear();

    void Draw() override;

    void DrawColored();

    void DrawBars();

    void DrawPC();

    void DrawLR();

    void DrawComp();

    int lookaheadLevel() const { return m_lookaheadDepth; }

private:
    void Update();

    const openpgl::cpp::Field &m_field;
    RadianceView &m_radianceView;
    std::pair<PGLDirectionalSignature, PGLDirectionalSignature> m_cachedSignaturesLR;  // sized numSignaures
    PGLDirectionalSignature m_cachedSignatureParent;  // sized numSignatures
    PGLDirectionalSignature m_cachedSignatureChild;  // sized numSignatures
    uint8_t m_splitDim = 3;
    bool m_isRight;
    PGLDirectionalSignature &m_integratedSignature;
    PGLDirectionalSignature m_storedSignature{};
    std::atomic_bool m_shouldUpdate = false;

    float m_scale = 1.0f;
    Colormap m_cmap = CMap_Inferno;
    bool m_showIntegratedSignature = false;
    bool m_showStd = true;
    bool m_hasStoredSignature = false;
    int m_lookaheadDepth = 0;

    struct {
        bool valid = false;
        pbrt::Point3f pos;
    } m_prev;

    float m_barXs;
    float m_barRXs;
};

#endif //SIGNATUREVIEW_H
