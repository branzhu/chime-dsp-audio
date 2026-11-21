# Upstream sources unmodified, LGPL-2.1-or-later; see COPYING and provenance.
if(CONFIG_CHIME_BT_VENDOR_CODECS)
    idf_component_register(SRCS "freeaptx.c" INCLUDE_DIRS ".")
    target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
else()
    idf_component_register()
endif()
