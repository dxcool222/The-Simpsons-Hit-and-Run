//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glcon.hpp>
#include <pddi/gles/gldev.hpp>
#include <pddi/gles/gldisplay.hpp>
#include <pddi/gles/gltex.hpp>
#include <pddi/gles/glmat.hpp>
#include <pddi/gles/glprog.hpp>

#include <pddi/base/debug.hpp>
#include <math.h>
#include <string.h>
#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif
#include <vector>
#include <diagnostics/tvosdiagnostics.h>

#include <microprofile.h>

// vertex arrays rendering
GLenum primTypeTable[5] =
{
    GL_TRIANGLES, //PDDI_PRIM_TRIANGLES
    GL_TRIANGLE_STRIP, //PDDI_PRIM_TRISTRIP
    GL_LINES, //PDDI_PRIM_LINES
    GL_LINE_STRIP, // PDDI_PRIM_LINESTRIP
    GL_POINTS, //PDDI_PRIM_POINTS
};

#ifdef RAD_TVOS
static unsigned int s_tvosFrameCount = 0;
static unsigned int s_tvosDrawCallCount = 0;
static unsigned int s_tvosVertexCount = 0;


static bool TvosShouldTraceFrame( unsigned int frame )
{
#if defined( RAD_TVOS_RENDER_DIAGNOSTICS )
    return ( frame <= 5 ) || ( ( frame % 300 ) == 0 );
#else
    (void) frame;
    return false;
#endif
}

static void TvosClearGlErrors( void )
{
#if defined( RAD_TVOS_RENDER_DIAGNOSTICS )
    while ( glGetError( ) != GL_NO_ERROR )
    {
    }
#endif
}

static bool TvosConsumeAndLogGlError( const char* tag, unsigned int frame )
{
#if defined( RAD_TVOS_RENDER_DIAGNOSTICS )
    GLenum err = glGetError( );
    if ( err != GL_NO_ERROR )
    {
        SRR2::Diagnostics::RecordGlError( tag, (uint32_t)err );
        SRR2::Diagnostics::Tracef(
            SRR2::Diagnostics::RENDER,
            "TVOS_GL_ERR tag=%s frame=%u err=0x%x",
            tag ? tag : "",
            frame,
            (unsigned)err );
        return true;
    }
    return false;
#else
    (void) tag;
    (void) frame;
    return false;
#endif
}
#endif

static inline void FillGLColour(pddiColour c, float* f)
{
    f[0] = float(c.Red()) / 255;
    f[1] = float(c.Green()) / 255;
    f[2] = float(c.Blue()) / 255;
    f[3] = float(c.Alpha()) / 255;
}

// extensions
class pglExtContext : public pddiExtGLContext 
{
public:
    pglExtContext(pglDisplay* d) : display(d) {}

    void BeginContext()
    {
        display->BeginContext();
    }

    void EndContext()
    {
        display->EndContext();
    }

private:
    pglDisplay* display;
};

class pglExtGamma : public pddiExtGammaControl
{
public:
    pglExtGamma(pglDisplay* d) { display = d;}

    void SetGamma(float r, float g, float b)     {display->SetGamma(r,g,b);}
    void GetGamma(float *r, float *g, float *b)  {display->GetGamma(r,g,b);}

protected:
    pglDisplay* display;
};

