/* Host-only wall clock for the upstream non-LHDC_HOST_BUILD path. Placement
 * attributes are stubbed; codec math, allocation hooks and window init are not. */
#include <stdint.h>
#include <time.h>
int64_t esp_timer_get_time(void){
    struct timespec ts;timespec_get(&ts,TIME_UTC);
    return (int64_t)ts.tv_sec*1000000+ts.tv_nsec/1000;
}
