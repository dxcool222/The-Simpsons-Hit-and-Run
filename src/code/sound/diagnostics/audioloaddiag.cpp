//=============================================================================
// tvOS audio load diagnostics — aggregation and context (instrumentation only).
//=============================================================================

#include <sound/diagnostics/audioloaddiag.hpp>
#include <sound/diagnostics/audioloaddiag_bridge.hpp>
#include <diagnostics/tvosdiagnostics.h>

#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )

#include <sound/soundrenderer/idasoundresource.h>
#include <radkey.hpp>

#if __has_include( <SDL2/SDL.h> )
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif

#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <string>

namespace {

const char* s_phase = "unknown";
const char* s_cluster = "unknown";

uint64_t s_cumReadUs = 0;
uint64_t s_cumStereoUs = 0;
uint64_t s_cumAlUs = 0;
unsigned int s_readEventCount = 0;

uint64_t s_cumClipSerialGapUs = 0;
unsigned int s_clipSerialGapCount = 0;
uint64_t s_cumClipWallUs = 0;
unsigned int s_clipLoadCount = 0;

unsigned int s_totalResourceCaptures = 0;

// Key string (e.g. "0x...") normalized — always safe post lockdown.
std::set< std::string > s_uniqueNormalizedKeys;
std::map< std::string, uintptr_t > s_firstResourceByNormKey;

// Literal filesystem paths (only when RegisterKeyLiteralPath ran for that key).
std::set< std::string > s_uniqueNormalizedLiteralPaths;
std::map< std::string, uintptr_t > s_firstResourceByNormLiteralPath;

std::map< radKey32, std::string > s_diagKeyToLiteralPath;

std::map< std::string, uint64_t > s_clipWallUsByCluster;

std::string NormalizeForDedup( const char* s )
{
    if ( s == NULL || s[0] == '\0' )
    {
        return std::string();
    }
    std::string out;
    out.reserve( strlen( s ) );
    for ( const char* p = s; *p != '\0'; ++p )
    {
        char c = *p;
        if ( c >= 'A' && c <= 'Z' )
        {
            c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
        }
        else if ( c == '\\' )
        {
            c = '/';
        }
        out.push_back( c );
    }
    return out;
}

const char* LookupLiteralPathCStr( radKey32 key )
{
    if ( key == 0 )
    {
        return NULL;
    }
    std::map< radKey32, std::string >::const_iterator it = s_diagKeyToLiteralPath.find( key );
    if ( it == s_diagKeyToLiteralPath.end() || it->second.empty() )
    {
        return NULL;
    }
    return it->second.c_str();
}

} // namespace

