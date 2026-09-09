#include <sound/soundrenderer/soundnucleus.hpp>
#include <main/commandlineoptions.h>
#include <memory/srrmemory.h>
#include <radmusic/radmusic.hpp>
#include <memory/srrmemory.h>

#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
#include <radtime.hpp>
#include <sound/diagnostics/audioloaddiag.hpp>
#include <sound/diagnostics/audioloaddiag_bridge.hpp>
#include <diagnostics/tvosdiagnostics.h>
#include <string.h>
#include <cstdint>
#endif

namespace Sound
{

const Encoding gPcmEncoding     = { IRadSoundHalAudioFormat::PCM, 1, 1 };
const Encoding gPcmBEncoding    = { IRadSoundHalAudioFormat::PCM_BIGENDIAN, 1, 1 };
const Encoding gVagEncoding     = { IRadSoundHalAudioFormat::VAG, 2, 7 };
const Encoding gGcAdpcmEncoding = { IRadSoundHalAudioFormat::GCNADPCM, 2, 7 };
const Encoding gRadAdpcmEncoding = { IRadSoundHalAudioFormat::RadicalAdpcm, 5, 16 };
const Encoding gXAdpcmEncoding = { IRadSoundHalAudioFormat::XBOXADPCM, 36, 128 };

const unsigned int NUM_AUX_SENDS = 1;

const unsigned int MUSIC_NUM_STREAM_PLAYERS = 4;
const unsigned int MUSIC_NUM_CLIP_PLAYERS = 2;
const unsigned int MUSIC_NUM_CHANNELS = 2;
const unsigned int MUSIC_SAMPLING_RATE = 24000;

const unsigned int TOTAL_PS2_FREE_UNCOMPRESSED_CLIP_BYTES = ( 1624 * 1024 * 7 ) / 2;

#if defined RAD_GAMECUBE

    const int ARAM_SILENT_BUFFER_SIZE = 1280;
    const int ARAM_USER_START = 0x4000;
    const int ARAM_RESERVED_BINK_MEMORY = 0x25800;
    const int GAMECUBE_SOUND_MEMORY_AVAILABLE =
        ( 1024 * 1024 * 10 ) - ARAM_SILENT_BUFFER_SIZE - ARAM_USER_START - ARAM_RESERVED_BINK_MEMORY;

    const int PLAYBACK_RATE = 32000;
    
    AudioFormat gCompressedStreamAudioFormat =   { 1, & gGcAdpcmEncoding, 24000 };
    AudioFormat gUnCompressedStreamAudioFormat = { 1, & gPcmBEncoding, 24000 };
    AudioFormat gClipFileAudioFormat =     { 1, & gPcmBEncoding, 24000 };
    AudioFormat gMusicAudioFormat =        { 2, & gPcmBEncoding, 24000 };
        
    const unsigned int STREAM_BUFFER_SIZE_MS = 6000;      
    const bool STREAM_USE_BUFFERED_DATA_SOURCES = false;
    const unsigned int STREAM_BUFFERED_DATA_SOURCE_SIZE_MS = 0;   
    const radMemorySpace STREAM_BUFFERED_DATA_SOURCE_MEMORY_SPACE = radMemorySpace_Local;
    
    const unsigned int CLIP_BUFFERED_DATA_SOURCE_SIZE_MS = 0;
    const radMemorySpace CLIP_BUFFERED_DATA_SOURCE_MEMORY_SPACE = radMemorySpace_Local; 
    
#elif defined RAD_XBOX || defined RAD_WIN32 || defined RAD_TVOS
    
    const int PLAYBACK_RATE = 0;

#if defined RAD_XBOX && defined PAL
    AudioFormat gCompressedStreamAudioFormat =   { 1, & gXAdpcmEncoding, 24000 };
#else
    AudioFormat gCompressedStreamAudioFormat =   { 1, & gPcmEncoding, 24000 };
#endif
    AudioFormat gUnCompressedStreamAudioFormat = { 1, & gPcmEncoding, 24000 };
    AudioFormat gClipFileAudioFormat =     { 1, & gPcmEncoding, 24000 };
    AudioFormat gMusicAudioFormat =        { 2, & gPcmEncoding, 24000 };    
    
    const unsigned int STREAM_BUFFER_SIZE_MS = 6000;    
    const bool STREAM_USE_BUFFERED_DATA_SOURCES = false;
    const unsigned int STREAM_BUFFERED_DATA_SOURCE_SIZE_MS = 0;
    const radMemorySpace STREAM_BUFFERED_DATA_SOURCE_MEMORY_SPACE = radMemorySpace_Local;  
    
    const unsigned int CLIP_BUFFERED_DATA_SOURCE_SIZE_MS = 0;
    const radMemorySpace CLIP_BUFFERED_DATA_SOURCE_MEMORY_SPACE = radMemorySpace_Local; 

             
#elif defined RAD_PS2

