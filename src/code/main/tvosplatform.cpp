//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//=============================================================================

#include <main/tvosplatform.h>

#ifdef RAD_TVOS
#include <input/tvos/tvos_controller.h>
#endif

#include <main/commandlineoptions.h>
#include <main/game.h>
#include <main/singletons.h>

#include <contexts/contextenum.h>

#include <gameflow/gameflow.h>

#include <memory/memoryutilities.h>
#include <memory/srrmemory.h>

#include <raddebug.hpp>
#include <radmemory.hpp>
#include <radthread.hpp>
#include <radplatform.hpp>
#include <radfile.hpp>
#include <radload/radload.hpp>
#include <radmemorymonitor.hpp>
#include <radmovie2.hpp>
#include <radtime.hpp>

#include <p3d/pure3d.hpp>
#include <p3d/context.hpp>
#include <p3d/file.hpp>
#include <p3d/platform.hpp>
#include <p3d/inventory.hpp>
#include <p3d/loadmanager.hpp>
#include <p3d/matrixstack.hpp>
#include <p3d/texturefont.hpp>
#include <p3d/unicode.hpp>
#include <p3d/png.hpp>
#include <p3d/texture.hpp>
#include <p3d/utility.hpp>

#include <render/RenderFlow/renderflow.h>
#include <render/RenderManager/RenderManager.h>
#include <render/Loaders/AllWrappers.h>
#include <render/Loaders/GeometryWrappedLoader.h>
#include <render/Loaders/StaticEntityLoader.h>
#include <render/Loaders/StaticPhysLoader.h>
#include <render/Loaders/TreeDSGLoader.h>
#include <render/Loaders/FenceLoader.h>
#include <render/Loaders/IntersectLoader.h>
#include <render/Loaders/AnimCollLoader.h>
#include <render/Loaders/AnimDSGLoader.h>
#include <render/Loaders/DynaPhysLoader.h>
#include <render/Loaders/InstStatPhysLoader.h>
#include <render/Loaders/InstStatEntityLoader.h>
#include <render/Loaders/WorldSphereLoader.h>
#include <render/Loaders/LensFlareLoader.h>
#include <render/Loaders/BillboardWrappedLoader.h>
#include <render/Loaders/InstParticleSystemLoader.h>
#include <render/Loaders/BreakableObjectLoader.h>
#include <render/Loaders/AnimDynaPhysLoader.h>

#include <loading/locatorloader.h>
#include <loading/cameradataloader.h>
#include <loading/roadloader.h>
#include <loading/intersectionloader.h>
#include <loading/pathloader.h>
#include <loading/roaddatasegmentloader.h>
#include <atc/atcloader.h>
#include <stateprop/statepropdata.hpp>

#include <sound/soundmanager.h>
#include <input/inputmanager.h>

#include <loading/p3dfilehandler.h>
#include <constants/srrchunks.h>

#include <simcommon/simutility.hpp>

TvosPlatform* TvosPlatform::spInstance = NULL;
SDL_Window* TvosPlatform::mWnd = NULL;

static const char ApplicationName[] = "The Simpsons: Hit & Run";

#define TVOS_SECTION "TVOS_SECTION"

//The Adlib font.  <sigh>
unsigned char gFont[] =
#include <font/defaultfont.h>

void LoadMemP3DFile( unsigned char* buffer, unsigned int size, tEntityStore* store )
{
    tFileMem* file = new tFileMem( buffer, size );
    file->AddRef();
    file->SetFilename( "memfile.p3d" );
    p3d::loadManager->GetP3DHandler()->Load( file, p3d::inventory );
    file->Release();
}

static void LogOutputFunction( void* /*userdata*/, int /*category*/, SDL_LogPriority /*priority*/, const char* message )
{
    printf( "%s\n", message );
    fflush( stdout );
}

TvosPlatform* TvosPlatform::CreateInstance()
{
    rAssert( spInstance == NULL );
    spInstance = new(GMA_PERSISTENT) TvosPlatform();
    rAssert( spInstance );
    return spInstance;
}

TvosPlatform* TvosPlatform::GetInstance()
{
    rAssert( spInstance != NULL );
    return spInstance;
}

void TvosPlatform::DestroyInstance()
{
    rAssert( spInstance != NULL );
    delete( GMA_PERSISTENT, spInstance );
    spInstance = NULL;
}

