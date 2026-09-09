//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//=============================================================================

#include <SDL.h>
#include <SDL_main.h>

#ifdef main
#undef main
#endif

#include <main/game.h>
#include <main/tvosplatform.h>
#include <main/singletons.h>
#include <main/commandlineoptions.h>

#include <memory/srrmemory.h>
#include <p3d/entity.hpp>

//========================================
// Forward Declarations
//========================================
static void ProcessCommandLineArguments( int argc, char *argv[] );
static void ProcessCommandLineArgumentsFromFile();

int SDL_main( int argc, char *argv[] );

extern "C" int main( int argc, char *argv[] )
{
    @autoreleasepool
    {
#if defined(RAD_MACOS)
        // macOS/Cocoa SDL: call the game entry directly (no UIKit runner).
        return SDL_main( argc, argv );
#else
        return SDL_UIKitRunApp( argc, argv, SDL_main );
#endif
    }
}

int SDL_main( int argc, char *argv[] )
{
    // Pick out and store command line settings.
    CommandLineOptions::InitDefaults();
    ProcessCommandLineArguments( argc, argv );
    ProcessCommandLineArgumentsFromFile();

    // Use native GameController.framework instead of SDL for controller input.
    // SDL is only used for window/video/events. Controller input is handled by tvoscontroller.cpp
    // and code/input/tvos/tvos_controller.mm which use Apple's GameController.framework directly.
    
#if !defined(RAD_MACOS)
    // Disable SDL controller UI events since we're not using SDL for controllers (tvOS)
    SDL_SetHint(SDL_HINT_APPLE_TV_CONTROLLER_UI_EVENTS, "0");
#endif
    
    // Initialize SDL for video and events only - NO controller/joystick subsystems
    SDL_Init( SDL_INIT_EVENTS | SDL_INIT_VIDEO );

    TvosPlatform::InitializeMemory();

    if( !TvosPlatform::InitializeWindow() )
    {
        return 0;
    }

    TvosPlatform::InitializeFoundation();

    srand( Game::GetRandomSeed() );

#ifndef RAD_RELEASE
    tName::SetAllocator( GMA_DEBUG );
#endif

    HeapMgr()->PushHeap( GMA_PERSISTENT );

    CreateSingletons();

    TvosPlatform* pPlatform = TvosPlatform::CreateInstance();
    rAssert( pPlatform != NULL );

    Game* pGame = Game::CreateInstance( pPlatform );
    rAssert( pGame != NULL );

    pGame->Initialize();

    HeapMgr()->PopHeap( GMA_PERSISTENT );

    pGame->Run();

    pGame->Terminate();

    DestroySingletons();

    Game::DestroyInstance();

    pPlatform->ShutdownPlatform();

    TvosPlatform::DestroyInstance();

    TvosPlatform::ShutdownMemory();

#ifndef RAD_RELEASE
    tName::SetAllocator( RADMEMORY_ALLOC_DEFAULT );
#endif

    SDL_Quit();

    return 0;
}

static void ProcessCommandLineArguments( int /*argc*/, char* /*argv*/[] )
{
}

static void ProcessCommandLineArgumentsFromFile()
{
}
