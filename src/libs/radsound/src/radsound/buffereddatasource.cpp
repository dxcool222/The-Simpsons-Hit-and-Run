//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include "pch.hpp"
#include <radsoundfile.hpp>
#include "buffereddatasource.hpp"

#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif
static unsigned int s_bufferedDSSetCount = 0;

static const char* GetBDSEncodingName(IRadSoundHalAudioFormat::Encoding enc) {
    switch(enc) {
        case IRadSoundHalAudioFormat::PCM: return "PCM";
        case IRadSoundHalAudioFormat::PCM_BIGENDIAN: return "PCM_BE";
        case IRadSoundHalAudioFormat::RadicalAdpcm: return "RADPCM";
        case IRadSoundHalAudioFormat::XBOXADPCM: return "XBOXADPCM";
        case IRadSoundHalAudioFormat::VAG: return "VAG";
        case IRadSoundHalAudioFormat::GCNADPCM: return "GCNADPCM";
        default: return "UNKNOWN";
    }
}
#endif

#ifndef FINAL
#ifdef RAD_PS2
#include <radtextdisplay.hpp>
#include <eekernel.h>
#include <eeregs.h>

//
// Simpsons 2 hack, see AllocateResources() below -- Esan
//
#ifdef RAD_RELEASE
extern void Simpsons2MFIFODisable();
#endif
#endif
#endif

//============================================================================
// radSoundBufferedDataSource::radSoundBufferedDataSource
//============================================================================

radSoundBufferedDataSource::radSoundBufferedDataSource( void )
	:
	m_State( NONE ),
	m_pFrameBuffer( NULL ),
	m_BufferSizeInFrames( 0 ),
	m_StartOfDataInFrames( 0 ),
	m_EndOfDataInFrames( 0 ),
	m_QueueFull( false ),
	m_FullCopySize( 0 ),
	m_FramesLeftToCopy( 0 ),
	m_CurrentFramesToCopy( 0 ),
	m_pCurrentCopyPointer( NULL ),
	m_CopyMemorySpace( radMemorySpace_Null ),
	m_xICopyRequest( NULL ),
	m_ReadSizeInFrames( 0 ),
	m_OutOfData( false ),
    m_LowWaterMark( 0.50f )
{
    ::radStringCreate( & m_xIRadString_Name, GetThisAllocator( ) );
}

//============================================================================
// radSoundBufferedDataSource::~radSoundBufferedDataSource
//============================================================================

radSoundBufferedDataSource::~radSoundBufferedDataSource( void )
{
	if ( m_pFrameBuffer != NULL )
	{
        m_xIRadMemoryAllocator_FrameBuffer->FreeMemoryAligned( m_pFrameBuffer );
	}
}

//============================================================================
// radSoundBufferedDataSource::IsInitialized
//============================================================================

IRadSoundHalDataSource::State radSoundBufferedDataSource::GetState( void )
{
	return m_State == INITIALIZED ? IRadSoundHalDataSource::Initialized : IRadSoundHalDataSource::Initializing;
}

//============================================================================
// radSoundBufferedDataSource::SetInputDataSource
//============================================================================

