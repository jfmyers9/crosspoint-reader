#pragma once

#include "FreeRTOS.h"

int xTaskCreate(void (*fn)(void*), const char*, unsigned, void*, unsigned, TaskHandle_t*);
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int, uint32_t);
void vTaskDelay(unsigned);
void vTaskDelete(TaskHandle_t);
void vTaskSuspend(TaskHandle_t);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t);
