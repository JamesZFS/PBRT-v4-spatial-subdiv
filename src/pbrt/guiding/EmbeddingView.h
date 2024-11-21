//
// Created by fengshi on 11/21/24.
//

#ifndef EMBEDDINGVIEW_H
#define EMBEDDINGVIEW_H



#include "View.h"
#include <openpgl/regionstatistics.h>
#include <pbrt/util/framebuffer.h>


class EmbeddingView : public View {
public:
    EmbeddingView(pbrt::Application *parent);

    ~EmbeddingView();

    void Set(const PGLDirectionalEmbedding &embedding) { m_embedding = embedding; }

    PGLDirectionalEmbedding Get() const { return m_embedding; }

    void Reset() { m_embedding = {}; }

    void Draw() override;

    void UpdateFramebuffer();

    bool IsActive() const { return m_isActive; }

private:
    // TODO: store the field here so that we can update the embedding per iteration automatically
    PGLDirectionalEmbedding m_embedding{};

    bool m_isActive = false;

    float m_scale = 1.0f;
    Colormap m_cmap = CMap_Inferno;

    GLuint m_renderingTex = 0;  // stores the selected cpu buffer
    Framebuffer m_framebuffer;
};



#endif //EMBEDDINGVIEW_H
