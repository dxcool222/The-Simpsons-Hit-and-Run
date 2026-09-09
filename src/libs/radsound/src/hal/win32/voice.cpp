//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

//========================================================================
// Include Files
//========================================================================

#include "pch.hpp"
#include "voice.hpp"
#include "listener.hpp"
#include "system.hpp"
#include <diagnostics/tvosdiagnostics.h>

#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
static unsigned int s_voicePlayCount = 0;
#endif

//============================================================================
// Static Initialization
//============================================================================

template<> radSoundHalVoiceWin * radLinkedClass<radSoundHalVoiceWin>::s_pLinkedClassHead = NULL;
template<> radSoundHalVoiceWin * radLinkedClass<radSoundHalVoiceWin>::s_pLinkedClassTail = NULL;

//========================================================================
// radSoundHalVoiceWin::radSoundHalVoiceWin
//========================================================================

radSoundHalVoiceWin::radSoundHalVoiceWin( void )
    :
	m_Priority( 5 ),
    m_Pitch( 1.0f ),
    m_Volume( 1.0f ),
    m_MuteFactor( 1.0f ),
    m_Trim( 1.0f ),
	m_xRadSoundHalPositionalGroup( NULL )
{
    alGenSources( 1, &m_Source );
    alSourcei( m_Source, AL_SOURCE_RELATIVE, AL_TRUE );
#ifdef RAD_TVOS
    SRR2::Diagnostics::Tracef( SRR2::Diagnostics::AUDIO_SOURCE_LIFETIME, "source_create sourceId=%u", m_Source );
#endif
}

//========================================================================
// radSoundHalVoiceWin::~radSoundHalVoiceWin
//========================================================================

radSoundHalVoiceWin::~radSoundHalVoiceWin
(
    void
)
{
    //
    // Tell our buffer object that we are done with the voice/(buffer), our
    // buffer object manages the lifetime if its voices.
    //

    Stop( );

	if ( m_xRadSoundHalPositionalGroup != NULL )
	{
		m_xRadSoundHalPositionalGroup->RemovePositionalEntity( this );
	}

    if (m_Source)
    {
#ifdef RAD_TVOS
        SRR2::Diagnostics::RecordAudioSourceStop( m_Source, "destroy" );
#endif
        alDeleteSources(1, &m_Source);
    }
}

void radSoundHalVoiceWin::SetPriority( unsigned int priority )
{
	m_Priority = priority;
}

unsigned int radSoundHalVoiceWin::GetPriority( void )
{
	return m_Priority;
}

//========================================================================
// radSoundHalVoiceWin::SetBuffer
//========================================================================

