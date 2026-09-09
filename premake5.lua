-- Simpsons Hit & Run - tvOS + macOS (Apple Silicon) Port
-- Premake 5 Build Configuration
-- Generated from existing Xcode project analysis

workspace "SRR2"
    configurations { "Debug", "Release" }
    platforms { "tvOS", "macosx" }
    location "build"
    
    -- CRITICAL: Enable searching user header paths for angle bracket includes
    -- This matches the original project behavior where HEADER_SEARCH_PATHS work for <angled> includes
    xcodebuildsettings {
        ["ALWAYS_SEARCH_USER_PATHS"] = "YES",
        ["USE_HEADERMAP"] = "NO",
    }
    
    -- tvOS specific settings
    filter "platforms:tvOS"
        system "ios"
        architecture "arm64"
        systemversion "14.0"
        xcodebuildsettings {
            ["SDKROOT"] = "appletvos",
        }
    filter {}

    -- macOS Apple Silicon settings
    filter "platforms:macosx"
        system "macosx"
        architecture "ARM64"
        systemversion "14.0"
        xcodebuildsettings {
            ["SDKROOT"] = "macosx",
            ["MACOSX_DEPLOYMENT_TARGET"] = "14.0",
            ["ARCHS"] = "arm64",
            ["ONLY_ACTIVE_ARCH"] = "YES",
        }
    filter {}
    
    -- Code signing for tvOS (workspace level, matches GTA pattern)
    filter { "platforms:tvOS", "action:xcode4" }
        xcodebuildsettings {
            ["CODE_SIGN_STYLE"] = "Automatic",
        }
    filter {}

    filter { "platforms:macosx", "action:xcode4" }
        xcodebuildsettings {
            ["CODE_SIGN_STYLE"] = "Automatic",
            ["CODE_SIGN_IDENTITY"] = "-",
        }
    filter {}

-- Path configuration
local SRC_DIR = "src"
local GAME_DIR = SRC_DIR .. "/code"
local LIBS_DIR = SRC_DIR .. "/libs"
local THIRD_PARTY = "third_party"
local MAC_DEPS = "MAC_PORT/deps/prefix"

-- Common defines for all Apple GLES console-style builds
-- RAD_TVOS is retained on Mac (Policy A) so shared Apple/GLES paths keep compiling.
local TVOS_DEFINES = {
    "RAD_RELEASE",
    "RAD_CONSOLE", 
    "RAD_TVOS",
    "RAD_GLES",
    "RAD_GLES_VERSION=2",
    "AL_LIBTYPE_STATIC",
    "RAD_TVOS_AUDIO_DIAGNOSTICS",
    "RAD_TVOS_RENDER_DIAGNOSTICS",
    "RAD_TVOS_STORAGE_DIAGNOSTICS",
}

local MACOS_DEFINES = {
    "RAD_RELEASE",
    "RAD_CONSOLE",
    "RAD_TVOS",
    "RAD_MACOS",
    "RAD_GLES",
    "RAD_GLES_VERSION=2",
    "AL_LIBTYPE_STATIC",
    "RAD_TVOS_AUDIO_DIAGNOSTICS",
    "RAD_TVOS_RENDER_DIAGNOSTICS",
    "RAD_TVOS_STORAGE_DIAGNOSTICS",
    "GL_SILENCE_DEPRECATION",
}

