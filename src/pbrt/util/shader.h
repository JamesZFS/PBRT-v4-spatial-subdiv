#pragma once
#include "opengl_includes.h"
#include <exception>
#include <filesystem>
#include <vector>

struct ShaderLoadingException : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Shader {
public:
    Shader();
    Shader(const Shader&) = delete;
    Shader(Shader&&);
    ~Shader();

    Shader& operator=(Shader&&);

    // ... Feel free to add more methods here (e.g. for setting uniforms or keeping track of texture units) ...
    void bind() const;

    // Query a uniform location by its name in the shader
    GLint getUniformLocation(const char *name) const;

    // Query an attribute location by its name in the shader
    GLint getAttributeLocation(const std::string &name) const;

    void setUniform1i(const char *name, int value) const;
    void setUniform1f(const char *name, float value) const;
    void setUniform2f(const char *name, float* value) const;
    void setUniform3f(const char *name, float* value) const;

private:
    friend class ShaderBuilder;
    Shader(GLuint program);

private:
    GLuint m_program;
};

class ShaderBuilder {
public:
    ShaderBuilder() = default;
    ShaderBuilder(const ShaderBuilder&) = delete;
    ShaderBuilder(ShaderBuilder&&) = default;
    ~ShaderBuilder();

    ShaderBuilder& addStage(GLuint shaderStage, std::filesystem::path shaderFile);
    Shader build();

private:
    void freeShaders();

private:
    std::vector<GLuint> m_shaders;
};
