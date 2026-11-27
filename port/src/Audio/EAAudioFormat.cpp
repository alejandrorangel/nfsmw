// See EAAudioFormat.h.

#include "EAAudioFormat.h"

#include <cstring>

namespace ea_audio {

namespace {

uint32_t ReadLE32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t ReadBE32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

int16_t ReadBE16(const uint8_t *p) {
    return static_cast<int16_t>((p[0] << 8) | p[1]);
}

int Clamp16(int v) {
    return v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
}

const int kCoef1[4] = {0, 240, 460, 392};
const int kCoef2[4] = {0, 0, -208, -220};

} // namespace

size_t ParsePatchHeader(const uint8_t *p, size_t size, PatchHeader *out) {
    size_t o = 0;
    while (o < size) {
        uint8_t tag = p[o++];
        if (tag == 0xFF) {
            return o;
        }
        if (tag >= 0xFC && tag <= 0xFE) {
            continue;
        }
        if (o >= size) {
            return 0;
        }
        uint8_t len = p[o++];
        if (o + len > size) {
            return 0;
        }
        uint32_t value = 0;
        for (int i = 0; i < len; i++) {
            value = (value << 8) | p[o + i];
        }
        o += len;
        switch (tag) {
            case 0x80: out->version = static_cast<int>(value); break;
            case 0x82: out->channels = static_cast<int>(value); break;
            case 0x83: out->codec = static_cast<int>(value); break;
            case 0x84: out->sample_rate = static_cast<int>(value); break;
            case 0x85: out->num_samples = static_cast<int>(value); break;
            case 0x86: out->loop_start = static_cast<int>(value); break;
            case 0x87: out->loop_length = static_cast<int>(value); break;
            case 0x88: out->data_offset = static_cast<int>(value); break;
            default: break;
        }
    }
    return 0;
}

size_t DecodeXaR2(XaChannel &ch, const uint8_t *data, size_t size, int16_t *out, size_t max_samples, size_t stride) {
    size_t written = 0;
    size_t o = 0;
    while (written < max_samples && o < size) {
        uint8_t info = data[o];
        if (info == 0xEE) {
            if (o + 61 > size) {
                break;
            }
            ch.hist1 = ReadBE16(data + o + 1);
            ch.hist2 = ReadBE16(data + o + 3);
            for (int i = 0; i < 28 && written < max_samples; i++) {
                int16_t s = ReadBE16(data + o + 5 + i * 2);
                out[written++ * stride] = s;
                ch.hist2 = ch.hist1;
                ch.hist1 = s;
            }
            o += 61;
        } else {
            if (o + 15 > size) {
                break;
            }
            int c1 = kCoef1[(info >> 4) & 3];
            int c2 = kCoef2[(info >> 4) & 3];
            int shift = (info & 0x0F) + 16;
            for (int i = 0; i < 14; i++) {
                uint8_t byte = data[o + 1 + i];
                for (int n = 0; n < 2 && written < max_samples; n++) {
                    int nibble = n == 0 ? (byte >> 4) : (byte & 0x0F);
                    int s = nibble >= 8 ? nibble - 16 : nibble;
                    int v = Clamp16(static_cast<int>((static_cast<int64_t>(s) << 28) >> shift) + ((ch.hist1 * c1 + ch.hist2 * c2) >> 8));
                    out[written++ * stride] = static_cast<int16_t>(v);
                    ch.hist2 = ch.hist1;
                    ch.hist1 = v;
                }
            }
            o += 15;
        }
    }
    return written;
}

bool DecodeStream(const uint8_t *data, size_t size, PatchHeader *header, std::vector<int16_t> *pcm, size_t *consumed) {
    if (size < 16 || std::memcmp(data, "SCHl", 4) != 0) {
        return false;
    }
    uint32_t block = ReadLE32(data + 4);
    *header = PatchHeader();
    // 'GSTR'/'PT\0\0' marker, 4 bytes of flags, then tags.
    if (ParsePatchHeader(data + 16, block > 16 ? block - 16 : 0, header) == 0 || header->channels < 1 || header->channels > 8) {
        return false;
    }
    int channels = header->channels;
    XaChannel state[8];
    pcm->clear();
    size_t o = block;
    while (o + 8 <= size) {
        const uint8_t *b = data + o;
        uint32_t len = ReadLE32(b + 4);
        if (len < 8 || o + len > size) {
            break;
        }
        if (std::memcmp(b, "SCDl", 4) == 0 && len >= 12 + 4u * channels) {
            uint32_t samples = ReadBE32(b + 8);
            size_t base = pcm->size();
            pcm->resize(base + static_cast<size_t>(samples) * channels);
            const uint8_t *chan_data = b + 12 + 4 * channels;
            size_t chan_area = len - (12 + 4 * channels);
            for (int c = 0; c < channels; c++) {
                uint32_t start = ReadBE32(b + 12 + 4 * c);
                uint32_t end = c + 1 < channels ? ReadBE32(b + 12 + 4 * (c + 1)) : static_cast<uint32_t>(chan_area);
                if (start > end || end > chan_area) {
                    return false;
                }
                DecodeXaR2(state[c], chan_data + start, end - start, pcm->data() + base + c, samples, channels);
            }
        } else if (std::memcmp(b, "SCEl", 4) == 0) {
            o += len;
            break;
        }
        o += len;
    }
    *consumed = o;
    return true;
}

bool ParseBank(const uint8_t *bnkl, size_t size, std::vector<BankSound> *sounds) {
    if (size < 12 || std::memcmp(bnkl, "BNKl", 4) != 0) {
        return false;
    }
    int count = bnkl[6] | (bnkl[7] << 8);
    if (12 + 4u * count > size) {
        return false;
    }
    sounds->assign(count, BankSound());
    for (int i = 0; i < count; i++) {
        size_t slot = 12 + 4u * i;
        uint32_t rel = ReadLE32(bnkl + slot);
        if (rel == 0 || slot + rel + 4 > size || std::memcmp(bnkl + slot + rel, "PT\0\0", 4) != 0) {
            continue;
        }
        BankSound &sound = (*sounds)[i];
        size_t header_at = slot + rel + 4;
        if (ParsePatchHeader(bnkl + header_at, size - header_at, &sound.header) == 0 || sound.header.data_offset < 0 ||
            static_cast<size_t>(sound.header.data_offset) >= size) {
            continue;
        }
        sound.data = bnkl + sound.header.data_offset;
        sound.data_size = size - sound.header.data_offset;
    }
    return true;
}

} // namespace ea_audio
