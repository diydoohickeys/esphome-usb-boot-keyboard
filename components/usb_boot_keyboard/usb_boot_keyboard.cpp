#if defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || defined(USE_ESP32_VARIANT_ESP32P4)

#include "usb_boot_keyboard.h"

#include "esphome/core/log.h"

#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

namespace esphome::usb_boot_keyboard {

static const char *const TAG = "usb_boot_keyboard";

UsbBootKeyboard *global_usb_boot_keyboard =  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
    nullptr;

// ---------------------------------------------------------------------------
// Descriptors. TUD_HID_REPORT_DESC_KEYBOARD() with no argument emits no
// HID_REPORT_ID item, which is what keeps the reports a plain 8 bytes.
// ---------------------------------------------------------------------------
static const uint8_t HID_REPORT_DESCRIPTOR[] = {TUD_HID_REPORT_DESC_KEYBOARD()};

enum : uint8_t { ITF_NUM_HID = 0, ITF_NUM_TOTAL };
static constexpr uint8_t EPNUM_HID = 0x81;
static constexpr uint16_t CONFIG_TOTAL_LEN = TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN;
static constexpr uint8_t STR_IDX_INTERFACE = 4;

static const uint8_t CONFIG_DESCRIPTOR[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // HID_ITF_PROTOCOL_KEYBOARD stamps bInterfaceSubClass=1 AND bInterfaceProtocol=1
    // — the two bytes a KVM hotkey sniffer classifies on.
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, STR_IDX_INTERFACE, HID_ITF_PROTOCOL_KEYBOARD, sizeof(HID_REPORT_DESCRIPTOR),
                       EPNUM_HID, 8, 10),
};

static char s_lang_id[2] = {0x09, 0x04};  // English (US)
static const char *s_string_descriptor[5] = {s_lang_id, nullptr, nullptr, nullptr, nullptr};

static tusb_desc_device_t s_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,  // per-interface; NOT composite/IAD, which a sniffer skips
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4B4D,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};


// ---------------------------------------------------------------------------
// Worker task. Every timed edge happens here so that no action, API handler or
// automation ever blocks the ESPHome loop.
// ---------------------------------------------------------------------------
void kb_worker_task(void *arg) {
  auto *kb = static_cast<UsbBootKeyboard *>(arg);
  Job *job = nullptr;
  for (;;) {
    if (xQueueReceive(kb->queue_, &job, portMAX_DELAY) == pdTRUE && job != nullptr) {
      kb->run_job_(*job);
      delete job;
      job = nullptr;
    }
  }
}

// ---------------------------------------------------------------------------
void UsbBootKeyboard::setup() {
  global_usb_boot_keyboard = this;

  if (this->serial_ == nullptr) {
    static std::string mac = get_mac_address();
    this->serial_ = mac.c_str();
  }
  s_device_descriptor.idVendor = this->vendor_id_;
  s_device_descriptor.idProduct = this->product_id_;
  s_string_descriptor[1] = this->manufacturer_;
  s_string_descriptor[2] = this->product_;
  s_string_descriptor[3] = this->serial_;
  s_string_descriptor[STR_IDX_INTERFACE] = this->product_;

  this->queue_ = xQueueCreate(this->queue_size_, sizeof(Job *));
  if (this->queue_ == nullptr) {
    ESP_LOGE(TAG, "Queue allocation failed");
    this->mark_failed();
    return;
  }

  tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
  tusb_cfg.port = TINYUSB_PORT_FULL_SPEED_0;
  tusb_cfg.phy.skip_setup = false;
  tusb_cfg.descriptor.device = &s_device_descriptor;
  tusb_cfg.descriptor.string = s_string_descriptor;
  tusb_cfg.descriptor.string_count = sizeof(s_string_descriptor) / sizeof(s_string_descriptor[0]);
  tusb_cfg.descriptor.full_speed_config = CONFIG_DESCRIPTOR;

  esp_err_t err = tinyusb_driver_install(&tusb_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "tinyusb_driver_install failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  if (xTaskCreate(kb_worker_task, "usb_boot_kb", 4096, this, 5, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "Worker task creation failed");
    this->mark_failed();
  }
}

