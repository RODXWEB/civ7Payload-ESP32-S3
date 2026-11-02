#ifndef DISPLAY_H
#define DISPLAY_H

typedef enum {
  DISPLAY_STOPPED = 0,
  DISPLAY_RUNNING,
  DISPLAY_DONE,
} display_state_t;

void display_init(void);
void display_set_status(display_state_t state);

#endif