void radSoundHalVoiceWin::SetBuffer( IRadSoundHalBuffer * pIRadSoundHalBuffer )
{
#ifdef RAD_TVOS
    ALint oldBufferId = 0;
    ALint oldLooping = 0;
    ALint oldState = 0;
    alGetSourcei( m_Source, AL_BUFFER, &oldBufferId );
    alGetSourcei( m_Source, AL_LOOPING, &oldLooping );
    alGetSourcei( m_Source, AL_SOURCE_STATE, &oldState );
    alGetError();
#endif

    Stop( );

#ifdef RAD_TVOS
    ref< radSoundHalBufferWin > xOldBuffer = m_xRadSoundHalBufferWin;
    const bool oldQueued = xOldBuffer != NULL && xOldBuffer->UsesBufferQueuing();
    if ( xOldBuffer != NULL && xOldBuffer->UsesBufferQueuing() )
    {
        xOldBuffer->SetQueuePlaybackRequested( false );
        xOldBuffer->FlushBufferQueue();
        xOldBuffer->SetAttachedSource( 0 );
    }
    alSourceStop( m_Source );
    alSourcei( m_Source, AL_LOOPING, AL_FALSE );
    alSourcei( m_Source, AL_BUFFER, 0 );
    ALenum resetErr = alGetError();
    SRR2::Diagnostics::RecordAudioSourceReset(
        m_Source,
        (uint32_t)oldBufferId,
        oldState == AL_PLAYING,
        oldLooping == AL_TRUE,
        oldQueued );
    SRR2::Diagnostics::RecordOpenALError( "SetBuffer.reset", m_Source, (uint32_t)resetErr );
#endif

    m_xRadSoundHalBufferWin = NULL;

	ref< IRadSoundHalAudioFormat > pOldIRadSoundHalAudioFormat = m_xIRadSoundHalAudioFormat;
    m_xIRadSoundHalAudioFormat = NULL;

    if ( pIRadSoundHalBuffer != NULL )
    {
        m_xRadSoundHalBufferWin = static_cast< radSoundHalBufferWin * >( pIRadSoundHalBuffer );
        rAssert( m_xRadSoundHalBufferWin != NULL );

#ifdef RAD_TVOS
        // Track which source this buffer is attached to
        m_xRadSoundHalBufferWin->SetAttachedSource( m_Source );
        
        // For buffer queuing (mono streaming): Do NOT attach buffer statically with AL_BUFFER
        // Buffers will be queued dynamically via alSourceQueueBuffers
        // For non-queuing buffers: attach normally
        bool isStreaming = m_xRadSoundHalBufferWin->IsStreaming();
        bool useQueuing = isStreaming && m_xRadSoundHalBufferWin->UsesBufferQueuing();  // Mono streaming uses queuing
        m_xRadSoundHalBufferWin->SetQueuePlaybackRequested( false );
        
        if (!useQueuing) {
            alSourcei( m_Source, AL_BUFFER, m_xRadSoundHalBufferWin->GetBuffer() );
            alSourcei( m_Source, AL_SAMPLE_OFFSET, 0 );
        } else {
            // CRITICAL: Hard reset for new stream identity
            // This prevents "old dude voice everywhere" bug
            m_xRadSoundHalBufferWin->FlushBufferQueue();
            
            // CRITICAL: Force looping OFF for streaming sources
            // Looping breaks buffer reclaim and causes "no free buffers"
            alSourcei( m_Source, AL_LOOPING, AL_FALSE );
            alGetError();  // Clear any error
            
            TVOS_AUDIO_DIAG("[VOICE_QUEUE] Source %u: hard reset + AL_LOOPING=FALSE for mono stream", m_Source);
        }
#else
        alSourcei( m_Source, AL_BUFFER, m_xRadSoundHalBufferWin->GetBuffer() );
#endif

        // Now get the format of the buffer, we'll just store it here
        m_xIRadSoundHalAudioFormat = m_xRadSoundHalBufferWin->GetFormat( );


		//
		// The new format had better be the same as the old one 
		// (if the old one isn't null
		//
		rAssert
		( 
			pOldIRadSoundHalAudioFormat != NULL ?
			m_xIRadSoundHalAudioFormat->Matches( pOldIRadSoundHalAudioFormat ) :
			true
		);

#ifdef RAD_TVOS
        // CRITICAL: Streaming sources must NEVER have AL_LOOPING=TRUE
        // Looping breaks buffer reclaim in queue model ("Unqueueing from looping source" bug)
        if ( m_xRadSoundHalBufferWin->IsStreaming() && m_xRadSoundHalBufferWin->UsesBufferQueuing() ) {
            alSourcei( m_Source, AL_LOOPING, AL_FALSE );
        } else {
            alSourcei( m_Source, AL_LOOPING, m_xRadSoundHalBufferWin->IsLooping() );
        }
        {
            ALint attachedLooping = 0;
            alGetSourcei( m_Source, AL_LOOPING, &attachedLooping );
            SRR2::Diagnostics::RecordAudioSourceAttach(
                m_Source,
                (uint32_t)m_xRadSoundHalBufferWin->GetBuffer(),
                attachedLooping == AL_TRUE,
                isStreaming,
                useQueuing );
        }
#else
        alSourcei( m_Source, AL_LOOPING, m_xRadSoundHalBufferWin->IsLooping() );
#endif
    }
    else
    {
#ifdef RAD_TVOS
        // Clear attached source tracking and flush queue before detaching
        if ( xOldBuffer != NULL )
        {
            // Flush any queued buffers first
            xOldBuffer->SetQueuePlaybackRequested( false );
            xOldBuffer->FlushBufferQueue();
            xOldBuffer->SetAttachedSource( 0 );
        }
#endif
        alSourcei( m_Source, AL_BUFFER, 0 );
        alSourcei( m_Source, AL_LOOPING, AL_FALSE );
#ifdef RAD_TVOS
        SRR2::Diagnostics::RecordAudioSourceAttach( m_Source, 0, false, false, false );
#endif
    }

    if( m_xRadSoundHalPositionalGroup != NULL )
    {
        OnApplyPositionalInfo( 1.0f );
    }
}

IRadSoundHalBuffer * radSoundHalVoiceWin::GetBuffer( void )
{
    return m_xRadSoundHalBufferWin;
}

