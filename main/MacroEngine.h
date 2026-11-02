#ifndef MacroEngine_h
#define MacroEngine_h

#include <stdbool.h>
#include <stdint.h>

#include "switch_consts.h"
#include "tusb.h"  // HID_KEY_* keycodes used by macro scripts

#ifndef MACRO_START_DELAY_MS
#define MACRO_START_DELAY_MS 1000
#endif


typedef enum {
  MACRO_OP_WAIT = 0,
  MACRO_OP_KEYS,
  MACRO_OP_PAD,
  MACRO_OP_BOTH,
  MACRO_OP_KEY_TIMES,
  MACRO_OP_END,
} MacroOp;

typedef struct {
  MacroOp op;
  uint32_t durationMs;
  uint32_t count;
  KeyboardReport keyboard;
  SwitchUsbReport gamepad;
} MacroStep;

#define MACRO_KBD_RELEASED \
  { 0, 0, { 0, 0, 0, 0, 0, 0 } }
#define MACRO_PAD_NEUTRAL                                             \
  {                                                                   \
    0, SWITCH_USB_HAT_NOTHING, SWITCH_USB_JOYSTICK_MID,               \
        SWITCH_USB_JOYSTICK_MID, SWITCH_USB_JOYSTICK_MID,             \
        SWITCH_USB_JOYSTICK_MID, 0                                    \
  }

#define WAIT(ms) { MACRO_OP_WAIT, ms, 1, MACRO_KBD_RELEASED, MACRO_PAD_NEUTRAL }
#define KEYS(ms, mod, k1, k2, k3, k4, k5, k6) \
  { MACRO_OP_KEYS, ms, 1, { mod, 0, { k1, k2, k3, k4, k5, k6 } }, MACRO_PAD_NEUTRAL }
#define KEY_TIMES(n, ms, mod, k) \
  { MACRO_OP_KEY_TIMES, ms, n, { mod, 0, { k, 0, 0, 0, 0, 0 } }, MACRO_PAD_NEUTRAL }
#define PAD(ms, buttons, hat, lx, ly, rx, ry) \
  { MACRO_OP_PAD, ms, 1, MACRO_KBD_RELEASED, { buttons, hat, lx, ly, rx, ry, 0 } }
#define PAD_BTN(ms, buttons)                                             \
  PAD(ms, buttons, SWITCH_USB_HAT_NOTHING, SWITCH_USB_JOYSTICK_MID,      \
      SWITCH_USB_JOYSTICK_MID, SWITCH_USB_JOYSTICK_MID,                  \
      SWITCH_USB_JOYSTICK_MID)
#define PAD_DPAD(ms, hat)                                                \
  PAD(ms, 0, hat, SWITCH_USB_JOYSTICK_MID, SWITCH_USB_JOYSTICK_MID,      \
      SWITCH_USB_JOYSTICK_MID, SWITCH_USB_JOYSTICK_MID)
#define PAD_LSTICK(ms, x, y)                                             \
  PAD(ms, 0, SWITCH_USB_HAT_NOTHING, x, y, SWITCH_USB_JOYSTICK_MID,      \
      SWITCH_USB_JOYSTICK_MID)
#define PAD_RSTICK(ms, x, y)                                             \
  PAD(ms, 0, SWITCH_USB_HAT_NOTHING, SWITCH_USB_JOYSTICK_MID,            \
      SWITCH_USB_JOYSTICK_MID, x, y)
#define BOTH(ms, mod, k1, k2, k3, k4, k5, k6, buttons, hat, lx, ly, rx, ry) \
  { MACRO_OP_BOTH, ms, 1, { mod, 0, { k1, k2, k3, k4, k5, k6 } },             \
    { buttons, hat, lx, ly, rx, ry, 0 } }
#define END() { MACRO_OP_END, 0, 1, MACRO_KBD_RELEASED, MACRO_PAD_NEUTRAL }

void macro_init(void);
void macro_reset(void);
void macro_start(uint32_t now_ms);
void macro_tick(uint32_t now_ms);
void macro_get_reports(SwitchUsbReport *pad, KeyboardReport *kbd);
bool macro_finished(void);

#endif
