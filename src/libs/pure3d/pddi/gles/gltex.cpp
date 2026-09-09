//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/gldisplay.hpp>
#include <pddi/gles/gltex.hpp>
#include <pddi/gles/glcon.hpp>
#include <pddi/gles/decompress.hpp>

#include <math.h>
#include <pddi/base/debug.hpp>
#include <radmemory.hpp>
#include <diagnostics/tvosdiagnostics.h>

#include <microprofile.h>

#ifdef RAD_TVOS
#if defined( RAD_TVOS_RENDER_DIAGNOSTICS )
#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
#else
    #include <SDL.h>
#endif
static unsigned int s_tvosTexCount = 0;
static unsigned int s_tvosVideoTexCount = 0;
static void TvosLogTexUpload( const char* path, int w, int h, GLenum internalFmt, GLenum fmt, GLenum type, GLuint texId = 0, bool isVideo = false, void* ptr = nullptr )
{
    s_tvosTexCount++;
    GLenum err = glGetError();
    SRR2::Diagnostics::RecordTextureUpload( path ? path : "upload", (uint32_t)w, (uint32_t)h, isVideo, (uint32_t)err );
    if ( isVideo )
    {
        s_tvosVideoTexCount++;
        // Only log first 3 video texture uploads to reduce spam
        if ( s_tvosVideoTexCount <= 3 )
        {
            SRR2::Diagnostics::Tracef( SRR2::Diagnostics::VIDEO,
                     "[VIDEO_TEX] #%u ptr=%p texID=%u %dx%d fmt=0x%x err=0x%x",
                     s_tvosVideoTexCount, ptr, texId, w, h, (unsigned)fmt, (unsigned)err );
        }
    }
    else if ( s_tvosTexCount <= 20 || ( s_tvosTexCount % 200 ) == 0 )
    {
        SRR2::Diagnostics::Tracef( SRR2::Diagnostics::RENDER,
                 "TVOS_GL Tex #%u: %s ptr=%p %dx%d internalFmt=0x%x fmt=0x%x type=0x%x err=0x%x",
                 s_tvosTexCount, path ? path : "upload", ptr, w, h,
                 (unsigned)internalFmt, (unsigned)fmt, (unsigned)type, (unsigned)err );
    }
}
#else
static void TvosLogTexUpload( const char*, int, int, GLenum, GLenum, GLenum, GLuint = 0, bool = false, void* = nullptr )
{
}
#endif
#endif

static inline GLenum PickPixelFormat(pddiPixelFormat format)
{
    switch (format)
    {
    case PDDI_PIXEL_RGB888: return GL_BGRA_EXT;
    case PDDI_PIXEL_ARGB8888: return GL_BGRA_EXT;
#ifdef RAD_VITAGL
    case PDDI_PIXEL_DXT1: return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    case PDDI_PIXEL_DXT3: return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    case PDDI_PIXEL_DXT5: return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
#else
    case PDDI_PIXEL_DXT1: return GL_RGBA;
    case PDDI_PIXEL_DXT3: return GL_RGBA;
    case PDDI_PIXEL_DXT5: return GL_RGBA;
#endif
    }
    PDDIASSERT(false);
    return GL_INVALID_ENUM;
};

static inline pddiPixelFormat PickPixelFormat(pddiTextureType type, int bitDepth, int alphaDepth)
{
    switch (type)
    {
    case PDDI_TEXTYPE_RGB:
        switch (alphaDepth)
        {
        case 0:
            return (bitDepth <= 16) ? PDDI_PIXEL_RGB565 : PDDI_PIXEL_RGB888;
        case 1:
            return (bitDepth <= 16) ? PDDI_PIXEL_ARGB1555 : PDDI_PIXEL_ARGB8888;
        default:
            return (bitDepth <= 16) ? PDDI_PIXEL_ARGB4444 : PDDI_PIXEL_ARGB8888;
        }
        break;

    case PDDI_TEXTYPE_PALETTIZED:
        return PDDI_PIXEL_PAL8;

    case PDDI_TEXTYPE_LUMINANCE:
        return PDDI_PIXEL_LUM8;

    case PDDI_TEXTYPE_BUMPMAP:
        return PDDI_PIXEL_DUDV88;

    case PDDI_TEXTYPE_DXT1:
        return PDDI_PIXEL_DXT1;

    case PDDI_TEXTYPE_DXT2:
        return PDDI_PIXEL_DXT2;

    case PDDI_TEXTYPE_DXT3:
        return PDDI_PIXEL_DXT3;

    case PDDI_TEXTYPE_DXT4:
        return PDDI_PIXEL_DXT4;

    case PDDI_TEXTYPE_DXT5:
        return PDDI_PIXEL_DXT5;

    case PDDI_TEXTYPE_YUV:
        return PDDI_PIXEL_YUV;
    }
    PDDIASSERT(false);
    return PDDI_PIXEL_UNKNOWN;
};

