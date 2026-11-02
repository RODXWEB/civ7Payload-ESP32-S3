#include <string.h>

#include "tusb.h"
#include "switch_consts.h"

uint8_t const *tud_descriptor_device_cb(void) {
  return switch_usb_device_descriptor;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return switch_usb_configuration_descriptor;
}

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  uint8_t chr_count;

  if (index == 0) {
    memcpy(&_desc_str[1], switch_usb_string_language, 2);
    chr_count = 1;
  } else {
    if (index >= sizeof(switch_usb_string_descriptors) /
                     sizeof(switch_usb_string_descriptors[0])) {
      return NULL;
    }
    const char *str = (const char *)switch_usb_string_descriptors[index];
    chr_count = (uint8_t)strlen(str);
    if (chr_count > 31) chr_count = 31;
    for (uint8_t i = 0; i < chr_count; i++) {
      _desc_str[1 + i] = (uint8_t)str[i];
    }
  }

  _desc_str[0] = (uint16_t)((0x03 << 8) | (2 * chr_count + 2));
  return _desc_str;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf) {
  if (itf == ITF_KEYBOARD) return keyboard_usb_report_descriptor;
  return switch_usb_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  (void)report_id;
  (void)report_type;
  (void)reqlen;

  if (itf == ITF_KEYBOARD) {
    KeyboardReport report = {0, 0, {0, 0, 0, 0, 0, 0}};
    memcpy(buffer, &report, sizeof(report));
    return sizeof(report);
  }

  SwitchUsbReport report = {0};
  memcpy(buffer, &report, sizeof(report));
  return sizeof(report);
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
  if (itf == ITF_KEYBOARD) {
    (void)report_type;
    return;
  }
  (void)report_type;
  tud_hid_n_report(ITF_GAMEPAD, report_id, buffer, bufsize);
}
