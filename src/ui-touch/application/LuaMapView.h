// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../widgets/MapTileLayer.h"
namespace ui { namespace maps {
struct LuaMapHost { widgets::MapTileLayer::Host tiles; bool (*night)(); };
void configureLuaMap(LuaMapHost);
} }
