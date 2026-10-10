#ifndef BOARD_PIZERO_H
#define BOARD_PIZERO_H

// Waveshare RP2040-PiZero: HDMI (DVI over PIO), microSD, USB host on the
// native USB-C port. Build with PICO_BOARD=waveshare_rp2040_pizero (16 MB flash).

#include "pico.h"

#if !PICO_RP2040
#error "Only the RP2040-PiZero is supported so far"
#endif

// GPIOs for SPI interface (SD card), dedicated bus
#define SD_SPI_INST     spi0
#define SD_SCLK_PIN     18
#define SD_MOSI_PIN     19
#define SD_MISO_PIN     20
#define SD_CS_PIN       21
// No card detect pin

// The menu is shown over HDMI from a framebuffer (common/lcddvi)
#define LCD_DVI 1

// TMDS pairs (the + pin, - is the next GPIO) for blue, green, red and the clock
#define DVI_PIN_D0      26
#define DVI_PIN_D1      24
#define DVI_PIN_D2      22
#define DVI_PIN_CLK     28
#define DVI_INVERT_DIFFPAIRS false

// Menu input from a USB keyboard or gamepad
#define USB_HOST_INPUT 1

// The menu is entered by pressing RUN twice within this time
#define DOUBLE_RESET_TIMEOUT_MS 500

// Menu layout for the 320x240 framebuffer
#define UI_X 20
#define UI_Y 0
#define UI_WIDTH 280
#define UI_HEIGHT 240
#define ITEMS_PER_PAGE 13

#endif // BOARD_PIZERO_H
