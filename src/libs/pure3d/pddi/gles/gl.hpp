//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


// stub OpenGL header, all pddi gl code uses this instead of '#include <GL/gl.h>
#ifdef RAD_VITAGL
#include <vitaGL.h>
#define GL_BGRA_EXT GL_BGRA
#define glGenVertexArraysOES glGenVertexArrays
#define glDeleteVertexArraysOES glDeleteVertexArrays
#define glBindVertexArrayOES glBindVertexArray
#elif defined(RAD_MACOS)
// macOS: desktop OpenGL 2.1 compatibility (GLES2 API subset + shims).
// Apple removed OpenGLES.framework from macOS; keep the GLES renderer via GL shims.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT GL_BGRA
#endif

#ifndef GL_DEPTH_COMPONENT24_OES
#ifdef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24_OES GL_DEPTH_COMPONENT24
#else
#define GL_DEPTH_COMPONENT24_OES 0x81A6
#endif
#endif

// GLES float depth entry points -> desktop doubles
#ifndef glClearDepthf
#define glClearDepthf(x) glClearDepth((double)(x))
#endif
#ifndef glDepthRangef
#define glDepthRangef(n, f) glDepthRange((double)(n), (double)(f))
#endif

// Map OES VAO entry points to Apple VAO extension (OpenGL 2.1 on macOS).
#define glGenVertexArraysOES glGenVertexArraysAPPLE
#define glBindVertexArrayOES glBindVertexArrayAPPLE
#define glDeleteVertexArraysOES glDeleteVertexArraysAPPLE
#define glIsVertexArrayOES glIsVertexArrayAPPLE

#ifndef GL_OES_vertex_array_object
#define GL_OES_vertex_array_object 1
#endif

#ifndef GL_VERTEX_ARRAY_BINDING_OES
#ifdef GL_VERTEX_ARRAY_BINDING_APPLE
#define GL_VERTEX_ARRAY_BINDING_OES GL_VERTEX_ARRAY_BINDING_APPLE
#else
#define GL_VERTEX_ARRAY_BINDING_OES 0x85B5
#endif
#endif

#elif defined(RAD_TVOS)
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#else
#include <glad/glad.h>
#endif
