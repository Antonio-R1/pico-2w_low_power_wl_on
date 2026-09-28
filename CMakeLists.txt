cmake_minimum_required(VERSION 3.13)

# Pico SDK from PICO_SDK_PATH (environment or -DPICO_SDK_PATH=...), see README.md.
# Uses the SDK's own external/pico_sdk_import.cmake, so no copy of it lives here.
set(PICO_BOARD pico2_w CACHE STRING "Board type")
if (NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()
if (NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    message(FATAL_ERROR "PICO_SDK_PATH ('${PICO_SDK_PATH}') is not a pico-sdk folder (see README.md)")
endif()
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

# The SDK's cyw43-driver must carry patches/cyw43-driver-release-wake-up.patch
# (CYW43_SPI_RELEASE_WAKE_UP_ON_SLEEP). Unpatched, the define below is ignored
# and the low-power modes 4-8 draw several mA more without any error.
file(STRINGS "${PICO_SDK_PATH}/lib/cyw43-driver/src/cyw43_ll.c" WLP_DRIVER_PATCHED
     REGEX "CYW43_SPI_RELEASE_WAKE_UP_ON_SLEEP" LIMIT_COUNT 1)
if (NOT WLP_DRIVER_PATCHED)
    message(FATAL_ERROR "cyw43-driver in ${PICO_SDK_PATH} is not patched. Run:\n"
        "  git -C \"${PICO_SDK_PATH}/lib/cyw43-driver\" apply "
        "\"${CMAKE_CURRENT_LIST_DIR}/patches/cyw43-driver-release-wake-up.patch\"")
endif()

project(wl_on_low_power C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# Host sleep style, one per build folder (see build.sh):
#   OFF: builds/12MHz_LPO       RP2350 sleeps at 12 MHz (crystal, PLLs off, clock-gated WFI)
#   ON:  builds/pstate_ram_off  RP2350 in Pstate with all SRAM off (every wake is a reboot)
option(WLP_PSTATE "Host sleeps in Pstate (all SRAM off) instead of 12 MHz WFI" OFF)
# Where the builds go: one subfolder per mode, <WLP_OUT_DIR>/<mode>/<mode>.uf2
set(WLP_OUT_DIR "${CMAKE_BINARY_DIR}" CACHE PATH "Output folder for the per-mode build folders")

# One executable and .uf2 per mode. The number selects the mode in src/config.h.
function(wlp_mode name number)
    add_executable(${name} src/main.c src/host.c src/radio.c src/wl_led.c)
    target_include_directories(${name} PRIVATE src ${CMAKE_CURRENT_LIST_DIR})
    target_compile_definitions(${name} PRIVATE
        WLP_MODE=${number}
        WLP_PSTATE=$<BOOL:${WLP_PSTATE}>
        CYW43_LWIP=0
        PICO_FLASH_ASSUME_CORE1_SAFE=1)
    if (number GREATER_EQUAL 4)
        # The low-power modes: the driver clears the gSPI WAKE_UP bit whenever it
        # puts the bus to sleep (patches/cyw43-driver-release-wake-up.patch).
        target_compile_definitions(${name} PRIVATE CYW43_SPI_RELEASE_WAKE_UP_ON_SLEEP=1)
    endif()
    target_link_libraries(${name}
        pico_stdlib
        pico_multicore
        pico_low_power
        pico_cyw43_arch_none
        pico_btstack_ble
        pico_btstack_cyw43
        pico_btstack_flash_bank
        hardware_clocks
        hardware_pll
        hardware_powman
        hardware_vreg)
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${WLP_OUT_DIR}/${name}
                                             ARCHIVE_OUTPUT_DIRECTORY ${WLP_OUT_DIR}/${name})  # .uf2
    pico_add_extra_outputs(${name})   # builds/<host>/<name>/<name>.uf2
endfunction()

wlp_mode(1_wl_on_off              1)
wlp_mode(2_wl_on_no_firmware      2)
wlp_mode(3_wl_on_firmware         3)
wlp_mode(4_lowest_no_adv          4)
wlp_mode(5_lowest_no_adv_led_5s   5)
wlp_mode(6_lowest_adv_100ms       6)
wlp_mode(7_lowest_adv_1s          7)
wlp_mode(8_lowest_adv_100ms_led   8)
