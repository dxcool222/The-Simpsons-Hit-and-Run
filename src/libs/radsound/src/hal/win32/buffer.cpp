//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


//============================================================================
// Include Files
//============================================================================

#include "pch.hpp"
#include <stdio.h>
#include "buffer.hpp"
#include "bufferloader.hpp"
#include "system.hpp"
#include "radinprogext.h"

#ifdef RAD_TVOS
#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif

// Audio diagnostic counters
static unsigned int s_audioBufferCount = 0;
static unsigned int s_alBufferDataCallCount = 0;

// Helper to log ALL alBufferData calls and catch any mono uploads
static inline void tvOS_alBufferData_Logged(ALuint buffer, ALenum format, const void* data, ALsizei size, ALsizei freq, const char* caller) {
    s_alBufferDataCallCount++;
    
    const char* formatStr = "UNKNOWN";
    bool isMono = false;
    switch(format) {
        case AL_FORMAT_MONO8:    formatStr = "MONO8";    isMono = true; break;
        case AL_FORMAT_MONO16:   formatStr = "MONO16";   isMono = true; break;
        case AL_FORMAT_STEREO8:  formatStr = "STEREO8";  break;
        case AL_FORMAT_STEREO16: formatStr = "STEREO16"; break;
    }
    
    // ALWAYS log mono uploads - these are bugs on tvOS!
    if (isMono) {
        SDL_Log("[MONO_UPLOAD_BUG!] #%u %s: buffer=%u format=%s size=%d freq=%d - THIS SHOULD NOT HAPPEN!",
                s_alBufferDataCallCount, caller, buffer, formatStr, size, freq);
    }
    // Log first 10 stereo uploads for verification
    else if (s_alBufferDataCallCount <= 10) {
        SDL_Log("[AL_UPLOAD] #%u %s: buffer=%u format=%s size=%d freq=%d",
                s_alBufferDataCallCount, caller, buffer, formatStr, size, freq);
    }
    
    // Also log first 4 samples if 16-bit to verify PCM data sanity
    if (s_alBufferDataCallCount <= 5 && size >= 8 && data != NULL) {
        const int16_t* samples = (const int16_t*)data;
        SDL_Log("[PCM_VERIFY] #%u samples=[%d,%d,%d,%d] (expect small values near zero for silence, or typical audio range)",
                s_alBufferDataCallCount, samples[0], samples[1], samples[2], samples[3]);
    }
    
    alBufferData(buffer, format, data, size, freq);
}

#define TVOS_AL_BUFFER_DATA(buf, fmt, data, size, freq, caller) tvOS_alBufferData_Logged(buf, fmt, data, size, freq, caller)
#else
#define TVOS_AL_BUFFER_DATA(buf, fmt, data, size, freq, caller) alBufferData(buf, fmt, data, size, freq)
#endif

const unsigned int RADSOUNDHAL_BUFFER_CHANNEL_ALIGNMENT = 1;

//============================================================================
// Static Initialization
//============================================================================

template<> radSoundHalBufferWin * radLinkedClass<radSoundHalBufferWin>::s_pLinkedClassHead = NULL;
template<> radSoundHalBufferWin * radLinkedClass<radSoundHalBufferWin>::s_pLinkedClassTail = NULL;

//========================================================================
// radSoundHalBufferWin::~radSoundHalBufferWin
//========================================================================

radSoundHalBufferWin::~radSoundHalBufferWin( void )
{
#ifdef RAD_TVOS
    // Clean up buffer pool for streaming
    if (m_UseBufferQueuing) {
        for (unsigned int i = 0; i < kStreamBufferPoolSize; i++) {
            if (m_StreamBufferPool[i] != 0) {
                alDeleteBuffers(1, &m_StreamBufferPool[i]);
            }
        }
    }
#endif
    if (m_Buffer)
    {
        alDeleteBuffers(1, &m_Buffer);
    }
}

//========================================================================
// radSoundHalBufferWin::radSoundHalBufferWin
//========================================================================

radSoundHalBufferWin::radSoundHalBufferWin(	void )
    :
    m_SizeInFrames( 0 ),
    m_LoadStartInBytes( 0 ),
    m_pLockedLoadBuffer( NULL ),
    m_LockedLoadBytes( 0 ),
    m_Looping( false ),
    m_Streaming( false ),
#ifdef RAD_TVOS
    m_ForcedStereo( false ),
    m_AttachedSource( 0 ),
    m_UseBufferQueuing( false ),
    m_LastDataSourcePtr( NULL ),
    m_QueuedCount( 0 ),
#endif
    m_Buffer( 0 ),
    m_refIRadSoundHalAudioFormat( NULL ),
    m_refIRadMemoryObject( NULL ),
    m_refIRadSoundHalBufferLoadCallback( NULL )
{
#ifdef RAD_TVOS
    // Initialize buffer pool
    for (unsigned int i = 0; i < kStreamBufferPoolSize; i++) {
        m_StreamBufferPool[i] = 0;
        m_StreamBufferFree[i] = true;
    }
#endif
}

//========================================================================
// radSoundHalBufferWin::Initialize
//========================================================================