void UsbBootKeyboard::loop() {
  if (this->mounted_ == this->reported_mounted_) {
    // Nothing to dispatch. on_bus_state_change() wakes us again.
    this->disable_loop();
    return;
  }
  this->reported_mounted_ = this->mounted_;
  if (this->reported_mounted_) {
    ESP_LOGI(TAG, "USB host attached");
    this->mount_callbacks_.call();
  } else {
    ESP_LOGI(TAG, "USB host detached");
    this->release_all();
    this->unmount_callbacks_.call();
  }
}

void UsbBootKeyboard::dump_config() {
  ESP_LOGCONFIG(TAG,
                "USB Boot Keyboard:\n"
                "  Vendor ID: 0x%04X\n"
                "  Product ID: 0x%04X\n"
                "  Manufacturer: '%s'\n"
                "  Product: '%s'\n"
                "  Serial: '%s'\n"
                "  Hold time: %ums\n"
                "  Gap time: %ums\n"
                "  Queue size: %u\n"
                "  Host attached: %s",
                this->vendor_id_, this->product_id_, this->manufacturer_, this->product_, this->serial_,
                this->hold_ms_, this->gap_ms_, this->queue_size_, YESNO(this->mounted_));
  if (this->is_failed())
    ESP_LOGE(TAG, "Setup failed — no keyboard is presented to the host");
}

void UsbBootKeyboard::on_bus_state_change(bool mounted) {
  this->mounted_ = mounted;
  this->enable_loop_soon_any_context();
}

// ---------------------------------------------------------------------------
void UsbBootKeyboard::send(const std::string &spec, uint16_t hold_ms, uint16_t gap_ms) {
  std::vector<Chord> chords;
  std::string bad;
  if (!parse_spec(spec, chords, bad)) {
    ESP_LOGW(TAG, "Cannot parse '%s' in spec '%s' — nothing sent", bad.c_str(), spec.c_str());
    return;
  }
  ESP_LOGD(TAG, "send '%s' (%u chords)", spec.c_str(), static_cast<unsigned>(chords.size()));
  this->enqueue_(JobKind::TAP, std::move(chords), hold_ms, gap_ms);
}

void UsbBootKeyboard::type(const std::string &text, uint16_t hold_ms, uint16_t gap_ms) {
  std::vector<Chord> chords;
  chords.reserve(text.size());
  for (char c : text) {
    Chord chord;
    if (!chord_for_char(c, chord)) {
      ESP_LOGW(TAG, "No US-layout key for character 0x%02X — skipped", static_cast<uint8_t>(c));
      continue;
    }
    chords.push_back(chord);
  }
  if (chords.empty())
    return;
  // Length only: `type` is how people send passwords into a KVM console.
  ESP_LOGD(TAG, "type %u characters", static_cast<unsigned>(chords.size()));
  this->enqueue_(JobKind::TAP, std::move(chords), hold_ms, gap_ms);
}

void UsbBootKeyboard::press(const std::string &spec) {
  Chord chord;
  if (!parse_chord(spec, chord)) {
    ESP_LOGW(TAG, "Cannot parse '%s' — nothing pressed", spec.c_str());
    return;
  }
  ESP_LOGD(TAG, "press '%s'", spec.c_str());
  this->enqueue_(JobKind::PRESS, {chord}, 0, 0);
}

void UsbBootKeyboard::release(const std::string &spec) {
  Chord chord;
  if (!parse_chord(spec, chord)) {
    ESP_LOGW(TAG, "Cannot parse '%s' — nothing released", spec.c_str());
    return;
  }
  ESP_LOGD(TAG, "release '%s'", spec.c_str());
  this->enqueue_(JobKind::RELEASE, {chord}, 0, 0);
}

void UsbBootKeyboard::release_all() {
  ESP_LOGD(TAG, "release all");
  this->enqueue_(JobKind::RELEASE_ALL, {}, 0, 0);
}

