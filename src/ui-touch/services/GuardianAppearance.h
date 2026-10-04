// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
namespace guardian {
enum class Appearance : uint8_t { Blue, Green };
Appearance loadAppearance();
bool saveAppearance(Appearance value);
bool loadMessageAlert();
bool saveMessageAlert(bool enabled);
}
