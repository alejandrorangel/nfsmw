// Native FMV playback: RCMP::AV_PLAYER (EA's rcmp 4.00.11 player, statically
// linked into speed.exe 1.3) and the PC movie texture glue, for the game's
// own MoviePlayer (Frontend/MoviePlayer/MoviePlayer.cpp).
//
// The PC movies (MOVIES/*.vp6) are EA "MV" files: an MVhd header (codec
// 'VP60', width, height, frame count, largest chunk, frame rate as
// denominator/numerator), then chunks of { tag, u32 little-endian size
// including the 8-byte header }: MV0K (key frame) / MV0F (inter frame) VP6
// pictures, and the EA audio stream SCHl (GSTR patch header) / SCCl / SCDl
// (EA-XA R2 frames, as in the music) / SCEl. VP6 is decoded with libavcodec
// (VP6Decoder.h); without FFmpeg every movie is skipped, as before.
//
// Timing follows the PC player: AV_PLAYER::IsTimeForDecode (0x7F81DF) sets
// the goal frame to (latency + SyncedAudioTime()) * frame rate / 1000 and
// asks for a new picture while the current frame is behind it; GetFrame
// (0x7F7BBA) decodes up to the goal (skipping pictures when late). The clock
// is the movie's audio as played by the mixer, or real time when the sound
// is off (no audio player: IsAudioFinished (0x7F7C16) is then true and the
// movie ends with its last picture).
//
// The picture: PC converts each frame (still bottom-up, as VP6 codes it) into
// a 32-bit A8R8G8B8 "MovieTexture" (PlatSetFirstMovieFrame 0x6E1880,
// MoviePlayer::FillInTextureInfo 0x6CAEC0) and cFEngRender::RenderMovie
// (0x599DB0) flips it with its texture coordinates. Natively the frames go
// bottom-up into a small ring of Metal textures, so a texture is never
// rewritten while a frame that shows it may still be drawn.
//
// NFSMW_NO_MOVIES=1 skips movies (as does the benchmark, so its runs are
// unchanged); NFSMW_TRACE_MOVIES=1 logs them.

#include "Speed/Indep/Src/Frontend/MoviePlayer/MoviePlayer.hpp"
#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/Frontend/Localization/Localize.hpp"
#include "Speed/Indep/bWare/Inc/Strings.hpp"
#include "rcmp/av/avplayer.h"

#include "../Audio/AudioMixer.h"
#include "../Audio/EAAudioFormat.h"
#include "../Mac/MetalBridge.h"
#include "../Platform/Benchmark.h"
#include "VP6Decoder.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <strings.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

std::string PortResolveDataPath(const char *name); // RealCoreNative.cpp

namespace {

bool TraceMovies() {
    static const bool on = std::getenv("NFSMW_TRACE_MOVIES") != nullptr;
    return on;
}

bool MoviesDisabled() {
    if (PortBenchmarkActive()) {
        return true; // the benchmark's flows skip the movies, as they always have
    }
    const char *off = std::getenv("NFSMW_NO_MOVIES");
    return off != nullptr && std::strcmp(off, "0") != 0;
}

uint32_t LE32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t BE32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

double NowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// BT.601 limited range YUV 4:2:0 to BGRA (alpha 0xFF), written bottom-up.
void ConvertFrame(const port_movie::YUVFrame &f, int width, int height, uint32_t *dst) {
    for (int y = 0; y < height; y++) {
        const uint8_t *py = f.plane[0] + static_cast<size_t>(y) * f.stride[0];
        const uint8_t *pu = f.plane[1] + static_cast<size_t>(y / 2) * f.stride[1];
        const uint8_t *pv = f.plane[2] + static_cast<size_t>(y / 2) * f.stride[2];
        uint32_t *row = dst + static_cast<size_t>(height - 1 - y) * width;
        for (int x = 0; x < width; x++) {
            int c = (py[x] - 16) * 298;
            int d = pu[x / 2] - 128;
            int e = pv[x / 2] - 128;
            int r = (c + 409 * e + 128) >> 8;
            int g = (c - 100 * d - 208 * e + 128) >> 8;
            int b = (c + 516 * d + 128) >> 8;
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            row[x] = 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        }
    }
}

} // namespace