// ---------------------------------------------------------------------------
void UsbBootKeyboard::enqueue_(JobKind kind, std::vector<Chord> &&chords, uint16_t hold_ms, uint16_t gap_ms) {
  if (this->queue_ == nullptr)
    return;
  // A release must still be queued while detached so the held state is cleared
  // before the next host attaches; anything else would key into the void.
  if (!this->mounted_ && kind != JobKind::RELEASE_ALL) {
    ESP_LOGW(TAG, "No USB host attached — keystroke dropped");
    return;
  }
  auto *job = new Job{kind, hold_ms, gap_ms, std::move(chords)};  // NOLINT(cppcoreguidelines-owning-memory)
  if (xQueueSend(this->queue_, &job, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Queue full (size %u) — keystroke dropped", this->queue_size_);
    delete job;
  }
}

void UsbBootKeyboard::run_job_(const Job &job) {
  switch (job.kind) {
    case JobKind::TAP:
      for (const auto &chord : job.chords) {
        this->send_report_(this->held_modifiers_ | chord.modifiers,
                           chord.keycode != 0 ? chord.keycode : this->held_keycode_);
        vTaskDelay(pdMS_TO_TICKS(job.hold_ms));
        this->send_report_(this->held_modifiers_, this->held_keycode_);
        vTaskDelay(pdMS_TO_TICKS(job.gap_ms));
      }
      break;

    case JobKind::PRESS:
      for (const auto &chord : job.chords) {
        this->held_modifiers_ |= chord.modifiers;
        if (chord.keycode != 0)
          this->held_keycode_ = chord.keycode;
      }
      this->send_report_(this->held_modifiers_, this->held_keycode_);
      break;

    case JobKind::RELEASE:
      for (const auto &chord : job.chords) {
        this->held_modifiers_ &= static_cast<uint8_t>(~chord.modifiers);
        if (chord.keycode != 0 && chord.keycode == this->held_keycode_)
          this->held_keycode_ = 0;
      }
      this->send_report_(this->held_modifiers_, this->held_keycode_);
      break;

    case JobKind::RELEASE_ALL:
      this->held_modifiers_ = 0;
      this->held_keycode_ = 0;
      this->send_report_(0, 0);
      break;
  }
}

void UsbBootKeyboard::send_report_(uint8_t modifiers, uint8_t keycode) {
  if (!tud_mounted())
    return;
  // Bounded wait for the IN endpoint. Dropping one edge beats wedging the
  // worker on a host that has stopped polling.
  for (int i = 0; i < 300 && !tud_hid_ready(); i++)
    vTaskDelay(pdMS_TO_TICKS(1));
  if (!tud_hid_ready()) {
    ESP_LOGW(TAG, "HID endpoint never became ready — report dropped");
    return;
  }
  uint8_t keys[6] = {keycode, 0, 0, 0, 0, 0};
  tud_hid_keyboard_report(0, modifiers, keycode != 0 ? keys : nullptr);
}

}  // namespace esphome::usb_boot_keyboard

// ---------------------------------------------------------------------------
// TinyUSB callbacks. Defined at global scope, where TinyUSB declares the weak
// symbols they override.
// These run on the TinyUSB task, so they record state and
// return — user automations are dispatched from loop().
// ---------------------------------------------------------------------------
extern "C" {

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void) instance;
  return esphome::usb_boot_keyboard::HID_REPORT_DESCRIPTOR;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t *buffer,
                               uint16_t reqlen) {
  (void) instance;
  (void) report_id;
  (void) type;
  (void) buffer;
  (void) reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t const *buffer,
                           uint16_t bufsize) {
  // The host's LED report (caps/num/scroll lock). Nothing to drive here.
  (void) instance;
  (void) report_id;
  (void) type;
  (void) buffer;
  (void) bufsize;
}

void tud_mount_cb(void) {
  if (esphome::usb_boot_keyboard::global_usb_boot_keyboard != nullptr)
    esphome::usb_boot_keyboard::global_usb_boot_keyboard->on_bus_state_change(true);
}

void tud_umount_cb(void) {
  if (esphome::usb_boot_keyboard::global_usb_boot_keyboard != nullptr)
    esphome::usb_boot_keyboard::global_usb_boot_keyboard->on_bus_state_change(false);
}

}  // extern "C"

#endif  // USE_ESP32_VARIANT_ESP32S2 || USE_ESP32_VARIANT_ESP32S3 || USE_ESP32_VARIANT_ESP32P4
