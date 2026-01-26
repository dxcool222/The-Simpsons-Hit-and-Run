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

#ifdef RAD_TVOS
#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif
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
    Stop( );

#ifdef RAD_TVOS
    ref< radSoundHalBufferWin > xOldBuffer = m_xRadSoundHalBufferWin;
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
        unsigned int channels = m_xRadSoundHalBufferWin->GetFormat()->GetNumberOfChannels();
        bool useQueuing = isStreaming && m_xRadSoundHalBufferWin->UsesBufferQueuing();  // Mono streaming uses queuing
        
        if (!useQueuing) {
            alSourcei( m_Source, AL_BUFFER, m_xRadSoundHalBufferWin->GetBuffer() );
        } else {
            // CRITICAL: Hard reset for new stream identity
            // This prevents "old dude voice everywhere" bug
            m_xRadSoundHalBufferWin->FlushBufferQueue();
            
            // CRITICAL: Force looping OFF for streaming sources
            // Looping breaks buffer reclaim and causes "no free buffers"
            alSourcei( m_Source, AL_LOOPING, AL_FALSE );
            alGetError();  // Clear any error
            
            SDL_Log("[VOICE_QUEUE] Source %u: hard reset + AL_LOOPING=FALSE for mono stream", m_Source);
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
            xOldBuffer->FlushBufferQueue();
            xOldBuffer->SetAttachedSource( 0 );
        }
#endif
        alSourcei( m_Source, AL_BUFFER, 0 );
        alSourcei( m_Source, AL_LOOPING, AL_FALSE );
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
        s_voicePlayCount++;
        
        // Log first 30 voice plays + every 50th after
        if (s_voicePlayCount <= 30 || (s_voicePlayCount % 50) == 0) {
            ALint bufferID = 0;
            alGetSourcei(m_Source, AL_BUFFER, &bufferID);
            
            // Get buffer info if available
            ALint bufferSize = 0, bufferFreq = 0, bufferBits = 0, bufferChannels = 0;
            if (bufferID != 0) {
                alGetBufferi(bufferID, AL_SIZE, &bufferSize);
                alGetBufferi(bufferID, AL_FREQUENCY, &bufferFreq);
                alGetBufferi(bufferID, AL_BITS, &bufferBits);
                alGetBufferi(bufferID, AL_CHANNELS, &bufferChannels);
            }
            
            SDL_Log("[VOICE_PLAY] #%u source=%u buffer=%d size=%d freq=%d bits=%d ch=%d vol=%.2f trim=%.2f",
                    s_voicePlayCount, m_Source, bufferID, bufferSize, bufferFreq, bufferBits, bufferChannels,
                    m_Volume, m_Trim);
            
            // ALWAYS log mono streaming voices to diagnose garbled audio
            if (bufferChannels == 1 && m_xRadSoundHalBufferWin != NULL && m_xRadSoundHalBufferWin->IsStreaming()) {
                ALfloat pitch = 1.0f;
                alGetSourcef(m_Source, AL_PITCH, &pitch);
                SDL_Log("[MONO_VOICE] source=%u buffer=%d streaming=YES pitch=%.3f size=%d freq=%d",
                        m_Source, bufferID, pitch, bufferSize, bufferFreq);
            }
        }
#endif
        alSourcePlay(m_Source);
#ifdef RAD_TVOS
        ALenum err = alGetError();
        if (err != AL_NO_ERROR) {
            SDL_Log("[VOICE_PLAY] ERROR: alSourcePlay failed! source=%u err=0x%x", m_Source, err);
        }
        
        // Verify source is actually playing after alSourcePlay
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
            
            SDL_Log("[VOICE_PLAY] #%u POST-Play: source=%u state=%s gain=%.3f",
                    s_voicePlayCount, m_Source, stateStr, sourceGain);
        }
#else
        rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::Play failed");
#endif
    }
}

void radSoundHalVoiceWin::Stop( void )
{
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

        alSourceStop(m_Source);

        rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::Stop failed");
    }
}

bool radSoundHalVoiceWin::IsPlaying( void )
{
    return IsHardwarePlaying( );
}

unsigned int radSoundHalVoiceWin::GetPlaybackPositionInSamples( void )
{
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

    alSourcef(m_Source, AL_PITCH, m_Pitch);

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

    alSource3f(m_Source, AL_POSITION, p->m_Position.m_x, p->m_Position.m_y, -p->m_Position.m_z);
    alSource3f(m_Source, AL_VELOCITY, p->m_Velocity.m_x, p->m_Velocity.m_y, -p->m_Velocity.m_z);
    alSource3f(m_Source, AL_DIRECTION, p->m_Direction.m_x, p->m_Direction.m_y, -p->m_Direction.m_z);
    alSourcef(m_Source, AL_CONE_INNER_ANGLE, p->m_ConeOuterAngle);
    alSourcef(m_Source, AL_CONE_OUTER_ANGLE, p->m_ConeInnerAngle);
    alSourcef(m_Source, AL_CONE_OUTER_GAIN, p->m_ConeOuterGain);
    alSourcef(m_Source, AL_REFERENCE_DISTANCE, p->m_ReferenceDistance);
    alSourcef(m_Source, AL_MAX_DISTANCE, p->m_MaxDistance);
    alSourcef(m_Source, AL_ROLLOFF_FACTOR, listenerRolloffFactor);
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




