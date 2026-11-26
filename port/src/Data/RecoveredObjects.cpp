// Global objects the decomp declares but hasn't defined yet. Unlike plain
// data, these have constructors, so a zero-filled stub leaves them broken.
// Constructor arguments are taken from the PC build's static initialisers.

#include "Speed/Indep/Src/Ecstasy/eStreamingPack.hpp"

void TextureLoadedStreamingEntryCallback(bChunk *chunk, eStreamingEntry *streaming_entry, eStreamingPack *streaming_pack);
void TextureUnLoadedStreamingEntryCallback(bChunk *chunk, eStreamingEntry *streaming_entry, eStreamingPack *streaming_pack);
void TextureLoadingStreamingPackPhase1(eStreamingPackHeaderLoadingInfoPhase1 *loading_info);
void TextureLoadingStreamingPackPhase2(eStreamingPackHeaderLoadingInfoPhase2 *loading_info);
void TextureUnloadingStreamingPack(eStreamingPack *streaming_pack);

void SolidLoadedStreamingEntryCallback(bChunk *chunk, eStreamingEntry *streaming_entry, eStreamingPack *streaming_pack);
void SolidUnloadedStreamingEntryCallback(bChunk *chunk, eStreamingEntry *streaming_entry, eStreamingPack *streaming_pack);
void SolidLoadingStreamingPackPhase1(eStreamingPackHeaderLoadingInfoPhase1 *loading_info);
void SolidLoadingStreamingPackPhase2(eStreamingPackHeaderLoadingInfoPhase2 *loading_info);
void SolidUnloadingStreamingPack(eStreamingPack *streaming_pack);

// speed.exe 1.3 static initialiser @ 0x886300 (object @ 0x91A098)
eStreamPackLoader StreamingTexturePackLoader(0x80, TextureLoadedStreamingEntryCallback, TextureUnLoadedStreamingEntryCallback,
                                             TextureLoadingStreamingPackPhase1, TextureLoadingStreamingPackPhase2,
                                             TextureUnloadingStreamingPack);

// speed.exe 1.3 static initialiser @ 0x886080 (object @ 0x91A060)
eStreamPackLoader StreamingSolidPackLoader(0x80, SolidLoadedStreamingEntryCallback, SolidUnloadedStreamingEntryCallback,
                                           SolidLoadingStreamingPackPhase1, SolidLoadingStreamingPackPhase2, SolidUnloadingStreamingPack);
