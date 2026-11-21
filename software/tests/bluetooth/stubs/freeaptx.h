#pragma once
#include <stddef.h>
struct aptx_context;
struct aptx_context *aptx_init(int hd);
void aptx_finish(struct aptx_context *ctx);
size_t aptx_decode16(struct aptx_context *,const unsigned char *,size_t,unsigned char *,size_t,size_t *);
