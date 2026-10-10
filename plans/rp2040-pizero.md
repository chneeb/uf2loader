# Port: Waveshare RP2040-PiZero (HDMI + USB input)

Goal: multiboot DVI apps on the RP2040-PiZero from the SD card: pico-infonesPlus,
tiny_agi (`tinyagi_dvi_rp2040`) and MCUME emulators that fit the RP2040's 264K
SRAM. The RP2350-PiZero follows once this works (see the end).

## Board

- RP2040, 16 MB flash, RUN button (reset) and BOOT button.
- HDMI (DVI over PIO): data 26/24/22, clock 28, not inverted (gusmanb's PicoDVI
  fork, `waveshare_rp2040_pizero`). The board feeds 5 V to HDMI pin 18, so TVs
  work (unlike the RP2350-PiZero).
- SD: spi0, SCK 18, MOSI 19, MISO 20, CS 21, no other devices on the bus.
- USB: native USB-C port as host for a keyboard or gamepad; it powers the device.
  The board is powered through the other USB-C port.
- DVI needs `clk_sys` = 252 MHz (10x the 25.2 MHz pixel clock for 640x480p60)
  with the core voltage raised; core 1 does the TMDS encoding.

## Decisions

- **DVI library:** upstream PicoDVI (github.com/Wren6991/PicoDVI) as a git
  submodule, pinned to a commit, using only `software/libdvi`. It is C, has
  1bpp/8bpp encoders, and the pin config is a struct defined in our board
  header, so no fork is needed. pico_lib (Galagino, tiny_agi) is not needed:
  its addition over PicoDVI is HDMI audio, and the menu has no sound. Apps keep
  their own DVI library; the chip resets between menu and app.
- **Framebuffer:** 320x240 RGB332 (77K), shown pixel- and line-doubled at
  640x480. The menu's drawing calls stay as they are. Fallback if RAM gets
  tight: a text mode.
- **Entering the menu:** double press of RUN within ~0.5 s (pattern of the
  SDK's `pico_bootsel_via_double_reset`: a magic value in uninitialised RAM
  survives a RUN reset, not a power cycle). USB doesn't fit in stage3's 8K.
- **No USB drive mode** on this board: the native port is the USB host
  (`UF2LOADER_USB_MSC=OFF`).

## RAM estimate for the menu (264K)

| Part | Size |
|---|---|
| Current menu (`BOOT2040.elf`, ResTouch build: 39K text, 40K bss) | ~80K |
| TinyUSB host + HID parser | ~20K |
| PicoDVI + TMDS line buffers | ~15K |
| RGB332 framebuffer 320x240 | 77K |
| **Total** | **~190K** |

## Work packages

1. **Board header and CMake** (0.5 d)
   - `boards/pizero2040.h`: SD pins, DVI pin struct, `UF2LOADER_BOARD=pizero2040`.
   - 16 MB flash (`PICO_FLASH_SIZE_BYTES`); stage3 sits in the top 16K as on
     every RP2040 build.
   - PicoDVI submodule, only added to the build for this board.
2. **stage3** (0.5 d)
   - `read_bootmode()`: double-press-RUN detection instead of keyboard/touch.
   - SD pins via the board header; everything else is unchanged RP2040 code.
   - Check the 8K limit.
3. **Menu display backend** (1-1.5 d)
   - A DVI implementation of the calls the UI uses (`lcd_init`, `draw_text`,
     `draw_rect_spi`, `draw_line_spi`, `draw_filled_rect`, `draw_battery_icon`,
     `lcd_print_string_color`, `lcd_set_cursor`) on the RGB332 framebuffer.
   - Clock to 252 MHz with the voltage raised, DVI scanout on core 1. The
     menu runs from RAM, so core 1 never fetches from flash (Galagino's red
     flicker problem doesn't apply).
   - Shut DVI down cleanly before starting an app.
4. **Menu USB input** (1 d)
   - TinyUSB host on the native port, Galagino's `usb_input.c` and `hidparser/`
     (keyboard, gamepads including the SNES clone `0079:0011` byte map).
   - Arrows/d-pad -> Up/Down, Enter/A -> Enter, Backspace/B -> Back.
5. **Keep the other boards unchanged** (0.5 d)
   - The DVI backend and USB input only build for this board. PicoCalc stays
     byte-identical, ResTouch unchanged; check by building both.
6. **Apps** (1-1.5 d)
   - tiny_agi `tinyagi_dvi_rp2040`: flash cache already reads the usable end
     from the vector table (`XIP_BASE+0x114`); should run as is.
   - pico-infonesPlus: limit its flash use to below uf2loader's top 16K
     (open item in CLAUDE.md), ~0.5 d.
   - MCUME: check per emulator which ones build for the RP2040-PiZero and how
     they use flash.

Total about 5-6 days.

## Risks

- RAM for the menu: measure after packages 3 and 4; text mode as fallback.
- Flash writes while DVI runs (both chips): flash can't be read during
  `flash_range_erase/program`, so core 1 and the DVI IRQ must not touch it.
  The menu is a no_flash build, so nothing of it is in flash and DVI should keep
  running; verify the map file once, and keep any flash reads (e.g. of the
  installed app's proginfo) on core 0 outside the write.
- SD under DVI: tiny_agi needed a polling SD driver because of a lost wakeup in
  its DMA/semaphore wait. The menu's `ui/lib/sdmmc` uses plain polled SPI
  without DMA, so it shouldn't hit this.

## Afterwards: RP2350-PiZero (about 1-1.5 d)

- Pins: TMDS on 32-38, SD on spi1 SCK 30 / MOSI 31 / MISO 40 / CS 43 (RP2350B).
  PIO needs `pio_set_gpio_base(pio0, 16)` before `dvi_init()`; upstream PicoDVI
  doesn't do this itself, so try it from our init first, fork only if needed.
- 16 MB flash, RP2350 partition layout as on the other pico2 builds.
- The board does not feed 5 V to HDMI pin 18; some TVs stay dark.
