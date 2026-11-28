#include "chime_bt_ready.h"
#include <assert.h>
#include <stdio.h>
static chime_bt_ready_t q;
static int32_t in[256],out[256];
static void fill(int32_t value){for(unsigned i=0;i<256;++i)in[i]=value+(int32_t)i;}
int main(void)
{
    chime_bt_ready_reset(&q,1,1);fill(10);
    assert(!chime_bt_ready_pop(&q,1,1,out)&&!q.underruns);
    assert(chime_bt_ready_push(&q,1,1,in));
    assert(!chime_bt_ready_pop(&q,1,1,out)&&q.count==1&&!q.underruns);
    for(unsigned j=1;j<4;++j){fill((int32_t)(j*10+10));assert(chime_bt_ready_push(&q,1,1,in));}
    assert(!chime_bt_ready_push(&q,1,1,in)&&q.count==4&&q.high_water==4);
    for(unsigned j=0;j<4;++j){assert(chime_bt_ready_pop(&q,1,1,out));for(unsigned i=0;i<256;++i)assert(out[i]==(int32_t)((j+1)*10+i));}
    assert(!chime_bt_ready_pop(&q,1,1,out)&&q.underruns==1&&!q.started);
    for(unsigned i=0;i<256;++i)assert(!out[i]);
    /* Old computation cannot publish after STOP or an output-only restart. */
    chime_bt_ready_reset(&q,1,2);assert(!chime_bt_ready_push(&q,1,1,in));
    assert(!chime_bt_ready_pop(&q,1,1,out));assert(!q.produced&&!q.consumed);
    chime_bt_ready_reset(&q,2,3);assert(!chime_bt_ready_push(&q,1,2,in));
    /* Ring wrap and exact FIFO over many bounded producer/consumer schedules. */
    for(unsigned j=0;j<10000;++j){fill((int32_t)j);assert(chime_bt_ready_push(&q,2,3,in));
        if(j){assert(chime_bt_ready_pop(&q,2,3,out));for(unsigned i=0;i<256;++i)assert(out[i]==(int32_t)(j-1+i));}}
    assert(q.produced==10000&&q.consumed==9999&&q.count==1&&!q.underruns);
    assert(!chime_bt_ready_push(&q,0,3,in));assert(!chime_bt_ready_push(&q,2,0,in));
    puts("ready bridge: FIFO, bounded capacity, prefill, underrun, generation cancellation PASS");
}
