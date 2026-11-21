/* Exercises the REAL, unmodified LGPL decoder, not the worker fakes.
 * Deterministic encode/decode + 24->16 conversion/chunk invariance; this is
 * not a Bluetooth interoperability or external golden-vector certification. */
#include "freeaptx.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define FRAMES 4096
static uint8_t pcm[FRAMES*6],encoded[FRAMES*2],pcm24[FRAMES*6+128],pcm16[FRAMES*4+128],chunked[FRAMES*4+128];
int main(void)
{
    for(unsigned i=0;i<FRAMES;++i){
        int32_t value=((int32_t)(i%128)-64)*65536;
        for(unsigned c=0;c<2;++c){uint32_t v=(uint32_t)(c?-value:value);
            for(unsigned b=0;b<3;++b)pcm[i*6+c*3+b]=(uint8_t)(v>>(8*b));}
    }
    for(int hd=0;hd<=1;++hd){
        struct aptx_context *e=aptx_init(hd),*d24=aptx_init(hd),*d16=aptx_init(hd),*dc=aptx_init(hd);
        assert(e&&d24&&d16&&dc);size_t n=0,tail=0;
        assert(aptx_encode(e,pcm,sizeof(pcm),encoded,sizeof(encoded),&n)==sizeof(pcm));
        assert(aptx_encode_finish(e,encoded+n,sizeof(encoded)-n,&tail));n+=tail;
        size_t a=0,b=0;assert(aptx_decode(d24,encoded,n,pcm24,sizeof(pcm24),&a)==n);
        assert(aptx_decode16(d16,encoded,n,pcm16,sizeof(pcm16),&b)==n);
        assert(a%6==0&&b==a/6*4&&b>=FRAMES*4);
        unsigned nonzero=0;
        for(size_t i=0;i<b/2;++i){assert(pcm16[i*2]==pcm24[i*3+1]&&pcm16[i*2+1]==pcm24[i*3+2]);nonzero+=pcm16[i*2+1]!=0;}
        assert(nonzero>FRAMES);
        size_t pos=0,out=0,block=hd?6U:4U;
        while(pos<n){size_t wanted=block*(1+(pos/block)%13);if(wanted>n-pos)wanted=n-pos;
            size_t w=0,consumed=aptx_decode16(dc,encoded+pos,wanted,chunked+out,48,&w);
            assert(consumed>0&&consumed<=wanted&&w<=48);pos+=consumed;out+=w;assert(out<=sizeof(chunked));}
        assert(out==b&&!memcmp(pcm16,chunked,b));
        aptx_reset(dc);size_t fresh=0;assert(aptx_decode16(dc,encoded,n,chunked,sizeof(chunked),&fresh)==n);
        assert(fresh==b&&!memcmp(pcm16,chunked,b));
        printf("PASS real freeaptx %s: %zu encoded bytes, %zu PCM16 frames; PCM24 truncation, small-buffer chunking and reset identical\n",hd?"HD":"classic",n,b/4);
        aptx_finish(e);aptx_finish(d24);aptx_finish(d16);aptx_finish(dc);
    }
    return 0;
}
