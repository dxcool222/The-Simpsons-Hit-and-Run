//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include "pch.hpp"
#include "system.hpp"
#include "listener.hpp"
#include "buffer.hpp"
#include "voice.hpp"
#include "../common/banner.hpp"
#include "../common/memoryregion.hpp"
#include <radplatform.hpp>
#include <AL/efx.h>
#include "radinprogext.h"

#ifdef RAD_TVOS
#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif
#endif

LPALBUFFERSTORAGESOFT radBufferStorageSOFT;
LPALMAPBUFFERSOFT radMapBufferSOFT;
LPALUNMAPBUFFERSOFT radUnmapBufferSOFT;

//================================================================================
// Static Members
//================================================================================

radSoundHalSystem * radSoundHalSystem::s_pRsdSystem = NULL;
static int g_RadSoundInitializeCount = 0;

//============================================================================
// radSoundHalSystem::radSoundHalSystem
//============================================================================

radSoundHalSystem::radSoundHalSystem( radMemoryAllocator allocator )
    :
    m_NumAuxSends( 0 ),
    m_pSoundMemory( 0 ),
    m_LastServiceTime( ::radTimeGetMilliseconds( ) )
{
    s_pRsdSystem = this;

    for( unsigned int i = 0; i < RSD_SYSTEM_MAX_AUX_SENDS; i++ )
    {
        m_refIRadSoundHalEffect[ i ] = NULL;
    }
    
	::radSoundPrintBanner( );
}

//============================================================================
// radSoundHalSystem::~radSoundHalSystem
//============================================================================

radSoundHalSystem::~radSoundHalSystem( void )
{
	radSoundHalListener::Terminate( );

    if (m_NumAuxSends > 0)
        alDeleteAuxiliaryEffectSlots(m_NumAuxSends, m_AuxSlots);

    alcMakeContextCurrent(NULL);
    if (m_pContext)
        alcDestroyContext(m_pContext);
    m_pContext = NULL;

    if (m_pDevice)
        alcCloseDevice(m_pDevice);
    m_pDevice = NULL;

	radSoundHalMemoryRegion::Terminate( );
    ::radMemoryFreeAligned( GetThisAllocator( ), m_pSoundMemory );

    s_pRsdSystem = NULL;
}

void AL_APIENTRY PrintOpenALErrors(ALenum source, ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message, void *userParam) noexcept
{
    (void)length;
    (void)userParam;
    TVOS_AUDIO_DIAG("OpenAL says: source=%u type=%u id=%u severity=%u '%s'", source, type, id, severity, message);
}

//============================================================================
// radSoundHalSystem::Initialize
//============================================================================

