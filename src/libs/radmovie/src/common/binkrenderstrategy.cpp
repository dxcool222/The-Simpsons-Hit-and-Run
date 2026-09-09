//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


//=============================================================================
//
// File:        binkrenderstrategy.cpp
// Subsystem:   Foundation Technologies - Movie
//
// Description:	Bink render stratetegy for all platforms (not ps2)
//
// Date:        October 31, 2002 RWS
//
//=============================================================================

//=============================================================================
// Include Files
//=============================================================================

#include "pch.hpp"
#include <radoptions.hpp>
#include <p3d/shader.hpp>
#include <p3d/utility.hpp>

#include <radprofiler.hpp>
#include "binkrenderstrategy.hpp"
#include <diagnostics/tvosdiagnostics.h>

#ifdef RAD_TVOS
#if defined(RAD_MACOS)
#include <pddi/gles/gl.hpp>
#else
#include <OpenGLES/ES2/gl.h>
#endif
#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif
static unsigned int s_RenderStrategyDrawCount = 0;
static bool s_VideoRenderDiagnosticsDone = false;

// ============================================================================
// VIDEO DIAGNOSTIC: Check if texture data is all black (zeros)
// ============================================================================
static bool VideoCheckTextureData( tTexture* tex, unsigned int tileIdx, unsigned int videoW, unsigned int videoH )
{
    if ( !tex ) {
        SRR2::Diagnostics::Errorf( SRR2::Diagnostics::VIDEO, "[VIDEO_DIAG] ERROR: Tile %u texture is NULL!", tileIdx );
        return false;
    }
    
    pddiTexture* pddiTex = tex->GetTexture();
    if ( !pddiTex ) {
        SRR2::Diagnostics::Errorf( SRR2::Diagnostics::VIDEO, "[VIDEO_DIAG] ERROR: Tile %u pddiTexture is NULL!", tileIdx );
        return false;
    }
    
    pddiLockInfo* lockInfo = pddiTex->Lock( 0, NULL );
    if ( !lockInfo || !lockInfo->bits ) {
        SRR2::Diagnostics::Errorf( SRR2::Diagnostics::VIDEO, "[VIDEO_DIAG] ERROR: Tile %u cannot lock texture!", tileIdx );
        return false;
    }
    
    int width = pddiTex->GetWidth();
    int height = pddiTex->GetHeight();
    int pitch = lockInfo->pitch;
    
    // CRITICAL FIX: lock.bits points to LAST row for negative pitch textures
    // We need to calculate the FIRST row (bits[0]) where video data actually starts
    unsigned char* firstRowPtr;
    int positivePitch;
    if ( pitch < 0 ) {
        positivePitch = -pitch;
        // lock.bits points to last row, calculate back to first row
        firstRowPtr = (unsigned char*)lockInfo->bits - (height - 1) * positivePitch;
    } else {
        positivePitch = pitch;
        firstRowPtr = (unsigned char*)lockInfo->bits;
    }
    
    SRR2::Diagnostics::Tracef( SRR2::Diagnostics::VIDEO,
             "[VIDEO_DIAG] Tile %u: texSize=%dx%d videoSize=%ux%u pitch=%d",
             tileIdx, width, height, videoW, videoH, pitch );
    SRR2::Diagnostics::Tracef( SRR2::Diagnostics::VIDEO,
             "[VIDEO_DIAG] Tile %u: lockBits=%p firstRow=%p",
             tileIdx, lockInfo->bits, (void*)firstRowPtr );
    
    // Sample from the ACTUAL video data area (first rows, not last rows)
    bool allBlack = true;
    unsigned int nonZeroCount = 0;
    unsigned int sampleR = 0, sampleG = 0, sampleB = 0, sampleA = 0;
    
    // Sample 8x8 grid from the video area
    for ( unsigned int y = 0; y < 8 && y < videoH; y++ ) {
        for ( unsigned int x = 0; x < 8 && x < videoW; x++ ) {
            unsigned char* pixel = firstRowPtr + y * positivePitch + x * 4;
            // BGRA format
            if ( pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0 ) {
                allBlack = false;
                nonZeroCount++;
                if ( nonZeroCount == 1 ) {
                    sampleB = pixel[0]; sampleG = pixel[1]; sampleR = pixel[2]; sampleA = pixel[3];
                }
            }
        }
    }
    
    // Also sample from center of video
    if ( videoW > 100 && videoH > 100 ) {
        unsigned int cx = videoW / 2;
        unsigned int cy = videoH / 2;
        unsigned char* centerPixel = firstRowPtr + cy * positivePitch + cx * 4;
        SRR2::Diagnostics::Tracef( SRR2::Diagnostics::VIDEO,
                 "[VIDEO_DIAG] Tile %u CENTER pixel (%u,%u): BGRA=(%u,%u,%u,%u)",
                 tileIdx, cx, cy, centerPixel[0], centerPixel[1], centerPixel[2], centerPixel[3] );
    }
    
    pddiTex->Unlock( 0 );
    
    if ( allBlack )
    {
        SRR2::Diagnostics::Anomalyf( SRR2::Diagnostics::VIDEO,
             "[VIDEO_BLACK_FRAME] tile=%u nonZero=%u sample=RGBA(%u,%u,%u,%u)",
             tileIdx, nonZeroCount, sampleR, sampleG, sampleB, sampleA );
    }
    SRR2::Diagnostics::Tracef( SRR2::Diagnostics::VIDEO,
             "[VIDEO_DIAG] Tile %u: allBlack=%s nonZero=%u sample=RGBA(%u,%u,%u,%u)",
             tileIdx, allBlack ? "YES_BAD" : "NO_GOOD", nonZeroCount,
             sampleR, sampleG, sampleB, sampleA );
    
    return !allBlack;
}
#endif

