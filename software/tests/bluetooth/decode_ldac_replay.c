/* Offline real decoder verification of continuous replay records. Host-only;
 * output is not device/microphone evidence and is never played here. */
#include "chime_ldac.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
    const char *input=argc==4?argv[1]:"software/tests/bluetooth/fixtures/ldac-adaptive/48000-abr.packets";
    unsigned rate=argc==4?(unsigned)atoi(argv[2]):48000;
    const char *output=argc==4?argv[3]:"tmp/ldac-pipeline-tests/replay-decoded.s16le";
    assert(rate==44100||rate==48000);FILE *f=fopen(input,"rb"),*out=fopen(output,"wb");assert(f&&out);
    void *decoder=chime_ldac_open(rate,2);assert(decoder);uint64_t frames=0;unsigned packets=0,peak=0;
    for(;;){
        uint8_t head[3],raw[2048];size_t n=fread(head,1,3,f);if(!n)break;assert(n==3);
        unsigned bytes=(unsigned)head[0]|((unsigned)head[1]<<8),nf=head[2];assert(bytes<=sizeof(raw)&&nf>=1&&nf<=15);
        assert(fread(raw,1,bytes,f)==bytes);size_t offset=0;
        for(unsigned i=0;i<nf;++i){
            size_t len=chime_ldac_frame_bytes(raw+offset,bytes-offset,rate);assert(len&&len<=bytes-offset);
            int16_t pcm[256];assert(chime_ldac_decode(decoder,raw+offset,len,(uint8_t *)pcm,sizeof(pcm))==sizeof(pcm));
            for(unsigned j=0;j<256;++j){unsigned v=pcm[j]<0?(unsigned)-(int)pcm[j]:(unsigned)pcm[j];if(v>peak)peak=v;}
            assert(fwrite(pcm,1,sizeof(pcm),out)==sizeof(pcm));offset+=len;frames+=128;
        }
        assert(offset==bytes);++packets;
    }
    assert(feof(f)&&!ferror(f)&&frames);chime_ldac_close(decoder);assert(!fclose(f)&&!fclose(out));
    printf("PASS host-only actual LDAC probe decode: packets=%u frames=%llu peak=%u; no device/RF acceptance\n",packets,(unsigned long long)frames,peak);return 0;
}
