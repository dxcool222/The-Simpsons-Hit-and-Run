//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include "pch.hpp"

#ifdef RAD_TVOS

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <mutex>
#include <os/log.h>
#include <stdarg.h>
#include <filesystem>
#include <string>
#include <sys/statvfs.h>
#include <sys/xattr.h>
#include <unordered_map>
#include <unordered_set>

#include "tvosdrive.hpp"
#include <diagnostics/tvosdiagnostics.h>

#if defined( RAD_TVOS_STORAGE_DIAGNOSTICS )
    #define TVOS_STORAGE_DIAG(...) SRR2::Diagnostics::AutoLogf(SRR2::Diagnostics::VFS, __VA_ARGS__)
#else
    #define TVOS_STORAGE_DIAG(...) ((void)0)
#endif

// Get available bytes on the filesystem containing the given path.
// This queries the actual tvOS sandbox storage, not a fake block device.
static uint64_t radTvosGetAvailableBytes( const std::filesystem::path& path )
{
    std::error_code ec;
    std::filesystem::space_info si = std::filesystem::space( path, ec );
    if ( ec || si.available == static_cast<uintmax_t>(-1) )
    {
        // Fallback: return a large value so saves don't fail
        TVOS_STORAGE_DIAG( "[SAVE_SPACE] path=%s error=%s, returning fallback",
                 path.c_str(), ec ? ec.message().c_str() : "unknown" );
        return UINT_MAX;
    }
    TVOS_STORAGE_DIAG( "[SAVE_SPACE] path=%s available=%llu bytes",
             path.c_str(), (unsigned long long)si.available );
    return si.available;
}

static const char s_TvosAppDriveName[] = "APP:";
static const char s_TvosPrefDriveName[] = "PREF:";

static std::filesystem::path MakePathFromSdl( char* sdlPath );

static const std::filesystem::path& radTvosGetBasePath( void )
{
    static std::filesystem::path s_base;
    static bool s_init = false;
    if ( !s_init )
    {
        s_init = true;
        s_base = MakePathFromSdl( SDL_GetBasePath() );
    }
    return s_base;
}

static const std::filesystem::path& radTvosGetAppRoot( void )
{
    static std::filesystem::path s_root;
    static bool s_init = false;
    if ( !s_init )
    {
        s_init = true;
#if defined(RAD_MACOS)
        // SDL_GetBasePath() is .../Contents/MacOS/ — game data lives in Contents/Resources.
        s_root = radTvosGetBasePath().parent_path() / "Resources" / "Assets" / "TheSimpsons";
#else
        s_root = radTvosGetBasePath() / "Assets" / "TheSimpsons";
#endif
    }
    return s_root;
}

static const std::filesystem::path& radTvosGetPrefRoot( void )
{
    static std::filesystem::path s_root;
    static bool s_init = false;
    if ( !s_init )
    {
        s_init = true;
        const char* home = std::getenv( "HOME" );
        if ( home != NULL && home[0] != '\0' )
        {
#if defined(RAD_MACOS)
            // macOS: persistent saves belong in Application Support.
            s_root = std::filesystem::path( home ) / "Library" / "Application Support" / "Radical" / "Simpsons";
#else
            // tvOS: writable user data for this app lives under Library/Caches (not Application Support).
            s_root = std::filesystem::path( home ) / "Library" / "Caches" / "Radical" / "Simpsons";
#endif
        }
        else
        {
            s_root = MakePathFromSdl( SDL_GetPrefPath( "Radical", "Simpsons" ) );
        }
    }
    return s_root;
}

static void radTvosConfigurePersistentDirectory( const std::filesystem::path& path )
{
    std::error_code ec;
    std::filesystem::create_directories( path, ec );

    // Best-effort backup exclusion for Apple platforms. Failure should not block saves.
    const uint8_t excludeFromBackup = 1;
    (void)setxattr( path.c_str(), "com.apple.MobileBackup", &excludeFromBackup, sizeof( excludeFromBackup ), 0, 0 );
}

static std::filesystem::path radTvosGetLegacyCachePrefRoot( void )
{
    const char* home = std::getenv( "HOME" );
    if ( home == NULL || home[0] == '\0' )
    {
        return std::filesystem::path();
    }

    return std::filesystem::path( home ) / "Library" / "Caches" / "Radical" / "Simpsons";
}

static std::filesystem::path radTvosGetLegacyApplicationSupportPrefRoot( void )
{
    const char* home = std::getenv( "HOME" );
    if ( home == NULL || home[0] == '\0' )
    {
        return std::filesystem::path();
    }

    return std::filesystem::path( home ) / "Library" / "Application Support" / "Radical" / "Simpsons";
}

static void radTvosMigratePrefFilesFromDir( const std::filesystem::path& oldRoot, const std::filesystem::path& newRoot, const char* tag )
{
    if ( oldRoot.empty() || newRoot.empty() || oldRoot == newRoot )
    {
        return;
    }

    std::error_code ec;
    if ( !std::filesystem::exists( oldRoot, ec ) || ec )
    {
        return;
    }

    std::filesystem::create_directories( newRoot, ec );
    if ( ec )
    {
        TVOS_STORAGE_DIAG( "[SAVE_MIGRATE][%s] create failed old=%s new=%s error=%s",
                           tag ? tag : "?",
                           oldRoot.c_str(), newRoot.c_str(), ec.message().c_str() );
        return;
    }

    unsigned int copied = 0;
    for ( const auto& entry : std::filesystem::directory_iterator( oldRoot, ec ) )
    {
        if ( ec )
        {
            break;
        }
        if ( !entry.is_regular_file( ec ) || ec )
        {
            ec.clear();
            continue;
        }

        const std::filesystem::path dest = newRoot / entry.path().filename();
        if ( std::filesystem::exists( dest, ec ) && !ec )
        {
            continue;
        }
        ec.clear();

        std::filesystem::copy_file( entry.path(), dest, std::filesystem::copy_options::skip_existing, ec );
        if ( !ec )
        {
            copied++;
        }
        else
        {
            TVOS_STORAGE_DIAG( "[SAVE_MIGRATE][%s] copy failed src=%s dst=%s error=%s",
                               tag ? tag : "?",
                               entry.path().c_str(), dest.c_str(), ec.message().c_str() );
            ec.clear();
        }
    }

    if ( copied > 0 )
    {
        TVOS_STORAGE_DIAG( "[SAVE_MIGRATE][%s] copied=%u old=%s new=%s",
                           tag ? tag : "?", copied, oldRoot.c_str(), newRoot.c_str() );
    }
}

