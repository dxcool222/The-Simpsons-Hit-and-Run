//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glcon.hpp>
#include <pddi/gles/gldisplay.hpp>
#include <pddi/base/debug.hpp>
#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif

#include <stdio.h>
#include <string.h>
#include <vector>

static const int kRenderWidth = 1920;
static const int kRenderHeight = 1080;

// GL_OES_depth24 extension constant - 24-bit depth buffer for better precision
#ifndef GL_DEPTH_COMPONENT24_OES
#define GL_DEPTH_COMPONENT24_OES 0x81A6
#endif

static void CreateRenderTargets( GLuint* outFBO, GLuint* outColor, GLuint* outDepth );
static void DestroyRenderTargets( GLuint* fbo, GLuint* color, GLuint* depth );

static const char* GlErrorToString( GLenum err )
{
    switch ( err )
    {
        case GL_NO_ERROR: return "GL_NO_ERROR";
        case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
#ifdef GL_INVALID_FRAMEBUFFER_OPERATION
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
#endif
        default: return "GL_UNKNOWN_ERROR";
    }
}

static void LogGlErrorOnce( const char* tag )
{
    GLenum err = glGetError( );
    if ( err != GL_NO_ERROR )
    {
        SDL_Log( "TVOS_GL_ERR %s: 0x%x (%s)", tag ? tag : "", (unsigned)err, GlErrorToString( err ) );
    }
}

static bool CompileShaderWithLog( GLuint shader, const char* src, const char* label )
{
    glShaderSource( shader, 1, &src, NULL );
    glCompileShader( shader );

    GLint ok = 0;
    glGetShaderiv( shader, GL_COMPILE_STATUS, &ok );
    if ( ok == GL_FALSE )
    {
        GLint len = 0;
        glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &len );
        if ( len < 1 )
        {
            SDL_Log( "TVOS_GL Shader compile failed (%s): <no log>", label ? label : "" );
        }
        else
        {
            std::vector<GLchar> buf( (size_t)len );
            glGetShaderInfoLog( shader, len, &len, buf.data( ) );
            SDL_Log( "TVOS_GL Shader compile failed (%s): %s", label ? label : "", buf.data( ) );
        }
        return false;
    }

    return true;
}

static bool LinkProgramWithLog( GLuint program )
{
    glLinkProgram( program );
    GLint ok = 0;
    glGetProgramiv( program, GL_LINK_STATUS, &ok );
    if ( ok == GL_FALSE )
    {
        GLint len = 0;
        glGetProgramiv( program, GL_INFO_LOG_LENGTH, &len );
        if ( len < 1 )
        {
            SDL_Log( "TVOS_GL Program link failed: <no log>" );
        }
        else
        {
            std::vector<GLchar> buf( (size_t)len );
            glGetProgramInfoLog( program, len, &len, buf.data( ) );
            SDL_Log( "TVOS_GL Program link failed: %s", buf.data( ) );
        }
        return false;
    }
    return true;
}

bool pglDisplay::CheckExtension( const char* extName )
{
    return SDL_GL_ExtensionSupported( extName ) == SDL_TRUE;
}

pglDisplay::pglDisplay( pddiDisplayInfo* info )
{
    displayInfo = info;
    mode = PDDI_DISPLAY_FULLSCREEN;
    winWidth = kRenderWidth;
    winHeight = kRenderHeight;
    winBitDepth = 32;

    context = NULL;

    win = NULL;
    hRC = NULL;
    prevRC = NULL;

    extBGRA = false;

    gammaR = gammaG = gammaB = 1.0f;

    reset = true;
    m_ForceVSync = false;

    mRenderFBO = 0;
    mRenderColor = 0;
    mRenderDepth = 0;

    mWindowFBO = 0;
    mWindowColorRB = 0;
}

pglDisplay::~pglDisplay()
{
    if ( hRC )
    {
        SDL_GL_MakeCurrent( win, hRC );
        DestroyRenderTargets( &mRenderFBO, &mRenderColor, &mRenderDepth );
        SDL_GL_DeleteContext( hRC );
        hRC = NULL;
    }
}

