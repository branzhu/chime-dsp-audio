/* Actual generated SDK function, with only types/allocator/callbacks faked. */
#include "chime_bt_vendor.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef struct {UINT16 event,len,offset,layer_specific;} BT_HDR;
typedef struct {BT_HDR *p_pkt;} tAVDT_SCB_EVT;
typedef struct {struct {UINT8 codec_info[14];} curr_cfg;struct {void (*p_data_cback)(UINT8,BT_HDR *,UINT32,UINT8);} cs;} tAVDT_SCB;
static unsigned allocations,frees,delivered,last_len,last_offset,last_seq;
static UINT32 last_time;
static UINT8 last_payload[128];
static int fail_alloc;
static void *osi_malloc(size_t n){if(fail_alloc)return NULL;void *p=malloc(n);assert(p);++allocations;return p;}
static void osi_free(void *p){assert(p);++frees;free(p);}
static UINT8 avdt_scb_to_hdl(tAVDT_SCB *p){(void)p;return 1;}
#define AVDT_TRACE_WARNING(...) ((void)0)
#define TRUE 1
#define FALSE 0
#define AVDT_MULTIPLEXING FALSE
#define BE_STREAM_TO_UINT16(v,p) do{(v)=(UINT16)((p)[0]*256U+(p)[1]);(p)+=2;}while(0)
#define BE_STREAM_TO_UINT32(v,p) do{(v)=((UINT32)(p)[0]<<24)|((UINT32)(p)[1]<<16)|((UINT32)(p)[2]<<8)|(p)[3];(p)+=4;}while(0)
#define AVDT_MSG_PRS_OCTET1(p,v,pad,x,cc) do{UINT8 a=*(p)++;(v)=a>>6;(pad)=(a>>5)&1;(x)=(a>>4)&1;(cc)=a&15;}while(0)
#define AVDT_MSG_PRS_M_PT(p,pt,m) do{UINT8 a=*(p)++;(pt)=a&127;(m)=a>>7;}while(0)
#define UNUSED(v) ((void)(v))
#include "avdt-patched.inc"
static void receive(UINT8 h,BT_HDR *p,UINT32 time,UINT8 pt){
    (void)pt;assert(h==1&&p->len<sizeof(last_payload));++delivered;
    last_len=p->len;last_offset=p->offset;last_seq=p->layer_specific;last_time=time;
    memcpy(last_payload,(UINT8 *)(p+1)+p->offset,p->len);
    /* Same metadata overlap constraints as BTA then esp_a2d_audio_buff_t. */
    assert(sizeof(BT_HDR)+p->offset>=20);osi_free(p);
}
static void send(tAVDT_SCB *scb,const UINT8 *data,unsigned len){
    BT_HDR *p=osi_malloc(sizeof(*p)+8+len);assert(p);p->len=(UINT16)len;p->offset=8;p->layer_specific=99;
    memcpy((UINT8 *)(p+1)+8,data,len);tAVDT_SCB_EVT e={p};avdt_scb_hdl_pkt_no_frag(scb,&e);
}

/* Execute the actual incoming config and outgoing peer-selection functions.
 * They must distinguish four vendors even though all codec ids are 0xff. */
