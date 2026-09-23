/*
 * hal_can_fdcan.c - rjs/hal_can.h on the STM32H7RS FDCAN1 peripheral.
 *
 *   FDCAN1 TX = PD1, RX = PD0 (AF9), kernel clock = PLL2P (200 MHz)
 *
 * This FDCAN version has a fixed message RAM with only 3 RX FIFO elements and
 * 3 TX FIFO elements. The RX interrupt therefore drains FIFO 0 completely on
 * every call and hands each frame to the application callback; the TX path
 * reports "busy" instead of blocking when all 3 elements are in use.
 *
 * Bus-off recovery: the controller enters INIT on bus-off. The error-status
 * interrupt clears INIT again, which starts the standard recovery sequence
 * (128 x 11 recessive bits) in hardware.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "rjs/can_timing.h"
#include "rjs/hal_can.h"
#include "rjs/protocol.h"
#include "stm32h7rsxx_hal.h"

#define FDCAN_IRQ_PRIORITY 6u   /* must be >= configMAX_SYSCALL_INTERRUPT_PRIORITY (5) */

static FDCAN_HandleTypeDef s_fdcan;
static rjs_can_rx_cb_t     s_rx_cb;
static void               *s_rx_user;
static volatile uint32_t   s_tx_dropped;
static volatile uint32_t   s_rx_overrun;
static volatile uint32_t   s_bus_off_count;

FDCAN_HandleTypeDef *board_fdcan_handle(void);   /* used by the interrupt handler */
FDCAN_HandleTypeDef *board_fdcan_handle(void)
{
    return &s_fdcan;
}

void HAL_FDCAN_MspInit(FDCAN_HandleTypeDef *hfdcan)
{
    if (hfdcan->Instance != FDCAN1) {
        return;
    }
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    pclk.FdcanClockSelection  = RCC_FDCANCLKSOURCE_PLL2P;
    (void)HAL_RCCEx_PeriphCLKConfig(&pclk);
    __HAL_RCC_FDCAN_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_0 | GPIO_PIN_1;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF9_FDCAN1;
    HAL_GPIO_Init(GPIOD, &g);

    HAL_NVIC_SetPriority(FDCAN1_IT0_IRQn, FDCAN_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
}

rjs_can_result_t rjs_can_init(const rjs_can_config_t *cfg, rjs_can_rx_cb_t rx_cb, void *user)
{
    if (cfg == NULL || rx_cb == NULL) {
        return RJS_CAN_ERR_PARAM;
    }
    s_rx_cb   = rx_cb;
    s_rx_user = user;

    /* Select the kernel clock first so the bit timing uses the real frequency. */
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    pclk.FdcanClockSelection  = RCC_FDCANCLKSOURCE_PLL2P;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
        return RJS_CAN_ERR_HW;
    }
    const uint32_t clk = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN);

    static const rjs_can_timing_limits_t nominal_limits = {512u, 256u, 128u, 128u};
    static const rjs_can_timing_limits_t data_limits    = {32u, 32u, 16u, 16u};
    rjs_can_timing_t nt;
    rjs_can_timing_t dt;
    if (!rjs_can_calc_timing(clk, cfg->nominal_bitrate, 800u, &nominal_limits, &nt) ||
        !rjs_can_calc_timing(clk, cfg->data_bitrate, 800u, &data_limits, &dt)) {
        return RJS_CAN_ERR_PARAM;
    }

    memset(&s_fdcan, 0, sizeof(s_fdcan));
    s_fdcan.Instance                  = FDCAN1;
    s_fdcan.Init.ClockDivider         = FDCAN_CLOCK_DIV1;
    s_fdcan.Init.FrameFormat          = FDCAN_FRAME_FD_BRS;
    s_fdcan.Init.Mode                 = FDCAN_MODE_NORMAL;
    s_fdcan.Init.AutoRetransmission   = ENABLE;
    s_fdcan.Init.TransmitPause        = ENABLE;
    s_fdcan.Init.ProtocolException    = ENABLE;
    s_fdcan.Init.NominalPrescaler     = nt.prescaler;
    s_fdcan.Init.NominalSyncJumpWidth = nt.sjw;
    s_fdcan.Init.NominalTimeSeg1      = nt.tseg1;
    s_fdcan.Init.NominalTimeSeg2      = nt.tseg2;
    s_fdcan.Init.DataPrescaler        = dt.prescaler;
    s_fdcan.Init.DataSyncJumpWidth    = dt.sjw;
    s_fdcan.Init.DataTimeSeg1         = dt.tseg1;
    s_fdcan.Init.DataTimeSeg2         = dt.tseg2;
    s_fdcan.Init.StdFiltersNbr        = 0;
    s_fdcan.Init.ExtFiltersNbr        = 0;
    s_fdcan.Init.TxFifoQueueMode      = FDCAN_TX_FIFO_OPERATION;
    if (HAL_FDCAN_Init(&s_fdcan) != HAL_OK) {
        return RJS_CAN_ERR_HW;
    }

    /* No ID filters yet: accept every standard frame into FIFO 0, reject extended and remote. */
    if (HAL_FDCAN_ConfigGlobalFilter(&s_fdcan, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_REJECT,
                                     FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) != HAL_OK) {
        return RJS_CAN_ERR_HW;
    }

    /* Transmitter delay compensation is required for bit rate switching. */
    if (HAL_FDCAN_ConfigTxDelayCompensation(&s_fdcan, dt.prescaler * dt.tseg1, 0) != HAL_OK ||
        HAL_FDCAN_EnableTxDelayCompensation(&s_fdcan) != HAL_OK) {
        return RJS_CAN_ERR_HW;
    }

    const uint32_t its = FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST |
                         FDCAN_IT_BUS_OFF | FDCAN_IT_ERROR_PASSIVE;
    if (HAL_FDCAN_ActivateNotification(&s_fdcan, its, 0) != HAL_OK) {
        return RJS_CAN_ERR_HW;
    }
    if (HAL_FDCAN_Start(&s_fdcan) != HAL_OK) {
        return RJS_CAN_ERR_HW;
    }
    return RJS_CAN_OK;
}