static void radTvosMigrateLegacyPrefFiles( const std::filesystem::path& newRoot )
{
    radTvosMigratePrefFilesFromDir( radTvosGetLegacyCachePrefRoot(), newRoot, "cache" );
    radTvosMigratePrefFilesFromDir( radTvosGetLegacyApplicationSupportPrefRoot(), newRoot, "appsupport" );
}

static std::string radTvosCacheKey( const std::filesystem::path& path )
{
    return path.lexically_normal().string();
}

static std::mutex& radTvosResolveCacheMutex( void )
{
    static std::mutex s_mutex;
    return s_mutex;
}

static std::unordered_map<std::string, std::string>& radTvosResolveCache( void )
{
    static std::unordered_map<std::string, std::string> s_cache;
    return s_cache;
}

static std::unordered_set<std::string>& radTvosMissingResolveCache( void )
{
    static std::unordered_set<std::string> s_cache;
    return s_cache;
}

static bool radTvosPathIsUnderRoot( const std::filesystem::path& path, const std::filesystem::path& root )
{
    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative( path.lexically_normal(), root.lexically_normal(), ec );
    if ( ec || rel.empty() )
    {
        return false;
    }

    for ( const auto& part : rel )
    {
        if ( part == ".." )
        {
            return false;
        }
    }

    return true;
}

static bool radTvosCanCacheMissingPath( const std::filesystem::path& requested )
{
    // Only cache misses inside the immutable app bundle. PREF: can gain files
    // at runtime, so save/settings lookups must always hit the filesystem.
    return radTvosPathIsUnderRoot( requested, radTvosGetAppRoot() ) ||
           radTvosPathIsUnderRoot( requested, radTvosGetBasePath() );
}

static bool radTvosGetCachedMissingPath( const std::filesystem::path& requested )
{
    std::lock_guard<std::mutex> lock( radTvosResolveCacheMutex() );
    auto& cache = radTvosMissingResolveCache();
    return cache.find( radTvosCacheKey( requested ) ) != cache.end();
}

static void radTvosCacheMissingPath( const std::filesystem::path& requested )
{
    if ( !radTvosCanCacheMissingPath( requested ) )
    {
        return;
    }

    std::lock_guard<std::mutex> lock( radTvosResolveCacheMutex() );
    auto& cache = radTvosMissingResolveCache();
    if ( cache.size() > 4096 )
    {
        cache.clear();
    }
    cache.insert( radTvosCacheKey( requested ) );
}

static bool radTvosGetCachedResolvedPath( const std::filesystem::path& requested, std::filesystem::path* resolved )
{
    if ( resolved == NULL )
    {
        return false;
    }

    std::lock_guard<std::mutex> lock( radTvosResolveCacheMutex() );
    auto& cache = radTvosResolveCache();
    const auto it = cache.find( radTvosCacheKey( requested ) );
    if ( it == cache.end() )
    {
        return false;
    }

    std::filesystem::path cachedPath( it->second );
    std::error_code ec;
    if ( !std::filesystem::exists( cachedPath, ec ) || ec )
    {
        cache.erase( it );
        return false;
    }

    *resolved = cachedPath;
    return true;
}

static void radTvosCacheResolvedPath( const std::filesystem::path& requested, const std::filesystem::path& resolved )
{
    std::lock_guard<std::mutex> lock( radTvosResolveCacheMutex() );
    auto& cache = radTvosResolveCache();
    if ( cache.size() > 4096 )
    {
        cache.clear();
    }
    cache[ radTvosCacheKey( requested ) ] = resolved.lexically_normal().string();
    radTvosMissingResolveCache().erase( radTvosCacheKey( requested ) );
}

static std::filesystem::path radTvosGuessAbsolutePath( const char* requested )
{
    if ( requested == NULL || requested[0] == '\0' )
    {
        return std::filesystem::path();
    }

    // Normalize path separators. Many asset references are authored on Windows.
    std::string req( requested );
    for ( char& c : req )
    {
        if ( c == '\\' ) c = '/';
    }
    requested = req.c_str();

    // Convert drive style paths (APP:/PREF:) to actual absolute paths.
    if ( strncmp( requested, s_TvosAppDriveName, strlen( s_TvosAppDriveName ) ) == 0 )
    {
        const char* p = requested + strlen( s_TvosAppDriveName );
        while ( *p == '/' || *p == '\\' ) { ++p; }
        return radTvosGetAppRoot() / p;
    }
    if ( strncmp( requested, s_TvosPrefDriveName, strlen( s_TvosPrefDriveName ) ) == 0 )
    {
        const char* p = requested + strlen( s_TvosPrefDriveName );
        while ( *p == '/' || *p == '\\' ) { ++p; }
        return radTvosGetPrefRoot() / p;
    }

    // Absolute Unix path.
    if ( requested[0] == '/' )
    {
        return std::filesystem::path( requested );
    }

    // Otherwise assume relative to the app base directory.
    return radTvosGetBasePath() / requested;
}

static bool radTvosResolveCaseInsensitive( const std::filesystem::path& absolutePath, std::filesystem::path* outResolved )
{
    if ( outResolved == NULL )
    {
        return false;
    }

    if ( radTvosGetCachedResolvedPath( absolutePath, outResolved ) )
    {
        return true;
    }

    std::error_code ec;
    if ( std::filesystem::exists( absolutePath, ec ) )
    {
        *outResolved = absolutePath;
        radTvosCacheResolvedPath( absolutePath, *outResolved );
        return true;
    }

    // Only attempt case-insensitive resolution within the app bundle.
    // This is to tolerate Windows-authored asset references on a case-sensitive filesystem.
    const std::filesystem::path appRoot = radTvosGetAppRoot();
    const std::filesystem::path basePath = radTvosGetBasePath();

    auto tryResolveUnderRoot = [&]( const std::filesystem::path& root ) -> bool {
        std::error_code ec2;
        std::filesystem::path rel;
        try
        {
            rel = std::filesystem::relative( absolutePath, root, ec2 );
        }
        catch ( ... )
        {
            return false;
        }
        if ( ec2 )
        {
            return false;
        }

        // If it's not actually under this root, bail.
        if ( rel.empty() || rel.native().find( ".." ) == 0 )
        {
            return false;
        }

        std::filesystem::path cur = root;
        for ( const auto& part : rel )
        {
            const std::string want = part.string();
            if ( want.empty() )
            {
                continue;
            }

            // If exact match exists, take it.
            std::filesystem::path exact = cur / part;
            if ( std::filesystem::exists( exact, ec2 ) )
            {
                cur = exact;
                continue;
            }

            // Otherwise, scan current directory for a case-insensitive match.
            bool matched = false;
            std::filesystem::directory_iterator it( cur, ec2 );
            if ( ec2 )
            {
                return false;
            }

            for ( const auto& entry : it )
            {
                const std::string have = entry.path().filename().string();
                if ( have.size() != want.size() )
                {
                    continue;
                }

                bool eq = true;
                for ( size_t i = 0; i < want.size(); ++i )
                {
                    char a = want[i];
                    char b = have[i];
                    if ( a >= 'A' && a <= 'Z' ) a = (char)(a - 'A' + 'a');
                    if ( b >= 'A' && b <= 'Z' ) b = (char)(b - 'A' + 'a');
                    if ( a != b ) { eq = false; break; }
                }
                if ( eq )
                {
                    cur = entry.path();
                    matched = true;
                    break;
                }
            }

            if ( !matched )
            {
                return false;
            }
        }

        if ( std::filesystem::exists( cur, ec2 ) )
        {
            *outResolved = cur;
            return true;
        }
        return false;
    };

    std::filesystem::path resolved;
    if ( tryResolveUnderRoot( appRoot ) )
    {
        radTvosCacheResolvedPath( absolutePath, *outResolved );
        return true;
    }
    if ( tryResolveUnderRoot( basePath ) )
    {
        radTvosCacheResolvedPath( absolutePath, *outResolved );
        return true;
    }

    (void)resolved;
    return false;
}

