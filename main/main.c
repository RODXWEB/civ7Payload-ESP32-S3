#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"
#include "esp_private/usb_phy.h"

#include "MacroEngine.h"
#include "display.h"
#include "switch_consts.h"

static const char *TAG = "civswitch";

// Forca o linker a incluir os callbacks do TinyUSB (usb_descriptors.c), que
// ficam em outro objeto e seriam descartados pela ordem dos arquivos .a.
__attribute__((used)) static const void *const _usb_callbacks[] = {
    (const void *)&tud_descriptor_device_cb,
    (const void *)&tud_descriptor_configuration_cb,
    (const void *)&tud_descriptor_string_cb,
    (const void *)&tud_hid_descriptor_report_cb,
    (const void *)&tud_hid_get_report_cb,
    (const void *)&tud_hid_set_report_cb,
};

void app_main(void) {
  macro_init();

  usb_phy_config_t phy_conf = {
      .controller = USB_PHY_CTRL_OTG,
      .target = USB_PHY_TARGET_INT,
      .otg_mode = USB_OTG_MODE_DEVICE,
      .otg_speed = USB_PHY_SPEED_FULL,
  };
  usb_phy_handle_t phy = NULL;
  esp_err_t err = usb_new_phy(&phy_conf, &phy);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "usb_new_phy falhou: %s", esp_err_to_name(err));
    return;
  }

  if (!tusb_init()) {
    ESP_LOGE(TAG, "tusb_init falhou");
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(100));
  display_init();
  ESP_LOGI(TAG, "aguardando USB (Pro Controller + teclado)");

  bool was_mounted = false;
  uint32_t last_ready = 0;
  SwitchUsbReport pad;
  KeyboardReport kbd;

  while (true) {
    tud_task();
    if (tud_suspended()) tud_remote_wakeup();

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (tud_hid_n_ready(ITF_GAMEPAD) || tud_hid_n_ready(ITF_KEYBOARD)) last_ready = now;
    /* sem host o endpoint HID fica ocupado: 500 ms sem ACK = cabo fora */
    bool mounted = tud_mounted() && (now - last_ready < 1000);

    if (mounted && !was_mounted) {
      /* reconectou: recomeca do zero */
      macro_reset();
      macro_start(now + MACRO_START_DELAY_MS);
      display_set_status(DISPLAY_RUNNING);
      ESP_LOGI(TAG, "reconectado: macro armado (%u ms)", MACRO_START_DELAY_MS);
    } else if (!mounted && was_mounted) {
      macro_reset();
      display_set_status(DISPLAY_STOPPED);
      ESP_LOGI(TAG, "desconectado: macro parado");
    }
    was_mounted = mounted;

    if (mounted) {
      macro_tick(now);
      if (macro_finished()) display_set_status(DISPLAY_DONE);
    }
    macro_get_reports(&pad, &kbd);

    if (tud_hid_n_ready(ITF_GAMEPAD)) {
      tud_hid_n_report(ITF_GAMEPAD, 0, &pad, sizeof(pad));
    }
    if (tud_hid_n_ready(ITF_KEYBOARD)) {
      tud_hid_n_report(ITF_KEYBOARD, 0, &kbd, sizeof(kbd));
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