pglContext::pglContext(pglDevice* dev, pglDisplay* disp) : pddiBaseContext((pddiDisplay*)disp,(pddiDevice*)dev)
{
    device = dev;
    display = disp;
    currentProgram = nullptr;

    device->AddRef();
    display->AddRef();
    disp->SetContext(this);

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    DefaultState();
    contextID = 0;

    extContext = new pglExtContext(display);
    extGamma = new pglExtGamma(display);

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    pglProgram::CompileShader(vertexShader,
        "precision highp float;\n"

        "attribute vec3 position;\n"
        "attribute vec3 normal;\n"
        "attribute vec2 texcoord;\n"
        "attribute vec4 color;\n"

        "uniform mat4 projection;\n"
        "uniform mat4 modelview;\n"

        "varying vec2 tc;\n"
        "varying vec4 cpri;\n"
        "varying vec4 csec;\n"

        "void main() {\n"
        "    vec4 V = modelview * vec4(position, 1.0);\n"
        "    tc = texcoord;\n"
        "    cpri = color;\n"
        "    csec = vec4(0.0, 0.0, 0.0, 0.0);\n"
        "    gl_Position = projection * V;\n"
        "}\n"
    );

    GLuint litShader = glCreateShader(GL_VERTEX_SHADER);
    pglProgram::CompileShader(litShader,
        "precision highp float;\n"

        "attribute vec3 position;\n"
        "attribute vec3 normal;\n"
        "attribute vec2 texcoord;\n"
        "attribute vec4 color;\n"

        "uniform mat4 projection;\n"
        "uniform mat4 modelview;\n"
        "uniform mat4 normalmatrix;\n"

        // Lights
        "uniform struct LightParams {\n"
        "    int enabled;\n"
        "    vec4 position;\n"
        "    vec4 colour;\n"
        "    vec3 attenuation;\n"
        "} lights[" PDDI_STRINGIZE(PDDI_MAX_LIGHTS) "];\n"

        // Scene
        "uniform vec4 acs;\n"

        // Material
        "uniform vec4 acm;\n"
        "uniform vec4 dcm;\n"
        "uniform vec4 scm;\n"
        "uniform vec4 ecm;\n"
        "uniform float srm;\n"

        "varying vec2 tc;\n"
        "varying vec4 cpri;\n"
        "varying vec4 csec;\n"

        "vec3 direction(vec4 p1, vec4 p2) { return normalize(p2.xyz * sign(p1.w) - p1.xyz * sign(p2.w)); }\n"
        "float power(float x, float y) { return y != 0.0 ? pow(x,y) : 1.0; }\n"
        "float product(vec3 x, vec3 y) { return max(dot(x,y), 0.0); }\n"

        "void main() {\n"
        "    vec4 V = modelview * vec4(position, 1.0);\n"
        "    vec3 n = normalize(mat3(normalmatrix) * normal);\n"

        "    vec3 diff = ecm.rgb + acm.rgb * acs.rgb;\n"
        "    vec3 spec = vec3(0.0);\n"
        "    for (int i = 0; i < " PDDI_STRINGIZE(PDDI_MAX_LIGHTS) "; i++) {\n"
        "        if (lights[i].enabled == 0) continue;\n"

        "        vec3 VP = direction(V, lights[i].position);\n"
        "        float f = product(n,VP) != 0.0 ? 1.0 : 0.0;\n"
        "        vec3 h = normalize(VP + vec3(0.0, 0.0, 1.0));\n"

        "        vec3 k = lights[i].attenuation;\n"
        "        float d = distance(V.xyz, lights[i].position.xyz);\n"
        "        float att = lights[i].position.w != 0.0 ? 1.0 / (k[0] + k[1] * d + k[2] * d * d) : 1.0;\n"

        "        diff += att * product(n,VP) * dcm.rgb * lights[i].colour.rgb;\n"
        "        spec += att * f * power(product(n,h),srm) * scm.rgb * lights[i].colour.rgb;\n"
        "    }\n"

        "    tc = texcoord;\n"
        "    cpri = color * vec4(diff, dcm.a);\n"
        "    csec = vec4(spec, 0.0);\n"
        "    gl_Position = projection * V;\n"
        "}\n"
    );

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    pglProgram::CompileShader(fragmentShader,
        "precision mediump float;\n"
        "varying vec2 tc;\n"
        "varying vec4 cpri;\n"
        "varying vec4 csec;\n"

        "void main() {\n"
        "    gl_FragColor = cpri + csec;\n"
        "}\n"
    );

    GLuint textureShader = glCreateShader(GL_FRAGMENT_SHADER);
    pglProgram::CompileShader(textureShader,
        "precision mediump float;\n"
        "varying vec2 tc;\n"
        "varying vec4 cpri;\n"
        "varying vec4 csec;\n"

        "uniform sampler2D tex;\n"

        "void main() {\n"
        "    gl_FragColor = texture2D(tex, tc) * cpri + csec;\n"
        "}\n"
    );

    GLuint alphaTestShader = glCreateShader(GL_FRAGMENT_SHADER);
    pglProgram::CompileShader(alphaTestShader,
        "precision mediump float;\n"
        "varying vec2 tc;\n"
        "varying vec4 cpri;\n"
        "varying vec4 csec;\n"

        "uniform float alpharef;\n"
        "uniform sampler2D tex;\n"

        "void main() {\n"
        "    vec4 c = texture2D(tex, tc) * cpri + csec;\n"
        "    if (c.a < alpharef) discard;\n"
        "    gl_FragColor = c;\n"
        "}\n"
    );

    // Lightmap vertex shader - passes both UV0 and UV1
    GLuint lightmapVertexShader = glCreateShader(GL_VERTEX_SHADER);
    pglProgram::CompileShader(lightmapVertexShader,
        "precision highp float;\n"

        "attribute vec3 position;\n"
        "attribute vec3 normal;\n"
        "attribute vec2 texcoord;\n"   // UV0 - base texture
        "attribute vec4 color;\n"
        "attribute vec2 texcoord1;\n"  // UV1 - lightmap (attribute 4)

        "uniform mat4 projection;\n"
        "uniform mat4 modelview;\n"

        "varying vec2 tc;\n"
        "varying vec2 tc1;\n"          // Lightmap UVs
        "varying vec4 cpri;\n"
        "varying vec4 csec;\n"

        "void main() {\n"
        "    vec4 V = modelview * vec4(position, 1.0);\n"
        "    tc = texcoord;\n"
        "    tc1 = texcoord1;\n"
        "    cpri = color;\n"
        "    csec = vec4(0.0, 0.0, 0.0, 0.0);\n"
        "    gl_Position = projection * V;\n"
        "}\n"
    );

    // Lightmap fragment shader - base texture * lightmap
    GLuint lightmapFragShader = glCreateShader(GL_FRAGMENT_SHADER);
    pglProgram::CompileShader(lightmapFragShader,
        "precision mediump float;\n"
        "varying vec2 tc;\n"
        "varying vec2 tc1;\n"
        "varying vec4 cpri;\n"
        "varying vec4 csec;\n"

        "uniform sampler2D tex;\n"
        "uniform sampler2D lightmapTex;\n"

        "void main() {\n"
        "    vec4 baseColor = texture2D(tex, tc);\n"
        "    vec4 lightmapColor = texture2D(lightmapTex, tc1);\n"
        "    // Multiply base texture by lightmap (standard lightmap blending)\n"
        "    gl_FragColor = baseColor * lightmapColor * cpri * 2.0 + csec;\n"
        "}\n"
    );

    // Lit lightmap vertex shader
    GLuint litLightmapVertexShader = glCreateShader(GL_VERTEX_SHADER);
    pglProgram::CompileShader(litLightmapVertexShader,
        "precision highp float;\n"

        "attribute vec3 position;\n"
        "attribute vec3 normal;\n"
        "attribute vec2 texcoord;\n"
        "attribute vec4 color;\n"
        "attribute vec2 texcoord1;\n"

        "uniform mat4 projection;\n"
        "uniform mat4 modelview;\n"
        "uniform mat4 normalmatrix;\n"

        "uniform struct LightParams {\n"
        "    int enabled;\n"
        "    vec4 position;\n"
        "    vec4 colour;\n"
        "    vec3 attenuation;\n"
        "} lights[" PDDI_STRINGIZE(PDDI_MAX_LIGHTS) "];\n"

        "uniform vec4 acs;\n"
        "uniform vec4 acm;\n"
        "uniform vec4 dcm;\n"
        "uniform vec4 scm;\n"
        "uniform vec4 ecm;\n"
        "uniform float srm;\n"

        "varying vec2 tc;\n"
        "varying vec2 tc1;\n"
        "varying vec4 cpri;\n"
        "varying vec4 csec;\n"

        "vec3 direction(vec4 p1, vec4 p2) { return normalize(p2.xyz * sign(p1.w) - p1.xyz * sign(p2.w)); }\n"
        "float power(float x, float y) { return y != 0.0 ? pow(x,y) : 1.0; }\n"
        "float product(vec3 x, vec3 y) { return max(dot(x,y), 0.0); }\n"

        "void main() {\n"
        "    vec4 V = modelview * vec4(position, 1.0);\n"
        "    vec3 n = normalize(mat3(normalmatrix) * normal);\n"

        "    vec3 diff = ecm.rgb + acm.rgb * acs.rgb;\n"
        "    vec3 spec = vec3(0.0);\n"
        "    for (int i = 0; i < " PDDI_STRINGIZE(PDDI_MAX_LIGHTS) "; i++) {\n"
        "        if (lights[i].enabled == 0) continue;\n"
        "        vec3 VP = direction(V, lights[i].position);\n"
        "        float f = product(n,VP) != 0.0 ? 1.0 : 0.0;\n"
        "        vec3 h = normalize(VP + vec3(0.0, 0.0, 1.0));\n"
        "        vec3 k = lights[i].attenuation;\n"
        "        float d = distance(V.xyz, lights[i].position.xyz);\n"
        "        float att = lights[i].position.w != 0.0 ? 1.0 / (k[0] + k[1] * d + k[2] * d * d) : 1.0;\n"
        "        diff += att * product(n,VP) * dcm.rgb * lights[i].colour.rgb;\n"
        "        spec += att * f * power(product(n,h),srm) * scm.rgb * lights[i].colour.rgb;\n"
        "    }\n"

        "    tc = texcoord;\n"
        "    tc1 = texcoord1;\n"
        "    cpri = color * vec4(diff, dcm.a);\n"
        "    csec = vec4(spec, 0.0);\n"
        "    gl_Position = projection * V;\n"
        "}\n"
    );

    colorProgram[0] = pglProgram::CreateProgram(vertexShader, fragmentShader);
    colorProgram[1] = pglProgram::CreateProgram(litShader, fragmentShader);

    textureProgram[0] = pglProgram::CreateProgram(vertexShader, textureShader);
    textureProgram[1] = pglProgram::CreateProgram(litShader, textureShader);

    alphaTestProgram[0] = pglProgram::CreateProgram(vertexShader, alphaTestShader);
    alphaTestProgram[1] = pglProgram::CreateProgram(litShader, alphaTestShader);

    // Lightmap programs (base * lightmap multiply)
    lightmapProgram[0] = pglProgram::CreateProgram(lightmapVertexShader, lightmapFragShader);
    lightmapProgram[1] = pglProgram::CreateProgram(litLightmapVertexShader, lightmapFragShader);

#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
    SRR2::Diagnostics::Checkpointf(SRR2::Diagnostics::RENDER,
            "[GLES_INIT] Shader programs created: color=%p/%p texture=%p/%p alphaTest=%p/%p lightmap=%p/%p",
            (void*)colorProgram[0], (void*)colorProgram[1],
            (void*)textureProgram[0], (void*)textureProgram[1],
            (void*)alphaTestProgram[0], (void*)alphaTestProgram[1],
            (void*)lightmapProgram[0], (void*)lightmapProgram[1]);
#endif

    // Don't leak shaders
    glDeleteShader(vertexShader);
    glDeleteShader(litShader);
    glDeleteShader(fragmentShader);
    glDeleteShader(textureShader);
    glDeleteShader(lightmapVertexShader);
    glDeleteShader(lightmapFragShader);
    glDeleteShader(litLightmapVertexShader);
    glDeleteShader(alphaTestShader);
    
    defaultShader = new pglMat(this);
    defaultShader->AddRef();
    SetShaderProgram(colorProgram[0]);
}

