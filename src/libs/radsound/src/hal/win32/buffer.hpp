//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef BUFFER_HPP
#define BUFFER_HPP

//============================================================================
// Include Files
//============================================================================
#include <radlinkedclass.hpp>

#include <radsoundobject.hpp>
#include "radsoundwin.hpp"

//============================================================================
// Component: IRadSoundBufferWin
//============================================================================

class radSoundHalBufferWin
    :
    public IRadSoundHalBuffer,
    public IRadSoundHalBufferLoadCallback,
	public radLinkedClass< radSoundHalBufferWin >,
	public radSoundObject
{
	public:

		// IRadSoundHalBuffer

		virtual void Initialize(
			IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat,
			IRadMemoryObject * pIRadMemoryObject,
			unsigned int sizeInFrames,
			bool looping,
            bool streaming );

		virtual IRadSoundHalAudioFormat * GetFormat( void );

		virtual unsigned int GetSizeInFrames( void );
		virtual IRadMemoryObject * GetMemoryObject( void );
		virtual bool IsLooping( void );   

		virtual void LoadAsync(
			IRadSoundHalDataSource * pIRadSoundHalDataSource,
			unsigned int bufferStartInFrames,
			unsigned int numberOfFrames,
			IRadSoundHalBufferLoadCallback * pIRadSoundHalBufferLoadCallback );

		virtual void ClearAsync( 
			unsigned int startPositionInFrames,
			unsigned int numberOfFrames,
			IRadSoundHalBufferClearCallback * pIRadSoundHalBufferClearCallback );

        virtual void CancelAsyncOperations( void );

        virtual unsigned int GetMinTransferSize( IRadSoundHalAudioFormat::SizeType sizeType );

        virtual void ReSetAudioFormat( IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat ) { };

        // IRadSoundHalBufferLoadCallback

        virtual void OnBufferLoadComplete( unsigned int dataSourceFrames );

		// Internal

		radSoundHalBufferWin( void );


		IMPLEMENT_REFCOUNTED( "radSoundHalBufferWin" )

		unsigned int GetSizeInBytes( void );
        bool IsStreaming( void );
		ALuint GetBuffer( void );

	private:

		virtual ~radSoundHalBufferWin( void );

		unsigned int m_SizeInFrames;
        unsigned int m_LoadStartInBytes;
        void * m_pLockedLoadBuffer;
        unsigned long m_LockedLoadBytes;

		bool m_Looping;
        bool m_Streaming;
#ifdef RAD_TVOS
        bool m_ForcedStereo;  // True if mono streaming buffer was converted to stereo for Apple OpenAL compatibility
        ALuint m_AttachedSource;  // Source this buffer is attached to
        
        // Buffer queuing for streaming (fixes cutoff/repeat issues)
        static const unsigned int kStreamBufferPoolSize = 6;  // 6 buffers for safety margin under CPU spikes
        ALuint m_StreamBufferPool[kStreamBufferPoolSize];  // Pool of buffers for queuing
        bool m_StreamBufferFree[kStreamBufferPoolSize];    // Which buffers are available
        bool m_UseBufferQueuing;  // True if using queue-based streaming
        void* m_LastDataSourcePtr;  // Track data source identity for stream reset
        unsigned int m_QueuedCount;  // Number of buffers currently queued
        
    public:
        void SetAttachedSource(ALuint source) { m_AttachedSource = source; }
        ALuint GetAttachedSource() const { return m_AttachedSource; }
        bool UsesBufferQueuing() const { return m_UseBufferQueuing; }
        void UnqueueProcessedBuffers();  // Reclaim finished buffers
        void FlushBufferQueue();  // Flush all queued buffers (for new stream)
        void SetLastDataSource(void* ptr) { m_LastDataSourcePtr = ptr; }
        void* GetLastDataSource() const { return m_LastDataSourcePtr; }
    private:
        ALuint GetFreeStreamBuffer();  // Get a buffer from the pool
        void ReturnStreamBuffer(ALuint buffer);  // Return buffer to pool
#endif
		ALuint m_Buffer;

		ref< IRadSoundHalAudioFormat >	m_refIRadSoundHalAudioFormat;
		ref< IRadMemoryObject >			m_refIRadMemoryObject;
        ref< IRadSoundHalBufferLoadCallback > m_refIRadSoundHalBufferLoadCallback;
};

#endif
