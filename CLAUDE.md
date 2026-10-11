# CLAUDE.md

uf2loader is an SD card bootloader for the RP2040 and RP2350: a small resident
stage in flash loads a menu (a RAM-only UF2) from the SD card, and the menu
installs app UF2s from the card into flash and starts them. Originally PicoCalc
only; this fork adds the Waveshare Pico-ResTouch-LCD-2.8 and the Waveshare
RP2040-PiZero (HDMI, USB input).

Remotes: `origin` = upstream (github.com/pelrun/uf2loader), `fork` = the
user's fork (github.com/chneeb/uf2loader). Board work lives on the
`restouch-port` branch, pushed to `fork`; no upstream PR yet. The PiZero port
is on `pizero-port`, branched from `restouch-port`.

## Layout

```
stage3/          Resident bootloader, 8K max (linker scripts memmap_2040/2350.ld)
  stage3.c       Boot mode (key/touch at power-on), mounts SD, loads BOOTxxxx.UF2 into RAM
  uf2.c          UF2 → RAM loader for the menu
  boot2_custom.S RP2040: boot2 that jumps to stage3 at the top of flash
  partitions.json.in  RP2350: partition table (app partition from 8K to end of flash)
ui/              Menu (BOOT2040.UF2 / BOOT2350.UF2, no_flash RAM image)
  main.c         Init, SD card detect, USB MSC, main loop
  text_directory_ui.c  File list, folders, status bar
  uf2.c          Writes an app UF2 into flash (+ proginfo into its vector table)
  key_event.c    Input: CardKB-style I2C keyboard (PicoCalc), touch (ResTouch) or USB (PiZero)
  input_touch.c  XPT2046 → Up/Down/Back/Enter zones (ResTouch)
  input_usb.c    USB keyboards/gamepads → menu keys (PiZero, from galagino_pizero)
  usb_host/      TinyUSB host config (PiZero)
  lib/           FatFs, sdmmc SPI driver, usb_msc, binary_info_reader, hidparser,
                 pico-vfs and PicoDVI (submodules)
common/
  bootloader/    proginfo.c: magic + flash_end in the app's vector table, stage3 commands
  lcdspi/        LCD driver: ILI9488 (PicoCalc) or ST7789 (ResTouch); lcddvi.c is
                 the same API on an HDMI framebuffer (PiZero)
  petit_fatfs/   Petit FatFs + SPI disk driver used by stage3
  i2ckbd/        PicoCalc keyboard (only linked for picocalc)
boards/          Per-board pin/feature headers, selected by UF2LOADER_BOARD
  board_config.h Includes picocalc.h, restouch.h or pizero.h; board_sd_bus_init()
diag/            PicoCalc-only diagnostic app
```

## Build

Pico SDK 2.2.0 (`~/Source/pico-sdk`). `PICO_BOARD` is global to a configure, so
each chip/board combination needs its own build dir:

```bash
# ResTouch, Pico 2 (what the user runs)
cmake -S . -B build/restouch-pico2 -DPICO_BOARD=pico2 \
      -DUF2LOADER_BOARD=restouch -DUF2LOADER_USB_MSC=OFF
make -C build/restouch-pico2 -j$(nproc)
# ResTouch, Pico: same with -DPICO_BOARD=pico and build/restouch-pico
# PicoCalc: -DUF2LOADER_BOARD=picocalc (the default)

# Waveshare RP2040-PiZero (needs the PicoDVI submodule: git submodule update --init)
cmake -S . -B build/pizero-pico -DPICO_BOARD=waveshare_rp2040_pizero \
      -DUF2LOADER_BOARD=pizero -DUF2LOADER_USB_MSC=OFF
make -C build/pizero-pico -j$(nproc)
```

Options:
- `UF2LOADER_BOARD` = `picocalc` (default) | `restouch` | `pizero`
- `UF2LOADER_USB_MSC` (ON): SD card as a USB drive while the menu is open. The
  user builds with OFF: the board is powered from the PC, and MSC starts as soon
  as the host enumerates, which hides the menu. Always off on the pizero, whose
  native USB port is the host for the menu input.
- `UF2LOADER_TOUCH_DEBUG` (OFF, restouch): raw touch values in the status bar.
- `UF2LOADER_USB_DEBUG` (OFF, pizero): in the status bar, the root port's line
  state (`-`/`L`/`F`), attach events, completed transfers, mounted devices and
  HID interfaces, the last interface's type and its last report. Build it in
  its own dir (`build/pizero-pico-usbdebug`) and rename its `BOOT2040.uf2` to
  `BOOT2040-usbdebug.uf2`, then relink the normal one: both write the same file.

