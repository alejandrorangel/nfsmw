// EA audio formats used by the PC data (SOUND/): "PT" patch headers and
// EA-XA ADPCM (R2) sample data, as found in SCHl/SCCl/SCDl/SCEl streams
// (music, speech) and in AEMS module banks (ABKC/BNKl).
//
// Verified against SOUND/PFDATA/MW_Music.mus (version 3 stream: stereo,
// 36000 Hz, big-endian SCDl fields).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ea_audio {

// A "PT\0\0" / GSTR patch header: tag (1 byte), length (1 byte), big-endian
// value; 0xFF ends it, 0xFC..0xFE are section markers without a value.
struct PatchHeader {
    int version = 0;     // tag 0x80
    int channels = 1;    // tag 0x82
    int codec = -1;      // tag 0x83 (absent: platform default, EA-XA on PC)
    int sample_rate = 0; // tag 0x84
    int num_samples = 0; // tag 0x85
    int loop_start = -1; // tag 0x86
    int loop_length = -1;// tag 0x87
    int data_offset = -1;// tag 0x88 (offset of the sample data in banks)
    int data_offset2 = -1;// tag 0x89 (banks: second channel's data, stereo patches)
    int volume = -1;     // tag 0x13 (banks: patch volume, 0..127)
};

// Parses tags from p (just after the 'PT\0\0'/'GSTR' marker and its 4 bytes
// of flags for streams). Returns bytes consumed, or 0 on malformed input.
size_t ParsePatchHeader(const uint8_t *p, size_t size, PatchHeader *out);

// EA-XA R2 decoder state for one channel.
struct XaChannel {
    int hist1 = 0;
    int hist2 = 0;
};

// Decodes one channel's EA-XA R2 frames: 15-byte ADPCM frames (info byte:
// predictor << 4 | shift, then 28 nibbles high-first) or 61-byte raw frames
// (0xEE, two big-endian history samples, 28 big-endian PCM samples). Writes up
// to max_samples to out (stride: interleave step). Returns samples written.
size_t DecodeXaR2(XaChannel &ch, const uint8_t *data, size_t size, int16_t *out, size_t max_samples, size_t stride);

// Decodes a whole SCHl..SCEl stream that starts at data (one stream segment).
// Interleaved 16-bit PCM in pcm; returns false if it isn't a stream.
bool DecodeStream(const uint8_t *data, size_t size, PatchHeader *header, std::vector<int16_t> *pcm, size_t *consumed);

// One sound of a BNKl sample bank (inside AEMS ABKC module banks). Data is
// EA-XA R2, one frame stream per channel: data_offset (tag 0x88) and, for
// stereo patches, data_offset2 (tag 0x89), both from the BNKl start.
struct BankSound {
    PatchHeader header;
    const uint8_t *data = nullptr; // EA-XA frames
    size_t data_size = 0;          // bytes available from data to the bank end
};

// Parses a 'BNKl' block (version 5 in the PC data: u16 version, u16 count,
// u32 size, then one u32 offset per slot, relative to that offset's own
// position; 0 = empty slot). Empty slots come back with data == nullptr.
bool ParseBank(const uint8_t *bnkl, size_t size, std::vector<BankSound> *sounds);

} // namespace ea_audio
