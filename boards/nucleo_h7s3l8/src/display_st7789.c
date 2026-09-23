/*
 * display_st7789.c - driver for a 2.0" 240x320 ST7789V SPI TFT module
 * (e.g. "GMT020-02-7P", 7 pins: CS DC RST SDA SCL VCC GND).
 *
 * Wiring to the NUCLEO-H7S3L8 (all pins configurable below):
 *
 *   module | MCU pin | Arduino header | note
 *   -------+---------+----------------+------------------------------------
 *   SCL    | PA5     | D13            | SPI1_SCK
 *   SDA    | PB5     | D11            | SPI1_MOSI (module has no MISO)
 *   CS     | PD14    | -              | chip select
 *   DC     | PD15    | -              | 0 = command, 1 = data
 *   RST    | PD11    | -              | hardware reset
 *   VCC    | 3V3     |                | module regulator + backlight
 *   GND    | GND     |                |
 *
 * Verify the header positions of PD11/PD14/PD15 in the board user manual before
 * wiring; they are free in this project (FDCAN uses PD0/PD1, USART3 PD8/PD9,
 * LEDs PD10/PD13).
 *
 * Pixels go out over SPI with DMA, so the CPU is free while a cell is drawn.
 * The data cache is cleaned before each transfer, because the DMA reads the
 * buffer from AXI SRAM and would otherwise see stale data.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "board.h"
#include "stm32h7rsxx_hal.h"

#define DISP_WIDTH  240u
#define DISP_HEIGHT 320u

#define DISP_SPI            SPI1
#define DISP_SPI_IRQn       SPI1_IRQn
#define DISP_DMA_CHANNEL    GPDMA1_Channel0
#define DISP_DMA_REQUEST    GPDMA1_REQUEST_SPI1_TX
#define DISP_DMA_IRQn       GPDMA1_Channel0_IRQn
#define DISP_SCK_PORT       GPIOA
#define DISP_SCK_PIN        GPIO_PIN_5
#define DISP_MOSI_PORT      GPIOB
#define DISP_MOSI_PIN       GPIO_PIN_5
#define DISP_CS_PORT        GPIOD
#define DISP_CS_PIN         GPIO_PIN_14
#define DISP_DC_PORT        GPIOD
#define DISP_DC_PIN         GPIO_PIN_15
#define DISP_RST_PORT       GPIOD
#define DISP_RST_PIN        GPIO_PIN_11
#define DISP_IRQ_PRIORITY   6u      /* >= configMAX_SYSCALL_INTERRUPT_PRIORITY */

/* One 8x16 character cell = 128 pixels = 256 bytes. */
#define CHUNK_PIXELS 128u

static SPI_HandleTypeDef  s_spi;
static DMA_HandleTypeDef  s_dma;
static SemaphoreHandle_t  s_done;
static bool               s_ready;

/* DMA source buffer: 32-byte aligned and a multiple of 32 bytes, so cache
 * maintenance never touches neighbouring variables. */
static uint8_t s_chunk[CHUNK_PIXELS * 2u] __attribute__((aligned(32)));

void GPDMA1_Channel0_IRQHandler(void);
void SPI1_IRQHandler(void);

/* ---- low level ------------------------------------------------------------- */

/* Wait without burning CPU once the scheduler runs (HAL_Delay busy-waits). */
static void delay_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
        HAL_Delay(ms);
    }
}

static inline void cs(bool low)   { HAL_GPIO_WritePin(DISP_CS_PORT, DISP_CS_PIN, low ? GPIO_PIN_RESET : GPIO_PIN_SET); }
static inline void dc(bool data)  { HAL_GPIO_WritePin(DISP_DC_PORT, DISP_DC_PIN, data ? GPIO_PIN_SET : GPIO_PIN_RESET); }

static void spi_write_blocking(const uint8_t *data, uint16_t len)
{
    (void)HAL_SPI_Transmit(&s_spi, data, len, 100);
}

static void spi_write_dma(const uint8_t *data, uint16_t len)
{
    SCB_CleanDCache_by_Addr((uint32_t *)(void *)data, (int32_t)len);
    if (HAL_SPI_Transmit_DMA(&s_spi, data, len) != HAL_OK) {
        return;
    }
    /* The transfer takes ~35 us at 40 MHz; 50 ms is a generous error timeout. */
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(50)) != pdTRUE) {
        (void)HAL_SPI_Abort(&s_spi);
    }
}