-- Common include directories (extracted from original working Xcode project)
-- These paths enable umbrella-style includes like <radmath/vector.hpp>, <p3d/p3dtypes.hpp>
local COMMON_INCLUDES = {
    -- Game code
    GAME_DIR,
    -- libs root (for cross-library includes)
    LIBS_DIR,
    -- radcore
    LIBS_DIR .. "/radcore/inc",
    LIBS_DIR .. "/radcore/src/pch",
    -- radmath (parent dir so <radmath/vector.hpp> resolves to libs/radmath/radmath/vector.hpp)
    LIBS_DIR .. "/radmath",
    -- pure3d (parent dir so <p3d/p3dtypes.hpp> resolves to libs/pure3d/p3d/p3dtypes.hpp)
    LIBS_DIR .. "/pure3d",
    -- pure3d/p3d directly (for includes like <p3dtypes.hpp> without prefix)
    LIBS_DIR .. "/pure3d/p3d",
    -- sim (parent dir so <simcommon/...> resolves correctly)
    LIBS_DIR .. "/sim",
    -- radcontent
    LIBS_DIR .. "/radcontent/inc",
    LIBS_DIR .. "/radcontent/src",
    -- radscript
    LIBS_DIR .. "/radscript/inc",
    LIBS_DIR .. "/radscript/src/pch",
    -- radsound
    LIBS_DIR .. "/radsound/inc",
    LIBS_DIR .. "/radsound/src/pch",
    LIBS_DIR .. "/radsound/src/common",
    -- radmusic
    LIBS_DIR .. "/radmusic/inc",
    LIBS_DIR .. "/radmusic/src",
    LIBS_DIR .. "/radmusic/src/pch",
    -- radmovie
    LIBS_DIR .. "/radmovie/inc",
    LIBS_DIR .. "/radmovie/src/pch",
    -- scrooby
    LIBS_DIR .. "/scrooby/inc",
    LIBS_DIR .. "/scrooby/src",
    -- choreo
    LIBS_DIR .. "/choreo/inc",
    -- poser
    LIBS_DIR .. "/poser/inc",
}

-- System include directories (third party) — tvOS uses root third_party/
local SYSTEM_INCLUDES_TVOS = {
    THIRD_PARTY .. "/SDL2/include",
    THIRD_PARTY .. "/SDL2/include/SDL2",
    THIRD_PARTY .. "/libpng/include/libpng16",
    THIRD_PARTY .. "/OpenALSoft/include",
    THIRD_PARTY .. "/OpenALSoft/include/AL",
    THIRD_PARTY .. "/ffmpeg/include",
}

-- macOS uses rebuilt deps under MAC_PORT/deps/prefix (USB)
local SYSTEM_INCLUDES_MACOS = {
    MAC_DEPS .. "/include",
    MAC_DEPS .. "/include/SDL2",
    MAC_DEPS .. "/include/libpng16",
    MAC_DEPS .. "/include/AL",
    MAC_DEPS .. "/include",
}

-- Helper function to build HEADER_SEARCH_PATHS table for Xcode
-- Returns a table that Premake will convert to a proper Xcode array
-- NOTE: $(SRCROOT) points to build/ folder where .xcodeproj lives,
-- so we use $(SRCROOT)/.. to reach the actual project root
local function buildHeaderSearchPaths(paths)
    local result = {}
    for _, p in ipairs(paths) do
        table.insert(result, "$(SRCROOT)/../" .. p)
    end
    return result
end

-- Pre-build the header search paths tables
local HEADER_SEARCH_PATHS_TBL = buildHeaderSearchPaths(COMMON_INCLUDES)
local SYSTEM_HEADER_SEARCH_PATHS_TVOS_TBL = buildHeaderSearchPaths(SYSTEM_INCLUDES_TVOS)
local SYSTEM_HEADER_SEARCH_PATHS_MACOS_TBL = buildHeaderSearchPaths(SYSTEM_INCLUDES_MACOS)

