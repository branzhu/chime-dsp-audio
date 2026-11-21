# Codec-only import; never link the reference transport, worker or renderer.
if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(PRIV_REQUIRES log esp_common esp_timer heap)
    return()
endif()
if(NOT CONFIG_CHIME_BT_VENDOR_CODECS)
    idf_component_register()
    return()
endif()
set(v "${CMAKE_CURRENT_LIST_DIR}/vendor")
set(n "${v}/native_bt/codecs")
set(ldac "${v}/esp-idf/components/bt/host/bluedroid/external/libldac-dec")
set(dec "${n}/lhdcv5/source_snapshot/decoder")
set(port "${CMAKE_CURRENT_BINARY_DIR}/port")
file(GLOB_RECURSE pinned CONFIGURE_DEPENDS "${v}/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${pinned}
    "${CMAKE_CURRENT_LIST_DIR}/prepare.py" "${CMAKE_CURRENT_LIST_DIR}/UPSTREAM.json")
idf_build_get_property(python PYTHON)
execute_process(COMMAND "${python}" "${CMAKE_CURRENT_LIST_DIR}/prepare.py" --output "${port}"
    RESULT_VARIABLE result OUTPUT_QUIET)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Pinned reference codec port generation failed")
endif()
set(sources "chime_reference_codec.c" "${port}/dbt_codec_lifecycle.c"
    "${port}/dbt_ldac.c" "${n}/ldac/dbt_ldac_decoder.c" "${n}/ldac/dbt_ldac_bt.c" "${n}/ldac/dbt_ldac_lib.c")
if(CONFIG_CHIME_BT_LHDC_V5)
    list(APPEND sources "${port}/dbt_lhdcv5_codec.c"
        "${port}/lhdc/lhdc_dec.c" "${port}/lhdc/lhdc_imdct.c" "${port}/lhdc/lhdc_entropy_dec.c" "${port}/lhdc/lhdc_sns_synth.c"
        "${dec}/lhdc_bit_reader.c" "${dec}/lhdc_diag_config.c" "${dec}/lhdc_tables.c")
endif()
idf_component_register(SRCS ${sources} INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "${n}/common" "${v}/native_bt/include" "${n}/ldac" "${n}/lhdcv5"
        "${port}/ldac" "${port}/lhdc" "${ldac}/inc" "${ldac}/src" "${dec}"
    PRIV_REQUIRES log esp_common esp_timer heap)
# RISC-V S31: do NOT copy the upstream Xtensa -mfpu=auto setting.
# Match the native codec port's guarded arithmetic, not the old fast-math profile.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -std=gnu11 -fwrapv)
if(CONFIG_CHIME_BT_LHDC_V5)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DBT_LHDC_QUALITY=1)
endif()
