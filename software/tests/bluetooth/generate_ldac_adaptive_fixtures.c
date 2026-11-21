/* Test-only Sony/AOSP libldac encoder e8ff0f96f26b84b47711c549e0d60baa425cd70e.
 * Apache-2.0 upstream encoder; never linked to firmware. Synthetic dual tone.
 * Records: u16 LE encoded bytes, u8 encoder-reported frame count, raw frames.
 * Preserve packet boundaries across live HQ/SQ/MQ/Q0/Q1 transitions. */
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "ldacBT.h"
static void record(FILE *f,const unsigned char *raw,int bytes,int frames){
    if(!bytes)return;
    if(bytes<3||bytes>4095||frames<1||frames>15)exit(5);
    fputc(bytes&255,f);fputc(bytes>>8,f);fputc(frames,f);
    if(fwrite(raw,1,(size_t)bytes,f)!=(size_t)bytes)exit(6);
}
int main(int argc,char **argv){
    if(argc!=2)return 1;
    char path[512];snprintf(path,sizeof(path),"%s/48000-abr.packets",argv[1]);
    FILE *f=fopen(path,"wb");if(!f)return 1;
    HANDLE_LDAC_BT h=ldacBT_get_handle();
    if(!h||ldacBT_init_handle_encode(h,1000,LDACBT_EQMID_HQ,LDACBT_CHANNEL_MODE_STEREO,LDACBT_SMPL_FMT_S16,48000))return 2;
    for(unsigned block=0;block<720;++block){
        if(block%24==0){
            unsigned profile=(block/24)%6;
            int q=profile==0?LDACBT_EQMID_HQ:profile==1||profile==4||profile==5?LDACBT_EQMID_SQ:LDACBT_EQMID_MQ;
            if(ldacBT_set_eqmid(h,q))return 3;
            if(profile>=3&&ldacBT_alter_eqmid_priority(h,profile==4?LDACBT_EQMID_INC_CONNECTION:LDACBT_EQMID_INC_QUALITY))return 3;
        }
        int16_t pcm[256];unsigned char raw[2048];int used=0,bytes=0,frames=0;
        for(unsigned i=0;i<128;++i){double t=(double)(block*128+i)/48000;
            pcm[2*i]=(int16_t)(6000*sin(6.283185307179586*997*t));
            pcm[2*i+1]=(int16_t)(4000*sin(6.283185307179586*2003*t));}
        if(ldacBT_encode(h,pcm,&used,raw,&bytes,&frames)||used!=sizeof(pcm))return 4;
        record(f,raw,bytes,frames);
    }
    for(unsigned i=0;i<4;++i){unsigned char raw[2048];int used=0,bytes=0,frames=0;
        if(ldacBT_encode(h,NULL,&used,raw,&bytes,&frames))return 4;
        record(f,raw,bytes,frames);}
    ldacBT_free_handle(h);if(fclose(f))return 6;
    puts("Generated independent LDAC adaptive packets at 48k (HQ/SQ/MQ/Q0/Q1)");return 0;
}