void radSoundHalVoiceWin::Play( )
{
    if (IsHardwarePlaying( ) == false)
    {
#ifdef RAD_TVOS
        const bool useQueue = ( m_xRadSoundHalBufferWin != NULL &&
                                m_xRadSoundHalBufferWin->UsesBufferQueuing() );
        if ( useQueue )
        {
            m_xRadSoundHalBufferWin->SetQueuePlaybackRequested( true );
        }
#endif

#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
        s_voicePlayCount++;
        ALint bufferID = 0;
        ALint looping = 0;
        alGetSourcei(m_Source, AL_BUFFER, &bufferID);
        alGetSourcei(m_Source, AL_LOOPING, &looping);

        ALint bufferSize = 0, bufferFreq = 0, bufferBits = 0, bufferChannels = 0;
        if (bufferID != 0) {
            alGetBufferi(bufferID, AL_SIZE, &bufferSize);
            alGetBufferi(bufferID, AL_FREQUENCY, &bufferFreq);
            alGetBufferi(bufferID, AL_BITS, &bufferBits);
            alGetBufferi(bufferID, AL_CHANNELS, &bufferChannels);
        } else if ( m_xRadSoundHalBufferWin != NULL && m_xRadSoundHalBufferWin->GetFormat() != NULL ) {
            bufferID = (ALint)m_xRadSoundHalBufferWin->GetBuffer();
            bufferSize = (ALint)m_xRadSoundHalBufferWin->GetSizeInBytes();
            bufferFreq = (ALint)m_xRadSoundHalBufferWin->GetFormat()->GetSampleRate();
            bufferBits = (ALint)m_xRadSoundHalBufferWin->GetFormat()->GetBitResolution();
            bufferChannels = (ALint)m_xRadSoundHalBufferWin->GetFormat()->GetNumberOfChannels();
        }
        // Probe calls above can leave a sticky AL error; clear before Play.
        alGetError();

        uint32_t expectedMs = 0;
        if ( bufferSize > 0 && bufferFreq > 0 && bufferBits > 0 && bufferChannels > 0 )
        {
            expectedMs = (uint32_t)( ( (uint64_t)bufferSize * 8000ULL ) /
                         ( (uint64_t)bufferFreq * (uint64_t)bufferBits * (uint64_t)bufferChannels ) );
        }
        SRR2::Diagnostics::RecordAudioSourcePlay(
            m_Source,
            (uint32_t)bufferID,
            looping == AL_TRUE,
            m_xRadSoundHalBufferWin != NULL && m_xRadSoundHalBufferWin->IsStreaming(),
            expectedMs );
        SRR2::Diagnostics::Tracef(
            SRR2::Diagnostics::AUDIO_PLAYBACK,
            "voice_play sourceId=%u buffer=%d size=%d freq=%d bits=%d ch=%d vol=%.2f trim=%.2f",
            m_Source,
            bufferID,
            bufferSize,
            bufferFreq,
            bufferBits,
            bufferChannels,
            m_Volume,
            m_Trim );
#endif
#ifdef RAD_TVOS
        // Streaming queue sources: don't Play until at least one buffer is queued.
        // Premature Play on Soft/macOS leaves the source in a bad state (INVALID_VALUE
        // on later Stop/Play) and matches the accumulating OPENAL_ERROR evidence.
        if ( useQueue && m_xRadSoundHalBufferWin->GetQueuedBufferCount() == 0 )
        {
            return;
        }
        alGetError();
#endif
        alSourcePlay(m_Source);
#ifdef RAD_TVOS
        ALenum err = alGetError();
        SRR2::Diagnostics::RecordOpenALError( "alSourcePlay", m_Source, (uint32_t)err );
        
        // Verify source is actually playing after alSourcePlay
#if defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
        if (s_voicePlayCount <= 30 || (s_voicePlayCount % 50) == 0) {
            ALint sourceState = 0;
            alGetSourcei(m_Source, AL_SOURCE_STATE, &sourceState);
            
            ALfloat sourceGain = 0.0f;
            alGetSourcef(m_Source, AL_GAIN, &sourceGain);
            
            const char* stateStr = "UNKNOWN";
            switch(sourceState) {
                case AL_INITIAL: stateStr = "INITIAL"; break;
                case AL_PLAYING: stateStr = "PLAYING"; break;
                case AL_PAUSED: stateStr = "PAUSED"; break;
                case AL_STOPPED: stateStr = "STOPPED"; break;
            }
            
            SRR2::Diagnostics::Tracef(
                SRR2::Diagnostics::AUDIO_PLAYBACK,
                "voice_post_play count=%u sourceId=%u state=%s gain=%.3f",
                s_voicePlayCount,
                m_Source,
                stateStr,
                sourceGain );
        }
#endif
#else
        rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::Play failed");
#endif
    }
}