long pglDisplay::ProcessWindowMessage( SDL_Window* /*wnd*/, const SDL_WindowEvent* /*event*/ )
{
    return 0;
}

void pglDisplay::SetWindow( SDL_Window* wnd )
{
    win = wnd;
}

bool pglDisplay::InitDisplay( int x, int y, int bpp )
{
    if ( ( x == winWidth ) && ( y == winHeight ) && ( bpp == winBitDepth ) )
    {
        return true;
    }

    displayInit.xsize = x;
    displayInit.ysize = y;
    displayInit.bpp = bpp;

    return InitDisplay( &displayInit );
}

static void CreateRenderTargets( GLuint* outFBO, GLuint* outColor, GLuint* outDepth )
{
    GLint prevFB = 0;
    glGetIntegerv( GL_FRAMEBUFFER_BINDING, &prevFB );

    SDL_Log( "TVOS_GL CreateRenderTargets begin: %dx%d", kRenderWidth, kRenderHeight );
    glGenTextures( 1, outColor );
    glBindTexture( GL_TEXTURE_2D, *outColor );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, kRenderWidth, kRenderHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
    LogGlErrorOnce( "CreateRenderTargets.Tex" );

    glGenRenderbuffers( 1, outDepth );
    glBindRenderbuffer( GL_RENDERBUFFER, *outDepth );
    // Use 24-bit depth buffer to prevent z-fighting and see-through terrain issues
    // GL_DEPTH_COMPONENT24 is available on tvOS via GL_OES_depth24 extension
    // Check extension support and fall back to 16-bit if needed
    bool hasDepth24 = SDL_GL_ExtensionSupported( "GL_OES_depth24" ) == SDL_TRUE;
    if ( hasDepth24 )
    {
        glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24_OES, kRenderWidth, kRenderHeight );
        SDL_Log( "TVOS_GL Using 24-bit depth buffer (GL_OES_depth24 supported)" );
    }
    else
    {
        // Fallback to 16-bit depth - may cause Z-fighting issues
        glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, kRenderWidth, kRenderHeight );
        SDL_Log( "TVOS_GL WARNING: Using 16-bit depth buffer (GL_OES_depth24 NOT supported) - may cause Z-fighting!" );
    }
    LogGlErrorOnce( "CreateRenderTargets.DepthBuffer" );

    glGenFramebuffers( 1, outFBO );
    glBindFramebuffer( GL_FRAMEBUFFER, *outFBO );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *outColor, 0 );
    glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *outDepth );

    GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
    SDL_Log( "TVOS_GL CreateRenderTargets FBO=%u Color=%u Depth=%u status=0x%x", (unsigned)*outFBO, (unsigned)*outColor, (unsigned)*outDepth, (unsigned)status );
    PDDIASSERT( status == GL_FRAMEBUFFER_COMPLETE );
    LogGlErrorOnce( "CreateRenderTargets.FBO" );

    glBindFramebuffer( GL_FRAMEBUFFER, (GLuint)prevFB );

    // Leave GL in a neutral state so SDL's swap/present path doesn't inherit our renderbuffer.
    glBindRenderbuffer( GL_RENDERBUFFER, 0 );
    glBindTexture( GL_TEXTURE_2D, 0 );
}

static void DestroyRenderTargets( GLuint* fbo, GLuint* color, GLuint* depth )
{
    if ( *depth )
    {
        glDeleteRenderbuffers( 1, depth );
        *depth = 0;
    }
    if ( *color )
    {
        glDeleteTextures( 1, color );
        *color = 0;
    }
    if ( *fbo )
    {
        glDeleteFramebuffers( 1, fbo );
        *fbo = 0;
    }
}