namespace AudioLoadDiag {

void SetPhaseCluster( const char* phase, const char* cluster )
{
    s_phase = ( phase != NULL && phase[0] != '\0' ) ? phase : "unknown";
    s_cluster = ( cluster != NULL && cluster[0] != '\0' ) ? cluster : "unknown";
    SRR2::Diagnostics::SetContextValue( "loading", s_phase );
    SRR2::Diagnostics::SetContextValue( "audioCluster", s_cluster );
}

void RegisterKeyLiteralPath( radKey32 key, const char* literalPath )
{
    if ( key == 0 || literalPath == NULL || literalPath[0] == '\0' )
    {
        return;
    }
    if ( s_diagKeyToLiteralPath.find( key ) != s_diagKeyToLiteralPath.end() )
    {
        return;
    }
    s_diagKeyToLiteralPath[ key ] = std::string( literalPath );
}

const char* LookupLiteralPathForKeyHex( const char* keyHex )
{
    if ( keyHex == NULL || keyHex[0] == '\0' )
    {
        return NULL;
    }
    radKey32 k = StringKeyToKey32( keyHex );
    return LookupLiteralPathCStr( k );
}

void LogResourceCapture(
    IDaSoundResource* resource,
    radKey32 resourceKey,
    const char* resourceKeyStr,
    bool streaming,
    bool alreadyCaptured )
{
    ++s_totalResourceCaptures;

    const char* pKey = ( resourceKeyStr != NULL && resourceKeyStr[0] != '\0' ) ? resourceKeyStr : "";
    const char* lit = LookupLiteralPathCStr( resourceKey );

    SRR2::Diagnostics::RecordAudioResourceKey( pKey, lit, streaming, alreadyCaptured );
    SRR2::Diagnostics::Tracef(
        SRR2::Diagnostics::AUDIO_LOAD,
        "resource_capture phase=%s cluster=%s key=%s path=%s streaming=%d ptr=%p alreadyCaptured=%d",
        s_phase,
        s_cluster,
        pKey,
        lit != NULL ? lit : "(key-only)",
        streaming ? 1 : 0,
        static_cast<void*>( resource ),
        alreadyCaptured ? 1 : 0 );

    if ( streaming || alreadyCaptured || pKey[0] == '\0' )
    {
        return;
    }

    const uintptr_t ptr = reinterpret_cast<uintptr_t>( resource );

    // --- Key-based duplicate detection (always meaningful post-lockdown) ---
    const std::string normKey = NormalizeForDedup( pKey );
    if ( !normKey.empty() )
    {
        if ( s_uniqueNormalizedKeys.insert( normKey ).second == false )
        {
            SRR2::Diagnostics::Anomalyf(
                SRR2::Diagnostics::AUDIO_LOAD,
                "[AUDIO_KEY_DUP_NORM] phase=%s cluster=%s resourceKey=%s normKey=%s resourcePtr=%p",
                s_phase,
                s_cluster,
                pKey,
                normKey.c_str(),
                static_cast<void*>( resource ) );
        }

        std::map< std::string, uintptr_t >::iterator kit = s_firstResourceByNormKey.find( normKey );
        if ( kit == s_firstResourceByNormKey.end() )
        {
            s_firstResourceByNormKey[ normKey ] = ptr;
        }
        else if ( kit->second != ptr )
        {
            SRR2::Diagnostics::Anomalyf(
                SRR2::Diagnostics::AUDIO_LOAD,
                "[AUDIO_RESOURCE_KEY_DUP] phase=%s cluster=%s normKey=%s resourceKey=%s firstResourcePtr=%p thisResourcePtr=%p",
                s_phase,
                s_cluster,
                normKey.c_str(),
                pKey,
                reinterpret_cast<void*>( kit->second ),
                static_cast<void*>( resource ) );
        }
    }

    // --- Literal path duplicate detection (only when we captured path at lockdown) ---
    if ( lit != NULL && lit[0] != '\0' )
    {
        const std::string normLit = NormalizeForDedup( lit );
        if ( !normLit.empty() )
        {
            if ( s_uniqueNormalizedLiteralPaths.insert( normLit ).second == false )
            {
                SRR2::Diagnostics::Anomalyf(
                    SRR2::Diagnostics::AUDIO_LOAD,
                    "[AUDIO_PATH_DUP_NORM] phase=%s cluster=%s path=%s normPath=%s resourceKey=%s resourcePtr=%p",
                    s_phase,
                    s_cluster,
                    lit,
                    normLit.c_str(),
                    pKey,
                    static_cast<void*>( resource ) );
            }

            std::map< std::string, uintptr_t >::iterator pit = s_firstResourceByNormLiteralPath.find( normLit );
            if ( pit == s_firstResourceByNormLiteralPath.end() )
            {
                s_firstResourceByNormLiteralPath[ normLit ] = ptr;
            }
            else if ( pit->second != ptr )
            {
                SRR2::Diagnostics::Anomalyf(
                    SRR2::Diagnostics::AUDIO_LOAD,
                    "[AUDIO_RESOURCE_PATH_DUP] phase=%s cluster=%s normPath=%s path=%s resourceKey=%s firstResourcePtr=%p thisResourcePtr=%p",
                    s_phase,
                    s_cluster,
                    normLit.c_str(),
                    lit,
                    pKey,
                    reinterpret_cast<void*>( pit->second ),
                    static_cast<void*>( resource ) );
            }
        }
    }
}

void EmitLoadSummaryCheckpoint( const char* checkpointId )
{
    const char* cp = ( checkpointId != NULL && checkpointId[0] != '\0' ) ? checkpointId : "unknown";

    SRR2::Diagnostics::Checkpointf(
        SRR2::Diagnostics::AUDIO_LOAD,
        "[AUDIO_LOAD_SUMMARY] checkpoint=%s total_captures=%u unique_norm_keys=%zu unique_norm_paths=%zu "
        "cum_read_us=%llu (events=%u) cum_stereo_us=%llu cum_al_us=%llu "
        "clip_loads=%u cum_clip_wall_us=%llu cum_clip_serial_gap_us=%llu clip_serial_gap_events=%u",
        cp,
        s_totalResourceCaptures,
        s_uniqueNormalizedKeys.size(),
        s_uniqueNormalizedLiteralPaths.size(),
        static_cast<unsigned long long>( s_cumReadUs ),
        s_readEventCount,
        static_cast<unsigned long long>( s_cumStereoUs ),
        static_cast<unsigned long long>( s_cumAlUs ),
        s_clipLoadCount,
        static_cast<unsigned long long>( s_cumClipWallUs ),
        static_cast<unsigned long long>( s_cumClipSerialGapUs ),
        s_clipSerialGapCount );

    for ( std::map< std::string, uint64_t >::const_iterator c = s_clipWallUsByCluster.begin();
          c != s_clipWallUsByCluster.end();
          ++c )
    {
        SRR2::Diagnostics::Summaryf(
            SRR2::Diagnostics::AUDIO_LOAD,
            "[AUDIO_TIMING_SUM] checkpoint=%s scope=clip_wall_by_cluster cluster=%s cum_wall_us=%llu",
            cp,
            c->first.c_str(),
            static_cast<unsigned long long>( c->second ) );
    }

    const uint64_t tot = s_cumReadUs + s_cumStereoUs + s_cumAlUs;
    const uint64_t denom = tot > 0 ? tot : 1ULL;
    SRR2::Diagnostics::Summaryf(
        SRR2::Diagnostics::AUDIO_LOAD,
        "[AUDIO_TIMING_SUM] checkpoint=%s scope=global read_us=%llu stereo_us=%llu al_us=%llu "
        "read_pct=%llu stereo_pct=%llu al_pct=%llu",
        cp,
        static_cast<unsigned long long>( s_cumReadUs ),
        static_cast<unsigned long long>( s_cumStereoUs ),
        static_cast<unsigned long long>( s_cumAlUs ),
        static_cast<unsigned long long>( ( 100ULL * s_cumReadUs ) / denom ),
        static_cast<unsigned long long>( ( 100ULL * s_cumStereoUs ) / denom ),
        static_cast<unsigned long long>( ( 100ULL * s_cumAlUs ) / denom ) );
}

} // namespace AudioLoadDiag