void radSoundHalVoiceWin::Stop( void )
{
#ifdef RAD_TVOS
    if ( m_xRadSoundHalBufferWin != NULL && m_xRadSoundHalBufferWin->UsesBufferQueuing() ) {
        m_xRadSoundHalBufferWin->SetQueuePlaybackRequested( false );
    }
#endif

    if (IsHardwarePlaying( ) == true)
    {
#ifdef RAD_DEBUG
        extern bool g_VoiceStoppingPlayingSilence;

        if ( g_VoiceStoppingPlayingSilence == false )
        {
            if ( ( m_Trim * m_Volume ) > 0.0f )
            {
                rDebugPrintf( "radsound: TRC Violation: Voice stopped while playing and (trim * volume) > 0.0f\n" );
            }
        }
#endif // RAD_DEBUG

        alGetError(); // drop sticky errors from prior positional/probe calls
        alSourceStop(m_Source);

#ifdef RAD_TVOS
        ALenum stopErr = alGetError();
        SRR2::Diagnostics::RecordAudioSourceStop( m_Source, "stop" );
        SRR2::Diagnostics::RecordOpenALError( "alSourceStop", m_Source, (uint32_t)stopErr );
#else
        rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::Stop failed");
#endif
    }
}

bool radSoundHalVoiceWin::IsPlaying( void )
{
    return IsHardwarePlaying( );
}

unsigned int radSoundHalVoiceWin::GetPlaybackPositionInSamples( void )
{
#ifdef RAD_TVOS
    if ( m_xRadSoundHalBufferWin != NULL && m_xRadSoundHalBufferWin->UsesBufferQueuing() ) {
        return m_xRadSoundHalBufferWin->GetQueuedPlaybackPositionInSamples();
    }
#endif

    ALint currentPosition = 0;
    alGetSourcei( m_Source, AL_SAMPLE_OFFSET, &currentPosition );
    rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::GetPlaybackPositionInSamples failed");

    return currentPosition;
}

void radSoundHalVoiceWin::SetPlaybackPositionInSamples( unsigned int positionInSamples )
{
    alSourcei( m_Source, AL_SAMPLE_OFFSET, positionInSamples );
    rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::SetPlaybackPositionInSamples failed");
}

void radSoundHalVoiceWin::SetMuted( bool muted)
{
    if ( muted != GetMuted( ) )
    {
        m_MuteFactor = muted ? 0.0f : 1.0f;
        SetVolumeInternal( );
    }
}

bool radSoundHalVoiceWin::GetMuted( void )
{
    return m_MuteFactor == 0.0f ? true : false;
}

void radSoundHalVoiceWin::SetVolume( float volume )
{
	::radSoundVerifyAnalogVolume( volume );

    if ( volume != m_Volume )
    {
        ::radSoundVerifyChangeThreshold(
            IsHardwarePlaying( ), "Volume", volume, m_Volume, radSoundVolumeChangeThreshold );

		m_Volume = volume;

        SetVolumeInternal( );
    }

}

float radSoundHalVoiceWin::GetVolume( void )
{
    return m_Volume;
}

void radSoundHalVoiceWin::SetTrim( float trim )
{
	::radSoundVerifyAnalogVolume( trim );

    if ( m_Trim != trim )
    {
        ::radSoundVerifyChangeThreshold(
            IsHardwarePlaying( ), "Trim", trim, m_Trim, radSoundVolumeChangeThreshold );

        m_Trim = trim;

        SetVolumeInternal( );
    }
}
    
float radSoundHalVoiceWin::GetTrim( void )
{
    return m_Trim;
}

void radSoundHalVoiceWin::SetPitch( float pitch )
{
    ::radSoundVerifyAnalogPitch( pitch );

    if ( m_Pitch != pitch )
    {
        m_Pitch = pitch;

		SetPitchInternal( );
    }
}

float radSoundHalVoiceWin::GetPitch( void )
{
    return m_Pitch;
}

void radSoundHalVoiceWin::SetPan( float pan )
{
    ::radSoundVerifyAnalogPan( pan );

    rWarningMsg(false, "voice::SetPan not available in win32");
}

