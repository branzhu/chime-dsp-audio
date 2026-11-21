/* Test data generator only. Link with AOSP libldac encoder commit
 * e8ff0f96f26b84b47711c549e0d60baa425cd70e, Apache-2.0; not in firmware.
 * Input: synthetic, independently generated left=997Hz/6000, right=2003Hz/4000.
 * Arguments: output directory (default software/tests/bluetooth/fixtures/ldac). */
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "ldacBT.h"
int main(int argc,char **argv){
    const char *dir=argc>1?argv[1]:"software/tests/bluetooth/fixtures/ldac";
    for(int r=0;r<2;++r)for(int q=0;q<3;++q){
        int rate=r?48000:44100;char name[512];snprintf(name,sizeof(name),"%s/%d-%d.ldac",dir,rate,q);
        FILE *f=fopen(name,"wb");if(!f)return 1;
        HANDLE_LDAC_BT h=ldacBT_get_handle();
        if(!h||ldacBT_init_handle_encode(h,1000,q,LDACBT_CHANNEL_MODE_STEREO,LDACBT_SMPL_FMT_S16,rate))return 2;
        for(int block=0;block<72;++block){
            int16_t pcm[256];unsigned char raw[2048];int used=0,bytes=0,frames=0;
            for(int i=0;i<128;++i){double t=(double)(block*128+i)/rate;
                pcm[i*2]=(int16_t)(6000*sin(6.283185307179586*997*t));
                pcm[i*2+1]=(int16_t)(4000*sin(6.283185307179586*2003*t));}
            if(ldacBT_encode(h,pcm,&used,raw,&bytes,&frames)||used!=sizeof(pcm))return 3;
            if(bytes)fwrite(raw,1,(size_t)bytes,f);
        }
        for(int i=0;i<4;++i){unsigned char raw[2048];int used,bytes,frames;
            if(ldacBT_encode(h,NULL,&used,raw,&bytes,&frames))return 4;
            if(bytes)fwrite(raw,1,(size_t)bytes,f);}
        fclose(f);ldacBT_free_handle(h);printf("Generated %s\n",name);
    }
    return 0;
}
