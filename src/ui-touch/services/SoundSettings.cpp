// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoundSettings.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
#include <cstdio>
#include <cstring>
namespace ui {
static_assert(SoundSettings::SlotCount == 3 && TOUCH_SND_MSG == 0 && TOUCH_SND_DM == 1 && TOUCH_SND_MEN == 2,
              "Persisted notification slots must match");
static_assert(notification::ColorCount == TOUCH_ATTAKY_NOTIFY_COLOR_COUNT, "Persisted colors must match");
SoundSettings::State SoundSettings::read() const {
  State state;
  state.flags[Master] = _host.quiet && !_host.quiet(_host.context);
  state.flags[Loud] = touchPrefsGetLoudAlerts();
  state.flags[Messages] = touchPrefsGetSoundMessages();
  state.flags[Direct] = touchPrefsGetSoundDirect();
  state.flags[Mentions] = touchPrefsGetSoundMentions();
  state.flags[Dnd] = touchPrefsGetDndEnabled();
  state.flags[Indicator] = touchPrefsGetAttakyNotifyEnabled();
  state.start = touchPrefsGetDndStartSlot();
  state.end = touchPrefsGetDndEndSlot();
  state.volume = touchPrefsGetSoundVolume();
  state.colors[0] = touchPrefsGetAttakyNotifyRoomColor();
  state.colors[1] = touchPrefsGetAttakyNotifyDmColor();
  return state;
}
bool SoundSettings::supports(Flag flag) const {
  if (flag < Master || flag >= FlagCount)
    return false;
  if (flag == Indicator)
    return _host.capabilities.indicator;
  return _host.capabilities.sound && (flag != Loud || _host.capabilities.loud);
}
void SoundSettings::changed() {
  if (_host.changed)
    _host.changed(_host.context);
}
bool SoundSettings::setFlag(Flag flag, bool on, uint32_t now) {
  if (!supports(flag))
    return false;
  bool saved = true;
  switch (flag) {
  case Master:
    if (_host.setQuiet)
      _host.setQuiet(_host.context, !on);
    break;
  case Loud:
    saved = touchPrefsSetLoudAlerts(on);
    break;
  case Messages:
    touchPrefsSetSoundMessages(on);
    break;
  case Direct:
    touchPrefsSetSoundDirect(on);
    break;
  case Mentions:
    touchPrefsSetSoundMentions(on);
    break;
  case Dnd:
    touchPrefsSetDndEnabled(on);
    break;
  case Indicator:
    saved = touchPrefsSetAttakyNotifyEnabled(on);
    if (!on)
      stopBlink(now);
    break;
  default:
    return false;
  }
  changed();
  return saved;
}
void SoundSettings::stepTime(bool end, int direction) {
  if (!_host.capabilities.sound || !direction)
    return;
  if (end)
    touchPrefsSetDndEndSlot(notification::stepSlot(touchPrefsGetDndEndSlot(), direction));
  else
    touchPrefsSetDndStartSlot(notification::stepSlot(touchPrefsGetDndStartSlot(), direction));
  changed();
}
void SoundSettings::stepVolume(int direction) {
  if (!_host.capabilities.sound || !_host.capabilities.volume || !direction)
    return;
  int volume = touchPrefsGetSoundVolume() + (direction < 0 ? -10 : 10);
  if (volume < 0)
    volume = 0;
  if (volume > 100)
    volume = 100;
  touchPrefsSetSoundVolume(volume);
  if (_host.volume)
    _host.volume(_host.context, volume);
  changed();
}
bool SoundSettings::setColor(unsigned row, unsigned color) {
  if (!_host.capabilities.indicator || row > 1 || color >= notification::ColorCount)
    return false;
  return row ? touchPrefsSetAttakyNotifyDmColor(color) : touchPrefsSetAttakyNotifyRoomColor(color);
}
bool SoundSettings::dndActive() const {
  if (!touchPrefsGetDndEnabled() || !_host.localMinute)
    return false;
  return notification::quietWindow(true, _host.localMinute(_host.context), touchPrefsGetDndStartSlot(),
                                   touchPrefsGetDndEndSlot());
}
SoundSettings::Preview SoundSettings::preview(unsigned slot) {
  if (!_host.capabilities.sound || slot >= SlotCount || !_host.play)
    return Preview::Silent;
  if (!_host.quiet || _host.quiet(_host.context))
    return Preview::Quiet;
  if (dndActive())
    return Preview::DoNotDisturb;
  _host.play(_host.context, slot);
  return Preview::Played;
}
SoundSettings::Preview SoundSettings::notifyMessage(bool direct, bool mention, bool messageMuted,
                                                    bool mentionMuted) {
  const int slot =
      notification::soundSlot(direct, mention, touchPrefsGetSoundMessages(), touchPrefsGetSoundDirect(),
                              touchPrefsGetSoundMentions(), messageMuted, mentionMuted);
  return slot < 0 ? Preview::Silent : preview(slot);
}
void SoundSettings::fileName(unsigned slot, char *out, size_t capacity) const {
  if (!out || !capacity)
    return;
  out[0] = 0;
  if (slot >= SlotCount || !_host.capabilities.files)
    return;
  char path[TOUCH_SOUND_PATH_MAXLEN]{};
  touchPrefsGetSoundFile(slot, path, sizeof path);
  if (!path[0])
    return; // The screen supplies the translated built-in label.
  const bool sd = !strncmp(path, "sd:", 3);
  const char *name = sd ? path + 3 : path;
  const char *base = strrchr(name, '/');
  snprintf(out, capacity, sd ? "SD: %s" : "%s", base ? base + 1 : name);
}
bool SoundSettings::useBuiltin(unsigned slot) {
  return _host.capabilities.files && slot < SlotCount && touchPrefsSetSoundFile(slot, "");
}
bool SoundSettings::selectFileSlot(unsigned slot) {
  if (!_host.capabilities.files || slot >= SlotCount)
    return false;
  _fileSlot = slot;
  return true;
}
void SoundSettings::stopBlink(uint32_t now) {
  ++_blinkGeneration;
  _blinkMask = 0;
  // Queue the off write; a failed bus write is retried without blocking UI.
  _transitions = 1;
  _lit = true;
  _blinkAt = now;
  tick(now);
}
void SoundSettings::blink(bool direct, uint32_t now) {
  if (!_host.capabilities.indicator || !touchPrefsGetAttakyNotifyEnabled())
    return;
  ++_blinkGeneration;
  _blinkMask = notification::colorMask(direct ? touchPrefsGetAttakyNotifyDmColor()
                                              : touchPrefsGetAttakyNotifyRoomColor());
  _transitions = 6;
  _lit = false;
  _blinkAt = now;
}
void SoundSettings::tick(uint32_t now) {
  if (_writing || !_transitions || int32_t(now - _blinkAt) < 0)
    return;
  const auto generation = _blinkGeneration;
  const bool lit = !_lit;
  _writing = true;
  const bool applied = _host.indicator && _host.indicator(_host.context, lit ? _blinkMask : 0);
  _writing = false;
  if (_blinkGeneration != generation)
    return;
  if (!applied) {
    _blinkAt = now + 50;
    return;
  }
  _lit = lit;
  --_transitions;
  _blinkAt = now + 220;
}
} // namespace ui