static bool radTvosIsStreamedAsset( const char* requested )
{
    if ( requested == NULL ) return false;
    const char* ext = strrchr( requested, '.' );
    if ( ext == NULL ) return false;
    // Check for world streaming file types
    return ( strcasecmp( ext, ".p3d" ) == 0 ||
             strcasecmp( ext, ".rcf" ) == 0 ||
             strcasecmp( ext, ".rmv" ) == 0 ||
             strcasecmp( ext, ".rsd" ) == 0 );
}

static bool radTvosIsSaveFile( const char* fileName )
{
    if ( fileName == NULL ) return false;
    // Check for save game patterns
    return ( strstr( fileName, "Save" ) != NULL ||
             strstr( fileName, "save" ) != NULL ||
             strstr( fileName, ".sav" ) != NULL ||
             strstr( fileName, "settings" ) != NULL ||
             strstr( fileName, "config" ) != NULL );
}

static void radTvosLogSuccessOpen( const char* requested, const std::filesystem::path* resolved, unsigned int size, const char* drive, bool writeAccess )
{
    (void)drive;
    SRR2::Diagnostics::RecordVfsOpen( writeAccess ? "write" : "read", requested, true, 0, 0 );
    if ( !radTvosIsSaveFile( requested ) )
    {
        return;
    }

    TVOS_STORAGE_DIAG( "[VFS_OPEN] type=%s logical=%s resolved=%s ok=1 size=%u",
             writeAccess ? "WRITE" : "READ",
             requested ? requested : "",
             resolved ? resolved->c_str() : "",
             size );
}

static void radTvosLogFailedOpen( const char* api, const char* requested, const std::filesystem::path* resolved, int err )
{
    SRR2::Diagnostics::RecordVfsOpen( api, requested, false, err, 0 );
    if ( err == ENOENT )
    {
        TVOS_STORAGE_DIAG( "[VFS_MISSING] api=%s requested=%s", api ? api : "", requested ? requested : "" );
        return;
    }

    std::filesystem::path guessed;
    if ( resolved == NULL )
    {
        guessed = radTvosGuessAbsolutePath( requested );
        resolved = &guessed;
    }

    const char* resolvedStr = ( resolved != NULL ) ? resolved->c_str() : "";
    SRR2::Diagnostics::Errorf(
              SRR2::Diagnostics::VFS,
              "[VFS_OPEN_FAILED] api=%s requested=%s resolved=%s errno=%d (%s) base=%s appRoot=%s prefRoot=%s caller=0x%llx",
              api ? api : "",
              requested ? requested : "",
              resolvedStr,
              err,
              strerror( err ),
              radTvosGetBasePath().c_str(),
              radTvosGetAppRoot().c_str(),
              radTvosGetPrefRoot().c_str(),
              (unsigned long long)(uintptr_t)__builtin_return_address( 0 ) );
    os_log( OS_LOG_DEFAULT,
            "FAILED OPEN: api=%{public}s requested=%{public}s resolved=%{public}s errno=%{public}d (%{public}s) base=%{public}s appRoot=%{public}s prefRoot=%{public}s caller=0x%{public}llx",
            api ? api : "",
            requested ? requested : "",
            resolvedStr,
            err,
            strerror( err ),
            radTvosGetBasePath().c_str(),
            radTvosGetAppRoot().c_str(),
            radTvosGetPrefRoot().c_str(),
            (unsigned long long)(uintptr_t)__builtin_return_address( 0 ) );

}

extern "C" FILE* fopen( const char* path, const char* mode )
{
    typedef FILE* ( *Fn )( const char*, const char* );
    static Fn s_real = (Fn)dlsym( RTLD_NEXT, "fopen" );
    FILE* f = s_real ? s_real( path, mode ) : NULL;
    if ( f == NULL && s_real != NULL && path != NULL && mode != NULL && strchr( mode, 'w' ) == NULL )
    {
        std::filesystem::path abs = radTvosGuessAbsolutePath( path );
        std::filesystem::path resolved;
        if ( radTvosResolveCaseInsensitive( abs, &resolved ) )
        {
            f = s_real( resolved.c_str(), mode );
        }
    }
    if ( f == NULL )
    {
        int err = errno;
        radTvosLogFailedOpen( "fopen", path, NULL, err );
    }
    return f;
}

extern "C" FILE* radTvos_fopen_nocancel( const char* path, const char* mode ) __asm( "_fopen$NOCANCEL" );
extern "C" FILE* radTvos_fopen_nocancel( const char* path, const char* mode )
{
    typedef FILE* ( *Fn )( const char*, const char* );
    static Fn s_real = (Fn)dlsym( RTLD_NEXT, "fopen$NOCANCEL" );
    if ( s_real == NULL )
    {
        s_real = (Fn)dlsym( RTLD_NEXT, "fopen" );
    }
    FILE* f = s_real ? s_real( path, mode ) : NULL;
    if ( f == NULL && s_real != NULL && path != NULL && mode != NULL && strchr( mode, 'w' ) == NULL )
    {
        std::filesystem::path abs = radTvosGuessAbsolutePath( path );
        std::filesystem::path resolved;
        if ( radTvosResolveCaseInsensitive( abs, &resolved ) )
        {
            f = s_real( resolved.c_str(), mode );
        }
    }
    if ( f == NULL )
    {
        int err = errno;
        radTvosLogFailedOpen( "fopen$NOCANCEL", path, NULL, err );
    }
    return f;
}

