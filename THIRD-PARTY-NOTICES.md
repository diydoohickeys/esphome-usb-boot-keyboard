# Third-party notices

The source in this repository is MIT licensed (see `LICENSE`). A **compiled
firmware image** built from it is not: it statically links the components below,
and ESPHome's runtime is GPL-3.0.

## Compiled images are GPL-3.0

ESPHome's C++ runtime (`esphome/core`, and every component linked into a build)
is licensed **GPL-3.0**. Any firmware binary produced from this component —
including the prebuilt image published on the releases page and the web
flasher — is therefore a GPL-3.0 work, and is distributed under those terms.

The corresponding source is this repository together with the ESPHome release
pinned in `.github/workflows/build.yml`.

## Components linked into a build

| Component | Licence | Source |
|---|---|---|
| ESPHome | GPL-3.0 (runtime); the CLI/codegen is MIT | https://github.com/esphome/esphome |
| ESP-IDF | Apache-2.0 | https://github.com/espressif/esp-idf |
| `espressif/esp_tinyusb` | Apache-2.0 | https://github.com/espressif/esp-usb |
| TinyUSB | MIT | https://github.com/hathach/tinyusb |
| FreeRTOS | MIT | https://github.com/FreeRTOS/FreeRTOS-Kernel |

## Web flasher

The install page loads [ESP Web Tools](https://github.com/esphome/esp-web-tools)
(Apache-2.0) from unpkg at runtime. It is not bundled or redistributed here.
