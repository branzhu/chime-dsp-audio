#include "sdkconfig.h"
#include "chime_s31_codec.h"
#include "oi_codec_sbc.h"
#include "oi_status.h"
#include "aacdec.h"
#include "latm_parser.h"
#if CONFIG_CHIME_BT_VENDOR_CODECS
#include "freeaptx.h"
#include "opus.h"
#include "ldacBT.h"
#endif
#if CONFIG_CHIME_BT_LHDC_V5
#include "lhdc_dec.h"
#include "lhdc_imdct.h"
#include "lhdc_entropy_dec.h"
#endif
#include <stdlib.h>
#include <string.h>

typedef struct {
    OI_CODEC_SBC_DECODER_CONTEXT context;
    OI_UINT32 data[CODEC_DATA_WORDS(2,SBC_CODEC_FAST_FILTER_BUFFERS)];
} sbc_state_t;
typedef struct { HAACDecoder decoder; latm_parser_t latm; uint8_t raw[4104]; } aac_state_t;
typedef struct {
    chime_s31_codec_t codec;
    unsigned rate,channels;
    void *state,*workspace;
    bool failed;
} codec_t;
/* LHDC has process-static caches; make the single-worker ownership explicit.
 * Not a cross-task API. Open/close/decode MUST all run on the decoder task. */
