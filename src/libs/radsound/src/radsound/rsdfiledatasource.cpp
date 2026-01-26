//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include "pch.hpp"
#include <radsoundfile.hpp>
#include "rsdfiledatasource.hpp"

#ifdef RAD_TVOS
#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif
static unsigned int s_audioFileLoadCount = 0;

static const char* GetEncodingName(IRadSoundHalAudioFormat::Encoding enc) {
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

RadSoundFilePerformanceCallback * gpFpc;

void Log(
    bool start,
    const char * pFile,
    unsigned int bytes )
{
    if ( gpFpc )
    {
        gpFpc( start, pFile, bytes );
    }
}    
    
void RadSoundSetFilePerformanceCallback( RadSoundFilePerformanceCallback * pCallback )
{
    gpFpc = pCallback;
}


radSoundRsdFileDataSource::radSoundRsdFileDataSource( void )
{
	m_StateInfo.m_State = StateInfo::NONE;
}

radSoundRsdFileDataSource::~radSoundRsdFileDataSource( void )
{
}

IRadSoundHalDataSource::State radSoundRsdFileDataSource::GetState( void )
{
    if ( m_StateInfo.m_State == StateInfo::INITIALIZED )
    {
        if ( m_refIRadFile == NULL )
        {            
	        ::radFileOpen(
	            & m_refIRadFile,
	            fileName,
	            false,
	            OpenExisting,
                NormalPriority, // m_StateInfo.m_InitializedInfo.m_HighPriority ? HighPriority : NormalPriority,
                0,
                RADMEMORY_ALLOC_TEMP,
                radMemorySpace_Local );
        }
        	    
	    m_StateInfo.m_State = StateInfo::OPENING_FILE;
	     
        m_refIRadFile->AddCompletionCallback( this, NULL );
 
    }
    else if (
        m_StateInfo.m_State == StateInfo::IDLE ||
	    m_StateInfo.m_State == StateInfo::READING_DATA )
	{
		return IRadSoundHalDataSource::Initialized;
	}
	else if ( m_StateInfo.m_State == StateInfo::FILE_ERROR )
	{
		return IRadSoundHalDataSource::Error;
	}
	
	return IRadSoundHalDataSource::Initializing;
}

void radSoundRsdFileDataSource::InitializeFromFile
( 
	IRadFile * pIRadFile,
	unsigned int initialPlaybackPosition,
	IRadSoundHalAudioFormat::SizeType positionType,
    IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat     
)
{
	rAssert( m_StateInfo.m_State == StateInfo::NONE );
	rAssert( pIRadFile != NULL );

    m_refIRadSoundHalAudioFormat = pIRadSoundHalAudioFormat;

    strcpy( fileName, "" );
    
	// We can't convert these values until we an audio format

    m_StateInfo.m_State = StateInfo::INITIALIZED;
    m_StateInfo.m_InitializedInfo.m_InitialPlaybackPos = initialPlaybackPosition;
    m_StateInfo.m_InitializedInfo.m_InitalPlaybackPosUnits = positionType;    
    m_StateInfo.m_InitializedInfo.m_HighPriority = true;
	//
	// We have a file that may or may not be open
	// We'll get the file system to call us back 
	// when the file is open and this will start our
	// state machine
	//

	m_refIRadFile = pIRadFile;

}

void radSoundRsdFileDataSource::InitializeFromFileName
( 
	const char * pFileName,
    bool highPriority,
	unsigned int initialPlaybackPosition,
	IRadSoundHalAudioFormat::SizeType positionType,
    IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat
)
{
    rAssert( pFileName != NULL );
	rAssert( m_StateInfo.m_State == StateInfo::NONE );

    m_refIRadSoundHalAudioFormat = pIRadSoundHalAudioFormat;
    
    strncpy( fileName, pFileName, 64 );
	fileName[ 63 ] = '\0';
			
	m_StateInfo.m_State = StateInfo::INITIALIZED;
	m_StateInfo.m_InitializedInfo.m_InitalPlaybackPosUnits = positionType;
	m_StateInfo.m_InitializedInfo.m_InitialPlaybackPos = initialPlaybackPosition;
	m_StateInfo.m_InitializedInfo.m_HighPriority = highPriority;

#ifdef RAD_TVOS
    s_audioFileLoadCount++;
    
    // Log first 50 audio files + every 100th after to catch NPC dialog loading
    if (s_audioFileLoadCount <= 50 || (s_audioFileLoadCount % 100) == 0) {
        const char* encoding = "N/A";
        unsigned int sampleRate = 0, channels = 0, bitRes = 0;
        
        if (pIRadSoundHalAudioFormat) {
            encoding = GetEncodingName(pIRadSoundHalAudioFormat->GetEncoding());
            sampleRate = pIRadSoundHalAudioFormat->GetSampleRate();
            channels = pIRadSoundHalAudioFormat->GetNumberOfChannels();
            bitRes = pIRadSoundHalAudioFormat->GetBitResolution();
        }
        
        // Check if this looks like a dialog/voice file
        bool isDialog = (strstr(pFileName, "dialog") != NULL || 
                         strstr(pFileName, "Dialog") != NULL ||
                         strstr(pFileName, "DIALOG") != NULL ||
                         strstr(pFileName, "voice") != NULL ||
                         strstr(pFileName, "Voice") != NULL);
        
        SDL_Log("[AUDIO_FILE] #%u %s: '%s'",
                s_audioFileLoadCount, isDialog ? "DIALOG" : "Load", pFileName);
        SDL_Log("[AUDIO_FILE] #%u Format: enc=%s rate=%u ch=%u bits=%u",
                s_audioFileLoadCount, encoding, sampleRate, channels, bitRes);
    }
#endif
};

IRadSoundHalAudioFormat * radSoundRsdFileDataSource::GetFormat( void )
{
    rAssert( m_StateInfo.m_State == StateInfo::IDLE || m_StateInfo.m_State == StateInfo::READING_DATA );
    rAssert( m_refIRadSoundHalAudioFormat != NULL );

	return m_refIRadSoundHalAudioFormat;
}

unsigned int radSoundRsdFileDataSource::GetRemainingFrames( void )
{
    rAssert( m_StateInfo.m_State == StateInfo::IDLE || m_StateInfo.m_State == StateInfo::READING_DATA );
	return m_FramesLeftInFile;
}

void radSoundRsdFileDataSource::GetFramesAsync
(
	void * pFrameBuffer,
	radMemorySpace destinationMemorySpace,
	unsigned int sizeInFrames,
	IRadSoundHalDataSourceCallback * pIRshdsc
)
{
    rAssert( m_StateInfo.m_State == StateInfo::IDLE );
	rAssert( pIRshdsc != NULL );

    /* rDebugPrintf( "FDS: [%s]\n\tthis: [0x%x] Request for [0x%x] frames from: [0x%x] @ [0x%x]\n",
        this->fileName,
        this,
        sizeInFrames,
        pIRshdsc,
        pFrameBuffer ); */
        
	if ( m_FramesLeftInFile < sizeInFrames )
	{
		sizeInFrames = m_FramesLeftInFile;
	}

	m_StateInfo.m_State = StateInfo::READING_DATA;

	// Get a read going

	m_StateInfo.m_ReadingDataInfo.m_ReadSizeInFrames = sizeInFrames;
#ifdef RAD_TVOS
	m_StateInfo.m_ReadingDataInfo.m_pReadBuffer = pFrameBuffer;
#endif
	m_refIRadSoundHalDataSourceCallback = pIRshdsc;

	// Figure out how many bytes we'll require
	// (And verify that our conversion is happening correctly)

	unsigned int bytes = m_refIRadSoundHalAudioFormat->FramesToBytes( sizeInFrames );
    
    /* if ( ((unsigned int)pFrameBuffer % radSoundStreamReadAlignmentGet( ) ) != 0 )
    {
        rDebugPrintf( "radSoundRsdFileDataSource: reading non-optimal alignment!\n" );
    } */

    // unsigned int optimalFrameReadMultiple = m_refIRadSoundHalAudioFormat->BytesToFrames( radSoundStreamReadMultipleGet( ) );

    /* if ( m_FramesLeftInFile > sizeInFrames )
    {
        if ( (sizeInFrames % optimalFrameReadMultiple ) != 0 )
        {
            rDebugPrintf( "radSoundRsdFileDataSource:: reading non-optimal size!\n" );
        }
    } */
    
    Log( true, this->fileName, bytes );
    
#ifdef RAD_PS2    
    //bytes = radMemoryRoundUp( bytes, 2048 );
#endif

#ifdef RAD_TVOS
    static unsigned int s_readAsyncCount = 0;
    s_readAsyncCount++;
    if (s_readAsyncCount <= 20 || (s_readAsyncCount % 100) == 0) {
        // Check buffer BEFORE read
        unsigned char* data = (unsigned char*)pFrameBuffer;
        bool preAllZero = true;
        for (unsigned int i = 0; i < 64 && i < bytes; i++) {
            if (data[i] != 0) { preAllZero = false; break; }
        }
        SDL_Log("[FILE_READ] #%u PRE-ReadAsync: file='%s' dest=%p bytes=%u memSpace=%d preZero=%s",
                s_readAsyncCount, fileName, pFrameBuffer, bytes, (int)destinationMemorySpace,
                preAllZero ? "yes" : "no");
    }
#endif
    
	m_refIRadFile->ReadAsync(pFrameBuffer, bytes, destinationMemorySpace );
	m_refIRadFile->AddCompletionCallback( this, NULL );

}

void radSoundRsdFileDataSource::OnFileOperationsComplete( void* pUserData )
{
	StateInfo::State state = m_StateInfo.m_State;

	switch ( m_StateInfo.m_State )
	{
		case StateInfo::NONE:
		{
			// do nothing
			break;
		}
		case StateInfo::OPENING_FILE:
		{
			_StateOpeningFile( );
			break;
		}
		case StateInfo::READING_HEADER:
		{
            Log( false, this->fileName, 0 );		
			_StateReadingHeader( );
			break;
		}
		case StateInfo::IDLE:
		{
			_StateIdle( );
			break;
		}
		case StateInfo::READING_DATA:
		{
            Log( false, this->fileName, 0 );		
			_StateReadingData( );
			break;
		}
        default:
        {
            rAssert( false );
        }
	};
}

void radSoundRsdFileDataSource::_StateOpeningFile( void )
{
	if( m_refIRadFile->IsOpen( ) == true )
	{
        m_refIRadFile->SetBufferedRead( IRadFile::BufferedReadOn );
        
#ifdef RAD_TVOS
        // CRITICAL: Log whether we're reading the actual RSD header or using pre-set format
        static unsigned int s_fileOpenCount = 0;
        s_fileOpenCount++;
        
        // GROUND TRUTH: Read the actual RSD header bytes to see what encoding the file declares
        // This is independent of what the game code thinks the format should be
        if (s_fileOpenCount <= 30 || (s_fileOpenCount % 50) == 0) {
            // Read first 20 bytes of file to get RSD header info
            // RSD header: bytes 0-3 = version tag, bytes 4-7 = encoding (e.g., "PCM ", "GADP")
            unsigned char headerBytes[20] = {0};
            
            // Save current position, seek to start, read header, restore position
            unsigned int savedPos = 0;
            m_refIRadFile->GetPositionSync(&savedPos);
            m_refIRadFile->SetPositionSync(0);
            m_refIRadFile->ReadSync(headerBytes, 20, radMemorySpace_Local);
            m_refIRadFile->SetPositionSync(savedPos);
            
            // Extract encoding string from header (bytes 4-7)
            char fileEnc[5] = {(char)headerBytes[4], (char)headerBytes[5], 
                               (char)headerBytes[6], (char)headerBytes[7], 0};
            
            // Log the ACTUAL file encoding vs what game code says
            const char* gameEncName = "NULL";
            if (m_refIRadSoundHalAudioFormat != NULL) {
                switch(m_refIRadSoundHalAudioFormat->GetEncoding()) {
                    case IRadSoundHalAudioFormat::PCM: gameEncName = "PCM"; break;
                    case IRadSoundHalAudioFormat::PCM_BIGENDIAN: gameEncName = "PCMB"; break;
                    case IRadSoundHalAudioFormat::RadicalAdpcm: gameEncName = "RADP"; break;
                    case IRadSoundHalAudioFormat::GCNADPCM: gameEncName = "GADP"; break;
                    case IRadSoundHalAudioFormat::VAG: gameEncName = "VAG"; break;
                    case IRadSoundHalAudioFormat::XBOXADPCM: gameEncName = "XADP"; break;
                }
            }
            
            SDL_Log("[RSD_TRUTH] #%u file='%s'", s_fileOpenCount, fileName);
            SDL_Log("[RSD_TRUTH] #%u ACTUAL FILE HEADER: enc='%s' (0x%02x%02x%02x%02x) tag='%.4s'",
                    s_fileOpenCount, fileEnc,
                    headerBytes[4], headerBytes[5], headerBytes[6], headerBytes[7],
                    (char*)headerBytes);
            SDL_Log("[RSD_TRUTH] #%u GAME CODE SAYS: enc='%s' (format %s)",
                    s_fileOpenCount, gameEncName,
                    m_refIRadSoundHalAudioFormat ? "PRE-SET" : "will read");
            
            // Flag mismatch - this is the smoking gun!
            if (m_refIRadSoundHalAudioFormat != NULL) {
                if (headerBytes[4] == 'G' && headerBytes[5] == 'A') {
                    SDL_Log("[RSD_TRUTH] #%u *** MISMATCH! File is GCNADPCM but game says %s ***",
                            s_fileOpenCount, gameEncName);
                } else if (headerBytes[4] == 'R' && headerBytes[5] == 'A') {
                    SDL_Log("[RSD_TRUTH] #%u *** MISMATCH! File is RadicalAdpcm but game says %s ***",
                            s_fileOpenCount, gameEncName);
                } else if (headerBytes[4] == 'P' && headerBytes[7] == 'B') {
                    SDL_Log("[RSD_TRUTH] #%u *** MISMATCH! File is PCM_BIGENDIAN but game says %s ***",
                            s_fileOpenCount, gameEncName);
                }
            }
        }
#endif
        
        if ( m_refIRadSoundHalAudioFormat == NULL )
        {
            rDebugPrintf(
                "radSoundRsdFileDataSource: WARNING reading header for: [%s], extra seek incurred\n",
                fileName );
               
		    //
		    // The file was opened correctly.  We can read in the
		    // header and find out what kind of a file this is.
		    //

            unsigned int initialPlaybackPos = m_StateInfo.m_InitializedInfo.m_InitialPlaybackPos;
            IRadSoundHalAudioFormat::SizeType initialPlaybackPosUnits
                = m_StateInfo.m_InitializedInfo.m_InitalPlaybackPosUnits;
            
		    m_StateInfo.m_State = StateInfo::READING_HEADER;
            m_StateInfo.m_ReadingHeaderInfo.m_InitialPlaybackPos = initialPlaybackPos;
            m_StateInfo.m_ReadingHeaderInfo.m_InitalPlaybackPosUnits = initialPlaybackPosUnits;
            
		    // Prepare a temporary structure to hold the header info

		    m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader =
			    static_cast< radSoundHalFileHeader * >(
				    ::radMemoryAllocAligned(
					    RADMEMORY_ALLOC_TEMP,
					    ::radMemoryRoundUp( sizeof( radSoundHalFileHeader ), radFileMaxSectorSize ),
					    radFileOptimalMemoryAlignment ) );

            ::radMemoryMonitorIdentifyAllocation(
                m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader,
                radSoundDebugChannel,
                "radSoundRsdFileDataSource::m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader" );                    

		    // Set the read position to the start of the file and
		    // read in the header
		    

            Log( true, this->fileName, sizeof( radSoundHalFileHeader ) );
    		    
		    m_refIRadFile->SetPositionAsync( 0 );
		    m_refIRadFile->ReadAsync(
			    m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader,
			    sizeof( radSoundHalFileHeader ) );
		    
		    m_refIRadFile->AddCompletionCallback( this, NULL );
        }
        else
        {
            // We already know the format (was passed in from client), just
            // go directly to setting things up

            InitFile( );
        }
	}
	else
	{
		rTunePrintf( "Failed to open file: [%s]\n", m_refIRadFile->GetFilename( ) );

		//
		// The file system didn't find our file.  Say that the
		// size of the file is zero and move into the idle state.
		// We don't need to fail because of this problem.
		//

		m_StateInfo.m_State = StateInfo::FILE_ERROR;
		m_FramesLeftInFile = 0;
		m_refIRadFile = NULL;
	}
}

void radSoundRsdFileDataSource::_StateReadingHeader( void )
{
	rAssert(  m_refIRadFile->IsOpen( ) );  // If the file isn't open, we shouldn't be here

#ifdef RAD_TVOS
    // Log the RAW encoding string BEFORE any conversion - this is the ground truth
    static unsigned int s_rsdHeaderCount = 0;
    s_rsdHeaderCount++;
    
    radSoundHalFileHeader* hdr = m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader;
    char encStr[5] = {hdr->m_SoundDataType[0], hdr->m_SoundDataType[1], 
                      hdr->m_SoundDataType[2], hdr->m_SoundDataType[3], 0};
    
    // Log first 30 + every 50th
    if (s_rsdHeaderCount <= 30 || (s_rsdHeaderCount % 50) == 0) {
        SDL_Log("[RSD_HEADER] #%u file='%s' rawEnc='%s' (0x%02x%02x%02x%02x) ch=%u bits=%u rate=%u",
                s_rsdHeaderCount, fileName,
                encStr,
                (unsigned char)hdr->m_SoundDataType[0],
                (unsigned char)hdr->m_SoundDataType[1],
                (unsigned char)hdr->m_SoundDataType[2],
                (unsigned char)hdr->m_SoundDataType[3],
                hdr->m_Channels, hdr->m_BitResolution, hdr->m_SamplingRate);
        
        // Flag suspicious encodings
        if (hdr->m_SoundDataType[0] == 'G' && hdr->m_SoundDataType[1] == 'A') {
            SDL_Log("[RSD_HEADER] #%u WARNING: GCNADPCM encoding detected - NEEDS DECODE ON TVOS!",
                    s_rsdHeaderCount);
        }
        if (hdr->m_SoundDataType[0] == 'P' && hdr->m_SoundDataType[3] == 'B') {
            SDL_Log("[RSD_HEADER] #%u WARNING: PCM Big-Endian detected - NEEDS BYTE-SWAP ON TVOS!",
                    s_rsdHeaderCount);
        }
    }
#endif

	// Get the data under control for strange platforms

	m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader->ConvertToPlatformEndian( );

	// Create a new audio format object

	m_refIRadSoundHalAudioFormat = ::radSoundHalAudioFormatCreate( GetThisAllocator( ) );

	m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader->InitializeAudioFormat(
		m_refIRadSoundHalAudioFormat, GetThisAllocator( ) );
	
	// Release the tempory memory

	::radMemoryFreeAligned( RADMEMORY_ALLOC_TEMP, m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader );
	m_StateInfo.m_ReadingHeaderInfo.m_pRadSoundHalFileHeader = NULL;

    InitFile( );
}

void radSoundRsdFileDataSource::InitFile( void )
{
	// Figure out where to start reading in the file

	unsigned int initialPlaybackOffsetInBytes =
	    m_refIRadSoundHalAudioFormat->ConvertSizeType(
		    IRadSoundHalAudioFormat::Bytes,
		    m_StateInfo.m_ReadingHeaderInfo.m_InitialPlaybackPos,
		    m_StateInfo.m_ReadingHeaderInfo.m_InitalPlaybackPosUnits );

	// Ensure that the offset is a multiple of the framesize

	initialPlaybackOffsetInBytes = m_refIRadSoundHalAudioFormat->FramesToBytes(
		m_refIRadSoundHalAudioFormat->BytesToFrames( initialPlaybackOffsetInBytes ) );

	// Calculate the amount of data contained in the file

	m_FramesLeftInFile = m_refIRadSoundHalAudioFormat->BytesToFrames(
		m_refIRadFile->GetSize( ) - RSD_FILE_DATA_OFFSET );

	rAssert( m_refIRadSoundHalAudioFormat->BytesToFrames( initialPlaybackOffsetInBytes ) < m_FramesLeftInFile );
	m_FramesLeftInFile -= m_refIRadSoundHalAudioFormat->BytesToFrames( initialPlaybackOffsetInBytes );

	//
	// An important assertion: The filesize needs to be a multiple of the framesize
	//
	unsigned int fileDataSize = m_refIRadFile->GetSize( ) - RSD_FILE_DATA_OFFSET - initialPlaybackOffsetInBytes;
	unsigned int roundedBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( m_FramesLeftInFile );
	rAssertMsg( fileDataSize == roundedBytes, "File Data is not block aligned (corrupted )" );

	// Move into the idle state before possibly getting called back

	m_StateInfo.m_State = StateInfo::IDLE;

	// Skip to the data and include the clients requested start position

	m_refIRadFile->SetPositionAsync( RSD_FILE_DATA_OFFSET + initialPlaybackOffsetInBytes );

    // Bit of a hack here, service the sound system so streamers pool the IsInitialized()
    // flag right now.

    ::radSoundHalSystemGet( )->Service( );
}

void radSoundRsdFileDataSource::_StateIdle( void )
{
}

void radSoundRsdFileDataSource::_StateReadingData( void )
{
	// Update our position info and callback the client

	m_FramesLeftInFile -= m_StateInfo.m_ReadingDataInfo.m_ReadSizeInFrames;
	m_StateInfo.m_State = StateInfo::IDLE;

	ref< IRadSoundHalDataSourceCallback > xIRadSoundHalDataSourceCallback = m_refIRadSoundHalDataSourceCallback;
	m_refIRadSoundHalDataSourceCallback = NULL;

#ifdef RAD_TVOS
    // Log file read completion with actual PCM data for mono streams
    static unsigned int s_fileReadCount = 0;
    s_fileReadCount++;
    
    unsigned int channels = m_refIRadSoundHalAudioFormat->GetNumberOfChannels();
    IRadSoundHalAudioFormat::Encoding enc = m_refIRadSoundHalAudioFormat->GetEncoding();
    
    // Always log mono stream file reads to trace corruption - LOG RAW FILE DATA
    if (channels == 1 && m_StateInfo.m_ReadingDataInfo.m_ReadSizeInFrames > 0) {
        int16_t* samples = (int16_t*)m_StateInfo.m_ReadingDataInfo.m_pReadBuffer;
        unsigned int numSamples = m_StateInfo.m_ReadingDataInfo.m_ReadSizeInFrames;
        
        // Also log raw bytes to check endianness
        unsigned char* rawBytes = (unsigned char*)m_StateInfo.m_ReadingDataInfo.m_pReadBuffer;
        
        SDL_Log("[FILE_RAW] #%u file='%s' frames=%u enc=%d ch=%u rawBytes=[%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x] samples=[%d,%d,%d,%d]",
                s_fileReadCount, fileName, m_StateInfo.m_ReadingDataInfo.m_ReadSizeInFrames,
                (int)enc, channels,
                rawBytes[0], rawBytes[1], rawBytes[2], rawBytes[3],
                rawBytes[4], rawBytes[5], rawBytes[6], rawBytes[7],
                samples[0], samples[1], samples[2], samples[3]);
    } else if (s_fileReadCount <= 20 || (s_fileReadCount % 100) == 0) {
        SDL_Log("[FILE_READ] #%u Complete: file='%s' frames=%u enc=%d ch=%u",
                s_fileReadCount, fileName, m_StateInfo.m_ReadingDataInfo.m_ReadSizeInFrames,
                (int)enc, channels);
    }
#endif
        
	xIRadSoundHalDataSourceCallback->OnDataSourceFramesLoaded( m_StateInfo.m_ReadingDataInfo.m_ReadSizeInFrames );
}

IRadSoundRsdFileDataSource * radSoundRsdFileDataSourceCreate( radMemoryAllocator allocator )
{
	return new ( "radSoundRsdFileDataSource", allocator ) radSoundRsdFileDataSource( );
}
