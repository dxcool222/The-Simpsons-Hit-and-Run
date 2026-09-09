//=============================================================================
// tvOS diagnostics: bounded telemetry, anomaly reporting, and crash breadcrumbs.
//=============================================================================

#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined( RAD_TVOS ) && \
    ( defined( RAD_TVOS_AUDIO_DIAGNOSTICS ) || defined( RAD_TVOS_RENDER_DIAGNOSTICS ) || \
      defined( RAD_TVOS_STORAGE_DIAGNOSTICS ) || defined( RAD_TVOS_INPUT_DIAGNOSTICS ) )
#define SRR2_TVOS_DIAGNOSTICS_ENABLED 1
#else
#define SRR2_TVOS_DIAGNOSTICS_ENABLED 0
#endif

#if SRR2_TVOS_DIAGNOSTICS_ENABLED

#if __has_include( <SDL2/SDL.h> )
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif

#if __has_include( <pthread.h> )
    #include <pthread.h>
#endif

namespace SRR2
{
namespace Diagnostics
{

enum Category
{
    CORE = 0,
    FRAME,
    INPUT,
    AUDIO_LOAD,
    AUDIO_PLAYBACK,
    AUDIO_SOURCE_LIFETIME,
    AUDIO_POSITIONAL,
    COLLISION_AUDIO,
    VFS,
    VIDEO,
    RENDER,
    UI_LIFECYCLE,
    MEMORY,
    GAMEPLAY,
    RESOURCE_LIFETIME,
    THREADING,
    STATE_MACHINE,
    CATEGORY_COUNT
};

enum Severity
{
    TRACE_INTERNAL = 0,
    INFO_SUMMARY,
    WARNING_ANOMALY,
    ERROR_BUG,
    FATAL_CRASH_CONTEXT,
    SEVERITY_COUNT
};

struct Event
{
    uint64_t ms;
    uint32_t frame;
    uint32_t id;
    Severity severity;
    char text[224];
};

struct Ring
{
    Event events[500];
    uint16_t head;
    uint16_t count;
};

struct RateSlot
{
    uint32_t hash;
    uint64_t lastPrintMs;
    uint32_t suppressed;
};

struct CategoryStats
{
    uint64_t bySeverity[SEVERITY_COUNT];
    uint64_t anomalyCount;
    uint64_t suppressedPrints;
};

struct FrameStats
{
    uint32_t frames;
    uint64_t totalMs;
    uint32_t worstMs;
    uint32_t over50;
    uint32_t over100;
    uint32_t over250;
    // Wall-clock pacing (pre-sim-clamp). Proves stutter vs average FPS.
    uint32_t wallWorstMs;
    uint32_t wallLe17;   // on-budget for 60Hz
    uint32_t wall18_21;  // slight late
    uint32_t wall22_33;  // missed vsync / hitchy
    uint32_t wall34_50;  // double-frame class
    uint32_t wallOver50; // severe
};

struct InputStats
{
    uint64_t lastPumpMs;
    uint64_t lastControllerSampleMs;
    uint32_t maxPumpGapMs;
    uint32_t maxControllerGapMs;
    uint32_t pumpStalls;
    uint32_t controllerStalls;
    uint32_t connectEvents;
    uint32_t disconnectEvents;
};

struct AudioStats
{
    uint32_t clipsLoaded;
    uint32_t slowClips;
    uint32_t duplicateKeys;
    uint32_t duplicatePaths;
    uint64_t clipWallUs;
    uint64_t readUs;
    uint64_t stereoUs;
    uint64_t alUs;
    uint32_t openAlErrors;
    uint32_t sourceResetErrors;
};

struct RenderStats
{
    uint32_t frames;
    uint64_t drawCallsTotal;
    uint32_t drawCallsMax;
    uint64_t vertsTotal;
    uint32_t textureUploads;
    uint32_t textureUploadSpikeFrames;
    uint32_t glErrors;
    uint32_t rejectedDraws;
    uint32_t nullTextures;
    uint32_t shaderCreates;
};

struct VfsStats
{
    uint32_t opens;
    uint32_t misses;
    uint32_t repeatedMisses;
    uint32_t caseFixes;
    uint32_t fuzzyFixes;
    uint32_t slowOps;
};

struct Context
{
    char gameState[48];
    char screen[48];
    char loadingPhase[64];
    char audioCluster[40];
    char video[64];
    char level[24];
    char mission[24];
};

struct PathSlot
{
    uint32_t hash;
    uint32_t count;
    char label[96];
};

struct SourceSlot
{
    uint32_t source;
    uint32_t buffer;
    uint64_t playStartMs;
    uint32_t expectedMs;
    bool active;
    bool looping;
    bool streaming;
};

inline Ring g_rings[CATEGORY_COUNT] = {};
inline RateSlot g_rateSlots[160] = {};
inline CategoryStats g_categoryStats[CATEGORY_COUNT] = {};
inline FrameStats g_frameStats = {};
inline InputStats g_inputStats = {};
inline AudioStats g_audioStats = {};
inline RenderStats g_renderStats = {};
inline VfsStats g_vfsStats = {};
inline Context g_context = { "unknown", "unknown", "none", "none", "none", "unknown", "unknown" };
inline PathSlot g_vfsMissSlots[128] = {};
inline PathSlot g_audioKeySlots[192] = {};
inline PathSlot g_audioPathSlots[192] = {};
inline SourceSlot g_sources[128] = {};
inline uint64_t g_startMs = 0;
inline uint32_t g_frameId = 0;
inline uint32_t g_nextId = 1;
inline uint32_t g_textureUploadsThisFrame = 0;
inline bool g_categoryEnabled[CATEGORY_COUNT] =
{
    true, true, true, true, true, true, true, true, true,
    true, true, true, true, true, true, true, true
};

inline const char* CategoryName( Category category )
{
    static const char* names[CATEGORY_COUNT] =
    {
        "CORE", "FRAME", "INPUT", "AUDIO_LOAD", "AUDIO_PLAYBACK",
        "AUDIO_SOURCE_LIFETIME", "AUDIO_POSITIONAL", "COLLISION_AUDIO",
        "VFS", "VIDEO", "RENDER", "UI_LIFECYCLE", "MEMORY", "GAMEPLAY",
        "RESOURCE_LIFETIME", "THREADING", "STATE_MACHINE"
    };
    return category < CATEGORY_COUNT ? names[category] : "UNKNOWN";
}

inline const char* SeverityName( Severity severity )
{
    static const char* names[SEVERITY_COUNT] =
    {
        "TRACE_INTERNAL", "INFO_SUMMARY", "WARNING_ANOMALY", "ERROR_BUG",
        "FATAL_CRASH_CONTEXT"
    };
    return severity < SEVERITY_COUNT ? names[severity] : "UNKNOWN";
}

inline uint64_t NowMs()
{
#if SDL_VERSION_ATLEAST(2, 0, 18)
    return SDL_GetTicks64();
#else
    return static_cast<uint64_t>( SDL_GetTicks() );
#endif
}

inline const char* ThreadName()
{
#if defined( __APPLE__ ) && __has_include( <pthread.h> )
    return pthread_main_np() ? "main" : "worker";
#else
    return "unknown";
#endif
}

inline void EnsureStarted()
{
    if ( g_startMs == 0 )
    {
        g_startMs = NowMs();
    }
}

inline uint32_t HashBytes( const char* s )
{
    uint32_t h = 2166136261u;
    if ( s == NULL )
    {
        return h;
    }
    while ( *s != '\0' )
    {
        unsigned char c = static_cast<unsigned char>( *s++ );
        if ( c == '\\' )
        {
            c = '/';
        }
        else if ( c >= 'A' && c <= 'Z' )
        {
            c = static_cast<unsigned char>( c - 'A' + 'a' );
        }
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

inline uint16_t RingCapacity( Category category )
{
    switch ( category )
    {
        case FRAME: return 300;
        case INPUT: return 300;
        case AUDIO_LOAD: return 500;
        case AUDIO_PLAYBACK: return 500;
        case AUDIO_SOURCE_LIFETIME: return 500;
        case VFS: return 500;
        case VIDEO: return 300;
        case RENDER: return 300;
        case UI_LIFECYCLE: return 200;
        case GAMEPLAY: return 300;
        case STATE_MACHINE: return 300;
        default: return 300;
    }
}

inline void CopyText( char* dst, size_t dstSize, const char* src )
{
    if ( dst == NULL || dstSize == 0 )
    {
        return;
    }
    if ( src == NULL )
    {
        src = "";
    }
    snprintf( dst, dstSize, "%s", src );
    dst[dstSize - 1] = '\0';
}

inline void SetContextValue( const char* key, const char* value )
{
    if ( key == NULL )
    {
        return;
    }
    if ( strcmp( key, "gameState" ) == 0 || strcmp( key, "context" ) == 0 )
    {
        CopyText( g_context.gameState, sizeof( g_context.gameState ), value );
    }
    else if ( strcmp( key, "screen" ) == 0 )
    {
        CopyText( g_context.screen, sizeof( g_context.screen ), value );
    }
    else if ( strcmp( key, "loading" ) == 0 || strcmp( key, "loadingPhase" ) == 0 )
    {
        CopyText( g_context.loadingPhase, sizeof( g_context.loadingPhase ), value );
    }
    else if ( strcmp( key, "audioCluster" ) == 0 )
    {
        CopyText( g_context.audioCluster, sizeof( g_context.audioCluster ), value );
    }
    else if ( strcmp( key, "video" ) == 0 )
    {
        CopyText( g_context.video, sizeof( g_context.video ), value );
    }
    else if ( strcmp( key, "level" ) == 0 )
    {
        CopyText( g_context.level, sizeof( g_context.level ), value );
    }
    else if ( strcmp( key, "mission" ) == 0 )
    {
        CopyText( g_context.mission, sizeof( g_context.mission ), value );
    }
}

inline Event* StoreEvent( Category category, Severity severity, uint32_t id, const char* text )
{
    if ( category >= CATEGORY_COUNT || !g_categoryEnabled[category] )
    {
        return NULL;
    }

    EnsureStarted();
    Ring& ring = g_rings[category];
    const uint16_t cap = RingCapacity( category );
    Event& event = ring.events[ring.head];
    event.ms = NowMs();
    event.frame = g_frameId;
    event.id = id;
    event.severity = severity;
    CopyText( event.text, sizeof( event.text ), text );
    ring.head = static_cast<uint16_t>( ( ring.head + 1 ) % cap );
    if ( ring.count < cap )
    {
        ring.count++;
    }

    if ( severity < SEVERITY_COUNT )
    {
        g_categoryStats[category].bySeverity[severity]++;
    }
    if ( severity >= WARNING_ANOMALY )
    {
        g_categoryStats[category].anomalyCount++;
    }
    return &event;
}

inline bool ShouldPrint( Category category, uint32_t hash, Severity severity, uint32_t rateMs )
{
    if ( severity >= ERROR_BUG || rateMs == 0 )
    {
        return true;
    }

    const uint32_t slotIndex = ( hash ^ static_cast<uint32_t>( category ) ) %
        ( sizeof( g_rateSlots ) / sizeof( g_rateSlots[0] ) );
    RateSlot& slot = g_rateSlots[slotIndex];
    const uint64_t now = NowMs();

    if ( slot.hash != hash )
    {
        slot.hash = hash;
        slot.lastPrintMs = now;
        slot.suppressed = 0;
        return true;
    }

    if ( now - slot.lastPrintMs >= rateMs )
    {
        if ( slot.suppressed > 0 )
        {
            SDL_Log( "[DIAG_RATE_LIMIT] category=%s suppressed=%u hash=0x%x",
                     CategoryName( category ), slot.suppressed, hash );
            g_categoryStats[category].suppressedPrints += slot.suppressed;
            slot.suppressed = 0;
        }
        slot.lastPrintMs = now;
        return true;
    }

    slot.suppressed++;
    return false;
}

inline void PrintEvent( Category category, Severity severity, uint32_t id, const char* text )
{
    SDL_Log(
        "[DIAG] category=%s severity=%s frame=%u ms=%llu thread=%s id=%u "
        "context=%s screen=%s level=%s mission=%s loading=%s audioCluster=%s video=%s %s",
        CategoryName( category ),
        SeverityName( severity ),
        g_frameId,
        static_cast<unsigned long long>( NowMs() ),
        ThreadName(),
        id,
        g_context.gameState,
        g_context.screen,
        g_context.level,
        g_context.mission,
        g_context.loadingPhase,
        g_context.audioCluster,
        g_context.video,
        text != NULL ? text : "" );
}

inline void VEventf( Category category, Severity severity, bool forcePrint, uint32_t rateMs, const char* fmt, va_list ap )
{
    char msg[224];
    if ( fmt == NULL )
    {
        fmt = "";
    }
    vsnprintf( msg, sizeof( msg ), fmt, ap );
    msg[sizeof( msg ) - 1] = '\0';

    const uint32_t id = g_nextId++;
    StoreEvent( category, severity, id, msg );

    if ( forcePrint && ShouldPrint( category, HashBytes( fmt ), severity, rateMs ) )
    {
        PrintEvent( category, severity, id, msg );
    }
}

inline void Tracef( Category category, const char* fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    VEventf( category, TRACE_INTERNAL, false, 0, fmt, ap );
    va_end( ap );
}

inline void Summaryf( Category category, const char* fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    VEventf( category, INFO_SUMMARY, true, 0, fmt, ap );
    va_end( ap );
}

inline void Checkpointf( Category category, const char* fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    VEventf( category, INFO_SUMMARY, true, 0, fmt, ap );
    va_end( ap );
}

inline void Anomalyf( Category category, const char* fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    VEventf( category, WARNING_ANOMALY, true, 5000, fmt, ap );
    va_end( ap );
}

inline void Errorf( Category category, const char* fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    VEventf( category, ERROR_BUG, true, 0, fmt, ap );
    va_end( ap );
}

inline bool LooksLikeWarningOrError( const char* fmt, Severity* severity )
{
    if ( severity == NULL )
    {
        return false;
    }
    if ( fmt == NULL )
    {
        *severity = TRACE_INTERNAL;
        return false;
    }
    if ( strstr( fmt, "FATAL" ) || strstr( fmt, "CRITICAL" ) || strstr( fmt, "ERROR" ) ||
         strstr( fmt, "FAILED" ) || strstr( fmt, "BUG" ) || strstr( fmt, "FAIL" ) )
    {
        *severity = ERROR_BUG;
        return true;
    }
    if ( strstr( fmt, "WARNING" ) || strstr( fmt, "WARN" ) || strstr( fmt, "INVALID" ) ||
         strstr( fmt, "MISSING" ) || strstr( fmt, "DROPPED" ) || strstr( fmt, "SKIP" ) )
    {
        *severity = WARNING_ANOMALY;
        return true;
    }
    *severity = TRACE_INTERNAL;
    return false;
}

inline void AutoLogf( Category category, const char* fmt, ... )
{
    Severity severity = TRACE_INTERNAL;
    const bool print = LooksLikeWarningOrError( fmt, &severity );
    va_list ap;
    va_start( ap, fmt );
    VEventf( category, severity, print, severity == ERROR_BUG ? 0 : 5000, fmt, ap );
    va_end( ap );
}

inline void DumpCategory( Category category, unsigned int maxEvents )
{
    if ( category >= CATEGORY_COUNT )
    {
        return;
    }
    Ring& ring = g_rings[category];
    const uint16_t cap = RingCapacity( category );
    unsigned int n = ring.count;
    if ( maxEvents > 0 && n > maxEvents )
    {
        n = maxEvents;
    }

    SDL_Log( "[DIAG_RING_DUMP] category=%s count=%u requested=%u", CategoryName( category ), ring.count, n );
    for ( unsigned int i = 0; i < n; ++i )
    {
        const uint16_t idx = static_cast<uint16_t>(
            ( ring.head + cap - n + i ) % cap );
        const Event& e = ring.events[idx];
        SDL_Log( "[DIAG_RING] category=%s frame=%u ms=%llu severity=%s id=%u %s",
                 CategoryName( category ),
                 e.frame,
                 static_cast<unsigned long long>( e.ms ),
                 SeverityName( e.severity ),
                 e.id,
                 e.text );
    }
}

inline void DumpCrashContext( const char* reason, const char* file, int line )
{
    StoreEvent( CORE, FATAL_CRASH_CONTEXT, g_nextId++, reason ? reason : "crash_context" );
    SDL_Log(
        "[CRASH_CONTEXT] reason=%s file=%s line=%d frame=%u uptime_ms=%llu "
        "screen=%s context=%s level=%s mission=%s loading=%s audioCluster=%s video=%s "
        "lastInputPumpGapMs=%u lastControllerGapMs=%u worstFrameMs=%u glErrors=%u openalErrors=%u vfsMisses=%u",
        reason ? reason : "unknown",
        file ? file : "unknown",
        line,
        g_frameId,
        static_cast<unsigned long long>( NowMs() - g_startMs ),
        g_context.screen,
        g_context.gameState,
        g_context.level,
        g_context.mission,
        g_context.loadingPhase,
        g_context.audioCluster,
        g_context.video,
        g_inputStats.maxPumpGapMs,
        g_inputStats.maxControllerGapMs,
        g_frameStats.worstMs,
        g_renderStats.glErrors,
        g_audioStats.openAlErrors,
        g_vfsStats.misses );

    DumpCategory( FRAME, 50 );
    DumpCategory( INPUT, 50 );
    DumpCategory( AUDIO_LOAD, 50 );
    DumpCategory( AUDIO_SOURCE_LIFETIME, 50 );
    DumpCategory( VFS, 50 );
    DumpCategory( UI_LIFECYCLE, 50 );
    DumpCategory( GAMEPLAY, 50 );
    DumpCategory( RENDER, 50 );
}

inline void DumpAll()
{
    Summaryf(
        CORE,
        "[SESSION_SUMMARY] uptime_ms=%llu frames=%u avg_frame_ms=%llu worst_frame_ms=%u "
        "frame50=%u frame100=%u frame250=%u "
        "wall_worst_ms=%u jitter_le17=%u jitter_18_21=%u jitter_22_33=%u jitter_34_50=%u jitter_over50=%u "
        "input_stalls=%u controller_stalls=%u "
        "audio_anomalies=%llu render_anomalies=%llu vfs_anomalies=%llu openal_errors=%u gl_errors=%u "
        "texture_uploads=%u vfs_misses=%u",
        static_cast<unsigned long long>( NowMs() - g_startMs ),
        g_frameStats.frames,
        static_cast<unsigned long long>( g_frameStats.frames ? g_frameStats.totalMs / g_frameStats.frames : 0 ),
        g_frameStats.worstMs,
        g_frameStats.over50,
        g_frameStats.over100,
        g_frameStats.over250,
        g_frameStats.wallWorstMs,
        g_frameStats.wallLe17,
        g_frameStats.wall18_21,
        g_frameStats.wall22_33,
        g_frameStats.wall34_50,
        g_frameStats.wallOver50,
        g_inputStats.pumpStalls,
        g_inputStats.controllerStalls,
        static_cast<unsigned long long>( g_categoryStats[AUDIO_LOAD].anomalyCount + g_categoryStats[AUDIO_SOURCE_LIFETIME].anomalyCount ),
        static_cast<unsigned long long>( g_categoryStats[RENDER].anomalyCount ),
        static_cast<unsigned long long>( g_categoryStats[VFS].anomalyCount ),
        g_audioStats.openAlErrors,
        g_renderStats.glErrors,
        g_renderStats.textureUploads,
        g_vfsStats.misses );

    Summaryf(
        AUDIO_LOAD,
        "[AUDIO_SOURCE_SUMMARY] clips=%u slow=%u read_us=%llu stereo_us=%llu al_us=%llu "
        "openal_errors=%u source_reset_errors=%u duplicate_keys=%u duplicate_paths=%u",
        g_audioStats.clipsLoaded,
        g_audioStats.slowClips,
        static_cast<unsigned long long>( g_audioStats.readUs ),
        static_cast<unsigned long long>( g_audioStats.stereoUs ),
        static_cast<unsigned long long>( g_audioStats.alUs ),
        g_audioStats.openAlErrors,
        g_audioStats.sourceResetErrors,
        g_audioStats.duplicateKeys,
        g_audioStats.duplicatePaths );

    Summaryf(
        INPUT,
        "[INPUT_SUMMARY] screen=%s context=%s max_input_gap_ms=%u max_poll_gap_ms=%u "
        "connects=%u disconnects=%u loading_during_gap=%s video=%s",
        g_context.screen,
        g_context.gameState,
        g_inputStats.maxControllerGapMs,
        g_inputStats.maxPumpGapMs,
        g_inputStats.connectEvents,
        g_inputStats.disconnectEvents,
        g_context.loadingPhase,
        g_context.video );

    Summaryf(
        RENDER,
        "[RENDER_SUMMARY] frames=%u draw_calls_avg=%llu draw_calls_max=%u verts_avg=%llu "
        "texture_uploads=%u texture_upload_spike_frames=%u gl_errors=%u rejected_draws=%u null_textures=%u shader_creates=%u",
        g_renderStats.frames,
        static_cast<unsigned long long>( g_renderStats.frames ? g_renderStats.drawCallsTotal / g_renderStats.frames : 0 ),
        g_renderStats.drawCallsMax,
        static_cast<unsigned long long>( g_renderStats.frames ? g_renderStats.vertsTotal / g_renderStats.frames : 0 ),
        g_renderStats.textureUploads,
        g_renderStats.textureUploadSpikeFrames,
        g_renderStats.glErrors,
        g_renderStats.rejectedDraws,
        g_renderStats.nullTextures,
        g_renderStats.shaderCreates );
}

inline void RecordWallFrame( uint32_t wallMs )
{
    if ( wallMs > g_frameStats.wallWorstMs )
    {
        g_frameStats.wallWorstMs = wallMs;
    }
    if ( wallMs <= 17 )
    {
        g_frameStats.wallLe17++;
    }
    else if ( wallMs <= 21 )
    {
        g_frameStats.wall18_21++;
    }
    else if ( wallMs <= 33 )
    {
        g_frameStats.wall22_33++;
    }
    else if ( wallMs <= 50 )
    {
        g_frameStats.wall34_50++;
    }
    else
    {
        g_frameStats.wallOver50++;
    }
}

inline void BeginFrame( uint32_t frame, uint32_t elapsedMs )
{
    EnsureStarted();
    g_frameId = frame;
    g_textureUploadsThisFrame = 0;
    g_frameStats.frames++;
    g_frameStats.totalMs += elapsedMs;
    if ( elapsedMs > g_frameStats.worstMs )
    {
        g_frameStats.worstMs = elapsedMs;
    }
    if ( elapsedMs > 50 )
    {
        g_frameStats.over50++;
        Anomalyf( FRAME, "[FRAME_STALL] elapsed_ms=%u threshold_ms=50", elapsedMs );
    }
    if ( elapsedMs > 100 )
    {
        g_frameStats.over100++;
        Anomalyf( FRAME, "[FRAME_STALL] elapsed_ms=%u threshold_ms=100", elapsedMs );
    }
    if ( elapsedMs > 250 )
    {
        g_frameStats.over250++;
        Errorf( FRAME, "[FRAME_STALL] elapsed_ms=%u threshold_ms=250", elapsedMs );
        DumpCategory( INPUT, 30 );
        DumpCategory( AUDIO_LOAD, 30 );
        DumpCategory( VFS, 30 );
        DumpCategory( UI_LIFECYCLE, 30 );
    }
    Tracef( FRAME, "frame_begin elapsed_ms=%u", elapsedMs );
    if ( ( frame % 3600 ) == 0 )
    {
        DumpAll();
    }
}

inline void EndFrame()
{
    if ( g_textureUploadsThisFrame > 64 )
    {
        g_renderStats.textureUploadSpikeFrames++;
        Anomalyf( RENDER, "[TEXTURE_UPLOAD_SPIKE] uploads_this_frame=%u threshold=64", g_textureUploadsThisFrame );
    }
    Tracef( FRAME, "frame_end texture_uploads=%u", g_textureUploadsThisFrame );
}

inline void RecordInputPump( uint32_t events )
{
    const uint64_t now = NowMs();
    if ( g_inputStats.lastPumpMs != 0 )
    {
        const uint32_t gap = static_cast<uint32_t>( now - g_inputStats.lastPumpMs );
        if ( gap > g_inputStats.maxPumpGapMs )
        {
            g_inputStats.maxPumpGapMs = gap;
        }
        if ( gap > 100 )
        {
            g_inputStats.pumpStalls++;
            Anomalyf( INPUT, "[INPUT_PUMP_STALL] gap_ms=%u events=%u threshold_ms=100", gap, events );
            DumpCategory( FRAME, 30 );
        }
    }
    g_inputStats.lastPumpMs = now;
    Tracef( INPUT, "sdl_pump events=%u", events );
}

inline void RecordControllerSample( bool connected, bool hasInput )
{
    const uint64_t now = NowMs();
    if ( g_inputStats.lastControllerSampleMs != 0 )
    {
        const uint32_t gap = static_cast<uint32_t>( now - g_inputStats.lastControllerSampleMs );
        if ( gap > g_inputStats.maxControllerGapMs )
        {
            g_inputStats.maxControllerGapMs = gap;
        }
        if ( connected && gap > 250 )
        {
            g_inputStats.controllerStalls++;
            Anomalyf( INPUT, "[CONTROLLER_SAMPLE_STALL] gap_ms=%u connected=1 hasInput=%d threshold_ms=250",
                      gap, hasInput ? 1 : 0 );
            DumpCategory( FRAME, 30 );
            DumpCategory( AUDIO_LOAD, 30 );
        }
    }
    g_inputStats.lastControllerSampleMs = now;
    Tracef( INPUT, "controller_sample connected=%d hasInput=%d", connected ? 1 : 0, hasInput ? 1 : 0 );
}

inline void RecordControllerConnection( bool connected, int slot, const char* vendor )
{
    if ( connected )
    {
        g_inputStats.connectEvents++;
        Checkpointf( INPUT, "[CONTROLLER_CONNECT] slot=%d vendor=%s", slot, vendor ? vendor : "unknown" );
    }
    else
    {
        g_inputStats.disconnectEvents++;
        Anomalyf( INPUT, "[CONTROLLER_DISCONNECT] slot=%d vendor=%s", slot, vendor ? vendor : "unknown" );
    }
}

inline bool TrackPathSlot( PathSlot* slots, unsigned int slotCount, const char* label, uint32_t* outCount )
{
    const uint32_t hash = HashBytes( label );
    unsigned int freeIndex = slotCount;
    for ( unsigned int i = 0; i < slotCount; ++i )
    {
        if ( slots[i].hash == hash )
        {
            slots[i].count++;
            if ( outCount != NULL )
            {
                *outCount = slots[i].count;
            }
            return false;
        }
        if ( slots[i].hash == 0 && freeIndex == slotCount )
        {
            freeIndex = i;
        }
    }
    if ( freeIndex == slotCount )
    {
        freeIndex = hash % slotCount;
    }
    slots[freeIndex].hash = hash;
    slots[freeIndex].count = 1;
    CopyText( slots[freeIndex].label, sizeof( slots[freeIndex].label ), label );
    if ( outCount != NULL )
    {
        *outCount = 1;
    }
    return true;
}

inline void RecordVfsOpen( const char* api, const char* path, bool ok, int error, uint32_t elapsedMs )
{
    g_vfsStats.opens++;
    if ( !ok )
    {
        g_vfsStats.misses++;
        uint32_t count = 0;
        TrackPathSlot( g_vfsMissSlots, sizeof( g_vfsMissSlots ) / sizeof( g_vfsMissSlots[0] ), path, &count );
        if ( count > 1 )
        {
            g_vfsStats.repeatedMisses++;
            Anomalyf( VFS, "[VFS_REPEATED_MISS] api=%s path=%s count=%u errno=%d",
                      api ? api : "?", path ? path : "", count, error );
        }
        else
        {
            Tracef( VFS, "vfs_miss api=%s path=%s errno=%d", api ? api : "?", path ? path : "", error );
        }
    }
    else
    {
        Tracef( VFS, "vfs_open api=%s path=%s elapsed_ms=%u", api ? api : "?", path ? path : "", elapsedMs );
    }
    if ( elapsedMs > 50 )
    {
        g_vfsStats.slowOps++;
        Anomalyf( VFS, "[VFS_SLOW_OPEN] api=%s path=%s elapsed_ms=%u threshold_ms=50",
                  api ? api : "?", path ? path : "", elapsedMs );
    }
}

inline void RecordVfsFixup( const char* kind, const char* requested, const char* resolved )
{
    if ( kind && strcmp( kind, "case" ) == 0 )
    {
        g_vfsStats.caseFixes++;
    }
    else
    {
        g_vfsStats.fuzzyFixes++;
    }
    Anomalyf( VFS, "[VFS_PATH_FIXUP] kind=%s requested=%s resolved=%s",
              kind ? kind : "unknown", requested ? requested : "", resolved ? resolved : "" );
}

inline void RecordAudioClipTiming( const char* path, uint64_t wallUs, uint64_t initPollUs, uint64_t loadClipUs )
{
    g_audioStats.clipsLoaded++;
    g_audioStats.clipWallUs += wallUs;
    if ( wallUs > 50000 || initPollUs > 33000 || loadClipUs > 33000 )
    {
        g_audioStats.slowClips++;
        Anomalyf(
            AUDIO_LOAD,
            "[AUDIO_CLIP_SLOW] path=%s wall_us=%llu init_poll_us=%llu loading_clip_us=%llu thresholds=50000/33000",
            path ? path : "",
            static_cast<unsigned long long>( wallUs ),
            static_cast<unsigned long long>( initPollUs ),
            static_cast<unsigned long long>( loadClipUs ) );
    }
    else
    {
        Tracef( AUDIO_LOAD, "clip_loaded path=%s wall_us=%llu", path ? path : "", static_cast<unsigned long long>( wallUs ) );
    }
}

inline void RecordAudioReadUs( uint64_t us )
{
    g_audioStats.readUs += us;
}

inline void RecordAudioStereoUs( uint64_t us )
{
    g_audioStats.stereoUs += us;
}

inline void RecordAudioAlUs( uint64_t us )
{
    g_audioStats.alUs += us;
}

inline void RecordAudioResourceKey( const char* key, const char* path, bool streaming, bool alreadyCaptured )
{
    uint32_t count = 0;
    TrackPathSlot( g_audioKeySlots, sizeof( g_audioKeySlots ) / sizeof( g_audioKeySlots[0] ), key, &count );
    if ( count > 1 && !alreadyCaptured )
    {
        g_audioStats.duplicateKeys++;
        Anomalyf( AUDIO_LOAD, "[AUDIO_DUP_KEY] key=%s path=%s count=%u streaming=%d",
                  key ? key : "", path ? path : "", count, streaming ? 1 : 0 );
    }
    if ( path != NULL && path[0] != '\0' )
    {
        uint32_t pathCount = 0;
        TrackPathSlot( g_audioPathSlots, sizeof( g_audioPathSlots ) / sizeof( g_audioPathSlots[0] ), path, &pathCount );
        if ( pathCount > 1 && !alreadyCaptured )
        {
            g_audioStats.duplicatePaths++;
            Anomalyf( AUDIO_LOAD, "[AUDIO_DUP_PATH] key=%s path=%s count=%u streaming=%d",
                      key ? key : "", path, pathCount, streaming ? 1 : 0 );
        }
    }
    Tracef( AUDIO_LOAD, "resource_capture key=%s path=%s streaming=%d alreadyCaptured=%d",
            key ? key : "", path ? path : "", streaming ? 1 : 0, alreadyCaptured ? 1 : 0 );
}

inline SourceSlot* FindSource( uint32_t source )
{
    SourceSlot* freeSlot = NULL;
    for ( unsigned int i = 0; i < sizeof( g_sources ) / sizeof( g_sources[0] ); ++i )
    {
        if ( g_sources[i].source == source )
        {
            return &g_sources[i];
        }
        if ( g_sources[i].source == 0 && freeSlot == NULL )
        {
            freeSlot = &g_sources[i];
        }
    }
    if ( freeSlot == NULL )
    {
        freeSlot = &g_sources[source % ( sizeof( g_sources ) / sizeof( g_sources[0] ) )];
    }
    memset( freeSlot, 0, sizeof( *freeSlot ) );
    freeSlot->source = source;
    return freeSlot;
}

inline void RecordAudioSourceReset( uint32_t source, uint32_t oldBuffer, bool wasPlaying, bool oldLooping, bool queued )
{
    SourceSlot* slot = FindSource( source );
    if ( wasPlaying && !oldLooping )
    {
        const uint32_t playMs = static_cast<uint32_t>( NowMs() - slot->playStartMs );
        if ( slot->expectedMs > 0 && playMs > slot->expectedMs + 1000 )
        {
            Anomalyf( AUDIO_SOURCE_LIFETIME,
                      "[AUDIO_SOURCE_OVERRUN] sourceId=%u buffer=%u play_ms=%u expected_ms=%u queued=%d",
                      source, oldBuffer, playMs, slot->expectedMs, queued ? 1 : 0 );
        }
    }
    slot->active = false;
    slot->buffer = 0;
    slot->looping = false;
    slot->streaming = false;
    Tracef( AUDIO_SOURCE_LIFETIME, "source_reset sourceId=%u oldBuffer=%u wasPlaying=%d oldLooping=%d queued=%d",
            source, oldBuffer, wasPlaying ? 1 : 0, oldLooping ? 1 : 0, queued ? 1 : 0 );
}

inline void RecordAudioSourceAttach( uint32_t source, uint32_t buffer, bool looping, bool streaming, bool queued )
{
    SourceSlot* slot = FindSource( source );
    slot->buffer = buffer;
    slot->looping = looping;
    slot->streaming = streaming;
    if ( !streaming && looping )
    {
        Anomalyf( AUDIO_SOURCE_LIFETIME,
                  "[AUDIO_SOURCE_LOOPING_ONESHOT_RISK] sourceId=%u buffer=%u streaming=0 queued=%d",
                  source, buffer, queued ? 1 : 0 );
    }
    Tracef( AUDIO_SOURCE_LIFETIME, "source_attach sourceId=%u buffer=%u looping=%d streaming=%d queued=%d",
            source, buffer, looping ? 1 : 0, streaming ? 1 : 0, queued ? 1 : 0 );
}

inline void RecordAudioSourcePlay( uint32_t source, uint32_t buffer, bool looping, bool streaming, uint32_t expectedMs )
{
    SourceSlot* slot = FindSource( source );
    if ( slot->active && !slot->looping )
    {
        const uint32_t age = static_cast<uint32_t>( NowMs() - slot->playStartMs );
        if ( slot->expectedMs > 0 && age > slot->expectedMs + 1000 )
        {
            Anomalyf( AUDIO_SOURCE_LIFETIME,
                      "[AUDIO_SOURCE_REUSED_AFTER_OVERRUN] sourceId=%u oldBuffer=%u newBuffer=%u age_ms=%u expected_ms=%u",
                      source, slot->buffer, buffer, age, slot->expectedMs );
        }
    }
    slot->buffer = buffer;
    slot->looping = looping;
    slot->streaming = streaming;
    slot->expectedMs = expectedMs;
    slot->playStartMs = NowMs();
    slot->active = true;
    Tracef( AUDIO_SOURCE_LIFETIME, "source_play sourceId=%u buffer=%u looping=%d streaming=%d expected_ms=%u",
            source, buffer, looping ? 1 : 0, streaming ? 1 : 0, expectedMs );
}

inline void RecordAudioSourceStop( uint32_t source, const char* reason )
{
    SourceSlot* slot = FindSource( source );
    slot->active = false;
    Tracef( AUDIO_SOURCE_LIFETIME, "source_stop sourceId=%u reason=%s", source, reason ? reason : "unknown" );
}

inline void RecordOpenALError( const char* op, uint32_t source, uint32_t err )
{
    if ( err == 0 )
    {
        return;
    }
    g_audioStats.openAlErrors++;
    Errorf( AUDIO_SOURCE_LIFETIME, "[OPENAL_ERROR] op=%s sourceId=%u err=0x%x",
            op ? op : "unknown", source, err );
}

inline void RecordCollisionSound( const char* phase, const char* soundName, const void* objA, const void* objB, bool suppressed )
{
    const uint32_t pairId = static_cast<uint32_t>(
        ( ( reinterpret_cast<uintptr_t>( objA ) >> 4 ) ^
          ( reinterpret_cast<uintptr_t>( objB ) >> 4 ) ) & 0xffffffffu );
    // Always print, rateMs=0 — Anomalyf's 5s rate-limit hid "playing" and
    // compressed pair_already_playing to one line per 5s (evidence gap).
    char msg[384];
    snprintf( msg, sizeof( msg ),
              "[COLLISION_SOUND] pairId=%u sound=%s objectA=%p objectB=%p phase=%s",
              pairId, soundName ? soundName : "", objA, objB, phase ? phase : "" );
    PrintEvent( COLLISION_AUDIO, WARNING_ANOMALY, HashBytes( msg ), msg );
    StoreEvent( COLLISION_AUDIO, WARNING_ANOMALY, HashBytes( msg ), msg );
}

inline void RecordRenderFrame( uint32_t drawCalls, uint32_t verts )
{
    g_renderStats.frames++;
    g_renderStats.drawCallsTotal += drawCalls;
    g_renderStats.vertsTotal += verts;
    if ( drawCalls > g_renderStats.drawCallsMax )
    {
        g_renderStats.drawCallsMax = drawCalls;
    }
    if ( drawCalls > 3500 )
    {
        Anomalyf( RENDER, "[DRAW_CALL_SPIKE] drawCalls=%u verts=%u threshold=3500", drawCalls, verts );
    }
    Tracef( RENDER, "render_frame drawCalls=%u verts=%u", drawCalls, verts );
}

inline void RecordTextureUpload( const char* label, uint32_t w, uint32_t h, bool video, uint32_t err )
{
    g_renderStats.textureUploads++;
    g_textureUploadsThisFrame++;
    if ( err != 0 )
    {
        g_renderStats.glErrors++;
        Errorf( RENDER, "[TEXTURE_UPLOAD_GL_ERROR] label=%s w=%u h=%u video=%d err=0x%x",
                label ? label : "", w, h, video ? 1 : 0, err );
    }
    if ( video && ( w > 640 || h > 480 ) )
    {
        Anomalyf( VIDEO, "[VIDEO_UPLOAD_PADDED] label=%s w=%u h=%u expected_max=640x480",
                  label ? label : "", w, h );
    }
    Tracef( video ? VIDEO : RENDER, "texture_upload label=%s w=%u h=%u video=%d",
            label ? label : "", w, h, video ? 1 : 0 );
}

inline void RecordGlError( const char* op, uint32_t err )
{
    if ( err == 0 )
    {
        return;
    }
    g_renderStats.glErrors++;
    Errorf( RENDER, "[GL_ERROR] op=%s err=0x%x", op ? op : "unknown", err );
}

inline void RecordRejectedDraw( const char* reason, uint32_t count )
{
    g_renderStats.rejectedDraws += count;
    Anomalyf( RENDER, "[DRAW_REJECTED] reason=%s count=%u", reason ? reason : "unknown", count );
}

inline void RecordShaderCreate( const char* name, int type )
{
    g_renderStats.shaderCreates++;
    Tracef( RENDER, "shader_create name=%s type=%d total=%u",
            name ? name : "(null)", type, g_renderStats.shaderCreates );
}

inline void SetCategoryEnabled( Category category, bool enabled )
{
    if ( category < CATEGORY_COUNT )
    {
        g_categoryEnabled[category] = enabled;
    }
}

class ScopedTimer
{
public:
    ScopedTimer( Category category, const char* name, uint32_t warnMs )
        : m_category( category ),
          m_name( name ),
          m_warnMs( warnMs ),
          m_startMs( NowMs() )
    {
    }

    ~ScopedTimer()
    {
        const uint32_t elapsed = static_cast<uint32_t>( NowMs() - m_startMs );
        if ( m_warnMs > 0 && elapsed > m_warnMs )
        {
            Anomalyf( m_category, "[SCOPE_SLOW] name=%s elapsed_ms=%u threshold_ms=%u",
                      m_name ? m_name : "", elapsed, m_warnMs );
        }
        else
        {
            Tracef( m_category, "scope name=%s elapsed_ms=%u", m_name ? m_name : "", elapsed );
        }
    }

private:
    Category m_category;
    const char* m_name;
    uint32_t m_warnMs;
    uint64_t m_startMs;
};

} // namespace Diagnostics
} // namespace SRR2

extern "C" void SRR2_Diagnostics_DumpAll( void );
extern "C" void SRR2_Diagnostics_DumpCrashContext( const char* reason, const char* file, int line );
extern "C" void SRR2_Diagnostics_SetContextValue( const char* key, const char* value );

#else

namespace SRR2
{
namespace Diagnostics
{
enum Category
{
    CORE = 0, FRAME, INPUT, AUDIO_LOAD, AUDIO_PLAYBACK, AUDIO_SOURCE_LIFETIME,
    AUDIO_POSITIONAL, COLLISION_AUDIO, VFS, VIDEO, RENDER, UI_LIFECYCLE,
    MEMORY, GAMEPLAY, RESOURCE_LIFETIME, THREADING, STATE_MACHINE, CATEGORY_COUNT
};
enum Severity { TRACE_INTERNAL = 0, INFO_SUMMARY, WARNING_ANOMALY, ERROR_BUG, FATAL_CRASH_CONTEXT };
inline void SetContextValue( const char*, const char* ) {}
inline void Tracef( Category, const char*, ... ) {}
inline void Summaryf( Category, const char*, ... ) {}
inline void Checkpointf( Category, const char*, ... ) {}
inline void Anomalyf( Category, const char*, ... ) {}
inline void Errorf( Category, const char*, ... ) {}
inline void AutoLogf( Category, const char*, ... ) {}
inline void DumpAll() {}
inline void DumpCrashContext( const char*, const char*, int ) {}
inline void BeginFrame( uint32_t, uint32_t ) {}
inline void RecordWallFrame( uint32_t ) {}
inline void EndFrame() {}
inline void RecordInputPump( uint32_t ) {}
inline void RecordControllerSample( bool, bool ) {}
inline void RecordControllerConnection( bool, int, const char* ) {}
inline void RecordVfsOpen( const char*, const char*, bool, int, uint32_t ) {}
inline void RecordVfsFixup( const char*, const char*, const char* ) {}
inline void RecordAudioClipTiming( const char*, uint64_t, uint64_t, uint64_t ) {}
inline void RecordAudioReadUs( uint64_t ) {}
inline void RecordAudioStereoUs( uint64_t ) {}
inline void RecordAudioAlUs( uint64_t ) {}
inline void RecordAudioResourceKey( const char*, const char*, bool, bool ) {}
inline void RecordAudioSourceReset( uint32_t, uint32_t, bool, bool, bool ) {}
inline void RecordAudioSourceAttach( uint32_t, uint32_t, bool, bool, bool ) {}
inline void RecordAudioSourcePlay( uint32_t, uint32_t, bool, bool, uint32_t ) {}
inline void RecordAudioSourceStop( uint32_t, const char* ) {}
inline void RecordOpenALError( const char*, uint32_t, uint32_t ) {}
inline void RecordCollisionSound( const char*, const char*, const void*, const void*, bool ) {}
inline void RecordRenderFrame( uint32_t, uint32_t ) {}
inline void RecordTextureUpload( const char*, uint32_t, uint32_t, bool, uint32_t ) {}
inline void RecordGlError( const char*, uint32_t ) {}
inline void RecordRejectedDraw( const char*, uint32_t ) {}
inline void RecordShaderCreate( const char*, int ) {}
inline void SetCategoryEnabled( Category, bool ) {}
class ScopedTimer { public: ScopedTimer( Category, const char*, uint32_t ) {} };
} // namespace Diagnostics
} // namespace SRR2

#endif