bool pglDisplay::InitDisplay( const pddiDisplayInit* init )
{
    displayInit = *init;

    mode = init->displayMode;
    winBitDepth = init->bpp;

    reset = true;

    if ( hRC )
    {
        // Keep the existing context; tvOS always renders at fixed resolution.
        return true;
    }

    prevRC = SDL_GL_GetCurrentContext();
    hRC = SDL_GL_CreateContext( win );
    PDDIASSERT( hRC );

    SDL_GL_MakeCurrent( win, hRC );

    // Capture SDL's window surface bindings (on iOS/tvOS this may not be framebuffer 0).
    {
        GLint fb = 0;
        GLint rb = 0;
        glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fb );
        glGetIntegerv( GL_RENDERBUFFER_BINDING, &rb );
        mWindowFBO = (GLuint)fb;
        mWindowColorRB = (GLuint)rb;
        SDL_Log( "TVOS_GL Window surface bindings: fb=%u rb=%u", (unsigned)mWindowFBO, (unsigned)mWindowColorRB );
        LogGlErrorOnce( "InitDisplay.WindowSurfaceBindings" );
    }

    {
        int drawableW = 0;
        int drawableH = 0;
        SDL_GL_GetDrawableSize( win, &drawableW, &drawableH );

        int r = 0, g = 0, b = 0, a = 0, depth = 0, stencil = 0, dbl = 0;
        SDL_GL_GetAttribute( SDL_GL_RED_SIZE, &r );
        SDL_GL_GetAttribute( SDL_GL_GREEN_SIZE, &g );
        SDL_GL_GetAttribute( SDL_GL_BLUE_SIZE, &b );
        SDL_GL_GetAttribute( SDL_GL_ALPHA_SIZE, &a );
        SDL_GL_GetAttribute( SDL_GL_DEPTH_SIZE, &depth );
        SDL_GL_GetAttribute( SDL_GL_STENCIL_SIZE, &stencil );
        SDL_GL_GetAttribute( SDL_GL_DOUBLEBUFFER, &dbl );

        SDL_Log( "TVOS_GL Context created: ctx=%p prev=%p drawable=%dx%d rgba=%d/%d/%d/%d depth=%d stencil=%d dbl=%d",
                 hRC, prevRC, drawableW, drawableH, r, g, b, a, depth, stencil, dbl );
    }

    char* glVendor = (char*)glGetString( GL_VENDOR );
    char* glRenderer = (char*)glGetString( GL_RENDERER );
    char* glVersion = (char*)glGetString( GL_VERSION );

    SDL_Log( "OpenGL ES - Vendor: %s, Renderer: %s, Version: %s", glVendor, glRenderer, glVersion );
    LogGlErrorOnce( "InitDisplay.glGetString" );

    // Apple's OpenGL ES on tvOS always supports BGRA textures via GL_EXT_texture_format_BGRA8888
    // Force this to true since extension string detection may fail but the functionality works
    extBGRA = true;
    SDL_Log( "TVOS_GL extBGRA forced=true (Apple GPU supports BGRA)" );

    CreateRenderTargets( &mRenderFBO, &mRenderColor, &mRenderDepth );

    glBindFramebuffer( GL_FRAMEBUFFER, mRenderFBO );
    GLenum rtStatus = glCheckFramebufferStatus( GL_FRAMEBUFFER );
    SDL_Log( "TVOS_GL RenderFBO bound=%u status=0x%x", (unsigned)mRenderFBO, (unsigned)rtStatus );
    glBindFramebuffer( GL_FRAMEBUFFER, mWindowFBO );
    LogGlErrorOnce( "InitDisplay.RenderFBO" );

    return true;
}

pddiDisplayInfo* pglDisplay::GetDisplayInfo( void )
{
    return displayInfo;
}

unsigned pglDisplay::GetFreeTextureMem( void )
{
    return unsigned( -1 );
}

unsigned pglDisplay::GetBufferMask( void )
{
    return unsigned( -1 );
}

int pglDisplay::GetHeight( void )
{
    return kRenderHeight;
}

int pglDisplay::GetWidth( void )
{
    return kRenderWidth;
}

int pglDisplay::GetDepth( void )
{
    return winBitDepth;
}

pddiDisplayMode pglDisplay::GetDisplayMode( void )
{
    return mode;
}

int pglDisplay::GetNumColourBuffer( void )
{
    return 2;
}

void pglDisplay::GetGamma( float* r, float* g, float* b )
{
    *r = gammaR;
    *g = gammaG;
    *b = gammaB;
}

void pglDisplay::SetGamma( float r, float g, float b )
{
    gammaR = r;
    gammaG = g;
    gammaB = b;
}