static codec_t *owner;
void chime_s31_close(void *handle)
{
    codec_t *s=handle;if(!s)return;
    if(s->codec==CHIME_S31_AAC){aac_state_t *a=s->state;if(a){if(a->decoder)AACFreeDecoder(a->decoder);free(a);}}
    else if(s->codec==CHIME_S31_SBC)free(s->state);
#if CONFIG_CHIME_BT_VENDOR_CODECS
    else if(s->codec==CHIME_S31_APTX||s->codec==CHIME_S31_APTX_HD){if(s->state)aptx_finish(s->state);}
    else if(s->codec==CHIME_S31_OPUS){if(s->state)opus_decoder_destroy(s->state);}
    else if(s->codec==CHIME_S31_LDAC){if(s->state){ldacBT_close_handle(s->state);ldacBT_free_handle(s->state);}}
#endif
#if CONFIG_CHIME_BT_LHDC_V5
    else if(s->codec==CHIME_S31_LHDC_V5){
        lhdc_imdct_free_480();lhdc_imdct_free_960();lhdc_imdct_free_1920();
        lhdc_imdct_free_cos();lhdc_dec_free_window();lhdc_entropy_free();free(s->workspace);
    }
#endif
    if(owner==s)owner=NULL;
    free(s);
}
void *chime_s31_open(chime_s31_codec_t codec,unsigned rate,unsigned channels)
{
    if(owner||channels<1||channels>2)return NULL;
    if(codec==CHIME_S31_SBC){if(rate!=16000&&rate!=32000&&rate!=44100&&rate!=48000)return NULL;}
    else if(codec==CHIME_S31_AAC){if(rate!=44100&&rate!=48000)return NULL;}
    else if(channels!=2||(rate!=44100&&rate!=48000))return NULL;
    if(codec==CHIME_S31_OPUS&&rate!=48000)return NULL;
    codec_t *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->codec=codec;s->rate=rate;s->channels=channels;owner=s;
    if(codec==CHIME_S31_SBC){
        sbc_state_t *b=calloc(1,sizeof(*b));s->state=b;
        if(!b||!OI_SUCCESS(OI_CODEC_SBC_DecoderReset(&b->context,b->data,sizeof(b->data),(OI_UINT8)channels,(OI_UINT8)channels,0,0)))goto fail;
    }else if(codec==CHIME_S31_AAC){
        aac_state_t *a=calloc(1,sizeof(*a));s->state=a;if(!a)goto fail;
        a->decoder=AACInitDecoder();if(!a->decoder)goto fail;
        AACFrameInfo info={0};info.nChans=(int)channels;info.sampRateCore=(int)rate;info.profile=AAC_PROFILE_LC;
        if(AACSetRawBlockParams(a->decoder,0,&info)!=0)goto fail;
        latm_parser_init(&a->latm);
    }
#if CONFIG_CHIME_BT_VENDOR_CODECS
    else if(codec==CHIME_S31_APTX||codec==CHIME_S31_APTX_HD)s->state=aptx_init(codec==CHIME_S31_APTX_HD);
    else if(codec==CHIME_S31_OPUS){int error;s->state=opus_decoder_create((opus_int32)rate,2,&error);if(error!=OPUS_OK)goto fail;}
    else if(codec==CHIME_S31_LDAC){
        s->state=ldacBT_get_handle();if(!s->state||ldacBT_init_handle_decode(s->state,LDACBT_CHANNEL_MODE_STEREO,(int)rate,0,0,0)!=0)goto fail;
    }
#endif
#if CONFIG_CHIME_BT_LHDC_V5
    else if(codec==CHIME_S31_LHDC_V5){
        lhdc_dec_config_t cfg={.sample_rate=(lhdc_dec_sample_rate_t)rate,.bit_depth=LHDC_DEC_BITDEPTH_16,
            .frame_duration=LHDC_DEC_FRAME_5MS,.channels=2,.max_frame_bytes=1538,.lossless_enable=0};
        size_t n=lhdc_dec_get_workspace_size(cfg.sample_rate,cfg.frame_duration);
        if(!n||(s->workspace=calloc(1,n))==NULL)goto fail;
        s->state=lhdc_dec_init(s->workspace,&cfg);
    }
#endif
    else goto fail;
    if(!s->state)goto fail;
    return s;
fail:chime_s31_close(s);return NULL;
}
static int decode(codec_t *s,chime_codec_input_t *in,chime_codec_output_t *out,chime_codec_info_t *info)
{
    *info=(chime_codec_info_t){s->rate,s->channels,16};
    if(s->codec==CHIME_S31_SBC){
        /* The IDF external callback already removed SBC's payload header.
         * Validate the full frame before allowing the OI sync-search decoder. */
        const uint8_t *p=in->buffer;if(in->len<4||p[0]!=0x9c)return CHIME_CODEC_ERROR;
        static const unsigned rates[]={16000,32000,44100,48000};
        unsigned mode=(p[1]>>2)&3,ch=mode?2:1,bands=(p[1]&1)?8:4,blocks=4*((p[1]>>4&3)+1),pool=p[2];
        unsigned bits=blocks*pool*(mode<2?ch:1)+(mode==3?bands:0);
        unsigned n=4+bands*ch/2+(bits+7)/8;
        if(rates[p[1]>>6]!=s->rate||ch!=s->channels||pool<2||pool>53||n>in->len||out->len<blocks*bands*ch*2)return CHIME_CODEC_ERROR;
        sbc_state_t *b=s->state;const OI_BYTE *next=p;OI_UINT32 left=n,bytes=out->len;
        if(!OI_SUCCESS(OI_CODEC_SBC_DecodeFrame(&b->context,&next,&left,(OI_INT16 *)out->buffer,&bytes))||left||bytes!=blocks*bands*ch*2)return CHIME_CODEC_ERROR;
        in->consumed=n;out->decoded_size=bytes;
    }else if(s->codec==CHIME_S31_AAC){
        aac_state_t *a=s->state;size_t n=0;
        if(out->len<1024*s->channels*2||in->len>4096||
           latm_parse_frame(&a->latm,in->buffer,in->len,a->raw,4096,&n,NULL)!=LATM_OK||!n)return CHIME_CODEC_ERROR;
        const latm_audio_config_t *c=latm_get_audio_config(&a->latm);
        if(!c||c->object_type!=2||c->sample_rate!=s->rate||c->channel_config!=s->channels||c->frame_length_flag||
           a->latm.num_sub_frames!=1||a->latm.num_program!=1||a->latm.num_layer!=1)return CHIME_CODEC_ERROR;
        memset(a->raw+n,0,8);unsigned char *p=a->raw;int left=(int)n;
        if(AACDecode(a->decoder,&p,&left,(short *)out->buffer)!=0||left!=0)return CHIME_CODEC_ERROR;
        AACFrameInfo f;AACGetLastFrameInfo(a->decoder,&f);
        if(f.nChans!=(int)s->channels||f.sampRateOut!=(int)s->rate||f.bitsPerSample!=16||f.outputSamps!=1024*(int)s->channels)return CHIME_CODEC_ERROR;
        in->consumed=in->len;out->decoded_size=(uint32_t)f.outputSamps*2;
    }
#if CONFIG_CHIME_BT_VENDOR_CODECS
    else if(s->codec==CHIME_S31_APTX||s->codec==CHIME_S31_APTX_HD){
        unsigned n=s->codec==CHIME_S31_APTX_HD?6:4;if(in->len%n||out->len<16)return CHIME_CODEC_ERROR;
        size_t bytes=0;in->consumed=(uint32_t)aptx_decode16(s->state,in->buffer,in->len,out->buffer,out->len,&bytes);out->decoded_size=(uint32_t)bytes;
    }else if(s->codec==CHIME_S31_OPUS){
        if(out->len<3840||in->len>1275||opus_packet_get_nb_samples(in->buffer,(opus_int32)in->len,48000)!=960)return CHIME_CODEC_ERROR;
        int n=opus_decode(s->state,in->buffer,(opus_int32)in->len,(opus_int16 *)out->buffer,960,0);
        if(n!=960)return CHIME_CODEC_ERROR;
        in->consumed=in->len;out->decoded_size=3840;
    }else if(s->codec==CHIME_S31_LDAC){
        if(in->len<4||in->len>515||out->len<512||in->buffer[0]!=0xaa||
           (in->buffer[1]>>5)!=(s->rate==48000?1:0)||((in->buffer[1]>>3)&3)!=2||
           (((unsigned)in->buffer[1]&7)*64+(in->buffer[2]>>2)+4)!=in->len)return CHIME_CODEC_ERROR;
        uint8_t padded[520]={0};memcpy(padded,in->buffer,in->len);
        int used=0,written=0;
        if(ldacBT_decode(s->state,padded,out->buffer,LDACBT_SMPL_FMT_S16,(int)in->len,&used,&written)!=0||used!=(int)in->len||written!=512)return CHIME_CODEC_ERROR;
        in->consumed=(uint32_t)used;out->decoded_size=(uint32_t)written;
    }
#endif
#if CONFIG_CHIME_BT_LHDC_V5
    else if(s->codec==CHIME_S31_LHDC_V5){
        if(in->len<20||in->len>1538||out->len<960)return CHIME_CODEC_ERROR;
        unsigned h=in->buffer[0]|((unsigned)in->buffer[1]<<8);
        if((h>>10)!=1||((h&1023)*2+2)!=in->len)return CHIME_CODEC_ERROR;
        size_t used=0;uint32_t frames=0;lhdc_dec_frame_info_t f;
        if(lhdc_dec_decode_frame(s->state,in->buffer,in->len,out->buffer,240,&used,&frames,&f)!=LHDC_DEC_OK||used!=in->len||frames!=240)return CHIME_CODEC_ERROR;
        in->consumed=(uint32_t)used;out->decoded_size=960;
    }
#endif
    else return CHIME_CODEC_ERROR;
    /* aptX legitimately consumes its initial predictor latency without PCM. */
    return in->consumed?CHIME_CODEC_OK:CHIME_CODEC_ERROR;
}
int chime_s31_decode(void *handle,chime_codec_input_t *in,chime_codec_output_t *out,chime_codec_info_t *info)
{
    codec_t *s=handle;
    if(!s||s!=owner||s->failed||!in||!out||!info||!in->buffer||!in->len||!out->buffer||((uintptr_t)out->buffer&1))return CHIME_CODEC_ERROR;
    in->consumed=0;out->decoded_size=0;
    int result=decode(s,in,out,info);
    if(result!=CHIME_CODEC_OK){s->failed=true;in->consumed=0;out->decoded_size=0;}
    return result;
}
