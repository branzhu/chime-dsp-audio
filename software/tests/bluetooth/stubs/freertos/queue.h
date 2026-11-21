#pragma once
#include "freertos/FreeRTOS.h"
#include <stddef.h>
#define pdTRUE 1
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned length,unsigned item_size);
int xQueueSend(QueueHandle_t queue,const void *item,uint32_t wait);
int xQueueReceive(QueueHandle_t queue,void *item,uint32_t wait);
void vQueueDelete(QueueHandle_t queue);

unsigned uxQueueMessagesWaiting(QueueHandle_t queue);
