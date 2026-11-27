// Native snd9 (EA "Snd" 9.x) for the PC data: AEMS module banks and their
// sample banks, played on port_audio::Mixer.
//
// AEMS ("ABKC" module banks, e.g. SOUND/FE/FE_MB.abk) are what Csis classes
// and functions drive. A bank holds modules -- one per Csis class -- each a
// small dataflow graph compiled by the sound tools into machine code for the
// target platform: on PC, x86 that walks the instance's node states (esi)
// calling the node update functions (sndaemsfuncs) and moving values between
// node outputs and inputs. A Csis instance of the class creates a module
// instance; every timer tick (20 ms) its code runs. Player nodes start bank
// samples (BNKl, EA-XA) on voices.
//
// Reconstructed from speed.exe 1.3:
//   SNDAEMS_addmodulebank 0x81D17D, bank resolve 0x81AB36,
//   SNDAEMS_removemodulebank 0x81A980, module instance creation 0x81A86B,
//   node update functions 0x8196C2-0x81A7F4 (table sndaemsfuncs at 0x90BC40,
//   same order as the GameCube build), timer update 0x81F98A, timer period
//   0x9C1B54 (20 ms unless set), player bank functions 0x81A330-0x81A451.
//
// The module code isn't run as x86: it is decoded at load into a short op
// list (the compiler only ever emits the handful of instruction forms below;
// every module in the PC data was checked) and interpreted against the
// instance's state block, which keeps the 32-bit layout of the original
// (AemsDef structs; pointer fields hold handles or bank offsets instead).
#include "AudioMixer.h"
#include "EAAudioFormat.h"

#include "csis/csis.h"
#include "snd/sndo.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

extern "C" int SNDstop(int shandle); // not in the public header

void CsisNativeLock();   // CsisNative.cpp: Csis and Snd share one mutex
void CsisNativeUnlock(); // (Snd::System::VectorToCsisMutex on PC)

namespace {

// --- helpers ----------------------------------------------------------------

bool LogEnabled() {
    static const bool on = std::getenv("NFSMW_AUDIO_LOG") != nullptr;
    return on;
}

void Log(const char *fmt, ...) {
    if (!LogEnabled()) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "[snd] ");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    va_end(args);
}

struct Lock {
    Lock() {
        CsisNativeLock();
    }
    ~Lock() {
        CsisNativeUnlock();
    }
};

