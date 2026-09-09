//=============================================================================
// C bridge so radsound can record timings without including game headers.
// Implemented in audioloaddiag.cpp when RAD_TVOS_AUDIO_DIAGNOSTICS is on.
//=============================================================================

#pragma once

#include <cstdint>

#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )

extern "C" {

void SRR2_AudioDiag_RecordReadUs( uint64_t microseconds );
void SRR2_AudioDiag_RecordStereoUs( uint64_t microseconds );
void SRR2_AudioDiag_RecordAlUs( uint64_t microseconds );
void SRR2_AudioDiag_RecordClipSerialGapUs( uint64_t microseconds );
void SRR2_AudioDiag_RecordClipWallUs( uint64_t microseconds );

}

#else

inline void SRR2_AudioDiag_RecordReadUs( uint64_t ) {}
inline void SRR2_AudioDiag_RecordStereoUs( uint64_t ) {}
inline void SRR2_AudioDiag_RecordAlUs( uint64_t ) {}
inline void SRR2_AudioDiag_RecordClipSerialGapUs( uint64_t ) {}
inline void SRR2_AudioDiag_RecordClipWallUs( uint64_t ) {}

#endif
