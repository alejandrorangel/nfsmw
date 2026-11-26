// EA RealFont ("realgraph", the RealFontOld namespace) as used by FEngFont.
// Fonts are used in place: the chunk data is the Font, with glyph and kerning
// tables at byte offsets (mGlyphTbl, mKernTbl), so there's no 64-bit layout
// work. Logic from speed.exe 1.3: BSearch 0x7E88F0, Create 0x7E8930,
// Destroy 0x7E8960, GetKern 0x7E89D0, GetGlyph 0x569BF0.

#include "Speed/Indep/Src/Frontend/RealFontOld.hpp"

#include <cstdint>
#include <cstring>

namespace RealFontOld {

namespace {

const int kWideGlyphFlag = 0x40000; // 16-byte glyphs + per-glyph kerning runs

int GlyphStride(const Font *font) {
    return (font->mFlags & kWideGlyphFlag) ? 0x10 : 0x0C;
}

const char *Base(const Font *font) {
    return reinterpret_cast<const char *>(font);
}

} // namespace

const Glyph *BSearch(short unicode, const Glyph *table, unsigned int count, int stride) {
    if (stride == 0) {
        stride = sizeof(Glyph); // callers relying on the default
    }
    const char *base = reinterpret_cast<const char *>(table);
    unsigned short want = static_cast<unsigned short>(unicode);
    while (count != 0) {
        const char *mid = base + (count >> 1) * stride;
        int diff = static_cast<int>(want) - static_cast<int>(reinterpret_cast<const Glyph *>(mid)->mUnicode);
        if (diff == 0) {
            return reinterpret_cast<const Glyph *>(mid);
        }
        if (diff > 0) {
            base = mid + stride;
            count--;
        }
        count >>= 1;
    }
    return nullptr;
}

Font *Font::Create(void *data) {
    return static_cast<Font *>(data); // no font driver on PC
}

void Font::Destroy(Font *font) {}

int Font::GetHeight() {
    return mStates[FONT_HEIGHT];
}

const Glyph *Font::GetGlyph(int unicode) const {
    int stride = GlyphStride(this);
    const Glyph *table = reinterpret_cast<const Glyph *>(Base(this) + mGlyphTbl);
    unsigned int index = static_cast<unsigned int>((unicode & 0xFFFF) - 0x20);
    if (index < mNum) {
        const Glyph *g = reinterpret_cast<const Glyph *>(reinterpret_cast<const char *>(table) + index * stride);
        if (g->mUnicode == static_cast<unsigned short>(unicode)) {
            return g;
        }
    }
    return BSearch(static_cast<short>(unicode), table, mNum, stride);
}

int Font::GetKern(const Glyph *glyph, wchar_t wch) {
    int count = glyph->mNumKern;
    if (count == 0) {
        return 0;
    }
    uint16_t second = static_cast<uint16_t>(wch);
    if (mFlags & kWideGlyphFlag) {
        // Per-glyph run of 4-byte pairs {u16 second, s8 kern, pad}.
        const char *pairs = Base(this) + mKernTbl + 4 + glyph->mKernIndex * 4;
        for (int i = 0; i < count; i++) {
            const char *pair = pairs + i * 4;
            uint16_t code;
            std::memcpy(&code, pair, 2);
            if (code == second) {
                return static_cast<signed char>(pair[2]);
            }
        }
        return 0;
    }
    // One global table: {s32 count} then 4-byte {u16 second, s8 kern, u8 first}.
    const char *table = Base(this) + mKernTbl;
    int32_t total;
    std::memcpy(&total, table, 4);
    for (int i = 0; i < total; i++) {
        const char *pair = table + 4 + i * 4;
        uint16_t code;
        std::memcpy(&code, pair, 2);
        if (code == second && static_cast<unsigned char>(pair[3]) == glyph->mUnicode) {
            return static_cast<signed char>(pair[2]);
        }
    }
    return 0;
}

} // namespace RealFontOld
