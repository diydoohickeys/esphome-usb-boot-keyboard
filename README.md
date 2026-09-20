# usb_boot_keyboard

An ESPHome external component that turns an ESP32 with native USB into a **USB
boot keyboard**, so Home Assistant can send keystrokes to whatever the board is
plugged into.

It exists for one reason: a cheap KVM switch's hotkey sniffer only recognises a
**single-interface boot keyboard**. Present anything else and the hotkey is
silently ignored — no error, no beep, nothing.

```yaml
external_components:
  - source: github://diydoohickeys/esphome-usb-boot-keyboard

usb_boot_keyboard:
  id: kbd

# Each entry under `button:` is one more button in Home Assistant. Add as many
# as you like — only the name and the keystroke change.
button:
  - platform: template
    name: "KVM Port 1"
    on_press:
      - usb_boot_keyboard.send: "ctrl;ctrl;1"

  - platform: template
    name: "KVM Port 2"
    on_press:
      - usb_boot_keyboard.send: "ctrl;ctrl;2"

  - platform: template
    name: "KVM Port 3"
    on_press:
      - usb_boot_keyboard.send: "ctrl;ctrl;3"

  # Nothing here is KVM-specific — any keystroke works.
  - platform: template
    name: "Lock the PC"
    on_press:
      - usb_boot_keyboard.send: "gui+l"
```

## The device shape, and why it is fixed

| | |
|---|---|
| `bDeviceClass` | `0x00` (per-interface) — **not** composite/IAD |
| Interfaces | exactly one: HID |
| HID subclass / protocol | `1` / `1` — boot keyboard |
| Report ID | none; plain 8-byte reports |

These are not tuning knobs. A KVM hotkey sniffer classifies on the interface
subclass and protocol bytes and then parses a fixed 8-byte report; a composite
device, a report-ID descriptor, or a non-boot keyboard makes it stop looking.

The practical consequences:

- **No consumer-control ("media") keys.** They require a second report, which
  forces report IDs onto the keyboard report too. This is deliberate, and is why
  this component exists rather than using an upstream keyboard component.
- **Nothing else on the USB device port.** The component conflicts with
  `tinyusb`, `usb_cdc_acm` and `usb_host`, and rejects `logger.hardware_uart:
  USB_CDC` / `USB_SERIAL_JTAG` at config time.

## Hardware

Any ESP32 variant with a native USB device controller: **ESP32-S2, ESP32-S3,
ESP32-P4**. Tested on an ESP32-S3 SuperMini. ESP-IDF framework only.

Because the USB port is the keyboard, there is **no USB serial console at
runtime**. Send logs to a hardware UART:

```yaml
logger:
  hardware_uart: UART0
```

To flash over USB, hold the **BOOT** button while plugging the board in. After
the first flash, updates go over the air.

## Configuration

```yaml
usb_boot_keyboard:
  id: kbd
  hold_time: 60ms          # how long each key edge is held
  gap_time: 80ms           # gap between chords in a sequence
  vendor_id: 0x303A
  product_id: 0x4B4D
  manufacturer: "ESPHome"
  product: "USB Boot Keyboard"
  serial: "0001"           # defaults to the device MAC
  queue_size: 8            # pending keystrokes before new ones are dropped
  on_mount:
    - logger.log: "host attached"
  on_unmount:
    - logger.log: "host went away"
```

> ⚠️ Lengthening `hold_time` / `gap_time` is safe. **Shortening them is how a KVM
> hotkey quietly stops registering** — the sniffer polls the bus, and an edge
> shorter than its poll interval is never seen. The defaults are roughly human.

## Keystroke syntax

Two forms, which compose:

| Form | Example | Meaning |
|---|---|---|
| chord | `ctrl+shift+4` | modifiers plus one key, tapped once |
| sequence | `ctrl;ctrl;1` | `;`-separated chords, each tapped in turn |

Everything is case-insensitive.

**Modifiers** — `ctrl`/`control`, `shift`, `alt`/`opt`/`option`,
`gui`/`win`/`cmd`/`meta`/`super`. Right-hand variants: `rctrl`, `rshift`,
`ralt`/`altgr`, `rgui`/`rwin`.

**Keys** — `a`–`z`, `0`–`9`, `f1`–`f12`, `enter`/`return`, `esc`/`escape`,
`tab`, `space`, `backspace`/`bksp`, `delete`/`del`, `insert`/`ins`, `home`,
`end`, `pageup`/`pgup`, `pagedown`/`pgdn`, `up`, `down`, `left`, `right`,
`printscreen`/`prtsc`, `scrolllock`, `pause`/`break`, `capslock`, `numlock`,
`menu`/`app`.