extern bool inLoop;

//=============================================================================
// radMovieRenderStrategyBink::radMovieRenderStrategyBink
//=============================================================================

radMovieRenderStrategyBink::radMovieRenderStrategyBink( void )
    :
    radRefCount( 0 ),
    m_pShader( NULL ),
    m_MoviePosX( 0 ),
    m_MoviePosY( 0 ),
    m_MovieWidth( 0 ),
    m_MovieHeight( 0 ),
    m_DisplayMultiplier( 1.0f ),
    m_NumTiles( 0 ),
    m_CurrentDestIndex( 0 ),
    m_DestLocked( false )
{
    ::memset( m_pTile, 0, sizeof( m_pTile ) );

    // The shader is associated with the render strategy for both life times

    m_pShader = new tShader( );
    rAssert( m_pShader != NULL );
    m_pShader->AddRef();
}

//=============================================================================
// radMovieRenderStrategyBink::~radMovieRenderStrategyBink
//=============================================================================

radMovieRenderStrategyBink::~radMovieRenderStrategyBink( void )
{
    if( m_pShader != NULL )
    {
        m_pShader->Release( );
        m_pShader = NULL;
    }

    for( unsigned int i = 0; i < RMV_MAX_NUM_TILES; i++ )
    {
        if( m_pTile[ i ].m_pTexture != NULL )
        {
            m_pTile[ i ].m_pTexture->Release( );
            m_pTile[ i ].m_pTexture = NULL;
        }
    }
}

//=============================================================================
// radMovieRenderStrategyBink::ResetDestinations
//=============================================================================

void radMovieRenderStrategyBink::ResetDestinations( void )
{
    rAssert( m_DestLocked != true );
    m_CurrentDestIndex = 0;
}

//=============================================================================
// radMovieRenderStrategyBink::LockNextDestination
//=============================================================================