void pglTexture::SetGLState(void)
{
    if(context->contextID != contextID)
    {
        contextID = context->contextID;
        gltexture = 0;
    }

    MICROPROFILE_SCOPEI("PDDI", "pglTexture::SetGLState", MP_RED);

    // Ensure we're using texture unit 0 (shader samples from unit 0)
    glActiveTexture(GL_TEXTURE0);

    if(!valid)
    {
        bool newlyCreated = false;
        if ( gltexture == 0 )
        {
            glGenTextures( 1, &gltexture );
            newlyCreated = true;
        }
        glBindTexture( GL_TEXTURE_2D, gltexture );

//      if(nMipMap == 0)
        if (type == PDDI_TEXTYPE_DXT1 || type == PDDI_TEXTYPE_DXT3 || type == PDDI_TEXTYPE_DXT5)
        {
#ifdef RAD_VITAGL
            unsigned int blocksize = lock.format == PDDI_PIXEL_DXT1 ? 8 : 16;
            GLenum internalFormat = lock.format == PDDI_PIXEL_DXT5 ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT :
                lock.format == PDDI_PIXEL_DXT3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT : GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, internalFormat, xSize,
                ySize, 0, ceil(xSize/4.0)*ceil(ySize/4.0)*blocksize, (GLvoid*)bits[0]);
#else
            unsigned char* image = new unsigned char[xSize * ySize * 4];
            unsigned int blocksize = lock.format == PDDI_PIXEL_DXT1 ? 8 : 16;
            if (type == PDDI_TEXTYPE_DXT1)
                BlockDecompressImageBC1(xSize, ySize, (const uint8_t*)bits[0], image);
            else if (type == PDDI_TEXTYPE_DXT3)
                BlockDecompressImageBC2(xSize, ySize, (const uint8_t*)bits[0], image);
            else if( type == PDDI_TEXTYPE_DXT5)
                BlockDecompressImageBC3(xSize, ySize, (const uint8_t*)bits[0], image);
#ifdef RAD_TVOS
            // tvOS: Use GL_RGBA for internal format (decompressed DXT is RGBA)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, xSize,
                ySize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                image);
            TvosLogTexUpload( "DXT", xSize, ySize, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE );
#else
            glTexImage2D(GL_TEXTURE_2D, 0, PickPixelFormat(lock.format), xSize,
                ySize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                image);
#endif
            delete [] image;
#endif
        }
#ifdef RAD_VITAGL
        else if (type == PDDI_TEXTYPE_YUV)
        {
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, VGL_YUV420P_BT601, xSize,
                ySize, 0, (xSize * ySize * 3) / 2, (GLvoid *)bits[0]);
        }
#endif
        else
        {
#ifdef RAD_TVOS
            GLenum internalFmt = GL_RGBA;
            GLenum srcFmt = lock.native ? GL_BGRA_EXT : GL_RGBA;
            bool isLikelyVideo = isVideoTexture || ( xSize == 1024 && ySize == 1024 );
            
            // One-time trace: verify video texture data without printing raw pixels every run.
            static bool s_texUploadDiagDone = false;
            if ( !s_texUploadDiagDone && isLikelyVideo ) {
                s_texUploadDiagDone = true;
#if defined( RAD_TVOS_RENDER_DIAGNOSTICS )
                unsigned char* src = (unsigned char*)bits[0];
                unsigned char* row100 = ( src && ySize > 100 ) ? ( src + 100 * xSize * 4 ) : src;
                bool hasData = src && ( src[0] != 0 || src[1] != 0 || src[2] != 0 || row100[0] != 0 || row100[1] != 0 || row100[2] != 0 );
                SRR2::Diagnostics::Tracef(
                    SRR2::Diagnostics::VIDEO,
                    "[TEX_UPLOAD_TRACE] ptr=%p size=%dx%d fmt=0x%x p00=%u,%u,%u,%u p10=%u,%u,%u,%u p0100=%u,%u,%u,%u hasData=%d",
                    (void*)bits[0],
                    xSize,
                    ySize,
                    (unsigned)srcFmt,
                    src ? src[0] : 0,
                    src ? src[1] : 0,
                    src ? src[2] : 0,
                    src ? src[3] : 0,
                    src ? src[4] : 0,
                    src ? src[5] : 0,
                    src ? src[6] : 0,
                    src ? src[7] : 0,
                    row100 ? row100[0] : 0,
                    row100 ? row100[1] : 0,
                    row100 ? row100[2] : 0,
                    row100 ? row100[3] : 0,
                    hasData ? 1 : 0 );
                if ( !hasData )
                {
                    SRR2::Diagnostics::Anomalyf(
                        SRR2::Diagnostics::VIDEO,
                        "[VIDEO_TEXTURE_BLACK] ptr=%p size=%dx%d fmt=0x%x",
                        (void*)bits[0],
                        xSize,
                        ySize,
                        (unsigned)srcFmt );
                }
#endif
            }
            
            if ( newlyCreated )
            {
                glTexImage2D( GL_TEXTURE_2D, 0, internalFmt, xSize,
                    ySize, 0, srcFmt, GL_UNSIGNED_BYTE,
                    (GLvoid*)bits[0] );
            }
            else
            {
                glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, xSize,
                    ySize, srcFmt, GL_UNSIGNED_BYTE,
                    (GLvoid*)bits[0] );
            }
            TvosLogTexUpload( newlyCreated ? "new" : "update", xSize, ySize, internalFmt, srcFmt, GL_UNSIGNED_BYTE, gltexture, isLikelyVideo, this );