void radSoundBufferedDataSource::SetInputDataSource( IRadSoundHalDataSource * pIRshds )
{
#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
    s_bufferedDSSetCount++;
    
    // Log first 50 + every 50th after
    if (s_bufferedDSSetCount <= 50 || (s_bufferedDSSetCount % 50) == 0) {
        if (pIRshds != NULL) {
            IRadSoundHalDataSource::State dsState = pIRshds->GetState();
            const char* stateName = (dsState == IRadSoundHalDataSource::Initializing) ? "Initializing" :
                                    (dsState == IRadSoundHalDataSource::Initialized) ? "Initialized" :
                                    (dsState == IRadSoundHalDataSource::Error) ? "Error" : "Unknown";
            
            TVOS_AUDIO_DIAG("[BUFFERED_DS] #%u SetInputDataSource: ptr=%p state=%s name='%s'",
                    s_bufferedDSSetCount, (void*)pIRshds, stateName,
                    m_xIRadString_Name ? m_xIRadString_Name->GetChars() : "?");
            
            if (dsState == IRadSoundHalDataSource::Initialized) {
                IRadSoundHalAudioFormat* fmt = pIRshds->GetFormat();
                if (fmt) {
                    TVOS_AUDIO_DIAG("[BUFFERED_DS] #%u Format: enc=%s rate=%u ch=%u bits=%u",
                            s_bufferedDSSetCount, GetBDSEncodingName(fmt->GetEncoding()),
                            fmt->GetSampleRate(), fmt->GetNumberOfChannels(), fmt->GetBitResolution());
                }
            }
        } else {
            TVOS_AUDIO_DIAG("[BUFFERED_DS] #%u SetInputDataSource: NULL (clearing)", s_bufferedDSSetCount);
        }
    }
#endif

    if ( m_State == NONE )
    {
        if ( pIRshds != NULL )
        {
	        m_State = INITIALIZING;
            m_xIRadSoundHalDataSource = pIRshds;
            AddToUpdateList( );
        }
    }
    else if ( m_State == INITIALIZING )
    {
        m_xIRadSoundHalDataSource = pIRshds;

        if ( pIRshds == NULL )
        {
            m_State = NONE;
            RemoveFromUpdateList( );
        }
    }
    else if ( m_State == REINITIALIZING )
    {
        // Just swap the source "on deck"

        m_xIRadSoundHalDataSource_ReInit = pIRshds;
    }
    else if ( m_State == INITIALIZED )
    {            
        // Here we are in the middle of a copy or a read.
        // We must wait until the outstanding operations have finished, then
        // re-initialize.

        m_State = REINITIALIZING;

        m_xIRadSoundHalDataSource_ReInit = pIRshds;
    }
    else
    {
        rAssert( false );
    }

	Service( ); // Kick start so we init the memory right now (if possible)
}

//============================================================================
// radSoundBufferedDataSource::Initialize
//============================================================================

void radSoundBufferedDataSource::Initialize
(
	radMemorySpace bufferSpace,
	IRadMemoryAllocator * pIRadMemoryAllocator,
	unsigned int milliseconds,
	IRadSoundHalAudioFormat::SizeType sizeType,
    IRadSoundHalAudioFormat * pIRshaf,
    const char * pIdentifier
)
{
    // This function is only called once at the beggining

	rAssert( m_State == NONE );
    rAssert( pIRadMemoryAllocator != NULL );
    rAssertMsg( pIdentifier != NULL,
        "You MUST name all of your buffered data source objects so we can track memory usefully" );

    m_xIRadString_Name->Copy( pIdentifier );

	m_InitSize = milliseconds;
	m_InitSizeType = sizeType;

	m_FrameBufferMemorySpace = bufferSpace;
	m_xIRadMemoryAllocator_FrameBuffer = pIRadMemoryAllocator;

    if ( pIRshaf != NULL )
    {
        m_xIRadSoundHalAudioFormat = pIRshaf;
        AllocateResources( );
    }
};

//============================================================================
// radSoundBufferedDataSource::SetLowWaterMark
//============================================================================

void radSoundBufferedDataSource::SetLowWaterMark( float lowWaterMark )
{
    rAssert( lowWaterMark > 0.0f && lowWaterMark <= 1.0f );
    m_LowWaterMark = lowWaterMark;
}

//============================================================================
// radSoundBufferedDataSource::GetLowWaterMark
//============================================================================

float radSoundBufferedDataSource::GetLowWaterMark( void )
{
    return m_LowWaterMark;
}

//============================================================================
// radSoundBufferedDataSource::GetFormat
//============================================================================

IRadSoundHalAudioFormat * radSoundBufferedDataSource::GetFormat( void )
{
    // The format can't change once it has been initialized

	return m_xIRadSoundHalAudioFormat;
}

//============================================================================
// radSoundBufferedDataSource::GetInputDataSource
//============================================================================

IRadSoundHalDataSource * radSoundBufferedDataSource::GetInputDataSource( void )
{
	return m_xIRadSoundHalDataSource_ReInit ?
           m_xIRadSoundHalDataSource_ReInit :
           m_xIRadSoundHalDataSource;
}

//============================================================================
// radSoundBufferedDataSource::GetAvailableFrames
//============================================================================

