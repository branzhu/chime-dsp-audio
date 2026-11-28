#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_INTERNAL 1U
#define MALLOC_CAP_8BIT 2U
void *heap_caps_aligned_calloc(size_t alignment,size_t n,size_t size,uint32_t caps);