static void ComputeLetterboxViewport( int dstW, int dstH, int srcW, int srcH, int* outX, int* outY, int* outW, int* outH )
{
    const float srcAspect = (float)srcW / (float)srcH;
    const float dstAspect = (float)dstW / (float)dstH;

    int w = dstW;
    int h = dstH;

    if ( dstAspect > srcAspect )
    {
        w = (int)( (float)dstH * srcAspect );
        h = dstH;
    }
    else
    {
        w = dstW;
        h = (int)( (float)dstW / srcAspect );
    }

    *outW = w;
    *outH = h;
    *outX = ( dstW - w ) / 2;
    *outY = ( dstH - h ) / 2;
}

void pglDisplay::SwapBuffers( void )
{
    static unsigned int s_swapCount = 0;
    s_swapCount++;

    static GLuint program = 0;
    static GLint aPos = -1;
    static GLint aUV = -1;
    static GLint uTex = -1;
    static GLuint quadVBO = 0;
#ifdef GL_OES_vertex_array_object
    static GLuint quadVAO = 0;
    static bool s_checkedVAO = false;
    static bool s_hasVAO = false;
#endif

    // Make sure the display context is current for the present path.
    // The engine may leave no context current after EndFrame/EndContext.
    {
        SDL_GLContext cur = SDL_GL_GetCurrentContext();
        if ( cur != hRC )
        {
            int mkErr = SDL_GL_MakeCurrent( win, hRC );
            if ( mkErr != 0 )
            {
                SDL_Log( "TVOS_GL SwapBuffers: SDL_GL_MakeCurrent failed: %s", SDL_GetError() );
            }
        }
    }

    // Reset GL error state so we can attribute any errors to the present path.
    while ( glGetError() != GL_NO_ERROR )
    {
    }

    // Present the fixed-resolution FBO to the default framebuffer.
    int drawableW = 0;
    int drawableH = 0;
    SDL_GL_GetDrawableSize( win, &drawableW, &drawableH );

    GLint prevProgram = 0;
    GLint prevActiveTex = 0;
    GLint prevTex2D = 0;
    GLint prevArrayBuffer = 0;
    GLint prevElementArrayBuffer = 0;
    GLint prevFB = 0;
    GLint prevRB = 0;
    GLint prevViewport[4] = { 0, 0, 0, 0 };
    GLboolean prevDepthTest = GL_FALSE;
    GLboolean prevDepthMask = GL_TRUE;
    GLboolean prevCullFace = GL_FALSE;
    GLboolean prevBlend = GL_FALSE;
    GLboolean prevScissorTest = GL_FALSE;
    GLboolean prevStencilTest = GL_FALSE;
    GLboolean prevPolygonOffsetFill = GL_FALSE;
#ifndef RAD_VITAGL
    GLboolean prevDither = GL_FALSE;
#endif
    glGetIntegerv( GL_CURRENT_PROGRAM, &prevProgram );
    glGetIntegerv( GL_ACTIVE_TEXTURE, &prevActiveTex );
    glGetIntegerv( GL_TEXTURE_BINDING_2D, &prevTex2D );
    glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer );
    glGetIntegerv( GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElementArrayBuffer );
    glGetIntegerv( GL_FRAMEBUFFER_BINDING, &prevFB );
    glGetIntegerv( GL_RENDERBUFFER_BINDING, &prevRB );
    glGetIntegerv( GL_VIEWPORT, prevViewport );
    prevDepthTest = glIsEnabled( GL_DEPTH_TEST );
    glGetBooleanv( GL_DEPTH_WRITEMASK, &prevDepthMask );
    prevCullFace = glIsEnabled( GL_CULL_FACE );
    prevBlend = glIsEnabled( GL_BLEND );
    prevScissorTest = glIsEnabled( GL_SCISSOR_TEST );
    prevStencilTest = glIsEnabled( GL_STENCIL_TEST );
    prevPolygonOffsetFill = glIsEnabled( GL_POLYGON_OFFSET_FILL );
#ifndef RAD_VITAGL
    prevDither = glIsEnabled( GL_DITHER );
#endif

#ifdef GL_OES_vertex_array_object
    GLint prevVAO = 0;
    glGetIntegerv( GL_VERTEX_ARRAY_BINDING_OES, &prevVAO );
