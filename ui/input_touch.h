#ifndef INPUT_TOUCH_H
#define INPUT_TOUCH_H

// XPT2046 resistive touch input, translated into the menu's key codes:
//
//   +---------------------------+
//   |       UP (top third)      |  repeats while held
//   +-------------+-------------+
//   |    BACK     |    ENTER    |  fires on release
//   +-------------+-------------+
//   |    DOWN (bottom third)    |  repeats while held
//   +---------------------------+

void touch_init(void);

// Returns a key code from key_event.h, or -1 if there is no new key
int touch_get_key(void);

#endif // INPUT_TOUCH_H