namespace RCMP {

// What FRAME::GetShape() points at natively: the converted picture. Only
// this file reads it (PlatSetFirstMovieFrame, FillInTextureInfo).
struct MovieShape {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels; // A8R8G8B8 (BGRA in memory), bottom-up rows
};

struct PortFrame : public FRAME {
    void Set(MovieShape *shape) {
        m_FrameType = FRAME_VP6;
        m_Shp = reinterpret_cast<RealShape::Shape *>(shape);
    }
};

struct PortMovie {
    std::string name;
    FILE *file = nullptr;
    bool eof = false;
    int width = 0;
    int height = 0;
    unsigned int frame_count = 0;
    double fps = 0.0;
    std::unique_ptr<port_movie::VP6Decoder> decoder;
    std::deque<std::vector<uint8_t>> video; // undecoded pictures
    unsigned int decoded = 0;               // pictures decoded so far
    unsigned int skipped = 0;               // decoded but never shown (late)
    MovieShape shape;
    PortFrame frame;
    std::vector<uint8_t> chunk;

    // Audio, filled here (game thread) and played by the mixer's thread.
    port_audio::Mixer *mixer = nullptr;
    uint32_t stream_id = 0;
    std::mutex lock;
    int channels = 0;
    int rate = 0;
    ea_audio::XaChannel xa[8];
    std::deque<int16_t> pcm;   // interleaved, not played yet
    double source_pos = 0.0;   // fractional read position into pcm (frames)
    uint64_t played = 0;       // source frames played
    bool audio_ended = false;  // SCEl (or the end of the file) reached
    bool paused = true;
    float volume = 1.0f;
    std::vector<int16_t> scratch;

    // The clock without audio, and after the audio ran out.
    double clock_base_ms = 0.0;  // clock value at clock_start
    double clock_start = -1.0;   // NowMs() when it last resumed, < 0 while paused
    double audio_end_ms = -1.0;  // clock when the played audio ran out

    ~PortMovie() {
        if (stream_id != 0) {
            mixer->RemoveStream(stream_id);
        }
        if (file != nullptr) {
            std::fclose(file);
        }
    }

    bool Open(const char *filename) {
        name = filename;
        std::string path = PortResolveDataPath(filename);
        file = std::fopen(path.c_str(), "rb");
        if (file == nullptr) {
            std::fprintf(stderr, "[movie] can't open %s (%s)\n", filename, path.c_str());
            return false;
        }
        uint8_t h[32];
        if (std::fread(h, 1, sizeof(h), file) != sizeof(h) || std::memcmp(h, "MVhd", 4) != 0 || LE32(h + 4) < 32) {
            std::fprintf(stderr, "[movie] %s: not an MV file\n", filename);
            return false;
        }
        if (std::memcmp(h + 8, "06PV", 4) != 0) { // 'VP60', little-endian
            std::fprintf(stderr, "[movie] %s: unsupported codec %.4s\n", filename, h + 8);
            return false;
        }
        width = h[12] | (h[13] << 8);
        height = h[14] | (h[15] << 8);
        frame_count = LE32(h + 16);
        uint32_t den = LE32(h + 24);
        uint32_t num = LE32(h + 28);
        fps = num != 0 ? static_cast<double>(den) / num : 0.0;
        std::fseek(file, LE32(h + 4), SEEK_SET);
        if (width <= 0 || height <= 0 || fps <= 0.0) {
            return false;
        }
        decoder.reset(port_movie::VP6Decoder::Create(width, height));
        if (!decoder) {
            return false;
        }
        shape.width = width;
        shape.height = height;
        shape.pixels.assign(static_cast<size_t>(width) * height, 0xFF000000u);
        frame.Set(&shape);
        return true;
    }

    size_t BufferedAudioFrames() {
        std::lock_guard<std::mutex> guard(lock);
        return channels > 0 ? pcm.size() / channels : 0;
    }

    void DecodeAudio(const uint8_t *b, size_t len) {
        if (channels < 1 || len < 12 + 4u * channels) {
            return;
        }
        uint32_t samples = BE32(b + 8);
        const uint8_t *chan_data = b + 12 + 4 * channels;
        size_t chan_area = len - (12 + 4 * channels);
        scratch.assign(static_cast<size_t>(samples) * channels, 0);
        for (int c = 0; c < channels; c++) {
            uint32_t start = BE32(b + 12 + 4 * c);
            uint32_t end = c + 1 < channels ? BE32(b + 12 + 4 * (c + 1)) : static_cast<uint32_t>(chan_area);
            if (start > end || end > chan_area) {
                return;
            }
            ea_audio::DecodeXaR2(xa[c], chan_data + start, end - start, scratch.data() + c, samples, channels);
        }
        std::lock_guard<std::mutex> guard(lock);
        pcm.insert(pcm.end(), scratch.begin(), scratch.end());
    }