#endif

    glBindFramebuffer( GL_FRAMEBUFFER, mWindowFBO );

    // The main renderer may have VAOs/VBOs bound. Client-side attrib arrays can
    // become invalid (interpreted as offsets) if GL_ARRAY_BUFFER is non-zero.
#ifdef GL_OES_vertex_array_object
    if ( !s_checkedVAO )
    {
        s_checkedVAO = true;
        s_hasVAO = CheckExtension( "GL_OES_vertex_array_object" );
        SDL_Log( "TVOS_GL Present VAO support: %d", (int)s_hasVAO );
    }
#endif
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );

    if ( ( s_swapCount <= 5 ) || ( ( s_swapCount % 300 ) == 0 ) )
    {
        GLint fb = 0;
        glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fb );
        SDL_Log( "TVOS_GL SwapBuffers #%u: drawable=%dx%d bindFB=%d windowFBO=%u windowRB=%u renderFBO=%u renderTex=%u", s_swapCount, drawableW, drawableH, (int)fb, (unsigned)mWindowFBO, (unsigned)mWindowColorRB, (unsigned)mRenderFBO, (unsigned)mRenderColor );
    }

    int vx, vy, vw, vh;
    ComputeLetterboxViewport( drawableW, drawableH, kRenderWidth, kRenderHeight, &vx, &vy, &vw, &vh );

    glViewport( vx, vy, vw, vh );
    if ( ( s_swapCount <= 5 ) || ( ( s_swapCount % 300 ) == 0 ) )
    {
        SDL_Log( "TVOS_GL Present viewport: x=%d y=%d w=%d h=%d", vx, vy, vw, vh );
    }

#ifdef RAD_TVOS
    // Diagnostic screens removed - rendering is working
#endif

    glDisable( GL_DEPTH_TEST );
    glDisable( GL_CULL_FACE );
    glDisable( GL_SCISSOR_TEST );
    glDisable( GL_STENCIL_TEST );
    glDisable( GL_BLEND );
    glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

    if ( ( s_swapCount <= 5 ) || ( ( s_swapCount % 300 ) == 0 ) )
    {
        LogGlErrorOnce( "SwapBuffers.PresentState" );
    }

    static const char* vsSrc =
        "attribute vec2 aPos;\n"
        "attribute vec2 aUV;\n"
        "varying vec2 vUV;\n"
        "void main(){ vUV=aUV; gl_Position=vec4(aPos,0.0,1.0); }\n";

    static const char* fsSrc =
        "precision mediump float;\n"
        "varying vec2 vUV;\n"
        "uniform sampler2D uTex;\n"
        "void main(){ gl_FragColor = texture2D(uTex,vUV); }\n";

    if ( program == 0 )
    {
        GLuint vs = glCreateShader( GL_VERTEX_SHADER );
        bool vsOk = CompileShaderWithLog( vs, vsSrc, "present_vs" );

        GLuint fs = glCreateShader( GL_FRAGMENT_SHADER );
        bool fsOk = CompileShaderWithLog( fs, fsSrc, "present_fs" );

        program = glCreateProgram();
        glAttachShader( program, vs );
        glAttachShader( program, fs );
        bool linkOk = LinkProgramWithLog( program );

        glDeleteShader( vs );
        glDeleteShader( fs );

        if ( !( vsOk && fsOk && linkOk ) )
        {
            SDL_Log( "TVOS_GL Present program creation failed (vsOk=%d fsOk=%d linkOk=%d)", (int)vsOk, (int)fsOk, (int)linkOk );
        }

        aPos = glGetAttribLocation( program, "aPos" );
        aUV = glGetAttribLocation( program, "aUV" );
        uTex = glGetUniformLocation( program, "uTex" );

        SDL_Log( "TVOS_GL Present program=%u aPos=%d aUV=%d uTex=%d", (unsigned)program, (int)aPos, (int)aUV, (int)uTex );
        LogGlErrorOnce( "SwapBuffers.ProgramInit" );
    }

    if ( quadVBO == 0 )
    {
        static const GLfloat quadInterleaved[] = {
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f,  0.0f, 1.0f,
             1.0f,  1.0f,  1.0f, 1.0f,
        };

        glGenBuffers( 1, &quadVBO );
        glBindBuffer( GL_ARRAY_BUFFER, quadVBO );
        glBufferData( GL_ARRAY_BUFFER, sizeof( quadInterleaved ), quadInterleaved, GL_STATIC_DRAW );

#ifdef GL_OES_vertex_array_object
        if ( s_hasVAO )
        {
            glGenVertexArraysOES( 1, &quadVAO );
            glBindVertexArrayOES( quadVAO );
            glEnableVertexAttribArray( (GLuint)aPos );
            glEnableVertexAttribArray( (GLuint)aUV );
            glVertexAttribPointer( (GLuint)aPos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof( GLfloat ), (const void*)0 );
            glVertexAttribPointer( (GLuint)aUV, 2, GL_FLOAT, GL_FALSE, 4 * sizeof( GLfloat ), (const void*)( 2 * sizeof( GLfloat ) ) );
            glBindVertexArrayOES( 0 );
        }
#endif

        glBindBuffer( GL_ARRAY_BUFFER, 0 );
    }

    glUseProgram( program );
    if ( ( s_swapCount <= 5 ) || ( ( s_swapCount % 300 ) == 0 ) )
    {
        LogGlErrorOnce( "SwapBuffers.glUseProgram" );
    }

    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, mRenderColor );
    if ( ( s_swapCount <= 5 ) || ( ( s_swapCount % 300 ) == 0 ) )
    {
        LogGlErrorOnce( "SwapBuffers.glBindTexture" );
    }
    glUniform1i( uTex, 0 );
    if ( ( s_swapCount <= 5 ) || ( ( s_swapCount % 300 ) == 0 ) )
    {
        LogGlErrorOnce( "SwapBuffers.glUniform1i" );
    }