-- ============================================================================
-- GLOBAL EXCLUSION LIST - AUTHORITATIVE SOURCE
-- Based on exact diff between original Xcode project and files on disk
-- DO NOT ADD FILES ONE-BY-ONE. This list is comprehensive and final.
-- ============================================================================
local EXCLUDED_FILES = {
    -- ========================================================================
    -- GLOBAL PATTERNS (apply to all projects)
    -- ========================================================================
    -- Unity/aggregation build files - NOT used in original project
    -- Be specific to avoid matching legitimate files like AllWrappers.cpp, allloaders.cpp
    "**/allcode.cpp",
    "**/allsound.cpp",
    "**/allai.cpp",
    "**/allrender.cpp",
    "**/allgui.cpp",
    "**/allworld.cpp",
    "**/allinput.cpp",
    "**/alldata.cpp",
    "**/allpresentation.cpp",
    -- Temporary/conflict/corrupt files (git conflicts, editor temp files)
    "**/.!*",
    "**/._*",
    -- Sample and test code - NOT part of runtime
    "**/sample/**",
    "**/test/**",
    
    -- ========================================================================
    -- PLATFORM-SPECIFIC EXCLUSIONS (non-tvOS platforms)
    -- ========================================================================
    -- Vita platform
    "**/platform/vita/**",
    "**/display_vita/**",
    -- Linux platform
    "**/platform/linux/**",
    "**/display_linux/**",
    -- SGI platform
    "**/platform/sgi/**",
    "**/display_sgi/**",
    -- Win32 platform (specific exclusions)
    -- Note: p3d/platform/win32/platform.cpp IS used on tvOS per original project
    -- Note: radsound/hal/win32 and radcore/radmemory/memoryspacewin32.cpp ARE used on tvOS
    "**/display_win32/**",
    "**/radfile/win32/**",
    "**/radcore/src/platform/win32/**",
    -- PS2 platform
    "**/ps2/**",
    "**/shadow_ps2.cpp",
    -- GameCube platform
    "**/gcn/**",
    "**/gamecube_extras/**",
    "**/shadow_gc.cpp",
    -- Xbox platform
    "**/xbox/**",
    "**/xboxmain.cpp",
    "**/xboxplatform.cpp",
    -- PS2 main files
    "**/main/ps2main.cpp",
    "**/main/ps2platform.cpp",
    -- GameCube main files
    "**/main/gcmain.cpp",
    "**/main/gcplatform.cpp",
    -- Win32 main files
    "**/main/win32main.cpp",
    "**/main/win32platform.cpp",
    -- DirectX backends
    "**/pddi/dx8/**",
    "**/pddi/d3d/**",
    -- OpenGL desktop (not GLES)
    "**/pddi/gl/**",
    -- GLAD loader (not used on tvOS)
    "**/glad/**",
    
    -- ========================================================================
    -- RADCORE EXCLUSIONS (debug tools, platform controllers, etc.)
    -- ========================================================================
    "**/radcore/src/pch/pch.cpp",
    "**/radcore/src/rad1394/**",
    "**/radcore/src/radcontroller/controllerbuffer.cpp",
    "**/radcore/src/radcontroller/directinputcontroller.cpp",
    "**/radcore/src/radcontroller/gcncontroller.cpp",
    "**/radcore/src/radcontroller/ps2controller.cpp",
    "**/radcore/src/radcontroller/sdlcontroller.cpp",
    "**/radcore/src/radcontroller/xboxcontroller.cpp",
    "**/radcore/src/radcrashhandler/gcncrashhandler.cpp",
    "**/radcore/src/radcrashhandler/ps2crashhandler.cpp",
    "**/radcore/src/radcrashhandler/xboxcrashhandler.cpp",
    -- raddebugcommunication: keep ONLY targetx.cpp per original project
    "**/radcore/src/raddebugcommunication/host*.cpp",
    "**/radcore/src/raddebugcommunication/target1394*.cpp",
    "**/radcore/src/raddebugcommunication/targetconnection.cpp",
    "**/radcore/src/raddebugcommunication/targetdeci*.cpp",
    "**/radcore/src/raddebugcommunication/targethio*.cpp",
    "**/radcore/src/raddebugcommunication/targetsocket*.cpp",
    "**/radcore/src/raddebugfileserver/**",
    "**/radcore/src/raddebugwatch/**",
    "**/radcore/src/radfile/common/buffereddrive.cpp",
    "**/radcore/src/radfile/common/signeddrive.cpp",
    "**/radcore/src/radmemory/memoryspacegcn.cpp",
    "**/radcore/src/radmemory/memoryspaceps2.cpp",
    -- Note: memoryspacewin32.cpp IS used on tvOS per original project
    "**/radcore/src/radobject/refcount.cpp",
    "**/radcore/src/radprofiler/microprofile.cpp",
    "**/radcore/src/radstacktrace/win32/**",
    "**/radscript/src/typeinfo/win32/**",
    
    -- ========================================================================
    -- PURE3D/P3D EXCLUSIONS
    -- ========================================================================
    -- p3d/platform/win32: only platform.cpp is used per original project
    "**/pure3d/p3d/platform/win32/plat_filemap.cpp",
    "**/pure3d/p3d/effects/opticcorona.cpp",
    "**/pure3d/p3d/mipmapfilter.cpp",
    "**/pure3d/p3d/shadow.cpp",
    
    -- ========================================================================
    -- SIM EXCLUSIONS (simflexible and simik not in original)
    -- ========================================================================
    "**/sim/simflexible/**",
    "**/sim/simik/**",
    
    -- ========================================================================
    -- SCROOBY EXCLUSIONS
    -- ========================================================================
    "**/scrooby/src/p2d/**",
    "**/scrooby/src/localization/**",
    "**/scrooby/src/FE2DCore.cpp",
    "**/scrooby/src/FeProjectLoader.cpp",
    "**/scrooby/src/FeResourceEntry.cpp",
    "**/scrooby/src/FeResourceUser.cpp",
    "**/scrooby/src/FeTranslateResource.cpp",
    "**/scrooby/src/lPath.cpp",
    "**/scrooby/src/ResourceManager/FeFontManager.cpp",
    "**/scrooby/src/strings/unicodeChar.cpp",
    "**/scrooby/src/utility/memory.cpp",
    "**/scrooby/src/utility/StreamReader.cpp",
    "**/scrooby/src/utility/Util.cpp",
    "**/scrooby/src/xml/XMLSaver.cpp",
    
    -- ========================================================================
    -- RADSOUND EXCLUSIONS
    -- ========================================================================
    "**/radsound/src/hal/common/softwarelistener.cpp",
    "**/radsound/src/hal/common/softwarepositionalgroup.cpp",
    
    -- ========================================================================
    -- RADMOVIE EXCLUSIONS
    -- ========================================================================
    "**/radmovie/src/common/audiodatasource.cpp",
    "**/radmovie/src/common/binkmovieplayer.cpp",
    "**/radmovie/src/common/binkradfile.cpp",
    "**/radmovie/src/common/movieplayer.cpp",
    
    -- ========================================================================
    -- GAME CODE EXCLUSIONS
    -- ========================================================================
    -- Input files not in original
    "**/input/basedamper.cpp",
    "**/input/constanteffect.cpp",
    "**/input/FEMouse.cpp",
    "**/input/forceeffect.cpp",
    "**/input/Gamepad.cpp",
    "**/input/Keyboard.cpp",
    "**/input/Mouse.cpp",
    "**/input/rumblegc.cpp",
    "**/input/rumbleps2.cpp",
    -- Note: rumblewin32.cpp IS used on tvOS per original project
    "**/input/rumblexbox.cpp",
    "**/input/steeringspring.cpp",
    "**/input/SteeringWheel.cpp",
    "**/input/usercontrollerWin32.cpp",
    "**/input/virtualinputs.cpp",
    "**/input/wheelrumble.cpp",
    -- Sound files not in original
    "**/sound/soundfx/gcreverbcontroller.cpp",
    "**/sound/soundfx/ps2reverbcontroller.cpp",
    -- Note: win32reverbcontroller.cpp IS used on tvOS per original project
    "**/sound/soundfx/xboxreverbcontroller.cpp",
    "**/sound/soundrenderer/scripts/french.cpp",
    "**/sound/soundrenderer/scripts/german.cpp",
    "**/sound/soundrenderer/scripts/spanish.cpp",
    -- Camera files not in original
    "**/camera/burnoutcam.cpp",
    "**/camera/pccam.cpp",
    -- Data files not in original
    "**/data/config/configstring.cpp",
    "**/data/config/gameconfigmanager.cpp",
    "**/data/PersistentSectors.cpp",
    -- GUI files not in original
    "**/presentation/gui/frontend/guiscreencontrollerWin32.cpp",
    "**/presentation/gui/frontend/guiscreencontrollerWin32old.cpp",
    "**/presentation/gui/frontend/guiscreendisplay.cpp",
    "**/presentation/gui/frontend/guiscreenmultichoosechar.cpp",
    "**/presentation/gui/frontend/guiscreenmultisetup.cpp",
    "**/presentation/gui/guimanagerfrontend.cpp",
    "**/presentation/gui/guiscreenlicense.cpp",
    "**/presentation/gui/guiscreenloadingfe.cpp",
    "**/presentation/gui/guiscreenmainmenu.cpp",
    "**/presentation/gui/guiscreensplash.cpp",
    "**/presentation/gui/ingame/guiscreenhudmap.cpp",
    "**/presentation/gui/ingame/guiscreenpausedisplay.cpp",
    -- Worldsim files not in original
    "**/worldsim/character/footprint/footprint.cpp",
}

