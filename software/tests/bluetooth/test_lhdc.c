#include "chime_lhdc.h"
#include "chime_reference_codec.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char raw[65536];
static int16_t pcm[480],decoded[48000];
static unsigned rnd=0x1dacc0de;
static unsigned random32(void){rnd^=rnd<<13;rnd^=rnd>>17;rnd^=rnd<<5;return rnd;}
static void vector(unsigned rate,unsigned quality){
    char name[160];snprintf(name,sizeof(name),"software/tests/bluetooth/fixtures/lhdc/%u-q%u.lhdc",rate,quality);
    FILE *f=fopen(name,"rb");assert(f);size_t length=fread(raw,1,sizeof(raw),f);assert(feof(f));fclose(f);
    void *d=chime_lhdc_open(rate,2);assert(d);size_t offset=0,samples=0;
#ifdef DBT_LHDC_QUALITY
    chime_reference_quality_t before,after;assert(chime_reference_quality_snapshot(&before));
#endif
    while(offset<length){size_t n=chime_lhdc_frame_bytes(raw+offset,length-offset);assert(n&&n<=1538);
        assert(chime_lhdc_decode(d,raw+offset,n,(uint8_t *)pcm,sizeof(pcm))==960);
        assert(samples+480<=sizeof(decoded)/sizeof(decoded[0]));memcpy(decoded+samples,pcm,sizeof(pcm));samples+=480;offset+=n;}
#ifdef DBT_LHDC_QUALITY
    assert(chime_reference_quality_snapshot(&after));
    assert(after.attempts-before.attempts==samples/480&&after.rejected==before.rejected);
    assert(after.channel0_errors==before.channel0_errors&&after.channel1_silenced==before.channel1_silenced);
    assert(after.peak_concealed==before.peak_concealed&&after.nonfinite_frames==before.nonfinite_frames);
#endif
    chime_lhdc_close(d);assert(samples>=8192&&samples%480==0);
    for(unsigned channel=0;channel<2;++channel){
        double energy=0,s=0,c=0,ss=0,cc=0,sc=0;unsigned count=0;double hz=channel?2003:997,expected=channel?4000:6000;
        for(size_t i=4096;i<samples/2;++i){double phase=6.283185307179586*hz*(double)i/rate,v=decoded[i*2+channel];
            double sv=sin(phase),cv=cos(phase);energy+=v*v;s+=v*sv;c+=v*cv;ss+=sv*sv;cc+=cv*cv;sc+=sv*cv;++count;}
        assert(count>0);double det=ss*cc-sc*sc,a=(s*cc-c*sc)/det,b=(c*ss-s*sc)/det;
        double amplitude=sqrt(a*a+b*b),coherence=(a*s+b*c)/energy;
        printf("LHDC V5 rate=%u quality=%u channel=%u samples=%zu amplitude=%.2f coherence=%.5f\n",rate,quality,channel,samples/2,amplitude,coherence);
        fflush(stdout);
        assert(amplitude>expected*.9&&amplitude<expected*1.1&&coherence>.99);
    }
    /* Reset must reproduce identical PCM, including MDCT overlap state. */
    d=chime_lhdc_open(rate,2);assert(d);offset=0;samples=0;
    while(offset<length){size_t n=chime_lhdc_frame_bytes(raw+offset,length-offset);
        assert(chime_lhdc_decode(d,raw+offset,n,(uint8_t *)pcm,sizeof(pcm))==960);
        assert(!memcmp(decoded+samples,pcm,sizeof(pcm)));samples+=480;offset+=n;}
    chime_lhdc_close(d);
    /* Every truncation of a real full frame must reject; wrong output capacity
     * must reject without entering unbounded bit reads. */
    d=chime_lhdc_open(rate,2);assert(d);size_t full=chime_lhdc_frame_bytes(raw,length);
    for(size_t n=0;n<full;++n)assert(!chime_lhdc_decode(d,raw,n,(uint8_t *)pcm,sizeof(pcm)));
    assert(!chime_lhdc_decode(d,raw,full,(uint8_t *)pcm,959));chime_lhdc_close(d);

}
int main(void){
    assert(!chime_lhdc_open(96000,2)&&!chime_lhdc_open(48000,1));chime_lhdc_close(NULL);
    for(unsigned r=0;r<2;++r)for(unsigned q=3;q<=8;++q)vector(r?48000:44100,q);
    void *d=chime_lhdc_open(48000,2);assert(d);
    /* Random data with valid frame envelopes enters the actual entropy/IMDCT path.
     * No playback-quality claim: these checks bound memory/control flow only. */
    for(unsigned trial=0;trial<10000;++trial){size_t n=(random32()%760+9)*2+2;
        for(size_t i=0;i<n;++i)raw[i]=(unsigned char)random32();
        unsigned h=(unsigned)((n-2)/2)|0x400;raw[0]=(uint8_t)h;raw[1]=(uint8_t)(h>>8);
        size_t out=chime_lhdc_decode(d,raw,n,(uint8_t *)pcm,sizeof(pcm));assert(!out||out==960);
        if(trial%13==0)assert(!chime_lhdc_decode(d,raw,n-1,(uint8_t *)pcm,sizeof(pcm)));
        if(!out){chime_lhdc_close(d);d=chime_lhdc_open(48000,2);assert(d);}
    }
    assert(!chime_lhdc_decode(d,raw,3,(uint8_t *)pcm,sizeof(pcm)));
    chime_lhdc_close(d);
    puts("PASS experimental real LHDC V5: independent C encoder tones, 44.1/48k stereo, unity-level synthetic tones (NOT certification), reset identity, 10k malformed frames");return 0;
}
