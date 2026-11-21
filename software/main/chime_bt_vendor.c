#include "chime_bt_vendor.h"
#include <string.h>
#if CONFIG_CHIME_BT_LHDC_V5
/* LHDC bitrate fields are enumerations, NOT capability bitmaps. */
static unsigned lhdc_max(unsigned b){static const unsigned n[]={1000,400,500,900};return n[(b>>4)&3];}
static unsigned lhdc_min(unsigned b){static const unsigned n[]={64,160,256,400};return n[b>>6];}
#endif
static bool one(unsigned v){return v&&!(v&(v-1));}
int chime_vendor_codec(const uint8_t *w)
{
    if(!w||w[1]||w[2]!=0xff)return 0;
    static const uint8_t aptx[]={0x4f,0,0,0,1,0};
    static const uint8_t hd[]={0xd7,0,0,0,0x24,0};
    static const uint8_t opus[]={0xe0,0,0,0,1,0};
    static const uint8_t ldac[]={0x2d,1,0,0,0xaa,0};
#if CONFIG_CHIME_BT_LHDC_V5
    static const uint8_t lhdc[]={0x3a,5,0,0,0x35,0x4c};
    if(w[0]==13&&!memcmp(w+3,lhdc,6))return CHIME_VENDOR_LHDC_V5;
#endif
    if(w[0]==10&&!memcmp(w+3,ldac,6))return CHIME_VENDOR_LDAC;
    if(w[0]==9&&!memcmp(w+3,aptx,6))return CHIME_VENDOR_APTX;
    if(w[0]==13&&!memcmp(w+3,hd,6)&&!(w[10]|w[11]|w[12]|w[13]))return CHIME_VENDOR_APTX_HD;
    if(w[0]==9&&!memcmp(w+3,opus,6))return CHIME_VENDOR_OPUS;
    return 0;
}
bool chime_vendor_capability(unsigned codec,uint8_t *w)
{
    memset(w,0,CHIME_VENDOR_WIRE_MAX);w[2]=0xff;w[0]=9;
    if(codec==CHIME_VENDOR_APTX){w[3]=0x4f;w[7]=1;w[9]=0x32;}
    else if(codec==CHIME_VENDOR_APTX_HD){w[0]=13;w[3]=0xd7;w[7]=0x24;w[9]=0x32;}
    else if(codec==CHIME_VENDOR_OPUS){w[3]=0xe0;w[7]=1;w[9]=0x92;}
    else if(codec==CHIME_VENDOR_LDAC){w[0]=10;w[3]=0x2d;w[4]=1;w[7]=0xaa;w[9]=0x30;w[10]=1;}
#if CONFIG_CHIME_BT_LHDC_V5
    else if(codec==CHIME_VENDOR_LHDC_V5){
        w[0]=13;w[3]=0x3a;w[4]=5;w[7]=0x35;w[8]=0x4c;
        w[9]=0x30;w[10]=0xa4;w[11]=0x11; /* PCM16, 256..500 kbps, 5ms, v1 */
    }
#endif
    else return false;
    return true;
}
bool chime_vendor_format(const uint8_t *w,unsigned *rate,unsigned *channels)
{
    int codec=chime_vendor_codec(w);if(!codec)return false;
    if(codec==CHIME_VENDOR_OPUS){if(w[9]!=0x92)return false;*rate=48000;*channels=2;return true;}
#if CONFIG_CHIME_BT_LHDC_V5
    if(codec==CHIME_VENDOR_LHDC_V5){
        if((w[9]!=0x10&&w[9]!=0x20)||(w[10]&15)!=4||w[11]!=0x11||w[12]||w[13]||
           lhdc_min(w[10])<256||lhdc_max(w[10])>500||lhdc_min(w[10])>lhdc_max(w[10]))return false;
        *rate=w[9]==0x10?48000:44100;*channels=2;return true;
    }
#endif
    if(codec==CHIME_VENDOR_LDAC){
        if((w[9]!=0x10&&w[9]!=0x20)||w[10]!=1)return false;
        *rate=w[9]==0x10?48000:44100;*channels=2;return true;
    }
    unsigned freq=w[9]&0xf0,mode=w[9]&0x0f;
    if(!one(freq)||(freq&~0x30U)||mode!=2)return false;
    *rate=freq==0x10?48000:44100;*channels=2;return true;
}
bool chime_vendor_supports(const uint8_t *caps,const uint8_t *config)
{
    unsigned r,c;int codec=chime_vendor_codec(caps);
    if(!codec||codec!=chime_vendor_codec(config)||!chime_vendor_format(config,&r,&c))return false;
    uint8_t local[CHIME_VENDOR_WIRE_MAX];chime_vendor_capability((unsigned)codec,local);
#if CONFIG_CHIME_BT_LHDC_V5
    if(codec==CHIME_VENDOR_LHDC_V5)return !(config[9]&~caps[9])&&(caps[10]&4)&&
        (caps[11]&0x11)==0x11&&lhdc_min(config[10])>=lhdc_min(caps[10])&&lhdc_max(config[10])<=lhdc_max(caps[10]);
#endif
    if(codec==CHIME_VENDOR_OPUS)return (caps[9]&0x92)==0x92&&config[9]==0x92;
    if(codec==CHIME_VENDOR_LDAC)return !(config[9]&~caps[9])&&(caps[10]&1);
    return !(config[9]&~caps[9])&&!(config[9]&~local[9]);
}
bool chime_vendor_select(const uint8_t *caps,uint8_t *out)
{
    int codec=chime_vendor_codec(caps);if(!codec)return false;
    uint8_t local[CHIME_VENDOR_WIRE_MAX];chime_vendor_capability((unsigned)codec,local);
    uint8_t bits=caps[9]&local[9];
#if CONFIG_CHIME_BT_LHDC_V5
    if(codec==CHIME_VENDOR_LHDC_V5){
        if(!bits||!(caps[10]&4)||(caps[11]&0x11)!=0x11)return false;
        unsigned lo=lhdc_min(caps[10]);if(lo<256)lo=256;
        unsigned hi=lhdc_max(caps[10]);if(hi>500)hi=500;
        if(lo>hi)return false;
        memcpy(out,local,CHIME_VENDOR_WIRE_MAX);out[9]=(uint8_t)(bits&0x10?0x10:0x20);
        out[10]=(uint8_t)(4|(lo==400?0xc0:0x80)|(hi==400?0x10:0x20));return true;
    }
#endif
    if(codec==CHIME_VENDOR_LDAC){
        if(!(caps[10]&1)||!bits)return false;
        memcpy(out,local,CHIME_VENDOR_WIRE_MAX);out[9]=(uint8_t)(bits&0x10?0x10:0x20);return true;
    }
    if(codec==CHIME_VENDOR_OPUS){if(bits!=0x92)return false;}
    else {if(!(bits&2)||!(bits&0x30))return false;bits=(uint8_t)((bits&0x10?0x10:0x20)|2);}
    memcpy(out,local,CHIME_VENDOR_WIRE_MAX);out[9]=bits;return true;
}
