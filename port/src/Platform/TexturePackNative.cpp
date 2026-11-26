// Native loading of PC texture packs.
//
// The game loads texture packs in place: TexturePackHeader, TextureIndexEntry,
// eStreamingEntry, TextureInfo and the platform record are used straight out of
// the file buffer, with 32-bit pointer fields patched at load time. With 64-bit
// pointers those layouts don't fit, so here every record is copied into a
// native struct, and the game only ever sees the native copies.
//
// The disk records keep a pointer back to their native copy in their own
// pointer slots (scratch space in the original too) so unloading can find it.
//
// Logic follows speed.exe 1.3 InternalLoadTexturePackHeaderChunks (0x5078E0)
// and TexturePack::AttachTextureInfo (0x502D40).

#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/Ecstasy/eStreamingPack.hpp"
#include "Speed/Indep/Src/Misc/SpeedChunks.hpp"
#include "Speed/Indep/bWare/Inc/bChunk.hpp"
#include "Speed/Indep/bWare/Inc/bMemory.hpp"

#include <cstddef>
#include <cstring>
#include <new>

namespace {

// On-disk (32-bit) record sizes and the native fields their POD prefix maps to.
const int kDiskPackHeaderPodSize = 0x70;     // Version .. EndianSwapped
const int kDiskIndexEntrySize = 0x8;         // NameHash, pTextureInfo
const int kDiskStreamingEntrySize = 0x18;    // ... ChunkData at 0x14
const int kDiskStreamingEntryPodSize = 0x14; // NameHash .. RefCount
const int kDiskTextureInfoPodOffset = 0xC;   // DebugName
const int kDiskTextureInfoPodSize = 0x5A;    // DebugName .. ScaleT

// Where a disk record stashes the pointer to its native copy.
const int kDiskPackHeaderNativeSlot = 0x70;  // pTexturePack/TextureIndexEntryTable slots (12 bytes)
const int kDiskTextureInfoNativeSlot = 0x0;  // PlatInfo/Next/Prev slots (12 bytes)

template <typename T> void StashNative(void *disk_record, int offset, T *native) {
    std::memcpy(static_cast<char *>(disk_record) + offset, &native, sizeof(native));
}

template <typename T> T *FetchNative(const void *disk_record, int offset) {
    T *native;
    std::memcpy(&native, static_cast<const char *>(disk_record) + offset, sizeof(native));
    return native;
}

template <typename T> T *AllocNativeArray(int count, const char *name) {
    if (count <= 0) {
        return nullptr;
    }
    void *mem = bMalloc(static_cast<int>(sizeof(T)) * count, name, 0, 0);
    std::memset(mem, 0, sizeof(T) * count);
    T *table = static_cast<T *>(mem);
    for (int i = 0; i < count; i++) {
        new (&table[i]) T();
    }
    return table;
}

void CheckLayouts() {
    // The POD runs must map 1:1 onto the native structs.
    static_assert(offsetof(TexturePackHeader, EndianSwapped) + sizeof(int32) == kDiskPackHeaderPodSize, "TexturePackHeader prefix");
    static_assert(offsetof(eStreamingEntry, RefCount) + sizeof(uint16) == kDiskStreamingEntryPodSize, "eStreamingEntry prefix");
}

// TextureInfo's POD block runs from DebugName to ScaleT (private), so locate it
// relative to the public fields around it.
char *TextureInfoPodStart(TextureInfo *t) {
    return t->DebugName;
}

void CopyTextureInfo(TextureInfo *native, const char *disk) {
    char *dst = TextureInfoPodStart(native);
    // pTexturePack follows ScaleT (+ alignment padding) natively.
    std::ptrdiff_t room = reinterpret_cast<char *>(&native->pTexturePack) - dst;
    if (room < kDiskTextureInfoPodSize) {
        __builtin_trap();
    }
    std::memcpy(dst, disk + kDiskTextureInfoPodOffset, kDiskTextureInfoPodSize);
}

void CopyPlatInfo(TextureInfoPlatInfo *native, const char *disk) {
    std::memcpy(native->Disk, disk, PORT_DISK_TEXTURE_PLAT_INFO_SIZE);
    std::memcpy(&native->Format, disk + PORT_DISK_TEXTURE_PLAT_INFO_FORMAT_OFFSET, sizeof(native->Format));
    native->MetalTexture = nullptr;
}

struct NativeTextureRecord {
    TextureInfo Info;
    TextureInfoPlatInfo Plat;
};

} // namespace

