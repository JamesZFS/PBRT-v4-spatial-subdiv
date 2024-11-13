#pragma once
#include "shader.h"

struct FramebufferException : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Framebuffer {
public:
    Framebuffer(int width, int height, const std::string &fragmentShaderPath);

    ~Framebuffer();

    void rescale(int width, int height);

    void bind();

    void unbind();

    void draw();

    void clear();

    Shader &getShader() { return m_shader; }

    GLuint getTexture() { return m_tex; }

private:
    GLuint m_fbo = 0;
    GLuint m_tex = 0;

    int m_width = 0;
    int m_height = 0;

    Shader m_shader;
    GLuint m_vbo = 0, m_ibo = 0, m_vao = 0;
};
