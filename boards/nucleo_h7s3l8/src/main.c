/*
 * main.c - NUCLEO-H7S3L8 entry point.
 *
 * SPDX-License-Identifier: MIT
 */
#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "board.h"

void vApplicationStackOverflowHook(TaskHandle_t task, char *name);
void vApplicationMallocFailedHook(void);

int main(void)
{
    board_init();
    app_start();
    vTaskStartScheduler();
    board_fatal("scheduler returned");   /* only if the idle task could not be created */
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    board_fatal("stack overflow");
}

void vApplicationMallocFailedHook(void)
{
    board_fatal("FreeRTOS heap exhausted");
}
