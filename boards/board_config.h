#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Selects the pin configuration for the board chosen with -DUF2LOADER_BOARD

#if UF2LOADER_BOARD_PICOCALC
#include "picocalc.h"
#elif UF2LOADER_BOARD_RESTOUCH
#include "restouch.h"
#else
#error "No board selected, set UF2LOADER_BOARD in CMake"
#endif

#include "hardware/gpio.h"

// Deselect any other devices sharing the SD card's SPI bus
static inline void board_sd_bus_init(void)
{
#ifdef SD_BUS_OTHER_CS_PINS
  static const uint8_t other_cs[] = {SD_BUS_OTHER_CS_PINS};

  for (unsigned i = 0; i < sizeof(other_cs); i++)
  {
    gpio_init(other_cs[i]);
    gpio_put(other_cs[i], 1);
    gpio_set_dir(other_cs[i], GPIO_OUT);
  }
#endif
}

#endif // BOARD_CONFIG_H
