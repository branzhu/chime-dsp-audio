/* Actual S31 frame frontends: preserve byte-buffer capacity/alignment contract. */
#include "chime_ldac.h"
#include "chime_lhdc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void wrapper(int lhdc){
    uint8_t raw[2048];FILE *f=fopen(lhdc?"software/tests/bluetooth/fixtures/lhdc/48000-q3.lhdc":"software/tests/bluetooth/fixtures/ldac/48000-2.ldac","rb");
    assert(f);size_t n=fread(raw,1,sizeof(raw),f);fclose(f);
    n=lhdc?chime_lhdc_frame_bytes(raw,n):chime_ldac_frame_bytes(raw,n,48000);assert(n>3);
    int16_t expected[480];size_t bytes=lhdc?960:512;
    for(unsigned pass=0;pass<3;pass++){
        void *d=lhdc?chime_lhdc_open(48000,2):chime_ldac_open(48000,2);assert(d);
        assert(!(lhdc?chime_lhdc_open(48000,2):chime_ldac_open(48000,2)));
        union {int16_t align;uint8_t data[964];} out;memset(out.data,0xa5,sizeof(out.data));
        assert(!(lhdc?chime_lhdc_decode(d,raw,n,out.data+1,bytes-1):chime_ldac_decode(d,raw,n,out.data+1,bytes-1)));
        for(unsigned i=0;i<sizeof(out.data);i++)assert(out.data[i]==0xa5);
        assert((lhdc?chime_lhdc_decode(d,raw,n,out.data+1,bytes):chime_ldac_decode(d,raw,n,out.data+1,bytes))==bytes);
        assert(out.data[0]==0xa5&&out.data[bytes+1]==0xa5);
        if(pass)assert(!memcmp(expected,out.data+1,bytes));else memcpy(expected,out.data+1,bytes);
        if(lhdc)chime_lhdc_close(d);else chime_ldac_close(d);
    }
}
int main(void){wrapper(0);wrapper(1);puts("PASS S31 frame frontend: singleton, short-capacity no-write/retry, unaligned PCM, reopen identity");return 0;}