#ifdef GL_OES_vertex_array_object
    if ( s_hasVAO )
    {
        glBindVertexArrayOES( quadVAO );
        glEnableVertexAttribArray( (GLuint)aPos );
        glEnableVertexAttribArray( (GLuint)aUV );
    }
    else
#endif
    {
        glBindBuffer( GL_ARRAY_BUFFER, quadVBO );
        glEnableVertexAttribArray( (GLuint)aPos );
        glEnableVertexAttribArray( (GLuint)aUV );
        glVertexAttribPointer( (GLuint)aPos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof( GLfloat ), (const void*)0 );
        glVertexAttribPointer( (GLuint)aUV, 2, GL_FLOAT, GL_FALSE, 4 * sizeof( GLfloat ), (const void*)( 2 * sizeof( GLfloat ) ) );
    }

    if ( ( s_swapCount <= 5 ) || ( ( s_swapCount % 300 ) == 0 ) )
    {
        LogGlErrorOnce( "SwapBuffers.VertexSetup" );
    }

    glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
    if ( ( s_swapCount <= 5 ) || ( ( s_swapCount % 300 ) == 0 ) )
    {
        LogGlErrorOnce( "SwapBuffers.Draw" );
    }

#ifdef GL_OES_vertex_array_object
    if ( s_hasVAO )
    {
        glBindVertexArrayOES( 0 );
    }
    else
