/* Test-only AOSP LDAC encoder (same pinned source as generate_ldac_fixtures.c).
 * 800 blocks: 200 deterministic full-band noise, 200 silence, 200 changing
 * independent tones, 200 quiet noise. Seed persists across rate/quality loops.
 * No external recordings; output raw full frames, no RTP or payload header.
 * Argument: output directory (must exist). Not linked into firmware. */
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "ldacBT.h"
static unsigned rng=12345;
static int noise(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return (int)(rng&32767)-16384;}
int main(int argc,char **argv){
    const char *dir=argc>1?argv[1]:"software/tests/bluetooth/fixtures/ldac-stress";
    for(int r=0;r<2;++r)for(int q=0;q<3;++q){
        int rate=r?48000:44100;char name[512];snprintf(name,sizeof(name),"%s/%d-%d.ldac",dir,rate,q);
        FILE *f=fopen(name,"wb");if(!f)return 1;
        HANDLE_LDAC_BT h=ldacBT_get_handle();
        if(!h||ldacBT_init_handle_encode(h,1000,q,LDACBT_CHANNEL_MODE_STEREO,LDACBT_SMPL_FMT_S16,rate))return 2;
        for(int block=0;block<800;++block){
            int16_t pcm[256];unsigned char raw[2048];int used=0,bytes=0,frames=0;
            for(int i=0;i<128;++i){double t=(double)(block*128+i)/rate;
                pcm[i*2]=(int16_t)(block<200?noise():block<400?0:block<600?12000*sin(6.283185307179586*(100+block*29)*t):noise()/32);
                pcm[i*2+1]=(int16_t)(block<200?noise():block<400?0:block<600?10000*sin(6.283185307179586*(400+block*17)*t):noise()/16);}
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
