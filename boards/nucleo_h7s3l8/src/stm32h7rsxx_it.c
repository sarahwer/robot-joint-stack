/*
 * stm32h7rsxx_it.c - interrupt and fault handlers.
 *
 * SVC_Handler and PendSV_Handler come from the FreeRTOS port (see FreeRTOSConfig.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "stm32h7rsxx_hal.h"

FDCAN_HandleTypeDef *board_fdcan_handle(void);
void xPortSysTickHandler(void);

void SysTick_Handler(void);
void FDCAN1_IT0_IRQHandler(void);
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);
void hard_fault_report(const uint32_t *frame);

/* One SysTick drives both time bases: the HAL tick (always, also before the
 * scheduler starts) and the FreeRTOS tick (once the scheduler runs). */
void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

void FDCAN1_IT0_IRQHandler(void)
{
    HAL_FDCAN_IRQHandler(board_fdcan_handle());
}

/* ---- faults ------------------------------------------------------------------
 * The HardFault handler finds the stacked exception frame (MSP or PSP) and
 * prints PC, LR and the fault status registers: enough to locate the crashing
 * instruction with  arm-none-eabi-addr2line -e firmware.elf <PC>.            */

void hard_fault_report(const uint32_t *frame)
{
    char msg[112];
    (void)snprintf(msg, sizeof(msg), "HardFault PC=0x%08lx LR=0x%08lx CFSR=0x%08lx HFSR=0x%08lx",
                   (unsigned long)frame[6], (unsigned long)frame[5], (unsigned long)SCB->CFSR,
                   (unsigned long)SCB->HFSR);
    board_fatal(msg);
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "b hard_fault_report \n");
}

void NMI_Handler(void)        { board_fatal("NMI"); }
void MemManage_Handler(void)  { board_fatal("MemManage fault"); }
void BusFault_Handler(void)   { board_fatal("BusFault"); }
void UsageFault_Handler(void) { board_fatal("UsageFault"); }
void DebugMon_Handler(void)   {}
