// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <stdint.h>
namespace guardian {
void rpcConnect();
void rpcDisconnect();
void rpcAbort();
void rpcSubscribe(bool enabled);
bool rpcReady();
bool rpcBusy();
bool rpcStart(const std::string& json, uint32_t now);
bool rpcTake(std::string& json, std::string& error);
bool rpcReceive(const uint8_t* bytes, size_t size, uint32_t now);
// Event is an independent PC-to-FW write stream, not a notification or RPC.
bool eventReceive(const uint8_t* bytes, size_t size, uint32_t now);
bool eventTake(std::string& json);
// Runs under the RPC mutex: BLE send must be nonblocking and never call UI.
bool rpcTick(uint32_t now, bool (*notify)(const uint8_t*, size_t));
}
