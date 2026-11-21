#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Test-only introspection: compile the unmodified, pinned decoder here to read
 * its real SDK error code. The runner MUST omit its duplicate reference-4.o.
 * No test diagnostic accessor or changed vendor bytes enter the firmware. */
#ifdef _MSC_VER
/* The imported source's conservative uint64 size guard is provably false for
 * its validated small config. Keep the pinned bytes; scope the /W4 warning. */
#pragma warning(push)
#pragma warning(disable:4754)
#endif
#include "../../components/chime_codec_reference/vendor/native_bt/codecs/ldac/dbt_ldac_decoder.c"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

static unsigned allocations,releases;
static uint8_t *allocate(void *context,uint32_t bytes)
{
    (void)context;uint8_t *p=calloc(1,bytes);if(p)++allocations;return p;
}
static void release(void *context,uint8_t *bytes)
{
    (void)context;assert(bytes);++releases;free(bytes);
}
static dbt_result_t publish(void *context,const void *samples,uint16_t frames,
                           const dbt_codec_config_t *config,uint32_t timestamp)
{
    (void)timestamp;assert(samples&&frames==128&&config->sample_bytes==4&&config->channels==2);
    *(unsigned *)context+=frames;return DBT_OK;
}
static dbt_result_t submit(dbt_codec_decoder_t *d,const uint8_t *input,unsigned bytes)
{
    uint8_t payload[516]={1};assert(bytes<=515);memcpy(payload+1,input,bytes);
    dbt_avdtp_media_t media={.payload=payload,.payload_type=96,.size=(uint16_t)(bytes+1),.ssrc=1};
    dbt_result_t result=d->config.ops->media(d,&media,0);
    assert(payload[0]==1&&!memcmp(payload+1,input,bytes));return result;
}
static void check(const char *label,const uint8_t *input,int expected_block,bool warm)
{
    unsigned pcm=0;dbt_codec_decoder_t d={0};dbt_codec_config_t config={0};
    dbt_codec_hooks_t hooks={&pcm,allocate,release,publish};
    uint8_t cie[]={0,0xff,0x2d,1,0,0,0xaa,0,0x10,1};
    assert(dbt_codec_init(&d,hooks)==DBT_OK);
    assert(dbt_ldac_configure(cie,sizeof(cie),&config)==DBT_OK);config.ops=&dbt_ldac_codec;
    assert(dbt_codec_open(&d,&config)==DBT_OK);
    if(warm){
        /* Known encoder-produced valid history, NOT the missing phone history.
         * This checks that a generic cold-start transform state isn't enough
         * to explain these syntax rejections. */
        FILE *f=fopen("software/tests/bluetooth/fixtures/ldac/48000-2.ldac","rb");assert(f);
        for(unsigned i=0;i<4;++i){uint8_t frame[515];assert(fread(frame,1,3,f)==3);
            unsigned n=(((frame[1]&7u)<<6)|(frame[2]>>2))+4u;assert(n>=4&&n<=sizeof(frame));
            assert(fread(frame+3,1,n-3,f)==n-3&&submit(&d,frame,n)==DBT_OK);
        }
        fclose(f);assert(pcm==512);
    }
    unsigned before=pcm;dbt_ldac_port_overread=0;
    dbt_result_t result=submit(&d,input,164);
    ldac_state_t *state=d.state;int error=ldacBT_get_error_code(state->codec);
    if(expected_block==0){
        assert(result==DBT_OK&&pcm==before+128&&error==0);
    }else{
        assert(result==DBT_INVALID&&pcm==before);
        if(expected_block<0)assert(!error); /* media layout rejected before SDK decode */
        else assert(LDACBT_API_ERR(error)==LDACBT_ERR_FATAL&&
                    LDACBT_HANDLE_ERR(error)==LDACBT_ERR_UNPACK_BLOCK_FAILED&&
                    LDACBT_BLOCK_ERR(error)==expected_block);
    }
    assert(!dbt_ldac_port_overread);
    printf("LDAC phone %s warm=%u pcm_frames=%u error=%08x block=%d\n",
           label,(unsigned)warm,pcm-before,error,LDACBT_BLOCK_ERR(error));
    dbt_codec_close(&d);assert(!d.allocated_bytes&&allocations==releases);
}
int main(void)
{
    static const struct {const char *file;int blocks[5];} cases[]={
        {"48000-sync-corrupt.packet",{LDACBT_ERR_SYNTAX_SPEC,-1,LDACBT_ERR_SYNTAX_SPEC,LDACBT_ERR_SYNTAX_SPEC,0}},
        {"48000-frame2-header-corrupt-2.packet",{LDACBT_ERR_SYNTAX_SPEC,-1,0,0,LDACBT_ERR_SYNTAX_GRAD_D}}
    };
    for(unsigned sample=0;sample<sizeof(cases)/sizeof(cases[0]);++sample){
        char path[256];snprintf(path,sizeof(path),"software/tests/bluetooth/fixtures/ldac-phone-failure/%s",cases[sample].file);
        uint8_t original[657];FILE *f=fopen(path,"rb");assert(f);
        assert(fread(original,1,sizeof(original),f)==sizeof(original)&&fgetc(f)==EOF);fclose(f);
        for(unsigned variant=0;variant<5;++variant){
            uint8_t frame[164];unsigned slot=variant<4?variant:1;
            memcpy(frame,original+1+164*slot,sizeof(frame));
            if(variant==4)memcpy(frame,"\xaa\x32\x80",3); /* SYNTHETIC, never a product repair */
            char label[96];snprintf(label,sizeof(label),"sample%u %s slot%u",sample+1,variant==4?"synthetic-header":"original",slot+1);
            for(unsigned warm=0;warm<2;++warm)check(label,frame,cases[sample].blocks[variant],warm!=0);
        }
    }
    puts("PASS real LDAC phone frame isolation: exact syntax/gradient rejections, valid sample2 frames3/4, cold and valid warm-up controls, immutable caller bytes, no bit overread or leaks; not RF/phone-history certification");
    return 0;
}
