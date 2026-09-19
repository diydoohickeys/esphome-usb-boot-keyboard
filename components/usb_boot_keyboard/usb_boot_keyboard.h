#pragma once
#if defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || defined(USE_ESP32_VARIANT_ESP32P4)

#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "keyspec.h"

namespace esphome::usb_boot_keyboard {

enum class JobKind : uint8_t {
  TAP,          // press and release each chord in turn
  PRESS,        // press and keep held
  RELEASE,      // release the named modifiers/key
  RELEASE_ALL,  // release everything
};

struct Job {
  JobKind kind;
  uint16_t hold_ms;
  uint16_t gap_ms;
  std::vector<Chord> chords;
};

class UsbBootKeyboard;

// Drains the job queue and performs the timed key edges. Declared here (not only
// as a friend) because a friend declaration alone is invisible to ordinary lookup.
void kb_worker_task(void *arg);

// A single-interface USB boot keyboard.
//
// The descriptor shape is the whole point of this component and is not a
// tuning knob: bDeviceClass 0 (per-interface), ONE HID interface at subclass 1 /
// protocol 1, no report ID, plain 8-byte reports. Cheap KVM hotkey sniffers only
// parse that shape — a composite device, a report-ID descriptor or a non-boot
// keyboard is silently ignored by them. Nothing else may share the bus, which is
// why the component conflicts with tinyusb/usb_cdc_acm/usb_host, and why there
// are no consumer-control ("media") keys: they need a second report, which forces
// report IDs, which breaks the sniffer.
class UsbBootKeyboard : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_vendor_id(uint16_t id) { this->vendor_id_ = id; }
  void set_product_id(uint16_t id) { this->product_id_ = id; }
  void set_manufacturer(const char *s) { this->manufacturer_ = s; }
  void set_product(const char *s) { this->product_ = s; }
  void set_serial(const char *s) { this->serial_ = s; }
  void set_hold_time(uint16_t ms) { this->hold_ms_ = ms; }
  void set_gap_time(uint16_t ms) { this->gap_ms_ = ms; }
  void set_queue_size(uint8_t n) { this->queue_size_ = n; }

  uint16_t default_hold_time() const { return this->hold_ms_; }
  uint16_t default_gap_time() const { return this->gap_ms_; }

  // Every one of these is non-blocking and safe to call from any context,
  // including an API handler or another component's callback: the spec is
  // parsed here and the timed edges are performed by the worker task.
  void send(const std::string &spec, uint16_t hold_ms, uint16_t gap_ms);
  void type(const std::string &text, uint16_t hold_ms, uint16_t gap_ms);
  void press(const std::string &spec);
  void release(const std::string &spec);
  void release_all();

  bool is_mounted() const { return this->mounted_; }

  void add_on_mount_callback(std::function<void()> &&cb) { this->mount_callbacks_.add(std::move(cb)); }
  void add_on_unmount_callback(std::function<void()> &&cb) { this->unmount_callbacks_.add(std::move(cb)); }

  // Called from the TinyUSB task — records the edge and wakes loop(). No user
  // code runs here.
  void on_bus_state_change(bool mounted);

 protected:
  friend void kb_worker_task(void *arg);

  void enqueue_(JobKind kind, std::vector<Chord> &&chords, uint16_t hold_ms, uint16_t gap_ms);
  void run_job_(const Job &job);
  void send_report_(uint8_t modifiers, uint8_t keycode);

  uint16_t vendor_id_{0x303A};
  uint16_t product_id_{0x4B4D};
  const char *manufacturer_{"ESPHome"};
  const char *product_{"USB Boot Keyboard"};
  const char *serial_{nullptr};
  uint16_t hold_ms_{60};
  uint16_t gap_ms_{80};
  uint8_t queue_size_{8};

  // Modifiers and key currently held by `press` and not yet released. Taps are
  // OR-ed on top of these, so `press: ctrl` + `send: a` produces ctrl+a.
  uint8_t held_modifiers_{0};
  uint8_t held_keycode_{0};

  QueueHandle_t queue_{nullptr};
  volatile bool mounted_{false};
  bool reported_mounted_{false};

  CallbackManager<void()> mount_callbacks_;
  CallbackManager<void()> unmount_callbacks_;
};

extern UsbBootKeyboard *global_usb_boot_keyboard;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace esphome::usb_boot_keyboard

#endif  // USE_ESP32_VARIANT_ESP32S2 || USE_ESP32_VARIANT_ESP32S3 || USE_ESP32_VARIANT_ESP32P4