    // Reads one chunk; false at the end of the file.
    bool ReadChunk() {
        uint8_t h[8];
        if (eof || std::fread(h, 1, 8, file) != 8) {
            eof = true;
            return false;
        }
        uint32_t size = LE32(h + 4);
        if (size < 8 || size > (64u << 20)) {
            eof = true;
            return false;
        }
        chunk.resize(size);
        std::memcpy(chunk.data(), h, 8);
        if (std::fread(chunk.data() + 8, 1, size - 8, file) != size - 8) {
            eof = true;
            return false;
        }
        const uint8_t *b = chunk.data();
        if (std::memcmp(b, "MV0K", 4) == 0 || std::memcmp(b, "MV0F", 4) == 0) {
            video.emplace_back(b + 8, b + size);
        } else if (std::memcmp(b, "SCHl", 4) == 0) {
            ea_audio::PatchHeader header;
            if (size > 16 && ea_audio::ParsePatchHeader(b + 16, size - 16, &header) != 0 && header.channels >= 1 &&
                header.channels <= 8 && channels == 0) {
                channels = header.channels;
                // No rate tag: 48000 Hz for version 3 streams (all the PC movies).
                rate = header.sample_rate > 0 ? header.sample_rate : (header.version == 3 ? 48000 : 22050);
            }
        } else if (std::memcmp(b, "SCDl", 4) == 0) {
            if (mixer != nullptr) {
                DecodeAudio(b, size);
            }
        } else if (std::memcmp(b, "SCEl", 4) == 0) {
            std::lock_guard<std::mutex> guard(lock);
            audio_ended = true;
        }
        return true;
    }

    // Keeps the next picture and about a second of audio read ahead.
    void Pump() {
        for (int guard = 0; guard < 4096; guard++) {
            bool need_video = video.empty();
            bool need_audio = mixer != nullptr && !audio_ended && (channels == 0 || BufferedAudioFrames() < static_cast<size_t>(rate));
            if ((!need_video && !need_audio) || video.size() > 240 || !ReadChunk()) {
                break;
            }
        }
        if (eof) {
            std::lock_guard<std::mutex> guard(lock);
            audio_ended = true;
        }
    }

    // Decodes the next picture into shape; false at the end of the movie.
    bool DecodeNext(bool convert) {
        while (true) {
            Pump();
            if (video.empty()) {
                return false;
            }
            std::vector<uint8_t> packet = std::move(video.front());
            video.pop_front();
            port_movie::YUVFrame yuv;
            if (!decoder->Decode(packet.data(), packet.size(), &yuv)) {
                std::fprintf(stderr, "[movie] %s: picture %u didn't decode\n", name.c_str(), decoded);
                decoded++;
                continue;
            }
            decoded++;
            if (convert && yuv.width >= width && yuv.height >= height) {
                ConvertFrame(yuv, width, height, shape.pixels.data());
            }
            return true;
        }
    }

    // Mixer thread: plays the buffered audio.
    static void Fill(void *user, float *out, size_t frames, int output_rate) {
        static_cast<PortMovie *>(user)->FillAudio(out, frames, output_rate);
    }

    void FillAudio(float *out, size_t frames, int output_rate) {
        std::lock_guard<std::mutex> guard(lock);
        if (paused || channels < 1 || rate <= 0) {
            return;
        }
        const double step = static_cast<double>(rate) / output_rate;
        const float gain = volume / 32768.0f;
        size_t available = pcm.size() / channels;
        for (size_t f = 0; f < frames; f++) {
            size_t i = static_cast<size_t>(source_pos);
            if (i >= available) {
                break; // starved (or done)
            }
            float l = pcm[i * channels];
            float r = channels > 1 ? pcm[i * channels + 1] : l;
            out[f * 2] += l * gain;
            out[f * 2 + 1] += r * gain;
            source_pos += step;
        }
        size_t used = std::min(static_cast<size_t>(source_pos), available);
        pcm.erase(pcm.begin(), pcm.begin() + used * channels);
        source_pos -= used;
        played += used;
    }

