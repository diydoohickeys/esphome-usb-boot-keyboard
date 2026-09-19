#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace esphome::usb_boot_keyboard {

// One keyboard edge: a modifier mask plus at most one key.
//
// At most ONE key is deliberate. This component is a BOOT keyboard: the host
// reads a fixed 8-byte report and a KVM hotkey sniffer only ever looks at the
// modifier byte and the first keycode slot. Chords of several simultaneous
// non-modifier keys are not expressible and are not wanted.
struct Chord {
  uint8_t modifiers{0};
  uint8_t keycode{0};

  bool empty() const { return this->modifiers == 0 && this->keycode == 0; }
};

// Parse a keystroke spec into the chords to tap in order.
//
//   chord      "ctrl+shift+4"   modifiers + one key, pressed together, tapped once
//   sequence   "ctrl;ctrl;1"    ';'-separated chords, each tapped in turn
//
// Case-insensitive. Modifiers: ctrl/control, shift, alt/opt/option,
// gui/win/cmd/meta/super. Keys: a-z, 0-9, f1-f12, named keys (enter, esc, tab,
// space, up, home, pageup, ...), single punctuation characters, the punctuation
// names (semicolon, comma, ...) and raw usages as "0x3A" or "key(58)".
//
// Returns false and leaves `error` set if any chord is unparsable; `out` then
// holds whatever parsed before the failure and must not be used.
bool parse_spec(const std::string &spec, std::vector<Chord> &out, std::string &error);

// Parse a single chord (no ';' handling). Used by the press/release actions.
bool parse_chord(const std::string &chord, Chord &out);

// Map one literal character to a chord, adding shift where the glyph needs it.
// Used by the `type` action; returns false for characters with no US-layout key.
bool chord_for_char(char c, Chord &out);

}  // namespace esphome::usb_boot_keyboard
