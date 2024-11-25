//
// Created by fengshi on 11/21/24.
//

#ifndef EMBEDDINGVIEW_H
#define EMBEDDINGVIEW_H



#include "View.h"
#include "RadianceView.h"
#include <openpgl/cpp/OpenPGL.h>
#include <pbrt/util/framebuffer.h>


class EmbeddingView : public View {
public:
    EmbeddingView(pbrt::Application *parent, const openpgl::cpp::Field &field, RadianceView &radianceView);

    ~EmbeddingView();

    void Update(const pbrt::Point3f &pos, bool lookahead);

    void Clear();

    void Draw() override;

    void UpdateFramebuffer();

private:
    const openpgl::cpp::Field &m_field;
    RadianceView &m_radianceView;
    PGLDirectionalEmbedding m_cachedEmbedding{};
    PGLDirectionalEmbedding &m_integratedEmbedding;
    pbrt::RGB m_selectionBuffer[PGL_EMBEDDING_SIZE];

    float m_scale = 1.0f;
    Colormap m_cmap = CMap_Inferno;
    bool m_showIntegratedEmbedding = false;

    GLuint m_cachedEmbeddingTex = 0;  // stores the cache embedding vector
    Framebuffer m_cachedEmbeddingFramebuffer;

    GLuint m_integratedEmbeddingTex = 0;  // stores the integrated embedding vector
    Framebuffer m_integratedEmbeddingFramebuffer;

    GLuint m_selectionTex = 0;
    Framebuffer m_selectionFramebuffer;
};



#endif //EMBEDDINGVIEW_H