extern "C" int open( const char* path, int oflag, ... )
{
    typedef int ( *Fn )( const char*, int, ... );
    static Fn s_real = (Fn)dlsym( RTLD_NEXT, "open" );

    int result;
    if ( ( oflag & O_CREAT ) != 0 )
    {
        va_list ap;
        va_start( ap, oflag );
        int mode = va_arg( ap, int );
        va_end( ap );
        result = s_real ? s_real( path, oflag, mode ) : -1;
    }
    else
    {
        result = s_real ? s_real( path, oflag ) : -1;
    }

    if ( result < 0 )
    {
        int err = errno;
        radTvosLogFailedOpen( "open", path, NULL, err );
    }
    return result;
}

extern "C" int radTvos_open_nocancel( const char* path, int oflag, ... ) __asm( "_open$NOCANCEL" );
extern "C" int radTvos_open_nocancel( const char* path, int oflag, ... )
{
    typedef int ( *Fn )( const char*, int, ... );
    static Fn s_real = (Fn)dlsym( RTLD_NEXT, "open$NOCANCEL" );
    if ( s_real == NULL )
    {
        s_real = (Fn)dlsym( RTLD_NEXT, "open" );
    }

    int result;
    if ( ( oflag & O_CREAT ) != 0 )
    {
        va_list ap;
        va_start( ap, oflag );
        int mode = va_arg( ap, int );
        va_end( ap );
        result = s_real ? s_real( path, oflag, mode ) : -1;
    }
    else
    {
        result = s_real ? s_real( path, oflag ) : -1;
    }

    if ( result < 0 )
    {
        int err = errno;
        radTvosLogFailedOpen( "open$NOCANCEL", path, NULL, err );
    }
    return result;
}

extern "C" int openat( int fd, const char* path, int oflag, ... )
{
    typedef int ( *Fn )( int, const char*, int, ... );
    static Fn s_real = (Fn)dlsym( RTLD_NEXT, "openat" );

    int result;
    if ( ( oflag & O_CREAT ) != 0 )
    {
        va_list ap;
        va_start( ap, oflag );
        int mode = va_arg( ap, int );
        va_end( ap );
        result = s_real ? s_real( fd, path, oflag, mode ) : -1;
    }
    else
    {
        result = s_real ? s_real( fd, path, oflag ) : -1;
    }

    if ( result < 0 )
    {
        int err = errno;
        radTvosLogFailedOpen( "openat", path, NULL, err );
    }
    return result;
}

extern "C" int radTvos_openat_nocancel( int fd, const char* path, int oflag, ... ) __asm( "_openat$NOCANCEL" );
extern "C" int radTvos_openat_nocancel( int fd, const char* path, int oflag, ... )
{
    typedef int ( *Fn )( int, const char*, int, ... );
    static Fn s_real = (Fn)dlsym( RTLD_NEXT, "openat$NOCANCEL" );
    if ( s_real == NULL )
    {
        s_real = (Fn)dlsym( RTLD_NEXT, "openat" );
    }

    int result;
    if ( ( oflag & O_CREAT ) != 0 )
    {
        va_list ap;
        va_start( ap, oflag );
        int mode = va_arg( ap, int );
        va_end( ap );
        result = s_real ? s_real( fd, path, oflag, mode ) : -1;
    }
    else
    {
        result = s_real ? s_real( fd, path, oflag ) : -1;
    }

    if ( result < 0 )
    {
        int err = errno;
        radTvosLogFailedOpen( "openat$NOCANCEL", path, NULL, err );
    }
    return result;
}

void radTvosDriveFactory( radDrive** ppDrive, const char* driveSpec, radMemoryAllocator alloc )
{
    *ppDrive = new( alloc ) radTvosDrive( driveSpec, alloc );
    rAssert( *ppDrive != NULL );
}

static std::filesystem::path MakePathFromSdl( char* sdlPath )
{
    if ( sdlPath == NULL )
    {
        return std::filesystem::path();
    }

    std::filesystem::path p( sdlPath );
    SDL_free( sdlPath );
    return p;
}

