/**
 * PicoCalc SD Firmware Loader
 *
 * Author: Hsuan Han Lai
 * Email: hsuan.han.lai@gmail.com
 * Website: https://hsuanhanlai.com
 * Year: 2025
 *
 * key_event.c
 *
 * Wrapper for post processing dispatch keyboard events
 *
 */

#include "i2ckbd.h"
#include <pico/stdio.h>
#include "board_config.h"
#include "key_event.h"
#include "debug.h"

#if defined(TP_IRQ_PIN)

#include "input_touch.h"

void keypad_init(void)
{
    touch_init();
}

int keypad_get_key(void)
{
    return touch_get_key();
}

// No battery gauge, the title bar skips the icon
int keypad_get_battery() {
    return -1;
}

#else

void keypad_init(void)
{
    init_i2c_kbd();
}

int keypad_get_key(void)
{
    return read_i2c_kbd();
}

int keypad_get_battery() {
    int bat_pcnt = read_battery();
    bat_pcnt = bat_pcnt >> 8;
    //int bat_charging = bitRead(bat_pcnt, 7);
    bitClear(bat_pcnt, 7);
    return bat_pcnt;
}

#endif
