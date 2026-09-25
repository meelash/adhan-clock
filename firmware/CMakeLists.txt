# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 1.5.1)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.0.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(adhan_clock C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ==============================================================================
# FatFS library (fetched from upstream)
# ==============================================================================
include(FetchContent)
FetchContent_Declare(
    fatfs
    URL      http://elm-chan.org/fsw/ff/arc/ff15.zip
    URL_HASH MD5=fb46ec41fc6aa82b45e5b3eb9262270e
)
FetchContent_MakeAvailable(fatfs)
# ff.h does #include "ffconf.h", which always resolves next to ff.h first, so
# our configuration (long file names, f_gets/f_puts, ...) must replace the
# stock file in the fetched sources.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/ffconf.h
               ${fatfs_SOURCE_DIR}/source/ffconf.h COPYONLY)

add_library(fatfs_lib STATIC
    ${fatfs_SOURCE_DIR}/source/ff.c
    ${fatfs_SOURCE_DIR}/source/ffsystem.c
    ${fatfs_SOURCE_DIR}/source/ffunicode.c
    src/drivers/sdcard.c          # provides diskio implementation
)
target_include_directories(fatfs_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${fatfs_SOURCE_DIR}/source
)
target_link_libraries(fatfs_lib PUBLIC
    pico_stdlib
    hardware_i2c
    hardware_uart
)

# ==============================================================================
# libadhan prayer time calculation library (compiled from source)
# ==============================================================================
add_library(adhan_lib STATIC
    lib/adhan/astronomical.c
    lib/adhan/solar_coordinates.c
    lib/adhan/solar_time.c
    lib/adhan/calculation_parameters.c
    lib/adhan/prayer_times.c
    lib/adhan/data_components.c
    lib/adhan/calendrical_helper.c
)
target_include_directories(adhan_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/adhan/include
)
# The library sources use "include/foo.h" relative paths
target_include_directories(adhan_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/adhan
)
target_link_libraries(adhan_lib PUBLIC pico_stdlib)


# ==============================================================================
# Main firmware executable
# ==============================================================================
add_executable(adhan_clock
    src/main.c
    # Drivers
    src/drivers/hub75.c
    src/drivers/ds3231.c
    src/drivers/gps.c
    src/drivers/audio_i2s.c
    src/drivers/buttons.c
    src/drivers/ir_remote.c
    src/drivers/light_sensor.c
    src/drivers/buzzer.c
    src/drivers/usb_msc.c
    src/drivers/usb_descriptors.c
    # Prayer library
    src/prayer/prayer_times.c
    src/prayer/hijri.c
    # UI
    src/ui/display.c
    src/ui/screens.c
    src/ui/fonts.c
    src/ui/theme.c
    # Application
    src/app/clock_app.c
    src/app/adhan.c
    src/app/settings.c
    src/app/timeutil.c
)

# I2S program (assembled by pioasm into audio_i2s.pio.h)
pico_generate_pio_header(adhan_clock ${CMAKE_CURRENT_LIST_DIR}/pio/audio_i2s.pio)

target_include_directories(adhan_clock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(adhan_clock
    pico_stdlib
    pico_multicore
    hardware_i2c
    hardware_adc
    hardware_pio
    hardware_dma
    hardware_pwm
    hardware_timer
    hardware_flash
    hardware_sync
    tinyusb_device
    tinyusb_board
    fatfs_lib
    adhan_lib
)

# Core 0 stack: the SDK default is 2 KB, too tight for FatFS objects.
# 4 KB is the most that fits in the SCRATCH_Y bank.
target_compile_definitions(adhan_clock PRIVATE PICO_STACK_SIZE=0x1000)

# USB is used for mass storage, UART0 for GPS: no stdio.
pico_enable_stdio_usb(adhan_clock 0)
pico_enable_stdio_uart(adhan_clock 0)

pico_add_extra_outputs(adhan_clock)

# ==============================================================================
# Scan-type diagnostic (standalone, no RTOS/FatFS/etc.)
# Flash scan_test.uf2 and observe which of the two alternating phases
# shows all 32 rows correctly — that tells you 1/16 vs 1/32 scan.
# ==============================================================================
add_executable(scan_test src/scan_test.c)
target_include_directories(scan_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(scan_test pico_stdlib)
pico_enable_stdio_uart(scan_test 0)
pico_add_extra_outputs(scan_test)
