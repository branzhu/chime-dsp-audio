set(e "${port}/external")
file(GLOB sbc_sources "${e}/sbc/decoder/srce/*.c")
# Match the S31 fork's explicit AAC-LC list, never compile the SBR sources.
set(aac_sources)
foreach(name aacdec aactabs bitstream buffers dct4 decelmnt dequant fft filefmt huffman hufftabs imdct noiseless pns stproc tns trigtabs)
    list(APPEND aac_sources "${e}/libaac-lc/libhelix_aac/${name}.c")
endforeach()
set(s31_sources ${sbc_sources} ${aac_sources} "${port}/stack/a2dp/latm_parser.c")
set(s31_includes "${port}/compat" "${e}/sbc/decoder/include"
    "${e}/libaac-lc/libhelix_aac" "${port}/stack/a2dp")
if(CONFIG_CHIME_BT_VENDOR_CODECS)
    file(GLOB opus_sources "${e}/opus/src/*.c" "${e}/opus/celt/*.c" "${e}/opus/silk/*.c" "${e}/opus/silk/fixed/*.c")
    list(FILTER opus_sources EXCLUDE REGEX "/(opus_demo|opus_compare|repacketizer_demo|opus_custom_demo)\\.c$")
    list(APPEND s31_sources ${opus_sources} "${e}/libfreeaptx/main/freeaptx.c"
        "${e}/libldac-dec/src/ldacBT.c" "${e}/libldac-dec/src/ldaclib.c")
    list(APPEND s31_includes "${e}/libfreeaptx/main" "${e}/libldac-dec/inc" "${e}/libldac-dec/src"
        "${e}/opus_config" "${e}/opus/include" "${e}/opus" "${e}/opus/celt"
        "${e}/opus/silk" "${e}/opus/silk/fixed" "${e}/opus/silk/float" "${e}/opus/dnn")
endif()
if(CONFIG_CHIME_BT_LHDC_V5)
    file(GLOB lhdc_sources "${e}/lhdc-v5/*.c")
    list(APPEND s31_sources ${lhdc_sources})
    list(APPEND s31_includes "${e}/lhdc-v5")
endif()