    const int PLAYBACK_RATE = 0;
    
    AudioFormat gCompressedStreamAudioFormat =   { 1, & gVagEncoding, 24000 };
    AudioFormat gUnCompressedStreamAudioFormat = { 1, & gVagEncoding, 24000 };
    AudioFormat gClipFileAudioFormat =     { 1, & gVagEncoding, 24000 };
    AudioFormat gMusicAudioFormat =        { 2, & gVagEncoding, 24000 };    
        
    const unsigned int STREAM_BUFFER_SIZE_MS = 1000;
    const bool STREAM_USE_BUFFERED_DATA_SOURCES = true;
    const unsigned int STREAM_BUFFERED_DATA_SOURCE_SIZE_MS = 4100;
    const radMemorySpace STREAM_BUFFERED_DATA_SOURCE_MEMORY_SPACE = radMemorySpace_Iop; 

    const unsigned int CLIP_BUFFERED_DATA_SOURCE_SIZE_MS = 5000;
    const radMemorySpace CLIP_BUFFERED_DATA_SOURCE_MEMORY_SPACE = radMemorySpace_Ee;  
              
#endif   

enum ClipLoadState
{
    ClipLoadState_Idle,
    ClipLoadState_InitFile,
    ClipLoadState_FillingBuffer,
    ClipLoadState_LoadingClip,
    ClipLoadState_Done
};
    
    
struct ClipLoadInfo
{
    IRadSoundRsdFileDataSource * pFds;
    IRadSoundClip * pClip;
    IRadSoundBufferedDataSource * pBds;
    ClipLoadState state;
    bool looping;
#ifdef RAD_TVOS
    IRadSoundHalDataSource * pDecodedDs;  // Holds decoded ADPCM stream if needed
#endif
#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
    char diagClipPath[512];
    radTime64 diagLoadStartUs;
    radTime64 diagRsdReadyUs;
    radTime64 diagFillingBufferEnterUs;
    radTime64 diagLoadingClipEnterUs;
    radTime64 diagClipDoneUs;
#endif
} gClipLoadInfo;

#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
static radTime64 s_clipDiagLastIdleUs = 0;
static bool s_clipDiagHaveIdle = false;
#endif

StreamerResources gStreamers[ SOUND_NUM_STREAM_PLAYERS ] =
{
    { NULL, NULL, NULL, & gCompressedStreamAudioFormat,   false, 0 },
    { NULL, NULL, NULL, & gCompressedStreamAudioFormat,   false, 0  },
    { NULL, NULL, NULL, & gCompressedStreamAudioFormat,   false, 0  },
    { NULL, NULL, NULL, & gUnCompressedStreamAudioFormat, false, 0  },
    { NULL, NULL, NULL, & gUnCompressedStreamAudioFormat, false, 0  },
    { NULL, NULL, NULL, & gUnCompressedStreamAudioFormat, false, 0  },
    { NULL, NULL, NULL, & gUnCompressedStreamAudioFormat, false, 0  },
    { NULL, NULL, NULL, & gUnCompressedStreamAudioFormat, false, 0  },
#ifdef RAD_XBOX
    { NULL, NULL, NULL, & gUnCompressedStreamAudioFormat, false, 0  },
#endif
    { NULL, NULL, NULL, & gUnCompressedStreamAudioFormat, false, 0  }
};                                  


void CreateAudioFormat( AudioFormat * pAf, radMemoryAllocator alloc )
{
    pAf->m_pAudioFormat = ::radSoundHalAudioFormatCreate( alloc );
    pAf->m_pAudioFormat->AddRef( );
        
    pAf->m_pAudioFormat->Initialize(
        pAf->m_pEncoding->m_Encoding,
        NULL,
        pAf->m_SamplingRate,
        pAf->m_Channels,
        16 );
}

void DestroyAudioFormat( AudioFormat * pAf )
{
    pAf->m_pAudioFormat->Release( );
    pAf->m_pAudioFormat = NULL;
}

void CreateStreamerResources( StreamerResources * pSi, radMemoryAllocator alloc )
{
    pSi->m_pStreamPlayer = ::radSoundStreamPlayerCreate( alloc );
    pSi->m_pStreamPlayer->AddRef( );

    pSi->m_pStreamPlayer->Initialize(
        pSi->m_pAudioFormat->m_pAudioFormat,
        STREAM_BUFFER_SIZE_MS,
        IRadSoundHalAudioFormat::Milliseconds,
        ::radSoundHalSystemGet( )->GetRootMemoryRegion( ),
        "Sound Stream Player" );
    
    pSi->m_pStitchedDataSource = ::radSoundStitchedDataSourceCreate( alloc );
    pSi->m_pStitchedDataSource->AddRef( );
    pSi->m_pStitchedDataSource->InitializeFromAudioFormat( pSi->m_pAudioFormat->m_pAudioFormat );

    if ( STREAM_USE_BUFFERED_DATA_SOURCES && ( false == CommandLineOptions::Get( CLO_FIREWIRE ) ) )
    {                                
        pSi->m_pBufferedDataSource = radSoundBufferedDataSourceCreate( alloc );
        pSi->m_pBufferedDataSource->AddRef( );

        pSi->m_pBufferedDataSource->Initialize(
            STREAM_BUFFERED_DATA_SOURCE_MEMORY_SPACE,
            radMemorySpaceGetAllocator( STREAM_BUFFERED_DATA_SOURCE_MEMORY_SPACE, RADMEMORY_ALLOC_DEFAULT ), // This is IOP Memory
            STREAM_BUFFERED_DATA_SOURCE_SIZE_MS,
            IRadSoundHalAudioFormat::Milliseconds,
            pSi->m_pAudioFormat->m_pAudioFormat,
            "DaSound Buffered DataSource" );
            
    }
    else
    {
        pSi->m_pBufferedDataSource = NULL;
    }
}

void DestroyStreamerResources( StreamerResources* pSi )
{
    // Its a stream player
    pSi->m_pStreamPlayer->Stop( );
    pSi->m_pStreamPlayer->SetDataSource( NULL );

    if( pSi->m_pBufferedDataSource )
    {
        pSi->m_pBufferedDataSource->Release( );
        pSi->m_pBufferedDataSource = NULL;
    }
    
    // Release the player
    pSi->m_pStreamPlayer->Release( );
    pSi->m_pStreamPlayer = NULL;
    

    pSi->m_pStitchedDataSource->Release( );
    pSi->m_pStitchedDataSource = NULL;
}

unsigned int CalculateStreamerSize( unsigned int ms, AudioFormat * pAf )
{
	unsigned int sizeInFrames = pAf->m_pAudioFormat->MillisecondsToFrames( ms );

    unsigned int optimalFrameMultiple =
        pAf->m_pAudioFormat->BytesToFrames( radSoundHalDataSourceReadMultipleGet( ) );

    // Our buffer must be at least as big as two optimal reads.

    sizeInFrames = radMemoryRoundUp( sizeInFrames, optimalFrameMultiple * 2 );

    sizeInFrames = ::radSoundHalBufferCalculateMemorySize( IRadSoundHalAudioFormat::Frames,
        sizeInFrames, IRadSoundHalAudioFormat::Frames, pAf->m_pAudioFormat );

	unsigned int sizeInBytes = pAf->m_pAudioFormat->FramesToBytes( sizeInFrames );
	
	return sizeInBytes;
}

void SoundNucleusInitialize( radMemoryAllocator alloc )
{
    
#if defined( RAD_PS2 ) || defined( RAD_GAMECUBE ) || defined( RAD_WIN32 ) || defined( RAD_TVOS )
    ::radSoundHalSystemInitialize( alloc );
#else
    ::radSoundHalSystemInitialize( GMA_XBOX_SOUND_MEMORY );
#endif

    CreateAudioFormat( & gCompressedStreamAudioFormat, alloc );
    CreateAudioFormat( & gUnCompressedStreamAudioFormat, alloc );
    CreateAudioFormat( & gClipFileAudioFormat, alloc );
    CreateAudioFormat( & gMusicAudioFormat, alloc );
    

    
    //
    // ESAN TODO: Investigate the magic number 150 below...
    //
    
    unsigned int totalStreamSoundMemoryNeeded = 0;
    unsigned int totalStreamBufferMemoryNeeded = 0;
    unsigned int totalClipBufferMemoryNeeded = 0;

    if ( CLIP_BUFFERED_DATA_SOURCE_SIZE_MS > 0 )
    {
        gClipLoadInfo.pBds = radSoundBufferedDataSourceCreate( GMA_AUDIO_PERSISTENT );
        gClipLoadInfo.pBds->AddRef( );
        gClipLoadInfo.pBds->Initialize(
            CLIP_BUFFERED_DATA_SOURCE_MEMORY_SPACE,
            radMemorySpaceGetAllocator(
                CLIP_BUFFERED_DATA_SOURCE_MEMORY_SPACE,
                GMA_AUDIO_PERSISTENT ),
            CLIP_BUFFERED_DATA_SOURCE_SIZE_MS,
            IRadSoundHalAudioFormat::Milliseconds,
            SoundNucleusGetClipFileAudioFormat( ),
            "Clip Bds" );
            
        gClipLoadInfo.pBds->SetLowWaterMark( 1.0f );            
            
        totalClipBufferMemoryNeeded =
            SoundNucleusGetClipFileAudioFormat( )->MillisecondsToBytes(
                CLIP_BUFFERED_DATA_SOURCE_SIZE_MS );               
    }
    
    
    for( unsigned int i = 0; i < SOUND_NUM_STREAM_PLAYERS; i ++ )
    {
        StreamerResources* pSi = gStreamers + i;
        
        unsigned int streamSoundMemoryNeeded = CalculateStreamerSize(
            STREAM_BUFFER_SIZE_MS,
            pSi->m_pAudioFormat );

        totalStreamSoundMemoryNeeded += streamSoundMemoryNeeded;
            
        unsigned int streamBufferMemoryNeeded = CalculateStreamerSize(
            STREAM_BUFFERED_DATA_SOURCE_SIZE_MS,
            pSi->m_pAudioFormat );
                                             
        totalStreamBufferMemoryNeeded += streamBufferMemoryNeeded;
                
        rTunePrintf( "AUDIO: Predicting SOUND streamer will allocate: Sound: [0x%x] Buffer:[0x%x]\n",
            streamSoundMemoryNeeded,
            streamBufferMemoryNeeded );
    }
    
    for( unsigned int i = 0; i < MUSIC_NUM_STREAM_PLAYERS; i ++ )
    {
        unsigned int streamSoundMemoryNeeded = CalculateStreamerSize(
            STREAM_BUFFER_SIZE_MS,
            & gMusicAudioFormat );

        totalStreamSoundMemoryNeeded += streamSoundMemoryNeeded;
                    
        unsigned int streamBufferMemoryNeeded = CalculateStreamerSize(
            STREAM_BUFFERED_DATA_SOURCE_SIZE_MS,
            & gMusicAudioFormat );

        totalStreamBufferMemoryNeeded += streamBufferMemoryNeeded;
                        
        rTunePrintf( "AUDIO: Predicting MUSIC streamer will allocate: Sound: [0x%x] Buffer:[0x%x]\n",
            streamSoundMemoryNeeded,
            streamBufferMemoryNeeded );                              
            
    }

    unsigned int totalClipMemoryNeeded =
        ( TOTAL_PS2_FREE_UNCOMPRESSED_CLIP_BYTES * gClipFileAudioFormat.m_pEncoding->m_CompressionNumerator ) /
            gClipFileAudioFormat.m_pEncoding->m_CompressionDenominator;
    
    rTunePrintf(
        "AUDIO: Sound Memory totals: Stream: [0x%x] Clip: [0x%x], Total: [0x%x]\n",
        totalStreamSoundMemoryNeeded,
        totalClipMemoryNeeded,
        totalStreamSoundMemoryNeeded + totalClipMemoryNeeded );

    #ifdef RAD_GAMECUBE
        rAssert(
            ( totalStreamSoundMemoryNeeded + totalClipMemoryNeeded ) <=
                GAMECUBE_SOUND_MEMORY_AVAILABLE );
    #endif

    rTunePrintf(
        "AUDIO: Sound Buffered Stream Memory Stream: [0x%x], Clip: [0x%x] Total:\n",
        totalStreamBufferMemoryNeeded,
        totalClipBufferMemoryNeeded,
        totalStreamBufferMemoryNeeded + totalClipBufferMemoryNeeded );

    IRadSoundHalSystem::SystemDescription desc;
    
    desc.m_MaxRootAllocations  = 170;
    desc.m_NumAuxSends         = NUM_AUX_SENDS;
#if defined( RAD_WIN32 ) || defined( RAD_TVOS )
    desc.m_SamplingRate        = 48000;
#endif
    
#ifndef RAD_PS2
    desc.m_ReservedSoundMemory = ( totalStreamSoundMemoryNeeded + totalClipMemoryNeeded ) * ( sizeof( void* ) / 4 );
#endif

#ifdef RAD_GAMECUBE
    desc.m_EffectsAllocator = GMA_AUDIO_PERSISTENT;
#endif

    ::radSoundHalSystemGet( )->Initialize( desc );    
    //::radSoundHalSystemGet( )->SetOutputMode( radSoundOutputMode_Surround );  
                     
    for( unsigned int i = 0; i < SOUND_NUM_STREAM_PLAYERS; i ++ )
    {
        gStreamers[ i ].index = i;
        CreateStreamerResources( gStreamers + i, alloc ); 
    }

    radmusic::stream_graph_description sgDesc[ MUSIC_NUM_STREAM_PLAYERS ];
            
    for( unsigned int i = 0; i < MUSIC_NUM_STREAM_PLAYERS; i ++ )
    {
        sgDesc[ i ].buffered_data_source_size_in_ms = STREAM_BUFFERED_DATA_SOURCE_SIZE_MS;
        sgDesc[ i ].buffered_data_source_space      = STREAM_BUFFERED_DATA_SOURCE_MEMORY_SPACE;     
        sgDesc[ i ].channels                        = MUSIC_NUM_CHANNELS;
        sgDesc[ i ].sampling_rate                   = MUSIC_SAMPLING_RATE;
        sgDesc[ i ].stream_buffer_size_in_ms        = STREAM_BUFFER_SIZE_MS;
        sgDesc[ i ].use_buffered_data_source        = CommandLineOptions::Get( CLO_FIREWIRE ) ? false : STREAM_USE_BUFFERED_DATA_SOURCES;
    }
        
    radmusic::initialize( sgDesc, MUSIC_NUM_STREAM_PLAYERS, MUSIC_NUM_CLIP_PLAYERS, GMA_MUSIC );
    radmusic::register_radload_loaders( );
    
}

void SoundNucleusTerminate( void )
{
    rAssert( ClipLoadState_Idle == gClipLoadInfo.state );
    
    if ( gClipLoadInfo.pBds != NULL )
    {
        gClipLoadInfo.pBds->Release( );
    }
    
    for( unsigned int i = 0; i < SOUND_NUM_STREAM_PLAYERS; i ++ )
    {
        DestroyStreamerResources( gStreamers + i );
    }
    
    DestroyAudioFormat( & gCompressedStreamAudioFormat );
    DestroyAudioFormat( & gUnCompressedStreamAudioFormat );
    DestroyAudioFormat( & gClipFileAudioFormat );
    DestroyAudioFormat( & gMusicAudioFormat );

    radmusic::terminate( );

    // Shutdown our related systems
    ::radSoundHalSystemTerminate( );        
    
}

IRadSoundHalAudioFormat * SoundNucleusGetStreamFileAudioFormat( void )
{
#if defined( RAD_GAMECUBE ) || ( defined( RAD_XBOX ) && defined( PAL ) ) || defined( RAD_WIN32 ) || defined( RAD_TVOS )
        return NULL;
    #else
        return gUnCompressedStreamAudioFormat.m_pAudioFormat;
    #endif

}

IRadSoundHalAudioFormat * SoundNucleusGetClipFileAudioFormat( void )
{
    return gClipFileAudioFormat.m_pAudioFormat;
}

StreamerResources* SoundNucleusCaptureStreamerResources( IRadSoundHalAudioFormat * pAf )
{
    for( unsigned int i = 0; i < SOUND_NUM_STREAM_PLAYERS; i ++ )
    {
        StreamerResources * pSi =
            gStreamers + i;
         
        if ( ! pSi->m_IsCaptured )
        {
            if ( pSi->m_pAudioFormat->m_pAudioFormat->Matches( pAf ) )
            {
                pSi->m_IsCaptured = true;
                return pSi;
            }
        }
    }
    
    rTuneAssertMsg( false, "Out of streamers of desired format" );
    
    return NULL;
}

void SoundNucleusUnCaptureStreamerResources( StreamerResources * pSi )
{
    rAssert( gStreamers[ pSi->index ].m_IsCaptured == true );
    gStreamers[ pSi->index ].m_IsCaptured = false;
}

void SoundNucleusLoadClip( const char * pFileName, bool looping )
{
    rAssert( ClipLoadState_Idle == gClipLoadInfo.state );

#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
    radTime64 nowUs = radTimeGetMicroseconds64( );
    unsigned long long gapUs = 0;
    if ( s_clipDiagHaveIdle )
    {
        gapUs = static_cast<unsigned long long>( nowUs - s_clipDiagLastIdleUs );
        SRR2_AudioDiag_RecordClipSerialGapUs( static_cast<uint64_t>( gapUs ) );
    }
    memset( gClipLoadInfo.diagClipPath, 0, sizeof( gClipLoadInfo.diagClipPath ) );
    if ( pFileName != NULL )
    {
        strncpy( gClipLoadInfo.diagClipPath, pFileName, sizeof( gClipLoadInfo.diagClipPath ) - 1 );
    }
    gClipLoadInfo.diagLoadStartUs = nowUs;
    gClipLoadInfo.diagRsdReadyUs = 0;
    gClipLoadInfo.diagFillingBufferEnterUs = 0;
    gClipLoadInfo.diagLoadingClipEnterUs = 0;
    gClipLoadInfo.diagClipDoneUs = 0;
    SRR2::Diagnostics::Tracef(
        SRR2::Diagnostics::AUDIO_LOAD,
        "[CLIP_PHASE_NUCLEUS] phase=InitFile_enter path='%s' serial_gap_since_idle_us=%llu",
        gClipLoadInfo.diagClipPath,
        gapUs );
#endif
        
    gClipLoadInfo.pFds = radSoundRsdFileDataSourceCreate( GMA_AUDIO_PERSISTENT );
    gClipLoadInfo.pFds->AddRef( );     
    gClipLoadInfo.looping = looping;
             

    // Initialize the file data source iwth our file
    //
#ifdef RAD_TVOS
    // tvOS: Pass NULL to let RSD header determine actual encoding (PCM vs RADP)
    gClipLoadInfo.pFds->InitializeFromFileName(
        pFileName,
        false,
        0,
        IRadSoundHalAudioFormat::Frames,
        NULL );
#else
    gClipLoadInfo.pFds->InitializeFromFileName(
        pFileName,
        false,
        0,
        IRadSoundHalAudioFormat::Frames,
        SoundNucleusGetClipFileAudioFormat( ) );
#endif
        
    gClipLoadInfo.state = ClipLoadState_InitFile;

#ifdef RAD_TVOS
    // Start the async file open in the same update that queued the clip.
    SoundNucleusServiceClipLoad( );
#endif
}

bool SoundNucleusIsClipLoaded( void )
{
    rAssert( ClipLoadState_Idle != gClipLoadInfo.state );
 
    return ClipLoadState_Done == gClipLoadInfo.state;      
}

bool SoundNucleusIsClipLoadInProgress( void )
{
    return
        ( gClipLoadInfo.state != ClipLoadState_Idle ) &&
        ( gClipLoadInfo.state != ClipLoadState_Done );
}

void SoundNucleusFinishClipLoad( IRadSoundClip ** ppClip )
{
    rAssert( ClipLoadState_Done == gClipLoadInfo.state );
    
    *ppClip = gClipLoadInfo.pClip;
    gClipLoadInfo.pClip = NULL;
    
    gClipLoadInfo.pFds->Release( );
    gClipLoadInfo.pFds = NULL;
    
#ifdef RAD_TVOS
    if ( NULL != gClipLoadInfo.pDecodedDs )
    {
        gClipLoadInfo.pDecodedDs->Release( );
        gClipLoadInfo.pDecodedDs = NULL;
    }
#endif
    
    if ( NULL != gClipLoadInfo.pBds )
    {
        gClipLoadInfo.pBds->SetInputDataSource( 0 );
    }
    
    gClipLoadInfo.state = ClipLoadState_Idle;
#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
    {
        const radTime64 finUs = radTimeGetMicroseconds64( );
        const unsigned long long sinceStart = static_cast<unsigned long long>(
            finUs - gClipLoadInfo.diagLoadStartUs );
        const unsigned long long sinceClipDone =
            gClipLoadInfo.diagClipDoneUs != 0
                ? static_cast<unsigned long long>( finUs - gClipLoadInfo.diagClipDoneUs )
                : 0ULL;
        const char* lit = AudioLoadDiag::LookupLiteralPathForKeyHex( gClipLoadInfo.diagClipPath );
        SRR2::Diagnostics::Tracef(
            SRR2::Diagnostics::AUDIO_LOAD,
            "[CLIP_PHASE_NUCLEUS] phase=FinishClipLoad path='%s' literal_path=%s since_load_start_us=%llu "
            "LoadingClip_done_to_FinishClipLoad_us=%llu",
            gClipLoadInfo.diagClipPath,
            lit != NULL ? lit : "(key-only)",
            sinceStart,
            sinceClipDone );
    }
    s_clipDiagLastIdleUs = radTimeGetMicroseconds64( );
    s_clipDiagHaveIdle = true;
#endif
}

void SoundNucleusCancelClipLoad( void )
{
    rAssert( ClipLoadState_Idle != gClipLoadInfo.state );
    
    if( NULL != gClipLoadInfo.pClip )
    {
        gClipLoadInfo.pClip->Release( );
        gClipLoadInfo.pClip = NULL;
    }

    if( NULL != gClipLoadInfo.pFds )
    {
        gClipLoadInfo.pFds->Release( );
        gClipLoadInfo.pFds = NULL;
    }
    
#ifdef RAD_TVOS
    if ( NULL != gClipLoadInfo.pDecodedDs )
    {
        gClipLoadInfo.pDecodedDs->Release( );
        gClipLoadInfo.pDecodedDs = NULL;
    }
#endif
    
    if ( NULL != gClipLoadInfo.pBds )
    {
        gClipLoadInfo.pBds->SetInputDataSource( 0 );
    }
    
    gClipLoadInfo.state = ClipLoadState_Idle;
#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
    s_clipDiagLastIdleUs = radTimeGetMicroseconds64( );
    s_clipDiagHaveIdle = true;
#endif
      
}


void SoundNucleusServiceClipLoad( void )
{
    ClipLoadState oldState;
    
    do
    {
        oldState = gClipLoadInfo.state;
        
        switch ( gClipLoadInfo.state )
        {
            case ClipLoadState_Idle:
            {
                break;
            }
            case ClipLoadState_InitFile:
            {
                if ( IRadSoundHalDataSource::Initialized == gClipLoadInfo.pFds->GetState( ) )
                {
#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
                    if ( gClipLoadInfo.diagRsdReadyUs == 0 )
                    {
                        gClipLoadInfo.diagRsdReadyUs = radTimeGetMicroseconds64( );
                        const unsigned long long pollUs = static_cast<unsigned long long>(
                            gClipLoadInfo.diagRsdReadyUs - gClipLoadInfo.diagLoadStartUs );
                        SRR2::Diagnostics::Tracef(
                            SRR2::Diagnostics::AUDIO_LOAD,
                            "[CLIP_PHASE_NUCLEUS] phase=WaitForFileOpen_ReadHeader_RsdReady path='%s' "
                            "InitFile_poll_until_GetState_initialized_us=%llu",
                            gClipLoadInfo.diagClipPath,
                            pollUs );
                    }
#endif
#ifdef RAD_TVOS
                    // tvOS: Check actual encoding from RSD header and wrap with decode stream if needed
                    IRadSoundHalDataSource* pDataSource = gClipLoadInfo.pFds;
                    
                    if ( gClipLoadInfo.pFds->GetFormat()->GetEncoding() == IRadSoundHalAudioFormat::RadicalAdpcm )
                    {
                        // File is RADP encoded - create decode stream to convert to PCM
                        IRadSoundAdpcmDecodeStream* pDecodeStream = radSoundAdpcmDecodeStreamCreate( GMA_AUDIO_PERSISTENT );
                        pDecodeStream->AddRef();
                        pDecodeStream->Initialize( gClipLoadInfo.pFds );
                        gClipLoadInfo.pDecodedDs = pDecodeStream;
                        pDataSource = pDecodeStream;
                    }
                    
                    if ( gClipLoadInfo.pBds != NULL )
                    {
                        unsigned int ms =
                            gClipLoadInfo.pBds->GetFormat( )->FramesToMilliseconds(
                                pDataSource->GetRemainingFrames( ) );
                        
                        rTuneAssert( ms < CLIP_BUFFERED_DATA_SOURCE_SIZE_MS );                        
                        
                        gClipLoadInfo.pBds->SetInputDataSource( pDataSource );
                        
                        gClipLoadInfo.state = ClipLoadState_FillingBuffer;
#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
                        gClipLoadInfo.diagFillingBufferEnterUs = radTimeGetMicroseconds64( );
                        SRR2::Diagnostics::Tracef(
                            SRR2::Diagnostics::AUDIO_LOAD,
                            "[CLIP_PHASE_NUCLEUS] phase=FillingBuffer_enter path='%s' since_load_start_us=%llu",
                            gClipLoadInfo.diagClipPath,
                            static_cast<unsigned long long>(
                                gClipLoadInfo.diagFillingBufferEnterUs - gClipLoadInfo.diagLoadStartUs ) );
#endif
                    }
                    else
                    {
                        gClipLoadInfo.pClip = radSoundClipCreate( GMA_AUDIO_PERSISTENT );
                        gClipLoadInfo.pClip->AddRef( );
                        gClipLoadInfo.pClip->Initialize(
                            pDataSource,
                            radSoundHalSystemGet( )->GetRootMemoryRegion( ),
                            gClipLoadInfo.looping,       
                            "Clip" );
                            
                        gClipLoadInfo.state = ClipLoadState_LoadingClip;
#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
                        gClipLoadInfo.diagLoadingClipEnterUs = radTimeGetMicroseconds64( );
                        SRR2::Diagnostics::Tracef(
                            SRR2::Diagnostics::AUDIO_LOAD,
                            "[CLIP_PHASE_NUCLEUS] phase=LoadingClip_enter path='%s' since_load_start_us=%llu",
                            gClipLoadInfo.diagClipPath,
                            static_cast<unsigned long long>(
                                gClipLoadInfo.diagLoadingClipEnterUs - gClipLoadInfo.diagLoadStartUs ) );
#endif
                    }
#else
                    if ( gClipLoadInfo.pBds != NULL )
                    {
                        unsigned int ms =
                            gClipLoadInfo.pBds->GetFormat( )->FramesToMilliseconds(
                                gClipLoadInfo.pFds->GetRemainingFrames( ) );
                        
                        rTuneAssert( ms < CLIP_BUFFERED_DATA_SOURCE_SIZE_MS );                        
                        
                        gClipLoadInfo.pBds->SetInputDataSource( gClipLoadInfo.pFds );
                        
                        gClipLoadInfo.state = ClipLoadState_FillingBuffer;
                    }
                    else
                    {
                        gClipLoadInfo.pClip = radSoundClipCreate( GMA_AUDIO_PERSISTENT );
                        gClipLoadInfo.pClip->AddRef( );
                        gClipLoadInfo.pClip->Initialize(
                            gClipLoadInfo.pFds,
                            radSoundHalSystemGet( )->GetRootMemoryRegion( ),
                            gClipLoadInfo.looping,       
                            "Clip" );
                            
                        gClipLoadInfo.state = ClipLoadState_LoadingClip;
                    }
#endif
                }
                
                break;
            }
            case ClipLoadState_FillingBuffer:
            {
                if ( gClipLoadInfo.pBds->IsBufferFull( ) )
                {
                    gClipLoadInfo.pClip = radSoundClipCreate( GMA_AUDIO_PERSISTENT );
                    gClipLoadInfo.pClip->AddRef( );
                    gClipLoadInfo.pClip->Initialize(
                        gClipLoadInfo.pBds,
                        radSoundHalSystemGet( )->GetRootMemoryRegion( ),
                        gClipLoadInfo.looping,       
                        "Clip" );
                        
                    gClipLoadInfo.state = ClipLoadState_LoadingClip;
#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
                    gClipLoadInfo.diagLoadingClipEnterUs = radTimeGetMicroseconds64( );
                    SRR2::Diagnostics::Tracef(
                        SRR2::Diagnostics::AUDIO_LOAD,
                        "[CLIP_PHASE_NUCLEUS] phase=LoadingClip_enter path='%s' since_load_start_us=%llu",
                        gClipLoadInfo.diagClipPath,
                        static_cast<unsigned long long>(
                            gClipLoadInfo.diagLoadingClipEnterUs - gClipLoadInfo.diagLoadStartUs ) );
#endif
                }
                
                break;
            }
            case ClipLoadState_LoadingClip:
            {
                if ( IRadSoundClip::Initialized == gClipLoadInfo.pClip->GetState( ) )
                {
#if defined( RAD_TVOS ) && defined( RAD_TVOS_AUDIO_DIAGNOSTICS )
                    radTime64 endUs = radTimeGetMicroseconds64( );
                    gClipLoadInfo.diagClipDoneUs = endUs;
                    const unsigned long long wallUs = static_cast<unsigned long long>( endUs - gClipLoadInfo.diagLoadStartUs );
                    SRR2_AudioDiag_RecordClipWallUs( static_cast<uint64_t>( wallUs ) );
                    unsigned long long initPollUs = 0ULL;
                    if ( gClipLoadInfo.diagRsdReadyUs != 0 )
                    {
                        initPollUs = static_cast<unsigned long long>(
                            gClipLoadInfo.diagRsdReadyUs - gClipLoadInfo.diagLoadStartUs );
                    }
                    unsigned long long postRsdUs = 0ULL;
                    if ( gClipLoadInfo.diagRsdReadyUs != 0 && gClipLoadInfo.diagLoadingClipEnterUs != 0 )
                    {
                        postRsdUs = static_cast<unsigned long long>(
                            gClipLoadInfo.diagLoadingClipEnterUs - gClipLoadInfo.diagRsdReadyUs );
                    }
                    unsigned long long fillingUs = 0ULL;
                    if ( gClipLoadInfo.diagFillingBufferEnterUs != 0 && gClipLoadInfo.diagLoadingClipEnterUs != 0 )
                    {
                        fillingUs = static_cast<unsigned long long>(
                            gClipLoadInfo.diagLoadingClipEnterUs - gClipLoadInfo.diagFillingBufferEnterUs );
                    }
                    unsigned long long loadClipUs = 0ULL;
                    if ( gClipLoadInfo.diagLoadingClipEnterUs != 0 )
                    {
                        loadClipUs = static_cast<unsigned long long>( endUs - gClipLoadInfo.diagLoadingClipEnterUs );
                    }
                    const char* lit = AudioLoadDiag::LookupLiteralPathForKeyHex( gClipLoadInfo.diagClipPath );
                    SRR2::Diagnostics::Tracef(
                        SRR2::Diagnostics::AUDIO_LOAD,
                        "[CLIP_PHASE_NUCLEUS] phase=LoadingClip_done path='%s' literal_path=%s wall_us=%llu "
                        "InitFile_poll_us=%llu post_rsd_us=%llu FillingBuffer_us=%llu LoadingClip_us=%llu "
                        "(mono16/alBufferData see [AUDIO_TIMING] clip_mono16)",
                        gClipLoadInfo.diagClipPath,
                        lit != NULL ? lit : "(key-only)",
                        wallUs,
                        initPollUs,
                        postRsdUs,
                        fillingUs,
                        loadClipUs );
                    SRR2::Diagnostics::RecordAudioClipTiming(
                        gClipLoadInfo.diagClipPath,
                        static_cast<uint64_t>( wallUs ),
                        static_cast<uint64_t>( initPollUs ),
                        static_cast<uint64_t>( loadClipUs ) );
#endif
                    gClipLoadInfo.state = ClipLoadState_Done;
                }
                
                break;
            }
            case ClipLoadState_Done:
            {
                break;
            }
        }
     }
     while( oldState != gClipLoadInfo.state );
}

}
