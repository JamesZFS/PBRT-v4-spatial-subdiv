#pragma once
#include <glad/glad.h>

#ifdef WIN32
#define NOMINMAX
#include <windows.h> // Must include <windows.h> before <GL.glu.h>
#endif

#ifdef __APPLE__
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif