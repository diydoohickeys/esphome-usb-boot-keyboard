from esphome import automation
from esphome import final_validate as fv
import esphome.codegen as cg
from esphome.components import esp32
from esphome.components.esp32 import (
    VARIANT_ESP32P4,
    VARIANT_ESP32S2,
    VARIANT_ESP32S3,
    add_idf_component,
    add_idf_sdkconfig_option,
)
import esphome.config_validation as cv
from esphome.const import CONF_HARDWARE_UART, CONF_ID, CONF_TRIGGER_ID, Framework
from esphome.core import Lambda
from esphome.types import ConfigType

CODEOWNERS = ["@diydoohickeys"]
DEPENDENCIES = ["esp32"]

# This component installs the TinyUSB driver itself, with its own descriptors, and
# must be the only thing on the USB device port. Anything else claiming the OTG
# peripheral produces either a composite device or a second driver install.
CONFLICTS_WITH = ["tinyusb", "usb_cdc_acm", "usb_host"]

CONF_GAP_TIME = "gap_time"
CONF_HOLD_TIME = "hold_time"
CONF_MANUFACTURER = "manufacturer"
CONF_ON_MOUNT = "on_mount"
CONF_ON_UNMOUNT = "on_unmount"
CONF_PRODUCT = "product"
CONF_PRODUCT_ID = "product_id"
CONF_QUEUE_SIZE = "queue_size"
CONF_SEQUENCE = "sequence"
CONF_SERIAL = "serial"
CONF_TEXT = "text"
CONF_VENDOR_ID = "vendor_id"

usb_boot_keyboard_ns = cg.esphome_ns.namespace("usb_boot_keyboard")
UsbBootKeyboard = usb_boot_keyboard_ns.class_("UsbBootKeyboard", cg.Component)

SendAction = usb_boot_keyboard_ns.class_("SendAction", automation.Action)
TypeAction = usb_boot_keyboard_ns.class_("TypeAction", automation.Action)
PressAction = usb_boot_keyboard_ns.class_("PressAction", automation.Action)
ReleaseAction = usb_boot_keyboard_ns.class_("ReleaseAction", automation.Action)
ReleaseAllAction = usb_boot_keyboard_ns.class_("ReleaseAllAction", automation.Action)
IsMountedCondition = usb_boot_keyboard_ns.class_(
    "IsMountedCondition", automation.Condition
)
MountTrigger = usb_boot_keyboard_ns.class_("MountTrigger", automation.Trigger.template())
UnmountTrigger = usb_boot_keyboard_ns.class_(
    "UnmountTrigger", automation.Trigger.template()
)

_TIMING = cv.All(
    cv.positive_time_period_milliseconds,
    cv.Range(min=cv.TimePeriod(milliseconds=1), max=cv.TimePeriod(milliseconds=2000)),
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(UsbBootKeyboard),
            # ~60ms/80ms is roughly human. A KVM hotkey sniffer polls the bus and
            # will miss an edge shorter than its poll interval, so shortening these
            # to "speed things up" is how the hotkey silently stops registering.
            cv.Optional(CONF_HOLD_TIME, default="60ms"): _TIMING,
            cv.Optional(CONF_GAP_TIME, default="80ms"): _TIMING,
            cv.Optional(CONF_VENDOR_ID, default=0x303A): cv.hex_uint16_t,
            cv.Optional(CONF_PRODUCT_ID, default=0x4B4D): cv.hex_uint16_t,
            cv.Optional(CONF_MANUFACTURER, default="ESPHome"): cv.string_strict,
            cv.Optional(CONF_PRODUCT, default="USB Boot Keyboard"): cv.string_strict,
            # Defaults to the device MAC address.
            cv.Optional(CONF_SERIAL): cv.string_strict,
            cv.Optional(CONF_QUEUE_SIZE, default=8): cv.int_range(min=1, max=64),
            cv.Optional(CONF_ON_MOUNT): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(MountTrigger)}
            ),
            cv.Optional(CONF_ON_UNMOUNT): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(UnmountTrigger)}
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_framework(Framework.ESP_IDF),
    esp32.only_on_variant(
        supported=[VARIANT_ESP32S2, VARIANT_ESP32S3, VARIANT_ESP32P4],
    ),
)


def _final_validate(config: ConfigType) -> None:
    logger_config = fv.full_config.get().get("logger")
    if logger_config is None:
        return
    uart = logger_config.get(CONF_HARDWARE_UART)
    # Both of these route the console over the USB pins this component drives, so
    # the symptom is a keyboard that never enumerates and logs that never appear.
    if uart in ("USB_CDC", "USB_SERIAL_JTAG"):
        raise cv.Invalid(
            f"'usb_boot_keyboard' cannot be used with 'logger.hardware_uart: {uart}' "
            "because the USB port is the keyboard. Set 'logger.hardware_uart' to a "
            "hardware UART (e.g. UART0), or disable the logger's serial output with "
            "'logger: baud_rate: 0' and read logs over the API instead"
        )


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_hold_time(int(config[CONF_HOLD_TIME].total_milliseconds)))
    cg.add(var.set_gap_time(int(config[CONF_GAP_TIME].total_milliseconds)))
    cg.add(var.set_vendor_id(config[CONF_VENDOR_ID]))
    cg.add(var.set_product_id(config[CONF_PRODUCT_ID]))
    cg.add(var.set_manufacturer(config[CONF_MANUFACTURER]))
    cg.add(var.set_product(config[CONF_PRODUCT]))
    cg.add(var.set_queue_size(config[CONF_QUEUE_SIZE]))
    if (serial := config.get(CONF_SERIAL)) is not None:
        cg.add(var.set_serial(serial))

    for conf in config.get(CONF_ON_MOUNT, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
    for conf in config.get(CONF_ON_UNMOUNT, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)

    add_idf_component(name="espressif/esp_tinyusb", ref="2.2.1")

    # One HID interface and nothing else. A second class would make the device
    # composite, which is exactly what this component exists to avoid.
    add_idf_sdkconfig_option("CONFIG_TINYUSB_HID_COUNT", 1)
    add_idf_sdkconfig_option("CONFIG_TINYUSB_CDC_ENABLED", False)
    add_idf_sdkconfig_option("CONFIG_TINYUSB_MSC_ENABLED", False)
    add_idf_sdkconfig_option("CONFIG_TINYUSB_MIDI_COUNT", 0)


# ---------------------------------------------------------------------------
# Actions
# ---------------------------------------------------------------------------
_TIMING_OVERRIDES = {
    cv.Optional(CONF_HOLD_TIME): cv.templatable(cv.positive_time_period_milliseconds),
    cv.Optional(CONF_GAP_TIME): cv.templatable(cv.positive_time_period_milliseconds),
}


def _spec_schema(key: str, extra: dict | None = None):
    schema = {
        cv.GenerateID(): cv.use_id(UsbBootKeyboard),
        cv.Required(key): cv.templatable(cv.string),
    }
    if extra:
        schema.update(extra)
    return cv.maybe_simple_value(schema, key=key)


async def _templatable_ms(value, args):
    if isinstance(value, Lambda):
        return await cg.templatable(value, args, cg.uint32)
    return await cg.templatable(int(value.total_milliseconds), args, cg.uint32)


async def _timed_action_to_code(config, action_id, template_arg, args, key, setter):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(setter(var)(await cg.templatable(config[key], args, cg.std_string)))
    if (hold := config.get(CONF_HOLD_TIME)) is not None:
        cg.add(var.set_hold_time(await _templatable_ms(hold, args)))
    if (gap := config.get(CONF_GAP_TIME)) is not None:
        cg.add(var.set_gap_time(await _templatable_ms(gap, args)))
    return var


@automation.register_action(
    "usb_boot_keyboard.send",
    SendAction,
    _spec_schema(CONF_SEQUENCE, _TIMING_OVERRIDES),
    synchronous=True,
)
async def send_action_to_code(config, action_id, template_arg, args):
    return await _timed_action_to_code(
        config, action_id, template_arg, args, CONF_SEQUENCE, lambda v: v.set_sequence
    )


@automation.register_action(
    "usb_boot_keyboard.type",
    TypeAction,
    _spec_schema(CONF_TEXT, _TIMING_OVERRIDES),
    synchronous=True,
)
async def type_action_to_code(config, action_id, template_arg, args):
    return await _timed_action_to_code(
        config, action_id, template_arg, args, CONF_TEXT, lambda v: v.set_text
    )


@automation.register_action(
    "usb_boot_keyboard.press",
    PressAction,
    _spec_schema(CONF_SEQUENCE),
    synchronous=True,
)
async def press_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_sequence(await cg.templatable(config[CONF_SEQUENCE], args, cg.std_string)))
    return var


@automation.register_action(
    "usb_boot_keyboard.release",
    ReleaseAction,
    _spec_schema(CONF_SEQUENCE),
    synchronous=True,
)
async def release_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_sequence(await cg.templatable(config[CONF_SEQUENCE], args, cg.std_string)))
    return var


@automation.register_action(
    "usb_boot_keyboard.release_all",
    ReleaseAllAction,
    automation.maybe_simple_id({cv.GenerateID(): cv.use_id(UsbBootKeyboard)}),
    synchronous=True,
)
async def release_all_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_condition(
    "usb_boot_keyboard.is_mounted",
    IsMountedCondition,
    automation.maybe_simple_id({cv.GenerateID(): cv.use_id(UsbBootKeyboard)}),
)
async def is_mounted_condition_to_code(config, condition_id, template_arg, args):
    var = cg.new_Pvariable(condition_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
