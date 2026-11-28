#include "sdkconfig.h"
#include "chime_ldac_fixture.h"
#include "chime_ldac_replay_format.h"
#include "chime_ldac.h"
#include "esp_partition.h"
#include "psa/crypto.h"
#include <string.h>
static chime_ldac_fixture_t saved;
static esp_partition_mmap_handle_t mapping;
static int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
esp_err_t chime_ldac_fixture_prepare(chime_ldac_fixture_t *out)
{
    if(!out)return ESP_ERR_INVALID_ARG;
    if(saved.payload){*out=saved;return ESP_OK;}
    const char *pin=CONFIG_CHIME_LDAC_REPLAY_FLASH_SHA256;
    if(strlen(pin)!=64)return ESP_ERR_INVALID_STATE;
    uint8_t expected[32];
    for(unsigned i=0;i<32;++i){int hi=hex(pin[i*2]),lo=hex(pin[i*2+1]);if(hi<0||lo<0)return ESP_ERR_INVALID_ARG;expected[i]=(uint8_t)(hi*16+lo);}
    const esp_partition_t *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"ldac_fixture");
    if(!part||part->address!=0x400000||part->size!=0x800000)return ESP_ERR_NOT_FOUND;
    uint8_t h[64];esp_err_t err=esp_partition_read(part,0,h,sizeof(h));if(err!=ESP_OK)return err;
    uint32_t rate=chime_replay_u32(h+8),frames=chime_replay_u32(h+12),packets=chime_replay_u32(h+16),length=chime_replay_u32(h+20);
    /* Only the qualified 48k/31s protocol is supported here, NOT arbitrary
     * looping of finite flushed streams or an assumed 44.1k equivalent. */
    if(memcmp(h,"LDACRP01",8)||rate!=48000||frames!=1488000||packets!=3875||
       length>part->size-64||length<packets*6||chime_replay_u32(h+24)!=1||
       chime_replay_u32(h+28)!=0||memcmp(h+32,expected,32))return ESP_ERR_INVALID_ARG;
    const void *mapped=NULL;
    err=esp_partition_mmap(part,0,length+64,ESP_PARTITION_MMAP_DATA,&mapped,&mapping);if(err!=ESP_OK)return err;
    const uint8_t *data=(const uint8_t *)mapped+64;uint8_t digest[32];size_t digest_length=0;
    if(psa_crypto_init()!=PSA_SUCCESS||psa_hash_compute(PSA_ALG_SHA_256,data,length,digest,sizeof(digest),&digest_length)!=PSA_SUCCESS||digest_length!=32||memcmp(digest,expected,32))goto invalid;
    size_t off=0;uint32_t count=0,samples=0;
    while(off<length){
        if(length-off<3)goto invalid;
        unsigned bytes=(unsigned)data[off]|((unsigned)data[off+1]<<8),nf=data[off+2];off+=3;
        if(nf!=3||bytes+1>CHIME_REPLAY_PACKET_MAX||bytes>length-off)goto invalid;
        size_t cursor=0;
        for(unsigned i=0;i<nf;++i){size_t n=chime_ldac_frame_bytes(data+off+cursor,bytes-cursor,rate);if(!n)goto invalid;cursor+=n;}
        if(cursor!=bytes)goto invalid;
        off+=bytes;++count;samples+=nf*128U;
    }
    if(count!=packets||samples!=frames)goto invalid;
    saved=(chime_ldac_fixture_t){.payload=data,.length=length,.rate=rate,.cycle_frames=frames,.packets=packets};*out=saved;return ESP_OK;
invalid:
    esp_partition_munmap(mapping);return ESP_ERR_INVALID_ARG;
}
