#pragma once
#include <stdbool.h>
/* Physical output masks, not USB input channel selection. */
#define BENCH_OUTPUT_L 1U
#define BENCH_OUTPUT_R 2U
#define BENCH_OUTPUT_SUB 4U
#define BENCH_OUTPUT_DEFAULT 7U
static inline bool bench_output_valid(unsigned mask)
{
    return mask==1U||mask==2U||mask==3U||mask==4U||mask==7U;
}