    bool AudioPlaying() {
        return mixer != nullptr && channels > 0;
    }

    bool AudioDone() {
        std::lock_guard<std::mutex> guard(lock);
        return audio_ended && pcm.size() / std::max(channels, 1) <= static_cast<size_t>(source_pos);
    }

    double WallClock() {
        return clock_base_ms + (clock_start >= 0.0 ? NowMs() - clock_start : 0.0);
    }

    // AV_PLAYER::SyncedAudioTime: ms of the movie played.
    double ClockMs() {
        if (!AudioPlaying()) {
            return WallClock();
        }
        double audio_ms;
        {
            std::lock_guard<std::mutex> guard(lock);
            audio_ms = static_cast<double>(played) * 1000.0 / rate;
        }
        if (AudioDone()) { // a picture track longer than the audio runs on in real time
            if (audio_end_ms < 0.0) {
                audio_end_ms = audio_ms;
                clock_base_ms = audio_ms;
                clock_start = paused ? -1.0 : NowMs();
            }
            return WallClock();
        }
        return audio_ms;
    }

    void SetPaused(bool on) {
        if (on && clock_start >= 0.0) {
            clock_base_ms += NowMs() - clock_start;
            clock_start = -1.0;
        } else if (!on && clock_start < 0.0) {
            clock_start = NowMs();
        }
        std::lock_guard<std::mutex> guard(lock);
        paused = on;
    }
};

// rcmp's frames are handed back through the player's DECODER; the native
// player owns its single frame, so there is nothing to release.
void DECODER::ReleaseFrame(FRAME *Frame) {}

CHUNK::CHUNK() : m_UserChunkData(nullptr), m_DataToDecode(nullptr), m_DataToDecodeSize(0) {}

AV_PLAYER::AV_PLAYER(const char *VideoFileName, int BufferSize, LOAD_ENUM LoadMode, SOUND_ENUM SndMode)
    : m_data_streamer(nullptr) {
    m_ap = nullptr;
    m_VideoFileName = VideoFileName;
    m_AudioFileName = VideoFileName;
    m_SndFromDifferentFile = false;
    m_LoadMode = LoadMode;
    m_SndMode = SndMode;
    m_VideoLatencyInMs = 0;
    m_CurFrame = 0;
    m_GoalFrame = 0.0f;
    // MoviePlayer only calls ReleaseFrame through it (a non-virtual no-op),
    // but it must not be null.
    m_pdecoder = reinterpret_cast<DECODER *>(this);
    m_SubtitleArray = nullptr;
    m_CurRCMPFrame = nullptr;
    m_Port = nullptr;
    if (MoviesDisabled() || !port_movie::VP6DecoderAvailable()) {
        return;
    }
    PortMovie *movie = new PortMovie();
    if (SndMode == SOUND_ON) {
        movie->mixer = port_audio::GetMixer();
    }
    if (!movie->Open(VideoFileName)) {
        delete movie;
        return;
    }
    m_Port = movie;
    if (TraceMovies()) {
        std::fprintf(stderr, "[movie] open %s: %dx%d, %u frames at %.3f fps, sound %s\n", VideoFileName, movie->width,
                     movie->height, movie->frame_count, movie->fps, movie->mixer ? "on" : "off");
    }
}

AV_PLAYER::~AV_PLAYER() {
    if (m_Port != nullptr && TraceMovies()) {
        std::fprintf(stderr, "[movie] close %s at frame %u, clock %.0f ms\n", m_Port->name.c_str(), m_CurFrame, m_Port->ClockMs());
    }
    delete m_Port;
    m_Port = nullptr;
}

FRAME *AV_PLAYER::GetFirstFrame(unsigned int MaxFramesOutstanding, int VideoLatencyInMs) {
    PortMovie *movie = m_Port;
    if (movie == nullptr) {
        return nullptr;
    }
    m_VideoLatencyInMs = VideoLatencyInMs;
    m_CurFrame = 0;
    m_GoalFrame = 0.0f;
    if (!movie->DecodeNext(true)) {
        return nullptr;
    }
    if (movie->mixer != nullptr && movie->channels > 0) {
        movie->stream_id = movie->mixer->AddStream(&PortMovie::Fill, movie);
    }
    movie->SetPaused(false);
    m_CurRCMPFrame = &movie->frame;
    return m_CurRCMPFrame;
}

FRAME *AV_PLAYER::GetFrame(float GoalFrame) {
    PortMovie *movie = m_Port;
    m_GoalFrame = GoalFrame;
    m_CurRCMPFrame = nullptr;
    if (movie == nullptr) {
        return nullptr;
    }
    unsigned int goal = GoalFrame > 0.0f ? static_cast<unsigned int>(GoalFrame) : 0;
    unsigned int target = std::max(m_CurFrame + 1, goal);
    while (movie->decoded <= target) {
        if (!movie->DecodeNext(movie->decoded == target)) {
            return nullptr;
        }
    }
    if (target > m_CurFrame + 1) {
        movie->skipped += target - m_CurFrame - 1;
    }
    m_CurFrame = movie->decoded - 1;
    m_CurRCMPFrame = &movie->frame;
    if (TraceMovies() && m_CurFrame % 150 == 0) {
        std::fprintf(stderr, "[movie] frame %u (%.0f ms) at clock %.0f ms, %u skipped\n", m_CurFrame,
                     m_CurFrame * 1000.0 / movie->fps, movie->ClockMs(), movie->skipped);
    }
    return m_CurRCMPFrame;
}

unsigned int AV_PLAYER::SyncedAudioTime() {
    return m_Port != nullptr ? static_cast<unsigned int>(m_Port->ClockMs()) : 0;
}

bool AV_PLAYER::IsTimeForDecode() {
    if (m_Port == nullptr) {
        return false;
    }
    m_GoalFrame = static_cast<float>(static_cast<double>(m_VideoLatencyInMs + SyncedAudioTime()) * m_Port->fps * 0.001);
    return static_cast<float>(m_CurFrame) < m_GoalFrame;
}

bool AV_PLAYER::IsAudioFinished() {
    if (m_Port == nullptr || !m_Port->AudioPlaying()) {
        return true; // no audio player
    }
    return m_Port->AudioDone();
}

int AV_PLAYER::Pause() {
    if (m_Port != nullptr) {
        m_Port->SetPaused(true);
    }
    return 0;
}

int AV_PLAYER::UnPause() {
    if (m_Port != nullptr) {
        m_Port->SetPaused(false);
    }
    return 0;
}

// Vol: 0..127 (MoviePlayer's MovieVolumeArray).
int AV_PLAYER::SetVol(unsigned int Vol) {
    if (m_Port != nullptr) {
        std::lock_guard<std::mutex> guard(m_Port->lock);
        m_Port->volume = std::min(Vol, 127u) / 127.0f;
    }
    return 0;
}

} // namespace RCMP

