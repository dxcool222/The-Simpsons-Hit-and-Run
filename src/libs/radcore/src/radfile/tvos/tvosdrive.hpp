//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#ifndef TVOSDRIVE_HPP
#define TVOSDRIVE_HPP

#include <filesystem>

#include "../common/drive.hpp"
#include "../common/drivethread.hpp"

void radTvosDriveFactory( radDrive** ppDrive, const char* driveSpec, radMemoryAllocator alloc );

class radTvosDrive : public radDrive
{
public:
    radTvosDrive( const char* driveSpec, radMemoryAllocator alloc );
    virtual ~radTvosDrive( void );

    void Lock( void );
    void Unlock( void );

    unsigned int GetCapabilities( void );
    const char* GetDriveName( void );

    CompletionStatus Initialize( void );

    CompletionStatus OpenFile( const char* fileName,
                               radFileOpenFlags flags,
                               bool writeAccess,
                               radFileHandle* pHandle,
                               unsigned int* pSize );

    CompletionStatus OpenSaveGame( const char* fileName,
                                   radFileOpenFlags flags,
                                   bool writeAccess,
                                   radMemcardInfo* memcardInfo,
                                   unsigned int maxSize,
                                   radFileHandle* pHandle,
                                   unsigned int* pSize );

    CompletionStatus CloseFile( radFileHandle handle, const char* fileName );

    CompletionStatus CommitFile( radFileHandle handle, const char* fileName );

    CompletionStatus ReadFile( radFileHandle handle,
                               const char* fileName,
                               IRadFile::BufferedReadState state,
                               unsigned int position,
                               void* pData,
                               unsigned int bytesToRead,
                               unsigned int* bytesRead,
                               radMemorySpace pDataSpace );

    CompletionStatus WriteFile( radFileHandle handle,
                                const char* fileName,
                                IRadFile::BufferedReadState state,
                                unsigned int position,
                                const void* pData,
                                unsigned int bytesToWrite,
                                unsigned int* bytesWritten,
                                unsigned int* size,
                                radMemorySpace pDataSpace );

    CompletionStatus CreateDir( const char* pName );
    CompletionStatus DestroyDir( const char* pName );
    CompletionStatus DestroyFile( const char* filename );

    CompletionStatus FindFirst( const char* searchSpec,
                                IRadDrive::DirectoryInfo* pDirectoryInfo,
                                radFileDirHandle* pHandle,
                                bool firstSearch );

    CompletionStatus FindNext( radFileDirHandle* pHandle, IRadDrive::DirectoryInfo* pDirectoryInfo );

    CompletionStatus FindClose( radFileDirHandle* pHandle );

private:
    void SetMediaInfo( void );

    bool IsAppDrive( void ) const;

    void MakeNativePath( const char* fileName, std::filesystem::path* outPath ) const;

    void TranslateDirInfo( IRadDrive::DirectoryInfo* pDirectoryInfo,
                           const std::filesystem::directory_entry& entry );

    bool FindNextMatching( radFileDirHandle* pHandle, IRadDrive::DirectoryInfo* pDirectoryInfo );

    unsigned int m_Capabilities;
    char m_DriveName[ radFileDrivenameMax + 1 ];

    std::filesystem::path m_Root;

    IRadThreadMutex* m_pMutex;
};

#endif
