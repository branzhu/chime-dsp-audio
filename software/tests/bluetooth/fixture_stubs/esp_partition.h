#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND 0x105
#endif
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_MMAP_DATA 0
typedef uint32_t esp_partition_mmap_handle_t;
typedef struct {uint32_t address,size;} esp_partition_t;
const esp_partition_t *esp_partition_find_first(unsigned type,unsigned subtype,const char *label);
esp_err_t esp_partition_read(const esp_partition_t *,size_t offset,void *data,size_t size);
esp_err_t esp_partition_mmap(const esp_partition_t *,size_t offset,size_t size,unsigned flags,const void **data,esp_partition_mmap_handle_t *handle);
void esp_partition_munmap(esp_partition_mmap_handle_t handle);
