// CoreAudio output for the native audio mixer: the default output unit,
// 48 kHz stereo float, pulling from port_audio::Mixer::Mix.

#include "../Audio/AudioMixer.h"

#include <AudioToolbox/AudioToolbox.h>

#include <cstdio>
#include <memory>

namespace port_audio {

namespace {

const int kOutputRate = 48000;

std::unique_ptr<Mixer> gMixer;
AudioComponentInstance gUnit = nullptr;

OSStatus Render(void *, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32, UInt32 frames, AudioBufferList *buffers) {
    float *out = static_cast<float *>(buffers->mBuffers[0].mData);
    if (gMixer) {
        gMixer->Mix(out, frames);
    } else {
        std::fill(out, out + frames * 2, 0.0f);
    }
    return noErr;
}

} // namespace

Mixer *GetMixer() {
    return gMixer.get();
}

bool StartAudioOutput() {
    if (gUnit != nullptr) {
        return true;
    }
    AudioComponentDescription desc = {};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (comp == nullptr || AudioComponentInstanceNew(comp, &gUnit) != noErr) {
        std::fprintf(stderr, "[audio] no default output unit\n");
        gUnit = nullptr;
        return false;
    }
    AudioStreamBasicDescription fmt = {};
    fmt.mSampleRate = kOutputRate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 32;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 8;
    fmt.mBytesPerPacket = 8;
    AURenderCallbackStruct cb = {Render, nullptr};
    gMixer.reset(new Mixer(kOutputRate));
    if (AudioUnitSetProperty(gUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt)) != noErr ||
        AudioUnitSetProperty(gUnit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb)) != noErr ||
        AudioUnitInitialize(gUnit) != noErr || AudioOutputUnitStart(gUnit) != noErr) {
        std::fprintf(stderr, "[audio] failed to start the output unit\n");
        AudioComponentInstanceDispose(gUnit);
        gUnit = nullptr;
        gMixer.reset();
        return false;
    }
    return true;
}

void StopAudioOutput() {
    if (gUnit != nullptr) {
        AudioOutputUnitStop(gUnit);
        AudioUnitUninitialize(gUnit);
        AudioComponentInstanceDispose(gUnit);
        gUnit = nullptr;
    }
    gMixer.reset();
}

} // namespace port_audio