extern "C" {

void SRR2_AudioDiag_RecordReadUs( uint64_t microseconds )
{
    s_cumReadUs += microseconds;
    ++s_readEventCount;
    SRR2::Diagnostics::RecordAudioReadUs( microseconds );
}

void SRR2_AudioDiag_RecordStereoUs( uint64_t microseconds )
{
    s_cumStereoUs += microseconds;
    SRR2::Diagnostics::RecordAudioStereoUs( microseconds );
}

void SRR2_AudioDiag_RecordAlUs( uint64_t microseconds )
{
    s_cumAlUs += microseconds;
    SRR2::Diagnostics::RecordAudioAlUs( microseconds );
}

void SRR2_AudioDiag_RecordClipSerialGapUs( uint64_t microseconds )
{
    s_cumClipSerialGapUs += microseconds;
    ++s_clipSerialGapCount;
}

void SRR2_AudioDiag_RecordClipWallUs( uint64_t microseconds )
{
    s_cumClipWallUs += microseconds;
    ++s_clipLoadCount;
    const std::string clusterKey = std::string( s_cluster != NULL ? s_cluster : "unknown" );
    s_clipWallUsByCluster[ clusterKey ] += microseconds;
}

} // extern "C"

#else

extern "C" {

void SRR2_AudioDiag_RecordReadUs( uint64_t ) {}
void SRR2_AudioDiag_RecordStereoUs( uint64_t ) {}
void SRR2_AudioDiag_RecordAlUs( uint64_t ) {}
void SRR2_AudioDiag_RecordClipSerialGapUs( uint64_t ) {}
void SRR2_AudioDiag_RecordClipWallUs( uint64_t ) {}

}

#endif

#if defined( RAD_TVOS )
extern "C" void SRR2_Diagnostics_DumpAll( void )
{
    SRR2::Diagnostics::DumpAll();
}

extern "C" void SRR2_Diagnostics_DumpCrashContext( const char* reason, const char* file, int line )
{
    SRR2::Diagnostics::DumpCrashContext( reason, file, line );
}

extern "C" void SRR2_Diagnostics_SetContextValue( const char* key, const char* value )
{
    SRR2::Diagnostics::SetContextValue( key, value );
}
#endif
