#include "shader.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <pbrt/util/print.h>

using pbrt::StringPrintf;

static constexpr GLuint invalid = 0xFFFFFFFF;

static bool checkShaderErrors(GLuint shader);
static bool checkProgramErrors(GLuint program);
static std::string readFile(std::filesystem::path filePath);

Shader::Shader(GLuint program)
    : m_program(program)
{
}

Shader::Shader()
    : m_program(invalid)
{
}

Shader::Shader(Shader&& other)
{
    m_program = other.m_program;
    other.m_program = invalid;
}

Shader::~Shader()
{
    if (m_program != invalid)
        glDeleteProgram(m_program);
}

Shader& Shader::operator=(Shader&& other)
{
    if (m_program != invalid)
        glDeleteProgram(m_program);

    m_program = other.m_program;
    other.m_program = invalid;
    return *this;
}

void Shader::bind() const
{
    assert(m_program != invalid);
    glUseProgram(m_program);
}

GLint Shader::getUniformLocation(const char *name) const {
    auto uniformLocation = glGetUniformLocation(m_program, name);
    if (uniformLocation != GL_INVALID_INDEX) {
        return uniformLocation;
    } else {
        std::cout << "WARNING : Could not bind uniform " << name << " invalid/unused name" << std::endl;
        return -1;
    }
}

GLint Shader::getAttributeLocation(const std::string &name) const
{
    auto loc = glGetAttribLocation(m_program, name.c_str());
    if (loc == invalid) {
        std::cerr << "WARNING : Could not find attribute " << name << std::endl;
    }
    return loc;
}

void Shader::setUniform1ui(const char *name, uint32_t value) const {
    glUniform1ui(getUniformLocation(name), value);
}

void Shader::setUniform1i(const char *name, int value) const {
    glUniform1i(getUniformLocation(name), value);
}

void Shader::setUniform1f(const char *name, float value) const {
    glUniform1f(getUniformLocation(name), value);
}

void Shader::setUniform2f(const char *name, float *value) const {
    glUniform2f(getUniformLocation(name), value[0], value[1]);
}

void Shader::setUniform3f(const char *name, float *value) const {
    glUniform3f(getUniformLocation(name), value[0], value[1], value[2]);
}

ShaderBuilder::~ShaderBuilder()
{
    freeShaders();
}

ShaderBuilder& ShaderBuilder::addStage(GLuint shaderStage, std::filesystem::path shaderFile)
{
    if (!std::filesystem::exists(shaderFile)) {
        throw ShaderLoadingException(StringPrintf("File %s does not exist", shaderFile.string().c_str()));
    }

    const std::string shaderSource = readFile(shaderFile);
    const GLuint shader = glCreateShader(shaderStage);
    const char* shaderSourcePtr = shaderSource.c_str();
    glShaderSource(shader, 1, &shaderSourcePtr, nullptr);
    glCompileShader(shader);
    if (!checkShaderErrors(shader)) {
        glDeleteShader(shader);
        throw ShaderLoadingException(StringPrintf("Failed to compile shader %s", shaderFile.string().c_str()));
    }

    m_shaders.push_back(shader);
    return *this;
}

Shader ShaderBuilder::build()
{
    // Combine vertex and fragment shaders into a single shader program.
    GLuint program = glCreateProgram();
    for (GLuint shader : m_shaders)
        glAttachShader(program, shader);
    glLinkProgram(program);
    freeShaders();

    if (!checkProgramErrors(program)) {
        throw ShaderLoadingException("Shader program failed to link");
    }

    return Shader(program);
}

void ShaderBuilder::freeShaders()
{
    for (GLuint shader : m_shaders)
        glDeleteShader(shader);
}

static std::string readFile(std::filesystem::path filePath)
{
    std::ifstream file(filePath, std::ios::binary);

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static bool checkShaderErrors(GLuint shader)
{
    // Check if the shader compiled successfully.
    GLint compileSuccessful;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileSuccessful);

    // If it didn't, then read and print the compile log.
    if (!compileSuccessful) {
        GLint logLength;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

        std::string logBuffer;
        logBuffer.resize(static_cast<size_t>(logLength));
        glGetShaderInfoLog(shader, logLength, nullptr, logBuffer.data());

        std::cerr << logBuffer << std::endl;
        return false;
    } else {
        return true;
    }
}

static bool checkProgramErrors(GLuint program)
{
    // Check if the program linked successfully
    GLint linkSuccessful;
    glGetProgramiv(program, GL_LINK_STATUS, &linkSuccessful);

    // If it didn't, then read and print the link log
    if (!linkSuccessful) {
        GLint logLength;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);

        std::string logBuffer;
        logBuffer.resize(static_cast<size_t>(logLength));
        glGetProgramInfoLog(program, logLength, nullptr, logBuffer.data());

        std::cerr << logBuffer << std::endl;
        return false;
    } else {
        return true;
    }
}