// ---------------------------------------------------------------------------
// The movie texture.
// ---------------------------------------------------------------------------

namespace {

const int kRingSize = 4;

TextureInfoPlatInfo gMoviePlatInfo;
void *gMovieRing[kRingSize];
int gMovieRingNext = 0;
int gMovieRingWidth = 0;
int gMovieRingHeight = 0;

void ReleaseRing() {
    for (void *&texture : gMovieRing) {
        if (texture != nullptr) {
            PortMetalReleaseTexture(texture);
            texture = nullptr;
        }
    }
    gMovieRingWidth = gMovieRingHeight = 0;
    gMoviePlatInfo.MetalTexture = nullptr;
}

void UploadMovieFrame(RCMP::MovieShape *shape) {
    if (shape == nullptr || shape->width <= 0) {
        return;
    }
    if (shape->width != gMovieRingWidth || shape->height != gMovieRingHeight) {
        ReleaseRing();
        for (void *&texture : gMovieRing) {
            texture = PortMetalCreateTexture(shape->width, shape->height, 1, PORT_PIXEL_BGRA8, "MovieTexture");
        }
        gMovieRingWidth = shape->width;
        gMovieRingHeight = shape->height;
    }
    void *texture = gMovieRing[gMovieRingNext];
    gMovieRingNext = (gMovieRingNext + 1) % kRingSize;
    if (texture == nullptr) {
        return;
    }
    PortMetalUploadMip(texture, 0, shape->width, shape->height, shape->pixels.data(), static_cast<size_t>(shape->width) * 4);
    gMoviePlatInfo.MetalTexture = texture;
}

} // namespace

