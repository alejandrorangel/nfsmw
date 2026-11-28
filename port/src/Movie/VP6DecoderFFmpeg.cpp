// VP6Decoder on libavcodec (see VP6Decoder.h). Built only when CMake found
// FFmpeg (NFSMW_PORT_FFMPEG); FFmpeg is LGPL and linked dynamically.

#include "VP6Decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include <cstdio>
#include <cstring>

namespace port_movie {

namespace {

class FFmpegVP6 : public VP6Decoder {
  public:
    ~FFmpegVP6() override {
        av_frame_free(&mFrame);
        av_packet_free(&mPacket);
        avcodec_free_context(&mContext);
    }

    bool Open(int width, int height) {
        const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_VP6);
        if (codec == nullptr) {
            std::fprintf(stderr, "[movie] libavcodec has no VP6 decoder\n");
            return false;
        }
        mContext = avcodec_alloc_context3(codec);
        mPacket = av_packet_alloc();
        mFrame = av_frame_alloc();
        if (mContext == nullptr || mPacket == nullptr || mFrame == nullptr) {
            return false;
        }
        mContext->width = width;
        mContext->height = height;
        mContext->thread_count = 1;
        return avcodec_open2(mContext, codec, nullptr) == 0;
    }

    bool Decode(const uint8_t *data, size_t size, YUVFrame *out) override {
        // libavcodec reads up to AV_INPUT_BUFFER_PADDING_SIZE past the end.
        if (av_new_packet(mPacket, static_cast<int>(size)) != 0) {
            return false;
        }
        std::memcpy(mPacket->data, data, size);
        int sent = avcodec_send_packet(mContext, mPacket);
        av_packet_unref(mPacket);
        if (sent != 0 || avcodec_receive_frame(mContext, mFrame) != 0) {
            return false;
        }
        if (mFrame->format != AV_PIX_FMT_YUV420P) {
            std::fprintf(stderr, "[movie] unexpected VP6 pixel format %d\n", mFrame->format);
            return false;
        }
        for (int i = 0; i < 3; i++) {
            out->plane[i] = mFrame->data[i];
            out->stride[i] = mFrame->linesize[i];
        }
        out->width = mFrame->width;
        out->height = mFrame->height;
        return true;
    }

  private:
    AVCodecContext *mContext = nullptr;
    AVPacket *mPacket = nullptr;
    AVFrame *mFrame = nullptr;
};

} // namespace

VP6Decoder *VP6Decoder::Create(int width, int height) {
    FFmpegVP6 *decoder = new FFmpegVP6();
    if (!decoder->Open(width, height)) {
        delete decoder;
        return nullptr;
    }
    return decoder;
}

bool VP6DecoderAvailable() {
    return true;
}

} // namespace port_movie
