# NUCLEO-H7S3L8 firmware (STM32H7S3L8, Cortex-M7, runs from 64 KB internal flash).
#
#   cmake --preset h7s3l8-node1 && cmake --build --preset h7s3l8-node1
#
# Third-party sources are fetched by scripts/fetch_deps.sh into third_party/.

set(RJS_NODE_ID 1 CACHE STRING "CAN node ID of this board (1 = controller)")
option(RJS_ENABLE_DISPLAY "Build with the ST7789 TFT status display" OFF)

set(TP ${CMAKE_SOURCE_DIR}/third_party)
foreach(dep FreeRTOS-Kernel stm32h7rsxx_hal_driver cmsis_device_h7rs CMSIS_5)
    if(NOT EXISTS ${TP}/${dep})
        message(FATAL_ERROR "third_party/${dep} is missing - run scripts/fetch_deps.sh first")
    endif()
endforeach()

set(CPU_FLAGS -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)

# ---- vendor code: ST HAL + CMSIS device (own target, relaxed warnings) --------
set(HAL ${TP}/stm32h7rsxx_hal_driver/Src)
add_library(st_hal STATIC
    ${HAL}/stm32h7rsxx_hal.c
    ${HAL}/stm32h7rsxx_hal_cortex.c
    ${HAL}/stm32h7rsxx_hal_rcc.c
    ${HAL}/stm32h7rsxx_hal_rcc_ex.c
    ${HAL}/stm32h7rsxx_hal_gpio.c
    ${HAL}/stm32h7rsxx_hal_pwr.c
    ${HAL}/stm32h7rsxx_hal_pwr_ex.c
    ${HAL}/stm32h7rsxx_hal_flash.c
    ${HAL}/stm32h7rsxx_hal_flash_ex.c
    ${HAL}/stm32h7rsxx_hal_dma.c
    ${HAL}/stm32h7rsxx_hal_dma_ex.c
    ${HAL}/stm32h7rsxx_hal_exti.c
    ${HAL}/stm32h7rsxx_hal_fdcan.c
    ${HAL}/stm32h7rsxx_hal_uart.c
    ${HAL}/stm32h7rsxx_hal_uart_ex.c
    ${HAL}/stm32h7rsxx_hal_spi.c
    ${HAL}/stm32h7rsxx_hal_spi_ex.c
    ${TP}/cmsis_device_h7rs/Source/Templates/system_stm32h7rsxx.c
)
target_include_directories(st_hal PUBLIC
    config
    ${TP}/stm32h7rsxx_hal_driver/Inc
    ${TP}/cmsis_device_h7rs/Include
    ${TP}/CMSIS_5/CMSIS/Core/Include
)
target_compile_definitions(st_hal PUBLIC STM32H7S3xx USE_HAL_DRIVER)
target_compile_options(st_hal PRIVATE -w)

# ---- FreeRTOS kernel --------------------------------------------------------
set(RTOS ${TP}/FreeRTOS-Kernel)
add_library(freertos STATIC
    ${RTOS}/tasks.c
    ${RTOS}/queue.c
    ${RTOS}/list.c
    ${RTOS}/portable/GCC/ARM_CM4F/port.c   # recommended port for Cortex-M7 cores other than r0p1
    ${RTOS}/portable/MemMang/heap_4.c
)
target_include_directories(freertos PUBLIC
    config
    ${RTOS}/include
    ${RTOS}/portable/GCC/ARM_CM4F
)
target_compile_options(freertos PRIVATE -w)

# ---- apply CPU flags to everything in this directory -----------------------
foreach(t st_hal freertos rjs_hal rjs_protocol rjs_node rjs_joint rjs_ui)
    target_compile_options(${t} PRIVATE ${CPU_FLAGS} -ffunction-sections -fdata-sections -O2 -g3)
endforeach()

# ---- firmware image ------------------------------------------------------------
set(FW rjs_h7s3l8_node${RJS_NODE_ID})
add_executable(${FW}
    src/main.c
    src/board.c
    src/hal_can_fdcan.c
    src/stm32h7rsxx_it.c
    src/syscalls.c
    ${CMAKE_SOURCE_DIR}/app/joint_node/app.c
    ${CMAKE_SOURCE_DIR}/app/joint_node/log.c
    ${TP}/cmsis_device_h7rs/Source/Templates/gcc/startup_stm32h7s3xx.s
)
target_include_directories(${FW} PRIVATE ${CMAKE_SOURCE_DIR}/app/joint_node)
target_compile_definitions(${FW} PRIVATE RJS_NODE_ID=${RJS_NODE_ID})
target_link_libraries(${FW} PRIVATE st_hal freertos rjs_protocol rjs_node rjs_joint rjs_hal rjs_ui)

if(RJS_ENABLE_DISPLAY)
    target_sources(${FW} PRIVATE src/display_st7789.c ${CMAKE_SOURCE_DIR}/app/joint_node/ui.c)
    target_compile_definitions(${FW} PRIVATE RJS_ENABLE_DISPLAY=1)
else()
    target_compile_definitions(${FW} PRIVATE RJS_ENABLE_DISPLAY=0)
endif()
target_compile_options(${FW} PRIVATE ${CPU_FLAGS} -ffunction-sections -fdata-sections -O2 -g3
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror>)

set(LDSCRIPT ${TP}/cmsis_device_h7rs/Source/Templates/gcc/linker/stm32h7s3xx_flash.ld)
target_link_options(${FW} PRIVATE
    ${CPU_FLAGS}
    -T${LDSCRIPT}
    --specs=nano.specs --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=${FW}.map
    -Wl,--print-memory-usage
    -Wl,--no-warn-rwx-segments
)
set_target_properties(${FW} PROPERTIES SUFFIX .elf LINK_DEPENDS ${LDSCRIPT})

add_custom_command(TARGET ${FW} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${FW}> ${FW}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${FW}> ${FW}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${FW}>
    COMMENT "Creating ${FW}.hex / .bin"
)
