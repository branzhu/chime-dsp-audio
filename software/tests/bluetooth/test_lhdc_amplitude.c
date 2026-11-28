/* Compile against each COMPLETE decoder tree; no gain overrides by default.
 * diag_* counters are inserted in a generated, test-only copy of lhdc_dec.c. */
#include "lhdc_dec.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
int g_pkframe;
int g_nzc_formula;
extern unsigned long long diag_concealed,diag_zero_filled[2],diag_preclip[2],diag_nonfinite[2],diag_channel_errors;
int main(int argc,char **argv){
    assert(argc==6);unsigned rate=atoi(argv[1]),bits=atoi(argv[2]);
#ifdef LHDC_GAIN_ABLATION
    extern unsigned diag_sample_rate;diag_sample_rate=rate;
#endif
    lhdc_dec_config_t cfg={.sample_rate=rate,.bit_depth=bits,.frame_duration=LHDC_DEC_FRAME_5MS,
        .channels=2,.max_frame_bytes=2048,.lossless_enable=0};
    void *ws=calloc(1,lhdc_dec_get_workspace_size(rate,LHDC_DEC_FRAME_5MS));assert(ws);
    lhdc_decoder_t *d=lhdc_dec_init(ws,&cfg);assert(d);
    FILE *f=fopen(argv[3],"rb"),*o=fopen(argv[4],"wb");assert(f&&o);
    uint8_t frame[4096];int32_t pcm[3840];unsigned total=0,rejected=0,outbits=0;
    while(fread(frame,1,2,f)==2){
        size_t n=(frame[0]|((unsigned)frame[1]<<8))&1023;n=n*2+2;assert(n<=sizeof(frame));
        assert(fread(frame+2,1,n-2,f)==n-2);size_t used=0;uint32_t generated=0;lhdc_dec_frame_info_t info={0};
        lhdc_dec_ret_t rc=lhdc_dec_decode_frame(d,frame,n,pcm,1920,&used,&generated,&info);
        if(rc!=LHDC_DEC_OK){rejected++;continue;}
        assert(used==n);outbits=info.bit_depth;assert(outbits==(bits==24?32:bits));
        assert(fwrite(pcm,1,generated*2*(outbits/8),o)==generated*2*(outbits/8));total++;
    }
    fclose(f);fclose(o);FILE *j=fopen(argv[5],"w");assert(j);
    fprintf(j,"{\"frames\":%u,\"rejected_frames\":%u,\"output_bits\":%u,\"concealed_frames\":%llu,\"zero_filled_samples\":[%llu,%llu],\"preclip_samples\":[%llu,%llu],\"nonfinite_samples\":[%llu,%llu],\"channel_errors\":%llu}\n",
        total,rejected,outbits,diag_concealed,diag_zero_filled[0],diag_zero_filled[1],diag_preclip[0],diag_preclip[1],diag_nonfinite[0],diag_nonfinite[1],diag_channel_errors);fclose(j);
#ifdef LHDC_STANDALONE
    lhdc_dec_deinit(d);
#endif
    free(ws);return rejected?2:0;
}

