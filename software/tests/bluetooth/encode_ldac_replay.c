/* Offline only. Sony/AOSP libldac e8ff0f96f26b84b47711c549e0d60baa425cd70e
 * (Apache-2.0), never linked into firmware. Encode continuous stereo PCM16,
 * retaining packet boundaries; no stream looping/restart seams. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ldacBT.h"
static void record(FILE *f,const unsigned char *raw,int bytes,int frames){
    if(!bytes)return;
    if(bytes<3||bytes>2047||frames<1||frames>15)exit(5);
    fputc(bytes&255,f);fputc(bytes>>8,f);fputc(frames,f);
    if(fwrite(raw,1,(size_t)bytes,f)!=(size_t)bytes)exit(6);
}
int main(int argc,char **argv){
    if(argc!=5){fputs("input.s16le rate output.packets MQ|SQ|HQ\n",stderr);return 1;}
    int rate=atoi(argv[2]);if(rate!=44100&&rate!=48000)return 1;
    int q=!strcmp(argv[4],"MQ")?LDACBT_EQMID_MQ:!strcmp(argv[4],"SQ")?LDACBT_EQMID_SQ:!strcmp(argv[4],"HQ")?LDACBT_EQMID_HQ:-1;
    if(q<0)return 1;
    FILE *in=fopen(argv[1],"rb"),*out=fopen(argv[3],"wb");if(!in||!out)return 1;
    HANDLE_LDAC_BT h=ldacBT_get_handle();
    if(!h||ldacBT_init_handle_encode(h,1000,q,LDACBT_CHANNEL_MODE_STEREO,LDACBT_SMPL_FMT_S16,rate))return 2;
    uint64_t samples=0;
    for(;;){
        int16_t pcm[256]={0};size_t n=fread(pcm,1,sizeof(pcm),in);if(!n)break;
        if(n%4||ferror(in))return 3;
        unsigned char raw[2048];int used=0,bytes=0,frames=0;
        if(ldacBT_encode(h,pcm,&used,raw,&bytes,&frames)||used!=sizeof(pcm))return 4;
        record(out,raw,bytes,frames);samples+=n/4;
        if(n<sizeof(pcm))break;
    }
    for(unsigned i=0;i<4;++i){unsigned char raw[2048];int used=0,bytes=0,frames=0;
        if(ldacBT_encode(h,NULL,&used,raw,&bytes,&frames))return 4;record(out,raw,bytes,frames);}
    ldacBT_free_handle(h);if(fclose(in)||fclose(out))return 6;
    printf("Encoded continuous stereo PCM16 %llu input frames at %d; decoder delay/flush retained, no looping\n",(unsigned long long)samples,rate);return 0;
}