bool TvosPlatform::InitializeWindow()
{
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 2 );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
    SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 0 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, SDL_TRUE );

    int flags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN;

    mWnd = SDL_CreateWindow( ApplicationName, 0, 0, 1920, 1080, flags );
    rAssert( mWnd != NULL );

    {
        int winW = 0;
        int winH = 0;
        SDL_GetWindowSize( mWnd, &winW, &winH );

        int drawableW = 0;
        int drawableH = 0;
        SDL_GL_GetDrawableSize( mWnd, &drawableW, &drawableH );

        SDL_Log( "TVOS_GL Window created: wnd=%p window=%dx%d drawable=%dx%d", mWnd, winW, winH, drawableW, drawableH );
    }

    SDL_LogSetOutputFunction( LogOutputFunction, NULL );

    return true;
}

void TvosPlatform::InitializeFoundation()
{
    ::radMemorySetOutOfMemoryCallback( PrintOutOfMemoryMessage, NULL );

    if( CommandLineOptions::Get( CLO_MEMORY_MONITOR ) )
    {
        const int KB = 1024;
        ::radMemoryMonitorInitialize( 64 * KB, GMA_DEBUG );
    }

    HeapMgr()->PrepareHeapsStartup();

    HeapMgr()->PushHeap( GMA_PERSISTENT );

    ::radPlatformInitialize( mWnd );

    ::radTimeInitialize();

    ::radFileInitialize( 50, 32, GMA_PERSISTENT );
    ::radLoadInitialize();

    ::radSetDefaultDrive( "APP:" );

    bool appMounted = ::radDriveMount( "APP:", GMA_PERSISTENT );
    rAssert( !appMounted );

    bool prefMounted = ::radDriveMount( "PREF:", GMA_PERSISTENT );
    rAssert( !prefMounted );

    ::radMovieInitialize2( GMA_PERSISTENT );

    HeapMgr()->PopHeap( GMA_PERSISTENT );
}

void TvosPlatform::InitializeMemory()
{
    if( gMemorySystemInitialized )
        return;

    gMemorySystemInitialized = true;

    ::radThreadInitialize();
    ::radMemoryInitialize();
}

void TvosPlatform::ShutdownMemory()
{
    if( gMemorySystemInitialized )
    {
        gMemorySystemInitialized = false;
        ::radThreadTerminate();
    }
}

TvosPlatform::TvosPlatform() :
    mpPlatform( NULL ),
    mpContext( NULL )
{
    mPauseForError = false;
    mErrorState = NONE;
}

TvosPlatform::~TvosPlatform()
{
}

void TvosPlatform::InitializePlatform()
{
    HeapMgr()->PushHeap( GMA_PERSISTENT );

    InitializePure3D();

    DisplaySplashScreen( Error );

    InitializeFoundationDrive();

#ifdef RAD_TVOS
    // Initialize native GameController.framework backend BEFORE waiting for controller
    TvOSInput_Init();
#endif

    WaitForController();

    GetInputManager()->Init();

    HeapMgr()->PopHeap( GMA_PERSISTENT );
}

void TvosPlatform::ShutdownPlatform()
{
    ShutdownPure3D();
    ShutdownFoundation();
}

void TvosPlatform::LaunchDashboard()
{
    GetGameFlow()->SetContext( CONTEXT_EXIT );
}

void TvosPlatform::ResetMachine()
{
}