void radSoundHalBufferWin::Initialize
(
	IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat,
    IRadMemoryObject * pIRadMemoryObject,
    unsigned int sizeInFrames,
	bool looping,
    bool streaming
)
{
    rAssert( pIRadSoundHalAudioFormat != NULL );
    rAssert( pIRadSoundHalAudioFormat->GetEncoding() == IRadSoundHalAudioFormat::PCM );
	rAssert( pIRadMemoryObject->GetMemorySize( ) >= ::radSoundHalBufferCalculateMemorySize( 
        IRadSoundHalAudioFormat::Bytes, sizeInFrames, 
        IRadSoundHalAudioFormat::Frames, pIRadSoundHalAudioFormat ) );

    m_refIRadSoundHalAudioFormat = pIRadSoundHalAudioFormat;
	m_refIRadMemoryObject = pIRadMemoryObject;
	m_Looping = looping;
    m_Streaming = streaming;
    m_SizeInFrames = sizeInFrames;

    alGenBuffers( 1, &m_Buffer );
    rAssert( alGetError() == AL_NO_ERROR );
    if( streaming )
    {
        ALenum format = AL_FORMAT_MONO8;
        if( m_refIRadSoundHalAudioFormat->GetNumberOfChannels() > 1 )
            format = m_refIRadSoundHalAudioFormat->GetBitResolution() == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
        else
            format = m_refIRadSoundHalAudioFormat->GetBitResolution() == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;

#ifdef RAD_TVOS
        // WORKAROUND: Apple's OpenAL map_buffer extension doesn't persist writes for mono buffers.
        // For mono streaming: Use buffer QUEUING (alSourceQueueBuffers) instead of single buffer replacement.
        // This fixes cutoff/repeat issues by allowing continuous playback while filling new buffers.
        
        if (m_refIRadSoundHalAudioFormat->GetNumberOfChannels() == 1) {
            m_ForcedStereo = true;
            m_UseBufferQueuing = true;
            
            // Create buffer pool for queuing
            alGenBuffers(kStreamBufferPoolSize, m_StreamBufferPool);
            ALenum genErr = alGetError();
            if (genErr != AL_NO_ERROR) {
                SDL_Log("[QUEUE_INIT] ERROR: alGenBuffers failed err=0x%x", genErr);
                m_UseBufferQueuing = false;
            } else {
                for (unsigned int i = 0; i < kStreamBufferPoolSize; i++) {
                    m_StreamBufferFree[i] = true;
                }
                SDL_Log("[QUEUE_INIT] Created buffer pool: [%u,%u,%u,%u] for mono streaming",
                        m_StreamBufferPool[0], m_StreamBufferPool[1], 
                        m_StreamBufferPool[2], m_StreamBufferPool[3]);
            }
        }
        else if ( radMapBufferSOFT == NULL || radUnmapBufferSOFT == NULL ) {
            m_ForcedStereo = false;
            m_UseBufferQueuing = true;

            alGenBuffers(kStreamBufferPoolSize, m_StreamBufferPool);
            ALenum genErr = alGetError();
            if (genErr != AL_NO_ERROR) {
                SDL_Log("[QUEUE_INIT] ERROR: alGenBuffers failed err=0x%x", genErr);
                m_UseBufferQueuing = false;
            } else {
                for (unsigned int i = 0; i < kStreamBufferPoolSize; i++) {
                    m_StreamBufferFree[i] = true;
                }
                SDL_Log("[QUEUE_INIT] Created buffer pool: [%u,%u,%u,%u] for stereo streaming",
                        m_StreamBufferPool[0], m_StreamBufferPool[1],
                        m_StreamBufferPool[2], m_StreamBufferPool[3]);
            }
        }
        
        // For forced stereo with queuing, DON'T use radBufferStorageSOFT - we use alBufferData + queue
        ALenum useFormat = format;
        unsigned int useSize = pIRadMemoryObject->GetMemorySize();
        bool useMapBuffer = !m_ForcedStereo && !m_UseBufferQueuing;  // Only use map_buffer for stereo streams
        
        static unsigned int s_streamBufInitCount = 0;
        s_streamBufInitCount++;
        if (s_streamBufInitCount <= 5) {
            SDL_Log("[STREAM_BUF] #%u: ch=%u bits=%u size=%u rate=%u forcedStereo=%d useQueue=%d",
                    s_streamBufInitCount, 
                    m_refIRadSoundHalAudioFormat->GetNumberOfChannels(),
                    m_refIRadSoundHalAudioFormat->GetBitResolution(),
                    useSize,
                    m_refIRadSoundHalAudioFormat->GetSampleRate(),
                    m_ForcedStereo ? 1 : 0,
                    m_UseBufferQueuing ? 1 : 0);
        }
#endif

        // AL_SOFTX_map_buffer extension may not be available on tvOS
        if( radBufferStorageSOFT != NULL )
        {
#ifdef RAD_TVOS
            // For forced stereo (mono streams), DON'T use BufferStorage - use alBufferData path
            if (useMapBuffer) {
                radBufferStorageSOFT( m_Buffer, useFormat,
                    pIRadMemoryObject->GetMemoryAddress(),
                    useSize,
                    m_refIRadSoundHalAudioFormat->GetSampleRate(),
                    AL_MAP_WRITE_BIT_SOFT | AL_MAP_PERSISTENT_BIT_SOFT
                );
                ALenum storageErr = alGetError();
                if (storageErr != AL_NO_ERROR) {
                    SDL_Log("[BUF_STORAGE] ERROR: buffer=%u err=0x%x format=0x%x size=%u", 
                            m_Buffer, storageErr, useFormat, useSize);
                }
            }
            // else: m_ForcedStereo buffers will use alBufferData in OnBufferLoadComplete
#else
            radBufferStorageSOFT( m_Buffer, format,
                pIRadMemoryObject->GetMemoryAddress(),
                pIRadMemoryObject->GetMemorySize(),
                m_refIRadSoundHalAudioFormat->GetSampleRate(),
                AL_MAP_WRITE_BIT_SOFT | AL_MAP_PERSISTENT_BIT_SOFT
            );
            rAssert( alGetError() == AL_NO_ERROR );
#endif
        }
        else
        {
            // Fallback: use standard alBufferData for streaming on platforms without map_buffer
            if ( !m_UseBufferQueuing )
            {
                TVOS_AL_BUFFER_DATA( m_Buffer, format,
                    pIRadMemoryObject->GetMemoryAddress(),
                    pIRadMemoryObject->GetMemorySize(),
                    m_refIRadSoundHalAudioFormat->GetSampleRate(), "INIT_FALLBACK");
            }
        }
    }
}

//========================================================================
// radSoundHalBufferWin::GetMemoryObject
//========================================================================

IRadMemoryObject * radSoundHalBufferWin::GetMemoryObject( void )
{
	return m_refIRadMemoryObject;
}

//========================================================================
// radSoundHalBufferWin::Clear
//========================================================================