typedef unsigned char BOOLEAN;
#define BTC_AV_EXT_CODEC TRUE
#define BTC_AV_CODEC_AAC_INCLUDED TRUE
#define BTA_AV_MAX_SEPS 7
#define BTA_AV_CODEC_SBC 0
#define BTA_AV_CODEC_M24 2
#define BTA_AV_CODEC_M12 1
#define BTA_AV_CODEC_ATRAC 4
#define BTC_AV_CODEC_SBC BTA_AV_CODEC_SBC
#define BTC_AV_CODEC_M24 BTA_AV_CODEC_M24
#define A2D_SUCCESS 0
#define A2D_BAD_CODEC_TYPE 1
#define A2D_NS_CODEC_TYPE 2
#define FUNC_TRACE() ((void)0)
#define APPL_TRACE_ERROR(...) ((void)0)
#define APPL_TRACE_DEBUG(...) ((void)0)
typedef struct {UINT8 id,info[14];} codec_t;
static struct {codec_t codec_caps[7],codec_cfg;UINT8 cur_seid;} bta_av_co_cb;
typedef struct {UINT8 codec_type,codec_caps[14];} peer_src_t;
typedef struct {UINT8 num_sup_srcs;peer_src_t srcs[7];} tBTA_AV_CO_PEER;
static unsigned resets;
static void bta_av_co_audio_codec_reset(void){++resets;bta_av_co_cb.codec_cfg=bta_av_co_cb.codec_caps[bta_av_co_cb.cur_seid];}
static UINT8 bta_av_sbc_cfg_in_external_codec_cap(UINT8 *a,UINT8 *b){(void)a;(void)b;return A2D_NS_CODEC_TYPE;}
static UINT8 bta_av_m24_cfg_in_external_codec_cap(UINT8 *a,UINT8 *b){return bta_av_sbc_cfg_in_external_codec_cap(a,b);}
static UINT8 bta_av_m24_cap_matches_cap(UINT8 *a,UINT8 *b){return bta_av_sbc_cfg_in_external_codec_cap(a,b);}
#include "bta_av_co_audio_sink_supports_config.inc"
typedef struct {UINT8 unused;} tA2D_SBC_CIE;
typedef tA2D_SBC_CIE tA2D_M24_CIE;
static UINT8 A2D_ParsSbcInfo(tA2D_SBC_CIE *a,UINT8 *b,BOOLEAN caps){(void)a;(void)b;(void)caps;return A2D_NS_CODEC_TYPE;}
static UINT8 A2D_ParsM24Info(tA2D_M24_CIE *a,UINT8 *b,BOOLEAN caps){return A2D_ParsSbcInfo(a,b,caps);}
static UINT8 bta_av_sbc_cfg_matches_cap(UINT8 *a,tA2D_SBC_CIE *b){(void)a;(void)b;return A2D_NS_CODEC_TYPE;}
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4244 4702) /* Original IDF narrowing/unreachable tail; <= six SEPs. */
#endif
#include "bta_av_co_audio_peer_src_supports_codec.inc"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
static void test_config(void){
    for(unsigned i=0;i<7;++i){bta_av_co_cb.codec_caps[i].id=i<2?(UINT8)(i*2):255;
        if(i>=2)assert(chime_vendor_capability(i+1,bta_av_co_cb.codec_caps[i].info));}
    for(unsigned codec=3;codec<=7;++codec){
        UINT8 config[14];assert(chime_vendor_select(bta_av_co_cb.codec_caps[codec-1].info,config));
        unsigned prior=resets;assert(bta_av_co_audio_sink_supports_config(255,config)==A2D_SUCCESS);
        assert(resets==prior+1&&bta_av_co_cb.cur_seid==codec-1);
        config[9]=0;assert(bta_av_co_audio_sink_supports_config(255,config)!=A2D_SUCCESS&&resets==prior+1);
        tBTA_AV_CO_PEER peer={.num_sup_srcs=1};peer.srcs[0].codec_type=255;
        assert(chime_vendor_capability(codec,peer.srcs[0].codec_caps));
        bta_av_co_cb.cur_seid=0;UINT8 index=99;
        assert(bta_av_co_audio_peer_src_supports_codec(&peer,&index)&&index==0&&bta_av_co_cb.cur_seid==codec-1);
        peer.srcs[0].codec_caps[3]^=0x40;bta_av_co_cb.cur_seid=0;
        assert(!bta_av_co_audio_peer_src_supports_codec(&peer,&index));
    }
    puts("PASS actual SDK BTA config + peer selection: all vendor subtypes resolve matching SEP, malformed/unknown rejected");
}
int main(void){
    test_config();
    tAVDT_SCB scb={0};scb.cs.p_data_cback=receive;
    UINT8 raw[]={0x80,0x60,0x33,0x44,0x55,0x66,0x77,0x88,9,10,11,12,13,14,15,16};
    assert(chime_vendor_capability(CHIME_VENDOR_APTX,scb.curr_cfg.codec_info));
    send(&scb,raw,sizeof(raw));assert(delivered==1&&last_len==sizeof(raw)&&last_offset==32&&last_time==0&&!memcmp(raw,last_payload,sizeof(raw)));
    /* RTP with CSRC, extension, padding: only media data reaches callback. */
    UINT8 packet[48]={0xb1,0xe0,0x12,0x34,0,0,0,77};
    packet[16]=0xab;packet[17]=0xcd;packet[18]=0;packet[19]=1;
    memcpy(packet+24,raw,6);packet[31]=2;
    for(unsigned codec=0;codec<8;++codec){
        if(codec==3)continue;
        memset(scb.curr_cfg.codec_info,0,14);
        if(codec>=4)assert(chime_vendor_capability(codec,scb.curr_cfg.codec_info));
        else scb.curr_cfg.codec_info[2]=(UINT8)codec;
        unsigned before=delivered;send(&scb,packet,32);
        assert(delivered==before+1&&last_len==6&&!memcmp(last_payload,raw,6)&&last_seq==0x1234&&last_time==77);
    }
    unsigned before=delivered;send(&scb,packet,10);assert(delivered==before);
    packet[18]=0xff;packet[19]=0xff;send(&scb,packet,32);assert(delivered==before);
    assert(chime_vendor_capability(CHIME_VENDOR_APTX,scb.curr_cfg.codec_info));
    send(&scb,raw,0);assert(delivered==before);
    // Fail only the second (raw headroom) allocation, not fixture creation.
    BT_HDR *p=osi_malloc(sizeof(*p)+sizeof(raw));assert(p);p->len=sizeof(raw);p->offset=0;
    memcpy(p+1,raw,sizeof(raw));tAVDT_SCB_EVT e={p};fail_alloc=1;avdt_scb_hdl_pkt_no_frag(&scb,&e);fail_alloc=0;assert(!e.p_pkt&&delivered==before);
    scb.cs.p_data_cback=NULL;send(&scb,raw,sizeof(raw));assert(delivered==before);
    assert(allocations==frees);
    puts("PASS actual SDK AVDTP hook: raw aptX intact/headroom, SBC/AAC/HD/Opus/LDAC/LHDC V5 RTP+CSRC+extension+padding stripped ONCE, malformed/OOM ownership");
    return 0;
}
