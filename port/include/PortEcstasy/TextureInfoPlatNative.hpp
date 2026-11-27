// Native (Metal) replacement for the PC TextureInfoPlatInfo.
//
// On PC this is a 0x20-byte record stored after each TextureInfo in texture
// packs (Speed/PC/Src/Ecstasy/TextureInfoPlat.hpp is only a guess at it).
// From speed.exe 1.3 TextureInfoPlatInterface::Init (0x6DDD90):
//   0x00 bTNode links, 0x14 D3DFORMAT, 0x18 IDirect3DTexture9 *, rest unused.
// Natively we copy the record out of the pack and keep a Metal texture instead.
#ifndef PORT_ECSTASY_TEXTURE_INFO_PLAT_NATIVE_HPP
#define PORT_ECSTASY_TEXTURE_INFO_PLAT_NATIVE_HPP

#include "Speed/Indep/bWare/Inc/bList.hpp"

// Sizes of the PC on-disk records the native structs are copied from.
enum {
    PORT_DISK_TEXTURE_INFO_SIZE = 0x7C,
    PORT_DISK_TEXTURE_PLAT_INFO_SIZE = 0x20,
    PORT_DISK_TEXTURE_PLAT_INFO_FORMAT_OFFSET = 0x14,
};

// D3DFORMAT values used by PC texture packs.
enum PortD3DFormat {
    PORT_D3DFMT_A8R8G8B8 = 21,
    PORT_D3DFMT_X8R8G8B8 = 22,
    PORT_D3DFMT_R5G6B5 = 23,
    PORT_D3DFMT_A1R5G5B5 = 25,
    PORT_D3DFMT_A4R4G4B4 = 26,
    PORT_D3DFMT_P8 = 41,
    PORT_D3DFMT_DXT1 = 0x31545844, // 'DXT1'
    PORT_D3DFMT_DXT3 = 0x33545844, // 'DXT3'
    PORT_D3DFMT_DXT5 = 0x35545844, // 'DXT5'
};

struct TextureInfoPlatInfo : public bTNode<TextureInfoPlatInfo> {
    unsigned int Format;    // D3DFORMAT from the pack
    void *MetalTexture;     // retained id<MTLTexture>, or null
    void *LockedPixels;     // 32-bit copy handed out by LockImage for converted formats
    unsigned char Disk[PORT_DISK_TEXTURE_PLAT_INFO_SIZE]; // original record, for reference
};

void eUnSwizzle8bitPalette(unsigned int *palette);
void eSwizzle8bitPalette(unsigned int *palette);

#endif
