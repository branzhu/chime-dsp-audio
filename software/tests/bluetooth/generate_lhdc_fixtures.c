#include "lhdcv5_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
int main(int argc,char **argv){
 assert(argc==2);unsigned rates[]={44100,48000};
 for(unsigned r=0;r<2;r++)for(unsigned br=3;br<=8;br++){
  unsigned rate=rates[r],mem=0,spf=0;
  assert(!lhdcv5_encoder_get_mem_req(1,&mem));lhdcv5_para_t *e=calloc(1,mem);assert(e);
  assert(!lhdcv5_encoder_new(1,e,mem));assert(!lhdcv5_encoder_init(e,rate,16,br,50,1000,10));
  assert(!lhdcv5_encoder_get_frame_len(e,&spf)&&spf==240);
  char path[512];snprintf(path,sizeof(path),"%s/%u-q%u.lhdc",argv[1],rate,br);FILE *f=fopen(path,"wb");assert(f);
  for(unsigned frame=0;frame<80;frame++){
   int16_t pcm[480];for(unsigned i=0;i<240;i++){
    double t=(double)(frame*240+i)/rate;
    pcm[2*i]=(int16_t)(6000*sin(2*3.141592653589793*997*t));
    pcm[2*i+1]=(int16_t)(4000*sin(2*3.141592653589793*2003*t));
   }
   uint8_t out[8192];uint32_t n=0,frames=0;
   assert(!lhdcv5_encoder_encode(e,(uint8_t *)pcm,sizeof(pcm),out,sizeof(out),&n,&frames));
   assert(fwrite(out,1,n,f)==n);
  }
  fclose(f);lhdcv5_encoder_free(e);free(e);
 }
 return 0;
}
