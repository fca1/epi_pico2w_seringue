cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(pico2_seringue C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/FreeRTOS-Kernel)
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

set(DISPENSER_SOURCES src/main.c src/app_tasks.c src/app_state.c src/buttons.c src/motor_control.c
  src/tmc5130.c src/tmc_current.c src/device_config.c src/config_store.c src/statistics.c src/safety.c src/command_api.c
  src/stallguard_calibration.c src/status_led.c)
if(PICO_BOARD STREQUAL "pico2_w")
  list(APPEND DISPENSER_SOURCES src/ble_service.c)
  set(DISPENSER_HAS_RADIO 1)
else()
  set(DISPENSER_HAS_RADIO 0)
endif()
add_executable(paste_dispenser ${DISPENSER_SOURCES})
target_include_directories(paste_dispenser PRIVATE include)
target_compile_definitions(paste_dispenser PRIVATE
  DISPENSER_HAS_RADIO=${DISPENSER_HAS_RADIO})
target_link_libraries(paste_dispenser pico_stdlib hardware_spi hardware_flash pico_flash
  FreeRTOS-Kernel-Heap4)
if(DISPENSER_HAS_RADIO)
  target_link_libraries(paste_dispenser pico_btstack_ble pico_btstack_cyw43
    pico_cyw43_arch_none pico_unique_id)
  pico_btstack_make_gatt_header(paste_dispenser PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/paste_dispenser.gatt)
endif()
pico_enable_stdio_usb(paste_dispenser 1)
pico_enable_stdio_uart(paste_dispenser 0)
pico_add_extra_outputs(paste_dispenser)