pglContext::~pglContext()
{
    defaultShader->Release();
    currentProgram->Release();
    for(int i = 0; i < 2; i++)
    {
        colorProgram[i]->Release();
        textureProgram[i]->Release();
        alphaTestProgram[i]->Release();
        if(lightmapProgram[i])
            lightmapProgram[i]->Release();
    }

    delete extContext;
    delete extGamma;

    display->SetContext(NULL);
    display->Release();
    device->Release();
}

// frame synchronisation
void pglContext::BeginFrame()
{
    pddiBaseContext::BeginFrame();

#ifdef RAD_TVOS
    s_tvosFrameCount++;
    
    
    if ( TvosShouldTraceFrame( s_tvosFrameCount ) )
    {
        GLint fb = 0;
        GLint vp[4] = { 0, 0, 0, 0 };
        glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fb );
        glGetIntegerv( GL_VIEWPORT, vp );
        SRR2::Diagnostics::Tracef(
            SRR2::Diagnostics::RENDER,
            "TVOS_GL FrameBegin #%u fb=%d viewport=%d,%d %dx%d",
            s_tvosFrameCount, (int)fb, (int)vp[0], (int)vp[1], (int)vp[2], (int)vp[3] );

        GLenum err = glGetError( );
        if ( err != GL_NO_ERROR )
        {
            SRR2::Diagnostics::RecordGlError( "BeginFrame", (uint32_t)err );
        }
    }
#endif

#ifdef RAD_TVOS
    static bool s_tvosSwapIntervalSet = false;
    if ( !s_tvosSwapIntervalSet )
    {
        int swapIntervalResult = SDL_GL_SetSwapInterval(display->GetForceVSync() ? 1 : 0);
#if defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        if ( swapIntervalResult != 0 )
        {
            SRR2::Diagnostics::Anomalyf(
                SRR2::Diagnostics::RENDER,
                "[SWAP_INTERVAL_UNSUPPORTED] error=%s",
                SDL_GetError() );
        }
#else
        (void) swapIntervalResult;
#endif
        SDL_ClearError();
        s_tvosSwapIntervalSet = true;
    }
#else
    SDL_GL_SetSwapInterval(display->GetForceVSync() ? 1 : 0);
#endif

    if(display->HasReset())
    {
        contextID++;

        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
#ifndef RAD_VITAGL
        glEnable(GL_DITHER);
#endif

        SyncState(0xffffffff);
    }

    // tvOS/iOS fix: Force clean depth state at frame start to prevent
    // state leakage from previous frame causing roads/terrain to not render
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    
    // Reset polygon offset state
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0.0f, 0.0f);
    
    // Unbind any stale VAO
    glBindVertexArrayOES(0);

    projection.Identity();
}

void pglContext::EndFrame()
{
    pddiBaseContext::EndFrame();

#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
    static unsigned int s_endCount = 0;
    s_endCount++;
    if ( ( s_endCount <= 10 ) || ( ( s_endCount % 300 ) == 0 ) )
    {
        SRR2::Diagnostics::Tracef(
            SRR2::Diagnostics::RENDER,
            "TVOS_GL EndFrame #%u drawCalls=%u verts=%u",
            s_endCount, s_tvosDrawCallCount, s_tvosVertexCount );
        GLenum err = glGetError( );
        if ( err != GL_NO_ERROR )
        {
            SRR2::Diagnostics::RecordGlError( "EndFrame", (uint32_t)err );
        }
    }
    SRR2::Diagnostics::RecordRenderFrame( s_tvosDrawCallCount, s_tvosVertexCount );
    s_tvosDrawCallCount = 0;
    s_tvosVertexCount = 0;
#endif
}

// buffer clearing
void pglContext::Clear(unsigned bufferMask)
{
    pddiBaseContext::Clear(bufferMask);

    int myClearMask = 0;
    myClearMask |= (bufferMask & PDDI_BUFFER_COLOUR) ? GL_COLOR_BUFFER_BIT : 0;
    myClearMask |= (bufferMask & PDDI_BUFFER_DEPTH) ? GL_DEPTH_BUFFER_BIT : 0;
    //myClearMask |= (bufferMask & PDDI_BUFFER_STENCIL) ? GL_STENCIL_BUFFER_BIT : 0;

#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
    static unsigned int s_clearCount = 0;
    s_clearCount++;
    if ( s_clearCount <= 10 || ( s_clearCount % 300 ) == 0 )
    {
        GLint fb = 0;
        glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fb );
        SRR2::Diagnostics::Tracef(
                 SRR2::Diagnostics::RENDER,
                 "TVOS_GL Clear #%u mask=0x%x fb=%d clearColor=(%d,%d,%d,%d)",
                 s_clearCount,
                 (unsigned)myClearMask,
                 (int)fb,
                 (int)state.viewState->clearColour.Red(),
                 (int)state.viewState->clearColour.Green(),
                 (int)state.viewState->clearColour.Blue(),
                 (int)state.viewState->clearColour.Alpha() );
    }
#endif

    glClearDepthf(state.viewState->clearDepth);
    glClearColor(float(state.viewState->clearColour.Red())/255.0f, 
                     float(state.viewState->clearColour.Green())/255.0f, 
                     float(state.viewState->clearColour.Blue())/255.0f,
                     float(state.viewState->clearColour.Alpha())/255.0f);
    glClearStencil(state.viewState->clearStencil);
    glClear(myClearMask);
}

void pglContext::SetupHardwareProjection(void)
{
    switch(state.viewState->projectionMode)
    {
        case PDDI_PROJECTION_DEVICE :
            projection.Identity();
            projection.SetOrthographic(0, display->GetWidth(),
                      display->GetHeight(), 0,
                      -1000.0f, 1000.0f);
            glViewport(0, 0, display->GetWidth(), display->GetHeight());
            break;

        case PDDI_PROJECTION_ORTHOGRAPHIC :
            projection.Identity();
            projection.SetOrthographic(-0.5,  0.5,
                      -((1/state.viewState->camera.aspect)/2),  ((1/state.viewState->camera.aspect)/2),
                      (state.viewState->camera.nearPlane),(state.viewState->camera.farPlane));
            glViewport(int(state.viewState->viewWindow.left * display->GetWidth()), 
                              int((1.0f - state.viewState->viewWindow.bottom) * display->GetHeight() ),
                              int((state.viewState->viewWindow.right - state.viewState->viewWindow.left) * display->GetWidth()), 
                              int((state.viewState->viewWindow.bottom - state.viewState->viewWindow.top) * display->GetHeight()));
            break;

        case PDDI_PROJECTION_PERSPECTIVE :
            projection.Identity();
            projection.SetPerspective(state.viewState->camera.fov,state.viewState->camera.aspect,state.viewState->camera.nearPlane,state.viewState->camera.farPlane);
            glViewport(int(state.viewState->viewWindow.left * display->GetWidth()), 
                            int((1.0f - state.viewState->viewWindow.bottom) * display->GetHeight() ),
                            int((state.viewState->viewWindow.right - state.viewState->viewWindow.left) * display->GetWidth()), 
                            int((state.viewState->viewWindow.bottom - state.viewState->viewWindow.top) * display->GetHeight()));
            break;
        default:
            PDDIASSERTMSG(0, "Bad projection mode","");
            break;
    }

    if(currentProgram)
        currentProgram->SetProjectionMatrix(&projection);
}

void pglContext::LoadHardwareMatrix(pddiMatrixType id)
{
    switch(id)
    {
        case PDDI_MATRIX_MODELVIEW :
        {
            if(currentProgram)
                currentProgram->SetModelViewMatrix(state.matrixStack[id]->Top());
        }
        break;
        default :
            PDDIASSERTMSG(0, "Invalid matrix load","");
            break;
    }
}

