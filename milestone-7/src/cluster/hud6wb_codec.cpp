// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Adapted algorithms: JayJay3071/nbt-android-auto 0fb9fd2 (see BMW_6WB.md).
#include "cluster/hud6wb_codec.hpp"
namespace nbt {
namespace hudterm {
namespace {
uint32_t nextCodePoint(const std::string& text, size_t& i) {
    const uint32_t replacement = 0xfffd;
    const unsigned lead = (unsigned char)text[i++];
    if (lead < 0x80) return lead ? lead : replacement;

    unsigned remaining;
    uint32_t cp, minimum;
    if (lead >= 0xc2 && lead <= 0xdf) {
        remaining = 1;
        cp = lead & 0x1f;
        minimum = 0x80;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        remaining = 2;
        cp = lead & 0x0f;
        minimum = 0x800;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        remaining = 3;
        cp = lead & 0x07;
        minimum = 0x10000;
    } else {
        return replacement;
    }
    while (remaining--) {
        if (i == text.size()) return replacement;
        const unsigned byte = (unsigned char)text[i];
        if ((byte & 0xc0) != 0x80) return replacement;
        ++i;
        cp = (cp << 6) | (byte & 0x3f);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return replacement;
    return cp;
}
} // namespace

std::vector<uint8_t> unicodeText(uint16_t handle, const std::string& text, size_t maxUnits) {
    if (maxUnits > kMaxExtendedUtf16Units) maxUnits = kMaxExtendedUtf16Units;
    std::vector<uint8_t> payload;
    putBe16(payload, handle);
    payload.push_back(0);
    for (size_t i = 0; i < text.size() && maxUnits;) {
        uint32_t cp = nextCodePoint(text, i);
        if (cp <= 0xffff) {
            putBe16(payload, (uint16_t)cp);
            --maxUnits;
        } else {
            if (maxUnits < 2) break;
            cp -= 0x10000;
            putBe16(payload, (uint16_t)(0xd800 + (cp >> 10)));
            putBe16(payload, (uint16_t)(0xdc00 + (cp & 0x3ff)));
            maxUnits -= 2;
        }
    }
    putBe16(payload, 0);
    return payload;
}

static void pixel(std::vector<uint8_t>& p, uint32_t c) {
    p.push_back((uint8_t)(c >> 24));
    p.push_back((uint8_t)c);
    p.push_back((uint8_t)(c >> 8));
    p.push_back((uint8_t)(c >> 16));
}
std::vector<uint8_t> rle32(const std::vector<uint32_t>& px) {
    std::vector<uint8_t> p;
    size_t i = 0;
    while (i < px.size()) {
        size_t n = 1;
        while (i + n < px.size() && n < 127 && px[i + n] == px[i])
            ++n;
        if (n >= 2) {
            p.push_back((uint8_t)(0x80 | n));
            pixel(p, px[i]);
            i += n;
            continue;
        }
        size_t start = i++;
        while (i < px.size() && i - start < 127) {
            if (i + 1 < px.size() && px[i] == px[i + 1]) break;
            ++i;
        }
        p.push_back((uint8_t)(i - start));
        for (size_t k = start; k < i; ++k)
            pixel(p, px[k]);
    }
    // Original compressor always appends padding, including for an aligned stream.
    size_t pad = 8 - p.size() % 8;
    p.insert(p.end(), pad, 0);
    return p;
}
std::vector<uint8_t> bitmap(uint16_t h, uint8_t id, const std::vector<uint32_t>& px, unsigned w, unsigned height,
                            size_t cap) {
    std::vector<uint8_t> p;
    if (!w || !height || w > 1024 || height > 1024 || px.size() != (size_t)w * height) return p;
    std::vector<uint8_t> r = rle32(px);
    if (r.size() + 10 > cap) return p;
    putBe16(p, h);
    putBe16(p, 0);
    putBe16(p, 0);
    p.push_back(1);
    p.push_back(1);
    p.push_back(0);
    p.push_back(id);
    p.insert(p.end(), r.begin(), r.end());
    return p;
}
} // namespace hudterm
} // namespace nbt
