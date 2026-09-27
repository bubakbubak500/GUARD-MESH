// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace ui { namespace images {
// UI-thread-only decoder pool. Returned allocations belong to the caller and
// must be released with lvglPsramFree; a supplied destination remains borrowed.
uint8_t* decodeBmpToRgb565(const uint8_t*, size_t, int*, int*);
uint8_t* decodeJpegToRgb565(const uint8_t*, size_t, int*, int*);
uint8_t* decodeJpegScaledToRgb565(const uint8_t*, size_t, int*, int*, int maxDimension, uint8_t* destination = nullptr, size_t capacity = 0);
uint8_t* decodePngToRgb565(const uint8_t*, size_t, int*, int*, uint8_t* destination = nullptr, size_t capacity = 0);
const char* lastError();
void clearError();
} }