// viewport clipping
void pglContext::SetScissor(pddiRect* rect)
{
    pddiBaseContext::SetScissor(rect);
    if(!rect)
    {
        glDisable(GL_SCISSOR_TEST);
    }
    else
    {
        glScissor(rect->left, display->GetHeight() - rect->bottom, rect->right - rect->left, rect->bottom - rect->top);
        glEnable(GL_SCISSOR_TEST);
    }
}

#include <vector>
class pglPrimStream : public pddiPrimStream
{
public:
    std::vector<pddiVector> coords;
    std::vector<pddiVector> normals;
    std::vector<GLubyte> colours;
    std::vector<pddiVector2> uvs;

    GLenum primitive;
    unsigned vertexType;

    void Coord(float x, float y, float z)  
    {
        coords.push_back( pddiVector{ x, y, z } );
    }

    void Normal(float x, float y, float z) 
    {
        normals.push_back( pddiVector{ x, y, z } );
    }

    void Colour(pddiColour colour, int channel = 0)
    {
        colours.push_back( colour.Red() );
        colours.push_back( colour.Green() );
        colours.push_back( colour.Blue() );
        colours.push_back( colour.Alpha() );
    }

    void UV(float u, float v, int channel = 0) 
    { 
        if(channel == 0)
        {
            uvs.push_back( pddiVector2{ u, v } );
        }
    }

    void Specular(pddiColour colour) 
    {
        //
    }

    void Vertex(pddiVector* v, pddiColour c) 
    {
        colours.push_back( c.Red() );
        colours.push_back( c.Green() );
        colours.push_back( c.Blue() );
        colours.push_back( c.Alpha() );
        coords.push_back( *v );
    }

    void Vertex(pddiVector* v, pddiVector* n)
    {
        normals.push_back( *n );
        coords.push_back( *v );
    }

    void Vertex(pddiVector* v, pddiVector2* uv)
    {
        uvs.push_back( *uv );
        coords.push_back( *v );
    }

    void Vertex(pddiVector* v, pddiColour c, pddiVector2* uv)
    {
        colours.push_back( c.Red() );
        colours.push_back( c.Green() );
        colours.push_back( c.Blue() );
        colours.push_back( c.Alpha() );
        uvs.push_back( *uv );
        coords.push_back( *v );
    }

    void Vertex(pddiVector* v, pddiVector* n, pddiVector2* uv)
    {
        normals.push_back( *n );
        uvs.push_back( *uv );
        coords.push_back( *v );
    }

} thePrimStream;

pddiPrimStream* pglContext::BeginPrims(pddiShader* mat, pddiPrimType primType, unsigned vertexType, int vertexCount, unsigned pass)
{
    if(!mat)
        mat = defaultShader;

    pddiBaseContext::BeginPrims(mat, primType, vertexType, vertexCount);
    pddiBaseShader* material = (pddiBaseShader*)mat;
    ADD_STAT( PDDI_STAT_MATERIAL_OPS, !material->IsCurrent() );
    material->SetMaterial();
    thePrimStream.primitive = primTypeTable[primType];
    thePrimStream.vertexType = vertexType;
    return &thePrimStream;
}

void pglContext::EndPrims(pddiPrimStream* stream)
{
    MICROPROFILE_SCOPEI("SRR2", "pglContext::EndPrims", MP_RED);

    pddiBaseContext::EndPrims(stream);
    pglPrimStream* glstream = (pglPrimStream*)stream;

#ifdef RAD_TVOS
    unsigned int frame = s_tvosFrameCount;
    bool trace = TvosShouldTraceFrame( frame );
    bool hadErr = false;
    if ( trace )
    {
        TvosClearGlErrors( );
    }
#endif

    glBindVertexArrayOES( 0 );
#ifdef RAD_TVOS
    if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glBindVertexArrayOES", frame );
#endif
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
#ifdef RAD_TVOS
    if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glBindBuffer.ARRAY", frame );
#endif
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
#ifdef RAD_TVOS
    if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glBindBuffer.ELEMENT", frame );
#endif
    glEnableVertexAttribArray( 0 );
#ifdef RAD_TVOS
    if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glEnableVertexAttribArray.0", frame );
#endif
    glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 0, glstream->coords.data() );
#ifdef RAD_TVOS
    if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glVertexAttribPointer.0", frame );
#endif

    if( !glstream->normals.empty() )
    {
        glEnableVertexAttribArray( 1 );
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glEnableVertexAttribArray.1", frame );
#endif
        glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, 0, glstream->normals.data() );
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glVertexAttribPointer.1", frame );
#endif
    }
    else
    {
        glDisableVertexAttribArray( 1 );
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glDisableVertexAttribArray.1", frame );
#endif
        glVertexAttrib3f( 1, 0.0f, 0.0f, 0.0f );
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glVertexAttrib3f.1", frame );
#endif
    }

    if( !glstream->uvs.empty() )
    {
        glEnableVertexAttribArray( 2 );
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glEnableVertexAttribArray.2", frame );
#endif
        glVertexAttribPointer( 2, 2, GL_FLOAT, GL_FALSE, 0, glstream->uvs.data() );
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glVertexAttribPointer.2", frame );
#endif
    }
    else
    {
        glDisableVertexAttribArray( 2 );
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glDisableVertexAttribArray.2", frame );
#endif
        glVertexAttrib2f( 2, 0.0f, 0.0f );
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glVertexAttrib2f.2", frame );
#endif
    }

    if( !glstream->colours.empty() )
    {
        glEnableVertexAttribArray( 3 );
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glEnableVertexAttribArray.3", frame );
#endif
        glVertexAttribPointer( 3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, glstream->colours.data() );
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glVertexAttribPointer.3", frame );
#endif
    }
    else
    {
        glDisableVertexAttribArray( 3 );
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glDisableVertexAttribArray.3", frame );
#endif
        glVertexAttrib4f( 3, 1.0f, 1.0f, 1.0f, 1.0f );
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glVertexAttrib4f.3", frame );
#endif
    }

    glDrawArrays( glstream->primitive, 0, glstream->coords.size() );
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
    s_tvosDrawCallCount++;
    s_tvosVertexCount += glstream->coords.size();
#endif
#ifdef RAD_TVOS
    if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "EndPrims.glDrawArrays", frame );
#endif

    glstream->coords.clear();
    glstream->normals.clear();
    glstream->colours.clear();
    glstream->uvs.clear();
}

class pglPrimBufferStream : public pddiPrimBufferStream
{
public:
    pglPrimBuffer* buffer;

    pglPrimBufferStream(pglPrimBuffer* b)
    {
        buffer = b;
    }

    bool CanWrite(const char* op)
    {
        if(buffer && buffer->total < buffer->allocated)
        {
            return true;
        }
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        static unsigned int s_oobWriteCount = 0;
        if(s_oobWriteCount++ < 10)
        {
            SRR2::Diagnostics::RecordRejectedDraw( "prim_stream_oob", 1 );
            SRR2::Diagnostics::Anomalyf(SRR2::Diagnostics::RENDER,
                    "[PRIM_OOB] stream write skipped op=%s total=%u allocated=%u",
                    op ? op : "?", buffer ? buffer->total : 0, buffer ? buffer->allocated : 0);
        }
#endif
        return false;
    }

    void Next(void)  
    {
        if(!CanWrite("Next"))
        {
            return;
        }

        if(buffer->coord)
            buffer->coord = (float*)((char*)buffer->coord + buffer->stride);

        if(buffer->normal)
            buffer->normal = (float*)((char*)buffer->normal + buffer->stride);

        if(buffer->uv0)
            buffer->uv0 = (float*)((char*)buffer->uv0 + buffer->stride);

        if(buffer->uv1)
            buffer->uv1 = (float*)((char*)buffer->uv1 + buffer->stride);

        if(buffer->colour)
            buffer->colour += buffer->stride;

        buffer->total++;
        PDDIASSERT(buffer->total <= buffer->allocated);
    }

