#ifndef INPUT_USB_H
#define INPUT_USB_H

// USB keyboards and gamepads on the native USB port (TinyUSB host), translated
// into the menu's key codes:
//
//   Up/Down (arrows, d-pad, stick)                 -> Up/Down, repeat while held
//   Enter, gamepad A or Start                      -> Enter
//   Backspace, Escape, Left, gamepad B or Select   -> Back

void usb_input_init(void);

// Services the USB host stack. Returns a key code from key_event.h, or -1 if
// there is no new key
int usb_input_get_key(void);

#endif // INPUT_USB_H
