# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)

project(lcdspi
        VERSION 0.0.1
        DESCRIPTION "lcdspi for picocalc"
        )

add_library(lcdspi INTERFACE)

if (UF2LOADER_BOARD STREQUAL "pizero")
  # HDMI from a framebuffer instead of an SPI panel, libdvi comes from ui/lib/PicoDVI
  target_sources(lcdspi INTERFACE
          lcddvi.c
          )

  target_link_libraries(lcdspi INTERFACE pico_stdlib pico_multicore hardware_spi hardware_vreg
                        libdvi board_config)
else()
  target_sources(lcdspi INTERFACE
          lcdspi.c
          )

  target_link_libraries(lcdspi INTERFACE  pico_stdlib hardware_spi board_config)
endif()

target_include_directories(lcdspi INTERFACE ${CMAKE_CURRENT_LIST_DIR})