void TvosPlatform::DisplaySplashScreen( SplashScreen screenID, const char* overlayText, float fontScale, float textPosX, float textPosY, tColour textColour, int fadeFrames )
{
    (void)screenID;

    HeapMgr()->PushHeap( GMA_TEMP );

    p3d::inventory->PushSection();
    p3d::inventory->AddSection( TVOS_SECTION );
    p3d::inventory->SelectSection( TVOS_SECTION );

    P3D_UNICODE unicodeText[256];

    pddiProjectionMode pm = p3d::pddi->GetProjectionMode();
    p3d::pddi->SetProjectionMode( PDDI_PROJECTION_DEVICE );

    pddiCullMode cm = p3d::pddi->GetCullMode();
    p3d::pddi->SetCullMode( PDDI_CULL_NONE );

    tTextureFont* thisFont = NULL;

    LoadMemP3DFile( gFont, DEFAULTFONT_SIZE, p3d::inventory );

    thisFont = p3d::find<tTextureFont>( "adlibn_20" );
    rAssert( thisFont );

    thisFont->AddRef();

    if ( overlayText == NULL )
    {
        overlayText = "";
    }

    p3d::AsciiToUnicode( overlayText, unicodeText, 256 );
    thisFont->SetMissingLetter( p3d::ConvertCharToUnicode( 'j' ) );

    int a = 0;
    do
    {
        p3d::pddi->SetColourWrite( true, true, true, true );
        p3d::pddi->SetClearColour( pddiColour(0,0,0) );
        p3d::pddi->BeginFrame();
        p3d::pddi->Clear( PDDI_BUFFER_COLOUR );

        int bright = 255;
        if ( a < fadeFrames ) bright = (a * 255) / fadeFrames;
        if ( bright > 255 ) bright = 255;

        if ( overlayText != NULL )
        {
            tColour colour = textColour;
            colour.SetAlpha( bright );

            thisFont->SetColour( colour );

            p3d::pddi->SetProjectionMode( PDDI_PROJECTION_ORTHOGRAPHIC );
            p3d::stack->Push();
            p3d::stack->LoadIdentity();

            p3d::stack->Translate( textPosX, textPosY, 1.0f );
            float scaleSize = 1.0f / 480.0f;
            p3d::stack->Scale( scaleSize * fontScale, scaleSize * fontScale, 1.0f );

            if ( textPosX != 0.0f || textPosY != 0.0f )
            {
                thisFont->DisplayText( unicodeText );
            }
            else
            {
                thisFont->DisplayText( unicodeText, 3 );
            }

            p3d::stack->Pop();
        }

        p3d::pddi->EndFrame();
        p3d::context->SwapBuffers();

        ++a;

    } while ( a <= fadeFrames + 1 );

    p3d::pddi->SetCullMode( cm );
    p3d::pddi->SetProjectionMode( pm );

    thisFont->Release();

    p3d::inventory->RemoveSectionElements( TVOS_SECTION );
    p3d::inventory->DeleteSection( TVOS_SECTION );
    p3d::inventory->PopSection();

    HeapMgr()->PopHeap( GMA_TEMP );
}

void TvosPlatform::DisplaySplashScreen( const char* /*textureName*/, const char* /*overlayText*/, float /*fontScale*/, float /*textPosX*/, float /*textPosY*/, tColour /*textColour*/, int /*fadeFrames*/ )
{
}

void TvosPlatform::OnControllerError( const char* msg )
{
    DisplaySplashScreen( Error, msg, 0.7f, 0.0f, 0.0f, tColour(255, 255, 255), 0 );
    mErrorState = CTL_ERROR;
    mPauseForError = true;
}

bool TvosPlatform::OnDriveError( radFileError error, const char* /*pDriveName*/, void* /*pUserData*/ )
{
    switch( error )
    {
        case Success:
        {
            if ( mErrorState != NONE )
            {
                DisplaySplashScreen( FadeToBlack );
                mErrorState = NONE;
                mPauseForError = false;
            }
            return true;
        }
        default:
        {
            DisplaySplashScreen( Error, "File Error", 1.0f, 0.0f, 0.0f, tColour(255,255,255), 0 );
            mErrorState = P_ERROR;
            mPauseForError = true;
            return true;
        }
    }
}

void TvosPlatform::InitializeFoundationDrive()
{
    char defaultDrive[ radFileDrivenameMax + 1 ];
    ::radGetDefaultDrive( defaultDrive );

    ::radDriveOpenSync( &mpIRadDrive,
                        defaultDrive,
                        NormalPriority,
                        GMA_PERSISTENT );

    rAssert( mpIRadDrive != NULL );

    mpIRadDrive->RegisterErrorHandler( this, NULL );
}

void TvosPlatform::ShutdownFoundation()
{
    mpIRadDrive->Release();
    mpIRadDrive = NULL;

    ::radMovieTerminate2();

    ::radDriveUnmount( "PREF:" );
    ::radDriveUnmount( "APP:" );

    ::radLoadTerminate();
    ::radFileTerminate();

    if( CommandLineOptions::Get( CLO_MEMORY_MONITOR ) )
    {
        ::radMemoryMonitorTerminate();
    }

    ::radTimeTerminate();
    ::radPlatformTerminate();
}

