// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HistoryFormat.h"
namespace ui { namespace history {
void encode(const MessageTypes::UIMessage& source, UiSegMsg* destination);
void decode(const UiSegMsg& source, MessageTypes::UIMessage* destination);
// Decode the frozen v6 ring record without inventing an extended sender name.
void decodeLegacy(const UiHistoryMsg& source, MessageTypes::UIMessage* destination);
} }
