// VP6Decoder without FFmpeg: no decoder, so movies are skipped.

#include "VP6Decoder.h"

namespace port_movie {

VP6Decoder *VP6Decoder::Create(int, int) {
    return nullptr;
}

bool VP6DecoderAvailable() {
    return false;
}

} // namespace port_movie
