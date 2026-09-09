//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glprog.hpp>
#include <pddi/gles/glmat.hpp>
#include <diagnostics/tvosdiagnostics.h>

#include <string>
#include <vector>
#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif

#ifdef RAD_TVOS
static void TvosLogUniformGlError( const char* tag )
{
    // Rate limit to avoid log drop.
    static unsigned int s_count = 0;
    s_count++;
    if ( !( s_count <= 25 || ( ( s_count % 500 ) == 0 ) ) )
    {
        (void)tag;
        return;
    }

    GLenum err = glGetError( );
    if ( err != GL_NO_ERROR )
    {
        GLint prog = 0;
        glGetIntegerv( GL_CURRENT_PROGRAM, &prog );
        SRR2::Diagnostics::RecordGlError( tag, (uint32_t)err );
        SRR2::Diagnostics::Errorf(
            SRR2::Diagnostics::RENDER,
            "[UNIFORM_GL_ERROR] tag=%s err=0x%x currentProgram=%d",
            tag ? tag : "",
            (unsigned)err,
            (int)prog );
    }
}
#endif

static inline void UniformColour(GLint loc, pddiColour c)
{
    if ( loc < 0 )
        return;
#if defined(RAD_MACOS)
    // Light/material setters can run on the radLoad worker. That thread has a
    // shared context for uploads but usually no program bound — skip uniforms.
    if ( !SDL_GL_GetCurrentContext() )
        return;
    GLint prog = 0;
    glGetIntegerv( GL_CURRENT_PROGRAM, &prog );
    if ( prog == 0 )
        return;
#endif
    glUniform4f(loc, float(c.Red()) / 255, float(c.Green()) / 255, float(c.Blue()) / 255, float(c.Alpha()) / 255);
}

pglProgram::pglProgram()
{
    program = 0;
    projection = modelview = normalmatrix = alpharef = sampler = lightmapSampler = acs = -1;
}

pglProgram::~pglProgram()
{
    if (program)
        glDeleteProgram(program);
}

void pglProgram::SetProjectionMatrix(const pddiMatrix* matrix)
{
    if (projection >= 0)
        glUniformMatrix4fv(projection, 1, GL_FALSE, matrix->m[0]);
#ifdef RAD_TVOS
    TvosLogUniformGlError( "SetProjectionMatrix" );
#endif
}

void pglProgram::SetModelViewMatrix(const pddiMatrix* matrix)
{
    if (modelview >= 0)
        glUniformMatrix4fv(modelview, 1, GL_FALSE, matrix->m[0]);
#ifdef RAD_TVOS
    TvosLogUniformGlError( "SetModelViewMatrix.modelview" );
#endif
    if (normalmatrix >= 0)
    {
        pddiMatrix inverse;
        inverse.Invert(*matrix);
        inverse.Transpose();
        glUniformMatrix4fv(normalmatrix, 1, GL_FALSE, inverse.m[0]);
#ifdef RAD_TVOS
        TvosLogUniformGlError( "SetModelViewMatrix.normalmatrix" );
#endif
    }
}

void pglProgram::SetTextureEnvironment(const pglTextureEnv* texEnv)
{
    if (sampler >= 0)
        glUniform1i(sampler, 0);
#ifdef RAD_TVOS
    TvosLogUniformGlError( "SetTextureEnvironment.sampler" );
#endif

    if (lightmapSampler >= 0)
        glUniform1i(lightmapSampler, 1);

    if (texEnv->lit)
    {
        UniformColour(acm, texEnv->ambient);
        UniformColour(ecm, texEnv->emissive);
        UniformColour(dcm, texEnv->diffuse);
        UniformColour(scm, texEnv->specular);
        glUniform1f(srm, texEnv->shininess);
#ifdef RAD_TVOS
        TvosLogUniformGlError( "SetTextureEnvironment.lit" );
#endif
    }
    else
    {
        UniformColour(acm, pddiColour(-1));
        UniformColour(ecm, pddiColour(-1));
        UniformColour(dcm, pddiColour(-1));
        UniformColour(scm, pddiColour(-1));
        glUniform1f(srm, 0.0f);
    }

    if (texEnv->alphaTest)
    {
        PDDIASSERT(texEnv->alphaCompareMode == PDDI_COMPARE_GREATER ||
            texEnv->alphaCompareMode == PDDI_COMPARE_GREATEREQUAL);
    }

    if (alpharef >= 0)
        glUniform1f(alpharef, texEnv->alphaTest ? texEnv->alphaRef : 0.0f);
#ifdef RAD_TVOS
    TvosLogUniformGlError( "SetTextureEnvironment.alpharef" );
#endif
}

