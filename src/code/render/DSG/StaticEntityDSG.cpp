//========================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        StaticEntityDSG.cpp
//
// Description: Implementation for StaticEntityDSG class.
//
// History:     Implemented	                         --Devin [5/27/2002]
//========================================================================

//========================================
// System Includes
//========================================
#include <stdio.h>
#ifdef RAD_TVOS
#if defined(RAD_MACOS)
#include <pddi/gles/gl.hpp>
#else
#include <OpenGLES/ES2/gl.h>
#endif
#endif

//========================================
// Project Includes
//========================================
#include <render/DSG/StaticEntityDSG.h>
#include <diagnostics/tvosdiagnostics.h>
#include <memory/srrmemory.h>
#include <p3d/utility.hpp>
#include <p3d/shader.hpp>
#include <pddi/pddi.hpp>

// Zone 3 (Lisa's School) approximate bounding coordinates
// Based on l1z3.p3d entity bounding boxes from logs
static bool IsInZone3(float x, float z)
{
    // Zone 3 roughly covers: X from -200 to 50, Z from -600 to -400
    return (x >= -250.0f && x <= 100.0f && z >= -650.0f && z <= -350.0f);
}

// DEBUG: Global flag to force Zone 3 geometry to render bright red
// This helps diagnose if geometry is being rasterized vs texture/shader issues
#ifdef RAD_TVOS
bool g_debugForceZone3Red = false;
extern "C" bool IsDebugZone3Red() { return g_debugForceZone3Red; }
#endif

static bool IsInZone1(float x, float z)
{
    // Zone 1 roughly covers: X from 150 to 550, Z from -300 to 0
    return (x >= 100.0f && x <= 600.0f && z >= -350.0f && z <= 50.0f);
}

//************************************************************************
//
// Global Data, Local Data, Local Classes
//
//************************************************************************

//************************************************************************
//
// Public Member Functions : StaticEntityDSG Interface
//
//************************************************************************
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
StaticEntityDSG::StaticEntityDSG()
{
   mpDrawstuff = NULL;
}
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
StaticEntityDSG::~StaticEntityDSG()
{
BEGIN_PROFILE( "StaticEntityDSG Destroy" );
   if(mpDrawstuff != NULL)
   {
      mpDrawstuff->Release();
   }
END_PROFILE( "StaticEntityDSG Destroy" );
}
//========================================================================
// StaticEntityDSG::SetRank
//========================================================================
//
// Description: Sets rank, defaults to default SetRank for normal geo
//              however, shadows always get drawn first in the translucent pass
//
// Parameters:  rmt::Vector& irRefPosn, rmt::Vector& mViewVector.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void StaticEntityDSG::SetRank(rmt::Vector& irRefPosn, rmt::Vector& mViewVector)
{
    if ( ( mIsGeo & IS_SHADOW ) == false )
    {
        IEntityDSG::SetRank( irRefPosn, mViewVector );
    }
    else
    {
        mRank = FLT_MAX;
    }
}

//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void StaticEntityDSG::SetGeometry(tGeometry* ipGeo)
{
   if(mpDrawstuff != NULL)
   {
      mpDrawstuff->Release();
   }
   
   mpDrawstuff = ipGeo;

   if(mpDrawstuff != NULL)
   {
      mpDrawstuff->AddRef();
   }
   mIsGeo = GEO;

   if(ipGeo->CastsShadow())
   {
       mIsGeo = mIsGeo | IS_SHADOW;
   }
   
//   mShaderUID = ipGeo->GetShader(0)->GetUID();
   ipGeo->ProcessShaders(*this);

   SetInternalState();
}
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
tGeometry* StaticEntityDSG::mpGeo()
{
   return (tGeometry*)mpDrawstuff;
}
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void StaticEntityDSG::SetDrawable(tDrawable* ipDraw)
{
   if(mpDrawstuff != NULL)
   {
      mpDrawstuff->Release();
   }
   
   mpDrawstuff = ipDraw;

   if(mpDrawstuff != NULL)
   {
      mpDrawstuff->AddRef();
   }

   mIsGeo = NOT_GEO;

   SetInternalState();
}
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
tDrawable* StaticEntityDSG::mpDraw()
{
    return mpDrawstuff;

}

