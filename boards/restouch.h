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

#define LCD_SPI1    1
#define LCD_SCK_PIN 10
#define LCD_MOSI_PIN 11
#define LCD_MISO_PIN 12
#define LCD_CS_PIN 9
#define LCD_DC_PIN 8
#define LCD_RST_PIN 15
#define LCD_BL_PIN 13

// GPIOs for the XPT2046 touch controller
#define TP_CS_PIN      16
#define TP_IRQ_PIN     17

#endif // BOARD_RESTOUCH_H
