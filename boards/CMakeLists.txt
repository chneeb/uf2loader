pico_add_library(board_config)

target_include_directories(board_config INTERFACE ${CMAKE_CURRENT_LIST_DIR})

string(TOUPPER "${UF2LOADER_BOARD}" board_upper)
target_compile_definitions(board_config INTERFACE UF2LOADER_BOARD_${board_upper}=1)

pico_mirrored_target_link_libraries(board_config INTERFACE hardware_gpio)