void pglProgram::SetLightState(int handle, const pddiLight* lightState)
{
    if( handle >= PDDI_MAX_LIGHTS )
        return;

#if defined(RAD_MACOS)
    if ( !SDL_GL_GetCurrentContext() )
        return;
    {
        GLint prog = 0;
        glGetIntegerv( GL_CURRENT_PROGRAM, &prog );
        if ( prog == 0 )
            return;
    }
#endif

    float dir[4];
    switch(lightState->type)
    {
        case PDDI_LIGHT_DIRECTIONAL :
            dir[0] = -lightState->worldDirection.x;
            dir[1] = -lightState->worldDirection.y;
            dir[2] = -lightState->worldDirection.z;
            dir[3] = 0.0f;
            break;

        case PDDI_LIGHT_POINT :
            dir[0] = lightState->worldPosition.x;
            dir[1] = lightState->worldPosition.y;
            dir[2] = lightState->worldPosition.z;
            dir[3] = 1.0f;
            break;

        case PDDI_LIGHT_SPOT :
            PDDIASSERT(0);
            break;
    }

    glUniform1i(lights[handle].enabled, lightState->enabled ? 1 : 0);
    glUniform4fv(lights[handle].position, 1, dir);
    UniformColour(lights[handle].colour, lightState->colour);
    glUniform3f(lights[handle].attenuation, lightState->attenA, lightState->attenB, lightState->attenC);
#ifdef RAD_TVOS
    TvosLogUniformGlError( "SetLightState" );
#endif
}

void pglProgram::SetAmbientLight(pddiColour ambient)
{
    UniformColour(acs, ambient);
#ifdef RAD_TVOS
    TvosLogUniformGlError( "SetAmbientLight" );
#endif
}

bool pglProgram::LinkProgram(GLuint vertexShader, GLuint fragmentShader)
{
    program = glCreateProgram();

    if(vertexShader)
        glAttachShader(program, vertexShader);
    if(fragmentShader)
        glAttachShader(program, fragmentShader);

    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "normal");
    glBindAttribLocation(program, 2, "texcoord");
    glBindAttribLocation(program, 3, "color");
    glBindAttribLocation(program, 4, "texcoord1");  // UV1 for lightmaps

    glLinkProgram(program);

    GLint isLinked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, (int *)&isLinked);
    if (isLinked == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

        // The maxLength includes the NULL character
        std::vector<GLchar> infoLog(maxLength);
        glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);

        SRR2::Diagnostics::Errorf(
            SRR2::Diagnostics::RENDER,
            "[PROGRAM_LINK_FAILED] log=%s",
            infoLog.data() );
        return false;
    }
    
    projection = glGetUniformLocation(program, "projection");
    modelview = glGetUniformLocation(program, "modelview");
    normalmatrix = glGetUniformLocation(program, "normalmatrix");
    alpharef = glGetUniformLocation(program, "alpharef");
    sampler = glGetUniformLocation(program, "tex");
    lightmapSampler = glGetUniformLocation(program, "lightmapTex");

    for (int i = 0; i < PDDI_MAX_LIGHTS; i++)
    {
        std::string prefix = std::string("lights[") + char('0' + i) + "].";
        lights[i].enabled = glGetUniformLocation(program, (prefix + "enabled").c_str());
        lights[i].position = glGetUniformLocation(program, (prefix + "position").c_str());
        lights[i].colour = glGetUniformLocation(program, (prefix + "colour").c_str());
        lights[i].attenuation = glGetUniformLocation( program, (prefix + "attenuation").c_str() );
    }

    acs = glGetUniformLocation(program, "acs");
    acm = glGetUniformLocation(program, "acm");
    dcm = glGetUniformLocation(program, "dcm");
    scm = glGetUniformLocation(program, "scm");
    ecm = glGetUniformLocation(program, "ecm");
    srm = glGetUniformLocation(program, "srm");

#ifndef RAD_VITAGL
    // Always detach shaders after a successful link
    if(vertexShader)
        glDetachShader(program, vertexShader);
    if(fragmentShader)
        glDetachShader(program, fragmentShader);
#endif
    return true;
}

bool pglProgram::CompileShader(GLuint shader, const char* source)
{
#if defined(RAD_MACOS)
    // Convert GLES100 shader text to desktop GLSL 1.20 (strip precision; add #version).
    std::string macSrc;
    macSrc.reserve( strlen( source ) + 64 );
    macSrc += "#version 120\n";
    const char* p = source;
    while ( *p )
    {
        const char* lineStart = p;
        while ( *p && *p != '\n' ) ++p;
        size_t len = (size_t)( p - lineStart );
        std::string line( lineStart, len );
        // Drop GLES precision qualifiers (invalid in desktop GLSL 120).
        if ( line.find( "precision " ) == std::string::npos )
        {
            macSrc.append( lineStart, len );
            if ( *p == '\n' ) macSrc.push_back( '\n' );
        }
        else if ( *p == '\n' )
        {
            // skip precision line entirely
        }
        if ( *p == '\n' ) ++p;
    }
    const char* srcPtr = macSrc.c_str();
    glShaderSource( shader, 1, &srcPtr, 0 );
#else
    glShaderSource(shader, 1, &source, 0);
#endif
    glCompileShader(shader);

    GLint isCompiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if(isCompiled == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

        // The maxLength includes the NULL character
        std::vector<GLchar> infoLog(maxLength);
        glGetShaderInfoLog(shader, maxLength, &maxLength, &infoLog[0]);

        // We don't need the shader anymore.
        glDeleteShader(shader);

        SRR2::Diagnostics::Errorf(
            SRR2::Diagnostics::RENDER,
            "[SHADER_COMPILE_FAILED] log=%s",
            infoLog.data() );
        return false;
    }
    return true;
}

pglProgram* pglProgram::CreateProgram(GLuint vertexShader, GLuint fragmentShader)
{
    pglProgram* program = new pglProgram();
    program->AddRef();
    if(!program->LinkProgram(vertexShader, fragmentShader))
    {
        program->Release();
        return nullptr;
    }
    return program;
}
