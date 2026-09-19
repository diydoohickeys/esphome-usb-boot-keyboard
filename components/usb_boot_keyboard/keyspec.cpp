#if defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || defined(USE_ESP32_VARIANT_ESP32P4)

#include "keyspec.h"

#include <cctype>
#include <cstdlib>

#include "class/hid/hid.h"

namespace esphome::usb_boot_keyboard {

namespace {

std::string to_lower(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s)
    out.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
  return out;
}

uint8_t modifier_bit(const std::string &t) {
  if (t == "ctrl" || t == "control")
    return KEYBOARD_MODIFIER_LEFTCTRL;
  if (t == "shift")
    return KEYBOARD_MODIFIER_LEFTSHIFT;
  if (t == "alt" || t == "opt" || t == "option")
    return KEYBOARD_MODIFIER_LEFTALT;
  if (t == "gui" || t == "win" || t == "cmd" || t == "meta" || t == "super")
    return KEYBOARD_MODIFIER_LEFTGUI;
  // Right-hand variants, for the rare host that distinguishes them.
  if (t == "rctrl")
    return KEYBOARD_MODIFIER_RIGHTCTRL;
  if (t == "rshift")
    return KEYBOARD_MODIFIER_RIGHTSHIFT;
  if (t == "ralt" || t == "altgr")
    return KEYBOARD_MODIFIER_RIGHTALT;
  if (t == "rgui" || t == "rwin")
    return KEYBOARD_MODIFIER_RIGHTGUI;
  return 0;
}

// A named key token, already lowercased. 0 = not a named key.
uint8_t named_key(const std::string &t) {
  if (t.size() >= 2 && t[0] == 'f' && ::isdigit(static_cast<unsigned char>(t[1]))) {
    int n = ::atoi(t.c_str() + 1);
    if (n >= 1 && n <= 12)
      return HID_KEY_F1 + (n - 1);
  }
  if (t == "enter" || t == "return")
    return HID_KEY_ENTER;
  if (t == "esc" || t == "escape")
    return HID_KEY_ESCAPE;
  if (t == "tab")
    return HID_KEY_TAB;
  if (t == "space")
    return HID_KEY_SPACE;
  if (t == "backspace" || t == "bksp")
    return HID_KEY_BACKSPACE;
  if (t == "delete" || t == "del")
    return HID_KEY_DELETE;
  if (t == "insert" || t == "ins")
    return HID_KEY_INSERT;
  if (t == "home")
    return HID_KEY_HOME;
  if (t == "end")
    return HID_KEY_END;
  if (t == "pageup" || t == "pgup")
    return HID_KEY_PAGE_UP;
  if (t == "pagedown" || t == "pgdn")
    return HID_KEY_PAGE_DOWN;
  if (t == "up")
    return HID_KEY_ARROW_UP;
  if (t == "down")
    return HID_KEY_ARROW_DOWN;
  if (t == "left")
    return HID_KEY_ARROW_LEFT;
  if (t == "right")
    return HID_KEY_ARROW_RIGHT;
  if (t == "printscreen" || t == "prtsc")
    return HID_KEY_PRINT_SCREEN;
  if (t == "scrolllock")
    return HID_KEY_SCROLL_LOCK;
  if (t == "pause" || t == "break")
    return HID_KEY_PAUSE;
  if (t == "capslock")
    return HID_KEY_CAPS_LOCK;
  if (t == "numlock")
    return HID_KEY_NUM_LOCK;
  if (t == "menu" || t == "app")
    return HID_KEY_APPLICATION;
  // Names for the punctuation that cannot be written literally: '+' is the
  // chord separator, ';' the sequence separator.
  if (t == "plus")
    return HID_KEY_EQUAL;  // caller adds shift below
  if (t == "semicolon")
    return HID_KEY_SEMICOLON;
  if (t == "comma")
    return HID_KEY_COMMA;
  if (t == "period" || t == "dot")
    return HID_KEY_PERIOD;
  if (t == "slash")
    return HID_KEY_SLASH;
  if (t == "backslash")
    return HID_KEY_BACKSLASH;
  if (t == "minus" || t == "dash")
    return HID_KEY_MINUS;
  if (t == "equal" || t == "equals")
    return HID_KEY_EQUAL;
  if (t == "grave" || t == "backtick")
    return HID_KEY_GRAVE;
  if (t == "apostrophe" || t == "quote")
    return HID_KEY_APOSTROPHE;
  if (t == "lbracket")
    return HID_KEY_BRACKET_LEFT;
  if (t == "rbracket")
    return HID_KEY_BRACKET_RIGHT;
  return 0;
}

// "0x3a" or "key(58)" -> a raw HID usage on the keyboard page, so any key is
// reachable from YAML without a change here. 0 = not a raw token.
uint8_t raw_key(const std::string &t) {
  long v = -1;
  if (t.size() > 2 && t[0] == '0' && t[1] == 'x') {
    v = ::strtol(t.c_str() + 2, nullptr, 16);
  } else if (t.size() > 5 && t.compare(0, 4, "key(") == 0 && t.back() == ')') {
    v = ::strtol(t.c_str() + 4, nullptr, 10);
  }
  return (v > 0 && v <= 0xFF) ? static_cast<uint8_t>(v) : 0;
}

}  // namespace

