#include "display.h"

#include <string.h>

#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#define LCD_HOST SPI2_HOST
#define LCD_H_RES 410
#define LCD_V_RES 502

#define PIN_SCLK 11
#define PIN_D0 4
#define PIN_D1 5
#define PIN_D2 6
#define PIN_D3 7
#define PIN_CS 12
#define PIN_RST 8
#define COL_OFFSET 22

#define BAND_H 56
#define SCALE 4
#define ADVANCE (6 * SCALE)

static const char *TAG = "display";
static esp_lcd_panel_handle_t s_panel;
static display_state_t s_state;
static bool s_ready;

/* 5x7, colunas (bit 0 = linha do topo). A-Z e espaco. */
static const uint8_t FONT[27][5] = {
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* F */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* espaco */
};

static uint16_t swap16(uint16_t c) {
  return (uint16_t)((c >> 8) | (c << 8));
}

static void memset_band(uint16_t *buf, int w, int h, uint16_t color) {
  uint16_t v = swap16(color);
  for (int i = 0; i < w * h; i++) buf[i] = v;
}

static void draw_text(uint16_t *buf, int x, int y, const char *s, uint16_t color) {
  uint16_t v = swap16(color);
  for (; *s; s++) {
    char c = *s;
    const uint8_t *g = (c >= 'A' && c <= 'Z') ? FONT[c - 'A'] : FONT[26];
    for (int col = 0; col < 5; col++) {
      uint8_t bits = g[col];
      for (int row = 0; row < 7; row++) {
        if (!(bits & (1 << row))) continue;
        for (int sy = 0; sy < SCALE; sy++) {
          for (int sx = 0; sx < SCALE; sx++) {
            int px = x + col * SCALE + sx;
            int py = y + row * SCALE + sy;
            if (px >= 0 && px < LCD_H_RES) buf[py * LCD_H_RES + px] = v;
          }
        }
      }
    }
    x += ADVANCE;
  }
}

static void paint(display_state_t state) {
  uint16_t bg, fg;
  const char *label;
  switch (state) {
    case DISPLAY_RUNNING:
      bg = 0xFD20; /* laranja */
      fg = 0x0000;
      label = "RODANDO";
      break;
    case DISPLAY_DONE:
      bg = 0x07E0; /* verde */
      fg = 0x0000;
      label = "TERMINADO";
      break;
    default:
      bg = 0xF800; /* vermelho */
      fg = 0xFFFF;
      label = "PARADO";
      break;
  }

  uint16_t *buf = heap_caps_malloc(LCD_H_RES * BAND_H * 2,
                                   MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!buf) {
    ESP_LOGW(TAG, "sem memoria para o status");
    return;
  }
  memset_band(buf, LCD_H_RES, BAND_H, bg);

  int len = (int)strlen(label);
  int text_w = len * ADVANCE - SCALE;
  int x = (LCD_H_RES - text_w) / 2;
  int y = (BAND_H - 7 * SCALE) / 2;
  draw_text(buf, x, y, label, fg);

  int y0 = (LCD_V_RES - BAND_H) / 2;
  esp_lcd_panel_draw_bitmap(s_panel, 0, y0, LCD_H_RES, y0 + BAND_H, buf);
  free(buf);
}

void display_init(void) {
  spi_bus_config_t buscfg = CO5300_PANEL_BUS_QSPI_CONFIG(
      PIN_SCLK, PIN_D0, PIN_D1, PIN_D2, PIN_D3,
      LCD_H_RES * BAND_H * 2);
  if (spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
    ESP_LOGW(TAG, "spi_bus_initialize falhou");
    return;
  }

  esp_lcd_panel_io_handle_t io = NULL;
  esp_lcd_panel_io_spi_config_t io_config =
      CO5300_PANEL_IO_QSPI_CONFIG(PIN_CS, NULL, NULL);
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config,
                               &io) != ESP_OK) {
    ESP_LOGW(TAG, "panel io falhou");
    return;
  }

  co5300_vendor_config_t vendor = {0};
  vendor.flags.use_qspi_interface = 1;
  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = PIN_RST,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16,
      .vendor_config = &vendor,
  };
  if (esp_lcd_new_panel_co5300(io, &panel_config, &s_panel) != ESP_OK) {
    ESP_LOGW(TAG, "painel falhou");
    return;
  }

  esp_lcd_panel_reset(s_panel);
  esp_lcd_panel_init(s_panel);
  esp_lcd_panel_set_gap(s_panel, COL_OFFSET, 0);
  esp_lcd_panel_disp_on_off(s_panel, true);
  esp_lcd_panel_co5300_set_brightness(s_panel, 100);

  s_ready = true;
  s_state = DISPLAY_STOPPED;
  paint(s_state);
}

void display_set_status(display_state_t state) {
  if (!s_ready || state == s_state) return;
  s_state = state;
  paint(state);
}
