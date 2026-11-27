// See AudioMixer.h.

#include "AudioMixer.h"
#include "EAAudioFormat.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace port_audio {

namespace {

struct Voice {
    VoiceId id = 0;
    SampleRef sample;
    double position = 0.0; // in source frames
    float volume = 1.0f;
    float pan = 0.0f;
    float pitch = 1.0f;
    long loop_start = -1;
    long loop_frames = -1;
};

} // namespace

struct Mixer::Impl {
    mutable std::mutex mutex;
    std::vector<Voice> voices;
    VoiceId next_id = 1;
    float master = 1.0f;

    Voice *Find(VoiceId id) {
        for (Voice &v : voices) {
            if (v.id == id) {
                return &v;
            }
        }
        return nullptr;
    }
};

Mixer::Mixer(int output_rate) : mImpl(new Impl), mOutputRate(output_rate) {}

Mixer::~Mixer() = default;

VoiceId Mixer::Play(SampleRef sample, float volume, float pan, float pitch, long loop_start, long loop_frames) {
    if (!sample || sample->Frames() == 0 || sample->channels < 1 || sample->channels > 2) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mImpl->mutex);
    Voice v;
    v.id = mImpl->next_id++;
    if (mImpl->next_id == 0) {
        mImpl->next_id = 1;
    }
    v.sample = std::move(sample);
    v.volume = volume;
    v.pan = pan;
    v.pitch = pitch;
    v.loop_start = loop_start;
    v.loop_frames = loop_frames;
    mImpl->voices.push_back(std::move(v));
    return mImpl->voices.back().id;
}

void Mixer::Stop(VoiceId id) {
    std::lock_guard<std::mutex> lock(mImpl->mutex);
    auto &vs = mImpl->voices;
    vs.erase(std::remove_if(vs.begin(), vs.end(), [id](const Voice &v) { return v.id == id; }), vs.end());
}

void Mixer::StopAll() {
    std::lock_guard<std::mutex> lock(mImpl->mutex);
    mImpl->voices.clear();
}

bool Mixer::IsPlaying(VoiceId id) const {
    std::lock_guard<std::mutex> lock(mImpl->mutex);
    for (const Voice &v : mImpl->voices) {
        if (v.id == id) {
            return true;
        }
    }
    return false;
}

void Mixer::SetVolume(VoiceId id, float volume) {
    std::lock_guard<std::mutex> lock(mImpl->mutex);
    if (Voice *v = mImpl->Find(id)) {
        v->volume = volume;
    }
}

void Mixer::SetPan(VoiceId id, float pan) {
    std::lock_guard<std::mutex> lock(mImpl->mutex);
    if (Voice *v = mImpl->Find(id)) {
        v->pan = pan;
    }
}

void Mixer::SetPitch(VoiceId id, float pitch) {
    std::lock_guard<std::mutex> lock(mImpl->mutex);
    if (Voice *v = mImpl->Find(id)) {
        v->pitch = pitch;
    }
}

void Mixer::SetMasterVolume(float volume) {
    std::lock_guard<std::mutex> lock(mImpl->mutex);
    mImpl->master = volume;
}

void Mixer::Mix(float *out, size_t frames) {
    std::fill(out, out + frames * 2, 0.0f);
    std::lock_guard<std::mutex> lock(mImpl->mutex);
    const float scale = 1.0f / 32768.0f;
    for (Voice &v : mImpl->voices) {
        const Sample &s = *v.sample;
        const size_t total = s.Frames();
        const double step = static_cast<double>(s.rate) / mOutputRate * v.pitch;
        // Constant-power pan.
        float angle = (std::clamp(v.pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
        float gl = std::cos(angle) * v.volume * mImpl->master * 1.41421356f; // unity at centre
        float gr = std::sin(angle) * v.volume * mImpl->master * 1.41421356f; // unity at centre
        for (size_t f = 0; f < frames; f++) {
            size_t i0 = static_cast<size_t>(v.position);
            if (i0 + 1 >= total) {
                if (v.loop_frames > 0) {
                    v.position = static_cast<double>(v.loop_start) + std::fmod(v.position - v.loop_start, static_cast<double>(v.loop_frames));
                    i0 = static_cast<size_t>(v.position);
                } else {
                    v.sample.reset(); // finished
                    break;
                }
            }
            float t = static_cast<float>(v.position - i0);
            float l;
            float r;
            if (s.channels == 2) {
                l = (s.pcm[i0 * 2] + (s.pcm[i0 * 2 + 2] - s.pcm[i0 * 2]) * t) * scale;
                r = (s.pcm[i0 * 2 + 1] + (s.pcm[i0 * 2 + 3] - s.pcm[i0 * 2 + 1]) * t) * scale;
            } else {
                l = r = (s.pcm[i0] + (s.pcm[i0 + 1] - s.pcm[i0]) * t) * scale;
            }
            out[f * 2] += l * gl;
            out[f * 2 + 1] += r * gr;
            v.position += step;
            if (v.loop_frames > 0 && v.position >= v.loop_start + v.loop_frames) {
                v.position -= v.loop_frames;
            }
        }
    }
    auto &vs = mImpl->voices;
    vs.erase(std::remove_if(vs.begin(), vs.end(), [](const Voice &v) { return !v.sample; }), vs.end());
}

void RunAudioSelfTest() {
    const char *path = std::getenv("NFSMW_TEST_AUDIO");
    Mixer *mixer = GetMixer();
    if (path == nullptr || mixer == nullptr) {
        return;
    }
    FILE *f = std::fopen(path, "rb");
    if (f == nullptr) {
        std::fprintf(stderr, "[audio] can't open %s\n", path);
        return;
    }
    std::vector<uint8_t> data(8u << 20);
    data.resize(std::fread(data.data(), 1, data.size(), f));
    std::fclose(f);
    size_t o = 0;
    while (o + 4 <= data.size() && std::memcmp(&data[o], "SCHl", 4) != 0) {
        o++;
    }
    auto sample = std::make_shared<Sample>();
    ea_audio::PatchHeader header;
    int segments = 0;
    // Chain consecutive SCHl..SCEl segments with the same format, up to ~60 s.
    while (o + 4 <= data.size()) {
        if (std::memcmp(&data[o], "SCHl", 4) != 0) {
            o++;
            continue;
        }
        ea_audio::PatchHeader h;
        std::vector<int16_t> pcm;
        size_t consumed = 0;
        if (!ea_audio::DecodeStream(data.data() + o, data.size() - o, &h, &pcm, &consumed) || consumed == 0) {
            break;
        }
        if (segments == 0) {
            header = h;
        } else if (h.channels != header.channels || h.sample_rate != header.sample_rate) {
            break;
        }
        sample->pcm.insert(sample->pcm.end(), pcm.begin(), pcm.end());
        segments++;
        o += consumed;
        if (sample->pcm.size() / header.channels > static_cast<size_t>(header.sample_rate) * 60) {
            break;
        }
    }
    if (segments == 0) {
        std::fprintf(stderr, "[audio] no EA stream in %s\n", path);
        return;
    }
    sample->channels = header.channels;
    sample->rate = header.sample_rate;
    std::fprintf(stderr, "[audio] %d segment(s)\n", segments);
    std::fprintf(stderr, "[audio] self test: %d ch, %d Hz, %.1f s\n", sample->channels, sample->rate,
                 static_cast<double>(sample->Frames()) / sample->rate);
    mixer->Play(sample, 0.8f, 0.0f, 1.0f, 0, static_cast<long>(sample->Frames()));
}

} // namespace port_audio