    void Position(float x, float y, float z)  
    { 
        if(!CanWrite("Position") || !buffer->coord)
            return;
        buffer->coord[0] = x;
        buffer->coord[1] = y;
        buffer->coord[2] = z;
        Next();
    }

    void Normal(float x, float y, float z) 
    { 
        if(!CanWrite("Normal") || !buffer->normal)
            return;
        buffer->normal[0] = x;
        buffer->normal[1] = y;
        buffer->normal[2] = z;
    }

    void Colour(pddiColour colour, int channel = 0)         
    {
        // HBW: Multiple CBVs not yet implemented.  For now just ignore channel.
        if(!CanWrite("Colour") || !buffer->colour)
            return;
        buffer->colour[0] = colour.Red();
        buffer->colour[1] = colour.Green();
        buffer->colour[2] = colour.Blue();
        buffer->colour[3] = colour.Alpha();
    }

    void TexCoord1(float u, int channel = 0) {}

    void TexCoord2(float u, float v, int channel = 0) 
    { 
        if(!CanWrite("TexCoord2"))
            return;
        if(channel == 0 && buffer->uv0)
        {
            buffer->uv0[0] = u;
            buffer->uv0[1] = v;
        }
        else if(channel == 1 && buffer->uv1)
        {
            buffer->uv1[0] = u;
            buffer->uv1[1] = v;
        }
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        else if(channel > 1)
        {
            // Log dropped UV channels beyond UV1 (first few occurrences)
            static int s_droppedUVCount = 0;
            if(s_droppedUVCount++ < 10)
                SRR2::Diagnostics::Anomalyf(SRR2::Diagnostics::RENDER,
                    "[GLES_FMT] UV channel %d dropped (only UV0/UV1 supported)", channel);
        }
#endif
    }

    void TexCoord3(float u, float v, float s, int channel = 0) {}
    void TexCoord4(float u, float v, float s, float t, int channel = 0) {}

    void Specular(pddiColour colour) 
    {
        //
    }

    void SkinIndices(unsigned, unsigned, unsigned, unsigned)
    {
    }

    void SkinWeights(float, float, float)
    {
    }

