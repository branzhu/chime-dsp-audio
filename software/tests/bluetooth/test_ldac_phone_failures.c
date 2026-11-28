#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chime_ldac.h"
enum { PHONE_EXPECT_REJECT_SPEC=1,PHONE_EXPECT_REJECT_GRAD=2 };
static void check(const char *label,const uint8_t *input,int expected_block,bool warm)
{
    void *d=chime_ldac_open(48000,2);assert(d);int16_t output[256];
    if(warm){
        /* Known encoder-produced valid history, NOT the missing phone history.
         * This checks that a generic cold-start transform state isn't enough
         * to explain these syntax rejections. */
        FILE *f=fopen("software/tests/bluetooth/fixtures/ldac/48000-2.ldac","rb");assert(f);
        for(unsigned i=0;i<4;++i){uint8_t frame[515];assert(fread(frame,1,3,f)==3);
            unsigned n=(((frame[1]&7u)<<6)|(frame[2]>>2))+4u;assert(n>=4&&n<=sizeof(frame));
            assert(fread(frame+3,1,n-3,f)==n-3&&chime_ldac_decode(d,frame,n,(uint8_t *)output,sizeof(output))==512);
        }
        fclose(f);
    }
    uint8_t saved[164];memcpy(saved,input,sizeof(saved));
    size_t bytes=chime_ldac_decode(d,input,164,(uint8_t *)output,sizeof(output));
    assert(!memcmp(saved,input,sizeof(saved)));
    /* Only public success/rejection is stable across the backend replacement;
     * private retired dbt error-code packing is intentionally not asserted. */
    assert(bytes==(expected_block==0?512u:0u));
    printf("LDAC phone %s warm=%u emitted=%u\n",label,(unsigned)warm,(unsigned)bytes);
    chime_ldac_close(d);

}
int main(void)
{
    static const struct {const char *file;int blocks[5];} cases[]={
        {"48000-sync-corrupt.packet",{PHONE_EXPECT_REJECT_SPEC,-1,PHONE_EXPECT_REJECT_SPEC,PHONE_EXPECT_REJECT_SPEC,0}},
        {"48000-frame2-header-corrupt-2.packet",{PHONE_EXPECT_REJECT_SPEC,-1,0,0,PHONE_EXPECT_REJECT_GRAD}}
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
    puts("PASS real LDAC phone frame isolation: public decode rejection/success, valid sample2 frames3/4, cold and valid warm-up controls, immutable caller bytes; not RF/phone-history certification");
    return 0;
}
