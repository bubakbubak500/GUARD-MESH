// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ui {
struct FirmwareRelease {
  char tag[64]{};
  char url[256]{};
  char sha256[65]{};
  uint32_t size = 0;
};
// Bounded GitHub metadata, exact board asset, repository URL and SHA-256.
bool parseFirmwareRelease(const char *, size_t, const char *board, FirmwareRelease &, char *, size_t);
bool validFirmwareRelease(const FirmwareRelease &, const char *board);
bool newerFirmwareRelease(const char *candidate, const char *installed);
} // namespace ui