static void write_cmd(uint8_t cmd)
{
    dc(false);
    cs(true);
    spi_write_blocking(&cmd, 1);
    cs(false);
}

static void write_data(const uint8_t *data, uint16_t len)
{
    dc(true);
    cs(true);
    spi_write_blocking(data, len);
    cs(false);
}

static void set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    const uint16_t x1 = (uint16_t)(x + w - 1u);
    const uint16_t y1 = (uint16_t)(y + h - 1u);
    uint8_t buf[4];

    buf[0] = (uint8_t)(x >> 8);  buf[1] = (uint8_t)x;
    buf[2] = (uint8_t)(x1 >> 8); buf[3] = (uint8_t)x1;
    write_cmd(0x2A);             /* CASET: column address */
    write_data(buf, 4);

    buf[0] = (uint8_t)(y >> 8);  buf[1] = (uint8_t)y;
    buf[2] = (uint8_t)(y1 >> 8); buf[3] = (uint8_t)y1;
    write_cmd(0x2B);             /* RASET: row address */
    write_data(buf, 4);

    write_cmd(0x2C);             /* RAMWR: start writing pixels */
}

/* ---- HAL glue ------------------------------------------------------------- */

void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != DISP_SPI) {
        return;
    }
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_SPI1;
    pclk.Spi1ClockSelection   = RCC_SPI1CLKSOURCE_PLL1Q;
    (void)HAL_RCCEx_PeriphCLKConfig(&pclk);

    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF5_SPI1;
    g.Pin       = DISP_SCK_PIN;
    HAL_GPIO_Init(DISP_SCK_PORT, &g);
    g.Pin = DISP_MOSI_PIN;
    HAL_GPIO_Init(DISP_MOSI_PORT, &g);

    HAL_NVIC_SetPriority(DISP_SPI_IRQn, DISP_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(DISP_SPI_IRQn);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != DISP_SPI) {
        return;
    }
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    portYIELD_FROM_ISR(woken);
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != DISP_SPI) {
        return;
    }
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    portYIELD_FROM_ISR(woken);
}

void GPDMA1_Channel0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&s_dma);
}

void SPI1_IRQHandler(void)
{
    HAL_SPI_IRQHandler(&s_spi);
}

/* ---- board.h display API ---------------------------------------------------- */

