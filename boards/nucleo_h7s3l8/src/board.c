/*
 * board.c - NUCLEO-H7S3L8 (STM32H7S3L8, Cortex-M7) implementation of board.h.
 *
 * Runs directly from the 64 KB internal flash, no external-memory boot stage.
 *
 * Clock tree: see clock_config() (HSE 24 MHz, CPU 600 MHz, FDCAN 200 MHz).
 *
 * Pins:
 *   LD1 green   PD10  status (blinks at 5 Hz while the comms task runs)
 *   LD2 yellow  PD13  debug pin, toggled every control loop -> 500 Hz square wave
 *   LD3 red     PB7   error (CAN error passive / bus-off / peer lost)
 *   B1  user    PC13  active low (pull-up)
 *   USART3      PD8 TX / PD9 RX -> ST-LINK virtual COM port, 115200 8N1
 *   FDCAN1      PD1 TX / PD0 RX -> external CAN FD transceiver
 *
 * SPDX-License-Identifier: MIT
 */
#include "board.h"

#include <stdio.h>
#include <string.h>

#include "stm32h7rsxx_hal.h"

#ifndef RJS_NODE_ID
#error "RJS_NODE_ID must be defined by the build (1..127)"
#endif

#define LED_STATUS_PORT GPIOD
#define LED_STATUS_PIN  GPIO_PIN_10
#define LED_DEBUG_PORT  GPIOD
#define LED_DEBUG_PIN   GPIO_PIN_13
#define LED_ERROR_PORT  GPIOB
#define LED_ERROR_PIN   GPIO_PIN_7
#define BUTTON_PORT     GPIOC
#define BUTTON_PIN      GPIO_PIN_13

static UART_HandleTypeDef s_uart;

/* ---- low-level set-up ------------------------------------------------------ */

/* Same MPU policy as ST's templates: block speculative accesses to the
 * unused external-memory address range (background region, no access). */
static void mpu_config(void)
{
    MPU_Region_InitTypeDef r = {0};
    HAL_MPU_Disable();
    r.Enable           = MPU_REGION_ENABLE;
    r.Number           = MPU_REGION_NUMBER0;
    r.BaseAddress      = 0x0;
    r.Size             = MPU_REGION_SIZE_4GB;
    r.SubRegionDisable = 0x87;
    r.TypeExtField     = MPU_TEX_LEVEL0;
    r.AccessPermission = MPU_REGION_NO_ACCESS;
    r.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.IsShareable      = MPU_ACCESS_SHAREABLE;
    r.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    r.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/*
 * HSE 24 MHz:
 *   PLL1: 24 MHz / M 2 = 12 MHz * N 50 = 600 MHz, P /1 -> SYSCLK 600 MHz (CPU)
 *   PLL2: 24 MHz / M 2 = 12 MHz * N 50 = 600 MHz, P /3 -> 200 MHz FDCAN kernel clock
 *   AHB /2 = 300 MHz, APB1/2/4/5 /2 = 150 MHz
 * Values as in ST's NUCLEO-H7S3L8 Template_XIP (Boot_HSE configuration).
 */
static void clock_config(void)
{
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK) {
        board_fatal("voltage scaling");
    }

    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType    = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState          = RCC_HSE_ON;
    osc.PLL1.PLLState     = RCC_PLL_ON;
    osc.PLL1.PLLSource    = RCC_PLLSOURCE_HSE;
    osc.PLL1.PLLM         = 2;
    osc.PLL1.PLLN         = 50;
    osc.PLL1.PLLP         = 1;
    osc.PLL1.PLLQ         = 2;
    osc.PLL1.PLLR         = 2;
    osc.PLL1.PLLS         = 2;
    osc.PLL1.PLLT         = 2;
    osc.PLL1.PLLFractional = 0;
    osc.PLL2.PLLState     = RCC_PLL_ON;
    osc.PLL2.PLLSource    = RCC_PLLSOURCE_HSE;
    osc.PLL2.PLLM         = 2;
    osc.PLL2.PLLN         = 50;
    osc.PLL2.PLLP         = 3;
    osc.PLL2.PLLQ         = 2;
    osc.PLL2.PLLR         = 2;
    osc.PLL2.PLLS         = 3;
    osc.PLL2.PLLT         = 2;
    osc.PLL2.PLLFractional = 0;
    osc.PLL3.PLLState     = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        board_fatal("oscillator config");
    }

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK4 | RCC_CLOCKTYPE_PCLK5;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    clk.AHBCLKDivider  = RCC_HCLK_DIV2;
    clk.APB1CLKDivider = RCC_APB1_DIV2;
    clk.APB2CLKDivider = RCC_APB2_DIV2;
    clk.APB4CLKDivider = RCC_APB4_DIV2;
    clk.APB5CLKDivider = RCC_APB5_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_7) != HAL_OK) {
        board_fatal("clock config");
    }
}

