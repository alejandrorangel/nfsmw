// VP6 video decoding for the movie player (MovieNative.cpp). With FFmpeg
// (NFSMW_PORT_FFMPEG) this is libavcodec's "vp6" decoder; without it
// Create() fails and every movie is skipped, as before movies were ported.
//
// The PC movies are VP60 (simple profile) in EA's MV container: each MV0K /
// MV0F chunk's payload is one VP6 frame. VP6 codes pictures bottom-up;
// libavcodec's "vp6" (unlike "vp6f", Flash's) flips them, so planes come back
// top-down. No extradata is needed.
#pragma once

#include <cstddef>
#include <cstdint>

namespace port_movie {

struct YUVFrame {
    const uint8_t *plane[3] = {}; // Y, U, V (4:2:0), top-down
    int stride[3] = {};
    int width = 0;
    int height = 0;
};

class VP6Decoder {
  public:
    // Null when no decoder is available.
    static VP6Decoder *Create(int width, int height);
    virtual ~VP6Decoder() {}
    // Decodes one frame; on success *out stays valid until the next call.
    virtual bool Decode(const uint8_t *data, size_t size, YUVFrame *out) = 0;
};

// Whether this build can play movies at all.
bool VP6DecoderAvailable();

} // namespace port_movie
