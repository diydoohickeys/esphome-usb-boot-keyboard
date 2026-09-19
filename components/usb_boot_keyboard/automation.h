#pragma once
#if defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || defined(USE_ESP32_VARIANT_ESP32P4)

#include <string>

#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"

#include "usb_boot_keyboard.h"

namespace esphome::usb_boot_keyboard {

// Shared by the two actions that have per-call timing overrides. Anything not
// given falls back to the component-level hold_time/gap_time.
template<typename... Ts> class TimedKeyAction : public Action<Ts...>, public Parented<UsbBootKeyboard> {
 public:
  TEMPLATABLE_VALUE(uint32_t, hold_time)
  TEMPLATABLE_VALUE(uint32_t, gap_time)

 protected:
  uint16_t hold_ms_(const Ts &...x) {
    auto value = this->hold_time_.optional_value(x...);
    return value.has_value() ? static_cast<uint16_t>(*value) : this->parent_->default_hold_time();
  }
  uint16_t gap_ms_(const Ts &...x) {
    auto value = this->gap_time_.optional_value(x...);
    return value.has_value() ? static_cast<uint16_t>(*value) : this->parent_->default_gap_time();
  }
};

template<typename... Ts> class SendAction : public TimedKeyAction<Ts...> {
 public:
  TEMPLATABLE_VALUE(std::string, sequence)

  void play(const Ts &...x) override {
    this->parent_->send(this->sequence_.value(x...), this->hold_ms_(x...), this->gap_ms_(x...));
  }
};

template<typename... Ts> class TypeAction : public TimedKeyAction<Ts...> {
 public:
  TEMPLATABLE_VALUE(std::string, text)

  void play(const Ts &...x) override {
    this->parent_->type(this->text_.value(x...), this->hold_ms_(x...), this->gap_ms_(x...));
  }
};

template<typename... Ts> class PressAction : public Action<Ts...>, public Parented<UsbBootKeyboard> {
 public:
  TEMPLATABLE_VALUE(std::string, sequence)

  void play(const Ts &...x) override { this->parent_->press(this->sequence_.value(x...)); }
};

template<typename... Ts> class ReleaseAction : public Action<Ts...>, public Parented<UsbBootKeyboard> {
 public:
  TEMPLATABLE_VALUE(std::string, sequence)

  void play(const Ts &...x) override { this->parent_->release(this->sequence_.value(x...)); }
};

template<typename... Ts> class ReleaseAllAction : public Action<Ts...>, public Parented<UsbBootKeyboard> {
 public:
  void play(const Ts &... /*x*/) override { this->parent_->release_all(); }
};

template<typename... Ts> class IsMountedCondition : public Condition<Ts...>, public Parented<UsbBootKeyboard> {
 public:
  bool check(const Ts &... /*x*/) override { return this->parent_->is_mounted(); }
};

class MountTrigger : public Trigger<> {
 public:
  explicit MountTrigger(UsbBootKeyboard *parent) {
    parent->add_on_mount_callback([this]() { this->trigger(); });
  }
};

class UnmountTrigger : public Trigger<> {
 public:
  explicit UnmountTrigger(UsbBootKeyboard *parent) {
    parent->add_on_unmount_callback([this]() { this->trigger(); });
  }
};

}  // namespace esphome::usb_boot_keyboard

#endif  // USE_ESP32_VARIANT_ESP32S2 || USE_ESP32_VARIANT_ESP32S3 || USE_ESP32_VARIANT_ESP32P4