Outputs go to `<build dir>/../output/`, i.e. **all build dirs under `build/`
share `build/output/`**, and a file there is only refreshed when its target
relinks. The names don't say which board they are for (still open, see below).
- `bootloader_<PICO_BOARD>.uf2` (e.g. `bootloader_pico2.uf2`,
  `bootloader_waveshare_rp2040_pizero.uf2`): stage3, flash with BOOTSEL.
- `BOOT2040.uf2` / `BOOT2350.uf2`: the menu, goes in the SD card root.
- Apps go in `pico1-apps/` / `pico2-apps/` on the card; subfolders are listed.

To compare builds, diff the `.bin`/`.elf` in the build dirs, not `output/`.

## Rules for changes

- **PicoCalc must stay byte-identical** unless a change is meant for it. Board
  code goes behind `#if`/`#ifdef` on board macros (`TP_IRQ_PIN`, `SD_DET_PIN`,
  `SD_LCD_SHARED_BUS`, `LCD_ST7789`, ...), and the stage3 link order is kept
  (`petit_fatfs ${stage3_kbd} bootloader board_config`) because reordering
  changes the binary. Check by building picocalc before and after.
- **stage3 has 8K of flash** (`_bootloader_app_size`). Check the size of the
  loadable sections (`arm-none-eabi-objdump -h`) after changes; ResTouch
  RP2350 is at about 6.4K.
- The menu runs entirely from RAM; stage3 runs from flash.

## Boot flow

1. stage3 reads the boot mode within 0.5 s of reset: PicoCalc keyboard (Up/F1/F5
   → menu, Down/F3 → BOOTSEL), ResTouch touch held on the screen → menu,
   PiZero a second press of RUN within the 0.5 s → menu.
2. A command from the menu (watchdog scratch[0..2], `bl_stage3_command()`) can
   also select the mode, e.g. `BOOT_RAM` with a filename.
3. Default: start the app in flash. Menu: mount SD with Petit FatFs (5 tries),
   load `BOOTxxxx.UF2` into RAM and jump to it. Failure → BOOTSEL.
4. The menu writes the selected app into flash and starts it with
   `watchdog_reboot(0, 0, 0)`.

## What apps need to know

- **RP2040:** stage3 sits in the top 16K of flash (its position follows
  `PICO_FLASH_SIZE_BYTES`: 2 MB on a Pico, 16 MB on the PiZero); apps must not
  write there.
