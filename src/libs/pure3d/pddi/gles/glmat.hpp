//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _GLMAT_HPP_
#define _GLMAT_HPP_

#include <pddi/pddi.hpp>
#include <pddi/base/baseshader.hpp>
#include <pddi/gles/gl.hpp>
#include <pddi/gles/glcon.hpp>
class pglTexture;
class pglProgram;

const int pglMaxPasses = 1;

// Shader type enumeration for GLES backend
enum pglShaderType
{
    PGL_SHADER_SIMPLE = 0,      // Basic single-texture shader
    PGL_SHADER_LIGHTMAP,        // Base texture * lightmap (2 textures, UV0 + UV1)
    PGL_SHADER_LAYERED,         // Multi-layer terrain/detail shader
    PGL_SHADER_ENVIRONMENT,     // Environment/reflection mapping
    PGL_SHADER_COUNT
};

struct pglTextureEnv
{
    bool enabled;
    pglTexture* texture;
    pglTexture* lightmapTexture;  // Second texture for lightmap/detail

    int uvSet;
    int lightmapUVSet;            // UV set for lightmap (typically 1)
    pddiTextureGen texGen;
    pddiUVMode uvMode;
    pddiFilterMode filterMode;

    bool alphaTest;
    pddiBlendMode alphaBlendMode;
    pddiCompareMode alphaCompareMode;
    float alphaRef;

    bool lit;
    bool twoSided;
    pddiShadeMode shadeMode;
    pddiColour diffuse;
    pddiColour specular;
    pddiColour ambient;
    pddiColour emissive;
    float shininess;
    
    // Shader type info
    pglShaderType shaderType;
    bool usesLightmap;
    bool usesDetailMap;
};

class pglMat : public pddiBaseShader
{
public:
    pglMat(pglContext*, pglShaderType type = PGL_SHADER_SIMPLE);
    ~pglMat();

    static pddiShadeColourTable colourTable[];
    static pddiShadeTextureTable textureTable[];
    static pddiShadeIntTable intTable[];
    static pddiShadeFloatTable floatTable[];
    
    // Shader type accessors
    pglShaderType GetShaderType() const { return shaderType; }
    bool UsesLightmap() const { return shaderType == PGL_SHADER_LIGHTMAP || texEnv[pass].usesLightmap; }
    bool UsesMultiUV() const { return UsesLightmap() || shaderType == PGL_SHADER_LAYERED; }

    const char* GetType(void);
    int         GetPasses(void);
    void        SetPass(int pass);

    pddiShadeTextureTable* GetTextureTable(void) { return textureTable;}
    pddiShadeIntTable*     GetIntTable(void)     { return intTable;}
    pddiShadeFloatTable*   GetFloatTable(void)   { return floatTable;}
    pddiShadeColourTable*  GetColourTable(void)  { return colourTable;}

    // texture
    void SetTexture(pddiTexture* texture);
    void SetLightmapTexture(pddiTexture* texture);  // For lightmap/detail second texture
    void SetUVMode(int mode);
    void SetFilterMode(int mode);

    // shading
    void SetShadeMode(int shade);
    void SetTwoSided(int);

    // lighting
    void EnableLighting(int);

    void SetDiffuse(pddiColour colour);
    void SetAmbient(pddiColour colour);
    void SetEmissive(pddiColour);
    void SetEmissiveAlpha(int);
    void SetSpecular(pddiColour);
    void SetShininess(float power);

    // alpha blending
    void SetBlendMode(int mode);
    void EnableAlphaTest(int);
    void SetAlphaCompare(int compare);
    void SetAlphaRef(float ref);

    int  CountDevPasses(void);
    void SetDevPass(unsigned);

private:
    pglContext* context;
    int pass;
    pglShaderType shaderType;
    pglTextureEnv texEnv[pglMaxPasses];
};

#endif