unsigned int radMovieRenderStrategyBink::LockNextDestination( IRadMovieRenderStrategy::LockedDestination * pLockedDestination )
{
    rAssert( m_DestLocked == false );

    if( m_CurrentDestIndex < m_NumTiles )
    {
        rAssert( m_pTile[ m_CurrentDestIndex ].m_pTexture != NULL );

        m_DestLocked = true;        

        pddiLockInfo * pPddiLockInfo = m_pTile[ m_CurrentDestIndex ].m_pTexture->Lock( 0, 0 );

        pLockedDestination->m_pDest = pPddiLockInfo->bits;
        pLockedDestination->m_DestPitch = pPddiLockInfo->pitch;
        pLockedDestination->m_DestPosX = 0;
        pLockedDestination->m_DestPosY = 0;
        pLockedDestination->m_SrcPosX = m_pTile[ m_CurrentDestIndex ].m_PosX;
        pLockedDestination->m_SrcPosY = m_pTile[ m_CurrentDestIndex ].m_PosY;
        pLockedDestination->m_Width = m_pTile[ m_CurrentDestIndex ].m_Width;
        pLockedDestination->m_Height = m_pTile[ m_CurrentDestIndex ].m_Height;

#ifdef RAD_MOVIEPLAYER_USE_BINK
        if( pLockedDestination->m_SrcPosY > 0 )
        {
            pLockedDestination->m_SrcPosY -= 1;
        }
#endif

        return m_NumTiles - m_CurrentDestIndex;
    }
    else
    {
        return 0;
    }
}

//=============================================================================
// radMovieRenderStrategyBink::SetParameters
//=============================================================================

void radMovieRenderStrategyBink::UnlockDestination( void )
{
    rAssert( m_DestLocked == true );

    m_pTile[ m_CurrentDestIndex ].m_pTexture->Unlock( 0 );
    m_CurrentDestIndex++;
    m_DestLocked = false;
}

//=============================================================================
// radMovieRenderStrategyBink::SetParameters
//=============================================================================