bool chord_for_char(char c, Chord &out) {
  out = Chord{};
  if (c >= 'a' && c <= 'z') {
    out.keycode = HID_KEY_A + (c - 'a');
    return true;
  }
  if (c >= 'A' && c <= 'Z') {
    out.keycode = HID_KEY_A + (c - 'A');
    out.modifiers = KEYBOARD_MODIFIER_LEFTSHIFT;
    return true;
  }
  if (c >= '1' && c <= '9') {
    out.keycode = HID_KEY_1 + (c - '1');
    return true;
  }
  if (c == '0') {
    out.keycode = HID_KEY_0;
    return true;
  }

  // Unshifted punctuation.
  switch (c) {
    case ' ':
      out.keycode = HID_KEY_SPACE;
      return true;
    case '\n':
      out.keycode = HID_KEY_ENTER;
      return true;
    case '\t':
      out.keycode = HID_KEY_TAB;
      return true;
    case '-':
      out.keycode = HID_KEY_MINUS;
      return true;
    case '=':
      out.keycode = HID_KEY_EQUAL;
      return true;
    case '[':
      out.keycode = HID_KEY_BRACKET_LEFT;
      return true;
    case ']':
      out.keycode = HID_KEY_BRACKET_RIGHT;
      return true;
    case '\\':
      out.keycode = HID_KEY_BACKSLASH;
      return true;
    case ';':
      out.keycode = HID_KEY_SEMICOLON;
      return true;
    case '\'':
      out.keycode = HID_KEY_APOSTROPHE;
      return true;
    case '`':
      out.keycode = HID_KEY_GRAVE;
      return true;
    case ',':
      out.keycode = HID_KEY_COMMA;
      return true;
    case '.':
      out.keycode = HID_KEY_PERIOD;
      return true;
    case '/':
      out.keycode = HID_KEY_SLASH;
      return true;
    default:
      break;
  }

  // Shifted punctuation (US layout — the only layout a raw HID usage can assume).
  out.modifiers = KEYBOARD_MODIFIER_LEFTSHIFT;
  switch (c) {
    case '!':
      out.keycode = HID_KEY_1;
      return true;
    case '@':
      out.keycode = HID_KEY_2;
      return true;
    case '#':
      out.keycode = HID_KEY_3;
      return true;
    case '$':
      out.keycode = HID_KEY_4;
      return true;
    case '%':
      out.keycode = HID_KEY_5;
      return true;
    case '^':
      out.keycode = HID_KEY_6;
      return true;
    case '&':
      out.keycode = HID_KEY_7;
      return true;
    case '*':
      out.keycode = HID_KEY_8;
      return true;
    case '(':
      out.keycode = HID_KEY_9;
      return true;
    case ')':
      out.keycode = HID_KEY_0;
      return true;
    case '_':
      out.keycode = HID_KEY_MINUS;
      return true;
    case '+':
      out.keycode = HID_KEY_EQUAL;
      return true;
    case '{':
      out.keycode = HID_KEY_BRACKET_LEFT;
      return true;
    case '}':
      out.keycode = HID_KEY_BRACKET_RIGHT;
      return true;
    case '|':
      out.keycode = HID_KEY_BACKSLASH;
      return true;
    case ':':
      out.keycode = HID_KEY_SEMICOLON;
      return true;
    case '"':
      out.keycode = HID_KEY_APOSTROPHE;
      return true;
    case '~':
      out.keycode = HID_KEY_GRAVE;
      return true;
    case '<':
      out.keycode = HID_KEY_COMMA;
      return true;
    case '>':
      out.keycode = HID_KEY_PERIOD;
      return true;
    case '?':
      out.keycode = HID_KEY_SLASH;
      return true;
    default:
      out = Chord{};
      return false;
  }
}

bool parse_chord(const std::string &chord, Chord &out) {
  out = Chord{};
  size_t pos = 0;
  while (pos <= chord.size()) {
    size_t sep = chord.find('+', pos);
    std::string token = to_lower(chord.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos));
    if (!token.empty()) {
      if (uint8_t m = modifier_bit(token)) {
        out.modifiers |= m;
      } else if (uint8_t raw = raw_key(token)) {
        out.keycode = raw;
      } else if (uint8_t named = named_key(token)) {
        out.keycode = named;
        if (token == "plus")
          out.modifiers |= KEYBOARD_MODIFIER_LEFTSHIFT;
      } else if (token.size() == 1) {
        Chord single;
        if (!chord_for_char(token[0], single))
          return false;
        out.keycode = single.keycode;
        out.modifiers |= single.modifiers;
      } else {
        return false;  // an unknown multi-character token is a typo, not a key
      }
    }
    if (sep == std::string::npos)
      break;
    pos = sep + 1;
  }
  return !out.empty();
}

bool parse_spec(const std::string &spec, std::vector<Chord> &out, std::string &error) {
  out.clear();
  error.clear();
  size_t pos = 0;
  while (pos <= spec.size()) {
    size_t sep = spec.find(';', pos);
    std::string part = spec.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);
    if (!part.empty()) {
      Chord c;
      if (!parse_chord(part, c)) {
        error = part;
        return false;
      }
      out.push_back(c);
    }
    if (sep == std::string::npos)
      break;
    pos = sep + 1;
  }
  if (out.empty()) {
    error = spec;
    return false;
  }
  return true;
}

}  // namespace esphome::usb_boot_keyboard

#endif  // USE_ESP32_VARIANT_ESP32S2 || USE_ESP32_VARIANT_ESP32S3 || USE_ESP32_VARIANT_ESP32P4
