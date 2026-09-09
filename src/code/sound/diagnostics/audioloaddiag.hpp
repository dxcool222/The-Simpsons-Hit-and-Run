//=============================================================================
// tvOS audio load diagnostics (log-only). No behavior changes to audio paths.
//=============================================================================

#pragma once

#include <radkey.hpp>

struct IDaSoundResource;

#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )

namespace AudioLoadDiag {

void SetPhaseCluster( const char* phase, const char* cluster );

// Called from SetResourceLockdown once per file slot, before m_pName is freed.
// Diagnostics-only copy of the literal path string for later logs (no audio ownership change).
void RegisterKeyLiteralPath( radKey32 key, const char* literalPath );

// Resolve a literal path registered for a key string like "0xabc123" (from GetFileKeyAt).
const char* LookupLiteralPathForKeyHex( const char* keyHex );

// Called once per CaptureResource (after refcount update and optional AllocateResource).
// resourceKeyStr should come from GetFileKeyAt (reads m_Key only; never m_pName).
void LogResourceCapture(
    IDaSoundResource* resource,
    radKey32 resourceKey,
    const char* resourceKeyStr,
    bool streaming,
    bool alreadyCaptured );

// Emits [AUDIO_LOAD_SUMMARY] with current cumulative counters (does not change menu flags).
void EmitLoadSummaryCheckpoint( const char* checkpointId );

} // namespace AudioLoadDiag

#else

namespace AudioLoadDiag {
inline void SetPhaseCluster( const char*, const char* ) {}
inline void RegisterKeyLiteralPath( radKey32, const char* ) {}
inline const char* LookupLiteralPathForKeyHex( const char* ) { return NULL; }
inline void LogResourceCapture( IDaSoundResource*, radKey32, const char*, bool, bool ) {}
inline void EmitLoadSummaryCheckpoint( const char* ) {}
} // namespace AudioLoadDiag

#endif