unsigned int radSoundBufferedDataSource::GetAvailableFrames( void )
{
    rAssert( m_State == INITIALIZED || m_State == REINITIALIZING );

	if ( m_StartOfDataInFrames < m_EndOfDataInFrames )
	{
		return m_EndOfDataInFrames - m_StartOfDataInFrames;
	}
	else if ( m_StartOfDataInFrames > m_EndOfDataInFrames )
	{
		return m_BufferSizeInFrames - ( m_StartOfDataInFrames - m_EndOfDataInFrames );
	}
	else
	{
		if ( m_QueueFull )
		{
			return m_BufferSizeInFrames;
		}
		else
		{
			return 0;
		}
	}		
}

//============================================================================
// radSoundBufferedDataSource::OnDataSourceFramesLoaded
//============================================================================

void radSoundBufferedDataSource::OnDataSourceFramesLoaded( unsigned int framesActuallyRead )
{
	rAssert( m_ReadSizeInFrames > 0 );

#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
    // Verify data was actually read into the buffer
    static unsigned int s_dataLoadedCount = 0;
    s_dataLoadedCount++;
    if (s_dataLoadedCount <= 20 || (s_dataLoadedCount % 100) == 0) {
        // Check a sample of the just-loaded data
        unsigned int loadOffset = m_xIRadSoundHalAudioFormat->FramesToBytes(m_EndOfDataInFrames);
        unsigned char* loadedData = (unsigned char*)(m_pFrameBuffer + loadOffset);
        bool allZero = true;
        unsigned char minVal = 255, maxVal = 0;
        unsigned int checkBytes = m_xIRadSoundHalAudioFormat->FramesToBytes(framesActuallyRead);
        if (checkBytes > 64) checkBytes = 64;
        for (unsigned int i = 0; i < checkBytes; i++) {
            if (loadedData[i] != 0) allZero = false;
            if (loadedData[i] < minVal) minVal = loadedData[i];
            if (loadedData[i] > maxVal) maxVal = loadedData[i];
        }
        TVOS_AUDIO_DIAG("[BUFFERED_DS] #%u OnFramesLoaded: frames=%u buffer=%p offset=%u allZero=%s range=[%u-%u]",
                s_dataLoadedCount, framesActuallyRead, (void*)m_pFrameBuffer, loadOffset,
                allZero ? "YES-BAD" : "no", minVal, maxVal);
    }
#endif

	m_EndOfDataInFrames = ( m_EndOfDataInFrames + framesActuallyRead ) % m_BufferSizeInFrames;

	if ( framesActuallyRead < m_ReadSizeInFrames )
	{
		m_OutOfData = true;
	}

	if ( m_EndOfDataInFrames == m_StartOfDataInFrames && ! m_OutOfData )
	{
		m_QueueFull = true;
	}

	m_ReadSizeInFrames = 0;

	if ( framesActuallyRead > 0 )
	{
		Service( );
	}
}

//============================================================================
// radSoundBufferedDataSource::GetRemainingFrames
//============================================================================

unsigned int radSoundBufferedDataSource::GetRemainingFrames( void )
{
	if ( m_xIRadSoundHalDataSource != NULL )
	{
	    if ( m_OutOfData )
	    {
	        return GetAvailableFrames( );
	    }
	    else if ( m_xIRadSoundHalDataSource->GetRemainingFrames( ) == 0xFFFFFFFF )
		{
			return 0xFFFFFFFF;
		}
		else
		{
			return m_xIRadSoundHalDataSource->GetRemainingFrames( ) + GetAvailableFrames( );
		}
	}

	return 0;
}

//============================================================================
// radSoundBufferedDataSource::GetFramesAsync
//============================================================================

void radSoundBufferedDataSource::GetFramesAsync
(
	void * pFrameBuffer,
	radMemorySpace destinationMemorySpace,
	unsigned int sizeInFrames,
	IRadSoundHalDataSourceCallback * pIRshdsc
)
{
	rAssert( m_State == INITIALIZED );
	//rAssert( m_FullCopySize == 0 );

	// Set up the read.

	m_FullCopySize = sizeInFrames;
	m_FramesLeftToCopy = sizeInFrames;
	m_xIRadSoundHalDataSourceCallback = pIRshdsc;
	m_pCurrentCopyPointer = (char*) pFrameBuffer;
	m_CopyMemorySpace = destinationMemorySpace;

    if ( GetAvailableFrames( ) < sizeInFrames && ( false == m_OutOfData ) )
	{
		rReleasePrintf( "AUDIO: Buffer Underrun: [%s]\n", m_xIRadSoundHalDataSource ?
		    m_xIRadSoundHalDataSource->GetName( ) : "NULL" );
	}

	Service( );
}