rjs_can_result_t rjs_can_send(const rjs_can_frame_t *frame)
{
    if (frame == NULL || frame->id > 0x7FFu) {
        return RJS_CAN_ERR_PARAM;
    }
    const uint8_t dlc = rjs_fd_len_to_dlc(frame->len);
    if (dlc == 0xFFu || (!frame->fd && frame->len > 8u)) {
        return RJS_CAN_ERR_PARAM;
    }
    if (HAL_FDCAN_GetTxFifoFreeLevel(&s_fdcan) == 0u) {
        s_tx_dropped++;
        return RJS_CAN_ERR_BUSY;
    }

    FDCAN_TxHeaderTypeDef h = {0};
    h.Identifier          = frame->id;
    h.IdType              = FDCAN_STANDARD_ID;
    h.TxFrameType         = FDCAN_DATA_FRAME;
    h.DataLength          = dlc;      /* FDCAN_DLC_BYTES_x equal the DLC code on this HAL */
    h.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    h.BitRateSwitch       = (frame->fd && frame->brs) ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
    h.FDFormat            = frame->fd ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN;
    h.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    h.MessageMarker       = 0;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&s_fdcan, &h, frame->data) != HAL_OK) {
        s_tx_dropped++;
        return RJS_CAN_ERR_BUSY;
    }
    return RJS_CAN_OK;
}

void rjs_can_get_status(rjs_can_status_t *status)
{
    FDCAN_ErrorCountersTypeDef ec = {0};
    FDCAN_ProtocolStatusTypeDef ps = {0};
    (void)HAL_FDCAN_GetErrorCounters(&s_fdcan, &ec);
    (void)HAL_FDCAN_GetProtocolStatus(&s_fdcan, &ps);
    status->tx_error_count = (uint8_t)ec.TxErrorCnt;
    status->rx_error_count = (uint8_t)(ec.RxErrorCnt > 255u ? 255u : ec.RxErrorCnt);
    status->error_passive  = ps.ErrorPassive != 0u;
    status->bus_off        = ps.BusOff != 0u;
    status->tx_dropped     = s_tx_dropped;
    status->rx_overrun     = s_rx_overrun;
}

/* ---- interrupt callbacks (called from HAL_FDCAN_IRQHandler) ------------------ */

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) != 0u) {
        s_rx_overrun++;
    }
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) == 0u) {
        return;
    }
    FDCAN_RxHeaderTypeDef h;
    rjs_can_frame_t f;
    while (HAL_FDCAN_GetRxFifoFillLevel(hfdcan, FDCAN_RX_FIFO0) > 0u) {
        if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &h, f.data) != HAL_OK) {
            break;
        }
        const uint8_t len = rjs_fd_dlc_to_len((uint8_t)h.DataLength);
        f.id  = (uint16_t)h.Identifier;
        f.len = (len == 0xFFu) ? 0u : len;
        f.fd  = (h.FDFormat == FDCAN_FD_CAN);
        f.brs = (h.BitRateSwitch == FDCAN_BRS_ON);
        if (s_rx_cb != NULL) {
            s_rx_cb(&f, s_rx_user);
        }
    }
}

void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t ErrorStatusITs)
{
    if ((ErrorStatusITs & FDCAN_IT_BUS_OFF) != 0u) {
        FDCAN_ProtocolStatusTypeDef ps;
        if (HAL_FDCAN_GetProtocolStatus(hfdcan, &ps) == HAL_OK && ps.BusOff != 0u) {
            s_bus_off_count++;
            /* Leave INIT mode: hardware waits for 128 x 11 recessive bits, then rejoins. */
            CLEAR_BIT(hfdcan->Instance->CCCR, FDCAN_CCCR_INIT);
        }
    }
}