void radSoundHalBufferWin::ClearAsync
( 
	unsigned int startPositionInFrames,
	unsigned int numberOfFrames,
	IRadSoundHalBufferClearCallback * pIRadSoundHalBufferClearCallback
)
{
    if ( m_Buffer != 0 )
    {
        rAssert(startPositionInFrames < m_SizeInFrames );
        rAssert( ( startPositionInFrames + numberOfFrames ) <= m_SizeInFrames );

        unsigned int offsetInBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( startPositionInFrames );
        unsigned int sizeInBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( numberOfFrames );  

        unsigned char fillChar = ( m_refIRadSoundHalAudioFormat->GetBitResolution( ) == 8 ) ? 128 : 0;

#ifdef RAD_TVOS
        // CRITICAL: Forced stereo buffers must NEVER use map_buffer - they use alBufferData only
        bool canUseMapBuffer = (m_Streaming == true && radMapBufferSOFT != NULL && !m_ForcedStereo);
#else
        bool canUseMapBuffer = (m_Streaming == true && radMapBufferSOFT != NULL);
#endif
        if( canUseMapBuffer )
        {
            void* dataPtr = radMapBufferSOFT( m_Buffer, offsetInBytes, sizeInBytes, AL_MAP_WRITE_BIT_SOFT | AL_MAP_PERSISTENT_BIT_SOFT );

            rAssertMsg( alGetError() == AL_NO_ERROR, "radSoundHalBufferWin::Clear - Lock Failed.\n" );

            if( dataPtr )
            {
                ::memset( dataPtr, fillChar, sizeInBytes );

                radUnmapBufferSOFT( m_Buffer );

                rAssertMsg( alGetError() == AL_NO_ERROR, "radSoundHalBufferWin::Clear - UnLock Failed.\n" );
            }
        }
        else
        {
            // Clear the memory object
            ::memset( static_cast<char*>(m_refIRadMemoryObject->GetMemoryAddress()) + offsetInBytes, fillChar, sizeInBytes );

#ifdef RAD_TVOS
            // For forced stereo: DON'T call alBufferData here - it would fail on attached buffer
            // The real upload happens in OnBufferLoadComplete with proper detach/reattach
            if (!m_ForcedStereo)
#endif
            {
                ALenum format = AL_FORMAT_MONO8;
                if( m_refIRadSoundHalAudioFormat->GetNumberOfChannels() > 1 )
                    format = m_refIRadSoundHalAudioFormat->GetBitResolution() == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
                else
                    format = m_refIRadSoundHalAudioFormat->GetBitResolution() == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;

#ifdef RAD_TVOS
                TVOS_AL_BUFFER_DATA( m_Buffer, format,
                    m_refIRadMemoryObject->GetMemoryAddress(),
                    m_refIRadMemoryObject->GetMemorySize(),
                    m_refIRadSoundHalAudioFormat->GetSampleRate(), "CLEAR_ASYNC");
#else
                alBufferData( m_Buffer, format,
                    m_refIRadMemoryObject->GetMemoryAddress(),
                    m_refIRadMemoryObject->GetMemorySize(),
                    m_refIRadSoundHalAudioFormat->GetSampleRate()
                );
#endif

                rAssertMsg( alGetError() == AL_NO_ERROR, "radSoundHalBufferWin::Clear Failed.\n" );
            }
        }
    }

	if ( pIRadSoundHalBufferClearCallback != NULL )
	{
		pIRadSoundHalBufferClearCallback->OnBufferClearComplete( );
	}
}

//========================================================================
// radSoundHalBufferWin::GetSizeInBytes
//========================================================================

unsigned int radSoundHalBufferWin::GetSizeInBytes( void )
{
    return m_refIRadSoundHalAudioFormat->FramesToBytes( m_SizeInFrames );
}

//========================================================================
// radSoundHalBufferWin::IsStreaming
//========================================================================

bool radSoundHalBufferWin::IsStreaming( void )
{
    return m_Streaming;
}

//========================================================================
// radSoundHalBufferWin::GetBuffer
//========================================================================

ALuint radSoundHalBufferWin::GetBuffer( void )
{
    rAssert(m_Buffer != 0);
    return m_Buffer;
}

//========================================================================
// radSoundHalBufferWin::GetSizeInFrames
//========================================================================

unsigned int radSoundHalBufferWin::GetSizeInFrames( void )
{
    return m_SizeInFrames;
}

//========================================================================
// radSoundHalBufferWin::GetSoundSampleFormat
//========================================================================
    
IRadSoundHalAudioFormat * radSoundHalBufferWin::GetFormat( void )
{
    return m_refIRadSoundHalAudioFormat;
}

//========================================================================
// radSoundHalBufferWin::LoadAsync
//========================================================================

