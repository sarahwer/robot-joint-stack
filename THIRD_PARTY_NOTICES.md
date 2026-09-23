# Third-party components

Fetched at build time by `scripts/fetch_deps.sh` into `third_party/` (not part of this repository):

| Component | Version | License | Source |
|---|---|---|---|
| FreeRTOS-Kernel | V11.1.0 | MIT | https://github.com/FreeRTOS/FreeRTOS-Kernel |
| STM32H7RSxx HAL driver | v1.2.1 | BSD-3-Clause | https://github.com/STMicroelectronics/stm32h7rsxx_hal_driver |
| CMSIS device STM32H7RS | v1.2.1 | Apache-2.0 | https://github.com/STMicroelectronics/cmsis_device_h7rs |
| CMSIS 5 (Core headers) | 5.9.0 | Apache-2.0 | https://github.com/ARM-software/CMSIS_5 |

Included in this repository:

| File | Origin | License |
|---|---|---|
| `boards/nucleo_h7s3l8/config/stm32h7rsxx_hal_conf.h` | STM32CubeH7RS, `Projects/NUCLEO-H7S3L8/Templates/Template/Boot` (FDCAN and UART modules enabled) | BSD-3-Clause, © STMicroelectronics |

| `common/ui/src/font8x16.c` | glyph bitmaps rendered from DejaVu Sans Mono by `tools/gen_font.py` | DejaVu fonts license (Bitstream Vera / Arev derivative), © Bitstream Inc. and the DejaVu authors |

The clock and MPU configuration in `boards/nucleo_h7s3l8/src/board.c` follows the values of ST's
NUCLEO-H7S3L8 templates.