    void Vertex(pddiVector* v, pddiColour c) 
    {
        if(!CanWrite("VertexPC") || !buffer->coord || !buffer->colour || !v)
            return;
        buffer->colour[0] = c.Red();
        buffer->colour[1] = c.Green();
        buffer->colour[2] = c.Blue();
        buffer->colour[3] = c.Alpha();
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiVector* n)
    {
        if(!CanWrite("VertexPN") || !buffer->coord || !buffer->normal || !v || !n)
            return;
        buffer->normal[0] = n->x;
        buffer->normal[1] = n->y;
        buffer->normal[2] = n->z;
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiVector2* uv)
    {
        if(!CanWrite("VertexPT") || !buffer->coord || !v)
            return;
        if(buffer->uv0 && uv)
        {
            buffer->uv0[0] = uv->u;
            buffer->uv0[1] = uv->v;
        }
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiColour c, pddiVector2* uv)
    {
        if(!CanWrite("VertexPCT") || !buffer->coord || !buffer->colour || !v)
            return;
        buffer->colour[0] = c.Red();
        buffer->colour[1] = c.Green();
        buffer->colour[2] = c.Blue();
        buffer->colour[3] = c.Alpha();
        if(buffer->uv0 && uv)
        {
            buffer->uv0[0] = uv->u;
            buffer->uv0[1] = uv->v;
        }
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiVector* n, pddiVector2* uv)
    {
        if(!CanWrite("VertexPNT") || !buffer->coord || !buffer->normal || !v || !n)
            return;
        buffer->normal[0] = n->x;
        buffer->normal[1] = n->y;
        buffer->normal[2] = n->z;
        if(buffer->uv0 && uv)
        {
            buffer->uv0[0] = uv->u;
            buffer->uv0[1] = uv->v;
        }
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    bool CheckMemImageVersion(int version) { return false; }
    void* GetMemImagePtr()                 { return NULL; }
    unsigned GetMemImageLength()           { return 0; }

};

pglPrimBuffer::pglPrimBuffer(pglContext* c, pddiPrimType type, unsigned vertexFormat, int nVertex, int nIndex) : context(c)
{
    if(nVertex < 0)
    {
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        SRR2::Diagnostics::Errorf(SRR2::Diagnostics::RENDER,
            "[PRIM_CREATE_CLAMP] negative vertex count=%d", nVertex);
#endif
        nVertex = 0;
    }
    if(nIndex < 0)
    {
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        SRR2::Diagnostics::Errorf(SRR2::Diagnostics::RENDER,
            "[PRIM_CREATE_CLAMP] negative index count=%d", nIndex);
#endif
        nIndex = 0;
    }

    stream = new pglPrimBufferStream(this);

    total = allocated = stride = nStrips = 0;
    coord = normal = uv0 = uv1 = NULL;
    colour = NULL;
    strips = NULL;
    indices = NULL;
    
    // Initialize computed offsets
    coordOffset = normalOffset = uv0Offset = uv1Offset = colourOffset = 0;
    numUVSets = 0;

    valid = false;
    vertexBuffer = indexBuffer = vertexArray = 0;

    primType = type;

    // GLES path only supports a subset of PDDI vertex components.
    // If we compute stride using unsupported flags, attribute offsets become wrong and
    // affected meshes can render invisible while collision still works.
    unsigned supported = vertexFormat;
    // Keep UV count but clamp to 2 (UV0 + UV1).
    unsigned uvCount = supported & PDDI_V_UVMASK;
    if ( uvCount > 2 )
    {
        uvCount = 2;
    }
    supported &= ~PDDI_V_UVMASK;
    supported |= uvCount;

    // If COLOUR2 is used, fall back to a single vertex colour set (channel 0).
    if ( supported & PDDI_V_COLOUR2 )
    {
        supported &= ~PDDI_V_COLOUR2;
        supported &= ~PDDI_V_COLOUR_MASK;
        supported |= PDDI_V_COLOUR;
    }

    // Drop unsupported/unused components.
    supported &= ~PDDI_V_SPECULAR;
    supported &= ~PDDI_V_BINORMAL;
    supported &= ~PDDI_V_TANGENT;
    supported &= ~PDDI_V_SIZE;
    supported &= ~PDDI_V_W;

    // POSITION bit isn't used by the GLES packing (position is always present), but keep it for consistency.
    vertexType = supported;

    allocated = nVertex;
    
    // Compute stride dynamically based on vertex format
    // Position is always present: 3 floats = 12 bytes
    stride = 12;
    coordOffset = 0;
    
    unsigned currentOffset = 12;
    
    // Normal: 3 floats = 12 bytes
    if(vertexType & PDDI_V_NORMAL)
    {
        normalOffset = currentOffset;
        currentOffset += 12;
        stride += 12;
    }
    
    // UV sets: check how many UV channels are needed (lower 4 bits = UV count)
    uvCount = vertexType & PDDI_V_UVMASK;
    if(uvCount >= 1)
    {
        uv0Offset = currentOffset;
        currentOffset += 8;  // 2 floats = 8 bytes
        stride += 8;
        numUVSets = 1;
    }
    if(uvCount >= 2)
    {
        uv1Offset = currentOffset;
        currentOffset += 8;  // 2 floats = 8 bytes
        stride += 8;
        numUVSets = 2;
    }
    
    // Colour: 4 bytes (RGBA)
    if(vertexType & PDDI_V_COLOUR)
    {
        colourOffset = currentOffset;
        currentOffset += 4;
        stride += 4;
    }
    if(stride < 36)
    {
        stride = 36;
    }

#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
    // Log vertex format info for debugging (first few buffers)
    static int s_bufferCreateCount = 0;
    if(s_bufferCreateCount++ < 20)
    {
        SRR2::Diagnostics::Tracef(SRR2::Diagnostics::RENDER,
                "[GLES_FMT] PrimBuffer: format=0x%x stride=%u uvCount=%u normal=%d colour=%d",
                vertexFormat, stride, numUVSets, 
                (vertexFormat & PDDI_V_NORMAL) ? 1 : 0,
                (vertexFormat & PDDI_V_COLOUR) ? 1 : 0);
    }
#endif

    mem = stride * nVertex;
    buffer = new unsigned char[mem];
    memset(buffer, 0, mem);  // Zero-initialize for safety

    // Set up pointers into the buffer
    unsigned char* ptr = buffer;
    coord = (float*)ptr;
    
    if(vertexType & PDDI_V_NORMAL)
    {
        normal = (float*)(buffer + normalOffset);
    }
    
    if(numUVSets >= 1)
    {
        uv0 = (float*)(buffer + uv0Offset);
    }
    
    if(numUVSets >= 2)
    {
        uv1 = (float*)(buffer + uv1Offset);
    }
    
    if(vertexType & PDDI_V_COLOUR)
    {
        colour = buffer + colourOffset;
    }

    indexCount = nIndex;
    indexRangeValidated = false;
    indexRangeValid = true;
    if(indexCount) 
        indices = new unsigned short[indexCount];

    nStrips = 0;
    strips = NULL;

    context->ADD_STAT(PDDI_STAT_BUFFERED_COUNT, 1);
    context->ADD_STAT(PDDI_STAT_BUFFERED_ALLOC, mem / 1024.0f);
}

pglPrimBuffer::~pglPrimBuffer()
{
    delete stream;

    delete [] strips;
    delete [] indices;
    delete [] buffer;

    context->ADD_STAT(PDDI_STAT_BUFFERED_COUNT, -1);
    context->ADD_STAT(PDDI_STAT_BUFFERED_ALLOC, -mem / 1024.0f);

    GLuint buffers[] = { vertexBuffer, indexBuffer };
    glDeleteBuffers(2, buffers);
    glDeleteVertexArraysOES(1, &vertexArray);
}

pddiPrimBufferStream* pglPrimBuffer::Lock()
{
    total = 0;
    return stream;
}

void pglPrimBuffer::Unlock(pddiPrimBufferStream* stream)
{
    if(coord)
        coord = (float*)((char*)coord - total * stride);

    if(normal)
        normal = (float*)((char*)normal - total * stride);

    if(uv0)
        uv0 = (float*)((char*)uv0 - total * stride);

    if(uv1)
        uv1 = (float*)((char*)uv1 - total * stride);

    if(colour)
        colour -= total * stride;

    indexRangeValidated = false;
    indexRangeValid = true;
    valid = false;
}

unsigned char* pglPrimBuffer::LockIndexBuffer()
{
    PDDIASSERT(0);
    return NULL;
}

void pglPrimBuffer::UnlockIndexBuffer(int count)
{
    PDDIASSERT(0);
}

void pglPrimBuffer::SetIndices(unsigned short* i, int count)
{
    if(i == NULL || indices == NULL || count <= 0)
    {
        indexRangeValidated = true;
        indexRangeValid = (count == 0);
        valid = false;
        return;
    }

    if(count > (int)indexCount)
    {
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        SRR2::Diagnostics::Anomalyf(SRR2::Diagnostics::RENDER,
            "[PRIM_INDEX_CLAMP] requested=%d capacity=%u", count, indexCount);
#endif
        count = (int)indexCount;
    }

    memcpy(indices, i, count * sizeof(unsigned short));
    indexCount = (unsigned)count;
    indexRangeValidated = false;
    indexRangeValid = true;
    valid = false;
}

// Static counters for tracking skipped geometry
static unsigned int s_skipCount = 0;
static unsigned int s_lastSkipLogFrame = 0;

void pglPrimBuffer::Display(void)
{
    MICROPROFILE_SCOPEI("PDDI", "pglPrimBuffer::Display", MP_RED);

    bool rebuiltOnce = false;

rebuild_attempt:

    // Guard against freed/invalid buffer data
    // Note: For indexed geometry, total=0 is valid - indexCount is used instead
    if(!buffer || (!total && !indexCount))
    {
        s_skipCount++;
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        unsigned int frame = s_tvosFrameCount;
        // Log every 300 frames if we're skipping geometry
        if(frame >= s_lastSkipLogFrame + 300 && s_skipCount > 0)
        {
            SRR2::Diagnostics::RecordRejectedDraw( "prim_skip_empty", s_skipCount );
            SRR2::Diagnostics::Anomalyf(SRR2::Diagnostics::RENDER,
                    "[PRIM_SKIP] frame=%u skipped=%u buffer=%p total=%u indexCount=%u",
                    frame, s_skipCount, (void*)buffer, total, indexCount);
            s_skipCount = 0;
            s_lastSkipLogFrame = frame;
        }
#endif
        return;
    }

#ifdef RAD_TVOS
    unsigned int frame = s_tvosFrameCount;
    bool trace = TvosShouldTraceFrame( frame );
    bool hadErr = false;
    if ( trace )
    {
        TvosClearGlErrors( );
    }
#endif

    if(!valid)
    {
        if(!vertexArray)
            glGenVertexArraysOES(1, &vertexArray);
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glGenVertexArraysOES", frame );
#endif
        glBindVertexArrayOES(vertexArray);
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glBindVertexArrayOES", frame );
#endif

        if(!vertexBuffer)
            glGenBuffers(1, &vertexBuffer);
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glGenBuffers.VBO", frame );
#endif
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glBindBuffer.ARRAY", frame );
#endif
        glBufferData(GL_ARRAY_BUFFER, mem, buffer, GL_STATIC_DRAW);
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glBufferData.ARRAY", frame );
#endif

        if(indexCount && indices)
        {
            if(!indexBuffer)
                glGenBuffers(1, &indexBuffer);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glGenBuffers.IBO", frame );
#endif
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,indexBuffer);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glBindBuffer.ELEMENT", frame );
#endif
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,indexCount*sizeof(unsigned short),indices,GL_STATIC_DRAW);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glBufferData.ELEMENT", frame );
#endif
        }
        else
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glBindBuffer.ELEMENT.0", frame );
#endif
        }

        // Use computed offsets instead of hardcoded values
        // Attribute layout:
        //   0 = position (vec3)
        //   1 = normal (vec3)
        //   2 = UV0 (vec2)
        //   3 = colour (vec4 ubyte)
        //   4 = UV1 (vec2) - for lightmaps
        
        // Position (always present)
        glEnableVertexAttribArray(0);
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glEnableVertexAttribArray.0", frame );
#endif
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)(uintptr_t)coordOffset);
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glVertexAttribPointer.0", frame );
#endif

        // Normal
        if(vertexType & PDDI_V_NORMAL)
        {
            glEnableVertexAttribArray(1);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glEnableVertexAttribArray.1", frame );
#endif
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(uintptr_t)normalOffset);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glVertexAttribPointer.1", frame );
#endif
        }
        else
        {
            glDisableVertexAttribArray(1);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glDisableVertexAttribArray.1", frame );
#endif
            glVertexAttrib3f(1, 0.0f, 0.0f, 0.0f);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glVertexAttrib3f.1", frame );
#endif
        }

        // UV0 (primary texture coordinates)
        if(numUVSets >= 1)
        {
            glEnableVertexAttribArray(2);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glEnableVertexAttribArray.2", frame );
#endif
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(uintptr_t)uv0Offset);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glVertexAttribPointer.2", frame );
#endif
        }
        else
        {
            glDisableVertexAttribArray(2);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glDisableVertexAttribArray.2", frame );
#endif
            glVertexAttrib2f(2, 0.0f, 0.0f);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glVertexAttrib2f.2", frame );
#endif
        }

        // Colour
        if(vertexType & PDDI_V_COLOUR)
        {
            glEnableVertexAttribArray(3);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glEnableVertexAttribArray.3", frame );
#endif
            glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)(uintptr_t)colourOffset);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glVertexAttribPointer.3", frame );