//============================================================================
// radSoundBufferedDataSource::ServiceCopy
//============================================================================

void radSoundBufferedDataSource::ServiceCopy( void )
{
    // If we are currently copying

	if( m_xICopyRequest != NULL )
	{
        rAssert( m_State == INITIALIZED || m_State == REINITIALIZING );

        // and the request is done

		if( m_xICopyRequest->IsDone( ) )
		{
			// The request is complete so set it to null
			m_xICopyRequest = NULL;
            
			rAssert( m_CurrentFramesToCopy > 0 );

			m_pCurrentCopyPointer += m_xIRadSoundHalAudioFormat->FramesToBytes( m_CurrentFramesToCopy );
			m_StartOfDataInFrames = ( m_StartOfDataInFrames + m_CurrentFramesToCopy ) % m_BufferSizeInFrames;
			m_FramesLeftToCopy -= m_CurrentFramesToCopy;
			m_CurrentFramesToCopy = 0;

			m_QueueFull = false;

			if( m_FramesLeftToCopy == 0 || m_State == REINITIALIZING ) 
			{
				unsigned int framesCopied = m_FullCopySize - m_FramesLeftToCopy;

				ref< IRadSoundHalDataSourceCallback > xIRshdsc( m_xIRadSoundHalDataSourceCallback );

				// Reset copy info

				m_FullCopySize = 0;
				m_FramesLeftToCopy = 0;
				m_xIRadSoundHalDataSourceCallback = NULL;
				m_pCurrentCopyPointer = NULL;
				m_CopyMemorySpace = radMemorySpace_Null;

				// Callback last thing so we don't blow up.

				Service( );

				xIRshdsc->OnDataSourceFramesLoaded( framesCopied );

				return;
			}
		}
	}
    else if ( m_FramesLeftToCopy > 0 )
	{
		m_CurrentFramesToCopy = GetAvailableFrames( );

		if ( m_CurrentFramesToCopy > 0 )
		{
			// Don't read past the end of the buffer, we will get the wrapped
			// chunk next time.
			
			if ( ( m_StartOfDataInFrames + m_CurrentFramesToCopy ) > m_BufferSizeInFrames )
			{
				m_CurrentFramesToCopy = m_BufferSizeInFrames - m_StartOfDataInFrames;
			}

			if ( m_CurrentFramesToCopy >= m_FramesLeftToCopy )
			{
				m_CurrentFramesToCopy = m_FramesLeftToCopy;
			}

			if ( m_CurrentFramesToCopy > 0 )
			{
#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
                // Log copy operation and verify source data
                static unsigned int s_copyCount = 0;
                s_copyCount++;
                if (s_copyCount <= 20 || (s_copyCount % 100) == 0) {
                    unsigned char* srcData = (unsigned char*)(m_pFrameBuffer + m_xIRadSoundHalAudioFormat->FramesToBytes( m_StartOfDataInFrames ));
                    bool srcAllZero = true;
                    unsigned char srcMin = 255, srcMax = 0;
                    unsigned int copyBytes = m_xIRadSoundHalAudioFormat->FramesToBytes( m_CurrentFramesToCopy );
                    unsigned int checkBytes = (copyBytes > 64) ? 64 : copyBytes;
                    for (unsigned int i = 0; i < checkBytes; i++) {
                        if (srcData[i] != 0) srcAllZero = false;
                        if (srcData[i] < srcMin) srcMin = srcData[i];
                        if (srcData[i] > srcMax) srcMax = srcData[i];
                    }
                    TVOS_AUDIO_DIAG("[BUFFERED_COPY] #%u src=%p dst=%p bytes=%u srcZero=%s range=[%u-%u]",
                            s_copyCount, (void*)srcData, (void*)m_pCurrentCopyPointer, copyBytes,
                            srcAllZero ? "YES-BAD" : "no", srcMin, srcMax);
                }
#endif

				m_xICopyRequest = ::radMemorySpaceCopyAsync(
					m_pCurrentCopyPointer,
					m_CopyMemorySpace,
					m_pFrameBuffer + m_xIRadSoundHalAudioFormat->FramesToBytes( m_StartOfDataInFrames ),
					m_FrameBufferMemorySpace, m_xIRadSoundHalAudioFormat->FramesToBytes( m_CurrentFramesToCopy ) );

				Service( );
			}
		}
		else if ( m_OutOfData || m_State == REINITIALIZING )
		{
			ref< IRadSoundHalDataSourceCallback > xIRshdsc( m_xIRadSoundHalDataSourceCallback );

            unsigned int framesCopiedSoFar = m_FullCopySize - m_FramesLeftToCopy;

			// Reset copy info
            
			m_FullCopySize = 0;
			m_FramesLeftToCopy = 0;
			m_xIRadSoundHalDataSourceCallback = NULL;
			m_pCurrentCopyPointer = NULL;
			m_CopyMemorySpace = radMemorySpace_Null;

			// Callback last thing so we don't blow up.

			Service( );

			xIRshdsc->OnDataSourceFramesLoaded( framesCopiedSoFar );
		}
	}
}

