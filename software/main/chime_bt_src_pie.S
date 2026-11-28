/* Original S31 PIE signed-16 dot products. No upstream implementation copied.
 * 192 * 32768^2 = 206158430208 < 2^39: every partial XACC sum fits,
 * irrespective of coefficient/input signs. All accesses are 16-byte aligned.
 * Do NOT use SRS: retain every accumulator bit and sign-extend to int64_t.
 * Q0/Q1/XACC are caller-clobbered, as in ESP-DSP's PIE kernels; IDF's
 * FreeRTOS PIE coprocessor port preserves these across task preemption.
 * Neither CFG nor SAR nor hardware-loop state is modified here. */
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32S31
    .section .iram1,"ax",@progbits
    .align 4
    .global chime_bt_src_dot_pie
    .type chime_bt_src_dot_pie,@function
/* a0 = aligned PCM planes L/R, a1 = aligned coefficient planes hi/lo,
 * a2 = aligned int64 parts[4], in order hi-L, hi-R, lo-L, lo-R. */
    .macro dot pcm_offset, coeff_offset
    addi a3, a0, \pcm_offset
    addi a4, a1, \coeff_offset
    li a5, 24
    esp.zero.xacc
.Ldot\@:
    esp.vld.128.ip q0, a3, 16
    esp.vld.128.ip q1, a4, 16
    esp.vmulas.s16.xacc q0, q1
    addi a5, a5, -1
    bnez a5, .Ldot\@
    esp.st.s.xacc.ip a2, 8
    .endm
chime_bt_src_dot_pie:
    dot 0, 0
    dot 384, 0
    dot 0, 384
    dot 384, 384
    ret
    .size chime_bt_src_dot_pie, .-chime_bt_src_dot_pie

    .align 4
    .global chime_bt_src_dot_pie_planar
    .type chime_bt_src_dot_pie_planar,@function
/* a0/a1 aligned L/R windows, a2 aligned hi/lo coefficient planes,
 * a3 parts[4]. Same exact signed-40-bit MAC as the validated raw primitive. */
    .macro planar_dot source, coeff_offset
    mv a4, \source
    addi a5, a2, \coeff_offset
    li a6, 24
    esp.zero.xacc
.Lplanar\@:
    esp.vld.128.ip q0, a4, 16
    esp.vld.128.ip q1, a5, 16
    esp.vmulas.s16.xacc q0, q1
    addi a6, a6, -1
    bnez a6, .Lplanar\@
    esp.st.s.xacc.ip a3, 8
    .endm
chime_bt_src_dot_pie_planar:
    planar_dot a0, 0
    planar_dot a1, 0
    planar_dot a0, 384
    planar_dot a1, 384
    ret
    .size chime_bt_src_dot_pie_planar, .-chime_bt_src_dot_pie_planar
#endif