void radMovieRenderStrategyBink::ChangeParameters( unsigned int width, unsigned int height )
{
    rAssert( width > 0 && height > 0 );
    rAssert( width <= ( unsigned int ) p3d::display->GetWidth( ) );
    rAssert( height <= ( unsigned int ) p3d::display->GetHeight( ) );

    //
    // Only allocate things if the width or height has changed
    //

    if( width != m_MovieWidth || height != m_MovieHeight )
    {
        // Remember the new dimensions

        m_MovieWidth = width;
        m_MovieHeight = height;

        // Assume that we'll fit the screen
        if ( p3d::display->GetWidth( ) >= p3d::display->GetHeight( ) )
        {
            m_DisplayMultiplier = (float) p3d::display->GetHeight( ) / ( float ) m_MovieHeight;
        }
        else
        {
            m_DisplayMultiplier = (float) p3d::display->GetWidth( ) / ( float ) m_MovieWidth;
        }

        m_MoviePosX = (int) ( ( p3d::display->GetWidth( ) - m_MovieWidth * m_DisplayMultiplier ) / 2.0f );
        m_MoviePosY = (int) ( ( p3d::display->GetHeight( ) - m_MovieHeight * m_DisplayMultiplier ) / 2.0f );

        // How many tiles will this movie require?
        // Use ceil division so that an exact multiple of RMV_TEXTURE_MAX_TEX_DIM
        // doesn't incorrectly allocate an extra tile.

        unsigned int horizontalTiles = ( width + RMV_TEXTURE_MAX_TEX_DIM - 1 ) / RMV_TEXTURE_MAX_TEX_DIM;
        unsigned int verticalTiles = ( height + RMV_TEXTURE_MAX_TEX_DIM - 1 ) / RMV_TEXTURE_MAX_TEX_DIM;
        m_NumTiles = horizontalTiles * verticalTiles;
        rAssert( m_NumTiles <= RMV_MAX_NUM_TILES );

        // Create the required number of textures and fill in tile info

        unsigned int tileIndex = 0;

        for( unsigned int y = 0; y < verticalTiles; y++ )
        {
            for( unsigned int x = 0; x < horizontalTiles; x++ )
            {
                const unsigned int tilePosX = x * RMV_TEXTURE_MAX_TEX_DIM;
                const unsigned int tilePosY = y * RMV_TEXTURE_MAX_TEX_DIM;
                unsigned int tileWidth = RMV_TEXTURE_MAX_TEX_DIM;
                unsigned int tileHeight = RMV_TEXTURE_MAX_TEX_DIM;

                if ( tilePosX + tileWidth > m_MovieWidth )
                {
                    tileWidth = m_MovieWidth - tilePosX;
                }
                if ( tilePosY + tileHeight > m_MovieHeight )
                {
                    tileHeight = m_MovieHeight - tilePosY;
                }

                if( m_pTile[ tileIndex ].m_pTexture != NULL )
                {
                    m_pTile[ tileIndex ].m_pTexture->Release( );
                    m_pTile[ tileIndex ].m_pTexture = NULL;
                }

                m_pTile[ tileIndex ].m_pTexture = new tTexture( );
                rAssert( m_pTile[ tileIndex ].m_pTexture != NULL );
                m_pTile[ tileIndex ].m_pTexture->AddRef( );

                //
                // Create texture (platform dependent)
                //
                
                #if RAD_VITAGL
                bool wasTextureCreated = m_pTile[ tileIndex ].m_pTexture->Create( m_MovieWidth, m_MovieHeight, RMV_TEXTURE_BITDEPTH, 0, 0, PDDI_TEXTYPE_YUV );
                #elif defined( RAD_TVOS )
                bool wasTextureCreated = m_pTile[ tileIndex ].m_pTexture->Create( tileWidth, tileHeight, RMV_TEXTURE_BITDEPTH, 8, 0, PDDI_TEXTYPE_RGB );
                #elif defined( RAD_WIN32 )
                bool wasTextureCreated = m_pTile[ tileIndex ].m_pTexture->Create( RMV_TEXTURE_MAX_TEX_DIM, RMV_TEXTURE_MAX_TEX_DIM, RMV_TEXTURE_BITDEPTH, 8, 0, PDDI_TEXTYPE_RGB );
                #elif RAD_XBOX
                bool wasTextureCreated = m_pTile[ tileIndex ].m_pTexture->Create( m_MovieWidth, m_MovieHeight, RMV_TEXTURE_BITDEPTH, 0, 0, PDDI_TEXTYPE_LINEAR );
                #elif RAD_GAMECUBE
                bool wasTextureCreated = m_pTile[ tileIndex ].m_pTexture->Create( m_MovieWidth, m_MovieHeight, RMV_TEXTURE_BITDEPTH, 0, 0, PDDI_TEXTYPE_GC_32BIT );
                #endif 

                rAssert( wasTextureCreated == true );
                
                #ifdef RAD_TVOS
                // Mark as video texture - requires positive pitch for sws_scale output
                m_pTile[ tileIndex ].m_pTexture->GetTexture()->SetVideoTexture( true );
                #endif

                m_pTile[ tileIndex ].m_PosX = tilePosX;
                m_pTile[ tileIndex ].m_PosY = tilePosY;
                m_pTile[ tileIndex ].m_Width = tileWidth;
                m_pTile[ tileIndex ].m_Height = tileHeight;

                tileIndex++;
            }
        }
    }
}

//=============================================================================
// radMovieRenderStrategyBink::Render
//=============================================================================