TexturePackHeader *PortLoadTexturePackHeaderChunks(bChunk *chunk) {
    CheckLayouts();

    char *disk_header = nullptr;
    char *disk_keys = nullptr;
    char *disk_entries = nullptr;
    char *disk_textures = nullptr;
    char *disk_plats = nullptr;
    int num_keys = 0;
    int num_entries = 0;
    int num_textures = 0;
    int num_plats = 0;

    for (bChunk *c = chunk->GetFirstChunk(); c != chunk->GetLastChunk(); c = c->GetNext()) {
        switch (c->GetID()) {
        case BCHUNK_TEXTURE_PACK_INFO_HEADER:
            disk_header = c->GetData();
            break;
        case BCHUNK_TEXTURE_PACK_INFO_KEYS:
            disk_keys = c->GetData();
            num_keys = c->GetSize() / kDiskIndexEntrySize;
            break;
        case BCHUNK_TEXTURE_PACK_INFO_ENTRIES:
            disk_entries = c->GetData();
            num_entries = c->GetSize() / kDiskStreamingEntrySize;
            break;
        case BCHUNK_TEXTURE_PACK_INFO_TEXTURES:
            disk_textures = c->GetData();
            num_textures = c->GetSize() / PORT_DISK_TEXTURE_INFO_SIZE;
            break;
        case BCHUNK_TEXTURE_PACK_INFO_COMPS:
            disk_plats = c->GetData();
            num_plats = c->GetSize() / PORT_DISK_TEXTURE_PLAT_INFO_SIZE;
            break;
        }
    }
    if (disk_header == nullptr || num_textures != num_plats) {
        return nullptr;
    }

    TexturePackHeader *header = AllocNativeArray<TexturePackHeader>(1, "TexturePackHeader (native)");
    std::memcpy(header, disk_header, kDiskPackHeaderPodSize);
    header->EndianSwapped = 1;
    StashNative(disk_header, kDiskPackHeaderNativeSlot, header);

    header->TextureIndexEntryTable = AllocNativeArray<TextureIndexEntry>(num_keys, "TextureIndexEntry (native)");
    for (int i = 0; i < num_keys; i++) {
        std::memcpy(&header->TextureIndexEntryTable[i].NameHash, disk_keys + i * kDiskIndexEntrySize, sizeof(uint32));
    }

    header->TextureStreamEntryTable = AllocNativeArray<eStreamingEntry>(num_entries, "eStreamingEntry (native)");
    for (int i = 0; i < num_entries; i++) {
        std::memcpy(&header->TextureStreamEntryTable[i], disk_entries + i * kDiskStreamingEntrySize, kDiskStreamingEntryPodSize);
    }

    TexturePack *pack = new TexturePack(header, num_keys, nullptr, nullptr);
    header->pTexturePack = pack;

    // Non-streaming packs carry their texture records in the header chunk.
    if (num_textures > 0) {
        NativeTextureRecord *records = AllocNativeArray<NativeTextureRecord>(num_textures, "TextureInfo (native)");
        for (int i = 0; i < num_textures; i++) {
            char *disk_tex = disk_textures + i * PORT_DISK_TEXTURE_INFO_SIZE;
            CopyTextureInfo(&records[i].Info, disk_tex);
            CopyPlatInfo(&records[i].Plat, disk_plats + i * PORT_DISK_TEXTURE_PLAT_INFO_SIZE);
            StashNative(disk_tex, kDiskTextureInfoNativeSlot, &records[i].Info);
            pack->AttachTextureInfo(&records[i].Info, &records[i].Plat, i < num_keys ? &header->TextureIndexEntryTable[i] : nullptr);
        }
    }
    return header;
}