**Punctuation** — single characters work (`ctrl+/`), and every glyph has a name
for when it cannot be written literally, because `+` is the chord separator and
`;` the sequence separator: `plus`, `semicolon`, `comma`, `period`/`dot`,
`slash`, `backslash`, `minus`/`dash`, `equal`, `grave`/`backtick`,
`apostrophe`/`quote`, `lbracket`, `rbracket`.

**Raw usages** — `0x3A` or `key(58)` sends any usage on the HID keyboard page,
so nothing is out of reach if it is not in the table above.

## Actions

All of these are templatable — `!lambda` returning a string works anywhere a
literal does.

| Action | Description |
|---|---|
| `usb_boot_keyboard.send: "ctrl;ctrl;1"` | Tap a chord or sequence. |
| `usb_boot_keyboard.type: "hello"` | Type literal text, character by character. |
| `usb_boot_keyboard.press: "alt"` | Press and **keep held**. |
| `usb_boot_keyboard.release: "alt"` | Release what `press` is holding. |
| `usb_boot_keyboard.release_all:` | Release everything. |

`send` and `type` also take `hold_time` / `gap_time` to override the
component defaults for that one call:

```yaml
- usb_boot_keyboard.send:
    sequence: "ctrl;ctrl;1"
    hold_time: 90ms
```

`press` holds a modifier across several taps, which a sequence cannot express —
the window switcher only cycles while `alt` stays down:

```yaml
- usb_boot_keyboard.press: "alt"
- usb_boot_keyboard.send: "tab"
- usb_boot_keyboard.send: "tab"
- usb_boot_keyboard.release: "alt"
```

`type` logs only the number of characters, never the text, because it is how
people send passwords into a KVM console.

**Condition:** `usb_boot_keyboard.is_mounted:` — true while a USB host has the
keyboard enumerated. Also available as `id(kbd)->is_mounted()` for a template
binary sensor.

Every action is non-blocking. The spec is parsed by the caller and the timed key
edges are performed by a worker task, so an automation, API handler or
dashboard button never stalls the ESPHome loop.

## Changing keystrokes without reflashing

Nothing about a KVM, a port count or `ctrl;ctrl;N` is baked into the component —
those live in your YAML, and they do not have to be fixed at compile time:

```yaml
text:
  - platform: template
    id: custom_sequence
    name: "Custom keystroke"
    optimistic: true
    restore_value: true
    initial_value: "ctrl+alt+t"
    max_length: 64
    mode: text

button:
  - platform: template
    name: "Send custom keystroke"
    on_press:
      - usb_boot_keyboard.send: !lambda "return id(custom_sequence).state;"
```

Type a new keystroke into the text entity in Home Assistant and the next press
sends it. `example/usb-boot-keyboard.yaml` shows this alongside fixed buttons
and a templated `select`.

### From an automation

For automations and scripts, expose an API action rather than driving the text
entity — that would be two service calls and a race:

```yaml
api:
  actions:
    - action: send_keystroke
      variables:
        keys: string
      then:
        - usb_boot_keyboard.send: !lambda "return keys;"
```

```yaml
# ...then in any Home Assistant automation
actions:
  - action: esphome.my_device_send_keystroke
    data:
      keys: "ctrl;ctrl;3"
```

`text` is a reserved variable name in an API action, so a "type this string"
action needs to call its variable something else — the example uses `message`.

## Why not an upstream component?

ESPHome's `tinyusb` component is a foundation: it configures `esp_tinyusb`
through Kconfig and lets that build the configuration descriptor from the
enabled classes (CDC / MSC / NCM). There is no hook for contributing a HID
interface descriptor, and anything it did emit would be part of a composite
device.

The upstream `tinyusb_keyboard` PR ([esphome#16079]) puts keyboard and
consumer-control reports in one descriptor, which forces report IDs — a
perfectly good keyboard for a PC, and invisible to a KVM sniffer. If it lands
with a boot-only option, this component becomes unnecessary.

[esphome#16079]: https://github.com/esphome/esphome/pull/16079

## Licence

The component source is MIT — see [LICENSE](LICENSE).

A **compiled image** is another matter: ESPHome's C++ runtime is GPL-3.0, so any
firmware binary built from this is a GPL-3.0
work. This repository, plus the ESPHome sources it is built against, is the
corresponding source. See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