void radSoundHalSystem::Initialize( const SystemDescription & systemDescription )
{
    rAssertMsg( systemDescription.m_SamplingRate != 0, 
        "ERROR radsound: system sampling rate must be set"
        "to the highest sampling rate required by your program (probably 48000Hz)" );

    m_NumAuxSends = systemDescription.m_NumAuxSends;

    // Initialize OpenAL

    m_pDevice = alcOpenDevice(NULL);

    rAssertMsg( m_pDevice != NULL, "OpenAL device couldn't be opened." );
    if ( m_pDevice == NULL )
    {
        return;
    }

    ALenum err = alcGetError(m_pDevice);
    rAssertMsg(err == AL_NO_ERROR, "OpenAL device error after open.");

    if (err == AL_NO_ERROR)
    {
        //
        // Setup the primary context.
        //

#ifdef RAD_TVOS
        // OpenAL Soft period tuning for smoother streaming under CPU spikes
        // periods=4 and period_size=1024 reduces stutter risk
        // These are OpenAL Soft specific attributes
        ALCint attr[] = {
            ALC_FREQUENCY, (ALCint)systemDescription.m_SamplingRate,
            ALC_MAX_AUXILIARY_SENDS, m_NumAuxSends,
            0x1998, 4,      // ALC_SOFT_output_limiter / periods (OpenAL Soft extension)
            0x1996, 1024,   // period_size (OpenAL Soft extension)  
            0
        };
        TVOS_AUDIO_DIAG("[AUDIO_INIT] OpenAL context with period tuning: periods=4, period_size=1024");
#else
        ALCint attr[] = {
            ALC_FREQUENCY, (ALCint)systemDescription.m_SamplingRate,
            ALC_MAX_AUXILIARY_SENDS, m_NumAuxSends,
            0
        };
#endif
        m_pContext = alcCreateContext(m_pDevice, attr);

        rAssertMsg( m_pContext != NULL, "OpenAL context couldn't be created." );
        if ( m_pContext == NULL )
        {
            return;
        }

        ALenum err = alcGetError(m_pDevice);
        rAssertMsg(err == AL_NO_ERROR, "OpenAL context couldn't be created.");

        if (err == AL_NO_ERROR)
        {
            alcMakeContextCurrent(m_pContext);

            // AL_SOFTX_map_buffer is an OpenAL Soft extension for streaming buffers
            // Required for streaming audio to work correctly (allows in-place buffer updates)
            if( alIsExtensionPresent( "AL_SOFTX_map_buffer" ) )
            {
                radBufferStorageSOFT = (LPALBUFFERSTORAGESOFT)alGetProcAddress( "alBufferStorageSOFT" );
                radMapBufferSOFT = (LPALMAPBUFFERSOFT)alGetProcAddress( "alMapBufferSOFT" );
                radUnmapBufferSOFT = (LPALUNMAPBUFFERSOFT)alGetProcAddress( "alUnmapBufferSOFT" );
#ifdef RAD_TVOS
                TVOS_AUDIO_DIAG("[AUDIO_INIT] AL_SOFTX_map_buffer extension ENABLED - streaming audio uses map buffer");
                TVOS_AUDIO_DIAG("[AUDIO_INIT] radBufferStorageSOFT=%p radMapBufferSOFT=%p radUnmapBufferSOFT=%p",
                        (void*)radBufferStorageSOFT, (void*)radMapBufferSOFT, (void*)radUnmapBufferSOFT);
#endif
            }
            else
            {
                // Extension not available - streaming audio will use fallback path
                // NOTE: This fallback does NOT work correctly for streaming because
                // alBufferData cannot modify buffers attached to playing sources
                radBufferStorageSOFT = NULL;
                radMapBufferSOFT = NULL;
                radUnmapBufferSOFT = NULL;
#ifdef RAD_TVOS
                TVOS_AUDIO_DIAG("[AUDIO_INIT] WARNING: AL_SOFTX_map_buffer NOT available!");
                TVOS_AUDIO_DIAG("[AUDIO_INIT] Streaming audio may not work correctly without this extension");
#endif
            }

#if !defined( RAD_TVOS ) || defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
            // enable debug messages, as of OpenAL-Soft v1.23.1 this extension has not been released yet
            if (alIsExtensionPresent("AL_EXT_debug"))
            {
                const ALenum alDebugOutputEnum = alGetEnumValue("AL_DEBUG_OUTPUT_EXT");
                const auto alDebugMessageCallbackEXT = (LPALDEBUGMESSAGECALLBACKEXT)alGetProcAddress("alDebugMessageCallbackEXT");
                alEnable(alDebugOutputEnum);
                alDebugMessageCallbackEXT(PrintOpenALErrors, /*userParam*/nullptr);
            }
#endif

            if (m_NumAuxSends > 0 && alcIsExtensionPresent(m_pDevice, "ALC_EXT_EFX"))
            {
                alGenAuxiliaryEffectSlots = (LPALGENAUXILIARYEFFECTSLOTS)alGetProcAddress("alGenAuxiliaryEffectSlots");
                alDeleteAuxiliaryEffectSlots = (LPALDELETEAUXILIARYEFFECTSLOTS)alGetProcAddress("alDeleteAuxiliaryEffectSlots");
                alAuxiliaryEffectSlotf = (LPALAUXILIARYEFFECTSLOTF)alGetProcAddress("alAuxiliaryEffectSlotf");
                alGetAuxiliaryEffectSlotf = (LPALGETAUXILIARYEFFECTSLOTF)alGetProcAddress("alGetAuxiliaryEffectSlotf");

                alcGetIntegerv(m_pDevice, ALC_MAX_AUXILIARY_SENDS, 1, &m_NumAuxSends);
                alGenAuxiliaryEffectSlots(m_NumAuxSends, m_AuxSlots);
            }
            else
            {
                m_NumAuxSends = 0;
            }
        }
    }

    radSoundHalListener::Initialize
	(
		GetThisAllocator( ),
        m_pContext
	);

    // Allocate memory

    m_pSoundMemory = ::radMemoryAllocAligned( 
        GetThisAllocator( ),
        systemDescription.m_ReservedSoundMemory, 
        radSoundHalDataSourceReadAlignmentGet( ) );

    radSoundHalMemoryRegion::Initialize( 
        m_pSoundMemory, 
        systemDescription.m_ReservedSoundMemory, 
        systemDescription.m_MaxRootAllocations,
        radSoundHalDataSourceReadAlignmentGet( ), 
        radMemorySpace_Local, GetThisAllocator( ) );
}