static void cycle_counter_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55u;          /* unlock DWT on Cortex-M7 */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void gpio_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin   = LED_STATUS_PIN | LED_DEBUG_PIN;
    HAL_GPIO_Init(GPIOD, &g);
    g.Pin = LED_ERROR_PIN;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOD, LED_STATUS_PIN | LED_DEBUG_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, LED_ERROR_PIN, GPIO_PIN_RESET);

    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    g.Pin  = BUTTON_PIN;
    HAL_GPIO_Init(BUTTON_PORT, &g);
}

static void uart_init(void)
{
    __HAL_RCC_USART3_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &g);

    s_uart.Instance            = USART3;
    s_uart.Init.BaudRate       = 115200;
    s_uart.Init.WordLength     = UART_WORDLENGTH_8B;
    s_uart.Init.StopBits       = UART_STOPBITS_1;
    s_uart.Init.Parity         = UART_PARITY_NONE;
    s_uart.Init.Mode           = UART_MODE_TX_RX;
    s_uart.Init.HwFlowCtl      = UART_HWCONTROL_NONE;
    s_uart.Init.OverSampling   = UART_OVERSAMPLING_16;
    s_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    s_uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    s_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&s_uart) != HAL_OK) {
        /* No UART, no message: show the error LED and stop. */
        HAL_GPIO_WritePin(LED_ERROR_PORT, LED_ERROR_PIN, GPIO_PIN_SET);
        for (;;) {
        }
    }
}

/* ---- board.h ------------------------------------------------------------ */

void board_init(void)
{
    mpu_config();
    SCB_EnableICache();
    SCB_EnableDCache();
    HAL_Init();            /* SysTick 1 ms HAL time base, NVIC priority group 4 */
    clock_config();
    cycle_counter_init();
    gpio_init();
    uart_init();
}

const char *board_name(void)
{
    return "NUCLEO-H7S3L8";
}

uint8_t board_node_id(void)
{
    return (uint8_t)RJS_NODE_ID;
}

uint32_t board_millis(void)
{
    return HAL_GetTick();
}

uint32_t board_cycles(void)
{
    return DWT->CYCCNT;
}

uint32_t board_cycles_per_us(void)
{
    return SystemCoreClock / 1000000u;
}

void board_led_status_toggle(void)
{
    HAL_GPIO_TogglePin(LED_STATUS_PORT, LED_STATUS_PIN);
}

void board_led_error(bool on)
{
    HAL_GPIO_WritePin(LED_ERROR_PORT, LED_ERROR_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void board_debug_pin_toggle(void)
{
    HAL_GPIO_TogglePin(LED_DEBUG_PORT, LED_DEBUG_PIN);
}

bool board_button_raw(void)
{
    return HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN) == GPIO_PIN_RESET;   /* active low */
}

/* Register-level polling: works from any context, even with interrupts off,
 * and never waits on a HAL lock held by an interrupted task. */
void board_uart_write(const char *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        while ((USART3->ISR & USART_ISR_TXE_TXFNF) == 0u) {
        }
        USART3->TDR = (uint8_t)data[i];
    }
}

void board_fatal(const char *reason)
{
    __disable_irq();
    HAL_GPIO_WritePin(LED_ERROR_PORT, LED_ERROR_PIN, GPIO_PIN_SET);
    static const char prefix[] = "\r\n*** FATAL: ";
    board_uart_write(prefix, sizeof(prefix) - 1u);
    board_uart_write(reason, strlen(reason));
    board_uart_write("\r\n", 2u);
    for (;;) {
        /* Stay here. With a debugger attached: halt and inspect the call stack. */
    }
}

void board_assert_failed(const char *file, int line);
void board_assert_failed(const char *file, int line)
{
    char msg[96];
    const char *base = strrchr(file, '/');
    (void)snprintf(msg, sizeof(msg), "assert %s:%d", base ? base + 1 : file, line);
    board_fatal(msg);
}

/* ST HAL calls this from assert_param() when USE_FULL_ASSERT is defined. */
void assert_failed(uint8_t *file, uint32_t line);
void assert_failed(uint8_t *file, uint32_t line)
{
    board_assert_failed((const char *)file, (int)line);
}