void TvosPlatform::InitializePure3D()
{
    mpPlatform = tPlatform::Create( mWnd );
    rAssert( mpPlatform != NULL );

    InitializeContext();

    P3DASSERT( p3d::context );

    p3d::InstallDefaultLoaders();

    tP3DFileHandler* p3dHandler = p3d::loadManager->GetP3DHandler();
    rAssert( p3dHandler );

    GeometryWrappedLoader* pGWL = (GeometryWrappedLoader*)GetAllWrappers()->mpLoader( AllWrappers::msGeometry );
    pGWL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pGWL );

    StaticEntityLoader* pSEL = (StaticEntityLoader*)GetAllWrappers()->mpLoader( AllWrappers::msStaticEntity );
    pSEL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pSEL );

    StaticPhysLoader* pSPL = (StaticPhysLoader*)GetAllWrappers()->mpLoader( AllWrappers::msStaticPhys );
    pSPL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pSPL );

    TreeDSGLoader* pTDL = (TreeDSGLoader*)GetAllWrappers()->mpLoader( AllWrappers::msTreeDSG );
    pTDL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pTDL );

    FenceLoader* pFL = (FenceLoader*)GetAllWrappers()->mpLoader( AllWrappers::msFenceEntity );
    pFL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pFL );

    IntersectLoader* pIL = (IntersectLoader*)GetAllWrappers()->mpLoader( AllWrappers::msIntersectDSG );
    pIL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pIL );

    AnimCollLoader* pACL = (AnimCollLoader*)GetAllWrappers()->mpLoader( AllWrappers::msAnimCollEntity );
    pACL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pACL );

    AnimDSGLoader* pAnimDSGLoader = (AnimDSGLoader*)GetAllWrappers()->mpLoader( AllWrappers::msAnimEntity );
    pAnimDSGLoader->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pAnimDSGLoader );

    DynaPhysLoader* pDPL = (DynaPhysLoader*)GetAllWrappers()->mpLoader( AllWrappers::msDynaPhys );
    pDPL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pDPL );

    InstStatPhysLoader* pISPL = (InstStatPhysLoader*)GetAllWrappers()->mpLoader( AllWrappers::msInstStatPhys );
    pISPL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pISPL );

    InstStatEntityLoader* pISEL = (InstStatEntityLoader*)GetAllWrappers()->mpLoader( AllWrappers::msInstStatEntity );
    pISEL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pISEL );

    LocatorLoader* pLL = (LocatorLoader*)GetAllWrappers()->mpLoader( AllWrappers::msLocator );
    pLL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pLL );

    RoadLoader* pRL = (RoadLoader*)GetAllWrappers()->mpLoader( AllWrappers::msRoadSegment );
    pRL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pRL );

    PathLoader* pPL = (PathLoader*)GetAllWrappers()->mpLoader( AllWrappers::msPathSegment );
    pPL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pPL );

    WorldSphereLoader* pWSL = (WorldSphereLoader*)GetAllWrappers()->mpLoader( AllWrappers::msWorldSphere );
    pWSL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pWSL );

    LensFlareLoader* pLSL = (LensFlareLoader*)GetAllWrappers()->mpLoader( AllWrappers::msLensFlare );
    pLSL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pLSL );

    BillboardWrappedLoader* pBWL = (BillboardWrappedLoader*)GetAllWrappers()->mpLoader( AllWrappers::msBillboard );
    pBWL->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pBWL );

    InstParticleSystemLoader* pInstParticleSystemLoader = (InstParticleSystemLoader*) GetAllWrappers()->mpLoader( AllWrappers::msInstParticleSystem );
    pInstParticleSystemLoader->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pInstParticleSystemLoader );

    BreakableObjectLoader* pBreakableObjectLoader = (BreakableObjectLoader*) GetAllWrappers()->mpLoader( AllWrappers::msBreakableObject );
    pBreakableObjectLoader->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pBreakableObjectLoader );

    AnimDynaPhysLoader* pAnimDynaPhysLoader = (AnimDynaPhysLoader*) GetAllWrappers()->mpLoader( AllWrappers::msAnimDynaPhys );
    pAnimDynaPhysLoader->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pAnimDynaPhysLoader );

    AnimDynaPhysWrapperLoader* pAnimWrapperLoader = (AnimDynaPhysWrapperLoader*) GetAllWrappers()->mpLoader( AllWrappers::msAnimDynaPhysWrapper );
    pAnimWrapperLoader->SetRegdListener( GetRenderManager(), 0 );
    p3dHandler->AddHandler( pAnimWrapperLoader );

    p3dHandler->AddHandler( new(GMA_PERSISTENT) CameraDataLoader, SRR2::ChunkID::FOLLOWCAM );
    p3dHandler->AddHandler( new(GMA_PERSISTENT) CameraDataLoader, SRR2::ChunkID::WALKERCAM );
    p3dHandler->AddHandler( new(GMA_PERSISTENT) IntersectionLoader );
    p3dHandler->AddHandler( new(GMA_PERSISTENT) RoadDataSegmentLoader );
    p3dHandler->AddHandler( new(GMA_PERSISTENT) ATCLoader );
    p3dHandler->AddHandler( new(GMA_PERSISTENT) CStatePropDataLoader );

    sim::InstallSimLoaders();
}