#endif
    {
        glDisableVertexAttribArray( (GLuint)aPos );
        glDisableVertexAttribArray( (GLuint)aUV );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );
    }

    // SDL's tvOS swap/present path can be sensitive to the currently bound renderbuffer.
    glBindRenderbuffer( GL_RENDERBUFFER, mWindowColorRB );
    if ( ( s_swapCount <= 5 ) || ( ( s_swapCount % 300 ) == 0 ) )
    {
        GLint rb = 0;
        glGetIntegerv( GL_RENDERBUFFER_BINDING, &rb );
        SDL_Log( "TVOS_GL PreSwap renderbuffer=%d", (int)rb );
        LogGlErrorOnce( "SwapBuffers.PreSwap" );
    }

    SDL_GL_SwapWindow( win );
    if ( ( s_swapCount <= 5 ) || ( ( s_swapCount % 300 ) == 0 ) )
    {
        const char* sdlErr = SDL_GetError();
        if ( sdlErr != NULL && sdlErr[0] != '\0' )
        {
            SDL_Log( "TVOS_GL SDL_GL_SwapWindow SDL_GetError: %s", sdlErr );
        }
        LogGlErrorOnce( "SwapBuffers.SwapWindow" );
    }

    // Rebind the render FBO for the next frame.
    glBindFramebuffer( GL_FRAMEBUFFER, mRenderFBO );
    if ( ( s_swapCount <= 5 ) || ( ( s_swapCount % 300 ) == 0 ) )
    {
        GLenum st = glCheckFramebufferStatus( GL_FRAMEBUFFER );
        SDL_Log( "TVOS_GL PostSwap rebind renderFBO=%u status=0x%x", (unsigned)mRenderFBO, (unsigned)st );
    }

    glUseProgram( (GLuint)prevProgram );
    glActiveTexture( (GLenum)prevActiveTex );
    glBindTexture( GL_TEXTURE_2D, (GLuint)prevTex2D );
    glBindBuffer( GL_ARRAY_BUFFER, (GLuint)prevArrayBuffer );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, (GLuint)prevElementArrayBuffer );
#ifdef GL_OES_vertex_array_object
    if ( s_hasVAO )
    {
        glBindVertexArrayOES( (GLuint)prevVAO );
    }
#endif
    glBindRenderbuffer( GL_RENDERBUFFER, 0 );
    if ( prevDepthTest ) glEnable( GL_DEPTH_TEST ); else glDisable( GL_DEPTH_TEST );
    glDepthMask( prevDepthMask );
    if ( prevCullFace ) glEnable( GL_CULL_FACE ); else glDisable( GL_CULL_FACE );
    if ( prevBlend ) glEnable( GL_BLEND ); else glDisable( GL_BLEND );
    if ( prevScissorTest ) glEnable( GL_SCISSOR_TEST ); else glDisable( GL_SCISSOR_TEST );
    if ( prevStencilTest ) glEnable( GL_STENCIL_TEST ); else glDisable( GL_STENCIL_TEST );
    if ( prevPolygonOffsetFill ) glEnable( GL_POLYGON_OFFSET_FILL ); else glDisable( GL_POLYGON_OFFSET_FILL );
#ifndef RAD_VITAGL
    if ( prevDither ) glEnable( GL_DITHER ); else glDisable( GL_DITHER );
#endif

    glBindFramebuffer( GL_FRAMEBUFFER, mRenderFBO );
    glViewport( 0, 0, kRenderWidth, kRenderHeight );

    reset = false;
}

unsigned pglDisplay::Screenshot( pddiColour* buffer, int nBytes )
{
    if ( nBytes < ( kRenderHeight * kRenderWidth * 4 ) )
        return 0;

    glBindFramebuffer( GL_FRAMEBUFFER, mRenderFBO );
    glReadPixels( 0, 0, kRenderWidth, kRenderHeight, GL_RGBA, GL_UNSIGNED_BYTE, buffer );
    return kRenderHeight * kRenderWidth * 4;
}

unsigned pglDisplay::FillDisplayModes( int /*displayIndex*/, pddiModeInfo* displayModes )
{
    displayModes[0].width = kRenderWidth;
    displayModes[0].height = kRenderHeight;
    displayModes[0].bpp = 32;
    return 1;
}

void pglDisplay::BeginTiming()
{
    beginTime = (float)SDL_GetTicks();
}

float pglDisplay::EndTiming()
{
    return (float)SDL_GetTicks() - beginTime;
}

void pglDisplay::BeginContext( void )
{
    prevRC = SDL_GL_GetCurrentContext();
    int error = SDL_GL_MakeCurrent( win, hRC );
    PDDIASSERT( !error );

    // Bind the fixed render target so PDDI renders into it.
    if ( mRenderFBO )
    {
        glBindFramebuffer( GL_FRAMEBUFFER, mRenderFBO );
        glViewport( 0, 0, kRenderWidth, kRenderHeight );
    }
}

void pglDisplay::EndContext( void )
{
    int error = SDL_GL_MakeCurrent( win, prevRC );
    PDDIASSERT( !error );
}
