#pragma once
/* Host compiler syntax shim only; no decoder arithmetic is replaced. */
#ifdef _MSC_VER
#define __attribute__(x)
#endif

#ifdef _MSC_VER
#define __restrict__ __restrict
#define __builtin_expect(value,expected) (value)
#endif

#ifdef _MSC_VER
#include <intrin.h>
static __inline int chime_host_clz(unsigned value){unsigned long bit;return _BitScanReverse(&bit,value)?31-(int)bit:32;}
#define __builtin_clz(value) chime_host_clz(value)
#endif