//============================================================================
// radSoundBufferedDataSource::OnMemoryCopyAsyncComplete
//============================================================================

void radSoundBufferedDataSource::OnMemoryCopyAsyncComplete( void * pUserData )
{

}

//============================================================================
// radSoundBufferedDataSource::Update
//============================================================================

void radSoundBufferedDataSource::Update( unsigned int elapsedTime )
{
	Service( );
}

//============================================================================
// radSoundBufferedDataSource::Service
//============================================================================

void radSoundBufferedDataSource::Service( void )
{
	switch ( m_State )
	{
		case NONE:
		{
			// do nothing
			break;
		}
		case INITIALIZING:
		{            
			ServiceInitializingSource( );

			break;
		}
		case INITIALIZED:
		{
			ServiceRead( );
			ServiceCopy( );

			break;
		}
        case REINITIALIZING:
        {
            if ( m_ReadSizeInFrames == 0 && m_FramesLeftToCopy == 0 )
            {
	            m_EndOfDataInFrames = 0;
                m_StartOfDataInFrames = 0;
	            m_QueueFull = false;
	            m_OutOfData = false;	            

                if ( m_xIRadSoundHalDataSource_ReInit != NULL )
                {
                    m_State = INITIALIZING;
                }
                else
                {
                    m_State = NONE;
                    RemoveFromUpdateList( );
                }

                m_xIRadSoundHalDataSource = m_xIRadSoundHalDataSource_ReInit;                    
                m_xIRadSoundHalDataSource_ReInit = NULL;
            }
            else
            {
                ServiceCopy( );
            }
        }
	}
}

//============================================================================
// radSoundBufferedDataSource::ServiceInitializingSource
//============================================================================

void radSoundBufferedDataSource::ServiceInitializingSource( void )
{
	if ( m_xIRadSoundHalDataSource->GetState( ) == IRadSoundHalDataSource::Initialized )
	{
        //
        // Check if we already have initialized the buffer memory and format.
        // This is true if we were REINITIALIZING
        //

        if ( m_xIRadSoundHalAudioFormat == NULL )
        {
            m_xIRadSoundHalAudioFormat = m_xIRadSoundHalDataSource->GetFormat( );
            AllocateResources( );
        }
        else
        {
            rAssert( m_xIRadSoundHalAudioFormat->Matches( m_xIRadSoundHalDataSource->GetFormat( ) ) );
            rAssert( m_pFrameBuffer != NULL );
            rAssert( m_BufferSizeInFrames > 0 );
        }

		m_State = INITIALIZED;

		Service( );
	}
}

//============================================================================
// radSoundBufferedDataSource::AllocateResources
//============================================================================