bool board_display_init(void)
{
    s_done = xSemaphoreCreateBinary();
    if (s_done == NULL) {
        return false;
    }

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPDMA1_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Pin   = DISP_CS_PIN | DISP_DC_PIN | DISP_RST_PIN;
    HAL_GPIO_Init(GPIOD, &g);
    cs(false);
    dc(true);

    /* SPI: master, transmit only, mode 0. PLL1Q / 8 keeps the clock inside the
     * ST7789 write timing (its minimum write cycle is 66 ns). */
    s_spi.Instance               = DISP_SPI;
    s_spi.Init.Mode              = SPI_MODE_MASTER;
    s_spi.Init.Direction         = SPI_DIRECTION_2LINES_TXONLY;
    s_spi.Init.DataSize          = SPI_DATASIZE_8BIT;
    s_spi.Init.CLKPolarity       = SPI_POLARITY_LOW;
    s_spi.Init.CLKPhase          = SPI_PHASE_1EDGE;
    s_spi.Init.NSS               = SPI_NSS_SOFT;
    s_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    s_spi.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    s_spi.Init.TIMode            = SPI_TIMODE_DISABLE;
    s_spi.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    s_spi.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
    s_spi.Init.FifoThreshold     = SPI_FIFO_THRESHOLD_01DATA;
    s_spi.Init.MasterSSIdleness  = SPI_MASTER_SS_IDLENESS_00CYCLE;
    s_spi.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    s_spi.Init.MasterReceiverAutoSusp  = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    s_spi.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;
    s_spi.Init.NSSPolarity       = SPI_NSS_POLARITY_LOW;
    s_spi.Init.IOSwap            = SPI_IO_SWAP_DISABLE;
    if (HAL_SPI_Init(&s_spi) != HAL_OK) {
        return false;
    }

    s_dma.Instance                 = DISP_DMA_CHANNEL;
    s_dma.Init.Request             = DISP_DMA_REQUEST;
    s_dma.Init.BlkHWRequest        = DMA_BREQ_SINGLE_BURST;
    s_dma.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    s_dma.Init.SrcInc              = DMA_SINC_INCREMENTED;
    s_dma.Init.DestInc             = DMA_DINC_FIXED;
    s_dma.Init.SrcDataWidth        = DMA_SRC_DATAWIDTH_BYTE;
    s_dma.Init.DestDataWidth       = DMA_DEST_DATAWIDTH_BYTE;
    s_dma.Init.Priority            = DMA_LOW_PRIORITY_LOW_WEIGHT;
    s_dma.Init.SrcBurstLength      = 1;
    s_dma.Init.DestBurstLength     = 1;
    s_dma.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
    s_dma.Init.TransferEventMode   = DMA_TCEM_BLOCK_TRANSFER;
    s_dma.Init.Mode                = DMA_NORMAL;
    if (HAL_DMA_Init(&s_dma) != HAL_OK) {
        return false;
    }
    __HAL_LINKDMA(&s_spi, hdmatx, s_dma);
    if (HAL_DMA_ConfigChannelAttributes(&s_dma, DMA_CHANNEL_NPRIV) != HAL_OK) {
        return false;
    }
    HAL_NVIC_SetPriority(DISP_DMA_IRQn, DISP_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(DISP_DMA_IRQn);

    /* Hardware reset, then the ST7789 start-up sequence. */
    HAL_GPIO_WritePin(DISP_RST_PORT, DISP_RST_PIN, GPIO_PIN_RESET);
    delay_ms(10);
    HAL_GPIO_WritePin(DISP_RST_PORT, DISP_RST_PIN, GPIO_PIN_SET);
    delay_ms(120);

    write_cmd(0x01);                    /* SWRESET */
    delay_ms(150);
    write_cmd(0x11);                    /* SLPOUT: leave sleep */
    delay_ms(120);
    const uint8_t colmod = 0x55;        /* 16 bit per pixel (RGB565) */
    write_cmd(0x3A);
    write_data(&colmod, 1);
    const uint8_t madctl = 0x00;        /* portrait, RGB order */
    write_cmd(0x36);
    write_data(&madctl, 1);
    write_cmd(0x21);                    /* INVON: these panels need inversion on */
    delay_ms(10);
    write_cmd(0x13);                    /* NORON: normal display mode */
    delay_ms(10);
    write_cmd(0x29);                    /* DISPON */
    delay_ms(50);

    s_ready = true;
    board_display_fill(0, 0, DISP_WIDTH, DISP_HEIGHT, 0x0000);
    return true;
}

void board_display_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels)
{
    if (!s_ready || w == 0u || h == 0u || (x + w) > DISP_WIDTH || (y + h) > DISP_HEIGHT) {
        return;
    }
    set_window(x, y, w, h);
    dc(true);
    cs(true);

    uint32_t remaining = (uint32_t)w * h;
    const uint16_t *src = pixels;
    while (remaining > 0u) {
        const uint16_t n = (remaining > CHUNK_PIXELS) ? (uint16_t)CHUNK_PIXELS : (uint16_t)remaining;
        for (uint16_t i = 0; i < n; ++i) {
            s_chunk[2u * i]      = (uint8_t)(src[i] >> 8);   /* ST7789 expects the high byte first */
            s_chunk[2u * i + 1u] = (uint8_t)src[i];
        }
        spi_write_dma(s_chunk, (uint16_t)(n * 2u));
        src += n;
        remaining -= n;
    }
    cs(false);
}

void board_display_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (!s_ready || w == 0u || h == 0u || (x + w) > DISP_WIDTH || (y + h) > DISP_HEIGHT) {
        return;
    }
    for (uint16_t i = 0; i < CHUNK_PIXELS; ++i) {
        s_chunk[2u * i]      = (uint8_t)(color >> 8);
        s_chunk[2u * i + 1u] = (uint8_t)color;
    }
    set_window(x, y, w, h);
    dc(true);
    cs(true);

    uint32_t remaining = (uint32_t)w * h;
    while (remaining > 0u) {
        const uint16_t n = (remaining > CHUNK_PIXELS) ? (uint16_t)CHUNK_PIXELS : (uint16_t)remaining;
        spi_write_dma(s_chunk, (uint16_t)(n * 2u));
        remaining -= n;
    }
    cs(false);
}
