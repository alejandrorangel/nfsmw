// TextureInfoPlatInterface on Metal: the native counterpart of the PC D3D9
// texture code (speed.exe 1.3 TextureInfoPlatInterface::Init @ 0x6DDD90).
//
// Init() runs when a texture's pixel data has been assigned (TexturePack::
// AssignTextureData). It converts the pack's D3D format into a Metal texture.

#include "Speed/Indep/Src/Ecstasy/Texture.hpp"

#include "../Mac/MetalBridge.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

struct FormatInfo {
    PortPixelFormat metal;
    int block_bytes; // bytes per 4x4 block for BC formats, 0 if uncompressed
    int src_bpp;     // source bytes per pixel for uncompressed formats
    bool convert;    // needs a CPU conversion to BGRA8
};

bool LookupFormat(unsigned int d3d, FormatInfo &out) {
    switch (d3d) {
    case PORT_D3DFMT_DXT1:
        out = {PORT_PIXEL_BC1, 8, 0, false};
        return true;
    case PORT_D3DFMT_DXT3:
        out = {PORT_PIXEL_BC2, 16, 0, false};
        return true;
    case PORT_D3DFMT_DXT5:
        out = {PORT_PIXEL_BC3, 16, 0, false};
        return true;
    case PORT_D3DFMT_A8R8G8B8:
    case PORT_D3DFMT_X8R8G8B8:
        out = {PORT_PIXEL_BGRA8, 0, 4, false};
        return true;
    case PORT_D3DFMT_R5G6B5:
    case PORT_D3DFMT_A1R5G5B5:
    case PORT_D3DFMT_A4R4G4B4:
        out = {PORT_PIXEL_BGRA8, 0, 2, true};
        return true;
    case PORT_D3DFMT_P8:
        out = {PORT_PIXEL_BGRA8, 0, 1, true};
        return true;
    }
    return false;
}

int MipBytes(const FormatInfo &f, int w, int h) {
    if (f.block_bytes != 0) {
        return ((w + 3) / 4) * ((h + 3) / 4) * f.block_bytes;
    }
    return w * h * f.src_bpp;
}

inline uint8_t Expand(unsigned v, int bits) {
    return static_cast<uint8_t>((v * 255 + ((1 << bits) - 1) / 2) / ((1 << bits) - 1));
}

// Converts one mip level to BGRA8 (D3D A8R8G8B8 memory order).
void ConvertToBGRA(unsigned int d3d, const uint8_t *src, int count, const uint8_t *palette, uint8_t *dst) {
    for (int i = 0; i < count; i++, dst += 4) {
        uint8_t b, g, r, a = 255;
        if (d3d == PORT_D3DFMT_P8) {
            const uint8_t *p = palette + src[i] * 4;
            b = p[0];
            g = p[1];
            r = p[2];
            a = p[3];
        } else {
            unsigned v = src[i * 2] | (src[i * 2 + 1] << 8);
            if (d3d == PORT_D3DFMT_R5G6B5) {
                r = Expand(v >> 11, 5), g = Expand((v >> 5) & 0x3F, 6), b = Expand(v & 0x1F, 5);
            } else if (d3d == PORT_D3DFMT_A1R5G5B5) {
                a = (v & 0x8000) ? 255 : 0, r = Expand((v >> 10) & 0x1F, 5), g = Expand((v >> 5) & 0x1F, 5), b = Expand(v & 0x1F, 5);
            } else { // A4R4G4B4
                a = Expand(v >> 12, 4), r = Expand((v >> 8) & 0xF, 4), g = Expand((v >> 4) & 0xF, 4), b = Expand(v & 0xF, 4);
            }
        }
        dst[0] = b;
        dst[1] = g;
        dst[2] = r;
        dst[3] = a;
    }
}

bool TraceTextures() {
    static const bool on = std::getenv("NFSMW_TRACE_TEXTURES") != nullptr;
    return on;
}

} // namespace

void TextureInfoPlatInterface::SetPlatInfo(TextureInfoPlatInfo *info) {
    this->PlatInfo = info;
}

void TextureInfoPlatInterface::Init() {
    TextureInfo *t = static_cast<TextureInfo *>(this);
    TextureInfoPlatInfo *plat = this->PlatInfo;
    if (plat == nullptr) {
        return;
    }
    if (plat->MetalTexture != nullptr) {
        PortMetalReleaseTexture(plat->MetalTexture);
        plat->MetalTexture = nullptr;
    }
    const uint8_t *data = static_cast<const uint8_t *>(t->ImageData);
    if (data == nullptr || t->Width <= 0 || t->Height <= 0) {
        return;
    }
    FormatInfo f;
    if (!LookupFormat(plat->Format, f)) {
        std::fprintf(stderr, "[texture] %s: unsupported D3D format %#x\n", t->DebugName, plat->Format);
        return;
    }

    // Keep only the mip levels whose data is actually present.
    int levels = t->NumMipMapLevels > 0 ? t->NumMipMapLevels : 1;
    int available = t->ImageSize;
    int fit = 0;
    for (int level = 0, used = 0, w = t->Width, h = t->Height; level < levels; level++) {
        used += MipBytes(f, w, h);
        if (used > available && level > 0) {
            break;
        }
        fit = level + 1;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }

    void *texture = PortMetalCreateTexture(t->Width, t->Height, fit, f.metal, t->DebugName);
    if (texture == nullptr) {
        return;
    }
    std::vector<uint8_t> converted;
    const uint8_t *src = data;
    for (int level = 0, w = t->Width, h = t->Height; level < fit; level++) {
        int bytes = MipBytes(f, w, h);
        if (f.block_bytes != 0) {
            PortMetalUploadMip(texture, level, w, h, src, static_cast<size_t>((w + 3) / 4) * f.block_bytes);
        } else if (f.convert) {
            converted.resize(static_cast<size_t>(w) * h * 4);
            ConvertToBGRA(plat->Format, src, w * h, static_cast<const uint8_t *>(t->PaletteData), converted.data());
            PortMetalUploadMip(texture, level, w, h, converted.data(), static_cast<size_t>(w) * 4);
        } else {
            PortMetalUploadMip(texture, level, w, h, src, static_cast<size_t>(w) * f.src_bpp);
        }
        src += bytes;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    plat->MetalTexture = texture;
    PortMetalSetDebugTexture(texture);
    if (TraceTextures()) {
        std::fprintf(stderr, "[texture] %s %dx%d fmt=%#x mips=%d/%d\n", t->DebugName, t->Width, t->Height, plat->Format, fit, levels);
    }
}

void TextureInfoPlatInterface::Close() {
    TextureInfoPlatInfo *plat = this->PlatInfo;
    if (plat != nullptr && plat->MetalTexture != nullptr) {
        PortMetalReleaseTexture(plat->MetalTexture);
        plat->MetalTexture = nullptr;
    }
}

void *TextureInfoPlatInterface::LockImage(TextureLockType lock) {
    return static_cast<TextureInfo *>(this)->ImageData;
}

void TextureInfoPlatInterface::UnlockImage(void *image_lock) {}

void *TextureInfoPlatInterface::LockPalette(TextureLockType lock) {
    return static_cast<TextureInfo *>(this)->PaletteData;
}

void TextureInfoPlatInterface::UnlockPalette(void *palette_lock) {}

void *TextureInfoPlatInterface::CreateAnimData() {
    return nullptr;
}

void TextureInfoPlatInterface::ReleaseAnimData(void *anim_data) {}

void TextureInfoPlatInterface::SetAnimData(void *anim_data) {}
