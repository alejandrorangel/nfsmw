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

// Frontend draw toggle (FEManager.cpp). Initialised .data in the original
// (GameCube 0x8041B99C); on by default.
bool DrawFEng = true;

// Chunk loaders registered by static bChunkLoader objects the decomp hasn't
// defined yet (found by scanning speed.exe 1.3 for calls to the bChunkLoader
// constructor @ 0x45D600). Only the ones the port can handle natively are
// registered; lights and emitters come later.
#include "Speed/Indep/bWare/Inc/bChunk.hpp"

int LoaderTexturePack(bChunk *chunk);
int UnloaderTexturePack(bChunk *chunk);

bChunkLoader bChunkLoaderTexturePackList(0xB3300000, LoaderTexturePack, UnloaderTexturePack); // @ 0x8862B4
bChunkLoader bChunkLoaderCramDataChunks(0xB3320000, LoaderTexturePack, UnloaderTexturePack);  // @ 0x8862D4
bChunkLoader bChunkLoaderTextureAnimPack(0xB0300100, LoaderTexturePack, UnloaderTexturePack); // @ 0x8862F4

int LoaderSolidList(bChunk *chunk);
int UnloaderSolidList(bChunk *chunk);
bChunkLoader bChunkLoaderSolidList(0x80134000, LoaderSolidList, UnloaderSolidList); // models (SolidNative.cpp)

int LoaderLights(bChunk *chunk);
int UnloaderLights(bChunk *chunk);
bChunkLoader bChunkLoaderLightMaterials(0x135200, LoaderLights, UnloaderLights); // light materials (eLight.cpp)

// Shaper light rigs (eLight.hpp), from speed.exe 1.3 .data @ 0x8F2AB0..0x8F2FB0:
// {mode, theta, phi, r, g, b, scale} per light; elSetupLights uses lights 1..3.
#include "Speed/Indep/Src/Ecstasy/eLight.hpp"

#define SHAPER(m, t, p, r, g, b, s) {static_cast<ESHAPER_LIGHT_MODE>(m), t, p, r, g, b, s}
#define SHAPER_OFF SHAPER(0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f)
#define GARAGE_RIG(sign)                                                                                                               \
    {0, {SHAPER_OFF, SHAPER(0, sign 90.0f, 48.0f, 1.0f, 1.0f, 0.95f, 0.98f), SHAPER(0, sign 134.0f, 185.0f, 0.83f, 0.83f, 1.0f, 0.5f),     \
         SHAPER(0, sign 27.0f, 175.0f, 0.83f, 0.83f, 1.0f, 0.35f)},                                                                     \
     bVector3(0.0f, 0.0f, 0.0f), 0}

eShaperLightRig ShaperLightsDefault = {
    0, {SHAPER_OFF, SHAPER(2, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.2f), SHAPER(3, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.2f), SHAPER(1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f)},
    bVector3(0.0f, 0.0f, 0.0f), 0};
eShaperLightRig ShaperLightsBackRoom = GARAGE_RIG(+);
eShaperLightRig ShaperLightsCarLot = GARAGE_RIG(-);
eShaperLightRig ShaperLightsCShop = GARAGE_RIG(+);
eShaperLightRig ShaperLightsQRace = GARAGE_RIG(+);
eShaperLightRig ShaperLightsSafehouse = GARAGE_RIG(+);
eShaperLightRig ShaperLightsCarsInGame = {
    0, {SHAPER_OFF, SHAPER(2, 0.0f, 350.0f, 0.85f, 0.9f, 1.0f, 0.83f), SHAPER(2, 50.0f, 220.0f, 0.55f, 0.6f, 1.0f, 0.4f),
        SHAPER(2, 310.0f, 220.0f, 0.65f, 0.7f, 1.0f, 0.45f)},
    bVector3(0.0f, 0.0f, 0.0f), 0};
eShaperLightRig ShaperLightsCharacters = {
    0, {SHAPER_OFF, SHAPER(1, 122.9f, 194.0f, 0.5f, 0.5f, 0.5f, 0.53f), SHAPER(1, 28.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.59f), SHAPER(1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f)},
    bVector3(0.0f, 0.0f, 0.0f), 0};
eShaperLightRig ShaperLightsCharactersBackup = {
    0, {SHAPER_OFF, SHAPER(2, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f), SHAPER(1, 30.1f, 0.0f, 1.0f, 1.0f, 1.0f, 0.63f), SHAPER(1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f)},
    bVector3(0.0f, 0.0f, 0.0f), 0};
