-- Simpsons Hit & Run - tvOS Port
-- Premake 5 Build Configuration
-- Generated from existing Xcode project analysis

workspace "SRR2"
    configurations { "Debug", "Release" }
    platforms { "tvOS" }
    location "build"
    
    -- CRITICAL: Enable searching user header paths for angle bracket includes
    -- This matches the original project behavior where HEADER_SEARCH_PATHS work for <angled> includes
    xcodebuildsettings {
        ["ALWAYS_SEARCH_USER_PATHS"] = "YES",
        ["USE_HEADERMAP"] = "NO",
        ["SDKROOT"] = "appletvos",  -- tvOS SDK for system framework headers
    }
    
    -- tvOS specific settings
    filter "platforms:tvOS"
        system "ios"
        architecture "arm64"
        systemversion "14.0"
    filter {}
    
    -- Code signing for tvOS (workspace level, matches GTA pattern)
    filter { "platforms:tvOS", "action:xcode4" }
        xcodebuildsettings {
            ["CODE_SIGN_STYLE"] = "Automatic",
        }
    filter {}

-- Path configuration
local SRC_DIR = "src"
local GAME_DIR = SRC_DIR .. "/code"
local LIBS_DIR = SRC_DIR .. "/libs"
local THIRD_PARTY = "third_party"

