idf_component_register(
    SRCS
        "app_main.c" "chime_led.c" "chime_indicator.c" "chime_input.c" "chime_controls.c"
        # Single DSP owner, SS+ control, local image and read-only diagnostics.
        "bench_bridge.c" "bench_dsp.c" "chime_dsp_boot.c" "bench_dsp_status.c"
        # Shared protected PCM renderer, USB / Bluetooth codecs and manual sine.
        "bench_audio.c" "bench_tone.c" "bench_sound.c" "chime_usb.c" "chime_pcm.c"
        "chime_source.c" "chime_bluetooth.c" "chime_bt_pcm.c" "chime_bt_delay.c" "chime_bt_decoder.c"
        # Network provisioning and web control/status.
        "chime_network.c" "chime_provision.c" "chime_setup_guard.c"
        "bench_web.c" "bench_status.c"
    INCLUDE_DIRS "include"
    REQUIRES board_support freertos log esp_timer esp_system esp_app_format
    PRIV_REQUIRES esp_driver_ledc bt chime_codec_reference chime_lhdc chime_ldac chime_freeaptx esp_audio_codec esp_tinyusb esp_driver_i2s sigma_tcp_bridge dsp_control esp_wifi esp_event esp_netif nvs_flash lwip esp_hw_support esp_rom esp_http_server mdns cjson)
target_add_binary_data(${COMPONENT_LIB} "bench_index.html" TEXT)

# esp_tinyusb has no audio Kconfig knobs. Inject only our UAC class options;
# retain its upstream tusb_config.h for target, DCD, OS and memory settings.
foreach(usb_component espressif__tinyusb espressif__esp_tinyusb main)
    idf_component_get_property(usb_lib ${usb_component} COMPONENT_LIB)
    target_compile_options(${usb_lib} PRIVATE "-include${CMAKE_CURRENT_SOURCE_DIR}/include/chime_tusb_audio_config.h")
endforeach()

# TinyUSB 0.21.0~2: first FIFO feedback must not be zero, and ZLP OUT must
# rearm reception. Generate from a hash-pinned upstream file; no managed edits.
idf_component_get_property(tusb_dir espressif__tinyusb COMPONENT_DIR)
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
set(audio_upstream "${tusb_dir}/src/class/audio/audio_device.c")
set(audio_fixed "${CMAKE_CURRENT_BINARY_DIR}/chime_audio_device.c")
set(audio_patch "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/patch-tinyusb-audio.py")
execute_process(COMMAND "${PYTHON}" "${audio_patch}" "${audio_upstream}" "${audio_fixed}"
    RESULT_VARIABLE audio_patch_check)
if(NOT audio_patch_check EQUAL 0)
    message(FATAL_ERROR "TinyUSB audio startup patch needs review/rebase")
endif()
get_target_property(tusb_sources ${tusb_lib} SOURCES)
list(REMOVE_ITEM tusb_sources "${audio_upstream}" "src/class/audio/audio_device.c")
set_property(TARGET ${tusb_lib} PROPERTY SOURCES ${tusb_sources})
target_sources(${tusb_lib} PRIVATE "${audio_fixed}")
target_include_directories(${tusb_lib} PRIVATE "${tusb_dir}/src/class/audio")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${audio_upstream}" "${audio_patch}")

# Default remains strict against the working SS project. An explicitly chosen
# Git snapshot can be used while editing SS; it MUST match every manifest hash.
set(CHIME_FROZEN_DSP_SOURCE_REF "" CACHE STRING "Explicit manifest-matching Git source snapshot for built-in DSP (empty checks working sources)")
set(dsp_source_args)
if(NOT CHIME_FROZEN_DSP_SOURCE_REF STREQUAL "")
    list(APPEND dsp_source_args --source-ref "${CHIME_FROZEN_DSP_SOURCE_REF}")
    message(STATUS "Built-in DSP source snapshot: ${CHIME_FROZEN_DSP_SOURCE_REF}; live SS files are not packaged")
endif()
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/freeze-dsp-image.py" ${dsp_source_args}
    RESULT_VARIABLE dsp_image_check)
if(NOT dsp_image_check EQUAL 0)
    message(FATAL_ERROR "Frozen DSP source/image mismatch; review source before updating.")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/ADAU1451.ssprj"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/esp32-bindings.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/manifest.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/chime_dsp_image.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/chime_dsp_parameters.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/freeze-dsp-image.py")

file(GLOB dsp_compiler_sources "${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/ADAU1451/ADAU145xSchematic_0/*.dat")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${dsp_compiler_sources}
    "${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/ADAU1451/ADAU145xSchematic_0/Program.bin"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/ADAU1451/ADAU145xSchematic_0/compiler_output.log")

