//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        platformdef.h
//
// Description: Common platform definitions to group similar platforms together.
//              Use these defines instead of checking individual platforms.
//
//=============================================================================

#ifndef PLATFORMDEF_H
#define PLATFORMDEF_H

//=============================================================================
// RAD_SDL_PLATFORM - Platforms using SDL for windowing/input
// Includes: Win32, tvOS (and future: Switch, Vita, etc.)
//=============================================================================
#if defined(RAD_WIN32) || defined(RAD_TVOS)
    #define RAD_SDL_PLATFORM
#endif

//=============================================================================
// RAD_MODERN_PLATFORM - Modern platforms with different timing/event handling
// These platforms may need different approaches for:
// - Screen transitions (immediate vs animated)
// - Interior state management
// - Input handling
//=============================================================================
#if defined(RAD_WIN32) || defined(RAD_TVOS)
    #define RAD_MODERN_PLATFORM
#endif

//=============================================================================
// RAD_OPENGL_PLATFORM - Platforms using OpenGL/GLES rendering
//=============================================================================
#if defined(RAD_WIN32) || defined(RAD_TVOS)
    #define RAD_OPENGL_PLATFORM
#endif

//=============================================================================
// RAD_CONSOLE_PLATFORM - Original console platforms (PS2, Xbox, GameCube)
//=============================================================================
#if defined(RAD_PS2) || defined(RAD_XBOX) || defined(RAD_GAMECUBE)
    #define RAD_CONSOLE_PLATFORM
#endif

#endif // PLATFORMDEF_H
