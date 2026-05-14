#pragma once

#include "freertos/FreeRTOS.h"

#define pdPASS 1
#define pdFALSE 0
#define tskNO_AFFINITY 0

typedef void *TaskHandle_t;

BaseType_t xTaskCreatePinnedToCore(
    void (*task_fn)(void *),
    const char *name,
    unsigned int stack_size,
    void *arg,
    unsigned int priority,
    TaskHandle_t *task_handle,
    int core_id
);

void vTaskDelay(unsigned int ticks);