# Bindings may select another repository-local compiler folder/project. Track
# every pinned input, not only the original vendor folder, for reconfigure.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/generated/manifest.json" dsp_manifest_json)
string(JSON dsp_input_count LENGTH "${dsp_manifest_json}" source_sha256)
math(EXPR dsp_input_last "${dsp_input_count} - 1")
foreach(dsp_input_index RANGE 0 ${dsp_input_last})
    string(JSON dsp_input MEMBER "${dsp_manifest_json}" source_sha256 ${dsp_input_index})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../${dsp_input}")
endforeach()

# Vendor codec support is isolated to this build; never edit IDF or managed code.
if(CONFIG_CHIME_BT_VENDOR_CODECS)
    idf_component_get_property(bt_dir bt COMPONENT_DIR)
    idf_component_get_property(bt_lib bt COMPONENT_LIB)
    set(vendor_dir "${CMAKE_CURRENT_BINARY_DIR}/bt_vendor")
    set(vendor_patch "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/patch-bluetooth-vendor.py")
    execute_process(COMMAND "${PYTHON}" "${vendor_patch}" "${bt_dir}" "${vendor_dir}"
        RESULT_VARIABLE vendor_check)
    if(NOT vendor_check EQUAL 0)
        message(FATAL_ERROR "Bluetooth vendor SDK pin mismatch; review/rebase or disable CHIME_BT_VENDOR_CODECS")
    endif()
    foreach(source btc_av.c bta_av_co.c btc_a2dp_sink_ext_coedc.c)
        set(original "${bt_dir}/host/bluedroid/btc/profile/std/a2dp/${source}")
        target_sources(${bt_lib} PRIVATE "${vendor_dir}/${source}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    endforeach()
    # Preserve target_sources additions while replacing upstream entries.
    get_target_property(bt_all_sources ${bt_lib} SOURCES)
    foreach(source btc_av.c bta_av_co.c btc_a2dp_sink_ext_coedc.c)
        list(REMOVE_ITEM bt_all_sources "${bt_dir}/host/bluedroid/btc/profile/std/a2dp/${source}" "host/bluedroid/btc/profile/std/a2dp/${source}")
    endforeach()
    set_property(TARGET ${bt_lib} PROPERTY SOURCES ${bt_all_sources})
    get_target_property(bt_all_sources ${bt_lib} SOURCES)
    set(avdt_original "${bt_dir}/host/bluedroid/stack/avdt/avdt_scb_act.c")
    list(REMOVE_ITEM bt_all_sources "${avdt_original}" "host/bluedroid/stack/avdt/avdt_scb_act.c")
    set_property(TARGET ${bt_lib} PROPERTY SOURCES ${bt_all_sources})
    target_sources(${bt_lib} PRIVATE "${vendor_dir}/avdt_scb_act.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${avdt_original}")
    target_sources(${bt_lib} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/chime_bt_vendor.c")
    # PUBLIC shadow header ensures the enlarged MCC ABI matches all consumers.
    target_include_directories(${bt_lib} BEFORE PUBLIC "${vendor_dir}")
    target_include_directories(${bt_lib} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    foreach(lib ${bt_lib} ${COMPONENT_LIB})
        target_compile_options(${lib} PRIVATE "-include${CMAKE_CURRENT_SOURCE_DIR}/include/chime_bt_stack_config.h")
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${vendor_patch}" "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/bluetooth-sdk-pins.json"
        "${bt_dir}/host/bluedroid/api/include/api/esp_a2dp_api.h")
endif()

# Recovery codecs use the SAME delay-report range fix, without vendor hooks.
if(NOT CONFIG_CHIME_BT_VENDOR_CODECS)
    idf_component_get_property(bt_dir bt COMPONENT_DIR)
    idf_component_get_property(bt_lib bt COMPONENT_LIB)
    set(delay_dir "${CMAKE_CURRENT_BINARY_DIR}/bt_delay")
    set(delay_patch "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/patch-bluetooth-vendor.py")
    execute_process(COMMAND "${PYTHON}" "${delay_patch}" --delay-only "${bt_dir}" "${delay_dir}" RESULT_VARIABLE delay_check)
    if(NOT delay_check EQUAL 0)
        message(FATAL_ERROR "Bluetooth delay SDK pin mismatch; review/rebase")
    endif()
    get_target_property(bt_all_sources ${bt_lib} SOURCES)
    list(REMOVE_ITEM bt_all_sources "${bt_dir}/host/bluedroid/btc/profile/std/a2dp/btc_av.c" "host/bluedroid/btc/profile/std/a2dp/btc_av.c")
    set_property(TARGET ${bt_lib} PROPERTY SOURCES ${bt_all_sources})
    target_sources(${bt_lib} PRIVATE "${delay_dir}/btc_av.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${delay_patch}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/bluetooth-sdk-pins.json"
        "${bt_dir}/host/bluedroid/btc/profile/std/a2dp/btc_av.c")
endif()