#else
            GLenum srcFmt = lock.native ? GL_BGRA_EXT : GL_RGBA;
            GLenum internalFmt = PickPixelFormat( lock.format );
            if ( newlyCreated )
            {
                glTexImage2D( GL_TEXTURE_2D, 0, internalFmt, xSize,
                    ySize, 0, srcFmt, GL_UNSIGNED_BYTE,
                    (GLvoid*)bits[0] );
            }
            else
            {
                glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, xSize,
                    ySize, srcFmt, GL_UNSIGNED_BYTE,
                    (GLvoid*)bits[0] );
            }
#endif
        }
        /*
        else
        {

            int tmpMipMap = nMipMap;
            tmpMipMap--;
            if(tmpMipMap < 0)
                tmpMipMap = 0;

            int i = 0;
            int width = xSize;
            int height = ySize;
            bool bottomed = false;
            bool done = false;

            while(!done)
            {
                char* data = bits[i];
                if(i > tmpMipMap)
                    data = bits[tmpMipMap];

                glTexImage2D(GL_TEXTURE_2D, i, GL_RGBA8, xSize >> i,
                    ySize >> i, 0, lock.native ? GL_BGRA_EXT : GL_RGBA, GL_UNSIGNED_BYTE,
                    (GLvoid *)data);

                done = (width == 1) || (height == 1);

                width >>= 1;
                if(width < 1) 
                {
                    width = 1;
                    bottomed = true;
                }

                height >>= 1;
                if(height < 1) 
                {
                    height = 1;
                    bottomed = true;
                }

                i++;
            }
        }
        */

        valid = true;
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, gltexture);
    }
}

int fastlog2(int x)
{
    int r = 0;
    int tmp = x;
    while(tmp > 1)
    {
        r++;
        tmp = tmp >> 1;

        if((tmp << r) != x)
            // not power of 2
            return -1;
    }
    return r;
}

bool pglTexture::Create(int x, int y, int bpp, int alphaDepth, int nMip, pddiTextureType textureType, pddiTextureUsageHint usageHint)
{
    xSize = x;
    ySize = y;
    nMipMap = nMip;
    type = textureType;

    log2X = fastlog2(xSize);
    log2Y = fastlog2(ySize);

#if !defined( RAD_VITA ) && !defined( RAD_TVOS )
    if((log2X == -1) || (log2Y == -1))
    {
        lastError = PDDI_TEX_NOT_POW_2;
        return false;
    }
#elif defined( RAD_TVOS )
    if ( ( ( log2X == -1 ) || ( log2Y == -1 ) ) && nMipMap > 0 )
    {
        lastError = PDDI_TEX_NOT_POW_2;
        return false;
    }
#endif

    if ((xSize > context->GetMaxTextureDimension()) ||
        (ySize > context->GetMaxTextureDimension()))
    {
        lastError = PDDI_TEX_TOO_BIG;
        return false;
    }

    // TODO palletized
    if (textureType == PDDI_TEXTYPE_PALETTIZED)
    {
        textureType = PDDI_TEXTYPE_RGB;
        bpp = 32;
    }

    bits = new char* [nMipMap + 1];
    if (type == PDDI_TEXTYPE_DXT1 || type == PDDI_TEXTYPE_DXT3 || type == PDDI_TEXTYPE_DXT5)
    {
        unsigned int blocksize = type == PDDI_TEXTYPE_DXT1 ? 8 : 16;
        for(int i = 0; i < nMipMap+1; i++)
            bits[i] = (char*)radMemoryAllocAligned(radMemoryGetCurrentAllocator(), size_t(ceil(double(xSize>>i)/4)*ceil(double(ySize>>i)/4)*blocksize), 16);
    }
    else
    {
        for(int i = 0; i < nMipMap+1; i++)
            bits[i] = (char*)radMemoryAllocAligned(radMemoryGetCurrentAllocator(), ((xSize>>i)*(ySize>>i)*bpp)/8, 16);
    }

    lock.depth = bpp;
    lock.format = PickPixelFormat(textureType, bpp, alphaDepth);

    if(context->GetDisplay()->ExtBGRA())
    {
        lock.native = true;
        lock.rgbaLShift[0] = lock.rgbaRShift[0] =
        lock.rgbaLShift[1] = lock.rgbaRShift[1] =
        lock.rgbaLShift[2] = lock.rgbaRShift[2] =
        lock.rgbaLShift[3] = lock.rgbaRShift[3] = 0;

        lock.rgbaMask[0] = 0x00ff0000;
        lock.rgbaMask[1] = 0x0000ff00;
        lock.rgbaMask[2] = 0x000000ff;
        lock.rgbaMask[3] = 0xff000000;
    }
    else
    {
        lock.native = false;
        lock.rgbaRShift[0] = 16;
        lock.rgbaLShift[2] = 16;

        lock.rgbaLShift[0] = 
        lock.rgbaLShift[1] = lock.rgbaRShift[1] =
        lock.rgbaRShift[2] =
        lock.rgbaLShift[3] = lock.rgbaRShift[3] = 0;

        lock.rgbaMask[0] = 0x000000ff;
        lock.rgbaMask[1] = 0x0000ff00;
        lock.rgbaMask[2] = 0x00ff0000;
        lock.rgbaMask[3] = 0xff000000;
    }

    context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_32BIT, (float)((xSize * ySize * lock.depth) / 8192));
    context->ADD_STAT(PDDI_STAT_TEXTURE_COUNT_32BIT, 1);

    return true;
}