void radSoundHalBufferWin::LoadAsync
(
	IRadSoundHalDataSource * pIRadSoundHalDataSource,
	unsigned int bufferStartInFrames,
	unsigned int numberOfFrames,
	IRadSoundHalBufferLoadCallback * pIRadSoundHalBufferLoadCallback 
)
{
    // At this time there should really only be one direct sound buffer created and 
    // it should be busy.  Let's verify that.

    rAssert( m_refIRadSoundHalBufferLoadCallback == NULL );

    m_LoadStartInBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( bufferStartInFrames );
    m_refIRadSoundHalBufferLoadCallback = pIRadSoundHalBufferLoadCallback;

#ifdef RAD_TVOS
    if ( m_Streaming && m_UseBufferQueuing && m_AttachedSource != 0 )
    {
        if ( m_LastDataSourcePtr != pIRadSoundHalDataSource )
        {
            FlushBufferQueue( );
            m_LastDataSourcePtr = pIRadSoundHalDataSource;
        }
    }
#endif

#ifdef RAD_TVOS
    // ALWAYS LOG ENTRY for mono streaming - this fires UNCONDITIONALLY
    unsigned int channels = m_refIRadSoundHalAudioFormat->GetNumberOfChannels();
    if (channels == 1 && m_Streaming) {
        SDL_Log("[MONO_LOAD] ENTRY buffer=%u streaming=%d mapExt=%p frames=%u startFrame=%u",
                m_Buffer, m_Streaming ? 1 : 0, (void*)radMapBufferSOFT, 
                numberOfFrames, bufferStartInFrames);
    }
#endif

    if( m_Streaming == true && radMapBufferSOFT != NULL && !m_UseBufferQueuing )
    {
        rAssert( m_pLockedLoadBuffer == NULL );

        m_LockedLoadBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( numberOfFrames );
        
#ifdef RAD_TVOS
        // WORKAROUND: Apple's map_buffer doesn't persist writes for mono buffers.
        // For forced stereo: Write mono data to memory object, then use alBufferData with converted stereo.
        if (m_ForcedStereo) {
            // Use memory object as intermediate buffer - mono data goes here first
            m_pLockedLoadBuffer = static_cast<char*>(m_refIRadMemoryObject->GetMemoryAddress()) + m_LoadStartInBytes;
            SDL_Log("[MONO_FIX] Using memobj for mono data ptr=%p bytes=%u (will convert+alBufferData)",
                    m_pLockedLoadBuffer, m_LockedLoadBytes);
        }
        else {
            // Stereo streams can use map_buffer normally
            m_pLockedLoadBuffer = radMapBufferSOFT( m_Buffer, m_LoadStartInBytes, m_LockedLoadBytes,
                AL_MAP_WRITE_BIT_SOFT | AL_MAP_PERSISTENT_BIT_SOFT );
            ALenum error = alGetError();
            rAssert( error == AL_NO_ERROR );
        }
#else
        m_pLockedLoadBuffer = radMapBufferSOFT( m_Buffer, m_LoadStartInBytes, m_LockedLoadBytes,
            AL_MAP_WRITE_BIT_SOFT | AL_MAP_PERSISTENT_BIT_SOFT );
        ALenum error = alGetError();
        rAssert( error == AL_NO_ERROR );
#endif
    }
    else
    {
        // Fallback path for non-streaming or when map_buffer extension unavailable
        m_pLockedLoadBuffer = static_cast<char*>(m_refIRadMemoryObject->GetMemoryAddress()) + m_LoadStartInBytes;
        m_LockedLoadBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( numberOfFrames );
    }

    new( "radSoundBufferLoaderWin", RADMEMORY_ALLOC_TEMP ) radSoundBufferLoaderWin(
        static_cast< IRadSoundHalBuffer * >( this ),
        m_pLockedLoadBuffer,
        pIRadSoundHalDataSource,
        m_refIRadSoundHalAudioFormat,
        m_refIRadSoundHalAudioFormat->BytesToFrames( m_LockedLoadBytes ),
        this );
}

//========================================================================
// radSoundHalBufferWin::IsLooping
//========================================================================

bool radSoundHalBufferWin::IsLooping
(
    void
)
{
    return m_Looping;
}   

//========================================================================
// radSoundHalBufferWin::OnBufferLoadComplete
//========================================================================

