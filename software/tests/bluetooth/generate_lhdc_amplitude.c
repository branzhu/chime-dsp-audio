/* Offline sine fixture generation. Independent encoder; not a Savitech oracle. */
#include "lhdcv5_api.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
int main(int argc,char **argv){
    assert(argc==8);
    unsigned rate=atoi(argv[1]),bits=atoi(argv[2]),quality=atoi(argv[3]),nf=atoi(argv[5]);
    double amplitude=pow(10.0,atof(argv[4])/20.0);unsigned mem=0,spf=0,br=0;
    assert(!lhdcv5_encoder_get_mem_req(1,&mem));lhdcv5_para_t *e=calloc(1,mem);assert(e);
    assert(!lhdcv5_encoder_new(1,e,mem));assert(!lhdcv5_encoder_init(e,rate,bits,quality,50,1000,10));
    assert(!lhdcv5_encoder_get_frame_len(e,&spf));assert(!lhdcv5_encoder_get_target_bitrate(e,&br));
    FILE *encoded=fopen(argv[6],"wb"),*source=fopen(argv[7],"wb");assert(encoded&&source);
    uint8_t pcm[1920*2*4],out[32768];uint64_t nbytes=0;
    for(unsigned frame=0;frame<nf;frame++){
        for(unsigned i=0;i<spf;i++)for(unsigned ch=0;ch<2;ch++){
            /* Different phase makes channel swaps visible, same peak/RMS target. */
            double phase=6.283185307179586*1000.0*(frame*spf+i)/rate+(ch?0.37:0);
            int32_t v=(int32_t)llround(amplitude*ldexp(1.0,bits-1)*sin(phase));
            for(unsigned b=0;b<bits/8;b++)pcm[(i*2+ch)*(bits/8)+b]=(uint8_t)((uint32_t)v>>(8*b));
        }
        assert(fwrite(pcm,1,spf*2*(bits/8),source)==spf*2*(bits/8));
        uint32_t n=0,frames=0;assert(!lhdcv5_encoder_encode(e,pcm,spf*2*(bits/8),out,sizeof(out),&n,&frames));
        assert(fwrite(out,1,n,encoded)==n);nbytes+=n;
    }
    fclose(source);fclose(encoded);lhdcv5_encoder_free(e);free(e);
    printf("{\"spf\":%u,\"target_bitrate\":%u,\"encoded_bytes\":%llu}\n",spf,br,(unsigned long long)nbytes);
}
