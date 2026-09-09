//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef RADCORE_PCH_HPP
#define RADCORE_PCH_HPP

//
// only win32 and xbox take advantage of the pch at this moment.
//
#if defined RAD_WIN32 || defined RAD_XBOX
//
// Microsoft header files
//
#ifdef WIN32
#define NOMINMAX
#define WINVER 0x0501
#define _WIN32_WINNT 0x400
#include <windows.h>
#ifdef RAD_PC
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#endif
#include <crtdbg.h>
#endif

#ifdef RAD_XBOX
#include <xtl.h>
#endif

//
// Standard C header files
//

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <math.h>

//
// FTech header files
//
#include "radoptions.hpp"

#endif // defined RAD_WIN32 || defined RAD_XBOX

#ifndef TVOS_AUDIO_DIAG
#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
    #if __has_include(<SDL2/SDL.h>)
        #include <SDL2/SDL.h>
    #else
        #include <SDL.h>
    #endif
    #define TVOS_AUDIO_DIAG(...) SDL_Log(__VA_ARGS__)
#else
    #define TVOS_AUDIO_DIAG(...) ((void)0)
#endif
#endif

#endif // RADCORE_PCH_HPP