void radSoundHalBufferWin::OnBufferLoadComplete( unsigned int dataSourceFrames )
{
    rAssert( m_refIRadSoundHalBufferLoadCallback != NULL );

    ALenum format = AL_FORMAT_MONO8;
    if (m_refIRadSoundHalAudioFormat->GetNumberOfChannels() > 1)
        format = m_refIRadSoundHalAudioFormat->GetBitResolution() == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
    else
        format = m_refIRadSoundHalAudioFormat->GetBitResolution() == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;

#ifdef RAD_TVOS
    // Minimal buffer load logging - removed verbose diagnostics
#endif

#ifdef RAD_TVOS
    if ( m_Streaming && m_UseBufferQueuing && m_AttachedSource != 0 && m_pLockedLoadBuffer != NULL && dataSourceFrames > 0 )
    {
        ALuint source = m_AttachedSource;

        UnqueueProcessedBuffers();

        ALuint queueBuf = GetFreeStreamBuffer();
        if (queueBuf == 0) {
            SDL_Log("[QUEUE_UNDERRUN] No free buffers! Forcing unqueue...");
            ALuint forcedBuf = 0;
            alSourceUnqueueBuffers(source, 1, &forcedBuf);
            if (alGetError() == AL_NO_ERROR && forcedBuf != 0) {
                ReturnStreamBuffer(forcedBuf);
                queueBuf = GetFreeStreamBuffer();
            }
        }

        if (queueBuf != 0) {
            if (m_ForcedStereo) {
                const unsigned int bits = m_refIRadSoundHalAudioFormat->GetBitResolution();
                if (bits == 8) {
                    unsigned int stereoBytes = dataSourceFrames * 2;
                    uint8_t* monoData = (uint8_t*)m_pLockedLoadBuffer;
                    uint8_t* stereoData = (uint8_t*)radMemoryAlloc(GetThisAllocator(), stereoBytes);
                    if (stereoData != NULL) {
                        for (unsigned int i = 0; i < dataSourceFrames; i++) {
                            uint8_t sample = monoData[i];
                            stereoData[i * 2 + 0] = sample;
                            stereoData[i * 2 + 1] = sample;
                        }
                        alBufferData(queueBuf, AL_FORMAT_STEREO8, stereoData, stereoBytes,
                                     m_refIRadSoundHalAudioFormat->GetSampleRate());
                        ALenum bufErr = alGetError();
                        radMemoryFree(stereoData);
                        if (bufErr != AL_NO_ERROR) {
                            SDL_Log("[QUEUE_ERR] alBufferData failed: err=0x%x buf=%u bytes=%u", bufErr, queueBuf, stereoBytes);
                            ReturnStreamBuffer(queueBuf);
                            queueBuf = 0;
                        }
                    } else {
                        SDL_Log("[QUEUE_ERR] ALLOC FAILED! stereoBytes=%u", stereoBytes);
                        ReturnStreamBuffer(queueBuf);
                        queueBuf = 0;
                    }
                } else {
                    unsigned int stereoBytes = dataSourceFrames * 4;
                    int16_t* monoData = (int16_t*)m_pLockedLoadBuffer;
                    int16_t* stereoData = (int16_t*)radMemoryAlloc(GetThisAllocator(), stereoBytes);
                    if (stereoData != NULL) {
                        for (unsigned int i = 0; i < dataSourceFrames; i++) {
                            int16_t sample = monoData[i];
                            stereoData[i * 2 + 0] = sample;
                            stereoData[i * 2 + 1] = sample;
                        }
                        alBufferData(queueBuf, AL_FORMAT_STEREO16, stereoData, stereoBytes,
                                     m_refIRadSoundHalAudioFormat->GetSampleRate());
                        ALenum bufErr = alGetError();
                        radMemoryFree(stereoData);
                        if (bufErr != AL_NO_ERROR) {
                            SDL_Log("[QUEUE_ERR] alBufferData failed: err=0x%x buf=%u bytes=%u", bufErr, queueBuf, stereoBytes);
                            ReturnStreamBuffer(queueBuf);
                            queueBuf = 0;
                        }
                    } else {
                        SDL_Log("[QUEUE_ERR] ALLOC FAILED! stereoBytes=%u", stereoBytes);
                        ReturnStreamBuffer(queueBuf);
                        queueBuf = 0;
                    }
                }
            } else {
                alBufferData(queueBuf, format, m_pLockedLoadBuffer, m_LockedLoadBytes,
                             m_refIRadSoundHalAudioFormat->GetSampleRate());
                ALenum bufErr = alGetError();
                if (bufErr != AL_NO_ERROR) {
                    SDL_Log("[QUEUE_ERR] alBufferData failed: err=0x%x buf=%u bytes=%lu", bufErr, queueBuf, (unsigned long)m_LockedLoadBytes);
                    ReturnStreamBuffer(queueBuf);
                    queueBuf = 0;
                }
            }
        }

        if (queueBuf != 0) {
            alSourceQueueBuffers(source, 1, &queueBuf);
            ALenum queueErr = alGetError();
            if (queueErr == AL_NO_ERROR) {
                m_QueuedCount++;
                ALint sourceState = 0;
                alGetSourcei(source, AL_SOURCE_STATE, &sourceState);
                if (sourceState != AL_PLAYING && m_QueuedCount > 0) {
                    alSourcePlay(source);
                    alGetError();
                }
            } else {
                SDL_Log("[QUEUE_ERR] alSourceQueueBuffers failed: err=0x%x buf=%u source=%u", queueErr, queueBuf, source);
                ReturnStreamBuffer(queueBuf);
            }
        } else {
            SDL_Log("[QUEUE_ERR] No buffer available even after force unqueue!");
        }

        m_pLockedLoadBuffer = NULL;
        m_LockedLoadBytes = 0;

        if( m_refIRadSoundHalBufferLoadCallback != NULL )
        {
            m_refIRadSoundHalBufferLoadCallback->OnBufferLoadComplete( dataSourceFrames );
            m_refIRadSoundHalBufferLoadCallback = NULL;
        }

        return;
    }
#endif

    if( m_Streaming == false || radUnmapBufferSOFT == NULL )
    {
#ifdef RAD_TVOS
        // CRITICAL FIX: On tvOS, NEVER upload AL_FORMAT_MONO16 - Apple's OpenAL corrupts it
        // Force mono clips to stereo by duplicating samples
        unsigned int channels = m_refIRadSoundHalAudioFormat->GetNumberOfChannels();
        unsigned int bits = m_refIRadSoundHalAudioFormat->GetBitResolution();
        
        if (channels == 1 && bits == 16 && m_LockedLoadBytes > 0) {
            // ENTRY LOG - proves this TU is compiled and path is hit
            SDL_Log("[CLIP_FIX] ENTER mono->stereo clip path buffer=%u bytes=%u", m_Buffer, m_LockedLoadBytes);
            
            // Mono 16-bit clip - must convert to stereo
            int16_t* monoData = (int16_t*)m_pLockedLoadBuffer;
            unsigned int monoFrames = m_LockedLoadBytes / 2;  // 2 bytes per mono sample
            unsigned int stereoBytes = monoFrames * 4;  // 2 channels * 2 bytes
            
            int16_t* stereoData = (int16_t*)radMemoryAlloc(GetThisAllocator(), stereoBytes);
            if (stereoData != NULL) {
                // Convert mono to stereo
                for (unsigned int i = 0; i < monoFrames; i++) {
                    int16_t sample = monoData[i];
                    stereoData[i * 2 + 0] = sample;  // Left
                    stereoData[i * 2 + 1] = sample;  // Right
                }
                
                static unsigned int s_clipFixCount = 0;
                s_clipFixCount++;
                if (s_clipFixCount <= 5) {
                    SDL_Log("[CLIP_FIX] #%u mono clip -> stereo: frames=%u monoBytes=%u stereoBytes=%u",
                            s_clipFixCount, monoFrames, m_LockedLoadBytes, stereoBytes);
                }
                
                // Upload as stereo
                TVOS_AL_BUFFER_DATA(m_Buffer, AL_FORMAT_STEREO16, stereoData, stereoBytes,
                             m_refIRadSoundHalAudioFormat->GetSampleRate(), "CLIP_STEREO16");
                ALenum err = alGetError();
                if (err != AL_NO_ERROR) {
                    SDL_Log("[CLIP_FIX] ERROR: alBufferData failed! buffer=%u err=0x%x", m_Buffer, err);
                }
                
                radMemoryFree(stereoData);
            } else {
                SDL_Log("[CLIP_FIX] ALLOC FAILED! stereoBytes=%u", stereoBytes);
                // Fallback to mono (will have static but won't crash)
                TVOS_AL_BUFFER_DATA(m_Buffer, format, m_pLockedLoadBuffer, m_LockedLoadBytes,
                             m_refIRadSoundHalAudioFormat->GetSampleRate(), "CLIP_FALLBACK");
            }
        }
        else if (channels == 1 && bits == 8 && m_LockedLoadBytes > 0) {
            // Mono 8-bit clip - convert to stereo 8-bit
            uint8_t* monoData = (uint8_t*)m_pLockedLoadBuffer;
            unsigned int monoFrames = m_LockedLoadBytes;
            unsigned int stereoBytes = monoFrames * 2;
            
            uint8_t* stereoData = (uint8_t*)radMemoryAlloc(GetThisAllocator(), stereoBytes);
            if (stereoData != NULL) {
                for (unsigned int i = 0; i < monoFrames; i++) {
                    stereoData[i * 2 + 0] = monoData[i];
                    stereoData[i * 2 + 1] = monoData[i];
                }
                TVOS_AL_BUFFER_DATA(m_Buffer, AL_FORMAT_STEREO8, stereoData, stereoBytes,
                             m_refIRadSoundHalAudioFormat->GetSampleRate(), "CLIP_STEREO8");
                radMemoryFree(stereoData);
            } else {
                TVOS_AL_BUFFER_DATA(m_Buffer, format, m_pLockedLoadBuffer, m_LockedLoadBytes,
                             m_refIRadSoundHalAudioFormat->GetSampleRate(), "CLIP_8BIT_FALLBACK");
            }
        }
        else {
            // Stereo clip - upload directly
            TVOS_AL_BUFFER_DATA(m_Buffer, format, m_pLockedLoadBuffer, m_LockedLoadBytes,
                         m_refIRadSoundHalAudioFormat->GetSampleRate(), "CLIP_NATIVE");
        }
        
        ALenum err = alGetError();
        if (err != AL_NO_ERROR) {
            SDL_Log("[AUDIO_BUF] ERROR: alBufferData failed! buffer=%u err=0x%x ch=%u",
                    m_Buffer, err, channels);
        }
#else
        // Non-streaming or fallback path: upload buffer data
        alBufferData( m_Buffer, format,
            m_pLockedLoadBuffer,
            m_LockedLoadBytes,
            m_refIRadSoundHalAudioFormat->GetSampleRate()
        );
        rAssert( alGetError() == AL_NO_ERROR );
#endif
    }
    else
    {
        // For streaming sounds we'll have to unlock the direct sound buffer before calling back
#ifdef RAD_TVOS
        // For mono streaming: Use buffer QUEUING instead of detach/upload/reattach
        // This allows continuous playback - queued buffers play in sequence without interruption
        if (m_ForcedStereo && m_UseBufferQueuing && m_pLockedLoadBuffer != NULL && dataSourceFrames > 0) {
            ALuint source = m_AttachedSource;
            
            // STEP 1: Unqueue any processed buffers back to the pool
            UnqueueProcessedBuffers();
            
            // STEP 2: Get a free buffer from the pool
            ALuint queueBuf = GetFreeStreamBuffer();
            if (queueBuf == 0) {
                // No free buffers - this means we're not keeping up with playback
                // Force unqueue of one buffer (may cause audio glitch)
                SDL_Log("[QUEUE_UNDERRUN] No free buffers! Forcing unqueue...");
                ALuint forcedBuf = 0;
                alSourceUnqueueBuffers(source, 1, &forcedBuf);
                if (alGetError() == AL_NO_ERROR && forcedBuf != 0) {
                    ReturnStreamBuffer(forcedBuf);
                    queueBuf = GetFreeStreamBuffer();
                }
            }
            
            if (queueBuf != 0) {
                const unsigned int bits = m_refIRadSoundHalAudioFormat->GetBitResolution();
                if (bits == 8) {
                    unsigned int stereoBytes = dataSourceFrames * 2;
                    uint8_t* monoData = (uint8_t*)m_pLockedLoadBuffer;
                    uint8_t* stereoData = (uint8_t*)radMemoryAlloc(GetThisAllocator(), stereoBytes);
                    if (stereoData != NULL) {
                        for (unsigned int i = 0; i < dataSourceFrames; i++) {
                            uint8_t sample = monoData[i];
                            stereoData[i * 2 + 0] = sample;
                            stereoData[i * 2 + 1] = sample;
                        }

                        alBufferData(queueBuf, AL_FORMAT_STEREO8, stereoData, stereoBytes,
                                     m_refIRadSoundHalAudioFormat->GetSampleRate());
                        ALenum bufErr = alGetError();

                        if (bufErr == AL_NO_ERROR) {
                            alSourceQueueBuffers(source, 1, &queueBuf);
                            ALenum queueErr = alGetError();

                            if (queueErr == AL_NO_ERROR) {
                                m_QueuedCount++;

                                ALint sourceState = 0;
                                alGetSourcei(source, AL_SOURCE_STATE, &sourceState);

                                static unsigned int s_queueLogCount = 0;
                                s_queueLogCount++;
                                if (s_queueLogCount <= 20 || (s_queueLogCount % 50) == 0) {
                                    ALint queued = 0;
                                    alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
                                    SDL_Log("[QUEUE_OK] #%u buf=%u queued=%d frames=%u state=%s source=%u queuedCount=%u",
                                            s_queueLogCount, queueBuf, queued, dataSourceFrames,
                                            (sourceState == AL_PLAYING) ? "PLAYING" :
                                            (sourceState == AL_STOPPED) ? "STOPPED" : "OTHER", source, m_QueuedCount);
                                }

                                if (sourceState != AL_PLAYING && m_QueuedCount > 0) {
                                    alSourcePlay(source);
                                    ALenum playErr = alGetError();
                                    if (playErr != AL_NO_ERROR) {
                                        SDL_Log("[QUEUE_PLAY] ERROR: alSourcePlay failed err=0x%x", playErr);
                                    } else {
                                        SDL_Log("[QUEUE_PLAY] Started/resumed playback on source=%u (underrun recovery)", source);
                                    }
                                }
                            } else {
                                SDL_Log("[QUEUE_ERR] alSourceQueueBuffers failed: err=0x%x buf=%u source=%u",
                                        queueErr, queueBuf, source);
                                ReturnStreamBuffer(queueBuf);
                            }
                        } else {
                            SDL_Log("[QUEUE_ERR] alBufferData failed: err=0x%x buf=%u bytes=%u",
                                    bufErr, queueBuf, stereoBytes);
                            ReturnStreamBuffer(queueBuf);
                        }

                        radMemoryFree(stereoData);
                    } else {
                        SDL_Log("[QUEUE_ERR] ALLOC FAILED! stereoBytes=%u", stereoBytes);
                        ReturnStreamBuffer(queueBuf);
                    }
                } else {
                    unsigned int stereoBytes = dataSourceFrames * 4;
                    int16_t* monoData = (int16_t*)m_pLockedLoadBuffer;
                    int16_t* stereoData = (int16_t*)radMemoryAlloc(GetThisAllocator(), stereoBytes);

                    if (stereoData != NULL) {
                        for (unsigned int i = 0; i < dataSourceFrames; i++) {
                            int16_t sample = monoData[i];
                            stereoData[i * 2 + 0] = sample;
                            stereoData[i * 2 + 1] = sample;
                        }

                        alBufferData(queueBuf, AL_FORMAT_STEREO16, stereoData, stereoBytes,
                                     m_refIRadSoundHalAudioFormat->GetSampleRate());
                        ALenum bufErr = alGetError();

                        if (bufErr == AL_NO_ERROR) {
                            alSourceQueueBuffers(source, 1, &queueBuf);
                            ALenum queueErr = alGetError();

                            if (queueErr == AL_NO_ERROR) {
                                m_QueuedCount++;

                                ALint sourceState = 0;
                                alGetSourcei(source, AL_SOURCE_STATE, &sourceState);

                                static unsigned int s_queueLogCount = 0;
                                s_queueLogCount++;
                                if (s_queueLogCount <= 20 || (s_queueLogCount % 50) == 0) {
                                    ALint queued = 0;
                                    alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
                                    SDL_Log("[QUEUE_OK] #%u buf=%u queued=%d frames=%u state=%s source=%u queuedCount=%u",
                                            s_queueLogCount, queueBuf, queued, dataSourceFrames,
                                            (sourceState == AL_PLAYING) ? "PLAYING" :
                                            (sourceState == AL_STOPPED) ? "STOPPED" : "OTHER", source, m_QueuedCount);
                                }

                                if (sourceState != AL_PLAYING && m_QueuedCount > 0) {
                                    alSourcePlay(source);
                                    ALenum playErr = alGetError();
                                    if (playErr != AL_NO_ERROR) {
                                        SDL_Log("[QUEUE_PLAY] ERROR: alSourcePlay failed err=0x%x", playErr);
                                    } else {
                                        SDL_Log("[QUEUE_PLAY] Started/resumed playback on source=%u (underrun recovery)", source);
                                    }
                                }
                            } else {
                                SDL_Log("[QUEUE_ERR] alSourceQueueBuffers failed: err=0x%x buf=%u source=%u",
                                        queueErr, queueBuf, source);
                                ReturnStreamBuffer(queueBuf);
                            }
                        } else {
                            SDL_Log("[QUEUE_ERR] alBufferData failed: err=0x%x buf=%u bytes=%u",
                                    bufErr, queueBuf, stereoBytes);
                            ReturnStreamBuffer(queueBuf);
                        }

                        radMemoryFree(stereoData);
                    } else {
                        SDL_Log("[QUEUE_ERR] ALLOC FAILED! stereoBytes=%u", stereoBytes);
                        ReturnStreamBuffer(queueBuf);
                    }
                }
            } else {
                SDL_Log("[QUEUE_ERR] No buffer available even after force unqueue!");
            }
            
            m_pLockedLoadBuffer = NULL;
            m_LockedLoadBytes = 0;
        }
        // Fallback for forced stereo WITHOUT queuing (shouldn't happen but just in case)
        else if (m_ForcedStereo && !m_UseBufferQueuing && m_pLockedLoadBuffer != NULL && dataSourceFrames > 0) {
            SDL_Log("[MONO_FALLBACK] Using old detach/reattach path - this shouldn't happen!");
            m_pLockedLoadBuffer = NULL;
            m_LockedLoadBytes = 0;
        }
        else
#endif
        {
            radUnmapBufferSOFT( m_Buffer );
            ALenum unmapErr = alGetError();
            rAssert( unmapErr == AL_NO_ERROR );

            m_pLockedLoadBuffer = NULL;
            m_LockedLoadBytes = 0;
        }
    }

    m_pLockedLoadBuffer = NULL;
    m_LockedLoadBytes = 0;

    if( m_refIRadSoundHalBufferLoadCallback != NULL )
    {
        m_refIRadSoundHalBufferLoadCallback->OnBufferLoadComplete( dataSourceFrames );
        m_refIRadSoundHalBufferLoadCallback = NULL;
    }
}

