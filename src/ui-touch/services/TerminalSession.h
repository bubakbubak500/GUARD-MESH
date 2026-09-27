// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ui {
enum TerminalColor : uint32_t {
  TERM_C_INPUT=0x7fe0ff, TERM_C_TX=0x63d863, TERM_C_RX_DM=0x4fc3f7,
  TERM_C_RX_CH=0xffcc66, TERM_C_ERR=0xff6b6b, TERM_C_INFO=0x9aa7b0,
  TERM_C_REPLY=0xcfd6dc, TERM_C_BANNER=0x7f8c99
};
// One recipient session can be shared by the device and web console. All calls
// run on the UI thread; the backend supplies copied contact/channel snapshots.
class TerminalSession {
public:
  struct Contact { char name[40]; uint8_t key[32]; };
  struct Channel { char name[40]; };
  struct Host {
    int (*contactCount)();
    bool (*contactAt)(uint32_t, Contact&);
    bool (*channelAt)(int, Channel&);
    int channelCount;
    void (*send)(bool, const uint8_t*, int16_t, const char*, const char*);
    void (*log)(uint32_t, const char*, const char*);
  };
  explicit TerminalSession(Host host) : _host(host) {}
  bool terminalRunChatCommand(const char* command);
private:
  Host _host;
  void reply(const char* prefix, const char* text) { _host.log(TERM_C_REPLY, prefix, text); }
  void termCmdTo(const char*);
  void termCmdList();
  void termCmdChannels();
  void termCmdSend(const char*);
  void termCmdPublic(const char*);
  void termCmdExit();
  static bool termIsCliKeyword(const char*, size_t);
// Current terminal recipient, set by `to <name>` (a DM contact OR a channel).
bool          s_term_to_set        = false;
bool          s_term_to_is_channel = false;
uint8_t       s_term_to_pub[32]    = {0};
int16_t       s_term_to_chan_slot  = -1;
char          s_term_to_name[40]   = {0};

};
}