- **RP2350:** the app runs from a partition at 8K that the bootrom maps to
  `0x10000000` via the QMI ATRANS registers. `flash_range_erase/program` take
  physical offsets, so `addr - XIP_BASE` writes 8K too low. Translate through
  `qmi_hw->atrans[virt >> 22]` (see `flash_offset.h` in
  rp2040-ili9341-infones or `phys_offset()` in tiny_agi's `flashfs.c`).
- **Usable flash:** the menu stores magic `0xe98cc638` and the end address of
  the app area in an unused vector table slot of the installed app:
  `XIP_BASE+0x110`/`+0x114` on RP2040, `XIP_BASE+0x20`/`+0x24` on RP2350.
- **Watchdog:** apps are started with `watchdog_reboot(0, 0, 0)`, which
  `watchdog_caused_reboot()` reports as true. Apps that use a watchdog reboot as
  their own signal should check `watchdog_enable_caused_reboot()` instead.
- **Shared buses survive the reset:** the Pico resets, the devices on the board
  don't. Apps should deselect everything on a shared SPI bus before using it,
  and free a shared I2C bus (clock SCL until SDA is released) before
  `i2c_init`.

## ResTouch (Waveshare Pico-ResTouch-LCD-2.8)

Pins are in `boards/restouch.h`:
- spi1 shared by everything: SCK 10, MOSI 11, MISO 12.
- LCD: ST7789 320x240, CS 9, DC 8, RST 15, BL 13. MADCTL 0xA0, INVON, RGB565.
- SD: CS 22, no card detect.
- Touch: XPT2046, CS 16, PENIRQ 17.
- The SD card's SDIO lines (GP5, GP18-21) are also wired to the Pico, so leave
  them unconfigured.

Details:
- **Shared bus:** each device sets its own SPI clock before each access (SD
  20 MHz after init, touch 1 MHz, LCD its own speed), and the other chip
  selects are parked high (`board_sd_bus_init()`, `lcd_spi_init()`).
- **Touch:** zones are top third = Up, bottom third = Down (both repeat), middle
  left = Back, middle right = Enter (fire on release). Calibration is in
  `restouch.h`: `TP_SWAP_XY 1` (the controller's Y channel runs along the screen's
  x axis), and the edge values are swapped to mirror both axes.
- **Touch at reset:** the XPT2046 keeps its power-down mode across a Pico reset,
  and an app that leaves touch CS floating while driving the LCD can switch
  PENIRQ off. stage3 therefore sends one conversion with PD=00 before polling
  PENIRQ (`touch_enable_penirq()`), and leaves touch CS and SD CS high for the
  app.
- No battery readout (`keypad_get_battery()` returns -1).

## RP2040-PiZero (Waveshare)

Board header `boards/pizero.h`, `PICO_BOARD=waveshare_rp2040_pizero`. Plan and
remaining work: `plans/rp2040-pizero.md`.
- HDMI: DVI over PIO with upstream PicoDVI (`ui/lib/PicoDVI`, only `libdvi`
  is built). Data pairs 26/24/22, clock 28. 640x480p60, so `clk_sys` = 252 MHz
  (set in `lcd_init()`) with the core at 1.2 V. The board feeds 5 V to HDMI pin
  18, so TVs work.
- Display: 320x240 RGB332 framebuffer, pixel and line doubled; core 1 encodes
  one line per two scanlines and handles the DVI DMA interrupt. The menu is RAM
  only, so DVI keeps running while the menu writes flash.
- stage3 loads the menu as one contiguous block below `SRAM_END - 0x2000`
  (`stage3/uf2.c`), but PicoDVI puts its encoder and tables in `.scratch_x`.
  The pizero menu therefore links with a copy of the SDK's
  `memmap_no_flash.ld` that pulls `.scratch_x/.scratch_y` into `.text`
  (generated in `ui/CMakeLists.txt`).
- SD: spi0, SCK 18, MOSI 19, MISO 20, CS 21, no card detect.
- Input: TinyUSB host on the native USB-C port (`ui/input_usb.c`): arrows,
  d-pad or stick → Up/Down (repeat), Enter / pad A / Start → Enter, Backspace /
  Escape / Left / pad B / Select → Back. Generic pads: buttons 1-2 = Enter, 3-4
  = Back. Known SNES clones use fixed byte maps (from galagino_pizero). Only
  interfaces declaring a gamepad/joystick collection go through the generic
  HID parser, so keyboards' extra interfaces (media keys, NKRO) are skipped.
  The menu's own descriptor check replaces TinyUSB's
  `tuh_hid_parse_report_descriptor()`, which misreads 4-byte items and can
  read far past the descriptor. Keyboards get boot protocol from TinyUSB
  itself; the menu doesn't set it again from the mount callback, where that
  would block the next interface's configuration. History: a wired keyboard
  works; a wireless keyboard+mouse receiver stalled with "USB F a1 x22 d0 h0"
  (2026-10-11), fix built, not yet retested.
- Entering the menu: press RUN twice within 0.5 s. stage3 keeps a magic value in
  `.uninitialized_data`, which RAM keeps across a RUN reset.

## Apps tested on the ResTouch (Pico 2)

All with fixes merged in their own repos:
- `~/Source/rp2040-ili9341-infones` (`PICO_RESTOUCH`): NES. Flash offset fix,
  `watchdog_enable_caused_reboot()`.
- `~/Source/RP2040-GNUBoy`: Game Boy, ROM compiled in. NES Mini controller on
  i2c0 GP4/5, display rotated, touch/SD CS parked.
- `~/Source/tiny_agi` (`tinyagi_restouch`): Sierra AGI. CardKB on i2c0 GP4/5
  (chained with the NES Mini controller), flash cache made partition-aware.

## Open items

- USB drive mode only on request (e.g. a long press) instead of on/off at build time.
- Separate output names per board, CI matrix for restouch, README section.
- The menu title still says "PicoCalc UF2 Loader".
- RP2040-PiZero: built, not yet tested on hardware; then apps and the
  RP2350-PiZero (`plans/rp2040-pizero.md`).
- pico-infonesPlus: flash fix (usable end from the vector table, ATRANS on the
  RP2350) on branch `uf2loader` in `~/Source/pico-infonesPlus` and its
  `pico_shared` submodule; local only, not tested on hardware. Pushing needs
  forks of both (pico_shared is fhoedemakers/pico_shared).
