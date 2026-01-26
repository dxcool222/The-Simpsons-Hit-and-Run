//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//=============================================================================

#ifndef TVOSPLATFORM_H
#define TVOSPLATFORM_H

#include "platform.h"
#include <SDL.h>

struct IRadMemoryHeap;
class tPlatform;
class tContext;

class TvosPlatform : public Platform
{
public:
    static TvosPlatform* CreateInstance();
    static TvosPlatform* GetInstance();
    static void DestroyInstance();

    static bool InitializeWindow();
    static void InitializeFoundation();
    static void InitializeMemory();
    static void ShutdownMemory();

    virtual void InitializePlatform();
    virtual void ShutdownPlatform();

    virtual void LaunchDashboard();
    virtual void ResetMachine();

    virtual void DisplaySplashScreen( SplashScreen screenID,
        const char* overlayText = NULL,
        float fontScale = 1.0f,
        float textPosX = 0.0f,
        float textPosY = 0.0f,
        tColour textColour = tColour( 255,255,255 ),
        int fadeFrames = 3 );

    virtual void DisplaySplashScreen( const char* textureName,
        const char* overlayText = NULL,
        float fontScale = 1.0f,
        float textPosX = 0.0f,
        float textPosY = 0.0f,
        tColour textColour = tColour( 255,255,255 ),
        int fadeFrames = 3 );

    virtual bool OnDriveError( radFileError error, const char* pDriveName, void* pUserData );
    virtual void OnControllerError( const char* msg );

    SDL_Window* GetWindow() const { return mWnd; }

private:
    TvosPlatform();
    virtual ~TvosPlatform();

    TvosPlatform( const TvosPlatform& );
    TvosPlatform& operator=( const TvosPlatform& );

    virtual void InitializeFoundationDrive();
    virtual void ShutdownFoundation();

    virtual void InitializePure3D();
    virtual void ShutdownPure3D();

    void InitializeContext();

    void WaitForController();
    bool HasAnyController() const;

private:
    static TvosPlatform* spInstance;

    static SDL_Window* mWnd;

    tPlatform* mpPlatform;
    tContext* mpContext;
};

#endif