//========================================================================
// radSoundHalBufferWin::CancelAsyncOperations
//========================================================================

void radSoundHalBufferWin::CancelAsyncOperations( void )
{
    radSoundBufferLoaderWin::CancelOperations( static_cast< IRadSoundHalBuffer * >( this ) );    

    if( m_refIRadSoundHalBufferLoadCallback != NULL )
    {
#ifdef RAD_TVOS
        // For forced stereo, we didn't map the buffer (used memory object instead)
        if( m_Streaming == true && radUnmapBufferSOFT != NULL && !m_ForcedStereo )
#else
        if( m_Streaming == true && radUnmapBufferSOFT != NULL )
#endif
        {
            radUnmapBufferSOFT( m_Buffer );
            rAssert( alGetError() == AL_NO_ERROR );
        }

        m_pLockedLoadBuffer = NULL;
        m_LockedLoadBytes = 0;
        m_refIRadSoundHalBufferLoadCallback = NULL;
    }
}

//========================================================================
// radSoundHalBufferWin::GetMinTransferSizeInFrames
//========================================================================

unsigned int radSoundHalBufferWin::GetMinTransferSize( IRadSoundHalAudioFormat::SizeType sizeType )
{
    rAssert( m_refIRadSoundHalAudioFormat != NULL );

    //
    // Channels of data are eventually dma'd seperately to spu.
    // Therefore, transfers must occur in multiples of the optimal
    // dma multiple * the number of channels.
    //

    return m_refIRadSoundHalAudioFormat->ConvertSizeType( sizeType, 
        radMemorySpace_OptimalMultiple * m_refIRadSoundHalAudioFormat->GetNumberOfChannels( ),
        IRadSoundHalAudioFormat::Bytes );
}

