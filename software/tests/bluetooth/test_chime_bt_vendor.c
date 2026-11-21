#include "chime_bt_vendor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    uint8_t caps[14],config[14],bad[14];unsigned rate=0,channels=0;
    for(unsigned kind=3;kind<=6;++kind){
        assert(chime_vendor_capability(kind,caps)&&chime_vendor_codec(caps)==(int)kind);
        assert(chime_vendor_select(caps,config)&&chime_vendor_supports(caps,config)&&chime_vendor_format(config,&rate,&channels));assert(rate==48000&&channels==2);
        for(unsigned bits=0;bits<=255;++bits){
            memcpy(bad,config,14);bad[9]=(uint8_t)bits;
            bool expected=kind==5?bits==0x92:kind==6?bits==0x10||bits==0x20:bits==0x12||bits==0x22;
            assert(chime_vendor_format(bad,&rate,&channels)==expected);
            assert(chime_vendor_supports(caps,bad)==expected);
            if(kind==6){memcpy(bad,config,14);bad[10]=(uint8_t)bits;
                assert(chime_vendor_format(bad,&rate,&channels)==(bits==1));}
        }
        for(unsigned i=0;i<9;++i){memcpy(bad,config,14);bad[i]^=0x80;assert(!chime_vendor_format(bad,&rate,&channels));}
        if(kind==4){for(unsigned i=10;i<14;++i){memcpy(bad,config,14);bad[i]=1;assert(!chime_vendor_format(bad,&rate,&channels));}}
        if(kind==6){
            caps[9]=0xff;caps[10]=7;assert(chime_vendor_select(caps,config)&&config[9]==0x10&&config[10]==1);
            caps[9]=0x20;assert(chime_vendor_select(caps,config)&&config[9]==0x20);
            caps[10]=6;assert(!chime_vendor_select(caps,config));caps[10]=1;
            caps[9]=0x0f;assert(!chime_vendor_select(caps,config));
        }else if(kind!=5){caps[9]=0xff;assert(chime_vendor_select(caps,config)&&config[9]==0x12);}
    }
#if CONFIG_CHIME_BT_LHDC_V5
    assert(chime_vendor_capability(CHIME_VENDOR_LHDC_V5,caps));
    assert(chime_vendor_select(caps,config)&&config[10]==0xa4&&chime_vendor_supports(caps,config));
    assert(chime_vendor_codec(config)==7&&chime_vendor_format(config,&rate,&channels)&&rate==48000&&channels==2);
    for(unsigned field=0;field<14;++field)for(unsigned v=0;v<256;++v){
        memcpy(bad,config,14);bad[field]=(uint8_t)v;
        bool expected=field==9?(v==0x10||v==0x20):field==10?(v==0x94||v==0xa4||v==0xd4||v==0xe4):v==config[field];
        assert(chime_vendor_format(bad,&rate,&channels)==expected);
        assert(chime_vendor_supports(caps,bad)==expected);
    }
    for(unsigned v=0;v<256;++v){
        chime_vendor_capability(7,caps);caps[10]=(uint8_t)v;
        static const unsigned max[]={1000,400,500,900},min[]={64,160,256,400};
        unsigned lo=min[v>>6],hi=max[(v>>4)&3];if(lo<256)lo=256;if(hi>500)hi=500;
        bool ok=(v&4)&&lo<=hi;
        assert(chime_vendor_select(caps,config)==ok);
        if(ok)assert(chime_vendor_supports(caps,config));
    }
    chime_vendor_capability(7,caps);caps[9]=0x25;caps[10]=7;caps[11]=0x1f;caps[12]=0xff;caps[13]=0xff;
    assert(chime_vendor_select(caps,config)&&config[9]==0x20&&config[10]==0xa4&&config[11]==0x11&&!config[12]&&!config[13]);
    caps[9]=5;assert(!chime_vendor_select(caps,config));
    chime_vendor_capability(7,caps);caps[11]=0x10;assert(!chime_vendor_select(caps,config));
    chime_vendor_capability(7,caps);caps[11]=1;assert(!chime_vendor_select(caps,config));
#else
    uint8_t disabled[14]={13,0,255,0x3a,5,0,0,0x35,0x4c,0x10,0xa4,0x11,0,0};
    assert(!chime_vendor_capability(CHIME_VENDOR_LHDC_V5,caps)&&!chime_vendor_codec(disabled)&&!chime_vendor_format(disabled,&rate,&channels));
#endif
    assert(!chime_vendor_codec(NULL));assert(!chime_vendor_capability(2,caps));
    memset(caps,0,sizeof(caps));assert(!chime_vendor_select(caps,config));
    puts("PASS vendor CIE: aptX/HD, Android Opus, LDAC 44.1/48 stereo, exact configs, gated LHDC V5 bitrate/features and capability selection");return 0;
}
