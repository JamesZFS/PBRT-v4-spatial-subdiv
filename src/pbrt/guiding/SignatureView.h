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

    void Rescale();

    void Clear();

    void Draw() override;

    void DrawColored();

    void DrawBars();

    void DrawPC();

    void DrawLR();

    void DrawComp();

    void UpdateFramebuffer();

    int lookaheadLevel() const { return m_lookaheadDepth; }

private:
    void Update();

    void BinInteraction(int modelIndex);

    const openpgl::cpp::Field &m_field;
    RadianceView &m_radianceView;
    std::vector<std::pair<PGLDirectionalSignature, PGLDirectionalSignature>> m_cachedSignaturesLR{1};  // sized numSignaures
    std::vector<PGLDirectionalSignature> m_cachedSignatureParent{1};  // sized numSignatures
    std::vector<PGLDirectionalSignature> m_cachedSignatureChild{1};  // sized numSignatures
    uint8_t m_splitDim = 3;
    bool m_isRight;
    PGLDirectionalSignature &m_integratedSignature;
    PGLDirectionalSignature m_storedSignature{};
    pbrt::RGB m_selectionBuffer[PGL_SIGNATURE_MAX_SIZE];
    std::atomic_bool m_shouldUpdate = false;

    float m_scale = 1.0f;
    Colormap m_cmap = CMap_Inferno;
    bool m_showIntegratedSignature = false;
    bool m_showStd = true;
    bool m_showMultipliedStd = false;
    bool m_showTValue = false;
    bool m_hasStoredSignature = false;
    int m_lookaheadDepth = 0;
    int m_numBins = 0;  // Application selected signature's numBins

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

    float m_barXs[PGL_SIGNATURE_MAX_SIZE];
    float m_barRXs[PGL_SIGNATURE_MAX_SIZE];
};

#endif //SIGNATUREVIEW_H
