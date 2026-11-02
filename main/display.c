#include "display.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ========================================================================
 * Pocket Dongle S3 / LilyGO T-Dongle-S3 — 0,96" ST7735 (80x160), SPI.
 *
 * Pinout (Pocket-Dongle-S3, aus der funktionierenden PS5-DISPLAY-Firmware):
 *   SCLK = GPIO10, MOSI = GPIO11, CS = GPIO12,
 *   DC = GPIO13, RST = GPIO14.
 *   Backlight ist fest verdrahtet (kein Steuer-Pin).
 * ====================================================================== */

#define PIN_SCLK 10
#define PIN_MOSI 11
#define PIN_CS 12
#define PIN_DC 13
#define PIN_RST 14

#define LCD_HOST SPI2_HOST
#define PIXEL_CLK_HZ (20 * 1000 * 1000) /* 20 MHz, sicher fuer ST7735 */

/* Querformat: 160 breit x 80 hoch. */
#define LCD_W 160
#define LCD_H 80

/* ------------------------------------------------------------------------
 * TUNING — diese 0,96"-Panels unterscheiden sich je nach Charge. Falls das
 * Bild nicht stimmt, hier anpassen (jeweils neu bauen + flashen):
 *
 *  - Bild horizontal/vertikal verschoben  -> X_OFFSET / Y_OFFSET aendern
 *                                             (typisch 1/26 ODER 0/24).
 *  - Farben wirken invertiert (Negativ)    -> INVERT_COLORS auf 0 setzen.
 *  - Rot und Blau vertauscht               -> COLOR_BGR umschalten (0<->1).
 *  - Bild steht auf dem Kopf / gespiegelt  -> MADCTL_BASE anderer Wert
 *                                             (0x60, 0xA0, 0xC0 oder 0x00).
 * ---------------------------------------------------------------------- */
#define X_OFFSET 1
#define Y_OFFSET 26
#define INVERT_COLORS 1
#define COLOR_BGR 0        /* Pocket-Dongle-Panel ist RGB (PLUGIN-Variante) */
#define MADCTL_BASE 0xA0   /* MY|MV = Rotation 1, 160x80 Querformat */

#define MADCTL_VAL (MADCTL_BASE | (COLOR_BGR ? 0x08 : 0x00))

/* Font-Skalierung: 160px breit, 5x7-Font. SCALE 3 -> ~18px pro Zeichen.
 * Lange Woerter (>8 Zeichen) ggf. auf 2 reduzieren. */
#define SCALE 3
#define ADVANCE (6 * SCALE)

static const char *TAG = "display";
static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_fb; /* Framebuffer, Bytes bereits getauscht (big-endian) */
static display_state_t s_state;
static bool s_ready;

/* 5x7, Spalten (bit 0 = oberste Zeile). A-Z und Leerzeichen. */
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
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* Leerzeichen */
};

static uint16_t swap16(uint16_t c) {
  return (uint16_t)((c >> 8) | (c << 8));
}

static void tx_cmd(uint8_t cmd, const uint8_t *data, size_t len) {
  esp_lcd_panel_io_tx_param(s_io, cmd, data, len);
}

static void fill_fb(uint16_t color) {
  uint16_t v = swap16(color);
  for (int i = 0; i < LCD_W * LCD_H; i++) s_fb[i] = v;
}

static void draw_text(int x, int y, const char *s, uint16_t color) {
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
            if (px >= 0 && px < LCD_W && py >= 0 && py < LCD_H)
              s_fb[py * LCD_W + px] = v;
          }
        }
      }
    }
    x += ADVANCE;
  }
}

static void flush_fb(void) {
  int x0 = X_OFFSET, y0 = Y_OFFSET;
  int x1 = X_OFFSET + LCD_W - 1, y1 = Y_OFFSET + LCD_H - 1;
  uint8_t caset[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
  uint8_t raset[4] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
  tx_cmd(0x2A, caset, 4); /* CASET */
  tx_cmd(0x2B, raset, 4); /* RASET */
  esp_lcd_panel_io_tx_color(s_io, 0x2C, s_fb, LCD_W * LCD_H * 2); /* RAMWR */
}

static void paint(display_state_t state) {
  uint16_t bg, fg;
  const char *label;
  switch (state) {
    case DISPLAY_RUNNING:
      bg = 0xFD20; /* orange */
      fg = 0x0000;
      label = "LAEUFT";
      break;
    case DISPLAY_DONE:
      bg = 0x07E0; /* gruen */
      fg = 0x0000;
      label = "FERTIG";
      break;
    default:
      bg = 0xF800; /* rot */
      fg = 0xFFFF;
      label = "GESTOPPT";
      break;
  }

  fill_fb(bg);

  int len = (int)strlen(label);
  int text_w = len * ADVANCE - SCALE;
  int x = (LCD_W - text_w) / 2;
  int y = (LCD_H - 7 * SCALE) / 2;
  if (x < 0) x = 0;
  draw_text(x, y, label, fg);
  flush_fb();
}

static void st7735_init_seq(void) {
  tx_cmd(0x01, NULL, 0); /* SWRESET */
  vTaskDelay(pdMS_TO_TICKS(150));
  tx_cmd(0x11, NULL, 0); /* SLPOUT */
  vTaskDelay(pdMS_TO_TICKS(255));

  uint8_t colmod = 0x05; /* 16 bit/pixel */
  tx_cmd(0x3A, &colmod, 1);

  uint8_t madctl = MADCTL_VAL;
  tx_cmd(0x36, &madctl, 1);

#if INVERT_COLORS
  tx_cmd(0x21, NULL, 0); /* INVON */
#else
  tx_cmd(0x20, NULL, 0); /* INVOFF */
#endif

  tx_cmd(0x13, NULL, 0); /* NORON */
  vTaskDelay(pdMS_TO_TICKS(10));
  tx_cmd(0x29, NULL, 0); /* DISPON */
  vTaskDelay(pdMS_TO_TICKS(100));
}

void display_init(void) {
  /* Backlight ist bei diesem Board fest verdrahtet (kein GPIO noetig). */

  /* Hardware-Reset. */
  gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_RST,
                       .mode = GPIO_MODE_OUTPUT};
  gpio_config(&rst);
  gpio_set_level(PIN_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(PIN_RST, 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level(PIN_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(150));

  spi_bus_config_t buscfg = {
      .sclk_io_num = PIN_SCLK,
      .mosi_io_num = PIN_MOSI,
      .miso_io_num = -1,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = LCD_W * LCD_H * 2 + 16,
  };
  if (spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
    ESP_LOGW(TAG, "spi_bus_initialize falhou");
    return;
  }

  esp_lcd_panel_io_spi_config_t io_config = {
      .cs_gpio_num = PIN_CS,
      .dc_gpio_num = PIN_DC,
      .spi_mode = 0,
      .pclk_hz = PIXEL_CLK_HZ,
      .trans_queue_depth = 10,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
  };
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config,
                               &s_io) != ESP_OK) {
    ESP_LOGW(TAG, "panel io falhou");
    return;
  }

  s_fb = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_DMA);
  if (!s_fb) {
    ESP_LOGW(TAG, "sem memoria para framebuffer");
    return;
  }

  st7735_init_seq();

  s_ready = true;
  s_state = DISPLAY_STOPPED;
  paint(s_state);
}

void display_set_status(display_state_t state) {
  if (!s_ready || state == s_state) return;
  s_state = state;
  paint(state);
}