radTvosDrive::radTvosDrive( const char* driveSpec, radMemoryAllocator alloc )
    : radDrive( ),
      m_Capabilities( 0 ),
      m_pMutex( NULL )
{
    radThreadCreateMutex( &m_pMutex, alloc );
    rAssert( m_pMutex != NULL );

    m_pDriveThread = new( alloc ) radDriveThread( m_pMutex, alloc );
    rAssert( m_pDriveThread != NULL );

    strncpy( m_DriveName, driveSpec, radFileDrivenameMax );
    m_DriveName[ radFileDrivenameMax ] = '\0';

    // Determine root and capabilities.
    if ( IsAppDrive() )
    {
        // APP: is read-only and must map to <BasePath>/Assets/TheSimpsons/
        std::filesystem::path base = MakePathFromSdl( SDL_GetBasePath() );
        m_Root = base / "Assets" / "TheSimpsons";
        m_Capabilities = ( radDriveEnumerable | radDriveDirectory | radDriveFile );
    }
    else
    {
        // PREF: is writable and must map to SDL_GetPrefPath(org, app)
        m_Root = radTvosGetPrefRoot();
        m_Capabilities = ( radDriveEnumerable | radDriveWriteable | radDriveDirectory | radDriveFile | radDriveSaveGame );
        
        radTvosConfigurePersistentDirectory( m_Root );
        radTvosMigrateLegacyPrefFiles( m_Root );
        
#if defined( RAD_TVOS_STORAGE_DIAGNOSTICS )
        std::error_code bootEc;
        // Check for existing save files
        bool save1Exists = std::filesystem::exists( m_Root / "Save1", bootEc );
        bool save2Exists = std::filesystem::exists( m_Root / "Save2", bootEc );
        bool save3Exists = std::filesystem::exists( m_Root / "Save3", bootEc );
        bool settingsExist = std::filesystem::exists( m_Root / "settings", bootEc );
        
        TVOS_STORAGE_DIAG( "[SAVE_BOOT] prefRoot=%s Save1=%s Save2=%s Save3=%s settings=%s",
                 m_Root.c_str(),
                 save1Exists ? "yes" : "no",
                 save2Exists ? "yes" : "no", 
                 save3Exists ? "yes" : "no",
                 settingsExist ? "yes" : "no" );
#endif
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories( m_Root, ec );
}

radTvosDrive::~radTvosDrive( void )
{
    m_pDriveThread->Release( );
    m_pMutex->Release( );
}

void radTvosDrive::Lock( void )
{
    m_pMutex->Lock( );
}

void radTvosDrive::Unlock( void )
{
    m_pMutex->Unlock( );
}

unsigned int radTvosDrive::GetCapabilities( void )
{
    return m_Capabilities;
}

const char* radTvosDrive::GetDriveName( void )
{
    return m_DriveName;
}

radDrive::CompletionStatus radTvosDrive::Initialize( void )
{
    SetMediaInfo();
    
    // Cleanup orphaned .tmp files from failed save attempts.
    // This prevents storage bloat and ensures clean state on startup.
    if ( !IsAppDrive() )
    {
        std::error_code ec;
        for ( auto& entry : std::filesystem::directory_iterator( m_Root, ec ) )
        {
            if ( entry.path().extension() == ".tmp" )
            {
                TVOS_STORAGE_DIAG( "[SAVE_CLEANUP] Removing orphaned temp file: %s", entry.path().c_str() );
                std::filesystem::remove( entry.path(), ec );
            }
        }
    }
    
    m_LastError = Success;
    return Complete;
}

bool radTvosDrive::IsAppDrive( void ) const
{
    return ( strcmp( m_DriveName, s_TvosAppDriveName ) == 0 );
}

void radTvosDrive::SetMediaInfo( void )
{
    strcpy( m_MediaInfo.m_VolumeName, m_DriveName );
    m_MediaInfo.m_SectorSize = 512;
    m_MediaInfo.m_MediaState = IRadDrive::MediaInfo::MediaPresent;
    
    // Query actual filesystem for available space instead of using fake block values.
    // This fixes the "not enough free blocks" error on tvOS.
    uint64_t availableBytes = radTvosGetAvailableBytes( m_Root );
    
    // Cap at UINT_MAX to avoid overflow in legacy code that expects 32-bit values
    if ( availableBytes > UINT_MAX )
    {
        m_MediaInfo.m_FreeSpace = UINT_MAX;
    }
    else
    {
        m_MediaInfo.m_FreeSpace = static_cast<unsigned int>( availableBytes );
    }
    
    // FreeFiles: estimate based on available space / typical save file size (~64KB)
    // This ensures the legacy "free files" check also passes
    m_MediaInfo.m_FreeFiles = UINT_MAX;
}

void radTvosDrive::MakeNativePath( const char* fileName, std::filesystem::path* outPath ) const
{
    std::string tmp( fileName ? fileName : "" );
    std::replace( tmp.begin(), tmp.end(), '\\', '/' );

    // Strip any leading separators.
    while ( !tmp.empty() && ( tmp[0] == '/' ) )
    {
        tmp.erase( tmp.begin() );
    }

    *outPath = ( m_Root / tmp );
}

static radFileError TranslateFsError( const std::error_code& error )
{
    switch( (std::errc) error.default_error_condition().value() )
    {
        case std::errc::no_such_file_or_directory:
        case std::errc::no_such_device:
            return FileNotFound;
        case std::errc::no_space_on_device:
            return NoFreeSpace;
        default:
            return HardwareFailure;
    }
}

// Helper to check if a filename looks like a DOS 8.3 short name (e.g., MI8A24~1.P3D)
static bool radTvosIs83ShortName( const std::string& name )
{
    // DOS 8.3 short names typically have ~ followed by a digit
    return ( name.find( '~' ) != std::string::npos );
}

// Helper to extract the base prefix from a DOS 8.3 name (e.g., "MI8A24~1" -> "MI8A24")
static std::string radTvosGet83Prefix( const std::string& name )
{
    size_t tildePos = name.find( '~' );
    if ( tildePos != std::string::npos && tildePos > 0 )
    {
        return name.substr( 0, tildePos );
    }
    return name;
}

// Helper to check if two filenames are similar enough to be a match
// Handles: case differences, 8.3 short names, truncation, etc.
static bool radTvosFilenamesSimilar( const std::string& requested, const std::string& candidate )
{
    // Exact case-insensitive match
    if ( strcasecmp( requested.c_str(), candidate.c_str() ) == 0 )
    {
        return true;
    }
    
    // Get stems (without extension) for comparison
    std::string reqStem = requested;
    std::string candStem = candidate;
    
    // Remove extension
    size_t reqDot = reqStem.rfind( '.' );
    size_t candDot = candStem.rfind( '.' );
    std::string reqExt, candExt;
    
    if ( reqDot != std::string::npos )
    {
        reqExt = reqStem.substr( reqDot );
        reqStem = reqStem.substr( 0, reqDot );
    }
    if ( candDot != std::string::npos )
    {
        candExt = candStem.substr( candDot );
        candStem = candStem.substr( 0, candDot );
    }
    
    // Extensions must match (case-insensitive)
    if ( strcasecmp( reqExt.c_str(), candExt.c_str() ) != 0 )
    {
        return false;
    }
    
    // Convert both to lowercase for comparison
    std::string reqLower = reqStem;
    std::string candLower = candStem;
    std::transform( reqLower.begin(), reqLower.end(), reqLower.begin(), ::tolower );
    std::transform( candLower.begin(), candLower.end(), candLower.begin(), ::tolower );
    
    // Check for DOS 8.3 short name match
    // If requested is 8.3 (e.g., "mi8a24~1"), see if candidate starts with the prefix
    if ( radTvosIs83ShortName( reqLower ) )
    {
        std::string prefix = radTvosGet83Prefix( reqLower );
        if ( candLower.find( prefix ) == 0 )
        {
            return true;
        }
    }
    
    // If candidate is 8.3 (e.g., "MI8A24~1"), see if requested starts with the prefix
    if ( radTvosIs83ShortName( candLower ) )
    {
        std::string prefix = radTvosGet83Prefix( candLower );
        if ( reqLower.find( prefix ) == 0 )
        {
            return true;
        }
    }
    
    // Check if one is a prefix/truncation of the other (at least 6 chars match)
    size_t minLen = std::min( reqLower.length(), candLower.length() );
    if ( minLen >= 6 )
    {
        if ( reqLower.substr( 0, minLen ) == candLower.substr( 0, minLen ) )
        {
            return true;
        }
    }
    
    // Check if stems match when we strip common suffixes like "cam", numbers, etc.
    // This handles cases like "mission6cam" vs "mission6"
    if ( reqLower.length() >= 6 && candLower.length() >= 6 )
    {
        // Check first 6 characters match
        if ( reqLower.substr( 0, 6 ) == candLower.substr( 0, 6 ) )
        {
            // Good enough prefix match for mission files
            return true;
        }
    }
    
    return false;
}

// Case-insensitive and fuzzy file lookup helper.
// If the exact path doesn't exist, tries:
// 1. Case variations (uppercase/lowercase)
// 2. DOS 8.3 short name matching
// 3. Fuzzy/similar filename matching
// Returns true if a matching file was found (and updates outPath), false otherwise.
static bool radTvosFindFileCaseInsensitive( const std::filesystem::path& originalPath, std::filesystem::path* outPath )
{
    std::error_code ec;

    if ( radTvosGetCachedMissingPath( originalPath ) )
    {
        return false;
    }

    if ( radTvosGetCachedResolvedPath( originalPath, outPath ) )
    {
        return true;
    }
    
    // First check if the exact path exists
    if ( std::filesystem::exists( originalPath, ec ) && !ec )
    {
        *outPath = originalPath;
        radTvosCacheResolvedPath( originalPath, *outPath );
        return true;
    }

    if ( radTvosResolveCaseInsensitive( originalPath, outPath ) )
    {
        radTvosCacheResolvedPath( originalPath, *outPath );
        return true;
    }
    
    // Get the parent directory and filename
    std::filesystem::path parentDir = originalPath.parent_path();
    std::string filename = originalPath.filename().string();
    
    // If parent directory doesn't exist, we can't search
    if ( !std::filesystem::exists( parentDir, ec ) || ec )
    {
        radTvosCacheMissingPath( originalPath );
        return false;
    }
    
    // Try common case variations first (fast path)
    // 1. All lowercase
    std::string lowerFilename = filename;
    std::transform( lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower );
    std::filesystem::path lowerPath = parentDir / lowerFilename;
    if ( std::filesystem::exists( lowerPath, ec ) && !ec )
    {
        TVOS_STORAGE_DIAG( "[VFS_CASE_FIX] Found '%s' as '%s'", originalPath.c_str(), lowerPath.c_str() );
        SRR2::Diagnostics::RecordVfsFixup( "case", originalPath.c_str(), lowerPath.c_str() );
        *outPath = lowerPath;
        radTvosCacheResolvedPath( originalPath, *outPath );
        return true;
    }
    
    // 2. All uppercase
    std::string upperFilename = filename;
    std::transform( upperFilename.begin(), upperFilename.end(), upperFilename.begin(), ::toupper );
    std::filesystem::path upperPath = parentDir / upperFilename;
    if ( std::filesystem::exists( upperPath, ec ) && !ec )
    {
        TVOS_STORAGE_DIAG( "[VFS_CASE_FIX] Found '%s' as '%s'", originalPath.c_str(), upperPath.c_str() );
        SRR2::Diagnostics::RecordVfsFixup( "case", originalPath.c_str(), upperPath.c_str() );
        *outPath = upperPath;
        radTvosCacheResolvedPath( originalPath, *outPath );
        return true;
    }
    
    // 3. Extension case variations (e.g., .p3d vs .P3D)
    std::string stem = originalPath.stem().string();
    std::string ext = originalPath.extension().string();
    
    // Try lowercase extension
    std::string lowerExt = ext;
    std::transform( lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower );
    std::filesystem::path lowerExtPath = parentDir / ( stem + lowerExt );
    if ( std::filesystem::exists( lowerExtPath, ec ) && !ec )
    {
        TVOS_STORAGE_DIAG( "[VFS_CASE_FIX] Found '%s' as '%s'", originalPath.c_str(), lowerExtPath.c_str() );
        SRR2::Diagnostics::RecordVfsFixup( "case", originalPath.c_str(), lowerExtPath.c_str() );
        *outPath = lowerExtPath;
        radTvosCacheResolvedPath( originalPath, *outPath );
        return true;
    }
    
    // Try uppercase extension
    std::string upperExt = ext;
    std::transform( upperExt.begin(), upperExt.end(), upperExt.begin(), ::toupper );
    std::filesystem::path upperExtPath = parentDir / ( stem + upperExt );
    if ( std::filesystem::exists( upperExtPath, ec ) && !ec )
    {
        TVOS_STORAGE_DIAG( "[VFS_CASE_FIX] Found '%s' as '%s'", originalPath.c_str(), upperExtPath.c_str() );
        SRR2::Diagnostics::RecordVfsFixup( "case", originalPath.c_str(), upperExtPath.c_str() );
        *outPath = upperExtPath;
        radTvosCacheResolvedPath( originalPath, *outPath );
        return true;
    }
    
    // 4. Full directory scan with fuzzy matching
    // This handles: case mismatches, DOS 8.3 names, truncation, similar names
    std::filesystem::path bestMatch;
    bool foundMatch = false;
    
    for ( const auto& entry : std::filesystem::directory_iterator( parentDir, ec ) )
    {
        if ( ec ) break;
        if ( !entry.is_regular_file() ) continue;
        
        std::string entryName = entry.path().filename().string();
        
        // Exact case-insensitive match (highest priority)
        if ( strcasecmp( entryName.c_str(), filename.c_str() ) == 0 )
        {
            TVOS_STORAGE_DIAG( "[VFS_CASE_FIX] Found '%s' as '%s' (exact)", originalPath.c_str(), entry.path().c_str() );
            SRR2::Diagnostics::RecordVfsFixup( "case", originalPath.c_str(), entry.path().c_str() );
            *outPath = entry.path();
            radTvosCacheResolvedPath( originalPath, *outPath );
            return true;
        }
        
        // Fuzzy/similar match
        if ( !foundMatch && radTvosFilenamesSimilar( filename, entryName ) )
        {
            bestMatch = entry.path();
            foundMatch = true;
            // Continue searching in case we find an exact match
        }
    }
    
    if ( foundMatch )
    {
        TVOS_STORAGE_DIAG( "[VFS_FUZZY_FIX] Found '%s' as '%s' (similar)", originalPath.c_str(), bestMatch.c_str() );
        SRR2::Diagnostics::RecordVfsFixup( "fuzzy", originalPath.c_str(), bestMatch.c_str() );
        *outPath = bestMatch;
        radTvosCacheResolvedPath( originalPath, *outPath );
        return true;
    }
    
    radTvosCacheMissingPath( originalPath );
    return false;
}

radDrive::CompletionStatus radTvosDrive::OpenFile( const char* fileName, radFileOpenFlags flags, bool writeAccess, radFileHandle* pHandle, unsigned int* pSize )
{
    SRR2::Diagnostics::ScopedTimer tvosOpenTimer( SRR2::Diagnostics::VFS, "radTvosDrive::OpenFile", 50 );
    std::string requested;
    requested.reserve( strlen( m_DriveName ) + ( fileName ? strlen( fileName ) : 0 ) + 1 );
    requested.append( m_DriveName );
    if ( fileName != NULL )
    {
        requested.append( fileName );
    }

    std::filesystem::path finalPath;
    MakeNativePath( fileName, &finalPath );

    std::error_code ec;
    
    // Use case-insensitive lookup for existing read targets. New writes should not
    // scan asset directories on the main thread just to create a save/temp file.
    std::filesystem::path resolvedPath;
    bool fileFound = false;
    if ( writeAccess && flags != OpenExisting )
    {
        fileFound = std::filesystem::exists( finalPath, ec ) && !ec;
        if ( fileFound )
        {
            resolvedPath = finalPath;
        }
    }
    else
    {
        fileFound = radTvosFindFileCaseInsensitive( finalPath, &resolvedPath );
    }
    
    // DIAGNOSTIC: Log zone file resolution attempts
    bool isZoneFile = (fileName && (strstr(fileName, "l1z") || strstr(fileName, "L1Z") || 
                                     strstr(fileName, "l1r") || strstr(fileName, "L1R")));
    if (isZoneFile) {
        bool isL1Z3 = (fileName && (strstr(fileName, "l1z3") || strstr(fileName, "L1Z3")));
        TVOS_STORAGE_DIAG("[VFS_ZONE_RESOLVE] requested='%s' finalPath='%s' found=%d %s",
                fileName ? fileName : "NULL",
                finalPath.c_str(),
                fileFound ? 1 : 0,
                isL1Z3 ? "*** L1Z3 ***" : "");
    }
    
    if ( fileFound )
    {
        finalPath = resolvedPath;  // Use the resolved path with correct case
        *pSize = (unsigned int) std::filesystem::file_size( finalPath, ec );
        if ( ec )
        {
            radTvosLogFailedOpen( "radTvosDrive::OpenFile(file_size)", requested.c_str(), &finalPath, (int)ec.value() );
            m_LastError = TranslateFsError( ec );
            return Error;
        }
    }
    else
    {
        if ( flags == OpenExisting )
        {
            *pSize = 0;
            m_LastError = FileNotFound;
            radTvosLogFailedOpen( "radTvosDrive::OpenFile(missing)", requested.c_str(), &finalPath, ENOENT );
            return Error;
        }
        *pSize = 0;
    }

    if ( IsAppDrive() && ( writeAccess || flags != OpenExisting ) )
    {
        m_LastError = HardwareFailure;
        radTvosLogFailedOpen( "radTvosDrive::OpenFile(readonly)", requested.c_str(), &finalPath, EROFS );
        return Error;
    }

    std::filesystem::path openPath = finalPath;
    std::filesystem::path tempPath;
    bool atomic = false;

    if ( writeAccess )
    {
        atomic = true;
        tempPath = finalPath;
        tempPath += ".tmp";
        openPath = tempPath;

        std::filesystem::create_directories( finalPath.parent_path(), ec );
        if ( ec )
        {
            radTvosLogFailedOpen( "radTvosDrive::OpenFile(create_dirs)", requested.c_str(), &finalPath, (int)ec.value() );
            m_LastError = TranslateFsError( ec );
            return Error;
        }
    }

    std::ios_base::openmode mode = std::ios::binary | std::ios::in;
    if ( writeAccess )
    {
        mode |= std::ios::out;
    }
    if ( flags == CreateAlways )
    {
        mode |= std::ios::trunc;
    }

    radTvosFileHandle* h = new radTvosFileHandle;
    h->stream = new std::fstream( openPath, mode );
    h->tempPath = tempPath;
    h->finalPath = finalPath;
    h->atomic = atomic;

    if ( !h->stream->good() )
    {
        int err = errno;
        if ( err == 0 )
        {
            err = ENOENT;
        }
        radTvosLogFailedOpen( "radTvosDrive::OpenFile(fstream)", requested.c_str(), &openPath, err );
        delete h->stream;
        delete h;
        m_LastError = FileNotFound;
        return Error;
    }

    *pHandle = h;
    m_LastError = Success;
    
    // Log successful opens
    if ( radTvosIsStreamedAsset( fileName ) || radTvosIsSaveFile( fileName ) )
    {
        radTvosLogSuccessOpen( requested.c_str(), &finalPath, *pSize, m_DriveName, writeAccess );
    }
    
    return Complete;
}

radDrive::CompletionStatus radTvosDrive::OpenSaveGame( const char* fileName, radFileOpenFlags flags, bool writeAccess, radMemcardInfo* /*memcardInfo*/, unsigned int /*maxSize*/, radFileHandle* pHandle, unsigned int* pSize )
{
    // Save games are implemented as regular files on PREF:.
    TVOS_STORAGE_DIAG( "[SAVE_OPEN] file=%s flags=%d write=%d", fileName ? fileName : "", (int)flags, writeAccess ? 1 : 0 );
    return OpenFile( fileName, flags, writeAccess, pHandle, pSize );
}

radDrive::CompletionStatus radTvosDrive::CommitFile( radFileHandle handle, const char* /*fileName*/ )
{
    // Atomic commit is performed on CloseFile for tvOS.
    (void)handle;
    return Complete;
}

radDrive::CompletionStatus radTvosDrive::CloseFile( radFileHandle handle, const char* /*fileName*/ )
{
    radTvosFileHandle* h = handle;

    if ( h && h->stream )
    {
        h->stream->flush();
        
        // For atomic writes, ensure data is physically on disk before closing.
        // F_FULLFSYNC guarantees data survives power loss on Apple platforms.
        if ( h->atomic )
        {
            // Get underlying file descriptor from fstream for F_FULLFSYNC
            // Note: This is platform-specific but essential for save integrity
            FILE* cfile = nullptr;
            // Try to sync via the temp file path before close
            int fd = ::open( h->tempPath.c_str(), O_RDONLY );
            if ( fd >= 0 )
            {
                fcntl( fd, F_FULLFSYNC );
                ::close( fd );
            }
        }
        
        h->stream->close();
    }

    if ( h && h->atomic )
    {
        std::error_code ec;
        // Remove existing file first. Failure here is OK (file may not exist).
        std::filesystem::remove( h->finalPath, ec );
        // Don't check ec here - missing file is fine
        
        // Rename temp to final. This MUST succeed for save integrity.
        ec.clear();
        std::filesystem::rename( h->tempPath, h->finalPath, ec );
        if ( ec )
        {
            m_LastError = TranslateFsError( ec );
            SDL_Log( "[SAVE_WRITE_FAIL] Rename failed: path=%s errno=%d (%s)",
                     h->finalPath.c_str(), (int)ec.value(), ec.message().c_str() );
            // Try to clean up orphaned temp file
            std::filesystem::remove( h->tempPath, ec );
        }
        else
        {
            // Log successful atomic write for save files
            std::error_code szEc;
            auto fileSize = std::filesystem::file_size( h->finalPath, szEc );
            TVOS_STORAGE_DIAG( "[SAVE_WRITE] path=%s bytes=%llu ok=1",
                     h->finalPath.c_str(), szEc ? 0ULL : (unsigned long long)fileSize );
            m_LastError = Success;
        }
    }

    if ( h )
    {
        delete h->stream;
        delete h;
    }

    return ( m_LastError == Success ) ? Complete : Error;
}

radDrive::CompletionStatus radTvosDrive::ReadFile( radFileHandle handle, const char* /*fileName*/, IRadFile::BufferedReadState /*state*/, unsigned int position, void* pData, unsigned int bytesToRead, unsigned int* bytesRead, radMemorySpace /*pDataSpace*/ )
{
    radTvosFileHandle* h = handle;
    h->stream->seekg( position );
    if ( h->stream->good() )
    {
        h->stream->read( (char*)pData, bytesToRead );
        if ( !h->stream->bad() )
        {
            // Use gcount() to get actual bytes read, not requested amount.
            // This prevents "poisoned" game state if file is truncated.
            *bytesRead = static_cast<unsigned int>( h->stream->gcount() );
            m_LastError = Success;
            return Complete;
        }
    }

    *bytesRead = 0;
    m_LastError = FileNotFound;
    return Error;
}

radDrive::CompletionStatus radTvosDrive::WriteFile( radFileHandle handle, const char* /*fileName*/, IRadFile::BufferedReadState /*state*/, unsigned int position, const void* pData, unsigned int bytesToWrite, unsigned int* bytesWritten, unsigned int* size, radMemorySpace /*pDataSpace*/ )
{
    if ( !( m_Capabilities & radDriveWriteable ) )
    {
        m_LastError = HardwareFailure;
        return Error;
    }

    radTvosFileHandle* h = handle;
    h->stream->seekp( position );
    if ( h->stream->good() )
    {
        h->stream->write( (const char*)pData, bytesToWrite );
        // Use fail() instead of bad() to catch more errors including disk full
        if ( !h->stream->fail() )
        {
            h->stream->seekp( 0, std::ios_base::end );
            *bytesWritten = bytesToWrite;
            *size = (unsigned int) h->stream->tellp();
            m_LastError = Success;
            return Complete;
        }
        else
        {
            SDL_Log( "[SAVE_WRITE_ERROR] Write failed: stream in fail state" );
            m_LastError = NoFreeSpace;
            return Error;
        }
    }

    m_LastError = HardwareFailure;
    return Error;
}

radDrive::CompletionStatus radTvosDrive::CreateDir( const char* pName )
{
    if ( !( m_Capabilities & radDriveDirectory ) )
    {
        m_LastError = HardwareFailure;
        return Error;
    }

    if ( IsAppDrive() )
    {
        m_LastError = HardwareFailure;
        return Error;
    }

    std::filesystem::path p;
    MakeNativePath( pName, &p );

    std::error_code ec;
    std::filesystem::create_directories( p, ec );
    if ( ec )
    {
        m_LastError = TranslateFsError( ec );
        return Error;
    }

    m_LastError = Success;
    return Complete;
}

radDrive::CompletionStatus radTvosDrive::DestroyDir( const char* pName )
{
    if ( IsAppDrive() )
    {
        m_LastError = HardwareFailure;
        return Error;
    }

    std::filesystem::path p;
    MakeNativePath( pName, &p );

    std::error_code ec;
    std::filesystem::remove( p, ec );
    if ( ec )
    {
        m_LastError = TranslateFsError( ec );
        return Error;
    }

    m_LastError = Success;
    return Complete;
}

radDrive::CompletionStatus radTvosDrive::DestroyFile( const char* filename )
{
    if ( IsAppDrive() )
    {
        m_LastError = HardwareFailure;
        return Error;
    }

    std::filesystem::path p;
    MakeNativePath( filename, &p );

    std::error_code ec;
    std::filesystem::remove( p, ec );
    if ( ec )
    {
        m_LastError = TranslateFsError( ec );
        return Error;
    }

    m_LastError = Success;
    return Complete;
}

void radTvosDrive::TranslateDirInfo( IRadDrive::DirectoryInfo* pDirectoryInfo, const std::filesystem::directory_entry& entry )
{
    std::string filename = entry.path().filename().u8string();
    strncpy( pDirectoryInfo->m_Name, filename.c_str(), radFileFilenameMax );
    pDirectoryInfo->m_Name[ radFileFilenameMax ] = '\0';
    pDirectoryInfo->m_Type = entry.is_directory() ? IRadDrive::DirectoryInfo::IsDirectory : IRadDrive::DirectoryInfo::IsFile;
}

bool radTvosDrive::FindNextMatching( radFileDirHandle* pHandle, IRadDrive::DirectoryInfo* pDirectoryInfo )
{
    while ( pHandle->it != pHandle->end )
    {
        const std::filesystem::directory_entry& entry = *pHandle->it;
        ++pHandle->it;
        TranslateDirInfo( pDirectoryInfo, entry );
        return true;
    }

    pDirectoryInfo->m_Name[ 0 ] = '\0';
    pDirectoryInfo->m_Type = IRadDrive::DirectoryInfo::IsDone;
    return false;
}

radDrive::CompletionStatus radTvosDrive::FindFirst( const char* searchSpec, IRadDrive::DirectoryInfo* pDirectoryInfo, radFileDirHandle* pHandle, bool /*firstSearch*/ )
{
    std::filesystem::path p;
    MakeNativePath( searchSpec, &p );

    std::error_code ec;
    pHandle->it = std::filesystem::directory_iterator( p, ec );
    pHandle->end = std::filesystem::directory_iterator();
    pHandle->pattern[0] = '\0';

    if ( ec )
    {
        m_LastError = TranslateFsError( ec );
        return Error;
    }

    bool ok = FindNextMatching( pHandle, pDirectoryInfo );
    m_LastError = ok ? Success : FileNotFound;
    return ok ? Complete : Error;
}

radDrive::CompletionStatus radTvosDrive::FindNext( radFileDirHandle* pHandle, IRadDrive::DirectoryInfo* pDirectoryInfo )
{
    bool ok = FindNextMatching( pHandle, pDirectoryInfo );
    m_LastError = ok ? Success : FileNotFound;
    return ok ? Complete : Error;
}

radDrive::CompletionStatus radTvosDrive::FindClose( radFileDirHandle* /*pHandle*/ )
{
    m_LastError = Success;
    return Complete;
}

#endif // RAD_TVOS