bool radMovieRenderStrategyBink::Render( void )
{
#ifdef RAD_TVOS
    s_RenderStrategyDrawCount++;
    
    // ========================================================================
    // VIDEO DIAGNOSTIC BLOCK - Runs ONCE to identify the problem
    // ========================================================================
    if ( !s_VideoRenderDiagnosticsDone && s_RenderStrategyDrawCount >= 3 ) {
        s_VideoRenderDiagnosticsDone = true;
        
        SRR2::Diagnostics::Checkpointf( SRR2::Diagnostics::VIDEO,
            "[VIDEO_DIAG] diagnostic_report frame=%u", s_RenderStrategyDrawCount );
        
        // Check 1: Basic parameters
        SRR2::Diagnostics::Tracef( SRR2::Diagnostics::VIDEO,
                 "[VIDEO_DIAG] NumTiles=%u MovieSize=%ux%u DisplayMult=%.2f Pos=(%d,%d)",
                 m_NumTiles, m_MovieWidth, m_MovieHeight, 
                 m_DisplayMultiplier, m_MoviePosX, m_MoviePosY );
        SRR2::Diagnostics::Tracef( SRR2::Diagnostics::VIDEO,
                 "[VIDEO_DIAG] Display=%dx%d",
                 p3d::display->GetWidth(), p3d::display->GetHeight() );
        
        // Check 2: Shader validity
        if ( m_pShader == NULL ) {
            SRR2::Diagnostics::Errorf( SRR2::Diagnostics::VIDEO, "[VIDEO_DIAG] CRITICAL ERROR: m_pShader is NULL!" );
        } else {
            pddiShader* pddiShd = m_pShader->GetShader();
            SRR2::Diagnostics::Tracef( SRR2::Diagnostics::VIDEO,
                     "[VIDEO_DIAG] Shader: tShader=%p pddiShader=%p",
                     (void*)m_pShader, (void*)pddiShd );
        }
        
        // Check 3: Tile textures - ARE THEY BLACK?
        for ( unsigned int t = 0; t < m_NumTiles; t++ ) {
            tTexture* tex = m_pTile[t].m_pTexture;
            SRR2::Diagnostics::Tracef( SRR2::Diagnostics::VIDEO,
                     "[VIDEO_DIAG] Tile %u: tTexture=%p pos=(%u,%u) size=(%u,%u)",
                     t, (void*)tex, m_pTile[t].m_PosX, m_pTile[t].m_PosY,
                     m_pTile[t].m_Width, m_pTile[t].m_Height );
            VideoCheckTextureData( tex, t, m_pTile[t].m_Width, m_pTile[t].m_Height );
        }
        
        // Check 4: OpenGL state
        GLint currentFBO = 0, currentProg = 0, boundTex = 0, activeTex = 0;
        glGetIntegerv( GL_FRAMEBUFFER_BINDING, &currentFBO );
        glGetIntegerv( GL_CURRENT_PROGRAM, &currentProg );
        glGetIntegerv( GL_TEXTURE_BINDING_2D, &boundTex );
        glGetIntegerv( GL_ACTIVE_TEXTURE, &activeTex );
        SRR2::Diagnostics::Tracef( SRR2::Diagnostics::VIDEO,
                 "[VIDEO_DIAG] GL State: FBO=%d Program=%d BoundTex=%d ActiveTexUnit=0x%x",
                 currentFBO, currentProg, boundTex, activeTex );
        
        // Check 5: FBO status
        GLenum fboStatus = glCheckFramebufferStatus( GL_FRAMEBUFFER );
        if ( fboStatus != GL_FRAMEBUFFER_COMPLETE )
        {
            SRR2::Diagnostics::Errorf( SRR2::Diagnostics::VIDEO,
                "[VIDEO_FBO_INCOMPLETE] status=0x%x", fboStatus );
        }
        SRR2::Diagnostics::Tracef( SRR2::Diagnostics::VIDEO,
                 "[VIDEO_DIAG] FBO Status: 0x%x (%s)",
                 fboStatus, fboStatus == GL_FRAMEBUFFER_COMPLETE ? "COMPLETE" : "INCOMPLETE!" );
    }
    
    // Clear any GL errors before rendering
    while( glGetError() != GL_NO_ERROR );
#endif

    // some camera settings
    p3d::pddi->PushState(PDDI_STATE_ALL);
    p3d::pddi->PushIdentityMatrix(PDDI_MATRIX_MODELVIEW);
    #if defined(RAD_WIN32) || defined(RAD_TVOS)
    p3d::pddi->SetProjectionMode(PDDI_PROJECTION_DEVICE);
    #else
    p3d::pddi->SetProjectionMode(PDDI_PROJECTION_ORTHOGRAPHIC); //PDDI_PROJECTION_DEVICE
    #endif
    p3d::pddi->SetCullMode(PDDI_CULL_NONE);
    

    // Note: A faster render strategy is possible...
    //
    // Rendering could be made faster by ditching the use of textures and 
    // copying the pixels onto the back buffer directly.  I didn't do it this
    // way because it requires Pure3d to create the d3d device in a different mode
    // that allows the back buffer to be locked.  
    //
    // At this time, nobody requires movies faster than 30fps so I didn't want to
    // run into any weird problems.  I'm leaving this note for future generations.

    rAssert( m_pShader != NULL );

    // Render each tile 

    for( unsigned int tile = 0; tile < m_NumTiles; tile++ )
    {
        rAssert( m_pTile[ tile ].m_pTexture != NULL );

        // Set up position info of the tile
        
        #if defined( RAD_WIN32 ) || defined( RAD_TVOS )

        float u = 0.0f;
        float du = m_pTile[ tile ].m_Width / ( float ) m_pTile[ tile ].m_pTexture->GetWidth( );
        
        #if defined( RAD_VITAGL ) || defined( RAD_TVOS )

        // VitaGL and tvOS: video data written top-to-bottom, use v=0 at top
        float v = 0.0f;
        float dv = m_pTile[ tile ].m_Height / ( float ) m_pTile[ tile ].m_pTexture->GetHeight( );

        #else

        // Win32: texture data is bottom-to-top, flip UVs to render correctly
        float v = 1.0f;
        float dv = - ( float ) ( m_pTile[ tile ].m_Height / ( float ) m_pTile[ tile ].m_pTexture->GetHeight( ) );

        #endif

        #elif RAD_XBOX

        float u = 0.0f;
        float v = ( float ) m_pTile[ tile ].m_Height;
        float du = ( float ) m_pTile[ tile ].m_Width;
        float dv = - ( float ) m_pTile[ tile ].m_Height;

        #elif RAD_GAMECUBE

        float u = 0.0f;
        float v = 0.0f;
        float du = m_pTile[ tile ].m_Width / ( float ) m_pTile[ tile ].m_pTexture->GetWidth( );
        float dv = m_pTile[ tile ].m_Height / ( float ) m_pTile[ tile ].m_pTexture->GetHeight( );

        #endif

        #if defined( RAD_WIN32 ) || defined( RAD_TVOS )

        float x = ( float ) m_DisplayMultiplier * m_pTile[ tile ].m_PosX + m_MoviePosX;
        float y = ( float ) m_DisplayMultiplier * m_pTile[ tile ].m_PosY + m_MoviePosY;
        float z = 0.0f;     // Use 0.0f as validated by diagnostic tests
        float dx = ( float ) m_DisplayMultiplier * m_pTile[ tile ].m_Width;
        float dy = ( float ) m_DisplayMultiplier * m_pTile[ tile ].m_Height;

        #else

        float x = -0.5f * m_DisplayMultiplier;
        float y = 0.5f / (4.0f / 3.0f);
        float z = 5.0f;
        float dx = x * -2.0f;
        float dy = y * -2.0f;

        #endif

        // Associate the tile's texture with our shader

        m_pShader->SetTexture( PDDI_SP_BASETEX, m_pTile[ tile ].m_pTexture );
        m_pShader->SetInt( PDDI_SP_BLENDMODE, PDDI_BLEND_NONE );
        if ( m_DisplayMultiplier != 1.0f )
        {
            m_pShader->SetInt( PDDI_SP_FILTER, PDDI_FILTER_BILINEAR );
        }

        // Stream the texture to the screen onto a couple of triangles

        pddiPrimStream* pStream = p3d::pddi->BeginPrims( m_pShader->GetShader( ), PDDI_PRIM_TRIANGLES, PDDI_V_CT, 6 );

        // bottom left

        pStream->Colour( tColour(255,255,255));
        pStream->UV( u, v + dv );
        pStream->Coord( x, y + dy, z );

        // bottom right

        pStream->Colour(tColour(255,255,255));  // Not sure if these need to happen every vertex
        pStream->UV( u + du, v + dv );
        pStream->Coord( x + dx, y + dy, z );

        // top right

        pStream->Colour(tColour(255,255,255));
        pStream->UV( u + du, v );
        pStream->Coord( x + dx, y, z );

        // bottom left

        pStream->Colour(tColour(255,255,255));
        pStream->UV( u, v + dv );
        pStream->Coord( x, y + dy, z );

        // top right

        pStream->Colour( tColour(255,255,255));
        pStream->UV( u + du, v );
        pStream->Coord( x + dx, y, z );

        // top left

        pStream->Colour( tColour(255,255,255));
        pStream->UV( u, v);
        pStream->Coord( x, y, z );

        // Done with this primgroup

        p3d::pddi->EndPrims( pStream );
    }

    p3d::pddi->PopMatrix(PDDI_MATRIX_MODELVIEW);
    p3d::pddi->PopState(PDDI_STATE_ALL);

    return true;
}

