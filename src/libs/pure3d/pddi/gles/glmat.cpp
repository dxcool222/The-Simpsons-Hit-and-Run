//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glmat.hpp>
#include <diagnostics/tvosdiagnostics.h>
#include <pddi/gles/gltex.hpp>
#include <pddi/gles/glcon.hpp>
#include <pddi/gles/glprog.hpp>

#include <vector>
#include <microprofile.h>

// DIAGNOSTIC: Uncomment to force ALL geometry to render as solid magenta
// This helps diagnose if geometry is being rasterized vs texture/shader issues
// #define DIAG_FORCE_SOLID_COLOR
#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif

pddiShadeColourTable pglMat::colourTable[] = 
{
    {PDDI_SP_AMBIENT  , SHADE_COLOUR(&pglMat::SetAmbient)},
    {PDDI_SP_DIFFUSE  , SHADE_COLOUR(&pglMat::SetDiffuse)},
    {PDDI_SP_EMISSIVE , SHADE_COLOUR(&pglMat::SetEmissive)},
    {PDDI_SP_SPECULAR , SHADE_COLOUR(&pglMat::SetSpecular)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeTextureTable pglMat::textureTable[] = 
{
    {PDDI_SP_BASETEX , SHADE_TEXTURE(&pglMat::SetTexture)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeIntTable pglMat::intTable[] = 
{
    {PDDI_SP_UVMODE , SHADE_INT(&pglMat::SetUVMode)},
    {PDDI_SP_FILTER , SHADE_INT(&pglMat::SetFilterMode)},
    {PDDI_SP_SHADEMODE , SHADE_INT(&pglMat::SetShadeMode)},
    {PDDI_SP_ISLIT , SHADE_INT(&pglMat::EnableLighting)},
    {PDDI_SP_BLENDMODE , SHADE_INT(&pglMat::SetBlendMode)},
    {PDDI_SP_ALPHATEST , SHADE_INT(&pglMat::EnableAlphaTest)},
    {PDDI_SP_ALPHACOMPARE , SHADE_INT(&pglMat::SetAlphaCompare)},
    {PDDI_SP_TWOSIDED , SHADE_INT(&pglMat::SetTwoSided)},
    {PDDI_SP_EMISSIVEALPHA , SHADE_INT(&pglMat::SetEmissiveAlpha)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeFloatTable pglMat::floatTable[] = 
{
    {PDDI_SP_SHININESS , SHADE_FLOAT(&pglMat::SetShininess)},
    {PDDI_SP_ALPHACOMPARE_THRESHOLD , SHADE_FLOAT(&pglMat::SetAlphaRef)},
    {PDDI_SP_NULL , NULL}
};

GLenum filterMagTable[5] =
{
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST,
    GL_LINEAR,
    GL_LINEAR
};

// GL_NEAREST_MIPMAP_LINEAR not used
GLenum filterMinTable[5] =
{
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST,//GL_NEAREST_MIPMAP_NEAREST,
    GL_LINEAR,//GL_LINEAR_MIPMAP_NEAREST,
    GL_LINEAR,//GL_LINEAR_MIPMAP_LINEAR
};

GLenum uvTable[3] =
{
    GL_REPEAT,
    GL_CLAMP_TO_EDGE,
    GL_CLAMP_TO_EDGE
};

GLenum alphaCompareTable[8] =
{
    GL_NEVER,
    GL_ALWAYS,
    GL_LESS,
    GL_LEQUAL,
    GL_GREATER,
    GL_GEQUAL,
    GL_EQUAL,
    GL_NOTEQUAL
};

GLenum alphaBlendTable[8][3] =
{
    { GL_FUNC_ADD, GL_ONE, GL_ZERO },                       //PDDI_BLEND_NONE,
    { GL_FUNC_ADD, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA },  //PDDI_BLEND_ALPHA
    { GL_FUNC_ADD, GL_ONE, GL_ONE },                        //PDDI_BLEND_ADD
    { GL_FUNC_REVERSE_SUBTRACT, GL_ONE, GL_ONE },           //PDDI_BLEND_SUBTRACT
    { GL_FUNC_ADD, GL_DST_COLOR, GL_ZERO },                 //PDDI_BLEND_MODULATE,
    { GL_FUNC_ADD, GL_DST_COLOR, GL_SRC_COLOR},             //PDDI_BLEND_MODULATE2,
    { GL_FUNC_ADD, GL_ONE, GL_SRC_ALPHA},                   //PDDI_BLEND_ADDMODULATEALPHA,
    { GL_FUNC_REVERSE_SUBTRACT, GL_SRC_ALPHA, GL_SRC_ALPHA} //PDDI_BLEND_SUBMODULATEALPHA
};

pglMat::pglMat(pglContext* c, pglShaderType type) 
{
    context = c;
    shaderType = type;

    for(int i = 0; i < pglMaxPasses; i++)
    {
        texEnv[i].enabled = false;
        texEnv[i].texture = NULL;
        texEnv[i].lightmapTexture = NULL;
        texEnv[i].uvSet = 0;
        texEnv[i].lightmapUVSet = 1;  // Lightmap typically uses UV1
        texEnv[i].texGen = PDDI_TEXGEN_NONE;
        texEnv[i].uvMode = PDDI_UV_CLAMP;
        texEnv[i].filterMode = PDDI_FILTER_BILINEAR;

        texEnv[i].lit = false;
        texEnv[i].twoSided = false;
        texEnv[i].shadeMode = PDDI_SHADE_GOURAUD;
        texEnv[i].ambient.Set(255,255,255);
        texEnv[i].diffuse.Set(255,255,255);
        texEnv[i].specular.Set(0,0,0);
        texEnv[i].emissive.Set(0,0,0);
        texEnv[i].shininess = 0.0f;

    //   srcBlend = PDDI_BF_ONE;
    //   destBlend = PDDI_BF_ZERO;

        texEnv[i].alphaTest = false;
        texEnv[i].alphaCompareMode = PDDI_COMPARE_GREATEREQUAL;
        texEnv[i].alphaBlendMode = PDDI_BLEND_NONE;
        texEnv[i].alphaRef = 0.5f;
        
        // Shader type info
        texEnv[i].shaderType = type;
        texEnv[i].usesLightmap = (type == PGL_SHADER_LIGHTMAP);
        texEnv[i].usesDetailMap = (type == PGL_SHADER_LAYERED);
    }
    texEnv[0].enabled = true;
    pass = 0;
}

pglMat::~pglMat() 
{
    for(int i = 0; i < pglMaxPasses; i++)
    {
        if(texEnv[i].texture)
            texEnv[i].texture->Release();
        if(texEnv[i].lightmapTexture)
            texEnv[i].lightmapTexture->Release();
    }
}


const char* pglMat::GetType(void)
{
    static char simple[] = "simple";
    static char lightmap[] = "lightmap";
    static char layered[] = "layered";
    static char environment[] = "environment";
    
    switch(shaderType)
    {
        case PGL_SHADER_LIGHTMAP:    return lightmap;
        case PGL_SHADER_LAYERED:     return layered;
        case PGL_SHADER_ENVIRONMENT: return environment;
        default:                     return simple;
    }
}

int pglMat::GetPasses(void)
{
    return 1;
}

void pglMat::SetPass(int pass)
{
    SetDevPass(pass);
}

void pglMat::SetTexture(pddiTexture* t) 
{
    if(t == texEnv[pass].texture)
        return;

    if(texEnv[pass].texture)
        texEnv[pass].texture->Release();

    texEnv[pass].texture = (pglTexture*)t;

    if(texEnv[pass].texture)
        texEnv[pass].texture->AddRef();
}

void pglMat::SetLightmapTexture(pddiTexture* t) 
{
    if(t == texEnv[pass].lightmapTexture)
        return;

    if(texEnv[pass].lightmapTexture)
        texEnv[pass].lightmapTexture->Release();

    texEnv[pass].lightmapTexture = (pglTexture*)t;

    if(texEnv[pass].lightmapTexture)
    {
        texEnv[pass].lightmapTexture->AddRef();
        texEnv[pass].usesLightmap = true;
    }
    else
    {
        texEnv[pass].usesLightmap = false;
    }
}

void pglMat::SetUVMode(int mode) 
{
    texEnv[pass].uvMode = (pddiUVMode)mode;
}

void pglMat::SetFilterMode(int mode) 
{
    texEnv[pass].filterMode = (pddiFilterMode)mode;
}

void pglMat::SetShadeMode(int shade) 
{
    texEnv[pass].shadeMode = (pddiShadeMode)shade;
}

void pglMat::SetTwoSided(int b)
{
    texEnv[pass].twoSided = b != 0;
}

void pglMat::EnableLighting(int b)
{
    texEnv[pass].lit = b != 0;
}

void pglMat::SetAmbient(pddiColour a) 
{
    texEnv[pass].ambient = a;
}

void pglMat::SetDiffuse(pddiColour colour) 
{
    texEnv[pass].diffuse = colour;
}

void pglMat::SetSpecular(pddiColour c) 
{
    texEnv[pass].specular = c;
}

void pglMat::SetEmissive(pddiColour c) 
{
    texEnv[pass].emissive = c;
    SetEmissiveAlpha(c.Alpha());
}

void pglMat::SetEmissiveAlpha(int alpha)
{
    texEnv[pass].diffuse.SetAlpha(alpha);
    if(alpha < 255)
    {
        texEnv[pass].specular.SetAlpha(0);
        texEnv[pass].ambient.SetAlpha(0);
        texEnv[pass].emissive.SetAlpha(0);
    }
    else
    {
        texEnv[pass].specular.SetAlpha(255);
        texEnv[pass].ambient.SetAlpha(255);
        texEnv[pass].emissive.SetAlpha(255);
    }
}

void pglMat::SetShininess(float power) 
{
    texEnv[pass].shininess = power;
}

void pglMat::SetBlendMode(int mode) 
{
    texEnv[pass].alphaBlendMode = (pddiBlendMode)mode;
}

void pglMat::EnableAlphaTest(int b) 
{
    texEnv[pass].alphaTest = b != 0;
}

void pglMat::SetAlphaCompare(int compare) 
{
    texEnv[pass].alphaCompareMode = pddiCompareMode(compare);
}

void pglMat::SetAlphaRef(float ref) 
{
    texEnv[pass].alphaRef = ref;
}

int pglMat::CountDevPasses(void) 
{
    return 1;
}

// Diagnostic define: Enable to force all geometry to render as solid color
// This helps identify if missing geometry is shader/UV issue vs vertex format/stride issue
// If geometry appears in solid color -> bug is shader/UV/lightmap path
// If geometry still missing -> it's vertex format/stride/attrib binding
// #define DIAG_FORCE_SOLID_COLOR

#ifdef RAD_TVOS
// External flag set by StaticEntityDSG to force Zone 3 geometry to render red
extern bool g_debugForceZone3Red;
#endif

void pglMat::SetDevPass(unsigned pass)
{
    MICROPROFILE_SCOPEI( "PDDI", "pglMat::SetDevPass", MP_RED );

#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
    // One-time diagnostic for video shader binding
    static unsigned int s_setDevPassCount = 0;
    static bool s_shaderDiagDone = false;
    s_setDevPassCount++;
#endif

    int i = 0;
    
#ifdef RAD_TVOS
    // Zone 3 diagnostic: Force red rendering when flag is set
    if(g_debugForceZone3Red)
    {
        pglTextureEnv diagEnv = texEnv[i];
        diagEnv.texture = NULL;           // No texture = use color program
        diagEnv.lightmapTexture = NULL;
        diagEnv.usesLightmap = false;
        diagEnv.alphaTest = false;
        diagEnv.lit = false;
        // Force bright red for Zone 3 geometry
        diagEnv.diffuse.Set(255, 0, 0, 255);
        context->SetTextureEnvironment(&diagEnv);
    }
    else
#endif
#if defined(RAD_TVOS) && defined(DIAG_FORCE_SOLID_COLOR)
    // Force solid color rendering for diagnostic purposes
    // Create a temporary texEnv that forces color-only rendering
    pglTextureEnv diagEnv = texEnv[i];
    diagEnv.texture = NULL;           // No texture = use color program
    diagEnv.lightmapTexture = NULL;
    diagEnv.usesLightmap = false;
    diagEnv.alphaTest = false;
    diagEnv.lit = false;
    // Force a visible solid color (magenta for easy spotting)
    diagEnv.diffuse.Set(255, 0, 255, 255);
    context->SetTextureEnvironment(&diagEnv);
    
    static bool s_diagLogDone = false;
    if(!s_diagLogDone)
    {
        s_diagLogDone = true;
        SRR2::Diagnostics::Anomalyf(
            SRR2::Diagnostics::RENDER,
            "[RENDER_FORCE_SOLID_COLOR] enabled=1 color=magenta" );
    }
#else
    {
        context->SetTextureEnvironment(&texEnv[i]);
    }
#endif

#if !defined(DIAG_FORCE_SOLID_COLOR)
    if(texEnv[i].texture)
    {
        texEnv[i].texture->SetGLState();

        if(texEnv[i].usesLightmap && texEnv[i].lightmapTexture)
        {
            glActiveTexture(GL_TEXTURE1);
            texEnv[i].lightmapTexture->SetGLState();
            glActiveTexture(GL_TEXTURE0);
        }

#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        // Log shader binding diagnostic ONCE when texture is set
        if ( !s_shaderDiagDone && s_setDevPassCount > 50 ) {
            s_shaderDiagDone = true;
            GLint currentProg = 0, boundTex = 0;
            glGetIntegerv( GL_CURRENT_PROGRAM, &currentProg );
            glGetIntegerv( GL_TEXTURE_BINDING_2D, &boundTex );
            SRR2::Diagnostics::Tracef(
                SRR2::Diagnostics::RENDER,
                "[SHADER_STATE_TRACE] texture=%p glProgram=%d boundTex=%d",
                (void*)texEnv[i].texture,
                currentProg,
                boundTex );
        }
#endif

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filterMagTable[texEnv[i].filterMode]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filterMinTable[texEnv[i].filterMode]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, uvTable[texEnv[i].uvMode]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, uvTable[texEnv[i].uvMode]);
    }
#endif // !DIAG_FORCE_SOLID_COLOR

    if(texEnv[i].alphaBlendMode == PDDI_BLEND_NONE)
    {
        glDisable(GL_BLEND);
    }
    else
    {
        glEnable(GL_BLEND);
        glBlendEquation(alphaBlendTable[texEnv[i].alphaBlendMode][0]);
        glBlendFunc(alphaBlendTable[texEnv[i].alphaBlendMode][1],alphaBlendTable[texEnv[i].alphaBlendMode][2]);
    }

    if( texEnv[i].twoSided || context->GetCullMode() == PDDI_CULL_NONE )
    {
        glDisable(GL_CULL_FACE);
    }
    else
    {
        glEnable(GL_CULL_FACE);
    }

#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
    // DIAGNOSTIC: Log material state for first 50 draws + every 1000th
    static int s_matStateDiagCount = 0;
    s_matStateDiagCount++;
    if(s_matStateDiagCount <= 50 || (s_matStateDiagCount % 1000 == 0))
    {
        GLboolean depthMask, depthTest, blendEnabled;
        GLint depthFunc, blendSrc, blendDst;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glGetBooleanv(GL_DEPTH_TEST, &depthTest);
        blendEnabled = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrc);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDst);
        
        int diffuseAlpha = texEnv[i].diffuse.Alpha();
        
        // Get texture dimensions if texture exists
        int texW = 0, texH = 0;
        if(texEnv[i].texture)
        {
            texW = texEnv[i].texture->GetWidth();
            texH = texEnv[i].texture->GetHeight();
        }
        
        SRR2::Diagnostics::Tracef(SRR2::Diagnostics::RENDER,
            "[MAT_STATE] #%d tex=%p(%dx%d) blendMode=%d diffuseA=%d zWrite=%d zTest=%d blend=%d zFunc=0x%x",
            s_matStateDiagCount,
            (void*)texEnv[i].texture,
            texW, texH,
            (int)texEnv[i].alphaBlendMode,
            diffuseAlpha,
            depthMask ? 1 : 0,
            depthTest ? 1 : 0,
            blendEnabled ? 1 : 0,
            depthFunc);
    }
#endif
}
