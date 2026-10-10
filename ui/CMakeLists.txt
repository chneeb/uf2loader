project(ui C CXX ASM)

add_subdirectory(lib/sdmmc)
add_subdirectory(lib/fatfs)
# The pizero's native USB port is a host for the menu input, no USB drive mode
if (NOT UF2LOADER_BOARD STREQUAL "pizero")
  add_subdirectory(lib/usb_msc)
  set(ui_usb_msc usb_msc)
endif()
add_subdirectory(lib/binary_info_reader)

add_executable(ui
  main.c
  uf2.c
  key_event.c
  text_directory_ui.c
)

if (PICO_PLATFORM MATCHES "^rp2040")
  set_target_properties(ui PROPERTIES OUTPUT_NAME "BOOT2040")
else ()
  set_target_properties(ui PROPERTIES OUTPUT_NAME "BOOT2350")
endif()

if (UF2LOADER_BOARD STREQUAL "restouch")
  target_sources(ui PRIVATE input_touch.c)

  option(UF2LOADER_TOUCH_DEBUG "Show raw touch readings in the status bar" OFF)
  if (UF2LOADER_TOUCH_DEBUG)
    target_compile_definitions(ui PRIVATE TOUCH_DEBUG=1)
  endif()
endif()

if (UF2LOADER_BOARD STREQUAL "pizero")
  # HDMI output (common/lcdspi/lcddvi.c) and USB host input
  add_subdirectory(lib/PicoDVI/software/libdvi libdvi)
  target_compile_definitions(libdvi INTERFACE DVI_VERTICAL_REPEAT=2 DVI_N_TMDS_BUFFERS=3)

  # stage3 loads the menu as one contiguous block below the scratch banks, so
  # PicoDVI's encoder and tables (.scratch_x/.scratch_y) go into .text instead
  file(READ ${PICO_LINKER_SCRIPT_PATH}/memmap_no_flash.ld ui_ld)
  string(REPLACE "*(.time_critical*)" "*(.time_critical*)\n        *(.scratch_x.*)\n        *(.scratch_y.*)"
         ui_ld "${ui_ld}")
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/memmap_ui.ld "${ui_ld}")
  pico_set_linker_script(ui ${CMAKE_CURRENT_BINARY_DIR}/memmap_ui.ld)

  target_sources(ui PRIVATE input_usb.c lib/hidparser/hidparser.c)
  target_include_directories(ui PRIVATE usb_host lib/hidparser)
  target_link_libraries(ui tinyusb_host)
endif()

target_compile_options(ui PRIVATE -mno-unaligned-access)

pico_set_program_name(ui "uf2load")
pico_set_program_description(ui "UI for UF2 Loader")
pico_set_program_version(ui "${VERSION_STRING}")

target_include_directories(ui PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(ui
  pico_stdlib
  pico_unique_id
  hardware_sync
  hardware_flash
  hardware_irq
  hardware_i2c
  hardware_spi
  hardware_dma
  i2ckbd
  lcdspi
  fatfs
  boot_uf2_headers
  boot_picobin_headers
  bootloader
  board_config
  pr_binary_info_reader
  ${ui_usb_msc}
)

pico_enable_stdio_usb(ui 0)
pico_enable_stdio_uart(ui 0)

pico_set_binary_type(ui no_flash)

pico_add_extra_outputs(ui)

target_link_options(ui PRIVATE -Wl,--print-memory-usage)

option(UF2LOADER_USB_MSC "Offer the SD card as a USB drive while the menu is open" ON)
if (UF2LOADER_USB_MSC AND ui_usb_msc)
  set(enable_usb 1)
else()
  set(enable_usb 0)
endif()

target_compile_options(ui PRIVATE -DENABLE_RAM_APPS=1 -DENABLE_USB=${enable_usb})
