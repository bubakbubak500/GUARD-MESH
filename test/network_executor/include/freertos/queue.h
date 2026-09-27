#pragma once
#include "FreeRTOS.h"
QueueHandle_t xQueueCreate(unsigned length, std::size_t itemSize);
int xQueueSend(QueueHandle_t queue, const void *item, unsigned waitTicks);
int xQueueReceive(QueueHandle_t queue, void *item, unsigned waitTicks);