#ifdef RAD_TVOS
//========================================================================
// radSoundHalBufferWin::GetFreeStreamBuffer
//========================================================================

ALuint radSoundHalBufferWin::GetFreeStreamBuffer()
{
    for (unsigned int i = 0; i < kStreamBufferPoolSize; i++) {
        if (m_StreamBufferFree[i] && m_StreamBufferPool[i] != 0) {
            m_StreamBufferFree[i] = false;
            return m_StreamBufferPool[i];
        }
    }
    return 0;  // No free buffers available
}

//========================================================================
// radSoundHalBufferWin::ReturnStreamBuffer
//========================================================================

void radSoundHalBufferWin::ReturnStreamBuffer(ALuint buffer)
{
    for (unsigned int i = 0; i < kStreamBufferPoolSize; i++) {
        if (m_StreamBufferPool[i] == buffer) {
            m_StreamBufferFree[i] = true;
            return;
        }
    }
}

//========================================================================
// radSoundHalBufferWin::UnqueueProcessedBuffers
//========================================================================

void radSoundHalBufferWin::UnqueueProcessedBuffers()
{
    if (!m_UseBufferQueuing || m_AttachedSource == 0) {
        return;
    }
    
    ALint processed = 0;
    alGetSourcei(m_AttachedSource, AL_BUFFERS_PROCESSED, &processed);
    
    // Check for looping source - this is a BUG if it happens
    ALint looping = AL_FALSE;
    alGetSourcei(m_AttachedSource, AL_LOOPING, &looping);
    if (looping == AL_TRUE) {
        SDL_Log("[QUEUE_BUG] Source %u has AL_LOOPING=TRUE! Forcing OFF.", m_AttachedSource);
        alSourcei(m_AttachedSource, AL_LOOPING, AL_FALSE);
        alGetError();
    }
    
    while (processed > 0) {
        ALuint unqueuedBuf = 0;
        alSourceUnqueueBuffers(m_AttachedSource, 1, &unqueuedBuf);
        ALenum err = alGetError();
        if (err == AL_NO_ERROR && unqueuedBuf != 0) {
            ReturnStreamBuffer(unqueuedBuf);
            if (m_QueuedCount > 0) m_QueuedCount--;
            
            static unsigned int s_unqueueLogCount = 0;
            s_unqueueLogCount++;
            if (s_unqueueLogCount <= 20 || (s_unqueueLogCount % 100) == 0) {
                SDL_Log("[QUEUE_UNQUEUE] #%u buf=%u returned, source=%u, queuedCount=%u",
                        s_unqueueLogCount, unqueuedBuf, m_AttachedSource, m_QueuedCount);
            }
        } else if (err != AL_NO_ERROR) {
            SDL_Log("[QUEUE_UNQUEUE] ERROR: alSourceUnqueueBuffers err=0x%x source=%u", err, m_AttachedSource);
            break;
        }
        processed--;
    }
}