void TvosPlatform::ShutdownPure3D()
{
    p3d::inventory->RemoveAllElements();
    p3d::inventory->DeleteAllSections();

    if( mpContext != NULL )
    {
        mpPlatform->DestroyContext( mpContext );
        mpContext = NULL;
    }

    if( mpPlatform != NULL )
    {
        tPlatform::Destroy( mpPlatform );
        mpPlatform = NULL;
    }
}

void TvosPlatform::InitializeContext()
{
    tContextInitData init;

    init.window = mWnd;
    init.displayMode = PDDI_DISPLAY_FULLSCREEN;

    mpContext = mpPlatform->CreateContext( &init );
    rAssert( mpContext != NULL );

    mpPlatform->SetActiveContext( mpContext );

    mpContext->SetClearColour( pddiColour(0,0,0) );
}

bool TvosPlatform::HasAnyController() const
{
#ifdef RAD_TVOS
    // Use native GameController.framework backend on tvOS
    int count = TvOSInput_GetPadCount();
    rDebugPrintf( "TvosPlatform::HasAnyController: TvOSInput_GetPadCount() = %d\n", count );
    return count > 0;
#else
    const int n = SDL_NumJoysticks();
    rDebugPrintf( "TvosPlatform::HasAnyController: SDL_NumJoysticks() = %d\n", n );
    for ( int i = 0; i < n; i++ )
    {
        const char* name = SDL_JoystickNameForIndex( i );
        bool isGameController = SDL_IsGameController( i ) == SDL_TRUE;
        rDebugPrintf( "  Joystick %d: '%s' isGameController=%d\n", i, name ? name : "(null)", isGameController ? 1 : 0 );
        
        if ( isGameController )
        {
            return true;
        }
    }
    return false;
#endif
}

void TvosPlatform::WaitForController()
{
#ifdef RAD_TVOS
    rDebugPrintf( "[TvosPlatform] WaitForController: Checking for controllers...\n" );
    
    // Pump controller events to ensure discovery has time to find controllers
    for ( int i = 0; i < 10; i++ )
    {
        TvOSInput_Pump();
        SDL_Event e;
        while ( SDL_PollEvent( &e ) ) { }
        SDL_Delay( 50 );
        
        int count = TvOSInput_GetPadCount();
        rDebugPrintf( "[TvosPlatform] WaitForController: pump %d, TvOSInput_GetPadCount()=%d\n", i, count );
        if ( count > 0 )
        {
            break;
        }
    }
#endif

    if ( HasAnyController() )
    {
        rDebugPrintf( "[TvosPlatform] WaitForController: Controller found, continuing.\n" );
        ClearControllerError();
        return;
    }

    rDebugPrintf( "[TvosPlatform] WaitForController: No controller found, showing error.\n" );
    OnControllerError( "Controller Required" );

    while ( !HasAnyController() )
    {
#ifdef RAD_TVOS
        TvOSInput_Pump();
#endif
        SDL_Event e;
        while ( SDL_PollEvent( &e ) )
        {
        }
        SDL_Delay( 16 );
    }

    rDebugPrintf( "[TvosPlatform] WaitForController: Controller connected, clearing error.\n" );
    ClearControllerError();
}