pglTexture::pglTexture(pglContext* c)
{
    context = c;
    contextID = c->contextID;
    bits = NULL;
    gltexture = 0;
    priority = 15;
    valid = false;
    isVideoTexture = false;  // Default to regular texture behavior
}

pglTexture::~pglTexture()
{
    if(gltexture) glDeleteTextures(1, &gltexture);

    for(int i = 0; i < nMipMap+1; i++)
        radMemoryFreeAligned(bits[i]);

    if(bits) delete [] bits;

    context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_32BIT, -(float)((xSize * ySize * lock.depth) / 8192));
    context->ADD_STAT(PDDI_STAT_TEXTURE_COUNT_32BIT, -1);
}

pddiPixelFormat pglTexture::GetPixelFormat()
{
    return PDDI_PIXEL_ARGB8888;
}

int   pglTexture::GetWidth()
{
    return xSize;
}

int   pglTexture::GetHeight()
{
    return ySize;
}

int   pglTexture::GetDepth()
{
    return 32;
}

int   pglTexture::GetNumMipMaps()
{
    return nMipMap;
}

int pglTexture::GetAlphaDepth()
{
    return 8;
}

pddiLockInfo* pglTexture::Lock(int mipMap, pddiRect* rect)
{
    PDDIASSERT(mipMap <= nMipMap);

    lock.width = xSize >> mipMap;
    lock.height = ySize >> mipMap;
    if (lock.format == PDDI_PIXEL_DXT1 || lock.format == PDDI_PIXEL_DXT3 || lock.format == PDDI_PIXEL_DXT5)
    {
        unsigned int blocksize = lock.format == PDDI_PIXEL_DXT1 ? 8 : 16;
        lock.pitch = ceil( double( xSize >> mipMap ) / 4 ) * blocksize;
        lock.bits = bits[mipMap];
    }
    else if (lock.format == PDDI_PIXEL_YUV)
    {
        lock.pitch = (lock.width * lock.depth) / 8;
        lock.bits = bits[mipMap];
    }
    else
    {
#ifdef RAD_TVOS
        if (isVideoTexture)
        {
            // Video textures: Use POSITIVE pitch - sws_scale outputs top-to-bottom
            lock.pitch = (lock.width * 4);
            lock.bits = bits[mipMap];
        }
        else
        {
            // Regular textures: Use NEGATIVE pitch - original engine expects bottom-up layout
            lock.pitch = -(lock.width * 4);
            lock.bits = bits[mipMap] + (lock.width * (lock.height - 1) * 4);
        }
#else
        lock.pitch = -(lock.width * 4);
        lock.bits = bits[mipMap] + (lock.width * (lock.height - 1) * 4);
#endif
    }

    return &lock;
}

void pglTexture::Unlock(int mipLevel)
{
    valid = false;
}

void pglTexture::SetPriority(int p)
{
    priority = p;
}

int pglTexture::GetPriority(void)
{
    return priority;
}

// paging control
void pglTexture::Prefetch(void)
{
}

void pglTexture::Discard(void)
{
}

// palette managment
int pglTexture::GetNumPaletteEntries(void)
{
    return 0;
}

void pglTexture::SetPalette(int nEntries, pddiColour* palette)
{
}

int pglTexture::GetPalette(pddiColour* palette)
{
    return 0;
}