//========================================================================
// radSoundHalBufferWin::FlushBufferQueue
// CRITICAL: This is the "hard reset" for stream identity changes.
// Must fully detach source from any buffers and reset to fresh state.
//========================================================================

void radSoundHalBufferWin::FlushBufferQueue()
{
    if (!m_UseBufferQueuing || m_AttachedSource == 0) {
        return;
    }
    
    ALuint src = m_AttachedSource;
    
    // STEP 1: Stop the source
    alSourceStop(src);
    alGetError();  // Clear error
    
    // STEP 2: CRITICAL - Force looping OFF (prevents buffer reclaim issues)
    alSourcei(src, AL_LOOPING, AL_FALSE);
    alGetError();
    
    // STEP 3: CRITICAL - Detach ALL buffers with AL_BUFFER=0
    // This is the "magic bullet" that returns source to fresh/undetermined state
    alSourcei(src, AL_BUFFER, 0);
    ALenum detachErr = alGetError();
    
    // After AL_BUFFER=0, queued buffers are automatically detached
    // But let's verify and log
    ALint queued = 0;
    alGetSourcei(src, AL_BUFFERS_QUEUED, &queued);
    
    SDL_Log("[QUEUE_FLUSH] Hard reset source=%u: AL_BUFFER=0 (err=0x%x), queued after=%d",
            src, detachErr, queued);
    
    // If any buffers somehow still queued, unqueue them (shouldn't happen after AL_BUFFER=0)
    while (queued > 0) {
        ALuint unqueuedBuf = 0;
        alSourceUnqueueBuffers(src, 1, &unqueuedBuf);
        if (alGetError() == AL_NO_ERROR && unqueuedBuf != 0) {
            ReturnStreamBuffer(unqueuedBuf);
        }
        queued--;
    }
    
    // STEP 4: Reset internal pool state - ALL buffers free
    for (unsigned int i = 0; i < kStreamBufferPoolSize; i++) {
        m_StreamBufferFree[i] = true;
    }
    m_QueuedCount = 0;
    
    // STEP 5: Clear data source identity (will be set by new stream)
    m_LastDataSourcePtr = NULL;
    
    SDL_Log("[QUEUE_FLUSH] Reset complete: source=%u, pool=[6 free], queuedCount=0", src);
}
#endif

//========================================================================
// ::radSoundCreateBufferWin
//========================================================================

IRadSoundHalBuffer * radSoundHalBufferCreate( radMemoryAllocator allocator )
{
	return new ( "radSoundHalBufferWin", allocator ) radSoundHalBufferWin( );
}

