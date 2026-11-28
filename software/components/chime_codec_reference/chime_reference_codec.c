#include "chime_reference_codec.h"
#include "chime_s31_codec.h"
#include <stdlib.h>
#include <string.h>
/* Preserve the byte-buffer contract of existing LDAC/LHDC frontends, including
 * short-capacity rejection without poisoning state and unaligned PCM buffers. */
typedef struct {void *codec;unsigned pcm_bytes,max_encoded;} reference_t;
void *chime_reference_open(int lhdc,unsigned rate,unsigned channels){
    reference_t *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->codec=chime_s31_open(lhdc?CHIME_S31_LHDC_V5:CHIME_S31_LDAC,rate,channels);
    if(!s->codec){free(s);return NULL;}
    s->pcm_bytes=lhdc?960:512;s->max_encoded=lhdc?1538:515;return s;
}
void chime_reference_close(void *handle){
    reference_t *s=handle;if(s){chime_s31_close(s->codec);free(s);}
}
static size_t decode_aligned(reference_t *s,const uint8_t *data,size_t length,uint8_t *pcm,size_t capacity){
    chime_codec_input_t in={.buffer=(uint8_t *)data,.len=(uint32_t)length};
    chime_codec_output_t out={.buffer=pcm,.len=(uint32_t)capacity};chime_codec_info_t info;
    return chime_s31_decode(s->codec,&in,&out,&info)==CHIME_CODEC_OK&&in.consumed==length?out.decoded_size:0;
}
#ifdef _MSC_VER
__declspec(noinline)
#else
__attribute__((noinline))
#endif
static size_t decode_unaligned(reference_t *s,const uint8_t *data,size_t length,uint8_t *pcm){
    int16_t scratch[480];size_t n=decode_aligned(s,data,length,(uint8_t *)scratch,s->pcm_bytes);
    if(n)memcpy(pcm,scratch,n);
    return n;
}
size_t chime_reference_decode(void *handle,const uint8_t *data,size_t length,uint8_t *pcm,size_t capacity){
    reference_t *s=handle;
    if(!s||!data||!pcm||!length||length>s->max_encoded||capacity<s->pcm_bytes)return 0;
    if((uintptr_t)pcm&1)return decode_unaligned(s,data,length,pcm);
    return decode_aligned(s,data,length,pcm,s->pcm_bytes);
}
bool chime_reference_quality_snapshot(chime_reference_quality_t *out){
    /* S31 does not export the retired native-port channel collector. */
    (void)out;return false;
}
