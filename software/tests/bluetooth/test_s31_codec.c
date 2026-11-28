#include "chime_s31_codec.h"
#include "chime_reference_codec.h"
#include "opus.h"
#include "freeaptx.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t packet[4096];
static int16_t pcm[2048],source[1920];
static void *open_codec(chime_s31_codec_t codec,unsigned rate,unsigned channels){
    void *d=chime_s31_open(codec,rate,channels);assert(d);
    for(int c=CHIME_S31_SBC;c<=CHIME_S31_LHDC_V5;c++)assert(!chime_s31_open((chime_s31_codec_t)c,48000,2));
    return d;
}
static unsigned decode_packet(void *d,unsigned length){
    chime_codec_input_t in={packet,length,0};chime_codec_output_t out={(uint8_t *)pcm,sizeof(pcm),0};chime_codec_info_t info;
    assert(chime_s31_decode(d,&in,&out,&info)==CHIME_CODEC_OK);
    assert(in.consumed&&in.consumed<=length&&out.decoded_size<=sizeof(pcm)&&info.bits_per_sample==16);
    return out.decoded_size;
}
static uint8_t crc_bit(uint8_t crc,unsigned b){return (uint8_t)((crc<<1)^(((crc>>7)^b)&1?0x1d:0));}
static void sbc(void){
    /* A complete SBC 48k/stereo/16-block/8-band frame. Construct its CRC
     * independently; unlike an RTOS mock this exercises real OI decoding. */
    static const unsigned rates[]={16000,32000,44100,48000};
    void *d;
    for(unsigned sf=0;sf<4;sf++)for(unsigned ch=1;ch<=2;ch++){
        unsigned n=4+8*ch/2+16*32/8;
        memset(packet,0,n);packet[0]=0x9c;packet[1]=(uint8_t)((sf<<6)|0x31|(ch==2?8:0));packet[2]=32;
        uint8_t crc=0x0f;
        for(unsigned byte=1;byte<=2;byte++)for(int b=7;b>=0;b--)crc=crc_bit(crc,(packet[byte]>>b)&1);
        for(unsigned bit=0;bit<32*ch;bit++)crc=crc_bit(crc,0);packet[3]=crc;
        d=open_codec(CHIME_S31_SBC,rates[sf],ch);assert(decode_packet(d,n)==128*ch*2);chime_s31_close(d);
    }
    d=open_codec(CHIME_S31_SBC,44100,2);
    chime_codec_input_t in={packet,76,0};chime_codec_output_t out={(uint8_t *)pcm,sizeof(pcm),0};chime_codec_info_t info;
    assert(chime_s31_decode(d,&in,&out,&info)!=0&&!in.consumed&&!out.decoded_size);chime_s31_close(d);
    puts("PASS S31 SBC real 16/32/44.1/48k mono/stereo CRC/frame decode and negotiated-format rejection");
}
static unsigned bitpos;
static void bits(unsigned value,unsigned count){for(unsigned i=count;i;i--){unsigned p=bitpos++;if((value>>(i-1))&1)packet[p/8]|=(uint8_t)(1u<<(7-p%8));}}
static void aac(void){
    /* AAC-LC stereo silence access unit, carried in MCP1 AudioMuxElement.
     * No ADTS or second RTP header; include a non-byte-aligned LATM payload. */
    static const uint8_t silence[]={0x21,0x10,0x04,0x60,0x8c,0x1c};
    memset(packet,0,sizeof(packet));bitpos=0;
    bits(0,1);bits(0,1);bits(1,1);bits(0,6);bits(0,4);bits(0,3);
    bits(2,5);bits(3,4);bits(2,4);bits(0,3); /* LC, 48k, stereo, 1024 */
    bits(0,3);bits(255,8);bits(0,1);bits(0,1);bits(sizeof(silence),8);
    for(unsigned i=0;i<sizeof(silence);i++)bits(silence[i],8);
    unsigned n=(bitpos+7)/8;void *d=open_codec(CHIME_S31_AAC,48000,2);
    assert(decode_packet(d,n)==4096);chime_s31_close(d);
    d=open_codec(CHIME_S31_AAC,48000,2);
    chime_codec_input_t in={packet,n-1,0};chime_codec_output_t out={(uint8_t *)pcm,sizeof(pcm),0};chime_codec_info_t info;
    assert(chime_s31_decode(d,&in,&out,&info)!=0&&!out.decoded_size);chime_s31_close(d);
    puts("PASS S31 Helix AAC real LATM silence decode and bit-truncation rejection");
}
static void opus(void){
    int error;OpusEncoder *e=opus_encoder_create(48000,2,OPUS_APPLICATION_AUDIO,&error);assert(e&&error==OPUS_OK);
    for(unsigned i=0;i<1920;i++)source[i]=(int16_t)((int)(i%100)*200-10000);
    int n=opus_encode(e,source,960,packet,1275);assert(n>0);
    void *d=open_codec(CHIME_S31_OPUS,48000,2);assert(decode_packet(d,(unsigned)n)==3840);
    chime_s31_close(d);opus_encoder_destroy(e);puts("PASS S31 Opus real fixed-point encode/decode (20ms)");
}
static void aptx(void){
    for(int hd=0;hd<2;hd++){
        uint8_t input[768];for(unsigned i=0;i<sizeof(input);i++)input[i]=(uint8_t)i;
        struct aptx_context *e=aptx_init(hd);assert(e);size_t written=0;
        assert(aptx_encode(e,input,sizeof(input),packet,sizeof(packet),&written)==sizeof(input));aptx_finish(e);
        void *d=open_codec(hd?CHIME_S31_APTX_HD:CHIME_S31_APTX,48000,2);
        assert(decode_packet(d,(unsigned)written)>0);chime_s31_close(d);
    }
    puts("PASS S31 aptX and aptX HD real encode/decode");
}
int main(void){
    assert(!chime_s31_open(CHIME_S31_SBC,12345,2));assert(!chime_s31_open(CHIME_S31_OPUS,44100,2));
    assert(!chime_s31_open(CHIME_S31_LDAC,96000,2));assert(!chime_s31_open(CHIME_S31_LHDC_V5,48000,1));
    chime_s31_close(NULL);chime_reference_quality_t q={.attempts=123};assert(!chime_reference_quality_snapshot(&q)&&q.attempts==123);
    sbc();aac();opus();aptx();puts("PASS S31 lifecycle/format/quality-unavailable boundaries");return 0;
}