-- ============================================================================
-- Main Application: SRR2 (MONOLITHIC BUILD)
-- All library sources compiled directly into SRR2, matching original Xcode project
-- ============================================================================
project "SRR2"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++17"
    
    targetdir "build/%{cfg.buildcfg}"
    objdir "build/obj/%{prj.name}/%{cfg.buildcfg}"
    
    -- ==========================================================================
    -- MONOLITHIC SOURCE INCLUSION
    -- All game code + all library sources compiled directly into SRR2
    -- ==========================================================================
    
    -- Game source files
    files {
        GAME_DIR .. "/**.cpp",
        GAME_DIR .. "/**.c",
        GAME_DIR .. "/**.mm",
    }
    
    -- Library sources (monolithic - compiled directly into SRR2)
    files {
        -- radcore
        LIBS_DIR .. "/radcore/src/**.cpp",
        LIBS_DIR .. "/radcore/src/**.c",
        -- radmath
        LIBS_DIR .. "/radmath/**.cpp",
        -- radcontent
        LIBS_DIR .. "/radcontent/src/**.cpp",
        -- radscript
        LIBS_DIR .. "/radscript/src/**.cpp",
        -- radsound
        LIBS_DIR .. "/radsound/src/**.cpp",
        -- radmusic
        LIBS_DIR .. "/radmusic/src/**.cpp",
        -- radmovie
        LIBS_DIR .. "/radmovie/src/**.cpp",
        -- pure3d (p3d + pddi + constants)
        LIBS_DIR .. "/pure3d/p3d/**.cpp",
        LIBS_DIR .. "/pure3d/pddi/base/**.cpp",
        LIBS_DIR .. "/pure3d/pddi/gles/*.cpp",
        LIBS_DIR .. "/pure3d/pddi/gles/*.c",
        LIBS_DIR .. "/pure3d/pddi/gles/display_tvos/**.cpp",
        LIBS_DIR .. "/pure3d/constants/**.cpp",
        -- choreo
        LIBS_DIR .. "/choreo/src/**.cpp",
        -- poser
        LIBS_DIR .. "/poser/src/**.cpp",
        -- scrooby
        LIBS_DIR .. "/scrooby/src/**.cpp",
        -- sim
        LIBS_DIR .. "/sim/simcollision/**.cpp",
        LIBS_DIR .. "/sim/simcommon/**.cpp",
        LIBS_DIR .. "/sim/simphysics/**.cpp",
    }
    
    -- Apply global exclusions
    removefiles(EXCLUDED_FILES)
    
    -- Exclude ALL unity build files (all*.cpp) throughout game code
    -- EXCEPT AllWrappers.cpp which is a legitimate file, not a unity file
    removefiles {
        GAME_DIR .. "/**/all*.cpp",
    }
    -- Re-include AllWrappers.cpp (incorrectly caught by all*.cpp pattern)
    files {
        GAME_DIR .. "/render/Loaders/AllWrappers.cpp",
    }
    
    -- Explicitly exclude platform-specific main files
    removefiles {
        GAME_DIR .. "/main/ps2main.cpp",
        GAME_DIR .. "/main/ps2platform.cpp",
        GAME_DIR .. "/main/gcmain.cpp",
        GAME_DIR .. "/main/gcplatform.cpp",
        GAME_DIR .. "/main/win32main.cpp",
        GAME_DIR .. "/main/win32platform.cpp",
        GAME_DIR .. "/main/xboxmain.cpp",
        GAME_DIR .. "/main/xboxplatform.cpp",
    }
    
    -- Explicitly include tvOS platform files
    files {
        GAME_DIR .. "/main/tvosplatform.cpp",
        GAME_DIR .. "/main/tvosmain.mm",
        GAME_DIR .. "/input/tvos/**.cpp",
        GAME_DIR .. "/input/tvos/**.mm",
        -- tvOS-specific radcore files
        LIBS_DIR .. "/radcore/src/radcontroller/tvoscontroller.cpp",
        LIBS_DIR .. "/radcore/src/radfile/tvos/**.cpp",
    }
    
    includedirs(COMMON_INCLUDES)
    
    -- ==========================================================================
    -- tvOS platform: original third_party + UIKit/OpenGLES
    -- ==========================================================================
    filter "platforms:tvOS"
        externalincludedirs(SYSTEM_INCLUDES_TVOS)
        defines(TVOS_DEFINES)

        libdirs {
            THIRD_PARTY .. "/SDL2/lib",
            THIRD_PARTY .. "/libpng/lib",
            THIRD_PARTY .. "/OpenALSoft/lib",
        }

        links {
            "SDL2",
            "png16",
            "openal",
            "z",
            "m",
            "pthread",
            "dl",
        }

        -- Frameworks MUST be in linkoptions (per-config). Premake's links{} frameworks
        -- go into a shared PBXFrameworksBuildPhase and would leak UIKit/OpenGLES onto Mac.
        linkoptions {
            "\"$(SRCROOT)/../third_party/ffmpeg/libavcodec.framework/libavcodec\"",
            "\"$(SRCROOT)/../third_party/ffmpeg/libavformat.framework/libavformat\"",
            "\"$(SRCROOT)/../third_party/ffmpeg/libavutil.framework/libavutil\"",
            "\"$(SRCROOT)/../third_party/ffmpeg/libswresample.framework/libswresample\"",
            "\"$(SRCROOT)/../third_party/ffmpeg/libswscale.framework/libswscale\"",
            "-framework GameController",
            "-framework Foundation",
            "-framework CoreFoundation",
            "-framework CoreVideo",
            "-framework CoreAudio",
            "-framework AudioToolbox",
            "-framework AVFoundation",
            "-framework CoreBluetooth",
            "-framework CoreGraphics",
            "-framework Metal",
            "-framework OpenGLES",
            "-framework QuartzCore",
            "-framework UIKit",
            "-weak_framework CoreHaptics",
            "-weak_framework GameController",
        }

        buildoptions { "-fPIE" }

        xcodebuildsettings {
            ["HEADER_SEARCH_PATHS"] = HEADER_SEARCH_PATHS_TBL,
            ["SYSTEM_HEADER_SEARCH_PATHS"] = SYSTEM_HEADER_SEARCH_PATHS_TVOS_TBL,
            ["PRODUCT_BUNDLE_IDENTIFIER"] = "com.simpsonstvos.srr2",
            ["INFOPLIST_FILE"] = "$(SRCROOT)/../Info.plist",
            ["ASSETCATALOG_COMPILER_APPICON_NAME"] = "AppIcon",
            ["TARGETED_DEVICE_FAMILY"] = "3",
            ["TVOS_DEPLOYMENT_TARGET"] = "14.0",
            ["SDKROOT"] = "appletvos",
            ["SUPPORTED_PLATFORMS"] = "appletvos",
            ["ENABLE_BITCODE"] = "NO",
            ["LD_RUNPATH_SEARCH_PATHS"] = "@executable_path/Frameworks",
            ["OTHER_CPLUSPLUSFLAGS"] = "-fPIE",
        }

        postbuildcommands {
            "if [[ \"${PLATFORM_NAME}\" == appletvos || \"${SDK_NAME}\" == appletvos* ]]; then",
            "mkdir -p \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH\"",
            "rsync -a \"$SRCROOT/../third_party/ffmpeg/libavcodec.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
            "rsync -a \"$SRCROOT/../third_party/ffmpeg/libavformat.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
            "rsync -a \"$SRCROOT/../third_party/ffmpeg/libavutil.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
            "rsync -a \"$SRCROOT/../third_party/ffmpeg/libswresample.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
            "rsync -a \"$SRCROOT/../third_party/ffmpeg/libswscale.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
            "if [ -n \"$EXPANDED_CODE_SIGN_IDENTITY\" ]; then /usr/bin/codesign --force --sign \"$EXPANDED_CODE_SIGN_IDENTITY\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libavcodec.framework\"; fi",
            "if [ -n \"$EXPANDED_CODE_SIGN_IDENTITY\" ]; then /usr/bin/codesign --force --sign \"$EXPANDED_CODE_SIGN_IDENTITY\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libavformat.framework\"; fi",
            "if [ -n \"$EXPANDED_CODE_SIGN_IDENTITY\" ]; then /usr/bin/codesign --force --sign \"$EXPANDED_CODE_SIGN_IDENTITY\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libavutil.framework\"; fi",
            "if [ -n \"$EXPANDED_CODE_SIGN_IDENTITY\" ]; then /usr/bin/codesign --force --sign \"$EXPANDED_CODE_SIGN_IDENTITY\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libswresample.framework\"; fi",
            "if [ -n \"$EXPANDED_CODE_SIGN_IDENTITY\" ]; then /usr/bin/codesign --force --sign \"$EXPANDED_CODE_SIGN_IDENTITY\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libswscale.framework\"; fi",
            "mkdir -p \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons\"",
            "cp -R \"${SRCROOT}/../assets/art\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "cp -R \"${SRCROOT}/../assets/scripts\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "cp -R \"${SRCROOT}/../assets/movies\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "cp -R \"${SRCROOT}/../assets/sound\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/ambience.rcf\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/carsound.rcf\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/dialog.rcf\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/music00.rcf\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/music01.rcf\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/music02.rcf\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/music03.rcf\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/nis.rcf\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/scripts.rcf\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/soundfx.rcf\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
            "fi",
        }
    filter {}

    -- ==========================================================================
    -- macOS arm64 platform: MAC_PORT deps + AppKit/OpenGL (no UIKit/OpenGLES)
    -- ==========================================================================
    filter "platforms:macosx"
        externalincludedirs(SYSTEM_INCLUDES_MACOS)
        defines(MACOS_DEFINES)

        libdirs {
            MAC_DEPS .. "/lib",
        }

        links {
            "SDL2",
            "png16",
            "openal",
            "z",
            "iconv",
            "m",
            "pthread",
            "dl",
        }

        linkoptions {
            "\"$(SRCROOT)/../MAC_PORT/deps/prefix/Frameworks/libavcodec.framework/libavcodec\"",
            "\"$(SRCROOT)/../MAC_PORT/deps/prefix/Frameworks/libavformat.framework/libavformat\"",
            "\"$(SRCROOT)/../MAC_PORT/deps/prefix/Frameworks/libavutil.framework/libavutil\"",
            "\"$(SRCROOT)/../MAC_PORT/deps/prefix/Frameworks/libswresample.framework/libswresample\"",
            "\"$(SRCROOT)/../MAC_PORT/deps/prefix/Frameworks/libswscale.framework/libswscale\"",
            "-framework GameController",
            "-framework Foundation",
            "-framework CoreFoundation",
            "-framework CoreVideo",
            "-framework CoreAudio",
            "-framework AudioToolbox",
            "-framework AVFoundation",
            "-framework CoreBluetooth",
            "-framework CoreGraphics",
            "-framework Metal",
            "-framework OpenGL",
            "-framework QuartzCore",
            "-framework AppKit",
            "-framework IOKit",
            "-framework ForceFeedback",
            "-framework Carbon",
            "-framework AudioUnit",
            "-framework CoreServices",
            "-weak_framework CoreHaptics",
            "-weak_framework GameController",
        }

        xcodebuildsettings {
            ["HEADER_SEARCH_PATHS"] = HEADER_SEARCH_PATHS_TBL,
            ["SYSTEM_HEADER_SEARCH_PATHS"] = SYSTEM_HEADER_SEARCH_PATHS_MACOS_TBL,
            ["PRODUCT_BUNDLE_IDENTIFIER"] = "com.simpsonsmac.srr2",
            ["INFOPLIST_FILE"] = "$(SRCROOT)/../MAC_PORT/Info-mac.plist",
            ["SDKROOT"] = "macosx",
            ["MACOSX_DEPLOYMENT_TARGET"] = "14.0",
            ["ARCHS"] = "arm64",
            ["ONLY_ACTIVE_ARCH"] = "YES",
            ["SUPPORTED_PLATFORMS"] = "macosx",
            ["ENABLE_BITCODE"] = "NO",
            ["LD_RUNPATH_SEARCH_PATHS"] = "@executable_path/../Frameworks",
            ["COMBINE_HIDPI_IMAGES"] = "YES",
            ["CLANG_ENABLE_OBJC_ARC"] = "YES",
        }

        -- Assets in Contents/Resources (codesign-safe). VFS resolves via parent_path()/Resources.
        postbuildcommands {
            "if [[ \"${PLATFORM_NAME}\" == macosx || \"${SDK_NAME}\" == macosx* ]]; then",
            "mkdir -p \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH\"",
            "rsync -a \"$SRCROOT/../MAC_PORT/deps/prefix/Frameworks/libavcodec.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
            "rsync -a \"$SRCROOT/../MAC_PORT/deps/prefix/Frameworks/libavformat.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
            "rsync -a \"$SRCROOT/../MAC_PORT/deps/prefix/Frameworks/libavutil.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
            "rsync -a \"$SRCROOT/../MAC_PORT/deps/prefix/Frameworks/libswresample.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
            "rsync -a \"$SRCROOT/../MAC_PORT/deps/prefix/Frameworks/libswscale.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
            "codesign --force --sign - --timestamp=none \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libavcodec.framework\" || true",
            "codesign --force --sign - --timestamp=none \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libavformat.framework\" || true",
            "codesign --force --sign - --timestamp=none \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libavutil.framework\" || true",
            "codesign --force --sign - --timestamp=none \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libswresample.framework\" || true",
            "codesign --force --sign - --timestamp=none \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libswscale.framework\" || true",
            "mkdir -p \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons\"",
            "rsync -a \"${SRCROOT}/../assets/art\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "rsync -a \"${SRCROOT}/../assets/scripts\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "rsync -a \"${SRCROOT}/../assets/movies\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "rsync -a \"${SRCROOT}/../assets/sound\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/ambience.rcf\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/carsound.rcf\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/dialog.rcf\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/music00.rcf\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/music01.rcf\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/music02.rcf\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/music03.rcf\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/nis.rcf\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/scripts.rcf\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "cp \"${SRCROOT}/../assets/soundfx.rcf\" \"${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/Assets/TheSimpsons/\"",
            "fi",
        }
    filter {}

    filter "configurations:Debug"
        symbols "On"
        optimize "Off"
        xcodebuildsettings {
            ["GCC_OPTIMIZATION_LEVEL"] = "0",
        }
        
    filter "configurations:Release"
        symbols "Off"
        optimize "Speed"
        defines { "NDEBUG" }
        xcodebuildsettings {
            ["GCC_OPTIMIZATION_LEVEL"] = "3",
            ["LLVM_LTO"] = "YES",
        }
    filter {}
