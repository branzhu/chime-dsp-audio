# Libraries from the user-selected S31 fork; never import its application/stack.
if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(PRIV_REQUIRES log esp_common esp_timer heap)
    return()
endif()
if(NOT CONFIG_BT_A2DP_USE_EXTERNAL_CODEC)
    idf_component_register(INCLUDE_DIRS ".")
    return()
endif()
set(port "${CMAKE_CURRENT_BINARY_DIR}/s31-port")
file(GLOB_RECURSE pinned CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/vendor/s31/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${pinned}
    "${CMAKE_CURRENT_LIST_DIR}/prepare.py" "${CMAKE_CURRENT_LIST_DIR}/UPSTREAM.json")
idf_build_get_property(python PYTHON)
execute_process(COMMAND "${python}" "${CMAKE_CURRENT_LIST_DIR}/prepare.py" --output "${port}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Pinned S31 codec port generation failed")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")
idf_component_register(SRCS "chime_s31_codec.c" "chime_reference_codec.c" "chime_ldac_memory.c" ${s31_sources}
    INCLUDE_DIRS "." PRIV_INCLUDE_DIRS ${s31_includes}
    PRIV_REQUIRES log esp_common esp_timer heap)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -std=gnu11 -fwrapv -fstack-usage)
target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_CONFIG_H=1 ESP32=1)
# Only imported sources receive their upstream warning policy.
set_source_files_properties(${s31_sources} PROPERTIES COMPILE_OPTIONS "-Wno-error;-Wno-unused-function;-Wno-unused-variable;-Wno-maybe-uninitialized")