inline int32_t Rd32(const uint8_t *p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
inline void Wr32(uint8_t *p, int32_t v) {
    std::memcpy(p, &v, 4);
}
inline float RdF(const uint8_t *p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}
inline void WrF(uint8_t *p, float v) {
    std::memcpy(p, &v, 4);
}
inline uint16_t Rd16(const uint8_t *p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
inline void Wr16(uint8_t *p, uint16_t v) {
    std::memcpy(p, &v, 2);
}

int Clip(int v, int lo, int hi) { // SNDI_clipint32
    return v < lo ? lo : (v > hi ? hi : v);
}

int FtoI(float f) { // SNDI_ftoifast (x87 fistp, round to nearest)
    return static_cast<int>(std::lrintf(f));
}

unsigned int Random() { // iSNDrandom
    static uint32_t state = 0x1234567u;
    state = state * 1103515245u + 12345u;
    return (state >> 8) & 0x7FFFFF;
}

const float kTimerPeriod = 20.0f; // Snd::gVariableTimerPeriod, ms

// --- sample banks and voices -------------------------------------------------

struct SampleBank {
    const uint8_t *data = nullptr;
    size_t size = 0;
    std::vector<ea_audio::BankSound> sounds;
    std::vector<port_audio::SampleRef> decoded;
};

std::vector<std::unique_ptr<SampleBank>> gSampleBanks; // handle = index

port_audio::SampleRef GetSample(SampleBank &bank, int patnum) {
    if (patnum < 0 || patnum >= static_cast<int>(bank.sounds.size())) {
        return nullptr;
    }
    if (bank.decoded[patnum]) {
        return bank.decoded[patnum];
    }
    const ea_audio::BankSound &snd = bank.sounds[patnum];
    if (snd.data == nullptr || snd.header.num_samples <= 0) {
        return nullptr;
    }
    auto sample = std::make_shared<port_audio::Sample>();
    int channels = snd.header.channels == 2 && snd.header.data_offset2 > 0 ? 2 : 1;
    sample->channels = channels;
    sample->rate = snd.header.sample_rate > 0 ? snd.header.sample_rate : 22050;
    sample->pcm.assign(static_cast<size_t>(snd.header.num_samples) * channels, 0);
    for (int c = 0; c < channels; c++) {
        int off = c == 0 ? snd.header.data_offset : snd.header.data_offset2;
        if (off < 0 || static_cast<size_t>(off) >= bank.size) {
            return nullptr;
        }
        ea_audio::XaChannel ch;
        ea_audio::DecodeXaR2(ch, bank.data + off, bank.size - off, sample->pcm.data() + c, snd.header.num_samples, channels);
    }
    bank.decoded[patnum] = sample;
    return sample;
}

struct Voice {
    int handle = -1;
    port_audio::VoiceId mix = 0;
    port_audio::SampleRef sample;
    int channels = 1;
    float patchVol = 1.0f;
    float progVol = 1.0f;     // SNDvol / AEMS bank volume
    int pitchMult = 0x1000;   // 0x1000 = unity
    uint16_t azimuth = 0;     // 0 front, 0x4000 right, 0x8000 rear, 0xC000 left
    long loopStart = -1;
};

const int kMaxVoices = 64;
Voice gVoices[kMaxVoices];
int gVoiceSerial = 0;

float PanOf(const Voice &v) {
    if (v.channels != 1) {
        return 0.0f;
    }
    return std::sin(v.azimuth * (6.28318531f / 65536.0f));
}

void ApplyVoice(Voice &v) {
    port_audio::Mixer *mixer = port_audio::GetMixer();
    if (mixer == nullptr || v.mix == 0) {
        return;
    }
    mixer->SetVolume(v.mix, v.progVol * v.patchVol);
    mixer->SetPitch(v.mix, v.pitchMult / 4096.0f);
    mixer->SetPan(v.mix, PanOf(v));
}

// SNDVOICEI_get (0x81F7CB): the voice index of a live handle, or <0.
int VoiceIndex(int handle) {
    if (handle < 0) {
        return SNDERR_INV_PARAM;
    }
    int index = handle & 0xFF;
    if (index >= kMaxVoices || gVoices[index].handle != handle) {
        return SNDERR_INV_PARAM;
    }
    Voice &v = gVoices[index];
    port_audio::Mixer *mixer = port_audio::GetMixer();
    if (mixer == nullptr || !mixer->IsPlaying(v.mix)) {
        v.handle = -1;
        v.sample.reset();
        return SNDERR_INV_PARAM;
    }
    return index;
}

// SNDBANK_play (0x81F337)
int BankPlay(int bhandle, int patnum, const SNDPLAYOPTS *opts) {
    if (bhandle < 0 || bhandle >= static_cast<int>(gSampleBanks.size()) || !gSampleBanks[bhandle]) {
        return SNDERR_INV_PARAM;
    }
    SampleBank &bank = *gSampleBanks[bhandle];
    port_audio::SampleRef sample = GetSample(bank, patnum);
    port_audio::Mixer *mixer = port_audio::GetMixer();
    if (!sample || mixer == nullptr) {
        Log("bank %d patch %d: no sample", bhandle, patnum);
        return SNDERR_INV_PARAM;
    }
    int index = -1;
    for (int i = 0; i < kMaxVoices; i++) {
        if (gVoices[i].handle < 0 || !mixer->IsPlaying(gVoices[i].mix)) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        return SNDERR_ALLOCATE;
    }
    const ea_audio::PatchHeader &h = bank.sounds[patnum].header;
    Voice &v = gVoices[index];
    v = Voice();
    gVoiceSerial = (gVoiceSerial + 1) & 0x7FFFFF;
    v.handle = (gVoiceSerial << 8) | index;
    v.sample = sample;
    v.channels = sample->channels;
    v.patchVol = h.volume >= 0 ? h.volume / 127.0f : 1.0f;
    v.progVol = opts->vol / 127.0f;
    v.pitchMult = opts->pitchmult;
    v.azimuth = opts->azimuth;
    long loopStart = -1;
    long loopFrames = -1;
    if (h.loop_start >= 0 && h.loop_length > 0) {
        loopStart = h.loop_start;
        loopFrames = h.loop_length;
    } else if (h.loop_start >= 0) {
        loopStart = h.loop_start;
        loopFrames = static_cast<long>(sample->Frames()) - h.loop_start;
    }
    v.loopStart = loopStart;
    v.mix = mixer->Play(sample, v.progVol * v.patchVol, PanOf(v), v.pitchMult / 4096.0f, loopStart, loopFrames);
    Log("play bank %d patch %d: %d Hz, %d ch, %zu frames%s, vol %.2f, pitch %.3f, azimuth 0x%04x -> voice %d", bhandle, patnum,
        sample->rate, sample->channels, sample->Frames(), loopFrames > 0 ? " (looped)" : "", v.progVol * v.patchVol,
        v.pitchMult / 4096.0f, v.azimuth, index);
    return v.handle;
}

// --- AEMS ---------------------------------------------------------------------

// ModuleBank header (0x5C) offsets.
enum {
    MB_NUMMODULES = 0x0A,
    MB_TOTALSIZE = 0x14,
    MB_RESIDENTSIZE = 0x18,
    MB_MODULEOFFSET = 0x1C,
    MB_SFXBANKOFFSET = 0x20,
    MB_SFXBANKSIZE = 0x24,
    MB_MIDIBANKOFFSET = 0x28,
    MB_MIDIBANKSIZE = 0x2C,
    MB_FUNCFIXUPOFFSET = 0x30,
    MB_STATICFIXUPOFFSET = 0x34,
    MB_INTERFACEOFFSET = 0x38,
    MB_HANDLE = 0x3C,
    MB_SFXBHANDLE = 0x40,
    MB_MIDIBHANDLE = 0x44,
};

// Interpreted module code.
enum OpKind : uint8_t {
    OP_PUSH,     // push esi
    OP_POP,      // pop esi
    OP_RET,      // ret
    OP_LOADARG,  // mov esi, [esp+8]
    OP_CALL,     // call sndaemsfuncs[imm] (esi pushed last is the argument)
    OP_ADDESI,   // add esi, imm
    OP_ADDESP,   // add esp, imm
    OP_STORE,    // mov [esi+d], eax
    OP_STOREB,   // mov byte [esi+d], imm
    OP_LOADEAX,  // mov eax, [esi+d]
    OP_LOADECX,  // mov ecx, [esi+d]
    OP_MOVEAXECX,// mov eax, ecx
    OP_CMP,      // cmp eax, ecx
    OP_ADD,      // add eax, [esi+d]
    OP_SUB,      // sub eax, [esi+d]
    OP_IMUL,     // imul eax, [esi+d]
    OP_JG,       // jg rel8
    OP_JL,       // jl rel8
};

struct Op {
    OpKind kind;
    int32_t a; // displacement / immediate / function / op target
    int32_t b; // byte immediate (STOREB)
};

struct ModuleBank;
struct Instance;

// Native stand-ins for the client structs and handle words inside the state
// block (32-bit there, so they can't hold native ones).
struct BoundClient {
    union {
        Csis::FunctionClient fn;
        Csis::ClassDestructorClient dtor;
    } c;
    Instance *inst;
    uint32_t off; // state offset in the instance block
};

struct Module {
    ModuleBank *bank = nullptr;
    uint32_t headerOff = 0;
    int id = 0;
    Csis::ClassHandle classHandle;
    Csis::ClassConstructorClient constructorClient;
    int curInstances = 0;
    int maxInstances = 0;
    int numGlobals = 0;
    int numFunctions = 0;
    int numPlayers = 0;
    bool classDestructorPresent = false;
    bool classDataPresent = false;
    int numClassControllers = 0;
    uint32_t pdata = 0;
    uint32_t datasize = 0;
    uint32_t destroyOff = 0;
    std::vector<uint32_t> playerOffs;
    std::vector<uint32_t> controllerOffs;
    std::vector<Op> code;
    std::vector<Instance *> instances;
};

struct ModuleBank {
    int handle = 0;
    uint8_t *base = nullptr;
    int sfxbhandle = -1;
    std::vector<std::unique_ptr<Module>> modules;
};

struct Instance {
    Module *mod = nullptr;
    Csis::Class *cls = nullptr;
    std::vector<uint8_t> data;
    std::vector<std::unique_ptr<BoundClient>> clients;
    BoundClient *dtorClient = nullptr;
    BoundClient *cdataClient = nullptr;
    std::vector<BoundClient *> globalClients;
    std::vector<BoundClient *> functionClients;
    bool dead = false;
    bool faulted = false;
};

std::vector<std::unique_ptr<ModuleBank>> gModuleBanks;
std::vector<Instance *> gInstances; // the timer list (front = newest)
int gModuleBankSerial = 0;

// Csis handles referenced from state blocks: slot word = index + 1.
struct HandleSlot {
    uint8_t type = 0; // AemsDef::InterfaceType
    Csis::FunctionHandle fn;
    Csis::ClassHandle cls;
    Csis::GlobalVariableHandle gv;
};
std::vector<std::unique_ptr<HandleSlot>> gHandles;
// Csis instances owned by ControlClass nodes: slot word = index + 1.
std::vector<Csis::Class *> gClassRefs;

HandleSlot *SlotHandle(const uint8_t *word) {
    int32_t i = Rd32(word) - 1;
    if (i < 0 || i >= static_cast<int32_t>(gHandles.size())) {
        return nullptr;
    }
    return gHandles[i].get();
}

Csis::Class *ClassRef(uint8_t *word) {
    int32_t i = Rd32(word) - 1;
    return i >= 0 && i < static_cast<int32_t>(gClassRefs.size()) ? gClassRefs[i] : nullptr;
}

void SetClassRef(uint8_t *word, Csis::Class *cls) {
    int32_t i = Rd32(word) - 1;
    if (i >= 0 && i < static_cast<int32_t>(gClassRefs.size())) {
        gClassRefs[i] = nullptr;
    }
    if (cls == nullptr) {
        Wr32(word, 0);
        return;
    }
    for (size_t k = 0; k < gClassRefs.size(); k++) {
        if (gClassRefs[k] == nullptr) {
            gClassRefs[k] = cls;
            Wr32(word, static_cast<int32_t>(k + 1));
            return;
        }
    }
    gClassRefs.push_back(cls);
    Wr32(word, static_cast<int32_t>(gClassRefs.size()));
}

ModuleBank *FindModuleBank(int handle) {
    for (auto &b : gModuleBanks) {
        if (b->handle == handle) {
            return b.get();
        }
    }
    return nullptr;
}

// Context of the node currently running.
struct Ctx {
    Instance *inst;
    uint8_t *d; // instance block
    uint32_t size;
    bool Ok(uint32_t off, uint32_t n) const {
        return off + n <= size && off + n >= off;
    }
};

// --- player bank functions (0x819FBD..0x81A451) ---

void BankPitchMult(int voice, int value) {
    gVoices[voice].pitchMult = Clip(value, 0, 0xFFFF);
    ApplyVoice(gVoices[voice]);
}

void BankVol(int voice, int value) {
    gVoices[voice].progVol = Clip(value, 0, 0x7FFF) * (1.0f / 32767.0f);
    ApplyVoice(gVoices[voice]);
}

void BankAzimuth(int voice, int value) {
    if (gVoices[voice].channels == 1) {
        gVoices[voice].azimuth = static_cast<uint16_t>(value);
        ApplyVoice(gVoices[voice]);
    }
}

void BankInput(int type, int voice, int value) { // playerinterfacefn
    switch (type) {
        case 0: BankPitchMult(voice, value); break;
        case 2: BankVol(voice, value); break;
        case 3: BankAzimuth(voice, value); break;
        default: break; // time multiplier, fx sends, filters, dry level: not modelled
    }
}

void OptInput(int type, SNDPLAYOPTS *spo, int value) { // SNDAEMSoptfn
    switch (type) {
        case 0: spo->pitchmult = static_cast<unsigned short>(Clip(value, 0, 0xFFFF)); break;
        case 1: spo->timemult = static_cast<unsigned short>(Clip(value, 0x800, 0x2000)); break;
        case 2: spo->vol = static_cast<signed char>(Clip(value, 0, 0x7FFF) >> 8); break;
        case 3: spo->azimuth = static_cast<unsigned short>(value); break;
        case 5: spo->fxlevel0 = static_cast<signed char>(Clip(value, 0, 0x7FFF) >> 8); break;
        case 6: spo->lowpasscutoff = static_cast<unsigned short>(Clip(value, 0, 0xFFFF)); break;
        case 7: spo->highpasscutoff = static_cast<unsigned short>(Clip(value, 0, 0xFFFF)); break;
        case 8: spo->drylevel = static_cast<signed char>(Clip(value, 0, 0x7FFF) >> 8); break;
        default: break;
    }
}

// PLAYERSTATE: pModuleBank 0 (here: module bank handle), psamplegroup 4 (bank
// offset), handle 8, prevplaycontrol[2] 0xC, numinputs 0xE, updateoutputs
// 0xF, sampletype 0x10, sampleselect 0x14, playcontrol 0x18, then inputs
// (12 bytes: type, prevvalue 4, value 8) and outputs (timeleft, timecurr).
int PlayerPlayBank(Ctx &c, uint8_t *ps, const uint8_t *entry) { // 0x81A330
    SNDPLAYOPTS spo;
    SNDplaysetdef(&spo);
    spo.fxlevel0 = 0;
    uint8_t *in = ps + 0x1C;
    for (int i = 0; i < ps[0xE]; i++, in += 12) {
        OptInput(in[0], &spo, Rd32(in + 8));
        Wr32(in + 4, Rd32(in + 8));
    }
    ModuleBank *mb = FindModuleBank(Rd32(ps + 0));
    if (mb == nullptr) {
        return SNDERR_INV_PARAM;
    }
    return BankPlay(mb->sfxbhandle, Rd32(entry + 4), &spo);
}

int PlayerResetOutputs(uint8_t *ps) { // 0x81A42F
    ps[0x10] = 0xFF;
    if (ps[0xF] != 0) {
        uint8_t *out = ps + 0x1C + ps[0xE] * 12;
        Wr32(out, 0);
        Wr32(out + 4, 0);
    }
    return 0;
}

void PlayerStop(uint8_t *ps) { // 0x81A394
    SNDstop(Rd32(ps + 8));
}

void PlayerPause(int handle) { // 0x81A3A2
    SNDvol(handle, 0);
    SNDpitchmult(handle, 0);
}

void PlayerUnpause(uint8_t *ps) { // 0x81A3BC
    int pitchmult = 0x1000;
    int vol = 0x7FFF;
    int voice = VoiceIndex(Rd32(ps + 8));
    if (voice < 0) {
        return;
    }
    uint8_t *in = ps + 0x1C;
    for (int i = 0; i < ps[0xE]; i++, in += 12) {
        if (in[0] == 0) {
            pitchmult = Rd32(in + 8);
            Wr32(in + 4, pitchmult);
        } else if (in[0] == 2) {
            vol = Rd32(in + 8);
            Wr32(in + 4, vol);
        }
    }
    BankPitchMult(voice, pitchmult);
    BankVol(voice, vol);
}

int PlayerUpdate(uint8_t *ps) { // 0x81A451
    int voice = VoiceIndex(Rd32(ps + 8));
    if (voice < 0) {
        return PlayerResetOutputs(ps);
    }
    uint8_t *in = ps + 0x1C;
    for (int i = 0; i < ps[0xE]; i++, in += 12) {
        if (Rd32(in + 8) != Rd32(in + 4)) {
            BankInput(in[0], voice, Rd32(in + 8));
            Wr32(in + 4, Rd32(in + 8));
        }
    }
    if (ps[0xF] != 0) {
        Voice &v = gVoices[voice];
        float inv = 1000.0f / v.sample->rate;
        long cur = port_audio::GetMixer()->Position(v.mix);
        if (cur < 0) {
            cur = 0;
        }
        Wr32(in + 4, FtoI(cur * inv));
        if (v.loopStart < 0) {
            Wr32(in, FtoI((static_cast<long>(v.sample->Frames()) - cur) * inv));
        } else {
            Wr32(in, 0);
        }
    }
    return 1;
}

// --- node update functions ---

int UpdateClassDestructor(Ctx &c, uint32_t o) { // 0: triggered at 0x10
    uint8_t *s = c.d + o;
    int r = Rd32(s + 0x10);
    Wr32(s + 0x10, 0);
    return r;
}

int UpdateClassData(Ctx &c, uint32_t o) { // 1: outputs at 0x14
    return Rd32(c.d + o + 0x14);
}

int UpdateGlobalVariable(Ctx &c, uint32_t o) { // 2: globalValue at 0x18
    return Rd32(c.d + o + 0x18);
}

int UpdateCreate(Ctx &c, uint32_t o) { // 3
    int r = Rd32(c.d + o);
    Wr32(c.d + o, 0);
    return r;
}

void DestroyInstance(Instance *inst);

int UpdateDestroy(Ctx &c, uint32_t o) { // 4: triggered at 0xC
    if (Rd32(c.d + o + 0xC) == 0) {
        return 0;
    }
    DestroyInstance(c.inst);
    return 0;
}

int UpdateCallFunction(Ctx &c, uint32_t o) { // 5
    uint8_t *s = c.d + o;
    int num = s[9];
    uint8_t *in;
    if (s[8] != 0) {
        uint8_t *clamp = s + 0xC;
        in = clamp + num * 8;
        for (int i = 0; i < num; i++) {
            Wr32(in + 4 + 4 * i, Clip(Rd32(in + 4 + 4 * i), Rd32(clamp + 8 * i), Rd32(clamp + 8 * i + 4)));
        }
    } else {
        in = s + 0xC;
    }
    if (Rd32(in) != 0) {
        if (HandleSlot *h = SlotHandle(s)) {
            int params[32] = {};
            for (int i = 0; i < num && i < 32; i++) {
                params[i] = Rd32(in + 4 + 4 * i);
            }
            Csis::Function::CallFast(&h->fn, params);
        }
    }
    return 0;
}

int UpdateCounter(Ctx &c, uint32_t o) { // 6
    uint8_t *s = c.d + o;
    int minv = Rd32(s), maxv = Rd32(s + 4);
    int ovr = Rd32(s + 0x14);
    if (ovr >= minv && ovr <= maxv) {
        return ovr;
    }
    int value = Rd32(s + 8);
    if (Rd32(s + 0x10) > 0) {
        value += static_cast<signed char>(s[0xC]);
        if (value > maxv) {
            value = minv;
        } else if (value < minv) {
            value = maxv;
        }
        Wr32(s + 8, value);
    }
    return value;
}

int UpdateRandom(Ctx &c, uint32_t o) { // 7
    uint8_t *s = c.d + o;
    if (Rd32(s + 0xC) == 0) {
        return Rd32(s + 8);
    }
    int range = Rd32(s + 4);
    int v = (range > 0 ? static_cast<int>(Random() % range) : 0) + Rd32(s);
    Wr32(s + 8, v);
    return v;
}

int UpdateRandomShuffle(Ctx &c, uint32_t o) { // 8
    uint8_t *s = c.d + o;
    uint32_t inOff = Rd16(s);
    if (!c.Ok(o + inOff, 4) || Rd32(s + inOff) == 0) {
        return Rd32(s + 0xC);
    }
    unsigned range = Rd16(s + 0xA);
    unsigned index = Rd16(s + 8);
    int avoid = static_cast<signed char>(s[3]);
    int span = static_cast<int>(range) - static_cast<int>(index) - avoid;
    unsigned swap = (span > 0 ? Random() % span : 0) + index;
    int out;
    if (s[2] == 1) {
        uint8_t *set = s + 0x10;
        out = set[swap];
        set[swap] = set[index];
        set[index] = static_cast<uint8_t>(out);
    } else {
        uint8_t *set = s + 0x10;
        out = Rd16(set + 2 * swap);
        Wr16(set + 2 * swap, Rd16(set + 2 * index));
        Wr16(set + 2 * index, static_cast<uint16_t>(out));
    }
    index++;
    out += Rd32(s + 4);
    Wr32(s + 0xC, out);
    if (index >= range) {
        index = 0;
        s[3] = 1;
    } else {
        s[3] = 0;
    }
    Wr16(s + 8, static_cast<uint16_t>(index));
    return out;
}

const uint8_t *BankPtr(Ctx &c, int32_t offset) {
    return c.inst->mod->bank->base + offset;
}

int UpdateRandomWeighted(Ctx &c, uint32_t o) { // 9: ptable 0 (bank offset)
    uint8_t *s = c.d + o;
    if (Rd32(s + 0x10) != 0) {
        unsigned randval = Random() % 100;
        const uint8_t *table = BankPtr(c, Rd32(s));
        unsigned total = 0;
        int range = Rd32(s + 8);
        for (int i = 0; i < range; i++) {
            total += static_cast<signed char>(table[0x10 + i]);
            if (total > randval) {
                Wr32(s + 0xC, i + Rd32(s + 4));
                break;
            }
        }
    }
    return Rd32(s + 0xC);
}

int UpdateRangeTrig(Ctx &c, uint32_t o) { // 10
    uint8_t *s = c.d + o;
    int in = Rd32(s + 0x14);
    if (in >= Rd32(s) && in <= Rd32(s + 4)) {
        if (s[0x10] == 0) {
            s[0x10] = 1;
            s[0x11] = 1;
            return 1;
        }
    } else if (in >= Rd32(s + 8) && in <= Rd32(s + 0xC)) {
        s[0x10] = 0;
    }
    s[0x11] = 0;
    return 0;
}

int UpdateDelayTrig(Ctx &c, uint32_t o) { // 11
    uint8_t *s = c.d + o;
    if (Rd32(s + 8) != 0) {
        WrF(s, 0.0f);
    }
    float cur = RdF(s);
    if (cur >= 0.0f) {
        if (cur >= static_cast<float>(Rd32(s + 0xC))) {
            s[4] = 1;
            WrF(s, -1.0f);
            return 1;
        }
        WrF(s, cur + kTimerPeriod);
    }
    s[4] = 0;
    return 0;
}

int UpdateStateGen(Ctx &c, uint32_t o) { // 12
    uint8_t *s = c.d + o;
    uint32_t inOff = Rd16(s);
    for (int i = 0; i < s[2]; i++) {
        if (c.Ok(o + inOff + 4 * i, 4) && Rd32(s + inOff + 4 * i) != 0) {
            Wr32(s + 4, Rd32(s + 8 + 4 * i));
            break;
        }
    }
    return Rd32(s + 4);
}

int UpdateMerge(Ctx &c, uint32_t o) { // 13
    uint8_t *s = c.d + o;
    for (int i = 0; i < s[0]; i++) {
        if (Rd32(s + 4 + 4 * i) != 0) {
            return 1;
        }
    }
    return 0;
}

void EnvelopeProgramSegment(uint8_t *s) { // 0x819BE2
    int seg = s[3];
    float duration = RdF(s + 0x18 + seg * 8);
    WrF(s + 4, duration);
    float delta = (RdF(s + 0x1C + seg * 8) - RdF(s + 0xC)) / duration;
    WrF(s + 8, delta * kTimerPeriod);
}

int UpdateEnvelope(Ctx &c, uint32_t o) { // 14
    uint8_t *s = c.d + o;
    uint32_t inOff = Rd16(s);
    int control = c.Ok(o + inOff, 4) ? Rd32(s + inOff) : 0;
    int prev = static_cast<signed char>(s[2]);
    int numseg = s[0x10];
    int release = static_cast<int16_t>(Rd16(s + 0x12));
    if (control == 1 && prev == 0) {
        WrF(s + 0xC, RdF(s + 0x14));
        s[3] = 0;
        EnvelopeProgramSegment(s);
    } else if (control == 3 && prev != 3 && s[3] < release) {
        s[3] = static_cast<uint8_t>(release);
        EnvelopeProgramSegment(s);
    } else if ((control == 1 || control == 3) && s[3] < numseg) {
        float remaining = RdF(s + 4) - kTimerPeriod;
        WrF(s + 4, remaining);
        if (remaining <= 0.0f) {
            WrF(s + 0xC, RdF(s + 0x1C + s[3] * 8));
            s[3]++;
            if (s[3] < numseg) {
                EnvelopeProgramSegment(s);
            } else {
                WrF(s + 0xC, 0.0f);
            }
        } else {
            WrF(s + 0xC, RdF(s + 0xC) + RdF(s + 8));
        }
    } else if (control != 2) {
        WrF(s + 0xC, 0.0f);
    }
    s[2] = static_cast<uint8_t>(control);
    return FtoI(RdF(s + 0xC));
}

int TableEntry(const uint8_t *table, int i) {
    switch (table[0]) {
        case 1: return static_cast<signed char>(table[0x10 + i]);
        case 2: return static_cast<int16_t>(Rd16(table + 0x10 + 2 * i));
        default: return Rd32(table + 0x10 + 4 * i);
    }
}

int UpdateTable(Ctx &c, uint32_t o) { // 15: ptable 0 (bank offset), prev 4, out 8, in 0xC
    uint8_t *s = c.d + o;
    int input = Rd32(s + 0xC);
    if (input != Rd32(s + 4)) {
        const uint8_t *table = BankPtr(c, Rd32(s));
        Wr32(s + 4, input);
        int minv = Rd32(table + 4);
        int index = Clip(input, minv, Rd32(table + 8)) - minv;
        float resolution = RdF(table + 0xC);
        if (resolution == 1.0f) {
            Wr32(s + 8, TableEntry(table, index));
        } else {
            float f = index * resolution;
            int ia = static_cast<int>(f);
            f -= ia;
            int ib = ia + 1;
            int count = Rd16(table + 2);
            if (ib >= count) {
                ib = count - 1;
            }
            float ea = static_cast<float>(TableEntry(table, ia));
            float eb = static_cast<float>(TableEntry(table, ib));
            Wr32(s + 8, FtoI(f * (eb - ea) + ea));
        }
    }
    return Rd32(s + 8);
}

int UpdateDelayLine(Ctx &c, uint32_t o) { // 16
    uint8_t *s = c.d + o;
    uint32_t inOff = Rd16(s);
    if (!c.Ok(o + inOff, 8)) {
        return 0;
    }
    uint8_t *in = s + inOff;
    int maxslots = Rd16(s + 2);
    int delaytime = Rd32(in + 4);
    if (delaytime != Rd32(s + 8)) {
        Wr32(s + 8, delaytime);
        if (delaytime < 0) {
            Wr32(in + 4, 0);
            delaytime = 0;
        }
        int offset = static_cast<int>(delaytime / kTimerPeriod + 0.5f);
        if (offset >= maxslots) {
            offset = maxslots - 1;
        }
        Wr16(s + 4, static_cast<uint16_t>(Rd16(s + 6) + offset));
    }
    int inslot = Rd16(s + 4);
    int outslot = Rd16(s + 6);
    if (inslot >= maxslots) {
        inslot -= maxslots;
    }
    if (outslot >= maxslots) {
        outslot = 0;
    }
    Wr32(s + 0xC + 4 * inslot, Rd32(in));
    int out = Rd32(s + 0xC + 4 * outslot);
    Wr16(s + 4, static_cast<uint16_t>(inslot + 1));
    Wr16(s + 6, static_cast<uint16_t>(outslot + 1));
    return out;
}

int UpdateMux(Ctx &c, uint32_t o) { // 17: inputvalue[0] is the control
    uint8_t *s = c.d + o;
    int control = Rd32(s + 4);
    if (control <= 0 || control > s[0]) {
        return 0;
    }
    return Rd32(s + 4 + 4 * control);
}

int UpdateDemux(Ctx &c, uint32_t o) { // 18
    uint8_t *s = c.d + o;
    int prev = static_cast<int16_t>(Rd16(s + 2));
    if (prev >= 1) {
        Wr32(s + 0xC + 4 * (prev - 1), 0);
    }
    int control = Rd32(s + 4);
    if (control > 0 && control <= s[0]) {
        Wr32(s + 0xC + 4 * (control - 1), Rd32(s + 8));
        Wr16(s + 2, static_cast<uint16_t>(control));
    }
    return Rd32(s + 0xC);
}

int UpdateMin(Ctx &c, uint32_t o) { // 19
    uint8_t *s = c.d + o;
    int v = Rd32(s + 4);
    for (int i = 1; i < s[0]; i++) {
        v = std::min(v, Rd32(s + 4 + 4 * i));
    }
    return v;
}

int UpdateMax(Ctx &c, uint32_t o) { // 20
    uint8_t *s = c.d + o;
    int v = Rd32(s + 4);
    for (int i = 1; i < s[0]; i++) {
        v = std::max(v, Rd32(s + 4 + 4 * i));
    }
    return v;
}

int UpdateScale(Ctx &c, uint32_t o) { // 21
    uint8_t *s = c.d + o;
    float v = static_cast<float>(Rd32(s + 8));
    for (int i = 1; i < s[0]; i++) {
        v *= static_cast<float>(Rd32(s + 8 + 4 * i));
    }
    return FtoI(v * RdF(s + 4));
}

int UpdateAdd(Ctx &c, uint32_t o) { // 22
    uint8_t *s = c.d + o;
    int v = Rd32(s + 4);
    for (int i = 1; i < s[0]; i++) {
        v += Rd32(s + 4 + 4 * i);
    }
    return v;
}

int UpdateSubtract(Ctx &c, uint32_t o) { // 23
    return Rd32(c.d + o) - Rd32(c.d + o + 4);
}

int UpdateMultiply(Ctx &c, uint32_t o) { // 24
    return Rd32(c.d + o) * Rd32(c.d + o + 4);
}

int UpdateDivide(Ctx &c, uint32_t o) { // 25
    int b = Rd32(c.d + o + 4);
    return b != 0 ? Rd32(c.d + o) / b : 0;
}

int UpdateModulo(Ctx &c, uint32_t o) { // 26
    int b = Rd32(c.d + o + 4);
    return b != 0 ? Rd32(c.d + o) % b : 0;
}

int UpdatePlayer(Ctx &c, uint32_t o) { // 27 (0x81A52C)
    uint8_t *ps = c.d + o;
    int play = Clip(Rd32(ps + 0x18), 0, 2);
    signed char type = static_cast<signed char>(ps[0x10]);
    if (play != static_cast<signed char>(ps[0xC])) {
        if (play == 0) {
            if (type >= 0) {
                PlayerStop(ps);
                PlayerResetOutputs(ps);
            }
        } else if (play == 1) {
            if (type < 0) {
                if (static_cast<signed char>(ps[0xC]) != 2 || static_cast<signed char>(ps[0xD]) != 1) {
                    const uint8_t *group = BankPtr(c, Rd32(ps + 4));
                    int count = Rd32(group);
                    int select = Rd32(ps + 0x14);
                    if (select >= count) {
                        select = count - 1;
                    } else if (select < 0) {
                        select = 0;
                    }
                    const uint8_t *entry = group + 4 + select * 12;
                    ps[0x10] = entry[0];
                    int handle = static_cast<signed char>(entry[0]) == 0 ? PlayerPlayBank(c, ps, entry) : SNDERR_UNSUPPORTED;
                    if (static_cast<signed char>(entry[0]) != 0) {
                        Log("player: sample entry type %d not supported", entry[0]);
                    }
                    Wr32(ps + 8, handle);
                    if (handle < 0) {
                        PlayerResetOutputs(ps);
                    }
                }
            } else {
                PlayerUnpause(ps);
            }
        } else if (type >= 0) {
            PlayerPause(Rd32(ps + 8));
        }
        ps[0xD] = ps[0xC];
        ps[0xC] = static_cast<uint8_t>(play);
    }
    if (play == 1 && static_cast<signed char>(ps[0x10]) >= 0) {
        play = PlayerUpdate(ps);
    }
    if (static_cast<signed char>(ps[0x10]) < 0) {
        play = 0;
    }
    return play;
}

int UpdateOscillator(Ctx &c, uint32_t o) { // 28
    uint8_t *s = c.d + o;
    int period = Rd32(s + 8);
    if (period <= 0) {
        return 0;
    }
    float increment = kTimerPeriod / period;
    float amplitude = static_cast<float>(Rd32(s + 0xC));
    float pc = RdF(s + 4);
    while (pc >= 1.0f) {
        pc -= 1.0f;
    }
    float out;
    switch (s[0]) {
        case 0: out = std::sin(pc * 6.28318531f) * amplitude; break;
        case 1: out = pc >= 0.5f ? amplitude : 0.0f; break;
        case 2: out = pc * amplitude; break;
        default: out = pc < 0.5f ? pc * 2 * amplitude : (1.0f - pc) * 2 * amplitude; break;
    }
    WrF(s + 4, pc + increment);
    return FtoI(out);
}

int UpdateRamp(Ctx &c, uint32_t o) { // 29
    uint8_t *s = c.d + o;
    int target = Rd32(s + 0x18);
    int duration = Rd32(s + 0x10);
    float cur = RdF(s);
    if (static_cast<float>(target) == cur) {
        return target;
    }
    if (target != Rd32(s + 8) || duration != Rd32(s + 0xC)) {
        Wr32(s + 8, target);
        Wr32(s + 0xC, duration);
        if (duration <= 0) {
            WrF(s, static_cast<float>(target));
            return target;
        }
        float delta = (target - cur) * kTimerPeriod / duration * 0.00024414063f;
        WrF(s + 4, delta);
    }
    float delta = RdF(s + 4);
    cur += delta * Rd32(s + 0x14);
    if (delta >= 0.0f) {
        if (cur > target) {
            cur = static_cast<float>(target);
        }
    } else if (cur < target) {
        cur = static_cast<float>(target);
    }
    WrF(s, cur);
    return FtoI(cur);
}

int UpdateAddMax(Ctx &c, uint32_t o) { // 30
    uint8_t *s = c.d + o;
    int v = Rd32(s + 8);
    for (int i = 1; i < s[0]; i++) {
        v += Rd32(s + 8 + 4 * i);
    }
    return std::min(v, Rd32(s + 4));
}

int UpdateSubtractMin(Ctx &c, uint32_t o) { // 31
    uint8_t *s = c.d + o;
    return std::max(Rd32(s + 4) - Rd32(s + 8), Rd32(s));
}

int UpdateMultiplyMax(Ctx &c, uint32_t o) { // 32
    uint8_t *s = c.d + o;
    return std::min(Rd32(s + 4) * Rd32(s + 8), Rd32(s));
}

int UpdateMin2(Ctx &c, uint32_t o) { // 33
    return std::min(Rd32(c.d + o), Rd32(c.d + o + 4));
}

int UpdateMax2(Ctx &c, uint32_t o) { // 34
    return std::max(Rd32(c.d + o), Rd32(c.d + o + 4));
}

int UpdateScale2(Ctx &c, uint32_t o) { // 35
    uint8_t *s = c.d + o;
    return FtoI(static_cast<float>(Rd32(s + 4)) * static_cast<float>(Rd32(s + 8)) * RdF(s));
}

int UpdateAdd2(Ctx &c, uint32_t o) { // 36
    return Rd32(c.d + o) + Rd32(c.d + o + 4);
}

int UpdateFunction(Ctx &c, uint32_t o) { // 37: triggered byte at 0x19
    uint8_t *s = c.d + o;
    int r = s[0x19];
    s[0x19] = 0;
    return r;
}

int UpdateControlClass(Ctx &c, uint32_t o) { // 38 (0x819875)
    uint8_t *s = c.d + o;
    bool clamp = s[0xC] != 0;
    int num = s[0xD];
    uint8_t *clampDesc = s + 0x10;
    uint8_t *in = clamp ? clampDesc + num * 8 : s + 0x10;
    uint8_t *params = in + 8;
    auto clampParams = [&]() {
        if (clamp) {
            for (int i = 0; i < num; i++) {
                Wr32(params + 4 * i, Clip(Rd32(params + 4 * i), Rd32(clampDesc + 8 * i), Rd32(clampDesc + 8 * i + 4)));
            }
        }
    };
    Csis::Class *cls = ClassRef(s + 8);
    if (Rd32(in + 4) != 0) { // destructor triggered
        if (cls != nullptr) {
            cls->ReleaseFast();
            SetClassRef(s + 8, nullptr);
            cls = nullptr;
        }
    } else if (Rd32(in) == 0) { // no constructor trigger: update member data
        if (cls != nullptr) {
            clampParams();
            int buf[32] = {};
            for (int i = 0; i < num && i < 32; i++) {
                buf[i] = Rd32(params + 4 * i);
            }
            cls->SetMemberDataFast(buf);
        }
    } else if (cls == nullptr) {
        clampParams();
        HandleSlot *h = SlotHandle(s);
        int buf[32] = {};
        for (int i = 0; i < num && i < 32; i++) {
            buf[i] = Rd32(params + 4 * i);
        }
        if (h == nullptr || Csis::Class::CreateInstanceFast(&h->cls, buf, &cls) < Csis::RESULT_OK) {
            cls = nullptr;
        }
        SetClassRef(s + 8, cls);
    }
    int refCount = 0;
    if (cls != nullptr) {
        cls->GetRefCount(&refCount);
    }
    return refCount;
}

int UpdateSetGlobalVariable(Ctx &c, uint32_t o) { // 39
    uint8_t *s = c.d + o;
    int value = Rd32(s + 0x14);
    if (Rd32(s + 0x10) != value) {
        Wr32(s + 0x10, value);
        value = Clip(value, Rd32(s + 8), Rd32(s + 0xC));
        if (HandleSlot *h = SlotHandle(s)) {
            Csis::GlobalVariable::SetFast(&h->gv, &value);
        }
    }
    return 0;
}

typedef int (*NodeFn)(Ctx &, uint32_t);

// sndaemsfuncs (0x90BC40)
const NodeFn kNodeFns[40] = {
    UpdateClassDestructor, UpdateClassData, UpdateGlobalVariable, UpdateCreate, UpdateDestroy,
    UpdateCallFunction, UpdateCounter, UpdateRandom, UpdateRandomShuffle, UpdateRandomWeighted,
    UpdateRangeTrig, UpdateDelayTrig, UpdateStateGen, UpdateMerge, UpdateEnvelope,
    UpdateTable, UpdateDelayLine, UpdateMux, UpdateDemux, UpdateMin,
    UpdateMax, UpdateScale, UpdateAdd, UpdateSubtract, UpdateMultiply,
    UpdateDivide, UpdateModulo, UpdatePlayer, UpdateOscillator, UpdateRamp,
    UpdateAddMax, UpdateSubtractMin, UpdateMultiplyMax, UpdateMin2, UpdateMax2,
    UpdateScale2, UpdateAdd2, UpdateFunction, UpdateControlClass, UpdateSetGlobalVariable,
};

// Minimum state size each node touches at its fixed offsets (bounds check).
const uint8_t kNodeSizes[40] = {
    0x14, 0x18, 0x1C, 4, 0x10, 0xC, 0x18, 0x10, 0x14, 0x14, 0x18, 0x10, 0xC, 8, 0x20, 0x10, 0x10, 8, 0x10, 8,
    8, 0xC, 8, 8, 8, 8, 8, 0x1C, 0x10, 0x1C, 0xC, 0xC, 0xC, 8, 8, 0xC, 8, 0x1C, 0x10, 0x18,
};

// --- module code ---

// Decodes the x86 the AEMS compiler emits for a module into ops. Function
// calls go through the function fixup list: the rel32 of each call holds the
// sndaemsfuncs index until the PC resolver patches it.
bool DecodeModuleCode(const uint8_t *code, uint32_t size, uint32_t codeOff, const std::vector<uint32_t> &funcFixups,
                      std::vector<Op> *ops) {
    std::vector<int> opAt(size + 1, -1); // byte offset -> op index
    std::vector<std::pair<size_t, uint32_t>> jumps; // op index, target byte offset
    uint32_t p = 0;
    auto esiOperand = [&](uint8_t modrm, int32_t *disp, uint32_t *len) -> bool {
        if ((modrm & 7) != 6) {
            return false;
        }
        switch (modrm >> 6) {
            case 0: *disp = 0; *len = 0; return true;
            case 1: *disp = static_cast<int8_t>(code[p + 2]); *len = 1; return true;
            case 2: *disp = Rd32(code + p + 2); *len = 4; return true;
            default: return false;
        }
    };
    while (p < size) {
        opAt[p] = static_cast<int>(ops->size());
        uint8_t b = code[p];
        Op op = {OP_RET, 0, 0};
        uint32_t len = 1;
        int32_t disp = 0;
        uint32_t dl = 0;
        if (b == 0x56) {
            op.kind = OP_PUSH;
        } else if (b == 0x5E) {
            op.kind = OP_POP;
        } else if (b == 0xC3) {
            op.kind = OP_RET;
        } else if (b == 0x8B && code[p + 1] == 0x74 && code[p + 2] == 0x24 && code[p + 3] == 0x08) {
            op.kind = OP_LOADARG;
            len = 4;
        } else if (b == 0xE8) {
            uint32_t at = codeOff + p + 1;
            if (std::find(funcFixups.begin(), funcFixups.end(), at) == funcFixups.end()) {
                Log("module code: call at +0x%x without a fixup", p);
                return false;
            }
            op.kind = OP_CALL;
            op.a = Rd32(code + p + 1);
            if (op.a < 0 || op.a >= 40) {
                Log("module code: bad node function %d", op.a);
                return false;
            }
            len = 5;
        } else if ((b == 0x83 || b == 0x81) && (code[p + 1] == 0xC6 || code[p + 1] == 0xC4)) {
            op.kind = code[p + 1] == 0xC6 ? OP_ADDESI : OP_ADDESP;
            op.a = b == 0x83 ? static_cast<int8_t>(code[p + 2]) : Rd32(code + p + 2);
            len = b == 0x83 ? 3 : 6;
        } else if (b == 0x8B && code[p + 1] == 0xC1) {
            op.kind = OP_MOVEAXECX;
            len = 2;
        } else if (b == 0x3B && code[p + 1] == 0xC1) {
            op.kind = OP_CMP;
            len = 2;
        } else if ((b == 0x89 || b == 0x8B || b == 0x03 || b == 0x2B) && esiOperand(code[p + 1], &disp, &dl)) {
            int reg = (code[p + 1] >> 3) & 7;
            if (b == 0x8B && reg == 1) {
                op.kind = OP_LOADECX;
            } else if (reg != 0) {
                Log("module code: unexpected register form %02x %02x", b, code[p + 1]);
                return false;
            } else {
                op.kind = b == 0x89 ? OP_STORE : b == 0x8B ? OP_LOADEAX : b == 0x03 ? OP_ADD : OP_SUB;
            }
            op.a = disp;
            len = 2 + dl;
        } else if (b == 0x0F && code[p + 1] == 0xAF) {
            // imul eax, [esi+d]: one opcode byte more than the forms above
            p++;
            bool ok = esiOperand(code[p + 1], &disp, &dl) && ((code[p + 1] >> 3) & 7) == 0;
            p--;
            if (!ok) {
                Log("module code: unexpected imul form");
                return false;
            }
            op.kind = OP_IMUL;
            op.a = disp;
            len = 3 + dl;
        } else if (b == 0xC6 && esiOperand(code[p + 1], &disp, &dl) && ((code[p + 1] >> 3) & 7) == 0) {
            op.kind = OP_STOREB;
            op.a = disp;
            op.b = code[p + 2 + dl];
            len = 3 + dl;
        } else if (b == 0x7F || b == 0x7C) {
            op.kind = b == 0x7F ? OP_JG : OP_JL;
            jumps.push_back({ops->size(), p + 2 + static_cast<int8_t>(code[p + 1])});
            len = 2;
        } else {
            Log("module code: unknown instruction %02x %02x at +0x%x", b, code[p + 1], p);
            return false;
        }
        ops->push_back(op);
        p += len;
        if (op.kind == OP_RET) {
            break;
        }
    }
    for (auto &j : jumps) {
        if (j.second > size || opAt[j.second] < 0) {
            Log("module code: jump into an instruction");
            return false;
        }
        (*ops)[j.first].a = opAt[j.second];
    }
    return !ops->empty() && ops->back().kind == OP_RET;
}

void RunModule(Instance *inst) {
    Module *mod = inst->mod;
    Ctx c = {inst, inst->data.data(), static_cast<uint32_t>(inst->data.size())};
    uint32_t esi = 0;
    int32_t eax = 0;
    int32_t ecx = 0;
    int cmp = 0;
    uint32_t stack[64];
    int sp = 0;
    const std::vector<Op> &code = mod->code;
    auto fault = [&](const char *what, int32_t v) {
        if (!inst->faulted) {
            std::fprintf(stderr, "[snd] module %d: %s (%d) -- instance stopped\n", mod->id, what, v);
        }
        inst->faulted = true;
    };
    for (size_t pc = 0, steps = 0; pc < code.size() && steps < 100000; pc++, steps++) {
        const Op &op = code[pc];
        switch (op.kind) {
            case OP_PUSH:
                if (sp == 64) {
                    return fault("stack overflow", sp);
                }
                stack[sp++] = esi;
                break;
            case OP_POP:
                if (sp > 0) {
                    esi = stack[--sp];
                }
                break;
            case OP_RET: return;
            case OP_LOADARG: esi = 0x18; break; // &pModuleInstance[1]
            case OP_CALL: {
                if (sp == 0) {
                    return fault("call without argument", op.a);
                }
                uint32_t arg = stack[sp - 1];
                if (!c.Ok(arg, kNodeSizes[op.a])) {
                    return fault("node state out of range", static_cast<int32_t>(arg));
                }
                eax = kNodeFns[op.a](c, arg);
                if (inst->dead) {
                    return;
                }
                break;
            }
            case OP_ADDESI: esi += op.a; break;
            case OP_ADDESP:
                sp -= op.a / 4;
                if (sp < 0) {
                    sp = 0;
                }
                break;
            case OP_STORE:
            case OP_LOADEAX:
            case OP_LOADECX:
            case OP_ADD:
            case OP_SUB:
            case OP_IMUL: {
                uint32_t at = esi + op.a;
                if (!c.Ok(at, 4)) {
                    return fault("access out of range", static_cast<int32_t>(at));
                }
                uint8_t *m = c.d + at;
                switch (op.kind) {
                    case OP_STORE: Wr32(m, eax); break;
                    case OP_LOADEAX: eax = Rd32(m); break;
                    case OP_LOADECX: ecx = Rd32(m); break;
                    case OP_ADD: eax = static_cast<int32_t>(static_cast<uint32_t>(eax) + static_cast<uint32_t>(Rd32(m))); break;
                    case OP_SUB: eax = static_cast<int32_t>(static_cast<uint32_t>(eax) - static_cast<uint32_t>(Rd32(m))); break;
                    default: eax = static_cast<int32_t>(static_cast<uint32_t>(eax) * static_cast<uint32_t>(Rd32(m))); break;
                }
                break;
            }
            case OP_STOREB: {
                uint32_t at = esi + op.a;
                if (!c.Ok(at, 1)) {
                    return fault("access out of range", static_cast<int32_t>(at));
                }
                c.d[at] = static_cast<uint8_t>(op.b);
                break;
            }
            case OP_MOVEAXECX: eax = ecx; break;
            case OP_CMP: cmp = eax > ecx ? 1 : (eax < ecx ? -1 : 0); break;
            case OP_JG:
                if (cmp > 0) {
                    pc = op.a - 1;
                }
                break;
            case OP_JL:
                if (cmp < 0) {
                    pc = op.a - 1;
                }
                break;
        }
    }
}

// --- instances ---

BoundClient *NewClient(Instance *inst, uint32_t off) {
    inst->clients.push_back(std::make_unique<BoundClient>());
    BoundClient *bc = inst->clients.back().get();
    std::memset(&bc->c, 0, sizeof(bc->c));
    bc->inst = inst;
    bc->off = off;
    return bc;
}

void OnClassDestructor(Csis::Class *, void *data) { // 0x81A80F
    BoundClient *bc = static_cast<BoundClient *>(data);
    Wr32(bc->inst->data.data() + bc->off + 0x10, 1);
}

void OnGlobalVariable(Csis::Parameter *p, void *data) { // 0x81A801
    BoundClient *bc = static_cast<BoundClient *>(data);
    Wr32(bc->inst->data.data() + bc->off + 0x18, p->iVal);
}

void OnClassData(Csis::Parameter *p, void *data) { // 0x81A81B
    BoundClient *bc = static_cast<BoundClient *>(data);
    uint8_t *s = bc->inst->data.data() + bc->off;
    for (int i = 0; i < s[0x10]; i++) {
        Wr32(s + 0x14 + 4 * i, p[i].iVal);
    }
    if (LogEnabled()) {
        char buf[160];
        int n = 0;
        for (int i = 0; i < s[0x10] && n < 140; i++) {
            n += std::snprintf(buf + n, sizeof(buf) - n, " %d", p[i].iVal);
        }
        buf[n] = 0;
        Log("module %d: member data%s", bc->inst->mod->id, buf);
    }
}

void OnFunctionCall(Csis::Parameter *p, void *data) { // 0x81A841
    BoundClient *bc = static_cast<BoundClient *>(data);
    uint8_t *s = bc->inst->data.data() + bc->off;
    for (int i = 0; i < s[0x18]; i++) {
        Wr32(s + 0x1C + 4 * i, p[i].iVal);
    }
    s[0x19] = 1;
}

// The module's constructor client (0x81A86B): a Csis instance of the class
// was created, so start an instance of the module.
void CreateModuleInstance(Csis::Class *pClass, Csis::Parameter *, void *clientData) {
    Lock lock;
    Module *mod = static_cast<Module *>(clientData);
    if (mod->curInstances >= mod->maxInstances) {
        Log("module %d: instance limit (%d)", mod->id, mod->maxInstances);
        return;
    }
    Instance *inst = new Instance;
    inst->mod = mod;
    inst->cls = pClass;
    inst->data.assign(mod->bank->base + mod->pdata, mod->bank->base + mod->pdata + mod->datasize);
    uint8_t *d = inst->data.data();
    // DESTROYSTATE.settings: module, instance, class (unused natively)
    Wr32(d + mod->destroyOff, mod->id);
    Wr32(d + mod->destroyOff + 4, 0);
    Wr32(d + mod->destroyOff + 8, 0);
    mod->instances.insert(mod->instances.begin(), inst);
    gInstances.insert(gInstances.begin(), inst);

    uint32_t off = 0x18;
    if (mod->classDestructorPresent) {
        BoundClient *bc = NewClient(inst, off);
        bc->c.dtor.pClientFunc = OnClassDestructor;
        bc->c.dtor.pClientData = bc;
        pClass->SubscribeDestructorFast(&bc->c.dtor);
        inst->dtorClient = bc;
        off += 0x14;
    }
    for (int i = 0; i < mod->numGlobals; i++, off += 0x1C) {
        BoundClient *bc = NewClient(inst, off);
        bc->c.fn.pClientFunc = OnGlobalVariable;
        bc->c.fn.pClientData = bc;
        if (HandleSlot *h = SlotHandle(d + off)) {
            Csis::GlobalVariable::SubscribeFast(&h->gv, &bc->c.fn);
        }
        inst->globalClients.push_back(bc);
    }
    if (mod->classDataPresent) {
        BoundClient *bc = NewClient(inst, off);
        bc->c.fn.pClientFunc = OnClassData;
        bc->c.fn.pClientData = bc;
        pClass->SubscribeMemberDataFast(&bc->c.fn);
        inst->cdataClient = bc;
        off += 0x14 + d[off + 0x10] * 4;
    }
    for (int i = 0; i < mod->numFunctions; i++) {
        BoundClient *bc = NewClient(inst, off);
        bc->c.fn.pClientFunc = OnFunctionCall;
        bc->c.fn.pClientData = bc;
        if (HandleSlot *h = SlotHandle(d + off)) {
            Csis::Function::SubscribeFast(&h->fn, &bc->c.fn);
        }
        inst->functionClients.push_back(bc);
        off += 0x1C + d[off + 0x18] * 4;
    }
    mod->curInstances++;
    Log("module %d: instance created (%d live)", mod->id, mod->curInstances);
}

// SNDAEMSI_updatedestroy (0x8196E8). The instance is freed by the timer
// once its code has returned.
void DestroyInstance(Instance *inst) {
    if (inst->dead) {
        return;
    }
    Module *mod = inst->mod;
    uint8_t *d = inst->data.data();
    mod->instances.erase(std::remove(mod->instances.begin(), mod->instances.end(), inst), mod->instances.end());
    if (inst->dtorClient != nullptr) {
        inst->cls->UnsubscribeDestructorFast(&inst->dtorClient->c.dtor);
    }
    for (BoundClient *bc : inst->globalClients) {
        if (HandleSlot *h = SlotHandle(d + bc->off)) {
            Csis::GlobalVariable::UnsubscribeFast(&h->gv, &bc->c.fn);
        }
    }
    if (inst->cdataClient != nullptr) {
        inst->cls->UnsubscribeMemberDataFast(&inst->cdataClient->c.fn);
    }
    for (BoundClient *bc : inst->functionClients) {
        if (HandleSlot *h = SlotHandle(d + bc->off)) {
            Csis::Function::UnsubscribeFast(&h->fn, &bc->c.fn);
        }
    }
    for (uint32_t po : mod->playerOffs) {
        uint8_t *ps = d + po;
        if (static_cast<signed char>(ps[0x10]) >= 0) {
            PlayerStop(ps);
        }
    }
    for (uint32_t co : mod->controllerOffs) {
        uint8_t *cs = d + co;
        if (Csis::Class *cls = ClassRef(cs + 8)) {
            cls->Release();
            SetClassRef(cs + 8, nullptr);
        }
    }
    mod->curInstances--;
    inst->dead = true;
    Log("module %d: instance destroyed (%d live)", mod->id, mod->curInstances);
}

// The AEMS variable timer client (0x81F98A): every module instance's code.
void TimerUpdate() {
    std::vector<Instance *> list = gInstances;
    for (Instance *inst : list) {
        if (!inst->dead && !inst->faulted) {
            RunModule(inst);
        }
    }
    for (size_t i = 0; i < gInstances.size();) {
        if (gInstances[i]->dead) {
            delete gInstances[i];
            gInstances.erase(gInstances.begin() + i);
        } else {
            i++;
        }
    }
}

std::atomic<bool> gTimerRunning{false};

void TimerThread() {
    auto next = std::chrono::steady_clock::now();
    const auto period = std::chrono::microseconds(static_cast<int>(kTimerPeriod * 1000));
    while (gTimerRunning.load()) {
        next += period;
        std::this_thread::sleep_until(next);
        Lock lock;
        TimerUpdate();
    }
}

void StartTimer() {
    bool expected = false;
    if (gTimerRunning.compare_exchange_strong(expected, true)) {
        std::thread(TimerThread).detach();
    }
}

int AddSampleBank(const uint8_t *bnkl, size_t size) {
    auto bank = std::make_unique<SampleBank>();
    if (!ea_audio::ParseBank(bnkl, size, &bank->sounds)) {
        return SNDERR_FORMAT;
    }
    bank->data = bnkl;
    bank->size = size;
    bank->decoded.resize(bank->sounds.size());
    for (size_t i = 0; i < gSampleBanks.size(); i++) {
        if (!gSampleBanks[i]) {
            gSampleBanks[i] = std::move(bank);
            return static_cast<int>(i);
        }
    }
    gSampleBanks.push_back(std::move(bank));
    return static_cast<int>(gSampleBanks.size() - 1);
}

} // namespace

// --- API --------------------------------------------------------------------

extern "C" {

void SNDSYS_entercritical() {
    CsisNativeLock();
}

void SNDSYS_leavecritical() {
    CsisNativeUnlock();
}

// PC: runs the registered service functions (async bank loaders); the native
// loaders complete synchronously.
void SNDSYS_service() {}

int SNDplaysetdef(SNDPLAYOPTS *pspo) { // 0x81D582
    std::memset(pspo, 0, sizeof(*pspo));
    pspo->vol = 0x7F;
    pspo->bend = 0x40;
    pspo->keynum = 0x3C;
    pspo->velocity = 0x7F;
    pspo->drylevel = 0x7F;
    pspo->fxlevel0 = 0;
    pspo->azimuth = 0;
    pspo->elevation = 0;
    pspo->pitchmult = 0x1000;
    pspo->timemult = 0x1000;
    pspo->tempomult = 0x1000;
    pspo->lowpasscutoff = 0xFFFF;
    pspo->highpasscutoff = 0;
    return 0;
}

int SNDBANK_play(int bhandle, int patnum, SNDPLAYOPTS *pspo) {
    Lock lock;
    SNDPLAYOPTS def;
    if (pspo == nullptr) {
        SNDplaysetdef(&def);
        pspo = &def;
    }
    return BankPlay(bhandle, patnum, pspo);
}

int SNDstop(int shandle) { // 0x81F39D
    Lock lock;
    int voice = VoiceIndex(shandle);
    if (voice >= 0) {
        port_audio::GetMixer()->Stop(gVoices[voice].mix);
        gVoices[voice].handle = -1;
        gVoices[voice].sample.reset();
    }
    return voice;
}

int SNDvol(int shandle, int vol) { // 0x81D7EF: 0..127
    Lock lock;
    int voice = VoiceIndex(shandle);
    if (voice >= 0) {
        gVoices[voice].progVol = vol * (1.0f / 127.0f);
        ApplyVoice(gVoices[voice]);
    }
    return voice;
}

int SNDpitchmult(int shandle, int pitchmult) { // 0x81E4BE
    Lock lock;
    int voice = VoiceIndex(shandle);
    if (voice >= 0) {
        gVoices[voice].pitchMult = pitchmult & 0xFFFF;
        ApplyVoice(gVoices[voice]);
    }
    return voice;
}

int SND3dpos(int shandle, int azimuth, int elevation) {
    Lock lock;
    int voice = VoiceIndex(shandle);
    if (voice >= 0 && gVoices[voice].channels == 1) {
        gVoices[voice].azimuth = static_cast<uint16_t>(azimuth);
        ApplyVoice(gVoices[voice]);
    }
    return voice;
}

int SNDtimeremaining(int shandle) {
    Lock lock;
    int voice = VoiceIndex(shandle);
    if (voice < 0) {
        return 0;
    }
    Voice &v = gVoices[voice];
    long cur = port_audio::GetMixer()->Position(v.mix);
    return static_cast<int>((static_cast<long>(v.sample->Frames()) - cur) * 1000 / v.sample->rate);
}

// 0x81D17D. The bank is used where it is unless mallocCb moves it (PC: into
// an audio bank slot); the fixup tables after the resident part are read
// from the original.
int SNDAEMS_addmodulebank(void *pBank, char *streamFileName, int streamFileNameOffset, void *(*mallocCb)(void *, int, int)) {
    Lock lock;
    uint8_t *src = static_cast<uint8_t *>(pBank);
    if (std::memcmp(src, "ABKC", 4) != 0) {
        Log("addmodulebank: not an ABKC bank");
        return SNDERR_FORMAT;
    }
    int resident = Rd32(src + MB_RESIDENTSIZE);
    int sfxOff = Rd32(src + MB_SFXBANKOFFSET);
    int sfxSize = Rd32(src + MB_SFXBANKSIZE);
    int midiOff = Rd32(src + MB_MIDIBANKOFFSET);
    int midiSize = Rd32(src + MB_MIDIBANKSIZE);
    int keep = resident + (sfxOff != 0 ? sfxSize : 0) + (midiOff != 0 ? midiSize : 0);
    uint8_t *base = static_cast<uint8_t *>(mallocCb != nullptr ? mallocCb(pBank, keep, Rd32(src + MB_TOTALSIZE)) : pBank);
    if (base == nullptr) {
        return SNDERR_MEM;
    }
    if (base != src) {
        std::memmove(base, src, keep);
    }

    auto mb = std::make_unique<ModuleBank>();
    gModuleBankSerial++;
    if (gModuleBankSerial < 0) {
        gModuleBankSerial = 1;
    }
    mb->handle = gModuleBankSerial;
    mb->base = base;
    Wr32(base + MB_HANDLE, mb->handle);
    if (sfxOff != 0) {
        mb->sfxbhandle = AddSampleBank(base + sfxOff, sfxSize);
        if (mb->sfxbhandle < 0) {
            Log("addmodulebank: bad sample bank");
        }
    }
    Wr32(base + MB_SFXBHANDLE, mb->sfxbhandle);
    Wr32(base + MB_MIDIBHANDLE, -1);

    // function fixups: positions of the call operands
    const uint8_t *ff = src + Rd32(src + MB_FUNCFIXUPOFFSET);
    std::vector<uint32_t> funcFixups(Rd32(ff));
    for (size_t i = 0; i < funcFixups.size(); i++) {
        funcFixups[i] = Rd32(ff + 4 + 4 * i);
    }

    // modules
    uint32_t mo = Rd32(src + MB_MODULEOFFSET);
    int nummodules = Rd16(src + MB_NUMMODULES);
    for (int i = 0; i < nummodules; i++) {
        auto mod = std::make_unique<Module>();
        uint8_t *m = base + mo;
        mod->bank = mb.get();
        mod->headerOff = mo;
        mod->id = Rd32(m);
        mod->maxInstances = static_cast<int16_t>(Rd16(m + 0x1E));
        mod->numGlobals = Rd16(m + 0x20);
        mod->numFunctions = Rd16(m + 0x22);
        mod->numPlayers = m[0x24];
        mod->classDestructorPresent = m[0x25] != 0;
        mod->classDataPresent = m[0x26] != 0;
        mod->numClassControllers = m[0x27];
        uint32_t pcode = Rd32(m + 0x28);
        mod->pdata = Rd32(m + 0x2C);
        mod->datasize = Rd32(m + 0x30);
        mod->destroyOff = Rd32(m + 0x34);
        for (int k = 0; k < mod->numPlayers; k++) {
            mod->playerOffs.push_back(Rd32(m + 0x3C + 4 * k));
        }
        for (int k = 0; k < mod->numClassControllers; k++) {
            mod->controllerOffs.push_back(Rd32(m + 0x3C + 4 * (mod->numPlayers + k)));
        }
        if (!DecodeModuleCode(base + pcode, mod->pdata - pcode, pcode, funcFixups, &mod->code)) {
            std::fprintf(stderr, "[snd] module %d: can't decode its code\n", mod->id);
            mod->code.clear();
        }
        for (uint32_t po : mod->playerOffs) {
            Wr32(base + mod->pdata + po, mb->handle); // PLAYERSETTINGS.pModuleBank
        }
        mo += 0x3C + 4 * (mod->numPlayers + mod->numClassControllers);
        mb->modules.push_back(std::move(mod));
    }

    // Static data fixups (sample groups, tables) stay bank offsets: the node
    // functions add the bank address themselves.

    // interface fixups: the Csis handles in module headers and state blocks
    const uint8_t *ifh = src + Rd32(src + MB_INTERFACEOFFSET);
    int numIfx = Rd32(ifh);
    for (int i = 0; i < numIfx; i++) {
        const uint8_t *ref = ifh + 4 + 12 * i;
        uint32_t handleOff = Rd32(ref);
        const uint8_t *idp = src + Rd32(ref + 4);
        Csis::InterfaceId id;
        id.systemCrc = static_cast<short>(Rd16(idp));
        id.interfaceCrc = static_cast<short>(Rd16(idp + 2));
        id.pString = reinterpret_cast<const char *>(idp + 4);
        int type = ref[8];
        Module *owner = nullptr;
        for (auto &mod : mb->modules) {
            if (handleOff == mod->headerOff + 4) {
                owner = mod.get();
            }
        }
        Csis::Result r;
        if (owner != nullptr) {
            r = owner->classHandle.SetFast(&id);
        } else {
            auto slot = std::make_unique<HandleSlot>();
            slot->type = static_cast<uint8_t>(type);
            r = type == 0 ? slot->gv.SetFast(&id) : type == 1 ? slot->cls.SetFast(&id) : slot->fn.SetFast(&id);
            gHandles.push_back(std::move(slot));
            Wr32(base + handleOff, static_cast<int32_t>(gHandles.size()));
            Wr32(base + handleOff + 4, 0);
        }
        Log("bank %d: interface %s (%s) -> %d", mb->handle, id.pString, type == 0 ? "global" : type == 1 ? "class" : "function", r);
    }

    for (auto &mod : mb->modules) {
        mod->constructorClient.pClientFunc = CreateModuleInstance;
        mod->constructorClient.pClientData = mod.get();
        Csis::Class::SubscribeConstructorFast(&mod->classHandle, &mod->constructorClient);
        Log("bank %d: module %d, %d ops, %d players, max %d instances", mb->handle, mod->id, static_cast<int>(mod->code.size()),
            mod->numPlayers, mod->maxInstances);
    }

    int handle = mb->handle;
    gModuleBanks.push_back(std::move(mb));
    StartTimer();
    return handle;
}

int SNDAEMS_removemodulebank(int mbhandle) { // 0x81A980
    Lock lock;
    for (size_t i = 0; i < gModuleBanks.size(); i++) {
        ModuleBank *mb = gModuleBanks[i].get();
        if (mb->handle != mbhandle) {
            continue;
        }
        for (auto &mod : mb->modules) {
            Csis::Class::UnsubscribeConstructorFast(&mod->classHandle, &mod->constructorClient);
            std::vector<Instance *> list = mod->instances;
            for (Instance *inst : list) {
                DestroyInstance(inst);
            }
        }
        for (size_t k = 0; k < gInstances.size();) {
            if (gInstances[k]->dead) {
                delete gInstances[k];
                gInstances.erase(gInstances.begin() + k);
            } else {
                k++;
            }
        }
        if (mb->sfxbhandle >= 0) {
            gSampleBanks[mb->sfxbhandle].reset(); // playing voices keep their decoded samples
        }
        gModuleBanks.erase(gModuleBanks.begin() + i);
        Log("bank %d removed", mbhandle);
        return 0;
    }
    return SNDERR_INV_PARAM;
}

// The async loaders (GameCube ARAM paths; on PC the bank-file variant can't
// complete with a sample bank) -- the PC game loads every bank whole and adds
// it with SNDAEMS_addmodulebank.
int gAsyncMemDone = 1;

int SNDAEMS_asyncloadmodulebank(char *, int, char *, int, void *, int, void *(*)(int)) {
    return SNDERR_UNSUPPORTED;
}

int SNDAEMS_asyncloadmodulebankdone() {
    return SNDERR_UNSUPPORTED;
}

int SNDAEMS_asyncloadmodulebankmem(void *pModuleBank, char *streamFileName, int streamFileOffset, void *(*)(int)) {
    gAsyncMemDone = 1;
    return SNDAEMS_addmodulebank(pModuleBank, streamFileName, streamFileOffset, nullptr);
}

int SNDAEMS_asyncloadmodulebankmemdone() {
    return gAsyncMemDone;
}

} // extern "C"

namespace Snd {

Result System::Init(const int) {
    port_audio::StartAudioOutput();
    return RESULT_OK;
}

Result System::VectorToCsisMutex() {
    return RESULT_OK;
}

} // namespace Snd
