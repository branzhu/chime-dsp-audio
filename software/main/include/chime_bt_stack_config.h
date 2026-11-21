#pragma once
#include "sdkconfig.h"
#if CONFIG_CHIME_BT_VENDOR_CODECS
/* Shared forced include for ALL bt/main units. Stock Kconfig caps SEPs at 3.
 * No edits to generated sdkconfig.h or SDK; internal and public limits agree. */
#undef CONFIG_BT_A2DP_SEP_NUM_MAX
#if CONFIG_CHIME_BT_LHDC_V5
#define CONFIG_BT_A2DP_SEP_NUM_MAX 7
#else
#define CONFIG_BT_A2DP_SEP_NUM_MAX 6
#endif
#define AVDT_CODEC_SIZE 14
#endif
