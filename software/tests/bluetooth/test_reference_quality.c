/* The new S31 source has no retired native-port quality collector. */
#include "chime_reference_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    chime_reference_quality_t out,saved;memset(&out,0xa5,sizeof(out));saved=out;
    assert(!chime_reference_quality_snapshot(NULL));
    for(int lhdc=0;lhdc<=1;lhdc++){
        assert(!chime_reference_quality_snapshot(&out)&&!memcmp(&out,&saved,sizeof(out)));
        void *d=chime_reference_open(lhdc,48000,2);assert(d);
        assert(!chime_reference_quality_snapshot(&out)&&!memcmp(&out,&saved,sizeof(out)));
        chime_reference_close(d);
        assert(!chime_reference_quality_snapshot(&out)&&!memcmp(&out,&saved,sizeof(out)));
    }
    puts("PASS S31 quality facade: unavailable/NULL/output-unchanged before, during and after real decoder lifecycle");return 0;
}
