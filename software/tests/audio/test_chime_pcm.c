#include "chime_pcm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    const uint8_t pcm[]={0,0,255,127,0,128,255,255};int32_t out[4];
    assert(chime_pcm_gain(0)==32768&&chime_pcm_gain(1000)==32768);
    assert(chime_pcm_gain(-60*256)>=32&&chime_pcm_gain(-99999)==chime_pcm_gain(-60*256));
    chime_pcm_convert(pcm,out,2,32768);
    assert(out[0]==0&&out[1]==2147418112&&out[2]==INT32_MIN&&out[3]==-65536);
    for(unsigned i=0;i<4;++i)assert(((uint32_t)out[i]&255U)==0);
    chime_pcm_convert(pcm,out,2,0);for(unsigned i=0;i<4;++i)assert(out[i]==0);
    chime_pcm_convert(pcm,out,2,chime_pcm_gain(-20*256));assert(out[1]>210000000&&out[1]<220000000);
    chime_pcm_levels_t levels={0};
    chime_pcm_convert(pcm,out,2,32768);chime_pcm_measure(&levels,pcm,out,2);
    assert(levels.input_peak[0]==32768&&levels.input_peak[1]==32767);
    assert(levels.output_peak[0]==8388608&&levels.output_peak[1]==8388352);
    assert(levels.input_nonzero[0]==1&&levels.input_nonzero[1]==2);
    assert(levels.output_nonzero[0]==1&&levels.output_nonzero[1]==2);
    chime_pcm_measure(&levels,pcm,out,2);assert(levels.input_nonzero[1]==4);
    /* Distinguish nonzero host data turned into silence from host silence. */
    memset(&levels,0,sizeof(levels));chime_pcm_convert(pcm,out,2,0);
    chime_pcm_measure(&levels,pcm,out,2);
    assert(levels.input_peak[0]==32768&&levels.output_peak[0]==0&&levels.output_nonzero[1]==0);
    const uint8_t silence[8]={0};memset(&levels,0,sizeof(levels));
    chime_pcm_convert(silence,out,2,32768);chime_pcm_measure(&levels,silence,out,2);
    assert(levels.input_peak[0]==0&&levels.input_peak[1]==0&&levels.output_peak[0]==0&&levels.output_peak[1]==0);
    /* Safe magnitude at INT32_MIN, and no sample modification. */
    out[0]=INT32_MIN;chime_pcm_measure(&levels,silence,out,1);
    assert(out[0]==INT32_MIN&&levels.output_peak[0]==8388608);
    /* Exhaustive signed PCM16 range: unity is exact, and every supported
     * master dB step stays bounded/aligned, with independent opposite channels. */
    for(int sample=-32768;sample<=32767;++sample){
        uint16_t a=(uint16_t)sample,b=(uint16_t)(-sample-1);
        const uint8_t pair[]={(uint8_t)a,(uint8_t)(a>>8),(uint8_t)b,(uint8_t)(b>>8)};
        chime_pcm_convert(pair,out,1,32768);
        assert(out[0]==(int32_t)((int64_t)sample*65536));
        assert(out[1]==(int32_t)((int64_t)(-sample-1)*65536));
        chime_pcm_convert(pair,out,1,40000);
        assert(out[0]==(int32_t)((int64_t)sample*65536));
        for(int db=-60;db<=0;++db){
            int32_t gain=chime_pcm_gain(db*256);
            chime_pcm_convert(pair,out,1,gain);
            for(unsigned ch=0;ch<2;++ch){
                int v=ch==0?sample:-sample-1;
                int64_t expected=((int64_t)v*256*gain/32768)*256;
                assert((int64_t)out[ch]==expected&&((uint32_t)out[ch]&255U)==0);
                assert(expected>=INT32_MIN&&expected<=2147483392LL);
                assert(v<0?out[ch]<=0:out[ch]>=0);
            }
        }
        chime_pcm_convert(pair,out,1,-1);assert(out[0]==0&&out[1]==0);
    }
    assert(chime_pcm_gain_q30(0)==1073741824);
    assert(chime_pcm_gain_q30(256)==1073741824);
    assert(chime_pcm_gain_q30(-60*256)==1073742);
    for(int db=-60;db<0;++db)assert(chime_pcm_gain_q30(db*256)<chime_pcm_gain_q30((db+1)*256));
    puts("PASS Q2.30 source gains: unity, clamps, -60dB precision and monotonicity");
    puts("PASS exhaustive PCM16 range: exact unity, all 61 master dB steps, clamped gain, signed endpoints, channel isolation, 24bit alignment and no overflow");
    puts("PASS PCM meter: independent L/R input16 and exact output24 peaks/nonzero counts, silence, zero gain, accumulation, INT32_MIN");
    puts("PASS USB PCM16 LE stereo sign/channel conversion, 24-in-32 alignment, unity full scale (no fixed ceiling), master attenuation bounds and mute gain");
}