void radSoundBufferedDataSource::AllocateResources( void )
{
    rAssert( m_BufferSizeInFrames == 0 );
    rAssert( m_pFrameBuffer == NULL );

    unsigned int buffersizeInBytes = 
		m_xIRadSoundHalAudioFormat->ConvertSizeType(
			IRadSoundHalAudioFormat::Bytes, // target
			m_InitSize,
			m_InitSizeType );
    //
    // Round up the buffersize in bytes for optimal disk access
    //

    buffersizeInBytes = ::radMemoryRoundUp( 
            buffersizeInBytes, 
            radSoundHalDataSourceReadMultipleGet( ) * 2 );

    m_BufferSizeInFrames = m_xIRadSoundHalAudioFormat->ConvertSizeType(
			IRadSoundHalAudioFormat::Frames, // target
			buffersizeInBytes,
			IRadSoundHalAudioFormat::Bytes );

	m_pFrameBuffer = (char*) m_xIRadMemoryAllocator_FrameBuffer->GetMemoryAligned(
		buffersizeInBytes,
		radSoundHalDataSourceReadAlignmentGet( ) );

    rAssert( m_pFrameBuffer != NULL );
#ifndef FINAL
#ifdef RAD_PS2
#ifdef RAD_RELEASE
    //IRadTextDisplay* textDisplay;

    //if( m_pFrameBuffer == NULL )
    //{
    //    //
    //    // HAAAAAAAAAACCCCCCCKKKKKKKKKK!!!!!!!!!!!!!
    //    //
    //    // Need to shut down the MFIFO for this to work properly.  Instead of dragging
    //    // P3D dependencies in here, I'm going to call an external function for it which
    //    // I'll define in the game code.  I'll also submit a feature request so that
    //    // future generations don't have to do horrible things like this. -- Esan
    //    //
    //    Simpsons2MFIFODisable();

    //    ::radTextDisplayGet( &textDisplay );

    //    textDisplay->SetBackgroundColor( 0 );
    //    textDisplay->SetTextColor( 0xffffffff );
    //    textDisplay->Clear();
    //    textDisplay->TextOutAt( "Out of IOP memory.  Bah.", 15, 7 );
    //    textDisplay->TextOutAt( ":-(", 15, 9 );
    //    textDisplay->SwapBuffers();
    //    textDisplay->Release();
    //}
#endif
#endif
#endif

    ::radMemoryMonitorIdentifyAllocation(
        m_pFrameBuffer, radSoundDebugChannel,
        m_xIRadString_Name->GetChars( ),
        NULL,
        m_FrameBufferMemorySpace );
}

//============================================================================
// radSoundBufferedDataSource::ServiceRead
//============================================================================

void radSoundBufferedDataSource::ServiceRead( void )
{
    if ( ! m_OutOfData )
    {
	    if ( m_ReadSizeInFrames == 0 )
	    {
            unsigned int lowWaterMarkInFrames = radSoundFloatToUInt(
                    (radSoundUIntToFloat( m_BufferSizeInFrames ) * m_LowWaterMark ) );

            unsigned int optimalReadMultipleInFrames =
                m_xIRadSoundHalAudioFormat->BytesToFrames( radSoundHalDataSourceReadMultipleGet( ) );

		    if ( GetAvailableFrames( ) <= lowWaterMarkInFrames )
		    {
			    unsigned int frames;

			    if ( m_QueueFull )
			    {
				    frames = 0;
			    }
			    else if ( m_StartOfDataInFrames <= m_EndOfDataInFrames )
			    {
				    // This handles !queueFull and start==end
				    frames = m_BufferSizeInFrames - m_EndOfDataInFrames;
			    }
			    else
			    {
				    frames = m_StartOfDataInFrames - m_EndOfDataInFrames;
			    }

                frames = ::radMemoryRoundDown( frames, optimalReadMultipleInFrames );

			    rAssert( frames + m_EndOfDataInFrames <= m_BufferSizeInFrames );

			    if ( frames > 0 )
			    {
				    m_ReadSizeInFrames = frames;

				    /* rDebugPrintf( "Buffer: Reading: [%d] frames at: [0x%x]\n", frames,
					    m_pFrameBuffer + m_xIRadSoundHalAudioFormat->FramesToBytes( m_EndOfDataInFrames ) ); */

				    m_xIRadSoundHalDataSource->GetFramesAsync( 
					    m_pFrameBuffer + m_xIRadSoundHalAudioFormat->FramesToBytes( m_EndOfDataInFrames ),
					    m_FrameBufferMemorySpace,
					    frames, 
					    this );
			    }
		    }
        }
	}
}

bool radSoundBufferedDataSource::IsBufferFull( void )
{
    return m_OutOfData;
}
    
//============================================================================
// ::radSoundBufferedDataSourceCreate
//============================================================================

IRadSoundBufferedDataSource * radSoundBufferedDataSourceCreate( radMemoryAllocator allocator )
{
	radSoundBufferedDataSource * pRsbds = new ( "radSoundBufferedDataSource", allocator ) radSoundBufferedDataSource( );
   
    return pRsbds;
}