#endif
        }
        else
        {
            glDisableVertexAttribArray(3);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glDisableVertexAttribArray.3", frame );
#endif
            glVertexAttrib4f(3, 1.0f, 1.0f, 1.0f, 1.0f);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glVertexAttrib4f.3", frame );
#endif
        }

        // UV1 (lightmap/detail texture coordinates)
        if(numUVSets >= 2)
        {
            glEnableVertexAttribArray(4);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glEnableVertexAttribArray.4", frame );
#endif
            glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, (void*)(uintptr_t)uv1Offset);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glVertexAttribPointer.4", frame );
#endif
        }
        else
        {
            glDisableVertexAttribArray(4);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glDisableVertexAttribArray.4", frame );
#endif
            glVertexAttrib2f(4, 0.0f, 0.0f);
#ifdef RAD_TVOS
            if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glVertexAttrib2f.4", frame );
#endif
        }

        valid = true;
    }
    else
    {
        glBindVertexArrayOES(vertexArray);
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glBindVertexArrayOES.cached", frame );
#endif
    }

    // Final safety check - ensure VAO is valid before drawing
    if(!vertexArray || !vertexBuffer)
    {
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        SRR2::Diagnostics::RecordRejectedDraw( "prim_invalid_buffer", 1 );
        SRR2::Diagnostics::Anomalyf(SRR2::Diagnostics::RENDER,
                "[PRIM_INVALID] frame=%u vertexArray=%u vertexBuffer=%u indexBuffer=%u valid=%d",
                frame, vertexArray, vertexBuffer, indexBuffer, (int)valid);
#endif
        if(!rebuiltOnce)
        {
            rebuiltOnce = true;
            valid = false;
            if(!vertexArray)
                vertexArray = 0;
            if(!vertexBuffer)
                vertexBuffer = 0;
            if(indexCount && indices && !indexBuffer)
                indexBuffer = 0;
            goto rebuild_attempt;
        }
        return;
    }

    if(indexCount && indices)
    {
        if(!indexRangeValidated)
        {
            unsigned short maxIndex = 0;
            for(unsigned i = 0; i < indexCount; ++i)
            {
                if(indices[i] > maxIndex)
                {
                    maxIndex = indices[i];
                }
            }

            indexRangeValid = (allocated > 0 && maxIndex < allocated);
            indexRangeValidated = true;
        }

        if(!indexRangeValid)
        {
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
            SRR2::Diagnostics::RecordRejectedDraw( "prim_index_oob", 1 );
            SRR2::Diagnostics::Errorf(SRR2::Diagnostics::RENDER,
                "[PRIM_INDEX_OOB] indexCount=%u allocatedVerts=%u", indexCount, allocated);
#endif
            glBindVertexArrayOES(0);
            return;
        }

        // Ensure index buffer is bound (VAO may not have captured it correctly)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        glDrawElements(primTypeTable[primType],indexCount,GL_UNSIGNED_SHORT,0);
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        s_tvosDrawCallCount++;
        s_tvosVertexCount += indexCount;
#endif
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glDrawElements", frame );
#endif
    }
    else
    {
        if(total)
            glDrawArrays(primTypeTable[primType], 0, total);
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        s_tvosDrawCallCount++;
        s_tvosVertexCount += total;
#endif
#ifdef RAD_TVOS
        if ( trace && !hadErr ) hadErr = TvosConsumeAndLogGlError( "PrimBuffer.glDrawArrays", frame );
#endif
    }
    
    // Unbind VAO after draw to prevent state leakage on tvOS/iOS
    // Apple's GLES implementation can have VAO state "stick" between draw calls
    glBindVertexArrayOES(0);
}

/*
    float* uv;
    unsigned char* colour;

    unsigned allocated;
    unsigned total;

};
*/

void pglContext::DrawPrimBuffer(pddiShader* mat, pddiPrimBuffer* buffer)
{
    // Guard against NULL buffer pointer
    if(!buffer)
    {
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        static unsigned int s_nullBufferCount = 0;
        static unsigned int s_lastNullLog = 0;
        s_nullBufferCount++;
        unsigned int frame = s_tvosFrameCount;
        if(frame >= s_lastNullLog + 300)
        {
            SRR2::Diagnostics::RecordRejectedDraw( "draw_null_buffer", s_nullBufferCount );
            SRR2::Diagnostics::Anomalyf(SRR2::Diagnostics::RENDER,
                "[DRAW_SKIP] frame=%u nullBuffer=%u", frame, s_nullBufferCount);
            s_nullBufferCount = 0;
            s_lastNullLog = frame;
        }
#endif
        return;
    }

    if(!mat)
        mat = defaultShader;

    pddiBaseShader* material = (pddiBaseShader*)mat;
    ADD_STAT(PDDI_STAT_MATERIAL_OPS, !material->IsCurrent());
    material->SetMaterial();
    ((pglPrimBuffer*)buffer)->Display();
}

// lighting

int pglContext::GetMaxLights(void)
{
    return PDDI_MAX_LIGHTS;
}

void pglContext::SetupHardwareLight(int handle)
{
    if(currentProgram)
        currentProgram->SetLightState(handle, &state.lightingState->light[handle]);
}

void pglContext::SetAmbientLight(pddiColour col)
{
    pddiBaseContext::SetAmbientLight(col);
    if(currentProgram)
        currentProgram->SetAmbientLight(col);
}


// backface culling
GLenum cullModeTable[3] =
{
    GL_FRONT, // PDDI_CULL_NONE (disabled using glDisable())
    GL_FRONT, // PDDI_CULL_NORMAL
    GL_BACK   // PDDI_CULL_INVERTED
};
    
void pglContext::SetCullMode(pddiCullMode mode)
{
    pddiBaseContext::SetCullMode(mode);

    if(mode == PDDI_CULL_NONE)
    {
        glDisable(GL_CULL_FACE);
    }
    else
    {
        glEnable(GL_CULL_FACE);
        glCullFace(cullModeTable[mode]);
    }
}

// z-buffer control
GLenum compTable[8] = {
    GL_NEVER,
    GL_ALWAYS,  
    GL_LESS,
    GL_LEQUAL,
    GL_GREATER,    
    GL_GEQUAL,  
    GL_EQUAL,
    GL_NOTEQUAL,
};

void pglContext::SetColourWrite( bool red, bool green, bool blue, bool alpha )
{
    pddiBaseContext::SetColourWrite(red, green, blue, alpha);
    glColorMask(red, green, blue, alpha);
}

