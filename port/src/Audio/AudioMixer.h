// Software mixer for the native audio backend: voices of 16-bit PCM (mono or
// stereo, any rate) resampled into the output stream with volume, pan, pitch
// and looping. The platform output (AudioOutputMac.mm) pulls from Mix() on
// its own thread; voice control calls come from the game thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace port_audio {

struct Sample {
    std::vector<int16_t> pcm; // interleaved
    int channels = 1;
    int rate = 22050;
    size_t Frames() const {
        return channels > 0 ? pcm.size() / channels : 0;
    }
};

using SampleRef = std::shared_ptr<const Sample>;

// Voice handle; 0 is never a valid voice.
using VoiceId = uint32_t;

class Mixer {
  public:
    explicit Mixer(int output_rate);
    ~Mixer();

    int OutputRate() const {
        return mOutputRate;
    }

    // Starts a voice; loop_start/loop_frames < 0 means no loop.
    VoiceId Play(SampleRef sample, float volume, float pan, float pitch, long loop_start = -1, long loop_frames = -1);
    void Stop(VoiceId id);
    void StopAll();
    bool IsPlaying(VoiceId id) const;
    // Current playback position in source frames, or -1 once the voice ended.
    long Position(VoiceId id) const;
    void SetVolume(VoiceId id, float volume);
    void SetPan(VoiceId id, float pan); // -1 left .. 1 right
    void SetPitch(VoiceId id, float pitch);
    void SetMasterVolume(float volume);

    // Called by the output thread: writes frames of interleaved stereo float.
    // NFSMW_AUDIO_DUMP=<file.wav> also records the mix (16-bit stereo).
    void Mix(float *out, size_t frames);

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
    int mOutputRate;
};

// The process-wide mixer, created by StartAudioOutput (null when audio is off).
Mixer *GetMixer();

// Starts/stops the platform output device and the global mixer.
bool StartAudioOutput();
void StopAudioOutput();

// Debug: NFSMW_TEST_AUDIO=<path to an EA stream file, e.g. MW_Music.mus>
// decodes the first stream segment and plays it on startup.
void RunAudioSelfTest();

} // namespace port_audio
