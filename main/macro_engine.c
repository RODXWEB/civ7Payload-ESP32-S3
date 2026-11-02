#include "MacroEngine.h"

#include "macro_script.h"

static bool _running;
static bool _finished;
static bool _stepLoaded;
static bool _keyDownPhase;
static uint32_t _pressesLeft;
static uint32_t _step;
static uint32_t _stepEnd;
static SwitchUsbReport _pad;
static KeyboardReport _kbd;

static void neutral_outputs(void) {
  _kbd = (KeyboardReport){0, 0, {0, 0, 0, 0, 0, 0}};
  _pad = (SwitchUsbReport){0, SWITCH_USB_HAT_NOTHING, SWITCH_USB_JOYSTICK_MID,
                           SWITCH_USB_JOYSTICK_MID, SWITCH_USB_JOYSTICK_MID,
                           SWITCH_USB_JOYSTICK_MID, 0};
}

void macro_init(void) {
  _running = false;
  _finished = false;
  _stepLoaded = false;
  _keyDownPhase = false;
  _pressesLeft = 0;
  _step = 0;
  _stepEnd = 0;
  neutral_outputs();
}

void macro_reset(void) {
  _running = false;
  _finished = false;
  _stepLoaded = false;
  _keyDownPhase = false;
  _pressesLeft = 0;
  _step = 0;
  _stepEnd = 0;
  neutral_outputs();
}

void macro_start(uint32_t now_ms) {
  if (_running || _finished) return;
  _running = true;
  _step = 0;
  _stepEnd = now_ms;
  _stepLoaded = false;
}

static void load_step(size_t index) {
  const MacroStep *step = &MACRO_SCRIPT[index];
  neutral_outputs();
  if (step->op == MACRO_OP_KEYS || step->op == MACRO_OP_BOTH) {
    _kbd = step->keyboard;
  }
  if (step->op == MACRO_OP_PAD || step->op == MACRO_OP_BOTH) {
    _pad = step->gamepad;
  }
  if (step->op == MACRO_OP_KEY_TIMES) {
    _kbd = step->keyboard;
    _pressesLeft = step->count;
    _keyDownPhase = true;
  }
}

static bool tick_repeat(const MacroStep *step) {
  if (step->op != MACRO_OP_KEY_TIMES) return false;
  if (_keyDownPhase) {
    _kbd = (KeyboardReport){0, 0, {0, 0, 0, 0, 0, 0}};
    _keyDownPhase = false;
    _stepEnd += step->durationMs;
    return true;
  }
  _pressesLeft--;
  if (_pressesLeft == 0) return false;
  _kbd = step->keyboard;
  _keyDownPhase = true;
  _stepEnd += step->durationMs;
  return true;
}

void macro_tick(uint32_t now_ms) {
  if (_finished || !_running) return;

  while (now_ms >= _stepEnd) {
    const MacroStep *step = &MACRO_SCRIPT[_step];
    if (step->op == MACRO_OP_END) {
      _finished = true;
      neutral_outputs();
      return;
    }
    if (!_stepLoaded) {
      load_step(_step);
      _stepLoaded = true;
      _stepEnd += step->durationMs;
      continue;
    }
    if (tick_repeat(step)) continue;
    _stepLoaded = false;
    _step++;
  }
}

void macro_get_reports(SwitchUsbReport *pad, KeyboardReport *kbd) {
  *pad = _pad;
  *kbd = _kbd;
}

bool macro_finished(void) {
  return _finished;
}
