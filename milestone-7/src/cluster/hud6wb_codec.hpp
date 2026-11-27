// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Wire layouts and RLE32 ABGR algorithm adapted from JayJay3071/nbt-android-auto,
// src/android_auto/knob/nbt_cluster.cpp at 0fb9fd2. No x86 DSI code is used.
// Declares the helpers that build 6WB text and bitmap payloads.
// Объявляет функции формирования текстовых и растровых данных для 6WB.
#ifndef CLUSTER_HUD6WB_CODEC_HPP
#define CLUSTER_HUD6WB_CODEC_HPP
#include "cluster/hudterm.hpp"
namespace nbt {
namespace hudterm {
// HUDTerminal UTF-16BE without BOM. The limit counts 16-bit code units.
std::vector<uint8_t> unicodeText(uint16_t handle, const std::string& utf8, size_t maxUnits);
std::vector<uint8_t> rle32(const std::vector<uint32_t>& argb);
// Full surface bitmap, one application segment. Oversize streams are refused, not truncated.
std::vector<uint8_t> bitmap(uint16_t handle, uint8_t id, const std::vector<uint32_t>& argb, unsigned width,
                            unsigned height, size_t maxBytes);
} // namespace hudterm
} // namespace nbt
#endif