///////////////////////////////////////////////////////////////////////
// Drawable
///////////////////////////////////////////////////////////////////////
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
static int sStaticEntityDisplayCount = 0;
static int sZone3LogCount = 0;
static int sZone1LogCount = 0;
static int sTerrainLogCount = 0;

// Check if entity name suggests terrain/road
static bool IsTerrainName(const char* name)
{
    if(!name) return false;
    // Check for common terrain/road naming patterns
    return (strstr(name, "road") || strstr(name, "Road") || strstr(name, "ROAD") ||
            strstr(name, "terra") || strstr(name, "Terra") || strstr(name, "TERRA") ||
            strstr(name, "ground") || strstr(name, "Ground") || strstr(name, "GROUND") ||
            strstr(name, "street") || strstr(name, "Street") || strstr(name, "STREET") ||
            strstr(name, "sidewalk") || strstr(name, "Sidewalk") ||
            strstr(name, "path") || strstr(name, "Path"));
}

// Material diagnostic logging for Zone comparison
static void LogMaterialDiag(const char* zone, int logNum, const char* name, tGeometry* geo, float x, float z)
{
    if(!geo) {
        SRR2::Diagnostics::Errorf(
            SRR2::Diagnostics::RENDER,
            "[MAT_DIAG_%s] #%d name='%s' pos=(%.1f,%.1f) error=geo_null",
            zone,
            logNum,
            name ? name : "NULL",
            x,
            z );
        return;
    }
    
    int numShaders = geo->GetNumShader();
    int numPrimGroups = geo->GetNumPrimGroup();
    
    SRR2::Diagnostics::Tracef(
        SRR2::Diagnostics::RENDER,
        "[MAT_DIAG_%s] #%d name='%s' pos=(%.1f,%.1f) shaders=%d primGroups=%d",
        zone,
        logNum,
        name ? name : "NULL",
        x,
        z,
        numShaders,
        numPrimGroups );
    
    for(int i = 0; i < numShaders && i < 4; i++) // Log up to 4 shaders
    {
        tShader* shader = geo->GetShader(i);
        if(!shader) {
            SRR2::Diagnostics::Anomalyf(
                SRR2::Diagnostics::RENDER,
                "[MAT_DIAG_%s] shader[%d]=NULL",
                zone,
                i );
            continue;
        }
        
        pddiShader* pddi = shader->GetShader();
        const char* shaderType = pddi ? pddi->GetType() : "NULL";
        bool translucent = shader->mTranslucent;
        
        SRR2::Diagnostics::Tracef(
            SRR2::Diagnostics::RENDER,
            "[MAT_DIAG_%s] shader[%d] type='%s' translucent=%d pddi=%p",
            zone,
            i,
            shaderType ? shaderType : "NULL",
            translucent ? 1 : 0,
            (void*)pddi );
    }
}
#endif

