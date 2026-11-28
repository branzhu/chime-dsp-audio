/* Actual mmap/validation adapter with fake storage/crypto. Exercises rejection
 * and ownership only; real SHA and actual LDAC seam proof are separate gates. */
#include "../../main/chime_ldac_fixture.c"
#include <assert.h>
#include <stdio.h>
static uint8_t image[64+3875*15];
static esp_partition_t partition={0x400000,0x800000};
static bool missing,bad_hash,read_fail,map_fail;static unsigned mapped,unmapped,hashes;
const esp_partition_t *esp_partition_find_first(unsigned type,unsigned subtype,const char *label){assert(type==1&&subtype==0x40&&!strcmp(label,"ldac_fixture"));return missing?NULL:&partition;}
esp_err_t esp_partition_read(const esp_partition_t *p,size_t offset,void *data,size_t size){assert(p==&partition&&!offset&&size==64);if(read_fail)return ESP_FAIL;memcpy(data,image,size);return ESP_OK;}
esp_err_t esp_partition_mmap(const esp_partition_t *p,size_t offset,size_t size,unsigned flags,const void **data,esp_partition_mmap_handle_t *handle){assert(p==&partition&&!offset&&size==sizeof(image)&&flags==0);if(map_fail)return ESP_FAIL;*data=image;*handle=123;++mapped;return ESP_OK;}
void esp_partition_munmap(esp_partition_mmap_handle_t handle){assert(handle==123);++unmapped;}
int psa_crypto_init(void){return PSA_SUCCESS;}
int psa_hash_compute(unsigned alg,const uint8_t *input,size_t length,uint8_t *out,size_t capacity,size_t *out_length){
    assert(alg==PSA_ALG_SHA_256&&input==image+64&&length==sizeof(image)-64&&capacity==32);memcpy(out,image+32,32);if(bad_hash)out[0]^=1;*out_length=32;++hashes;return PSA_SUCCESS;}
static void u32(unsigned at,uint32_t n){for(unsigned i=0;i<4;++i)image[at+i]=(uint8_t)(n>>(i*8));}
static void reset(void){
    saved=(chime_ldac_fixture_t){0};missing=bad_hash=read_fail=map_fail=false;partition.address=0x400000;partition.size=0x800000;
    memset(image,0,sizeof(image));memcpy(image,"LDACRP01",8);u32(8,48000);u32(12,1488000);u32(16,3875);u32(20,sizeof(image)-64);u32(24,1);
    const char *pin=CONFIG_CHIME_LDAC_REPLAY_FLASH_SHA256;
    for(unsigned i=0;i<32;++i)image[32+i]=(uint8_t)(hex(pin[i*2])*16+hex(pin[i*2+1]));
    for(unsigned p=0;p<3875;++p){size_t at=64+p*15;image[at]=12;image[at+2]=3;
        for(unsigned i=0;i<3;++i){image[at+3+i*4]=0xaa;image[at+4+i*4]=0x30;}}
}
int main(void){
    chime_ldac_fixture_t f={0};reset();assert(chime_ldac_fixture_prepare(NULL)==ESP_ERR_INVALID_ARG);
    missing=true;assert(chime_ldac_fixture_prepare(&f)==ESP_ERR_NOT_FOUND);reset();
    partition.address+=0x1000;assert(chime_ldac_fixture_prepare(&f)==ESP_ERR_NOT_FOUND);reset();
    read_fail=true;assert(chime_ldac_fixture_prepare(&f)==ESP_FAIL);reset();
    image[0]^=1;assert(chime_ldac_fixture_prepare(&f)==ESP_ERR_INVALID_ARG);reset();
    u32(8,44100);assert(chime_ldac_fixture_prepare(&f)==ESP_ERR_INVALID_ARG);reset();
    image[32]^=1;assert(chime_ldac_fixture_prepare(&f)==ESP_ERR_INVALID_ARG);reset();
    u32(20,0x800000);assert(chime_ldac_fixture_prepare(&f)==ESP_ERR_INVALID_ARG);reset();
    map_fail=true;assert(chime_ldac_fixture_prepare(&f)==ESP_FAIL);reset();
    unsigned before=unmapped;bad_hash=true;assert(chime_ldac_fixture_prepare(&f)==ESP_ERR_INVALID_ARG&&unmapped==before+1);reset();
    image[64+2]=2;before=unmapped;assert(chime_ldac_fixture_prepare(&f)==ESP_ERR_INVALID_ARG&&unmapped==before+1);reset();
    image[64+4]=0x10;assert(chime_ldac_fixture_prepare(&f)==ESP_ERR_INVALID_ARG);reset(); /* wrong frame rate */
    image[64]=13;assert(chime_ldac_fixture_prepare(&f)==ESP_ERR_INVALID_ARG);reset(); /* unaccounted tail */
    assert(chime_ldac_fixture_prepare(&f)==ESP_OK&&f.payload==image+64&&f.length==sizeof(image)-64&&f.cycle_frames==1488000&&f.packets==3875);
    unsigned old=hashes;assert(chime_ldac_fixture_prepare(&f)==ESP_OK&&hashes==old&&mapped==unmapped+1);
    puts("PASS fixture adapter: explicit partition bounds/pin, malformed headers/frames, failed hash/map/read cleanup, immutable cached mapping; mocked crypto, no flash write");return 0;
}
