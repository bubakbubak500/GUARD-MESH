// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ui {
// Read only ESP application/segment headers for diagnostics. This is a size
// measurement, not image validation; the bootloader/OTA writer validates images.
inline size_t firmwareImageSize(void *context, size_t capacity,
                               bool (*read)(void *, size_t, void *, size_t)) {
  uint8_t header[24];
  if (!read || capacity < sizeof header || !read(context, 0, header, sizeof header) ||
      header[0] != 0xe9 || !header[1] || header[1] > 16 || header[23] > 1) return 0;
  size_t offset = sizeof header;
  for (unsigned i = 0; i < header[1]; ++i) {
    uint8_t segment[8];
    if (capacity - offset < sizeof segment || !read(context, offset, segment, sizeof segment)) return 0;
    offset += sizeof segment;
    const uint32_t length = uint32_t(segment[4]) | uint32_t(segment[5]) << 8 |
                            uint32_t(segment[6]) << 16 | uint32_t(segment[7]) << 24;
    if (length > capacity - offset) return 0;
    offset += length;
  }
  // The checksum occupies the last byte of a 16-byte aligned block.
  const size_t tail = 16 - offset % 16 + (header[23] ? 32 : 0);
  return tail <= capacity - offset ? offset + tail : 0;
}
} // namespace ui