void StaticEntityDSG::Display()
{
#ifdef PROFILER_ENABLED
    char profileName[] = "  StaticEntityDSG Display";
#endif
    if(IS_DRAW_LONG) return;
    DSG_BEGIN_PROFILE(profileName)

#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
    sStaticEntityDisplayCount++;
    
    // Get position for zone detection
    rmt::Box3D bbox;
    GetBoundingBox(&bbox);
    float centerX = (bbox.low.x + bbox.high.x) * 0.5f;
    float centerZ = (bbox.low.z + bbox.high.z) * 0.5f;
    
    // Zone 3 (Lisa's School) - detailed material logging
    bool isZone3Entity = IsInZone3(centerX, centerZ) && (mIsGeo & GEO);
    if(isZone3Entity)
    {
        sZone3LogCount++;
        // Log first 30 Zone 3 entities + every 100th
        if(sZone3LogCount <= 30 || (sZone3LogCount % 100 == 0))
        {
            const char* name = GetName() ? GetName() : "NULL";
            LogMaterialDiag("Z3", sZone3LogCount, name, (tGeometry*)mpDrawstuff, centerX, centerZ);
        }
    }
    
    // Zone 1 (working area) - comparison logging
    if(IsInZone1(centerX, centerZ) && (mIsGeo & GEO))
    {
        sZone1LogCount++;
        // Log first 10 Zone 1 entities for comparison
        if(sZone1LogCount <= 10)
        {
            const char* name = GetName() ? GetName() : "NULL";
            LogMaterialDiag("Z1", sZone1LogCount, name, (tGeometry*)mpDrawstuff, centerX, centerZ);
        }
    }
    
    // TERRAIN DIAGNOSTIC: Log any entity with terrain/road-like name
    const char* entityName = GetName();
    if(IsTerrainName(entityName) && (mIsGeo & GEO))
    {
        sTerrainLogCount++;
        if(sTerrainLogCount <= 50 || (sTerrainLogCount % 500 == 0))
        {
            SRR2::Diagnostics::Tracef(
                SRR2::Diagnostics::RENDER,
                "[TERRAIN_DIAG] #%d name='%s' pos=(%.1f,%.1f) inZ3=%d",
                sTerrainLogCount,
                entityName ? entityName : "NULL",
                centerX,
                centerZ,
                isZone3Entity ? 1 : 0 );
        }
    }
#endif

    if(mIsGeo & IS_SHADOW)
    {
        p3d::pddi->SetZWrite(false);
        mpDrawstuff->Display();
        p3d::pddi->SetZWrite(true);
    }
    else
    {
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        // CRITICAL: Log GL state RIGHT BEFORE Zone 3 entity draws
        if(isZone3Entity && (sZone3LogCount <= 10 || (sZone3LogCount % 500 == 0)))
        {
            GLboolean depthTest, depthMask, blendEnabled, cullFaceEnabled;
            GLint depthFunc, cullFaceMode;
            glGetBooleanv(GL_DEPTH_TEST, &depthTest);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
            blendEnabled = glIsEnabled(GL_BLEND);
            cullFaceEnabled = glIsEnabled(GL_CULL_FACE);
            glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
            glGetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode);
            SRR2::Diagnostics::Tracef(
                SRR2::Diagnostics::RENDER,
                "[Z3_DRAW_STATE] #%d zTest=%d zWrite=%d blend=%d zFunc=0x%x cull=%d cullMode=0x%x pos=(%.1f,%.1f)",
                sZone3LogCount,
                depthTest ? 1 : 0,
                depthMask ? 1 : 0,
                blendEnabled ? 1 : 0,
                depthFunc,
                cullFaceEnabled ? 1 : 0,
                cullFaceMode,
                centerX,
                centerZ );
        }
        
        // DEBUG: Set flag to force Zone 3 geometry to render bright red
        // Enable this to test if geometry is being rasterized correctly
        // DISABLED: Diagnostic showed buildings are StaticEntityDSG but roads/terrain are NOT
        // g_debugForceZone3Red = isZone3Entity;
#endif
        mpDrawstuff->Display();
#if defined( RAD_TVOS ) && defined( RAD_TVOS_RENDER_DIAGNOSTICS )
        // g_debugForceZone3Red = false;
#endif
    }
    DSG_END_PROFILE(profileName)
}

#ifndef RAD_RELEASE
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void StaticEntityDSG::DisplayBoundingBox(tColour colour)
{
#ifndef RAD_RELEASE
   mpDrawstuff->DisplayBoundingBox(colour);
#endif
}
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void StaticEntityDSG::DisplayBoundingSphere(tColour colour)
{
   mpDrawstuff->DisplayBoundingSphere(colour);
}
#endif

//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void StaticEntityDSG::GetBoundingBox(rmt::Box3D* box)
{
   mpDrawstuff->GetBoundingBox(box);
}
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void StaticEntityDSG::GetBoundingSphere(rmt::Sphere* sphere)
{
   mpDrawstuff->GetBoundingSphere(sphere);
}
///////////////////////////////////////////////////////////////////////
// IEntityDSG
///////////////////////////////////////////////////////////////////////
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
rmt::Vector* StaticEntityDSG::pPosition()
{
   return &mPosn;
}
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
const rmt::Vector& StaticEntityDSG::rPosition()
{
   return mPosn;
}
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void StaticEntityDSG::GetPosition( rmt::Vector* ipPosn )
{
   *ipPosn = mPosn;
}
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void StaticEntityDSG::RenderUpdate()
{
   //Do Nothing
}
//************************************************************************
//
// Protected Member Functions : StaticEntityDSG 
//
//************************************************************************
//========================================================================
// StaticEntityDSG::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void StaticEntityDSG::SetInternalState()
{
   rmt::Sphere sphere;

   mpDrawstuff->GetBoundingSphere(&sphere);
   mPosn = sphere.centre;
}
//************************************************************************
//
// Private Member Functions : StaticEntityDSG 
//
//************************************************************************
