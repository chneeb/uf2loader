#ifndef BOARD_RESTOUCH_H
#define BOARD_RESTOUCH_H

// Waveshare Pico-ResTouch-LCD-2.8 (Pico or Pico 2 carrier)

// GPIOs for SPI interface (SD card)
// The SD card runs in SPI mode on spi1, shared with the LCD and touch controller.
// Its SDIO lines (CLK GP5, CMD GP18, D0-D2 GP19-21) are also wired to the Pico,
// so those pins must be left unconfigured.
#define SD_SPI_INST     spi1
#define SD_SCLK_PIN     10
#define SD_MOSI_PIN     11
#define SD_MISO_PIN     12
#define SD_CS_PIN       22
// No card detect pin

// Other chip selects on the SD bus, driven high before the SD card is accessed
#define SD_BUS_OTHER_CS_PINS LCD_CS_PIN, TP_CS_PIN
// SD and LCD share spi1, so each sets its own clock before using the bus
#define SD_LCD_SHARED_BUS 1

#define LCD_ST7789 1

#define LCD_SPI1    1
#define LCD_SCK_PIN 10
#define LCD_MOSI_PIN 11
#define LCD_MISO_PIN 12
#define LCD_CS_PIN 9
#define LCD_DC_PIN 8
#define LCD_RST_PIN 15
#define LCD_BL_PIN 13

// GPIOs for the XPT2046 touch controller
#define TP_SPI_INST    spi1
#define TP_CS_PIN      16
#define TP_IRQ_PIN     17

// Touch calibration: raw readings at the screen edges. TP_SWAP_XY selects
// which controller channel runs along the screen's x axis; swapping an edge
// pair mirrors that axis. Build with -DUF2LOADER_TOUCH_DEBUG=ON to show the
// raw values in the status bar.
#define TP_SWAP_XY     1
#define TP_RAW_LEFT    200
#define TP_RAW_RIGHT   3900
#define TP_RAW_TOP     3900
#define TP_RAW_BOTTOM  200

// Menu layout for the 320x240 panel
#define UI_X 20
#define UI_Y 0
#define UI_WIDTH 280
#define UI_HEIGHT 240
#define ITEMS_PER_PAGE 13

#endif // BOARD_RESTOUCH_H