// PC 0x6CAF50.
unsigned int RCMP_GetMaxFramesOutStanding() {
    return 2;
}

// PC 0x6E1880: sets up "MovieTexture" (A8R8G8B8, the movie's size) and shows
// the first picture.
void PlatSetFirstMovieFrame(TextureInfo *texture_info, RealShape::Shape *yuv_shape, bool isVP6Movie) {
    RCMP::MovieShape *shape = reinterpret_cast<RCMP::MovieShape *>(yuv_shape);
    texture_info->SetPlatInfo(&gMoviePlatInfo);
    gMoviePlatInfo.Format = PORT_D3DFMT_A8R8G8B8;
    std::memset(texture_info->DebugName, 0, sizeof(texture_info->DebugName));
    std::strcpy(texture_info->DebugName, "MovieTexture");
    texture_info->NameHash = bStringHash("MovieTexture");
    texture_info->Width = static_cast<int16>(shape->width);
    texture_info->Height = static_cast<int16>(shape->height);
    texture_info->ImageCompressionType = 0x20;
    texture_info->NumMipMapLevels = 1;
    texture_info->AlphaUsageType = TEXUSAGE_NONE;
    texture_info->AlphaBlendType = TEXBLEND_SRCCOPY;
    texture_info->ImageData = nullptr; // the pixels live in the Metal textures
    texture_info->ImageSize = shape->width * shape->height * 4;
    UploadMovieFrame(shape);
}

// PC 0x6CAEC0: copies the new picture into the movie texture.
void MoviePlayer::FillInTextureInfo(uint32 *frame_address, TextureInfo *texture_info, RealShape::Shape *shape) {
    UploadMovieFrame(reinterpret_cast<RCMP::MovieShape *>(shape));
}

void PlatFinishMovie() {
    ReleaseRing();
}

// FEngMovieStarter::Callback (FEObjectCallbacks.cpp): NFSMW_DEBUG_MOVIE=<name>
// (e.g. blacklist_01, storyfmv_bus12) plays that movie in place of the first
// one the game starts (the EA logo at boot).
const char *PortDebugMovieName(const char *movie_name) {
    static bool used = false;
    const char *name = std::getenv("NFSMW_DEBUG_MOVIE");
    if (used || name == nullptr || name[0] == '\0') {
        return movie_name;
    }
    used = true;
    return name;
}

// FEngMovieStarter::Callback (FEObjectCallbacks.cpp), when neither the
// current language's nor English's movie exists: the installed one
// ("MOVIES\<basename>_<language>_<pal|ntsc>.vp6"), preferring the current
// language. Returns false (buffer unchanged) when there is none.
bool PortFindInstalledMovie(const char *basename, char *buffer, int bufsize) {
    if (MoviesDisabled() || !port_movie::VP6DecoderAvailable()) {
        return false; // no file: the screen finishes at once, as before movies were ported
    }
    std::string dir = PortResolveDataPath("MOVIES");
    DIR *d = opendir(dir.c_str());
    if (d == nullptr) {
        return false;
    }
    const char *language = GetLanguageName(GetCurrentLanguage());
    size_t base_len = std::strlen(basename);
    std::string best;
    bool best_is_language = false;
    while (dirent *e = readdir(d)) {
        const char *name = e->d_name;
        size_t len = std::strlen(name);
        if (len < base_len + 6 || strncasecmp(name, basename, base_len) != 0 || name[base_len] != '_' ||
            strcasecmp(name + len - 4, ".vp6") != 0) {
            continue;
        }
        // The rest is exactly "<language>_<region>.vp6".
        std::string rest(name + base_len + 1, len - base_len - 5);
        size_t underscore = rest.find('_');
        if (underscore == std::string::npos || rest.find('_', underscore + 1) != std::string::npos) {
            continue;
        }
        bool is_language = language != nullptr && strcasecmp(rest.substr(0, underscore).c_str(), language) == 0;
        if (best.empty() || (is_language && !best_is_language)) {
            best = name;
            best_is_language = is_language;
        }
    }
    closedir(d);
    if (best.empty() || static_cast<int>(best.size() + 8) > bufsize) {
        return false;
    }
    std::snprintf(buffer, bufsize, "MOVIES\\%s", best.c_str());
    if (TraceMovies()) {
        std::fprintf(stderr, "[movie] %s: using the installed %s\n", basename, buffer);
    }
    return true;
}