//============================================================================
// radSoundHalSystem::GetRootMemoryRegion
//============================================================================

IRadSoundHalMemoryRegion * radSoundHalSystem::GetRootMemoryRegion( void )
{
	return radSoundHalMemoryRegion::GetRootRegion( );
}

//============================================================================
// radSoundHalSystem::GetNumAuxSends
//============================================================================

unsigned int radSoundHalSystem::GetNumAuxSends( )
{
    return m_NumAuxSends;
}

//============================================================================
// radSoundHalSystem::SetOutputMode
//============================================================================

void radSoundHalSystem::SetOutputMode( radSoundOutputMode mode )
{
	rDebugString( "radSoundHalSystem: SetOutputMode() not supported on Win32/XBox use DashBoard\n" );
}

//============================================================================
// radSoundHalSystem::GetOutputMode
//============================================================================

radSoundOutputMode radSoundHalSystem::GetOutputMode( void )
{
	return radSoundOutputMode_Stereo;
}

//============================================================================
// radSoundHalSystem::Service
//============================================================================

void radSoundHalSystem::Service( void )
{
    unsigned int now = ::radTimeGetMilliseconds( );

    unsigned int dif = now - m_LastServiceTime;
    if ( dif > 200 )
    {
        dif = 200;
    }

    radSoundUpdatableObject::UpdateAll( dif );

    m_LastServiceTime = now;
}

//============================================================================
// radSoundHalSystem::ServiceOncePerFrame
//============================================================================

void radSoundHalSystem::ServiceOncePerFrame( void )
{
	radSoundHalListener::GetInstance( )->UpdatePositionalSettings( );
}

//============================================================================
// radSoundHalSystem::GetStats
//============================================================================
    
void radSoundHalSystem::GetStats( IRadSoundHalSystem::Stats * pStats )
{
    rAssert( pStats );

    ::memset( pStats, 0, sizeof( IRadSoundHalSystem::Stats ) );

	//
	// Get voice info
	//

	radSoundHalVoiceWin * pVoiceSearch = radSoundHalVoiceWin::GetLinkedClassHead( );
		
    while ( pVoiceSearch != NULL )
    {
		if ( pVoiceSearch->GetPositionalGroup( ) != NULL )
		{
			pStats->m_NumPosVoices++;

			if ( pVoiceSearch->IsPlaying( ) )
			{
				pStats->m_NumPosVoicesPlaying++;
			}				
		}
		else
		{
			pStats->m_NumVoices++;

			if ( pVoiceSearch->IsPlaying( ) )
			{
				pStats->m_NumVoicesPlaying++;
			}
		}

        pVoiceSearch = pVoiceSearch->GetLinkedClassNext( );
    }

	//
	// GetBuffer info
	//
	
	radSoundHalBufferWin * pBufferSearch = radSoundHalBufferWin::GetLinkedClassHead( );

	while ( pBufferSearch != NULL )
	{
		pStats->m_NumBuffers ++;
		pStats->m_BufferMemoryUsed += pBufferSearch->GetSizeInBytes( );
		
		pBufferSearch = pBufferSearch->GetLinkedClassNext( );
	}
	
	// Effects Memory is always zero it is in the hardware.

	pStats->m_EffectsMemoryUsed = 0;
									
	radSoundHalMemoryRegion::GetRootRegion( )->GetStats( & pStats->m_TotalFreeSoundMemory, NULL, NULL, true );
}

