#include "chime_ldac.h"
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char raw[32768];
static int16_t pcm[256],decoded[32768];
static unsigned rnd=0x1dacc0de;
static unsigned random32(void){rnd^=rnd<<13;rnd^=rnd>>17;rnd^=rnd<<5;return rnd;}
static void vector(unsigned rate,unsigned quality){
    char name[160];snprintf(name,sizeof(name),"software/tests/bluetooth/fixtures/ldac/%u-%u.ldac",rate,quality);
    FILE *f=fopen(name,"rb");assert(f);size_t length=fread(raw,1,sizeof(raw),f);assert(feof(f));fclose(f);
    void *d=chime_ldac_open(rate,2);assert(d);size_t offset=0,samples=0;
    while(offset<length){size_t n=chime_ldac_frame_bytes(raw+offset,length-offset,rate);assert(n&&n<=515);
        assert(chime_ldac_decode(d,raw+offset,n,(uint8_t *)pcm,sizeof(pcm))==512);
        assert(samples+256<=sizeof(decoded)/sizeof(decoded[0]));memcpy(decoded+samples,pcm,sizeof(pcm));samples+=256;offset+=n;}
    chime_ldac_close(d);assert(samples>=8192&&samples%256==0);
    for(unsigned channel=0;channel<2;++channel){
        double energy=0,s=0,c=0,ss=0,cc=0,sc=0;unsigned count=0;double hz=channel?2003:997,expected=channel?4000:6000;
        for(size_t i=4096;i<samples/2;++i){double phase=6.283185307179586*hz*(double)i/rate,v=decoded[i*2+channel];
            double sv=sin(phase),cv=cos(phase);energy+=v*v;s+=v*sv;c+=v*cv;ss+=sv*sv;cc+=cv*cv;sc+=sv*cv;++count;}
        assert(count>0);double det=ss*cc-sc*sc,a=(s*cc-c*sc)/det,b=(c*ss-s*sc)/det;
        double amplitude=sqrt(a*a+b*b),coherence=(a*s+b*c)/energy;
        printf("LDAC rate=%u quality=%u channel=%u samples=%zu amplitude=%.2f coherence=%.5f\n",rate,quality,channel,samples/2,amplitude,coherence);
        assert(amplitude>expected*.9&&amplitude<expected*1.1&&coherence>.97);
    }
    /* Reset must reproduce identical PCM, including MDCT overlap state. */
    d=chime_ldac_open(rate,2);assert(d);offset=0;samples=0;
    while(offset<length){size_t n=chime_ldac_frame_bytes(raw+offset,length-offset,rate);
        assert(chime_ldac_decode(d,raw+offset,n,(uint8_t *)pcm,sizeof(pcm))==512);
        assert(!memcmp(decoded+samples,pcm,sizeof(pcm)));samples+=256;offset+=n;}
    chime_ldac_close(d);
    /* Every truncation of a real full frame must reject; wrong output capacity
     * and negotiated rate must reject without entering unbounded bit reads. */
    d=chime_ldac_open(rate,2);assert(d);size_t full=chime_ldac_frame_bytes(raw,length,rate);
    for(size_t n=0;n<full;++n)assert(!chime_ldac_decode(d,raw,n,(uint8_t *)pcm,sizeof(pcm)));
    assert(!chime_ldac_decode(d,raw,full,(uint8_t *)pcm,511));chime_ldac_close(d);
    assert(!chime_ldac_frame_bytes(raw,length,rate==48000?44100:48000));
}
/* Real encoded transients/full-band spectra exercise paths absent from
 * the narrow-band sine fixtures. Reopen must reproduce the entire PCM hash. */
static void stress_vector(unsigned rate,unsigned quality){
    char name[180];snprintf(name,sizeof(name),"software/tests/bluetooth/fixtures/ldac-stress/%u-%u.ldac",rate,quality);
    uint32_t reference=0;
    for(unsigned pass=0;pass<2;++pass){
        FILE *f=fopen(name,"rb");assert(f);void *d=chime_ldac_open(rate,2);assert(d);
        unsigned frames=0,zero_frames=0;uint32_t hash=2166136261U;
        while(fread(raw,1,3,f)==3){
            size_t bytes=chime_ldac_frame_bytes(raw,sizeof(raw),rate);assert(bytes>=4&&bytes<=515);
            assert(fread(raw+3,1,bytes-3,f)==bytes-3);
            assert(chime_ldac_decode(d,raw,bytes,(uint8_t *)pcm,sizeof(pcm))==512);
            bool silent=true;
            for(unsigned i=0;i<sizeof(pcm);++i){uint8_t v=((uint8_t *)pcm)[i];hash=(hash^v)*16777619U;if(v)silent=false;}
            if(silent)++zero_frames;
            ++frames;
        }
        printf("LDAC stress rate=%u quality=%u frames=%u zero_frames=%u\n",rate,quality,frames,zero_frames);fflush(stdout);
        assert(feof(f)&&!ferror(f)&&frames==801&&zero_frames>100&&zero_frames<300);
        fclose(f);chime_ldac_close(d);
        if(pass)assert(hash==reference);else reference=hash;
    }
    printf("PASS LDAC stress rate=%u quality=%u: 801 noise/silence/transient/quiet frames + reset identity\n",rate,quality);
}
int main(void){
    assert(!chime_ldac_open(96000,2)&&!chime_ldac_open(48000,1));chime_ldac_close(NULL);
    for(unsigned r=0;r<2;++r)for(unsigned q=0;q<3;++q){vector(r?48000:44100,q);stress_vector(r?48000:44100,q);}
    void *d=chime_ldac_open(48000,2);assert(d);
    /* 30k malformed/truncated/random frames, bounded reader + all table indexes.
     * ASan host run is separate; these are not substitutes for RF soak tests. */
    for(unsigned trial=0;trial<30000;++trial){size_t n=random32()%516;
        for(size_t i=0;i<n;++i)raw[i]=(unsigned char)random32();
        if(n>=4&&trial%2){raw[0]=0xaa;raw[1]=(uint8_t)(0x30|((n-4)>>6));raw[2]=(uint8_t)(((n-4)&63)<<2);}
        if(n>=3&&trial%7==0)raw[1]|=0xe0;
        size_t frame=chime_ldac_frame_bytes(raw,n,48000);
        size_t out=chime_ldac_decode(d,raw,n,(uint8_t *)pcm,sizeof(pcm));assert(!out||(frame==n&&out==512));
        if(!out){chime_ldac_close(d);d=chime_ldac_open(48000,2);assert(d);}
    }
    assert(!chime_ldac_decode(d,raw,3,(uint8_t *)pcm,sizeof(pcm)));
    assert(!chime_ldac_decode(d,raw,515,(uint8_t *)pcm,511));chime_ldac_close(d);
    puts("PASS real LDAC: Sony encoded HQ/SQ/MQ stereo tones at 44.1/48k, numerical energy/channel checks, reset identity, 30k malformed inputs");return 0;
}