-- Common defines for all tvOS builds
local TVOS_DEFINES = {
    "RAD_RELEASE",
    "RAD_CONSOLE", 
    "RAD_TVOS",
    "RAD_GLES",
    "RAD_GLES_VERSION=2",
    "AL_LIBTYPE_STATIC"
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

-- System include directories (third party)
local SYSTEM_INCLUDES = {
    THIRD_PARTY .. "/SDL2/include",
    THIRD_PARTY .. "/SDL2/include/SDL2",
    THIRD_PARTY .. "/libpng/include/libpng16",
    THIRD_PARTY .. "/OpenALSoft/include",
    THIRD_PARTY .. "/OpenALSoft/include/AL",
    THIRD_PARTY .. "/ffmpeg/include",
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
local SYSTEM_HEADER_SEARCH_PATHS_TBL = buildHeaderSearchPaths(SYSTEM_INCLUDES)

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
    sysincludedirs(SYSTEM_INCLUDES)
    
    defines(TVOS_DEFINES)
    
    -- Third party static libraries
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
    
    -- FFmpeg static binaries (linked directly like original project)
    -- SRCROOT is the build folder, so go up one level to project root
    linkoptions {
        "\"$(SRCROOT)/../third_party/ffmpeg/libavcodec.framework/libavcodec\"",
        "\"$(SRCROOT)/../third_party/ffmpeg/libavformat.framework/libavformat\"",
        "\"$(SRCROOT)/../third_party/ffmpeg/libavutil.framework/libavutil\"",
        "\"$(SRCROOT)/../third_party/ffmpeg/libswresample.framework/libswresample\"",
        "\"$(SRCROOT)/../third_party/ffmpeg/libswscale.framework/libswscale\"",
    }
    
    -- System frameworks
    links {
        "GameController.framework",
        "Foundation.framework",
        "CoreFoundation.framework",
        "CoreVideo.framework",
        "CoreAudio.framework",
        "AudioToolbox.framework",
        "AVFoundation.framework",
        "CoreBluetooth.framework",
        "CoreGraphics.framework",
        "Metal.framework",
        "OpenGLES.framework",
        "QuartzCore.framework",
        "UIKit.framework",
    }
    
    -- Weak frameworks
    linkoptions {
        "-weak_framework CoreHaptics",
        "-weak_framework GameController",
    }
    
    -- Position independent executable (required for tvOS)
    buildoptions { "-fPIE" }
    
    -- App bundle settings
    xcodebuildsettings {
        -- CRITICAL: Set HEADER_SEARCH_PATHS directly for angle bracket includes
        ["HEADER_SEARCH_PATHS"] = HEADER_SEARCH_PATHS_TBL,
        ["SYSTEM_HEADER_SEARCH_PATHS"] = SYSTEM_HEADER_SEARCH_PATHS_TBL,
        ["PRODUCT_BUNDLE_IDENTIFIER"] = "com.simpsonstvos.srr2",
        ["INFOPLIST_FILE"] = "$(SRCROOT)/../Info.plist",
        ["ASSETCATALOG_COMPILER_APPICON_NAME"] = "AppIcon",
        ["TARGETED_DEVICE_FAMILY"] = "3",  -- Apple TV
        ["TVOS_DEPLOYMENT_TARGET"] = "14.0",
        ["SDKROOT"] = "appletvos",
        ["SUPPORTED_PLATFORMS"] = "appletvos",
        ["ENABLE_BITCODE"] = "NO",
        ["LD_RUNPATH_SEARCH_PATHS"] = "@executable_path/Frameworks",
        ["OTHER_CPLUSPLUSFLAGS"] = "-fPIE",
    }
    
    -- Post-build: Copy ALL game assets to app bundle (tvOS uses flat bundle structure)
    -- Game expects data at Assets/TheSimpsons/ (see tvosdrive.cpp radTvosGetAppRoot)
    -- Assets are in local assets/ folder (self-contained project)
    postbuildcommands {
        -- Create Frameworks folder and embed FFmpeg dynamic frameworks
        "mkdir -p \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH\"",
        "rsync -a \"$SRCROOT/../third_party/ffmpeg/libavcodec.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
        "rsync -a \"$SRCROOT/../third_party/ffmpeg/libavformat.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
        "rsync -a \"$SRCROOT/../third_party/ffmpeg/libavutil.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
        "rsync -a \"$SRCROOT/../third_party/ffmpeg/libswresample.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
        "rsync -a \"$SRCROOT/../third_party/ffmpeg/libswscale.framework\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/\"",
        -- Code sign each FFmpeg framework
        "if [ -n \"$EXPANDED_CODE_SIGN_IDENTITY\" ]; then /usr/bin/codesign --force --sign \"$EXPANDED_CODE_SIGN_IDENTITY\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libavcodec.framework\"; fi",
        "if [ -n \"$EXPANDED_CODE_SIGN_IDENTITY\" ]; then /usr/bin/codesign --force --sign \"$EXPANDED_CODE_SIGN_IDENTITY\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libavformat.framework\"; fi",
        "if [ -n \"$EXPANDED_CODE_SIGN_IDENTITY\" ]; then /usr/bin/codesign --force --sign \"$EXPANDED_CODE_SIGN_IDENTITY\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libavutil.framework\"; fi",
        "if [ -n \"$EXPANDED_CODE_SIGN_IDENTITY\" ]; then /usr/bin/codesign --force --sign \"$EXPANDED_CODE_SIGN_IDENTITY\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libswresample.framework\"; fi",
        "if [ -n \"$EXPANDED_CODE_SIGN_IDENTITY\" ]; then /usr/bin/codesign --force --sign \"$EXPANDED_CODE_SIGN_IDENTITY\" \"$TARGET_BUILD_DIR/$FRAMEWORKS_FOLDER_PATH/libswscale.framework\"; fi",
        -- Copy game assets to app bundle
        "mkdir -p \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons\"",
        -- Copy art folder
        "cp -R \"${SRCROOT}/../assets/art\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
        -- Copy scripts folder
        "cp -R \"${SRCROOT}/../assets/scripts\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
        -- Copy movies folder
        "cp -R \"${SRCROOT}/../assets/movies\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
        -- Copy sound folder
        "cp -R \"${SRCROOT}/../assets/sound\" \"${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Assets/TheSimpsons/\"",
        -- Copy RCF files (audio/dialog data)
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
    }
    
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
        flags { "LinkTimeOptimization" }
        xcodebuildsettings {
            ["GCC_OPTIMIZATION_LEVEL"] = "3",
            ["LLVM_LTO"] = "YES",
        }
    filter {}
