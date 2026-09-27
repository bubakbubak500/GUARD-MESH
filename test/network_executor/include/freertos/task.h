#pragma once
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t fn, const char *name,
                                           unsigned stackDepth, void *parameter,
                                           unsigned priority, StackType_t *stack,
                                           StaticTask_t *tcb, int core);
void vTaskDelay(unsigned ticks);
