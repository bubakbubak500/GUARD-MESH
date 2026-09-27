#include "TerminalSession.h"
#include "../i18n.h"
#include <cstring>
#include <strings.h>
#include <cstdio>
namespace ui {
void TerminalSession::termCmdTo(const char* arg) {
  while (*arg == ' ' || *arg == '\t') ++arg;
  if (!*arg) {
    if (s_term_to_set) {
      char r[64];
      snprintf(r, sizeof r, "recipient: %s%s",
               s_term_to_is_channel ? "[chan] " : "", s_term_to_name);
      _host.log(TERM_C_INFO, nullptr, r);
    } else {
      _host.log(TERM_C_INFO, nullptr, "no recipient set - 'to <name>'");
    }
    return;
  }
  size_t alen = strlen(arg);
  int nc = _host.contactCount();
  for (int i = 0; i < nc; ++i) {
    Contact c{};
    if (_host.contactAt((uint32_t)i, c) && c.name[0] &&
        strncasecmp(c.name, arg, alen) == 0) {
      memcpy(s_term_to_pub, c.key, 32);
      s_term_to_is_channel = false;
      s_term_to_set = true;
      strncpy(s_term_to_name, c.name, sizeof(s_term_to_name) - 1);
      s_term_to_name[sizeof(s_term_to_name) - 1] = '\0';
      char r[64];
      snprintf(r, sizeof r, TR("now talking to %s"), s_term_to_name);
      _host.log(TERM_C_INFO, nullptr, r);
      _host.log(TERM_C_INFO, nullptr, "(type to send; 'exit' leaves)");
      return;
    }
  }
  for (int s = 0; s < _host.channelCount; ++s) {
    Channel cd;
    if (_host.channelAt(s, cd) && cd.name[0] &&
        strncasecmp(cd.name, arg, alen) == 0) {
      s_term_to_chan_slot = (int16_t)s;
      s_term_to_is_channel = true;
      s_term_to_set = true;
      strncpy(s_term_to_name, cd.name, sizeof(s_term_to_name) - 1);
      s_term_to_name[sizeof(s_term_to_name) - 1] = '\0';
      char r[64];
      snprintf(r, sizeof r, TR("now on channel %s"), s_term_to_name);
      _host.log(TERM_C_INFO, nullptr, r);
      _host.log(TERM_C_INFO, nullptr, "(type to send; 'exit' leaves)");
      return;
    }
  }
  char r[80];
  snprintf(r, sizeof r, "no contact/channel matching '%s'", arg);
  _host.log(TERM_C_ERR, nullptr, r);
}

void TerminalSession::termCmdList() {
  int nc = _host.contactCount();
  char hdr[40];
  snprintf(hdr, sizeof hdr, TR("contacts (%d):"), nc);
  _host.log(TERM_C_INFO, nullptr, hdr);
  int shown = 0;
  for (int i = 0; i < nc && shown < 60; ++i) {
    Contact c{};
    if (_host.contactAt((uint32_t)i, c) && c.name[0]) {
      reply("  ", c.name);
      ++shown;
    }
  }
}

void TerminalSession::termCmdChannels() {
  _host.log(TERM_C_INFO, nullptr, "channels:");
  for (int s = 0; s < _host.channelCount; ++s) {
    Channel cd;
    if (_host.channelAt(s, cd) && cd.name[0]) {
      char line[48];
      snprintf(line, sizeof line, "  [%d] %s", s, cd.name);
      reply(nullptr, line);
    }
  }
}

void TerminalSession::termCmdSend(const char* text) {
  while (*text == ' ' || *text == '\t') ++text;
  if (!*text) { _host.log(TERM_C_ERR, nullptr, "usage: send <text>"); return; }
  if (!s_term_to_set) { _host.log(TERM_C_ERR, nullptr, "no recipient - use 'to <name>' first"); return; }
  if (s_term_to_is_channel) {
    Channel current{};
    if (!_host.channelAt(s_term_to_chan_slot, current) || strcmp(current.name, s_term_to_name)) {
      _host.log(TERM_C_ERR, nullptr, "channel not found");
      return;
    }
  }
  _host.send(s_term_to_is_channel, s_term_to_pub, s_term_to_chan_slot, s_term_to_name, text);
}

void TerminalSession::termCmdPublic(const char* text) {
  while (*text == ' ' || *text == '\t') ++text;
  if (!*text) { _host.log(TERM_C_ERR, nullptr, "usage: public <text>"); return; }
  Channel cd;
  if (!_host.channelAt(0, cd) || !cd.name[0]) { _host.log(TERM_C_ERR, nullptr, "no public channel"); return; }
  _host.send(true, nullptr, 0, cd.name, text);
}

void TerminalSession::termCmdExit() {
  if (!s_term_to_set) { _host.log(TERM_C_INFO, nullptr, "not in a chat"); return; }
  char r[64];
  snprintf(r, sizeof r, "left %s", s_term_to_name);
  s_term_to_set        = false;
  s_term_to_is_channel = false;
  s_term_to_chan_slot  = -1;
  s_term_to_name[0]    = '\0';
  _host.log(TERM_C_INFO, nullptr, r);
}

bool TerminalSession::termIsCliKeyword(const char* word, size_t len) {
  static const char* kw[] = {
    "help", "ver", "version", "clock", "time", "status", "get", "set",
    "advert", "advert.zerohop", "reboot", "wifi", "mqtt", "tcp", "ble", "ota",
    "ok", "cancel",
  };
  for (const char* k : kw) {
    if (strlen(k) == len && strncasecmp(word, k, len) == 0) return true;
  }
  return false;
}

bool TerminalSession::terminalRunChatCommand(const char* cmd) {
  if (!cmd) return false;
  while (*cmd == ' ' || *cmd == '\t') ++cmd;
  auto is = [](const char* s, const char* name, const char** rest) -> bool {
    size_t n = strlen(name);
    if (strncasecmp(s, name, n) == 0 && (s[n] == '\0' || s[n] == ' ' || s[n] == '\t')) {
      if (rest) { const char* r = s + n; while (*r == ' ' || *r == '\t') ++r; *rest = r; }
      return true;
    }
    return false;
  };
  const char* rest = nullptr;
  if (is(cmd, "help", &rest) || is(cmd, "?", &rest)) {   // prepend the messaging commands, then fall through to the config-CLI help
    _host.log(TERM_C_INFO, nullptr, "messaging:");
    reply("  ", "list / contacts    - list your contacts");
    reply("  ", "channels           - list channels");
    reply("  ", "to <name>          - talk to a contact or channel");
    reply("  ", "send <text>        - send to the current recipient");
    reply("  ", "public <text>      - send on the public channel");
    reply("  ", "exit / leave       - stop talking to that recipient");
    reply("  ", "(after 'to', a bare line is sent as a message)");
    _host.log(TERM_C_INFO, nullptr, "node / config:");
    return false;   // config-CLI help is appended by runLocalCli
  }
  if (is(cmd, "list", &rest) || is(cmd, "contacts", &rest)) { termCmdList();     return true; }
  if (is(cmd, "channels", &rest))                           { termCmdChannels(); return true; }
  if (is(cmd, "to", &rest))                                 { termCmdTo(rest);   return true; }
  if (is(cmd, "exit", &rest) || is(cmd, "leave", &rest))    { termCmdExit();     return true; }
  if (is(cmd, "send", &rest))                               { termCmdSend(rest); return true; }
  if (is(cmd, "public", &rest))                             { termCmdPublic(rest); return true; }
  // Room mode: once a recipient is selected, a line whose first word isn't a
  // config-CLI keyword is sent straight to that recipient as a chat message.
  // (To send text that starts with a keyword, use `send <text>`.)
  if (s_term_to_set && *cmd) {
    size_t wl = 0;
    while (cmd[wl] && cmd[wl] != ' ' && cmd[wl] != '\t') ++wl;
    if (!termIsCliKeyword(cmd, wl)) {
      termCmdSend(cmd);
      return true;
    }
  }
  return false;
}
}