/*
bool radMovieRenderStrategyBink::Render( void )
{
    radProfilerBeginFrame( );
    radProfilerBeginProfile( "radMovieRenderStrategyBink::Render" );

    // Get the back buffer from pure3d

    IDirect3DSurface8 * pIDirect3DSurface8 = NULL;
    HRESULT hr = static_cast< d3dDisplay * >( p3d::display )->GetD3DDevice( )->GetBackBuffer( 0, D3DBACKBUFFER_TYPE_MONO, & pIDirect3DSurface8 );
    rAssert( SUCCEEDED( hr ) );

    // Find out about the back buffer

    D3DSURFACE_DESC desc;
    hr = pIDirect3DSurface8->GetDesc( & desc );
    rAssert( SUCCEEDED( hr ) );

    // Lock the back buffer

    D3DLOCKED_RECT rect;
    hr = pIDirect3DSurface8->LockRect( & rect, NULL, NULL );
    rAssert( SUCCEEDED( hr ) );

    // Fill the back buffer

    unsigned char * dest = ( unsigned char * ) rect.pBits;
    unsigned char * src = ( unsigned char * ) m_pDecodedVideoFrame;

    int height = desc.Height;
    int width  = desc.Width;

    if( height > m_Height )
    {
        height = m_Height;
    }

    if( width > m_Width )
    {
        width = m_Width;
    }

    for (int j = 0; j < height; j++)
    {
        memcpy( dest, src, width * 4 );
        dest += rect.Pitch;
        src += m_Width * 4;

    }

    // Unlock the back buffer

    hr = pIDirect3DSurface8->UnlockRect( );
    rAssert( SUCCEEDED( hr ) );
    pIDirect3DSurface8->Release( );
    pIDirect3DSurface8 = NULL;

    radProfilerEndProfile( "radMovieRenderStrategyBink::Render" );
    radProfilerEndFrame( );

    return true;
}
*/

//=============================================================================
// ::radMovieSimpleFullScreenRenderStrategyCreate
//=============================================================================

IRadMovieRenderStrategy * radMovieSimpleFullScreenRenderStrategyCreate( radMemoryAllocator allocator )
{
    return new( allocator )radMovieRenderStrategyBink( );
}