//============================================================================
// radSoundHalSystem::SetAuxEffect
//============================================================================

void radSoundHalSystem::SetAuxEffect( unsigned int auxNumber, IRadSoundHalEffect * pIRadSoundHalEffect )
{
    rAssert( auxNumber < m_NumAuxSends );

    if( m_refIRadSoundHalEffect[ auxNumber ] != NULL )
    {
        m_refIRadSoundHalEffect[ auxNumber ]->Detach( );
    }

    m_refIRadSoundHalEffect[ auxNumber ] = pIRadSoundHalEffect;

    if( m_refIRadSoundHalEffect[ auxNumber ] != NULL )
    {
        m_refIRadSoundHalEffect[ auxNumber ]->Attach( auxNumber );
    }
}

//============================================================================
// radSoundHalSystem::GetAuxEffect
//============================================================================

IRadSoundHalEffect * radSoundHalSystem::GetAuxEffect( unsigned int auxNumber )
{
    rAssert( auxNumber < m_NumAuxSends );
    return m_refIRadSoundHalEffect[ auxNumber ];
}

//============================================================================
// radSoundHalSystem::SetAuxGain
//============================================================================

void radSoundHalSystem::SetAuxGain( unsigned int aux, float gain )
{
    rAssert(aux < m_NumAuxSends);
    alAuxiliaryEffectSlotf(m_AuxSlots[aux], AL_EFFECTSLOT_GAIN, gain);
    rAssert(alGetError() == AL_NO_ERROR);
}

//============================================================================
// radSoundHalSystem::GetAuxGain
//============================================================================

float radSoundHalSystem::GetAuxGain( unsigned int aux )
{
    rAssert(aux < m_NumAuxSends);
    rWarningMsg( false, "system::GetAuxGain not supported on PC" );
    ALfloat gain;
    alGetAuxiliaryEffectSlotf(m_AuxSlots[aux], AL_EFFECTSLOT_GAIN, &gain);
    rAssert(alGetError() == AL_NO_ERROR);
    return gain;
}

//============================================================================
// radSoundHalSystem::GetOpenALDevice
//============================================================================

ALCdevice * radSoundHalSystem::GetOpenALDevice( void )
{
    return m_pDevice;
}

//============================================================================
// radSoundHalSystem::GetOpenALContext
//============================================================================

ALCcontext * radSoundHalSystem::GetOpenALContext( void )
{
    return m_pContext;
}

//============================================================================
// radSoundHalSystem::GetContext
//============================================================================

ALuint radSoundHalSystem::GetOpenALAuxSlot( unsigned int aux )
{
    rAssert(aux < m_NumAuxSends);

    return m_AuxSlots[aux];
}

//============================================================================
// radSoundHalSystem::GetInstance
//============================================================================

radSoundHalSystem * radSoundHalSystem::GetInstance( void )
{
    return s_pRsdSystem;
}

//================================================================================
// ::rsdGetSystem
//================================================================================

IRadSoundHalSystem * radSoundHalSystemGet( void )
{
    rAssert( radSoundHalSystem::s_pRsdSystem != NULL );

    return radSoundHalSystem::s_pRsdSystem;
}

//================================================================================
// ::radSoundIntialize
//================================================================================

void radSoundHalSystemInitialize( radMemoryAllocator allocator  )
{
    rAssert( radSoundHalSystem::s_pRsdSystem == NULL );

    new( "radSoundHalSystem", allocator ) radSoundHalSystem( allocator );
    radSoundHalSystem::s_pRsdSystem->AddRef( );
}

//================================================================================
// ::radSoundIntialize
//================================================================================
        
void radSoundHalSystemTerminate( void )
{
    rAssert( radSoundHalSystem::s_pRsdSystem != NULL );

    radSoundHalSystem::s_pRsdSystem->Release( );
}











   