float radSoundHalVoiceWin::GetPan( void )
{
    rWarningMsg(false, "voice::GetPan not available in win32");
    return 0.0f;
}

radSoundAuxMode radSoundHalVoiceWin::GetAuxMode( unsigned int aux )
{
    rWarningMsg( false, "voice::GetAuxMode not available in win32" );
    return radSoundAuxMode_PreFader;
}

void radSoundHalVoiceWin::SetAuxMode( unsigned int aux, radSoundAuxMode  mode )
{
    rWarningMsg( false, "voice::SetAuxMode not available in win32" );
}

float radSoundHalVoiceWin::GetAuxGain( unsigned int aux )
{
    rWarningMsg( false, "voice::GetAuxGain not available in win32" );
    return 1.0f;
}

void radSoundHalVoiceWin::SetAuxGain( unsigned int aux, float gain )
{
    rWarningMsg( false, "voice::SetAuxGain not available in win32" );
}

//========================================================================
// Function radSoundHalVoiceWin::IsHardwarePlaying
//========================================================================

bool radSoundHalVoiceWin::IsHardwarePlaying( void )
{
    ALint state;
    alGetSourcei(m_Source, AL_SOURCE_STATE, &state);
    rWarningMsg( alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::IsHardwarePlaying failed");

    // Check our internal flag of the last known "play state", if our flag
    // is playing but the hardware voice has stopped it means we haven't notified
    // the client that the voice was done

    return ( state == AL_PLAYING );
}

//========================================================================
// radSoundHalVoiceWin::SetVolumeInternal
//========================================================================

void radSoundHalVoiceWin::SetVolumeInternal( void )
{
    float volume = m_Trim * m_Volume * m_MuteFactor;

	alSourcef( m_Source, AL_GAIN, ::radSoundVolumeDbToHardwareWin( ::radSoundVolumeAnalogToDb( volume ) ) );

    rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::SetVolumeInternal failed!");
}

//========================================================================
// radSoundHalVoiceWin::SetPitchInternal
//========================================================================

void radSoundHalVoiceWin::SetPitchInternal( void )
{
    ::radSoundVerifyAnalogPitch(m_Pitch);

    // OpenAL rejects pitch <= 0 with AL_INVALID_VALUE; clamp for Soft/macOS.
    const float pitch = ( m_Pitch > 0.001f ) ? m_Pitch : 0.001f;
    alSourcef(m_Source, AL_PITCH, pitch);

    rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::SetPitchInternal failed!");
}

//========================================================================
// radSoundHalVoiceWin::SetPositionalGroup
//========================================================================

/* virtual */ void radSoundHalVoiceWin::SetPositionalGroup
( 
	IRadSoundHalPositionalGroup * pIRadSoundHalPositionalGroup 
)
{
	radSoundHalPositionalGroup * pRadSoundHalPositionalGroup
		= dynamic_cast< radSoundHalPositionalGroup * >(
			pIRadSoundHalPositionalGroup );

    if ( pRadSoundHalPositionalGroup != m_xRadSoundHalPositionalGroup )
    {
	    if ( pRadSoundHalPositionalGroup != m_xRadSoundHalPositionalGroup )
        {
		    if ( m_xRadSoundHalPositionalGroup != NULL )
		    {
			    m_xRadSoundHalPositionalGroup->RemovePositionalEntity( this );
		    }

		    m_xRadSoundHalPositionalGroup = pRadSoundHalPositionalGroup;

		    if ( m_xRadSoundHalPositionalGroup != NULL )
		    {
			    m_xRadSoundHalPositionalGroup->AddPositionalEntity( this );
		    }
	    }

        ref<radSoundHalSystem> refSystem = radSoundHalSystem::GetInstance();
        for (unsigned int i = 0; i < refSystem->GetNumAuxSends(); i++)
        {
            alSource3i( m_Source, AL_AUXILIARY_SEND_FILTER,
                refSystem->GetOpenALAuxSlot( i ),
                i, 0 );
            rWarningMsg( alGetError() == AL_NO_ERROR, "Failed to set the source aux send filter" );
        }
    }

    if( m_xRadSoundHalPositionalGroup != NULL )
    {
        OnApplyPositionalInfo( 1.0f );
    }
    else
    {
        alSource3f( m_Source, AL_POSITION, 0.0f, 0.0f, 0.0f );
        alSource3f( m_Source, AL_VELOCITY, 0.0f, 0.0f, 0.0f );
        alSource3f( m_Source, AL_DIRECTION, 0.0f, 0.0f, 0.0f );
        alSourcei( m_Source, AL_CONE_INNER_ANGLE, 360 );
        alSourcei( m_Source, AL_CONE_OUTER_ANGLE, 360 );
        alSourcef( m_Source, AL_CONE_OUTER_GAIN, 1.0f );
        		alSourcef( m_Source, AL_REFERENCE_DISTANCE, 1.0f );
		alSourcef( m_Source, AL_MAX_DISTANCE, 1000.0f );
		alSourcef( m_Source, AL_ROLLOFF_FACTOR, 0.0f );
		alSourcei( m_Source, AL_SOURCE_RELATIVE, AL_TRUE );
	}
}

//========================================================================
// radSoundHalVoiceWin::GetPositionalGroup
//========================================================================

/* virtual */ IRadSoundHalPositionalGroup * radSoundHalVoiceWin::GetPositionalGroup
(	
	void 
)
{
	return m_xRadSoundHalPositionalGroup;
}


//========================================================================
// radSoundHalVoiceWin::SetPositionalGroup
//========================================================================

/* virtual */ void radSoundHalVoiceWin::OnApplyPositionalInfo( float listenerRolloffFactor )
{
	SetVolumeInternal( );

    radSoundHalPositionalGroup* p = m_xRadSoundHalPositionalGroup;
    rAssert( p );

    auto finiteOr = []( float v, float fallback ) -> float
    {
        return ( v == v && v != 1e30f && v != -1e30f ) ? v : fallback;
    };

    const float px = finiteOr( p->m_Position.m_x, 0.0f );
    const float py = finiteOr( p->m_Position.m_y, 0.0f );
    const float pz = finiteOr( p->m_Position.m_z, 0.0f );
    const float vx = finiteOr( p->m_Velocity.m_x, 0.0f );
    const float vy = finiteOr( p->m_Velocity.m_y, 0.0f );
    const float vz = finiteOr( p->m_Velocity.m_z, 0.0f );
    const float dx = finiteOr( p->m_Direction.m_x, 0.0f );
    const float dy = finiteOr( p->m_Direction.m_y, 0.0f );
    const float dz = finiteOr( p->m_Direction.m_z, -1.0f );

    float coneInner = finiteOr( p->m_ConeOuterAngle, 360.0f );
    float coneOuter = finiteOr( p->m_ConeInnerAngle, 360.0f );
    if ( coneInner < 0.0f ) coneInner = 0.0f;
    if ( coneInner > 360.0f ) coneInner = 360.0f;
    if ( coneOuter < 0.0f ) coneOuter = 0.0f;
    if ( coneOuter > 360.0f ) coneOuter = 360.0f;

    float coneGain = finiteOr( p->m_ConeOuterGain, 0.0f );
    if ( coneGain < 0.0f ) coneGain = 0.0f;
    if ( coneGain > 1.0f ) coneGain = 1.0f;

    float refDist = finiteOr( p->m_ReferenceDistance, 1.0f );
    float maxDist = finiteOr( p->m_MaxDistance, 1000.0f );
    if ( refDist < 0.001f ) refDist = 0.001f;
    if ( maxDist < refDist ) maxDist = refDist;

    float rolloff = finiteOr( listenerRolloffFactor, 1.0f );
    if ( rolloff < 0.0f ) rolloff = 0.0f;

    alGetError();
    alSource3f(m_Source, AL_POSITION, px, py, -pz);
    alSource3f(m_Source, AL_VELOCITY, vx, vy, -vz);
    alSource3f(m_Source, AL_DIRECTION, dx, dy, -dz);
    alSourcef(m_Source, AL_CONE_INNER_ANGLE, coneInner);
    alSourcef(m_Source, AL_CONE_OUTER_ANGLE, coneOuter);
    alSourcef(m_Source, AL_CONE_OUTER_GAIN, coneGain);
    alSourcef(m_Source, AL_REFERENCE_DISTANCE, refDist);
    alSourcef(m_Source, AL_MAX_DISTANCE, maxDist);
    alSourcef(m_Source, AL_ROLLOFF_FACTOR, rolloff);
    alSourcei(m_Source, AL_SOURCE_RELATIVE, AL_FALSE);

    rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::OnApplyPositionalInfo Failed.\n");
}

//========================================================================
// ::radSoundhalVoiceCreate
//========================================================================


IRadSoundHalVoice * radSoundHalVoiceCreate( radMemoryAllocator allocator )
{
    return new ( "radSoundHalVoiceWin", allocator ) radSoundHalVoiceWin( );
}