void PortUnloadTexturePackHeaderChunks(bChunk *chunk) {
    TexturePackHeader *header = nullptr;
    char *disk_textures = nullptr;
    int num_textures = 0;
    for (bChunk *c = chunk->GetFirstChunk(); c != chunk->GetLastChunk(); c = c->GetNext()) {
        if (c->GetID() == BCHUNK_TEXTURE_PACK_INFO_HEADER) {
            header = FetchNative<TexturePackHeader>(c->GetData(), kDiskPackHeaderNativeSlot);
        } else if (c->GetID() == BCHUNK_TEXTURE_PACK_INFO_TEXTURES) {
            disk_textures = c->GetData();
            num_textures = c->GetSize() / PORT_DISK_TEXTURE_INFO_SIZE;
        }
    }
    if (header == nullptr) {
        return;
    }
    TexturePack *pack = header->pTexturePack;
    NativeTextureRecord *records = nullptr;
    if (num_textures > 0) {
        // Records were allocated as one array; the first stash points at its start.
        records = reinterpret_cast<NativeTextureRecord *>(FetchNative<TextureInfo>(disk_textures, kDiskTextureInfoNativeSlot));
        pack->UnAssignTextureData(0, pack->GetTextureDataSize());
        for (int i = 0; i < num_textures; i++) {
            pack->UnattachTextureInfo(&records[i].Info, &records[i].Plat, nullptr);
        }
    }
    delete pack;
    if (records != nullptr) {
        bFree(records);
    }
    if (header->TextureStreamEntryTable != nullptr) {
        bFree(header->TextureStreamEntryTable);
    }
    if (header->TextureIndexEntryTable != nullptr) {
        bFree(header->TextureIndexEntryTable);
    }
    bFree(header);
}

// A streamed texture arrives as one chunk whose last bytes are its TextureInfo
// and platform record (disk layout). Returns the native copy, creating it on
// first use.
TextureInfo *PortGetStreamedTextureInfo(unsigned char *pdata, int uncompressed_size, TextureInfoPlatInfo **plat_info, bool create) {
    char *disk_plat = reinterpret_cast<char *>(pdata) + uncompressed_size - PORT_DISK_TEXTURE_PLAT_INFO_SIZE;
    char *disk_tex = disk_plat - PORT_DISK_TEXTURE_INFO_SIZE;
    NativeTextureRecord *record;
    if (create) {
        record = AllocNativeArray<NativeTextureRecord>(1, "Streamed TextureInfo (native)");
        CopyTextureInfo(&record->Info, disk_tex);
        CopyPlatInfo(&record->Plat, disk_plat);
        StashNative(disk_tex, kDiskTextureInfoNativeSlot, &record->Info);
    } else {
        record = reinterpret_cast<NativeTextureRecord *>(FetchNative<TextureInfo>(disk_tex, kDiskTextureInfoNativeSlot));
    }
    *plat_info = &record->Plat;
    return &record->Info;
}

void PortFreeStreamedTextureInfo(TextureInfo *texture_info) {
    bFree(texture_info); // NativeTextureRecord starts with its TextureInfo
}

// ---------------------------------------------------------------------------
// NFSMW_TEXTURE_SMOKE=1: after localization init, stream every texture in
// LANGUAGES\LANGUAGETEXTURES.BIN so the Metal debug view shows real pack data.
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

void PortTextureSmokeTest() {
    if (std::getenv("NFSMW_TEXTURE_SMOKE") == nullptr) {
        return;
    }
    const char *pack_name = "LANGUAGES\\LANGUAGETEXTURES.BIN";
    eStreamingPack *pack = StreamingTexturePackLoader.GetLoadedStreamingPack(pack_name);
    if (pack == nullptr || pack->pTexturePackHeader == nullptr) {
        std::fprintf(stderr, "[smoke] %s not loaded\n", pack_name);
        return;
    }
    TexturePackHeader *header = pack->pTexturePackHeader;
    int n = header->pTexturePack->GetNumTextures();
    std::fprintf(stderr, "[smoke] streaming %d textures from %s\n", n, pack_name);
    uint32 *hashes = static_cast<uint32 *>(bMalloc(n * sizeof(uint32), "smoke", 0, 0));
    for (int i = 0; i < n; i++) {
        hashes[i] = header->TextureIndexEntryTable[i].NameHash;
    }
    eLoadStreamingTexture(hashes, n, nullptr, nullptr, 0);
    eWaitForStreamingTexturePackLoading(pack_name);
    bFree(hashes);
    std::fprintf(stderr, "[smoke] done; holding 4s so the debug view can be inspected\n");
    usleep(4 * 1000 * 1000);
}