void pglContext::EnableZBuffer(bool enable)
{
    pddiBaseContext::EnableZBuffer(enable);
    if(enable)
    {
        glEnable(GL_DEPTH_TEST);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
}


void pglContext::SetZCompare(pddiCompareMode compareMode)
{
    pddiBaseContext::SetZCompare(compareMode);
    glDepthFunc(compTable[compareMode]);
}

void pglContext::SetZWrite(bool b)
{
    pddiBaseContext::SetZWrite(b);
    glDepthMask(b);
}

void pglContext::SetZBias(float bias)
{
    pddiBaseContext::SetZBias(bias);
    
    // Use glPolygonOffset to implement Z bias for preventing z-fighting
    // between coplanar surfaces like roads on terrain.
    // D3D's ZBIAS uses small integer values (0-16), glPolygonOffset needs scaling.
    // factor: scales by polygon slope (use 0 for flat surfaces like roads)
    // units: constant depth offset (negative = toward camera = renders on top)
    if(bias != 0.0f)
    {
        glEnable(GL_POLYGON_OFFSET_FILL);
        // tvOS/iOS requires much larger offset values than Win32 D3D due to different
        // depth buffer precision and range. Increased from 2.0 to 8.0 for roads/terrain.
        // Factor of -2.0 helps with sloped surfaces on Apple GPUs.
        glPolygonOffset(-2.0f, -bias * 8.0f);
    }
    else
    {
        glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(0.0f, 0.0f);
    }
}

void pglContext::SetZRange(float n, float f)
{
    pddiBaseContext::SetZRange(n,f);
    glDepthRangef(n,f);
}

// stencil buffer control
GLenum stencilTable[6] = {
    GL_KEEP,
    GL_ZERO,
    GL_REPLACE,
    GL_INCR,
    GL_DECR,
    GL_INVERT
};

void pglContext::EnableStencilBuffer(bool enable)
{
    pddiBaseContext::EnableStencilBuffer(enable);
    if(enable)
        glEnable(GL_STENCIL_TEST);
    else
        glDisable(GL_STENCIL_TEST);
}
        
void pglContext::SetStencilCompare(pddiCompareMode compare)
{
    pddiBaseContext::SetStencilCompare(compare);
    glStencilFunc(compTable[compare], state.stencilState->ref, state.stencilState->mask);
}

void pglContext::SetStencilRef(int ref)
{
    pddiBaseContext::SetStencilRef(ref);
    glStencilFunc(compTable[state.stencilState->compare], ref, state.stencilState->mask);
}

void pglContext::SetStencilMask(unsigned mask)
{
    pddiBaseContext::SetStencilMask(mask);
    glStencilFunc(compTable[state.stencilState->compare], state.stencilState->ref, mask);
}

void pglContext::SetStencilWriteMask(unsigned mask)
{
    pddiBaseContext::SetStencilWriteMask(mask);
    glStencilMask(mask);
}

void pglContext::SetStencilOp(pddiStencilOp failOp, pddiStencilOp zFailOp, pddiStencilOp zPassOp)
{
    pddiBaseContext::SetStencilOp(failOp, zFailOp, zPassOp);
    glStencilOp(stencilTable[failOp],stencilTable[zFailOp],stencilTable[zPassOp]);
}

void pglContext::SetFillMode(pddiFillMode mode)
{
    pddiBaseContext::SetFillMode(mode);
}

// fog
void pglContext::EnableFog(bool enable)
{
    pddiBaseContext::EnableFog(enable);
}

void pglContext::SetFog(pddiColour colour, float start, float end)
{
    pddiBaseContext::SetFog(colour,start,end);

    float fog[4];
    fog[0] = float(colour.Red()) / 255;
    fog[1] = float(colour.Green()) / 255;
    fog[2] = float(colour.Blue()) / 255;
    fog[3] = float(colour.Alpha()) / 255;
}

int pglContext::GetMaxTextureDimension(void)
{
    return maxTexSize;
}

pddiExtension* pglContext::GetExtension(unsigned extID)
{ 
    switch(extID)
    {
        case PDDI_EXT_GL_CONTEXT :
            return extContext;
        case PDDI_EXT_GAMMACONTROL :
            return extGamma;
    }

    return pddiBaseContext::GetExtension(extID);
}

bool pglContext::VerifyExtension(unsigned extID)
{ 
    switch(extID)
    {
        case PDDI_EXT_GL_CONTEXT :
        case PDDI_EXT_GAMMACONTROL :
            return true;
    }

    return pddiBaseContext::VerifyExtension(extID);
}

void  pglContext::BeginTiming(void)
{
    display->BeginTiming();
}

float pglContext::EndTiming(void)
{
    return display->EndTiming();
}

void pglContext::SetShaderProgram(pglProgram* program)
{
    if(program == currentProgram)
        return;

    if(currentProgram)
        currentProgram->Release();
    currentProgram = program;
    if(!currentProgram)
        return;

    currentProgram->AddRef();
    glUseProgram(currentProgram->GetProgram());
    currentProgram->SetProjectionMatrix(&projection);

    LoadHardwareMatrix(PDDI_MATRIX_MODELVIEW);
    if(currentProgram->SupportsLighting())
    {
        for (int i = 0; i < PDDI_MAX_LIGHTS; i++)
            SetupHardwareLight(i);
        SetAmbientLight(state.lightingState->ambient);
    }
}

void pglContext::InvalidateShaderProgram()
{
    if(currentProgram)
    {
        currentProgram->Release();
        currentProgram = nullptr;
    }
}

#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
// Static counters for shader usage tracking
static unsigned int s_colorProgramCount = 0;
static unsigned int s_textureProgramCount = 0;
static unsigned int s_shaderLogInterval = 0;
#endif

void pglContext::SetTextureEnvironment(const pglTextureEnv* texEnv)
{
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
    // One-time diagnostic for shader program selection
    static bool s_texEnvDiagDone = false;
    static unsigned int s_texEnvCallCount = 0;
    s_texEnvCallCount++;
    
    if ( !s_texEnvDiagDone && s_texEnvCallCount > 100 && texEnv->texture ) {
        s_texEnvDiagDone = true;
        
        pglProgram* selectedProg = texEnv->alphaTest ? alphaTestProgram[texEnv->lit] : textureProgram[texEnv->lit];
        if ( selectedProg == colorProgram[0] || selectedProg == colorProgram[1] ) {
            SRR2::Diagnostics::Anomalyf(
                SRR2::Diagnostics::RENDER,
                "[SHADER_SELECT] WARNING: Using COLOR program while texture exists tex=%p alphaTest=%d lit=%d selected=%p color0=%p texture0=%p",
                (void*)texEnv->texture,
                texEnv->alphaTest ? 1 : 0,
                texEnv->lit ? 1 : 0,
                (void*)selectedProg,
                (void*)colorProgram[0],
                (void*)textureProgram[0] );
        } else {
            SRR2::Diagnostics::Tracef(
                SRR2::Diagnostics::RENDER,
                "[SHADER_SELECT] tex=%p alphaTest=%d lit=%d selected=%p color0=%p texture0=%p",
                (void*)texEnv->texture,
                texEnv->alphaTest ? 1 : 0,
                texEnv->lit ? 1 : 0,
                (void*)selectedProg,
                (void*)colorProgram[0],
                (void*)textureProgram[0] );
        }
    }
    
    // Track shader program usage
    if(texEnv->texture)
        s_textureProgramCount++;
    else
        s_colorProgramCount++;
    
    // Log shader usage stats periodically
    s_shaderLogInterval++;
    if(s_shaderLogInterval >= 18000) // ~5 seconds at 60fps with 300 calls/frame
    {
        SRR2::Diagnostics::Summaryf(SRR2::Diagnostics::RENDER,
                "[SHADER_STATS] textureProgram=%u colorProgram=%u",
                s_textureProgramCount, s_colorProgramCount);
        s_textureProgramCount = 0;
        s_colorProgramCount = 0;
        s_shaderLogInterval = 0;
    }
#endif

    if(texEnv->texture)
    {
        // Select appropriate shader program based on shader type
        if(texEnv->usesLightmap && texEnv->lightmapTexture && lightmapProgram[texEnv->lit])
        {
            // Use lightmap shader for geometry that needs base * lightmap blending
            SetShaderProgram(lightmapProgram[texEnv->lit]);
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
            static int s_lightmapUseCount = 0;
            if(s_lightmapUseCount++ < 5)
                SRR2::Diagnostics::Tracef(SRR2::Diagnostics::RENDER,
                    "[GLES_SHADER] Using lightmap program (lit=%d)", texEnv->lit ? 1 : 0);
#endif
        }
        else if(texEnv->alphaTest)
        {
            SetShaderProgram(alphaTestProgram[texEnv->lit]);
        }
        else
        {
            SetShaderProgram(textureProgram[texEnv->lit]);
        }
    }
    else
    {
        SetShaderProgram(colorProgram[texEnv->lit]);
    }
    currentProgram->SetTextureEnvironment(texEnv);
}
